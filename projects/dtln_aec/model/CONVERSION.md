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

The port uses int16 activations and per-channel int8 weights for learned
affine and recurrent computation. `InstantNormalization` and `LogPower`
retain float32 through the quantizer's module exclusions. ExecuTorch selects
the CPU/NPU partition boundaries automatically. No imported package is edited.
Calibration uses 192 synthetic recurrent frames and MinMax observers.
Held-out per-method evaluation uses 128 frames with a different random seed.
The stage2 per-method error feeds float stage1 output, while each quantized
method propagates its own recurrent state. The full pipeline evaluation feeds
quantized stage1 output into quantized stage2 and propagates both states.

| Metric | stage1 | stage2 |
| --- | --- | --- |
| Float shared-state maximum error with both signals nonzero | 0.00002861 | 0.00001526 |
| PTQ output mean absolute error | 0.00173218 | 0.00016711 |
| PTQ output maximum absolute error | 0.0836005 | 0.00405368 |
| PTQ state mean absolute error | 0.0246789 | 0.0117512 |
| PTQ state maximum absolute error | 2.24620 | 1.13061 |

`conversion_metrics.json` and `network.yml` record these measurements.
`heldout_metrics.json` compares an independent five-second fake source with
silence, echo-only, double talk, near-end-only, and silence sections. It omits
250 ms at each section boundary for the section RMS metrics and compares the
whole signal for global error. The builtin TFLite reference runs with default
delegates disabled so its execution path is reproducible. Float global audio
MAE against this reference is 0.00017957 and PTQ MAE is 0.00263472.
Echo-only input RMS 0.048997 becomes 0.017576 after PTQ. Double-talk residual
RMS is 0.010827. These synthetic checks establish numeric and demo behavior.
Speech-quality scores require appropriate recorded and labelled audio.

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

The PTE is 1,763,792 bytes and occupies the OSPI slot beginning at
`0xC3C00000`. `stage1` has one Ethos delegate and `stage2` has two.
Their planned method tensor buffers are 7,232 and 12,304 bytes respectively.
Runtime metadata, allocator alignment and NPU scratch add to these sizes.
The Vela summaries for the three delegates report the following compiler
estimates. These require independent board timing and allocator measurements.
`vela_report.txt` preserves the matching compiler console summaries.

| Delegate in execution order | SRAM KiB | Encoded constants KiB | MACs | DRAM traffic MB/frame |
| --- | --- | --- | --- | --- |
| stage1 learned/recurrent | 5.05 | 475.70 | 496770 | 0.47 |
| stage2 shared encoder | 249.19 | 251.19 | 524288 | 0.48 |
| stage2 learned/recurrent/decoder | 8.00 | 952.39 | 1053184 | 0.93 |

Use allocator high-water marks for pool sizing. CPU floating normalization
and log operations remain in the exported methods. The shared encoder is
automatically delegated for both signals and its constant storage is shared.

## Reproduction

Run these from the repository root in the configured Python environment.
TensorFlow 2.20 is needed for source TFLite validation and weight extraction.

```powershell
.venv/Scripts/python.exe projects/dtln_aec/model/quantize.py
.venv/Scripts/python.exe scripts/create_ai_layer.py --project dtln_aec
.venv/Scripts/python.exe projects/dtln_aec/model/validate_conversion.py
.venv/Scripts/python.exe scripts/validate_networks.py projects/dtln_aec/model/network.yml
```

Keep generated caches and the AI layer together after requantization.

## Integer reference and board fixtures

The original warm-state fixtures in `model_test_data.h` and
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
