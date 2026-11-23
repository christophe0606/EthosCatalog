# DTLN-aec 128 conversion

The two trained stages were reconstructed from the upstream float TFLite
files at commit `9d24e128b4f409db18227b8babb343016625921f`. The upstream
repository supplies the files under its MIT license. Its notice is preserved
in `upstream/LICENSE`, and `upstream/provenance.json` records hashes and URLs.

## Runtime contract

`ai_layer/model.pte` exports `stage1` and `stage2`. Each method takes three
float32 tensors in the order signal, previous state, far-end reference and
returns two float32 tensors in the order output signal and next state.

| Method | Signal and reference | State | Output signal |
| --- | --- | --- | --- |
| stage1 | [1,1,257] FFT magnitudes | [1,2,128,2] | [1,1,257] spectral mask |
| stage2 | [1,1,512] time samples | [1,2,128,2] | [1,1,512] enhanced block |

The state axes are batch, LSTM layer, hidden unit, and hidden/cell selector.
The last-axis selector 0 is hidden state and selector 1 is cell state.
Initialize both method states to zero and copy every next state before the
next invocation. Keep independent states for the two stages. Input and output
signal samples are PCM full-scale fractions. Stage1 magnitudes use the
unscaled forward 512-point FFT. Preserve the microphone phase when applying
the stage1 mask and use an inverse FFT scaled by 1/512. Stage2 output is
added to a 512-sample rolling overlap buffer and emits 128 samples per hop.
The resulting output buffering alignment is 384 samples,
24 ms at 16 kHz. Rectangular windows match the upstream reference.

## Quantization and fidelity

The port quantizes the learned linear operators to int16 activations
and per-channel int8 weights. Spectral features, normalization, gate
nonlinearities and LSTM state updates use float32. The exported hidden
and cell states therefore retain their own precision between hops.
ExecuTorch selects the CPU and Ethos-U85 partitions automatically.

MinMax observers see 2,000 continuous frames of synthetic audio. The
16-second sequence varies level, pitch and echo delays and carries the
recurrent state through talk and silence transitions. The 128-frame
per-method evaluation uses a different seed and supplies the float stage1
signal to stage2. The separate audio regression carries both quantized
stages' own recurrent states and feeds stage1 output directly to stage2.

| Held-out metric | Stage 1 | Stage 2 |
| --- | ---: | ---: |
| Output mean absolute error | 0.00086121 | 0.00014885 |
| Output maximum absolute error | 0.03142327 | 0.00586574 |
| State mean absolute error | 0.00613399 | 0.00765107 |
| State maximum absolute error | 0.38628960 | 0.63264656 |

