# Copyright 2026 Arm Limited and/or its affiliates.
# SPDX-License-Identifier: Apache-2.0
"""Import the original pre-Vela RNNoise graph with every original INT8 Q/DQ.

Inputs: features, VAD state, noise state, denoise state. Outputs: denoise state,
gains, noise state, VAD state, probability. Recurrent states are explicit.
"""
from pathlib import Path
import hashlib
import importlib
import numpy as np
import torch
from torch import nn
from torch.nn import functional as F
import torchao.quantization.pt2e.quantize_pt2e  # register Q/DQ

SOURCE = Path(__file__).with_name('upstream') / 'rnnoise_INT8.tflite'
SOURCE_SHA256 = '9c582545b7c13af44616c44b654f4fe721aa2585630b0ca173ca3589f6f11c2c'
INPUT_SHAPES = ((1, 1, 42), (1, 24), (1, 48), (1, 96))
OUTPUT_SHAPES = ((1, 1, 96), (1, 1, 22), (1, 1, 48), (1, 1, 24), (1, 1, 1))
QD = torch.ops.quantized_decomposed


def options(op, name):
    cls = getattr(importlib.import_module('ethosu.vela.tflite.' + name), name)
    table = op.BuiltinOptions()
    obj = cls()
    obj.Init(table.Bytes, table.Pos)
    return obj


