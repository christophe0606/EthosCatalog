# CPU kernels

The first uninterrupted board harmonic call spent 132.416 seconds in total,
including 78.584 milliseconds of U85 work. Its portable CPU normalization
operators were the demonstrated hotspot. These project-owned overrides keep
the original normalization graph, including population variance computed from
centered values, full time-and-channel moments, learned scale/shift and epsilon.
They do not alter the pretrained weights or PTE.

Add `normalization.cpp` and `quantization.cpp` to the project and call
`midi_ddsp_install_normalization(module)` before loading any method. The module
uses its existing local kernel registry; unrelated projects keep their original
registry. Six float32 operators have tight contiguous loops: mean, subtraction,
square, addition, reciprocal square root and multiplication. Mean handles a
contiguous suffix reduction, including the complete time-and-channel extent.
Binary operations handle scalar, identical-shape and repeated channel suffixes.
An additional row-scalar path handles batched moments such as[B,1] over[B,C],
including reversed operands and in-place full-tensor outputs. Interleaved or
incompatible broadcasts retain the original fallback.
Unsupported shapes, non-contiguous layouts, types and exponents use the original
pack functions. No imported library is changed.

`midi_kernel_stats.fast/fallback[6]` records coverage in the order above. Capture
these counters with complete neural outputs and timing on the board: host checks
do not establish target compiler behavior or neural accuracy. Eight independent
float32 mean accumulators change reduction order, so compare with a numerical
tolerance rather than requiring bit identity.

From the repository root:

```text
python tests/validate_midi_ddsp_kernels.py --vcvars <vcvars64.bat> --pack-root <CMSIS-pack-root>
```

The host check compiles the production loops, compares98 numerical cases with
independent PyTorch expressions, checks six suffix and seven row-shape cases, and optionally
compiles the complete ExecuTorch wrapper against pinned 1.4.1 headers. Ordinary
normalization has a 6e-5 absolute tolerance; near-constant and offset 1000 inputs
have separately documented 2e-4/8e-4 tolerances because mean rounding is amplified
by reciprocal standard deviation. `model/normalization_validation.json` records
individual errors and source hashes. Final board acceptance remains separate.

Board build b-22/d-23 exercised every fast path with zero portable fallbacks.
One complete 4,013-call neural schedule took 34.4165 seconds versus the original
262.6213-second baseline; its NPU portion remained 28.3326 seconds. Harmonic
time fell from 132.4183 to 1.7053 seconds. Outputs remained finite and PMU
overflow flags stayed clear. The held harmonic maximum error 0.0286215 still
exceeds its provisional gate, so this performance improvement does not close
the numerical warning or establish final audio quality.

The same local registry contains Short quantize/dequantize overrides. Profiling
the original operators in b35/d36 measured813.32ms quantize and196.13ms dequantize
per full schedule; every observed activation uses zero point zero. The new
`quantization_loops.hpp` processes four elements per MVE vector on a target with
floating-point MVE, with portable scalar loops elsewhere. Quantization keeps
the original float32 reciprocal/multiply, nearest-even rounding and integral
saturation limits. Dequantization keeps exact signed widening and float32
scaling. The wrapper checks static dense shapes, non-overlap, supported types,
scale, limits and rounding mode; unsupported cases retain the pinned kernels.
NaN, infinity or unsafe int64 conversion ranges also fall back.

`midi_quantization_stats[9]` records per-method ticks, calls, elements and fast
or fallback usage. `midi_normalization_stats[9]` records ticks/calls for the six
float operators using the same current-method label. Set
`MIDI_DDSP_NORMALIZATION_TIMING=0` to omit normalization timer reads. The host
check now includes nearest-even ties and adjacent floats at five scales and
three clipping intervals, exact dequantization of all65536 Short values,
23tail lengths and4nonfinite/range guards. The optional `--rtos-include` argument
also compiles the quantization wrapper. Target edge-case tests and complete
neural output/timing comparisons must pass before these loops are accepted.

The corrected vector bit guard passed717target edge cases in b39/d40 with MVE
bitmap3. Full neural outputs/errors matched the baseline. Quantization fell
from813ms to163ms, but explicit MVE dequantization rose from196ms to263ms.
Therefore `MIDI_DDSP_FAST_DEQUANTIZE` defaults to0, retaining the faster pinned
native path. Matched b43/d44 takes4.51751seconds for4seconds of controls, with
all quantize calls fast and all dequantize calls native. This still excludes
CPU audio synthesis/output and does not establish one playable voice. New
batched row-broadcast paths also run on target with zero normalization fallbacks.
The batch16 complete schedule takes at most 2.85898343 seconds in the observed
runs, with all held errors and instrument-0 valid-frame trace matching B1. All
thirteen instrument identities complete; [benchmark results](../model/BENCHMARK.md)
retain the harmonic accuracy warning and startup limitation. Audio DSP/output
costs are still unmeasured.
