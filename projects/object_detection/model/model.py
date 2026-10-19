# Copyright 2026 Arm Limited and/or its affiliates.
# SPDX-License-Identifier: Apache-2.0
"""Import original YOLO Fastest int8 TFLite as PyTorch Q/DQ, without calibration."""
import hashlib
import importlib
from pathlib import Path
import numpy as np
import torch
from torch import nn
from torch.nn import functional as F
import torchao.quantization.pt2e.quantize_pt2e  # register Q/DQ

INPUT_SHAPE = (1, 192, 192, 1)
OUTPUT_SHAPES = ((1, 6, 6, 18), (1, 12, 12, 18))
SOURCE = Path(__file__).with_name("yolo-fastest_192_face_v4.tflite")
SOURCE_SHA256 = "e94bcdb011784bead70ab0c0e9d2dae1a9ea5f103b43e1e6fac3019302cf71ab"
QD = torch.ops.quantized_decomposed


def qparams(tensor):
    qp = tensor.Quantization()
    assert qp.ScaleLength() == 1 and qp.ZeroPointLength() == 1
    return float(qp.Scale(0)), int(qp.ZeroPoint(0))


def dq(x, params):
    return QD.dequantize_per_tensor.default(x, *params, -128, 127, torch.int8)


def q(x, params):
    return QD.quantize_per_tensor.default(x, *params, -128, 127, torch.int8)


def activation(x, code):
    if code == 1:
        return F.relu(x)
    if code == 3:
        return F.hardtanh(x, 0., 6.)
    assert code == 0, f"Unsupported fused activation {code}"
    return x


class Conv(nn.Module):
    def __init__(self, weight, bias, weight_scales, bias_scales, inp, out,
                 stride, padding, groups, act):
        super().__init__()
        for name, value in (("weight", weight), ("bias", bias),
                            ("weight_scales", weight_scales), ("bias_scales", bias_scales)):
            self.register_buffer(name, torch.from_numpy(value.copy()))
        self.register_buffer("zeros", torch.zeros(len(weight_scales), dtype=torch.int64))
        self.inp, self.out, self.stride, self.padding, self.groups, self.act = inp, out, stride, padding, groups, act

    def forward(self, x):
        x = dq(x, self.inp)
        w = QD.dequantize_per_channel.default(self.weight, self.weight_scales, self.zeros, 0, -127, 127, torch.int8)
        b = QD.dequantize_per_channel.default(self.bias, self.bias_scales, self.zeros, 0, -(2**31), 2**31-1, torch.int32)
        if self.padding[0] == self.padding[1] and self.padding[2] == self.padding[3]:
            padding = (self.padding[2], self.padding[0])
        else:
            x = dq(q(F.pad(x, self.padding), self.inp), self.inp)
            padding = (0, 0)
        x = F.conv2d(x, w, b, self.stride, padding, groups=self.groups)
        if self.act and self.out[1] != -128:
            # Source op100 has ReLU6 with zero point -7. The Arm backend only
            # fuses conv+ReLU6 when zp==qmin. Make the original quantized output
            # domain explicit so both conv and clamp have quantized boundaries.
            # Q(clamp(x,0,6)) == Q(clamp(DQ(Q(x)),0,6)) for these qparams.
            x = dq(q(x, self.out), self.out)
        return q(activation(x, self.act), self.out)


class Operation(nn.Module):
    def __init__(self, kind, params, output, options):
        super().__init__()
        self.kind, self.params, self.output, self.options = kind, params, output, options

    def forward(self, *xs):
        values = [dq(x, p) for x, p in zip(xs, self.params)]
        if self.kind == "PAD":
            value = F.pad(values[0], self.options)
        elif self.kind == "ADD":
            value = activation(values[0] + values[1], self.options)
        elif self.kind == "CONCATENATION":
            value = torch.cat(values, dim=1)
        elif self.kind == "MAX_POOL_2D":
            kernel, stride, padding = self.options
            value = F.max_pool2d(values[0], kernel, stride, padding)
        elif self.kind == "RESIZE_NEAREST_NEIGHBOR":
            value = F.interpolate(values[0], size=self.options, mode="nearest")
        else:
            raise AssertionError(self.kind)
        return q(value, self.output)


