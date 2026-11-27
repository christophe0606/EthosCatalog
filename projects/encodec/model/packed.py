# Copyright 2026 Arm Limited and/or its affiliates.
# SPDX-License-Identifier: Apache-2.0
"""Reorder two calibrated EnCodec convolutions for the Ethos-U schedule."""

import torch

ATEN = torch.ops.aten


def calibrated_convolution(module, shape):
    """Find a static convolution with an INT16 activation and INT8 weights."""
    qops = torch.ops.quantized_decomposed
    matches = [
        node for node in module.graph.nodes
        if node.target == ATEN.conv1d.default and tuple(node.args[1].meta["val"].shape) == shape
    ]
    if len(matches) != 1:
        raise ValueError(f"Expected one calibrated EnCodec convolution with weights of shape {shape}")
    convolution = matches[0]
    activation, weight, _ = convolution.args[:3]
    if (activation.target != qops.dequantize_per_tensor.default or activation.args[-1] != torch.int16
            or activation.args[0].target != qops.quantize_per_tensor.default
            or weight.target != qops.dequantize_per_channel.default or weight.args[-1] != torch.int8
            or weight.args[3] != 0 or weight.args[0].op != "get_attr"):
        raise ValueError(f"Unexpected INT16/INT8 EnCodec quantization at {convolution.name}")
    return convolution, activation, weight


def pack_encoder_front(module):
    convolution, activation, weight = calibrated_convolution(module, (512, 256, 16))
    if list(convolution.args[3]) != [8] or tuple(activation.meta["val"].shape) != (1, 256, 80):
        raise ValueError("Expected the nine-frame EnCodec stride-8 encoder convolution")

    # Group eight time positions into the channel dimension. The existing
    # stride-8 convolution becomes a stride-1 convolution with two time taps.
    weights = module.get_buffer(weight.args[0].target)
    packed = weights.reshape(512, 256, 2, 8).transpose(2, 3).reshape(512, 2048, 2)
    setattr(module, weight.args[0].target, packed.contiguous())

    graph = module.graph
    with graph.inserting_before(activation):
        lanes = graph.call_function(ATEN.reshape.default, (activation.args[0], (1, 256, 10, 8)))
        lanes = graph.call_function(ATEN.permute.default, (lanes, (0, 1, 3, 2)))
        lanes = graph.call_function(ATEN.reshape.default, (lanes, (1, 2048, 10)))
    activation.args = (lanes, *activation.args[1:])
    convolution.args = (*convolution.args[:3], [1])
    graph.lint()
    module.recompile()


def pack_decoder_back(module):
    _, activation, weight = calibrated_convolution(module, (2048, 512, 2))
    if tuple(activation.meta["val"].shape) != (1, 512, 10):
        raise ValueError("Expected nine EnCodec latent frames and one history frame")

    # Each row combines the previous latent frame and the current latent frame.
    # A pointwise convolution evaluates the same two-tap polyphase upsampler.
    original = weight.args[0].target
    weights = module.get_buffer(original)
    packed = weights.transpose(1, 2).flatten(1).contiguous().unsqueeze(2)
    module.register_buffer("packed_upsample_weight", packed)

    graph = module.graph
    with graph.inserting_before(weight):
        weights_node = graph.get_attr("packed_upsample_weight")
    weight.args = (weights_node, *weight.args[1:])
    with graph.inserting_before(activation):
        first = graph.call_function(ATEN.slice.Tensor, (activation.args[0], 2, 0, 9))
        second = graph.call_function(ATEN.slice.Tensor, (activation.args[0], 2, 1, 10))
        lanes = graph.call_function(ATEN.cat.default, ([first, second], 1))
    activation.args = (lanes, *activation.args[1:])
    graph.eliminate_dead_code()
    if not any(node.op == "get_attr" and node.target == original for node in graph.nodes):
        delattr(module, original)
    graph.lint()
    module.recompile()
