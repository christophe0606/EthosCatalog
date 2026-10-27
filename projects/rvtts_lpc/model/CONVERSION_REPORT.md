# rvTTS shared model conversion

Source: [rvTTS](https://github.com/ArmstrongSubero/rvTTS), commit
`b30af35f5366197f1a8845ef55af01a6c7c39a64`. Selected unmodified source and
the two weight blobs are in `upstream/`; `upstream_manifest.json` pins their
SHA256 values. The upstream Apache-2.0 license is preserved there. No CH32
vendor startup, full external project or preprint is included.

`model.py` reconstructs the bidirectional 64-unit GRU prosody model and the
128-channel acoustic convolution model. The supplied weights are already
symmetric int8 with per-tensor scales, float biases and float activations.
The trained float weights and their original quantization error are unavailable.
`rvtts_float.safetensors` contains the dequantized supplied parameters, including
frontend embeddings. The saved method `.pt2` graphs contain no Vela conversion.

`validate.py` compiles the unmodified upstream C with a small host adapter and
checks 1, 2, 7, 32, 96 and 173-frame sequences. Maximum absolute differences
are 2.87e-6 for prosody and 5.25e-6 for acoustic features. The inputs include
synthetic phoneme runs, stress and word IDs. These establish numerical mapping,
not perceptual quality. Full records and source-derived fixtures are in
`float_equivalence.json` and `source_fixtures.npz`.

## Export interface

The whole fixed 8-frame prosody graph was given to the normal ExecuTorch
partitioner first. It compiles as one delegate (182 NPU operators); see
`whole_prosody_trial.json`. The required variable utterance interface fails
`torch.export`: GRU specializes the frame count. The complete dynamic acoustic
export produces no Ethos delegate; see `dynamic_acoustic_trial.json`.

The deployed interface consequently exposes exact recurrent steps and fixed
temporal tiles. CPU code performs the original input feature/embedding lookup
and bidirectional scan. This is required for arbitrary UART utterance lengths;
it does not truncate the GRU context or approximate a long utterance as separate
short utterances. ExecuTorch partitions each method. The complete acoustic
network stays in a single method. Its 34-frame halo covers the full receptive
field; validity masks preserve the original zero boundary at every layer.
Unquantized method orchestration agrees with the complete PyTorch models within
2.4e-6. Details are in `ADAPTER_CONTRACT.md`.

## Quantization and result

`quantize.py` preserves the supplied per-tensor int8 weight grids, adds symmetric
int16 activations and uses full MinMax calibration with a 10% activation range
guard. Calibration uses eight synthetic sequences of 1–223 frames; evaluation
uses four independent sequences of 2, 19, 81 and 157 frames. No training occurs.
An exploratory per-channel weight requantization added avoidable rounding and
was replaced by the original per-tensor grids.

| Method | Held-out maximum absolute error | RMSE |
| --- | ---: | ---: |
| prosody_pre | 0.000174 | 0.0000357 |
| prosody_gru | 0.000261 | 0.0000430 |
| prosody_head | 0.000602 | 0.000151 |
| acoustic | 0.000918 | 0.000133 |

With quantized recurrent state fed back, the maximum complete feature error is
0.00180 over the evaluation sequences. `conversion_metrics.json` and
`network.yml` contain the same final measurements. This is a synthetic-feature
evaluation; speaker listening validation remains required.

The generated `../ai_layer/model.pte` is 528,144 bytes, with four Ethos
delegates. Only float boundary quantize/dequantize operators remain on the CPU.
Vela estimates an acoustic arena of about 91 KiB. These are compiler results,
not measured board real-time performance.

## Reproduce

From the repository root:

```powershell
uv run projects/rvtts_lpc/model/validate.py
uv run projects/rvtts_lpc/model/trial_full.py
uv run projects/rvtts_lpc/model/trial_dynamic.py
uv run projects/rvtts_lpc/model/quantize.py --project rvtts_lpc
uv run scripts/create_ai_layer.py --project rvtts_lpc
```

The host reference uses a host Clang compiler and writes its build products to
`tmp/rvtts-reference-lpc/`. On restricted Windows sessions, set `TMPDIR`, `TEMP`
and `TMP` to a writable folder under this repository before running Vela.
