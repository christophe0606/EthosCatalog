# Copyright 2026 Arm Limited and/or its affiliates.
# SPDX-License-Identifier: Apache-2.0
"""Compare the EnCodec streaming implementation against the upstream inference modules."""

import argparse
from pathlib import Path
import sys

import torch

from model import BLOCK_SAMPLES, BOOKS, SAMPLE_RATE, Decoder, Encoder, read_weights, run_stages, staged_methods


def audio_blocks(seed, count):
    generator = torch.Generator().manual_seed(seed)
    time = torch.arange(count * BLOCK_SAMPLES) / SAMPLE_RATE
    audio = 0.23 * torch.sin(2 * torch.pi * (139 * time + 470 * time.square()))
    audio += 0.08 * torch.sin(2 * torch.pi * 2113 * time)
    audio += 0.045 * torch.randn(time.shape, generator=generator)
    audio *= 0.46 + 0.42 * torch.sin(2 * torch.pi * 2.7 * time)
    audio[BLOCK_SAMPLES // 2:BLOCK_SAMPLES] = 0
    audio[2 * BLOCK_SAMPLES] = 0.8
    return audio.reshape(1, 1, -1)


def load_reference(reference, weights):
    # The inference subset retains all upstream relative imports. Populate the
    # three names ordinarily re-exported by modules/__init__.py.
    sys.path[:0] = [str(reference), str(reference / "deps")]
    import encodec.modules as modules
    from encodec.modules import conv, lstm

    modules.SConv1d = conv.SConv1d
    modules.SConvTranspose1d = conv.SConvTranspose1d
    modules.SLSTM = lstm.SLSTM
    from encodec.modules.seanet import SEANetDecoder, SEANetEncoder
    from encodec.quantization.vq import ResidualVectorQuantizer

    components = (("encoder", SEANetEncoder(causal=True)),
                  ("decoder", SEANetDecoder(causal=True)),
                  ("quantizer", ResidualVectorQuantizer(dimension=128, n_q=32, bins=1024)))
    for prefix, module in components:
        start = prefix + "."
        module.load_state_dict({key.removeprefix(start): value for key, value in weights.items()
                                if key.startswith(start)})
        module.eval()
    return [module for _, module in components]


@torch.no_grad()
def verify(weights, reference, books, count):
    encoder = Encoder(weights, books).eval()
    decoder = Decoder(weights, books).eval()
    ref_encoder, ref_decoder, quantizer = load_reference(reference, weights)
    audio = audio_blocks(42, count)
    ref_latent = ref_encoder(audio)
    ref_codes = quantizer.encode(ref_latent, 75, books * 0.75).transpose(0, 1).to(torch.int32)
    reference_audio = ref_decoder(quantizer.decode(ref_codes.transpose(0, 1)))
    methods = staged_methods(encoder, decoder)

    def call(name, *inputs):
        return methods[name](*inputs)

    def init(name):
        return (torch.ones(methods[name + "_front"].state_size), torch.ones(1024),
                torch.ones(1024), torch.ones(methods[name + "_back"].state_size))

    # Nonzero starting states verify that reset explicitly ignores stale values.
    encoder_state = torch.ones(encoder.state_size)
    latent_state = encoder_state.clone()
    decoder_state = torch.ones(decoder.state_size)
    staged_encoder_state = init("encode")
    staged_decoder_state = init("decode")
    latent_blocks, code_blocks, decoded_blocks = [], [], []
    stage_error = 0.0
    for index, samples in enumerate(audio.split(BLOCK_SAMPLES, dim=-1)):
        reset = torch.tensor([index == 0])
        codes, encoder_state = encoder(samples, encoder_state, reset)
        latent, latent_state = encoder.latent(samples, latent_state, reset)
        decoded, decoder_state = decoder(codes, decoder_state, reset)
        stage_codes, staged_encoder_state = run_stages(call, "encode", samples, staged_encoder_state, reset)
        stage_audio, staged_decoder_state = run_stages(call, "decode", stage_codes, staged_decoder_state, reset)
        torch.testing.assert_close(stage_codes, codes, atol=0, rtol=0)
        torch.testing.assert_close(stage_audio, decoded, atol=2e-6, rtol=1e-4)
        torch.testing.assert_close(torch.cat(staged_encoder_state), encoder_state, atol=2e-6, rtol=1e-4)
        torch.testing.assert_close(torch.cat(staged_decoder_state), decoder_state, atol=2e-6, rtol=1e-4)
        stage_error = max(stage_error, (stage_audio - decoded).abs().max().item())
        latent_blocks.append(latent.transpose(1, 2))
        code_blocks.append(codes)
        decoded_blocks.append(decoded)

    actual_latent = torch.cat(latent_blocks, dim=-1)
    actual_codes = torch.cat(code_blocks, dim=-1)
    actual_audio = torch.cat(decoded_blocks, dim=-1)
    print(f"{count} blocks, {books} codebooks, codes matched {(actual_codes == ref_codes).sum().item()}"
          f"/{ref_codes.numel()}")
    print(f"maximum absolute error: latent {(actual_latent - ref_latent).abs().max().item():.8g}, "
          f"PCM {(actual_audio - reference_audio).abs().max().item():.8g}, staged PCM {stage_error:.8g}")
    torch.testing.assert_close(actual_latent, ref_latent, atol=3e-5, rtol=1e-4)
    torch.testing.assert_close(actual_codes, ref_codes, atol=0, rtol=0)
    torch.testing.assert_close(actual_audio, reference_audio, atol=3e-5, rtol=1e-4)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("checkpoint", type=Path)
    parser.add_argument("reference", type=Path)
    parser.add_argument("--books", type=int, choices=(2, 4, 8, 16, 32), default=BOOKS)
    parser.add_argument("--blocks", type=int, default=12)
    args = parser.parse_args()
    if args.blocks < 3:
        parser.error("at least three blocks are needed to exercise the test pattern")
    torch.set_num_threads(3)
    verify(read_weights(args.checkpoint), args.reference, args.books, args.blocks)


if __name__ == "__main__":
    main()