`conversion_metrics.json` and `network.yml` record these measurements.
`heldout_metrics.json` measures the independent five-second pattern.
The [USB validation](../VALIDATION.md#usb-signal-processing-results)
compares the longer continuous stream and recorded clips against the
original TFLite model.

The fake source formula is in `test_patterns.py` for reuse by the board demo.
Independent benchmark fixtures in `model_test_data.h` and
`method_fixtures.npz` use nonzero signal and reference tensors with warm
states from held-out frame 40. Each expected output comes from the saved
pre-Vela PTQ graph using the same supplied inputs. Start board comparisons
with maximum absolute tolerances of 0.005 for stage1 mask, 0.0005 for stage2
audio, and 0.05 for state. These are proposed numeric checks and require board
verification. Record discrepancies and inspect their cause before changing
the acceptance limits.

## Delegation and memory

The 1,742,288-byte PTE occupies the OSPI slot at `0xC3C00000`. The
real-time target checks the PTE and stages it in SRAM0. Stage1 runs three
Ethos-U85 delegates and stage2 runs five. The generated
`partition_summary.json` lists method buffers and CPU operators.
`vela_report.txt` records the compiler estimates for the eight delegates.

## Reproduction

Run these from the repository root in the configured Python environment.
TensorFlow 2.20 is needed for source TFLite validation and weight extraction.

```powershell
.venv/Scripts/python.exe scripts/create_ai_layer.py --project dtln_aec --quantize
.venv/Scripts/python.exe projects/dtln_aec/model/generate_integer_fixtures.py
.venv/Scripts/python.exe projects/dtln_aec/model/validate_conversion.py
.venv/Scripts/python.exe scripts/validate_networks.py projects/dtln_aec/model/network.yml
```

Keep generated caches and the AI layer together after requantization.

## Integer reference and board fixtures

For the previous quantization policy, the warm-state fixtures in `model_test_data.h` and
`method_fixtures.npz` contain pre-Vela PTQ outputs. The board's integer
arithmetic exceeded their proposed tight limits. An independent integer TOSA
reference run reproduces every observed board discrepancy against those PTQ
outputs. The board matches all 1,793 integer-reference output and state values
exactly in float32 on the captured SRAM-staged run.

| Method/output | Integer vs pre-Vela PTQ maximum error | Mean error | RMS error |
| --- | --- | --- | --- |
| stage1 mask | 0.02390748 | 0.00300229 | 0.00356941 |
| stage1 state | 0.05753945 | 0.00429298 | 0.00925982 |
| stage2 block | 0.00053193 | 0.00010852 | 0.00016801 |
| stage2 state | 0.12853813 | 0.00845585 | 0.01565745 |

`generate_integer_fixtures.py` reads the saved model caches and uses the
deployment compile specification and automatic Ethos partitioner. It captures
each delegate's serialized TOSA immediately before Vela and runs the installed
integer TOSA reference model. The CPU portion executes through PyTorch.
The generator writes `integer_test_data.h` for board checks and
`integer_fixture_validation.json` for cache, graph and version provenance.
Its expected values are independent of board captures. The optional capture
argument adds board comparisons to the report after generating those values.
The repository pins the installed reference tools in
`requirements-arm-tosa.txt` to `tosa-tools==2026.5.0`.

```powershell
.venv/Scripts/python.exe projects/dtln_aec/model/generate_integer_fixtures.py
```

To reproduce the recorded comparison, provide a directory containing
`board_sram_stage1.json` and `board_sram_stage2.json`, each with
`output0` and `output1` arrays.

```powershell
.venv/Scripts/python.exe projects/dtln_aec/model/generate_integer_fixtures.py --board-captures-dir <capture-directory>
```

The integer reference includes the rescaling and nonlinear approximations
introduced by TOSA lowering. These warm-state comparisons establish arithmetic
agreement for the supplied tensors. The continuous fake-source checks verify
signal behavior while propagating the board's recurrent states.

## Numerical findings

On the 30-second regression, a float stage1 followed by the original
quantized stage2 reproduced the near-end collapse on the host. With the
original quantized stage1 and float stage2, near-end gain stayed within
1.3 dB of the clean signal. The old quantizer rounded the packed hidden
and cell tensor on every hop using a single activation scale. It also
quantized the spectral inputs before log-power. Quantizing only linear
operators avoids both sources of rounding while keeping the matrix
multiplications on the accelerator.

The initial full-operation a16w8 PTQ caused substantial echo-only degradation,
with RMS 0.048997 becoming 0.048795. Keeping log and normalization in float32
recovered the synthetic echo cancellation above. This was sensitivity to
quantized normalization, without a compiler crash or an Ethos fault.

Stage1's log power maps silence to 257 equal float values near -16.1181.
Different reduction orders produce small mean errors which are amplified by
the normalization epsilon 1e-7. Float TFLite and PyTorch states can differ by
2.84877 when either signal is zero, even though their nonzero-input mapping
agrees within 0.00002861 with shared states. With XNNPACK enabled the original
five-second reference follows a slightly different trajectory, and the float
audio MAE falls to 1.4e-8. Both silence outputs remain zero. This sensitivity
is documented explicitly because state parity alone cannot establish audio
parity for this network.

[Upstream repository](https://github.com/breizhn/DTLN-aec)
