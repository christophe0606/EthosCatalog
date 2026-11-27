# Copyright 2026 Arm Limited and/or its affiliates.
# SPDX-License-Identifier: Apache-2.0
"""Calibrate the pretrained fixed-block EnCodec methods and save pre-Vela exports."""

import argparse
from dataclasses import replace
from pathlib import Path
import sys

import torch
import yaml
from executorch.backends.arm.quantizer.arm_quantizer_utils import NodeFinder

from model import BLOCK_SAMPLES, BOOKS, FRAMES, load_codec, run_stages, staged_methods
from packed import pack_decoder_back, pack_encoder_front
from validate_reference import audio_blocks

PROJECT = Path(__file__).resolve().parents[1]
ROOT = PROJECT.parents[1]
DEFAULT_SOLUTION = ROOT / "EthosCatalog.csolution.yml"
DEFAULT_CHECKPOINT = ROOT / ".flow/encodec/encodec_24khz-d7cc33bc.th"
sys.path.insert(0, str(ROOT / "scripts"))
from quantized_model import compile_spec, method_configs, save_quantized_program
from validate_networks import load_network


class ConvolutionActivations(NodeFinder):
    def find_nodes(self, model):
        for node in model.graph.nodes:
            if node.target == torch.ops.aten.elu.default:
                yield node
            elif node.target == torch.ops.aten.add.Tensor:
                scopes = node.meta.get("nn_module_stack", {}).values()
                if any(kind.rsplit(".", 1)[-1] == "Residual" for _, kind in scopes):
                    yield node


class ConvolutionBoundaries(NodeFinder):
    def __init__(self, factors):
        self.widths = tuple(FRAMES * factor for factor in factors)

    def find_nodes(self, model):
        for node in model.graph.nodes:
            if (node.target == torch.ops.aten.elu.default
                    and len(node.meta.get("nn_module_stack", {})) == 1
                    and node.meta["val"].shape[-1] in self.widths):
                yield node


def examples(models):
    reset = torch.ones(1, dtype=torch.bool)
    example_data = {"encode": torch.zeros(1, 1, BLOCK_SAMPLES),
                    "decode": torch.zeros(1, BOOKS, FRAMES, dtype=torch.int32)}
    features = torch.zeros(1, FRAMES, 512)
    projected = torch.zeros(1, 2048)
    states, inputs = {}, {}
    for name, data in example_data.items():
        states[name] = (torch.zeros(models[name + "_front"].state_size),
                        torch.zeros(1024), torch.zeros(1024),
                        torch.zeros(models[name + "_back"].state_size))
        front, hidden0, hidden1, back = states[name]
        inputs.update({
            name + "_front": (data, front, reset),
            name + "_hidden0": (projected, hidden0, reset),
            name + "_project1": (features,),
            name + "_hidden1": (projected, hidden1, reset),
            # The skip input and recurrent output are distinct at runtime.
            name + "_back": (features, torch.zeros_like(features), back, reset),
        })
    return states, inputs


def prepare_methods(spec, models, inputs):
    from executorch.backends.arm.quantizer import EthosUQuantizer, get_symmetric_a16w8_quantization_config
    from torchao.quantization.pt2e.observer import MinMaxObserver
    from torchao.quantization.pt2e.quantize_pt2e import prepare_pt2e

    config = get_symmetric_a16w8_quantization_config(epsilon=2 ** -24)
    activation = replace(config.input_activation,
                         observer_or_fake_quant_ctr=MinMaxObserver.with_args(eps=2 ** -24))
    config = replace(config, input_activation=activation, output_activation=activation)
    bands = {"encode_front": (40, 8), "decode_back": (8, 40, 160)}
    result = {}
    for name, model in models.items():
        print(f"Exporting and preparing {name}", flush=True)
        exported = torch.export.export(model, inputs[name]).module(check_guards=False)
        quantizer = EthosUQuantizer(spec).set_global(config if name in bands else None)
        quantizer.set_node_target(torch.ops.aten.conv1d.default, config)
        quantizer.set_node_target(torch.ops.aten.linear.default, config)
        if name in (*bands, "encode_back"):
            quantizer.set_node_finder(config, ConvolutionActivations())
        if name in bands:
            # Portable ELUs divide the large convolution paths into payloads
            # that fit the board's SRAM staging banks.
            quantizer.set_node_finder(None, ConvolutionBoundaries(bands[name]))
        result[name] = prepare_pt2e(exported, quantizer)
    return result


