# Silero VAD conversion

## Result
Successful fixed-shape PyTorch PTQ export and automatic ExecuTorch/Ethos-U85 lowering.
PTE:393,504bytes; two Ethos delegates. CPU operators: Portable pow (magnitude
square root) and quantize/dequantize. No manually separated subnetworks.

## Source and license
Pinned https://github.com/snakers4/silero-vad commit
`066409f3d9db45a30d21dfcbf9147826ffdd7052`. The original MIT license is preserved
in `upstream/LICENSE`; checksums and original paths are in `upstream/provenance.json`.
The current official TorchScript model supplies the16k branch weights. The separate
16k safetensors distributed at that commit is a different weight snapshot
(conv1 maximum absolute weight difference4.0274); it was inspected, then removed.
The PyTorch reconstruction follows upstream architecture and expands LSTMCell
algebraically to fixed-shape linear/sigmoid/tanh operations. No training occurs.
The original tinygrad architecture file is retained as a source reference; tinygrad
is not installed or needed. Only PyTorch, NumPy and the repository export stack
are used; optional audio/FFmpeg/torchcodec dependencies are not used.

## Contract
- Method `forward`: float32 inputs audio[1,576], h[1,128], c[1,128].
- Audio is mono16kHz signed PCM normalized by32768, with64 prior samples followed
  by512 new samples. At startup prepend64 zeros. Preserve the last64 new samples
  for the next call; reset context/state on capture discontinuity.
- Returns probability[1,1], next_h[1,128], next_c[1,128], all float32.
- State is zero initially, propagated every32ms. Probability>=0.5 means voice.
- Internals use int16 activations and per-channel int8 weights. Outputs remain
  float boundaries and all backend boundaries are chosen by ExecuTorch.

## Validation
On937 continuous frames from the first30s of the pinned upstream60s PCM16 WAV,
float probability and h/c maximum absolute error versus upstream JIT is exactly0.
Calibration uses those937 frames plus startup and signed tone amplitude cases.
Evaluation uses the last30s (937frames), with independent state trajectories for
float and quantized models:
- Mean probability absolute error:0.0226897455.
- Maximum probability absolute error:0.5641487017.
- Threshold0.5 disagreements:18/937 (1.921%); agreement98.079%.
- Float positive frames:713/937.
These are fidelity measurements against upstream, not ground-truth VAD accuracy;
the upstream WAV has no labels here. Full accuracy/product validation remains outside this demo.

`model_test_data.h` replaces the gain template with one zero-state silence vector
and one warm-state speech vector, including explicit audio,h,c and all expected
PTQ outputs. Replay the supplied h/c for each independent benchmark repetition.
Expected probabilities are0.0004880567 and0.9978319407. Initial board tolerances
are0.04 probability and0.12 state absolute error; benchmarking must record actual
observed differences and must require actual NPU PMU activity. No board timing is
claimed at conversion stage. Template benchmark records were removed.

## Reproduction
From repository root, using the configured Python environment:

```powershell
.venv/Scripts/python.exe projects/silero_vad/model/quantize.py --project silero_vad
.venv/Scripts/python.exe projects/silero_vad/model/generate_test_data.py
.venv/Scripts/python.exe scripts/create_ai_layer.py --project silero_vad
.venv/Scripts/python.exe scripts/validate_networks.py projects/silero_vad/model/network.yml
```

Original weights, architecture, license, WAV and provenance are project-owned
small upstream inputs. No other local project or installed dependency source is
modified. The generated layer owns only this project.