class RNNoise(nn.Module):
    def __init__(self, source=SOURCE, quantized=True):
        super().__init__()
        from ethosu.vela.tflite.Model import Model
        from ethosu.vela.tflite.BuiltinOperator import BuiltinOperator
        from ethosu.vela.tflite.TensorType import TensorType
        data = Path(source).read_bytes()
        if hashlib.sha256(data).hexdigest() != SOURCE_SHA256:
            raise ValueError('Unexpected RNNoise source SHA256')
        model = Model.GetRootAsModel(data, 0)
        graph = model.Subgraphs(0)
        self.quantized = quantized
        self.inputs = tuple(int(i) for i in graph.InputsAsNumpy())
        self.outputs = tuple(int(i) for i in graph.OutputsAsNumpy())
        self.params, self.shapes, self.meta = {}, {}, {}
        self.constants = []
        dtype = {TensorType.INT8: np.int8, TensorType.INT32: np.int32}
        for i in range(graph.TensorsLength()):
            tensor = graph.Tensors(i)
            self.shapes[i] = tuple(int(s) for s in tensor.ShapeAsNumpy()) if tensor.ShapeLength() else ()
            q = tensor.Quantization()
            if q is not None and q.ScaleLength():
                assert q.ScaleLength() == 1 and q.ZeroPointLength() == 1
                self.params[i] = (float(q.Scale(0)), int(q.ZeroPoint(0)), torch.int8 if tensor.Type() == TensorType.INT8 else torch.int32)
            buffer = model.Buffers(tensor.Buffer())
            if buffer.DataLength():
                value = np.frombuffer(buffer.DataAsNumpy().tobytes(), dtype=dtype[tensor.Type()]).copy().reshape(self.shapes[i])
                self.register_buffer('constant_' + str(i), torch.from_numpy(value))
                self.constants.append(i)
                if i not in self.params: self.meta[i] = value.tolist()
        names = {v: k for k, v in vars(BuiltinOperator).items() if isinstance(v, int)}
        self.steps = []
        for i in range(graph.OperatorsLength()):
            op = graph.Operators(i)
            code = model.OperatorCodes(op.OpcodeIndex())
            name = names[max(code.BuiltinCode(), code.DeprecatedBuiltinCode())]
            ins = tuple(int(x) for x in op.InputsAsNumpy())
            outs = tuple(int(x) for x in op.OutputsAsNumpy())
            activation, axis = 0, 0
            if name in ('FULLY_CONNECTED', 'ADD', 'SUB', 'MUL'):
                cls = {'FULLY_CONNECTED': 'FullyConnectedOptions', 'ADD': 'AddOptions', 'SUB': 'SubOptions', 'MUL': 'MulOptions'}[name]
                activation = int(options(op, cls).FusedActivationFunction())
                assert activation in (0, 1)
            if name in ('CONCATENATION', 'PACK', 'UNPACK'):
                axis = int(options(op, {'CONCATENATION': 'ConcatenationOptions', 'PACK': 'PackOptions', 'UNPACK': 'UnpackOptions'}[name]).Axis())
            if name == 'SPLIT': axis = int(self.meta[ins[0]])
            if name == 'SPLIT_V': axis = int(self.meta[ins[2]])
            if name == 'SUB':
                # Give each recurrent gate its own broadcast-one tensor.
                # Reusing a scalar buffer across partitions exposes an Arm
                # FuseEqualPlaceholdersPass duplicate-target deletion bug.
                one = getattr(self, 'constant_' + str(ins[0]))
                assert one.numel() == 1
                self.register_buffer('sub_one_' + str(outs[0]), one.expand(self.shapes[outs[0]]).clone())
            if name == 'FULLY_CONNECTED':
                weight = getattr(self, 'constant_' + str(ins[1]))
                bias = getattr(self, 'constant_' + str(ins[2]))
                channels = weight.shape[0]
                self.register_buffer('conv_weight_' + str(outs[0]), weight.reshape(channels, -1, 1, 1).clone())
                self.register_buffer('weight_scales_' + str(outs[0]), torch.full((channels,), self.params[ins[1]][0], dtype=torch.float32))
                self.register_buffer('bias_scales_' + str(outs[0]), torch.full((channels,), self.params[ins[2]][0], dtype=torch.float32))
                self.register_buffer('zero_points_' + str(outs[0]), torch.zeros(channels, dtype=torch.int64))
            self.steps.append((name, ins, outs, activation, axis))

    def dq(self, x, index):
        if not self.quantized: return x
        scale, zero, dtype = self.params[index]
        lo, hi = (-128, 127) if dtype == torch.int8 else (-(2**31), 2**31-1)
        return QD.dequantize_per_tensor.default(x, scale, zero, lo, hi, dtype)

    def q(self, x, index):
        if not self.quantized: return x
        scale, zero, dtype = self.params[index]
        return QD.quantize_per_tensor.default(x, scale, zero, -128, 127, dtype)

    def forward(self, features, vad_state, noise_state, denoise_state):
        values = dict(zip(self.inputs, (features, vad_state, noise_state, denoise_state)))
        for i in self.constants:
            value = getattr(self, 'constant_' + str(i))
            values[i] = value if self.quantized or i not in self.params else (value.float() - self.params[i][1]) * self.params[i][0]
        for name, ins, outs, activation, axis in self.steps:
            if name == 'RESHAPE':
                values[outs[0]] = values[ins[0]].reshape(self.shapes[outs[0]])
            elif name == 'PACK':
                values[outs[0]] = torch.stack([values[i] for i in ins], dim=axis)
            elif name == 'UNPACK':
                result = torch.unbind(values[ins[0]], dim=axis)
                for i, out in enumerate(outs): values[out] = result[i]
            elif name in ('SPLIT', 'SPLIT_V'):
                index = ins[1] if name == 'SPLIT' else ins[0]
                result = torch.split(values[index], tuple(self.shapes[o][axis] for o in outs), dim=axis)
                for i, out in enumerate(outs): values[out] = result[i]
            elif name == 'CONCATENATION':
                values[outs[0]] = torch.cat([values[i] for i in ins], dim=axis)
            elif name == 'FULLY_CONNECTED' and self.quantized:
                # A 1x1 convolution is the same dense matrix multiply. This
                # form exposes quantized inputs directly to the Arm backend;
                # its linear import otherwise leaves unsupported CPU linear.
                suffix = str(outs[0])
                x = self.dq(values[ins[0]].reshape(1, -1, 1, 1), ins[0])
                zero = getattr(self, 'zero_points_' + suffix)
                weight = QD.dequantize_per_channel.default(getattr(self, 'conv_weight_' + suffix), getattr(self, 'weight_scales_' + suffix), zero, 0, -127, 127, torch.int8)
                bias = QD.dequantize_per_channel.default(values[ins[2]], getattr(self, 'bias_scales_' + suffix), zero, 0, -(2**31), 2**31-1, torch.int32)
                result = F.conv2d(x, weight, bias)
                if activation == 1: result = F.relu(result)
                values[outs[0]] = self.q(result, outs[0]).reshape(self.shapes[outs[0]])
            else:
                x = [self.dq(values[i], i) for i in ins]
                if name == 'FULLY_CONNECTED': result = F.linear(x[0], x[1], x[2])
                elif name == 'ADD': result = x[0] + x[1]
                elif name == 'SUB':
                    if self.quantized: x[0] = self.dq(getattr(self, 'sub_one_' + str(outs[0])), ins[0])
                    result = x[0] - x[1]
                elif name == 'MUL': result = x[0] * x[1]
                elif name == 'TANH': result = torch.tanh(x[0])
                elif name == 'LOGISTIC': result = torch.sigmoid(x[0])
                elif name == 'QUANTIZE': result = x[0]
                else: raise ValueError('Unsupported source operator: ' + name)
                if activation == 1: result = F.relu(result)
                values[outs[0]] = self.q(result, outs[0])
        return tuple(values[i] for i in self.outputs)