@torch.no_grad()
def calibrate(prepared, states):
    def call(name, *inputs):
        return prepared[name](*inputs)

    for seed in (321, 772, 960):
        encoder_state, decoder_state = states["encode"], states["decode"]
        for index, samples in enumerate(audio_blocks(seed, 8).split(BLOCK_SAMPLES, dim=-1)):
            reset = torch.tensor([index == 0])
            codes, encoder_state = run_stages(call, "encode", samples, encoder_state, reset)
            _, decoder_state = run_stages(call, "decode", codes, decoder_state, reset)
        print(f"Calibrated eight sequential blocks, seed {seed}", flush=True)


@torch.no_grad()
def evaluate(encoder, decoder, quantized, states):
    def call(name, *inputs):
        return quantized[name](*inputs)

    float_encoder = torch.zeros(encoder.state_size)
    float_decoder = torch.zeros(decoder.state_size)
    quant_encoder, quant_decoder = states["encode"], states["decode"]
    outputs, references = [], []
    code_matches = 0
    code_count = 0
    for index, samples in enumerate(audio_blocks(601, 12).split(BLOCK_SAMPLES, dim=-1)):
        reset = torch.tensor([index == 0])
        expected_codes, float_encoder = encoder(samples, float_encoder, reset)
        reference, float_decoder = decoder(expected_codes, float_decoder, reset)
        codes, quant_encoder = run_stages(call, "encode", samples, quant_encoder, reset)
        output, quant_decoder = run_stages(call, "decode", codes, quant_decoder, reset)
        code_matches += (expected_codes == codes).sum().item()
        code_count += expected_codes.numel()
        references.append(reference)
        outputs.append(output)

    reference = torch.cat(references, dim=-1)
    actual = torch.cat(outputs, dim=-1)
    error = actual - reference
    if not torch.isfinite(actual).all():
        raise ValueError("Quantized codec produced nonfinite PCM")
    print(f"Matched codes {code_matches}/{code_count}, PCM MAE {error.abs().mean().item():.8g}, "
          f"max error {error.abs().max().item():.8g}, "
          f"SNR vs float {10 * torch.log10(reference.square().sum() / error.square().sum()).item():.2f} dB",
          flush=True)


def save_models(spec, network, checkpoint):
    from torchao.quantization.pt2e.quantize_pt2e import convert_pt2e

    encoder, decoder = load_codec(checkpoint)
    models = staged_methods(encoder, decoder)
    states, inputs = examples(models)
    prepared = prepare_methods(spec, models, inputs)
    calibrate(prepared, states)
    # Calibrate the packed decoder with codes and state from the float codec.
    decoder_back = prepare_methods(spec, {"decode_back": models["decode_back"]}, inputs)
    calibrate({**models, **decoder_back}, states)
    prepared.update(decoder_back)
    quantized = {name: convert_pt2e(module, fold_quantize=True) for name, module in prepared.items()}
    for name, pack in (("encode_front", pack_encoder_front), ("decode_back", pack_decoder_back)):
        module = torch.export.export(quantized[name], inputs[name]).module(check_guards=False)
        pack(module)
        quantized[name] = module
    evaluate(encoder, decoder, quantized, states)
    configs = method_configs(network)
    for name, module in quantized.items():
        print(f"Saving pre-Vela {name} graph and weights", flush=True)
        save_quantized_program(torch.export.export(module, inputs[name]), spec.target, PROJECT, configs[name])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--solution", type=Path, default=DEFAULT_SOLUTION)
    parser.add_argument("--project", type=Path, help="Optional project passed by the shared AI-layer generator")
    parser.add_argument("--checkpoint", type=Path, default=DEFAULT_CHECKPOINT)
    args = parser.parse_args()
    if args.project and args.project.resolve().parent != PROJECT:
        parser.error("--project must point to the EnCodec cproject")
    solution = yaml.safe_load(args.solution.read_text(encoding="utf-8"))["solution"]
    network = load_network(PROJECT / "model/network.yml")
    torch.set_num_threads(4)
    save_models(compile_spec(solution, PROJECT, network), network, args.checkpoint)


if __name__ == "__main__":
    main()
