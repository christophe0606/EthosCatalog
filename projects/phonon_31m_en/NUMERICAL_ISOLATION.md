# First-token target/reference isolation

Status: the compiler-option trial reduces the first-token divergence, but
same-binary repeats are not stable, 2026-10-21. Numerical correctness, full
synthesis and speech-quality acceptance remain pending.

Release b-47/d-48 changes only Portable GELU to `-fno-fast-math`, keeping global
Release `-O3 -ffast-math` and the exact PTE/assets. First-token hidden maximum
error falls from 4.61525488 to **0.000190377235**; last-row KV maximum error
falls from 8.19478226 to **0.000855207443**. Embedding remains exact; every
layer's K/V error is below 0.000856. Complete flash CRC and 64 MiB RAM checks
still pass. This establishes the GELU compilation option as causal for the
observed first-token failure. A narrower per-source
`-fno-finite-math-only` trial gives hidden error 0.0615944676 and is rejected;
the full `-fno-fast-math` option is restored.

Release b-51/d-52 is byte-identical to b-47 (AXF SHA-256
`24f43dd1d73ce5728d73ed68f58058c4adf326f48733415247adca6e7f70b33a`),
but hidden/KV errors become 0.0179824159/0.111280769. Layers 0–3 K/V match
the earlier good run, with the first large recorded K/V error in layer 4.
Flash CRC, the full 64 MiB address-pattern RAM test and NPU call count still
pass. The option change alone therefore does not establish repeatability.
In fresh session d-53, layer 3 MLP1 input/output differ from the host trace
by at most 4/2 int16 steps, and the following GELU quantized input by at most
1 step. That capture has not reproduced the later corruption at this boundary.
Its final errors exactly reproduce b-47. An uninterrupted d-54 first-token
repeat also reproduces those errors; d-52's instability remains unexplained.

The current CLI PTE is SHA-256
`cf07d603198495055f45bdc6eae73d119823c0d196cff65e4bf84c849844fab7`.
The inspected Release is b-44 (AC6, `-O3 -ffast-math`). The first input token is
3248 (`Hello from Phonon.`), absolute position 125, with the complete immutable
voice prefix copied into writable Flow state. Its embedding is exact against
the saved PTQ reference. Final first-token hidden/cache errors are materially
larger than quantization rounding, before autoregressive accumulation.

Run `model/first_token_reference.py` after generating the final saved caches,
startup assets and benchmark fixtures. It checks their provenance and writes
`first_token_reference.h`, `.json` and the ignored small-intermediate trace
`model/weights/first_token_trace.safetensors`. Its traced execution is checked
against normal saved-graph execution. The header supports isolated first-token
checks in the hardware harness. Host graph execution does not emulate Vela.

## Earliest captured divergence

The following comparisons use the saved PTQ first-token trace and target
captures. Delegate numbers are zero based within `backbone`.

| Boundary | Elements | Unequal | Maximum absolute difference |
| --- | ---: | ---: | ---: |
| Delegate 0 QKV input | 512 | 0 | 0 int16 steps |
| Delegate 0 QKV output | 1536 | 41 | 1 int16 step |
| Delegate 1 attention-projection input | 512 | 10 | 2 int16 steps |
| Delegate 1 attention-projection output | 512 | 50 | 1 int16 step |
| Delegate 2 first MLP input | 512 | 60 | 2 int16 steps |
| Delegate 2 first MLP output | 2048 | 35 | 1 int16 step |
| Delegate 3 second MLP input, after CPU GELU | 2048 | 1950 | 33571 int16 steps |

The first eight first-MLP output values are exactly
`[-7, -4, 17, 4, 6, 0, 0, 2]`. The expected next-MLP input starts
`[-7, -4, 16, 4, 6, 0, 0, 2]`; the captured target starts with eight zeros.

Relevant captures under `model/` are `target_first_delegate.json`,
`target_first_delegate_scratch.json`, `target_first_delegate_planned.json`,
`target_attention_input.json`, `target_delegate_1_scratch.json`,
`target_delegate_2_input.json`, `target_delegate_2_scratch.json`,
`target_delegate_3_input.json` and `target_gelu_buffers.json`.

## CPU activation boundary

The planned arena base is `0xA0000000`. For this exact PTE:

| Operation | Planned values and addresses | Scale |
| --- | --- | ---: |
| First MLP NPU output | value 374, int16[2048], `0xA1800000` | 0.00010234076762571931 |
| CPU dequantization | value 375, float[2048], `0xA1A00000` | same |
| CPU GELU, approximate `none` | value 382, float[2048], `0xA1800000` | float |
| CPU quantization | value 384, int16[2048], `0xA1A00000` | 0.00005296894596540369 |

After quantization, only the last 1024 dequantized floats remain intact;
the first 4096 bytes share storage with the int16 quantization output. The
surviving floats differ from the reference by at most one input quantization
step. The entire GELU output should remain live at the next delegate entry.
Instead, its first 4096 bytes retain the first-MLP int16 bytes and its last
4096 bytes are zero. Interpreting those old bytes as float includes nonfinite
values. This explains the following zeros/saturation but does not yet establish
why GELU writes are absent.

Live inspection of the failing b-44 confirms float type, 2048 elements, correct input/output
pointers, and a four-character `none` argument. Argument, resize and dimension
order checks pass. The subsequent b-47 option trial is described above.
A debugger-invoked math function has not been treated as proof of a library
defect, because debugger inferior calls can use a different calling convention.

## Cache and lifetime precautions

External RAM is write-back cached. A raw debugger read can return old backing
memory while the CPU sees a dirty cache line. Explicitly clean the cache
through the existing target cache-maintenance callback before comparisons.
The NPU driver also cleans before its inference-begin hook. The GELU buffer
captures were repeated after an explicit clean.

Do not compare a planned buffer after another output has reused its storage.
For example, the first MLP int16 output and GELU float output share an address;
the int16 values are only independently preserved in the captured NPU scratch.

Injecting the captured first-QKV output into the host PTQ graph produces a
hidden maximum error of 0.00023044 and cache-tail maximum error of 0.00105775.
Executing the lowered EDGE graph on the host produces errors of 0.00017529
and 0.00105793 respectively. These checks support tracing the later CPU
boundary; neither is a substitute for target synthesis validation.

## Final bounded hardware comparisons

The later Shared_Sram 100 MHz run b-55/d-56 repeats the small first-token
hidden/KV errors 0.000190377235/0.000855207443, but first-two-frame maxima
remain hidden 0.377230823, latent 1.67419875, EOS 4.01282406 and PCM
0.108738497. Its two uninterrupted frames average 18483 ms per 80 ms audio.

Dedicated_Sram b-59/d-60 uses unchanged caches, prefix and fixtures with a
separate verified 393216-byte SRAM fast buffer. Full physical CRC/RAM gates
pass, but the first token fails: hidden/KV errors 0.168142885/1.42356992,
with layer-zero K/V errors already 1.13564205/0.216028303. First-two-frame
maxima are hidden 0.319567919, latent 3.18251085, EOS 3.93008041 and PCM
0.234893143. All values are finite. First-token inspection pauses around the
first NPU command; subsequent prefill and frame intervals are uninterrupted.

Dedicated frames average 18459.5 ms, including 18057 ms outside backend
execute intervals. These results fail realtime and numerical acceptance.
They do not isolate a single cause of the remaining errors or prove that the
memory-mode change caused them. Further numerical experiments were deferred
after these bounded feasibility trials. See model/shared_100mhz_result.json,
model/dedicated_100mhz_result.json and MEMORY_MODES.md for exact scope and
matched local snapshots. No UART/audio demonstration is accepted.
