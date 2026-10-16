# Copyright 2026 Arm Limited and/or its affiliates.
# SPDX-License-Identifier: Apache-2.0
"""Import MicroNet's existing TFLite quantization into PyTorch Q/DQ operators.

No training, calibration, or quantizer is involved. The exported interface is
the original NHWC int8 MFCC input and int8 class scores (there is no softmax).
The Q/DQ graph is consumed by ExecuTorch's Arm partitioner, which calls Vela.
"""
from pathlib import Path
import hashlib
import importlib

import numpy as np
import torch
from torch import nn
from torch.nn import functional as F
import torchao.quantization.pt2e.quantize_pt2e  # register decomposed Q/DQ

INPUT_SHAPE = (1, 49, 10, 1)
OUTPUT_SHAPE = (1, 12)
SOURCE_SHA256 = "15723bcc7d73599546cfebd289632799393e329df84bba883205b08b666a1aa3"
SOURCE = Path(__file__).with_name("kws_micronet_l.tflite")
QD = torch.ops.quantized_decomposed


def qparams(tensor):
    q = tensor.Quantization()
    return float(q.Scale(0)), int(q.ZeroPoint(0))


class QuantizedConv(nn.Module):
    def __init__(self, weight, bias, weight_scales, bias_scales,
                 input_qparams, output_qparams, stride, padding, groups, relu6):
        super().__init__()
        self.register_buffer("weight", torch.from_numpy(weight.copy()))
        self.register_buffer("bias", torch.from_numpy(bias.copy()))
        self.register_buffer("weight_scales", torch.from_numpy(weight_scales.copy()))
        self.register_buffer("bias_scales", torch.from_numpy(bias_scales.copy()))
        self.register_buffer("zero_points", torch.zeros(len(weight_scales), dtype=torch.int64))
        self.input_qparams, self.output_qparams = input_qparams, output_qparams
        self.stride, self.padding, self.groups, self.relu6 = stride, padding, groups, relu6

    def forward(self, x):
        x = QD.dequantize_per_tensor.default(x, *self.input_qparams, -128, 127, torch.int8)
        weight = QD.dequantize_per_channel.default(
            self.weight, self.weight_scales, self.zero_points, 0, -127, 127, torch.int8)
        bias = QD.dequantize_per_channel.default(
            self.bias, self.bias_scales, self.zero_points, 0, -(2**31), 2**31 - 1, torch.int32)
        if self.padding[0] == self.padding[1] and self.padding[2] == self.padding[3]:
            conv_padding = (self.padding[2], self.padding[0])
        else:
            x = F.pad(x, self.padding)
            x = QD.quantize_per_tensor.default(x, *self.input_qparams, -128, 127, torch.int8)
            x = QD.dequantize_per_tensor.default(x, *self.input_qparams, -128, 127, torch.int8)
            conv_padding = (0, 0)
        x = F.conv2d(x, weight, bias, self.stride, conv_padding, groups=self.groups)
        if self.relu6:
            x = F.hardtanh(x, 0.0, 6.0)
        return QD.quantize_per_tensor.default(x, *self.output_qparams, -128, 127, torch.int8)


class QuantizedPool(nn.Module):
    def __init__(self, params, kernel, stride):
        super().__init__()
        self.params, self.kernel, self.stride = params, kernel, stride

    def forward(self, x):
        x = QD.dequantize_per_tensor.default(x, *self.params, -128, 127, torch.int8)
        x = F.avg_pool2d(x, self.kernel, self.stride)
        return QD.quantize_per_tensor.default(x, *self.params, -128, 127, torch.int8)


