# YAMNet conversion report

The conversion and quantization below are unchanged. Current CPU execution uses
project-owned Helium convolutions: median complete forward **167.678814 ms**,
with all retained board scores bitwise unchanged. See
[the optimization report](../HELIUM_OPTIMIZATION.md); older timing figures below
describe the original portable CPU implementation.

The official pretrained TensorFlow/Keras classifier is reconstructed in PyTorch and statically quantized without training. The selected executable candidate uses float32 first/depthwise spatial convolutions on the CPU and a16w8 pointwise convolutions/classifier on Ethos-U85. ExecuTorch partitions the complete graph automatically into14 delegates; no hand-authored subnetworks are used. Saved `.pt2` and `.safetensors` contain the quantized graph before Vela compilation.

## Source and license

The few copied Apache upstream files are pinned to TensorFlow Models commit `c14bf9ad91962cf189f9f58db2132c06247fcd53`. Official15,296,092-byte H5 weights SHA256 is `13c3308955bbfaef262f175ac9c40e47b134573a93984f009220dd7cc12a1744`. `upstream/provenance.json` retains public URLs/hashes and `upstream/LICENSE` retains the Apache-2.0 grant. TensorFlow headers and attribution are preserved. No AudioSet media/ontology, full external checkout, GPL code or training was used. Optional host dependencies are pinned in `requirements-conversion.txt`.

## Functional reconstruction and host numerical checks

Input is float32 `[1,96,64]` natural-log mel magnitudes; output is float32 `[1,521]` independent sigmoid scores, not a softmax. Kernel transposes, grouped depthwise filters, float64 batch-normalization folding (epsilon0.0001), and asymmetric TensorFlow SAME padding are explicit. Host-only1024-element embeddings were compared too.

Original pinned Keras was run on96 calibration patterns and40 evaluation patterns. Maximum PyTorch float score error is2.95043e-6; embedding error1.00136e-5. `float_metrics.json` records versions and per-set checks. Synthetic waveforms include silence, normal/uniform noise, tones, harmonic modulations, decaying bursts and chirps at several amplitudes. Seeds51773/19429 produce38 independently seeded evaluation patterns plus shared silence/440Hz controls. These are numerical checks, not labelled AudioSet accuracy or real-world generalization evidence.

Selected static PTQ leaves first/depthwise modules unquantized and uses per-output-channel int8 weights with symmetric int16 activations for pointwise/head operations. Host error versus original TensorFlow on40 patterns: mean0.000292900, maximum0.0992098; top1 agreement39/40 (97.5%), independent37/38 (97.368%); mean top5-set overlap91%; original top1 in quantized top5 for all40. Reloaded pre-Vela cache outputs reproduce the saved quantized fixtures exactly. `conversion_metrics.json` and `network.yml` retain these figures and limitations.

## Actual hardware results and numerical limitation

Alif DevKit-E8, M55_HP/Ethos-U85-256, CPU/AXI/NPU/timer400MHz, verified OSPI100MHz/divider4. Model bytes execute directly from OSPI, without a full model SRAM copy. Full PTE CRC passed. b77/d78 completed all40 fixtures and ten subsequent warmed forwards, each with14 completed NPU calls, finite scores in[0,1], equal PMU active/total cycles and no overflow.

| Comparison on40 patterns | Mean score error | Maximum score error | Top1 agreement | Top5-set overlap |
|---|---:|---:|---:|---:|
| Board versus original TensorFlow | 0.000713756 | 0.118445352 | 97.5% | 90.5% |
| Board versus saved pre-Vela PTQ | 0.000688327 | 0.064208984 | 100% | 99.5% |

Original TensorFlow's top class is in the board's top five for every pattern. Independent-pattern top1 agreement is37/38. Strict score parity at0.005 **failed** and remains recorded as firmware error-405; it has not been relabeled as a parity pass. Classification agreement supports proceeding with a demo-quality review, while the score drift is disclosed. It does not establish production dataset accuracy or bit-exact backend equivalence. The precise source of fixed-point/backend score drift has not been isolated.

Median ten warmed forwards: **1482.136571ms total**, **60.735554ms NPU subtotal**; CPU operations dominate. The ten measured calls were uninterrupted. A debugger inspection interrupted validation fixture38, so its individual fixture time is excluded from latency aggregation. The full model cannot process each480ms window serially in real time; a live demo must coalesce to the latest complete window and bound pending work.

Runtime peaks are 829,016 bytes for the method pool (including 807,168 bytes of planned tensor storage) and 298,752 bytes for the temporary pool. The original b77/d78 diagnostic reserved 1 MiB / 1.125 MiB. The final benchmark and live application instead reserve **1 MiB / 512 KiB**, both verified on board. The completed application includes both LCD framebuffers; see [LIVE_VALIDATION.md](../LIVE_VALIDATION.md) for its final linked memory and stack measurements.

Raw evidence: `board_validation_scores.npz`, `board_validation_metrics.json`, `board_validation_uart.txt`, and `board_validation_provenance.json` retain every521-score vector, per-case timings, ten warmed timings, exact firmware/model hashes, clocks and limits. The strict failure is preserved separately from the later classification acceptance in `benchmark_release_*` and the final live validation.

## Compiler and rejected experiments