class YoloFastest(nn.Module):
    def __init__(self, source=SOURCE):
        super().__init__()
        from ethosu.vela.tflite.Model import Model
        from ethosu.vela.tflite.BuiltinOperator import BuiltinOperator
        from ethosu.vela.tflite.TensorType import TensorType
        data = Path(source).read_bytes()
        if hashlib.sha256(data).hexdigest() != SOURCE_SHA256:
            raise ValueError("Unexpected original YOLO Fastest SHA256")
        model = Model.GetRootAsModel(data, 0)
        graph = model.Subgraphs(0)
        names = {v: k for k, v in vars(BuiltinOperator).items() if isinstance(v, int)}

        def constant(index):
            tensor = graph.Tensors(int(index))
            dtype = {TensorType.INT8: np.int8, TensorType.INT32: np.int32}[tensor.Type()]
            return np.frombuffer(model.Buffers(tensor.Buffer()).DataAsNumpy().tobytes(), dtype=dtype).reshape(tuple(tensor.ShapeAsNumpy())).copy()

        def options(op, name):
            cls = getattr(importlib.import_module("ethosu.vela.tflite." + name), name)
            value, table = cls(), op.BuiltinOptions()
            value.Init(table.Bytes, table.Pos)
            return value

        self.input_index = int(graph.Inputs(0))
        self.output_indices = tuple(int(i) for i in graph.OutputsAsNumpy())
        self.input_qparams = qparams(graph.Tensors(self.input_index))
        self.output_qparams = tuple(qparams(graph.Tensors(i)) for i in self.output_indices)
        self.routes, layers = [], []
        for i in range(graph.OperatorsLength()):
            op = graph.Operators(i)
            code = model.OperatorCodes(op.OpcodeIndex())
            kind = names[max(code.BuiltinCode(), code.DeprecatedBuiltinCode())]
            inputs = tuple(int(v) for v in op.InputsAsNumpy())
            out_index = int(op.Outputs(0))
            inp, out = graph.Tensors(inputs[0]), graph.Tensors(out_index)
            if kind in ("CONV_2D", "DEPTHWISE_CONV_2D"):
                opt = options(op, "Conv2DOptions" if kind == "CONV_2D" else "DepthwiseConv2DOptions")
                wt, bt = graph.Tensors(inputs[1]), graph.Tensors(inputs[2])
                assert not np.any(wt.Quantization().ZeroPointAsNumpy()) and not np.any(bt.Quantization().ZeroPointAsNumpy())
                assert opt.DilationHFactor() == opt.DilationWFactor() == 1
                w, b = constant(inputs[1]), constant(inputs[2])
                depthwise = kind == "DEPTHWISE_CONV_2D"
                groups = int(inp.Shape(3)) if depthwise else 1
                w = w.transpose(3, 0, 1, 2) if depthwise else w.transpose(0, 3, 1, 2)
                stride = (opt.StrideH(), opt.StrideW())
                ph = max((int(out.Shape(1))-1)*stride[0]+w.shape[2]-int(inp.Shape(1)), 0) if opt.Padding() == 0 else 0
                pw = max((int(out.Shape(2))-1)*stride[1]+w.shape[3]-int(inp.Shape(2)), 0) if opt.Padding() == 0 else 0
                padding = (pw//2, pw-pw//2, ph//2, ph-ph//2)
                layer = Conv(w, b, wt.Quantization().ScaleAsNumpy(), bt.Quantization().ScaleAsNumpy(),
                             qparams(inp), qparams(out), stride, padding, groups, opt.FusedActivationFunction())
                routes = inputs[:1]
            else:
                routes = inputs
                if kind == "PAD":
                    pads = constant(inputs[1])
                    assert not pads[[0, 3]].any()
                    extra = tuple(int(x) for x in (*pads[2], *pads[1]))
                    routes = inputs[:1]
                elif kind == "ADD":
                    extra = options(op, "AddOptions").FusedActivationFunction()
                elif kind == "CONCATENATION":
                    opt = options(op, "ConcatenationOptions")
                    assert opt.Axis() == 3 and opt.FusedActivationFunction() == 0
                    extra = None
                elif kind == "MAX_POOL_2D":
                    opt = options(op, "Pool2DOptions")
                    assert opt.FusedActivationFunction() == 0 and opt.Padding() == 0
                    kernel = (opt.FilterHeight(), opt.FilterWidth())
                    stride = (opt.StrideH(), opt.StrideW())
                    assert stride == (1, 1) and all(k % 2 for k in kernel)
                    extra = (kernel, stride, (kernel[0]//2, kernel[1]//2))
                elif kind == "RESIZE_NEAREST_NEIGHBOR":
                    opt = options(op, "ResizeNearestNeighborOptions")
                    assert not opt.AlignCorners() and opt.HalfPixelCenters()
                    extra = tuple(int(v) for v in constant(inputs[1]))
                    assert extra == (int(inp.Shape(1))*2, int(inp.Shape(2))*2)
                    # For this exact 2x resize, floor((i+0.5)/2) == floor(i/2).
                    # Thus TFLite half-pixel nearest and ATen nearest coincide.
                    routes = inputs[:1]
                else:
                    raise ValueError(f"Unexpected operator {kind}")
                layer = Operation(kind, tuple(qparams(graph.Tensors(j)) for j in routes), qparams(out), extra)
            self.routes.append((routes, out_index))
            layers.append(layer)
        self.layers = nn.ModuleList(layers)

    def forward(self, x):
        tensors = {self.input_index: x.permute(0, 3, 1, 2)}
        for layer, (inputs, output) in zip(self.layers, self.routes):
            tensors[output] = layer(*(tensors[j] for j in inputs))
        return tuple(tensors[j].permute(0, 2, 3, 1) for j in self.output_indices)