class MicroNet(nn.Module):
    def __init__(self, source=SOURCE):
        super().__init__()
        from ethosu.vela.tflite.Model import Model
        from ethosu.vela.tflite.BuiltinOperator import BuiltinOperator
        from ethosu.vela.tflite.TensorType import TensorType
        data = Path(source).read_bytes()
        if hashlib.sha256(data).hexdigest() != SOURCE_SHA256:
            raise ValueError("Unexpected MicroNet TFLite source SHA256")
        model = Model.GetRootAsModel(data, 0)
        graph = model.Subgraphs(0)
        names = {v: k for k, v in vars(BuiltinOperator).items() if isinstance(v, int)}

        def constant(index):
            tensor = graph.Tensors(index)
            dtype = {TensorType.INT8: np.int8, TensorType.INT32: np.int32}[tensor.Type()]
            return np.frombuffer(model.Buffers(tensor.Buffer()).DataAsNumpy().tobytes(),
                                 dtype=dtype).reshape(tuple(tensor.ShapeAsNumpy())).copy()

        self.input_qparams = qparams(graph.Tensors(graph.Inputs(0)))
        self.output_qparams = qparams(graph.Tensors(graph.Outputs(0)))
        layers = []
        for i in range(graph.OperatorsLength()):
            op = graph.Operators(i)
            code = model.OperatorCodes(op.OpcodeIndex())
            name = names[max(code.BuiltinCode(), code.DeprecatedBuiltinCode())]
            inputs = op.InputsAsNumpy()
            output = graph.Tensors(op.Outputs(0))
            if name == "RESHAPE":
                if tuple(constant(inputs[1])) != OUTPUT_SHAPE:
                    raise ValueError("Unexpected output reshape")
                continue
            option_name = {"CONV_2D": "Conv2DOptions", "DEPTHWISE_CONV_2D": "DepthwiseConv2DOptions",
                           "AVERAGE_POOL_2D": "Pool2DOptions"}[name]
            options = getattr(importlib.import_module("ethosu.vela.tflite." + option_name), option_name)()
            table = op.BuiltinOptions()
            options.Init(table.Bytes, table.Pos)
            inp = graph.Tensors(inputs[0])
            stride = (options.StrideH(), options.StrideW())
            if name == "AVERAGE_POOL_2D":
                if options.Padding() != 1 or qparams(inp) != qparams(output):
                    raise ValueError("Expected VALID pool with matching quantization")
                layers.append(QuantizedPool(qparams(inp),
                                           (options.FilterHeight(), options.FilterWidth()), stride))
                continue
            wt, bt = graph.Tensors(inputs[1]), graph.Tensors(inputs[2])
            weight, bias = constant(inputs[1]), constant(inputs[2])
            groups = int(inp.Shape(3)) if name == "DEPTHWISE_CONV_2D" else 1
            weight = weight.transpose(3, 0, 1, 2) if groups != 1 else weight.transpose(0, 3, 1, 2)
            if options.DilationHFactor() != 1 or options.DilationWFactor() != 1:
                raise ValueError("Unexpected dilated convolution")
            if np.any(wt.Quantization().ZeroPointAsNumpy()) or np.any(bt.Quantization().ZeroPointAsNumpy()):
                raise ValueError("Expected symmetric weights and bias")
            # TensorFlow SAME can put an extra cell on the bottom/right.
            ih, iw = int(inp.Shape(1)), int(inp.Shape(2))
            oh, ow = int(output.Shape(1)), int(output.Shape(2))
            kh, kw = weight.shape[2:]
            ph = max((oh-1)*stride[0]+kh-ih, 0) if options.Padding() == 0 else 0
            pw = max((ow-1)*stride[1]+kw-iw, 0) if options.Padding() == 0 else 0
            padding = (pw//2, pw-pw//2, ph//2, ph-ph//2)
            # Padding uses real zero after dequantization.
            conv = QuantizedConv(weight, bias, wt.Quantization().ScaleAsNumpy(),
                                 bt.Quantization().ScaleAsNumpy(), qparams(inp), qparams(output),
                                 stride, padding, groups, options.FusedActivationFunction() == 3)
            if options.FusedActivationFunction() not in (0, 3):
                raise ValueError("Unexpected fused activation")
            layers.append(conv)
        self.layers = nn.ModuleList(layers)

    def forward(self, x):
        x = x.permute(0, 3, 1, 2)
        for layer in self.layers:
            x = layer(x)
        return x.reshape(OUTPUT_SHAPE)