ExecuTorch1.4.1, torch2.13.0, torchao0.18.0, Vela5.1.0, Ethos_U85_SRAM_OSPI/Shared_Sram. Selected PTE3601536B SHA256 `bd5c2b892839f8d42467f284a448cbc35a2510a4930959ae1dd5e4987deb86b4`. Fourteen automatically generated NPU delegates cover quantized pointwise/head work and an input shape operation. CPU operators are convolution, padding, ReLU and quantize/dequantize. Vela counts66850816 NPU MAC-equivalents (including shape/data movement work), maximum delegate scratch298752B and3288064B total encoded weights. Every delegate stream was matched byte-for-byte to the board PTE by `benchmark_vela_provenance.json`;14 CSV rows are retained in `benchmark_vela_summary.csv`. Compiler cycle estimates exclude CPU work and are not measured latency.

The original fully delegated a16 graph had acceptable host metrics but repeatedly stalled with zero completed calls. Valid direct-OSPI/full-CRC/gated-launch trials, MRAM/SRAM diagnostic trials, mixed depthwise8, equivalent diagonal ordinary convolution, first-convolution8 and compiler scheduling flags are documented in `HARDWARE_INVESTIGATION.md`. Queue position is not proof of the failing operation, and no compiler/hardware defect is claimed. Known working KWS and similarly sized handwriting direct-OSPI controls passed. The user's no-full-model-SRAM-copy preference is preserved.

All-int8 PTQ was rejected (30%top1, mean error0.00338073, maximum0.960565). Depthwise8/rest16 was also rejected (37.5%top1, maximum0.967294). Companion diagnostic metric JSON files retain every trial. `MOBILENET_COMPARISON.md` and `MOBILENET_V2_REFERENCE.json` compare the user-suggested official int8 MobileNetV2 reference; that reference does not prove support for YAMNet's int16 spatial lowering.

## Frontend contract

Mono16kHz normalized PCM, periodic Hann400, hop160, FFT512 magnitude (not power), TensorFlow mel257x64 spanning125–7500Hz, natural log(mel+0.001). No DC removal, pre-emphasis, DCT or per-patch normalization. First patch15600samples/975ms; subsequent patches advance7680samples/480ms. Layout time-major96x64 with continuous overlapping frames.

`frontend_tables.h` contains original TensorFlow Hann/mel coefficients (mel stored band-major64x257). `frontend_test_data.h` contains an independent400-sample int16 PCM fixture and64 TensorFlow log-mel values; normalize PCM by32768. `model_test_data.h` retains40 patches and separate original-float/pre-Vela scores. `class_names.h` derives from the pinned Apache class map.

NumPy frontend cross-check maximum differences were0.00547838/0.00330544 log units on calibration/evaluation, mainly nearly empty tone FFT bins. Classifier/PTQ use original TensorFlow features directly. The final CMSIS-DSP frontend passed an isolated frame and three independent overlapping TensorFlow windows with maximum error 4.77e-7 log units at tolerance 0.001. It also checks exact raw-window alignment; see [LIVE_VALIDATION.md](../LIVE_VALIDATION.md).

## Reproduce

From repository root after normal requirements:

```powershell
uv pip install --python .venv/Scripts/python.exe -r projects/yamnet/model/requirements-conversion.txt
.venv/Scripts/python.exe projects/yamnet/model/fetch_upstream.py
.venv/Scripts/python.exe projects/yamnet/model/prepare_reference.py
.venv/Scripts/python.exe projects/yamnet/model/quantize.py --project yamnet
.venv/Scripts/python.exe scripts/create_ai_layer.py --project yamnet
.venv/Scripts/python.exe projects/yamnet/model/validate_conversion.py
.venv/Scripts/python.exe projects/yamnet/model/capture_vela_estimates.py
.venv/Scripts/python.exe projects/yamnet/model/analyze_board_validation.py
.venv/Scripts/python.exe scripts/validate_networks.py projects/yamnet/model/network.yml
```

Default quantizer arguments select the measured float-spatial/a16-pointwise policy and40 firmware patterns. Ordinary cached builds do not require TensorFlow/Keras. Build, flash, serial and debugger actions use CMSIS tools as documented in the [direct OSPI guide](../../../documentation/ospi-in-place-inference.md). The temporary diagnostic start gate was removed. The final Release application starts autonomously, passes classification acceptance and runs continuous CMSIS Stream audio with a landscape LCD; strict score parity remains a separate failure.


## Formal benchmark follow-up

Release b80/d81 passes explicit classification gates autonomously; the manual start gate was removed. Strict score parity remains a separate FAIL(-405), with unchanged disclosed maxima. Temp512KiB is board-verified (peak298752B); method1MiB/peak829016B. Fresh ten-call medians: complete module.forward1482.16009625ms, NPU60.7354775ms. Schema-valid benchmark.yml and benchmark_release_* bind results to the matching binary. Original40-pattern raw scores and strict-failure evidence remain in board_validation_*. See [CHECKPOINT.md](../CHECKPOINT.md) for the completed application status.

## Completed live application

The final CMSIS Stream application is now implemented and board-validated.
See ../LIVE_VALIDATION.md for b-91/d-92 firmware provenance, exact streaming
frontend checks, 40-case startup classification, continuous capture and LCD,
bounded coalescing, memory/stack measurements and remaining score limitations.
The selftest implementation now lives in ../yamnet_selftest.cpp. The final
application starts autonomously and remains running directly from OSPI.

