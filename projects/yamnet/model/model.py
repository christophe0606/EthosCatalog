# Copyright 2026 Arm Limited and/or its affiliates.
# Copyright 2019 The TensorFlow Authors All Rights Reserved.
# SPDX-License-Identifier: Apache-2.0
"""PyTorch transcription of pinned Apache YAMNet (see upstream/provenance.json).

Batch normalization is folded during import. TensorFlow SAME padding is
asymmetric for stride two. Boundary: [1,96,64] natural-log mel magnitudes.
"""
from pathlib import Path
import hashlib
import numpy as np
import torch
from torch import nn
from torch.nn import functional as F

INPUT_SHAPE = (1, 96, 64)
OUTPUT_SHAPE = (1, 521)
SOURCE = Path(__file__).parent / 'upstream/yamnet.h5'
SOURCE_SHA256 = '13c3308955bbfaef262f175ac9c40e47b134573a93984f009220dd7cc12a1744'
LAYER_DEFS = [(32, 2), (64, 1), (128, 2), (128, 1), (256, 2),
              (256, 1), (512, 2), (512, 1), (512, 1), (512, 1),
              (512, 1), (512, 1), (1024, 2), (1024, 1)]


class ConvRelu(nn.Module):
    def __init__(self, cin, cout, kernel, stride, groups=1):
        super().__init__()
        self.kernel, self.stride = kernel, stride
        self.conv = nn.Conv2d(cin, cout, kernel, stride=stride, groups=groups, bias=True)

    def forward(self, x):
        if self.kernel == 3:
            h, w = x.shape[-2:]
            ph = max((h + self.stride - 1) // self.stride * self.stride - self.stride + 3 - h, 0)
            pw = max((w + self.stride - 1) // self.stride * self.stride - self.stride + 3 - w, 0)
            x = F.pad(x, (pw // 2, pw - pw // 2, ph // 2, ph - ph // 2))
        return F.relu(self.conv(x))


class YAMNet(nn.Module):
    def __init__(self, source=SOURCE, depthwise_implementation='depthwise'):
        super().__init__()
        blocks = []
        cin = 1
        names = []
        for i, (cout, stride) in enumerate(LAYER_DEFS, 1):
            if i == 1:
                blocks.append(ConvRelu(cin, cout, 3, stride))
                names.append(f'layer{i}/conv')
            else:
                blocks.extend([ConvRelu(cin, cin, 3, stride, groups=cin), ConvRelu(cin, cout, 1, 1)])
                names.extend([f'layer{i}/depthwise_conv', f'layer{i}/pointwise_conv'])
            cin = cout
        self.blocks = nn.ModuleList(blocks)
        self.classifier = nn.Linear(1024, 521)
        if source is not None:
            self.load_keras_weights(source, names)
        if depthwise_implementation == 'dense':
            self.expand_depthwise()
        elif depthwise_implementation != 'depthwise':
            raise ValueError(depthwise_implementation)

    def expand_depthwise(self):
        """Equivalent diagonal group-one convolutions for lowering diagnostics."""
        for block in self.blocks:
            old = block.conv
            if old.groups == 1:
                continue
            replacement = nn.Conv2d(old.in_channels, old.out_channels,
                                    old.kernel_size, stride=old.stride, bias=True)
            with torch.no_grad():
                replacement.weight.zero_()
                channels = torch.arange(old.in_channels)
                replacement.weight[channels, channels] = old.weight[:, 0]
                replacement.bias.copy_(old.bias)
            block.conv = replacement

    def load_keras_weights(self, source, names):
        import h5py
        source = Path(source)
        assert hashlib.sha256(source.read_bytes()).hexdigest() == SOURCE_SHA256
        tensors = {}
        with h5py.File(source, 'r') as f:
            f.visititems(lambda key, value: tensors.update({key: np.array(value)}) if isinstance(value, h5py.Dataset) else None)
        def get(suffix):
            keys = [key for key in tensors if key.endswith(suffix)]
            assert len(keys) == 1, (suffix, keys)
            return tensors[keys[0]].astype(np.float64)
        with torch.no_grad():
            for block, name in zip(self.blocks, names):
                kernel_name = 'depthwise_kernel:0' if 'depthwise' in name else 'kernel:0'
                w = get(name + '/' + kernel_name)
                w = w.transpose(2, 3, 0, 1) if 'depthwise' in name else w.transpose(3, 2, 0, 1)
                beta, mean, var = [get(name + '/bn/' + field + ':0') for field in ['beta', 'moving_mean', 'moving_variance']]
                factor = 1 / np.sqrt(var + 1e-4)
                block.conv.weight.copy_(torch.from_numpy(w * factor[:, None, None, None]).float())
                block.conv.bias.copy_(torch.from_numpy(beta - mean * factor).float())
            self.classifier.weight.copy_(torch.from_numpy(get('logits/kernel:0').T).float())
            self.classifier.bias.copy_(torch.from_numpy(get('logits/bias:0')).float())

    def embedding(self, x):
        x = x.unsqueeze(1)
        for block in self.blocks:
            x = block(x)
        return x.mean(dim=(-2, -1))

    def forward(self, features):
        return torch.sigmoid(self.classifier(self.embedding(features)))

