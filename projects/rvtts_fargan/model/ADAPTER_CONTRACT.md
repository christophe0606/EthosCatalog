# FARGAN adapter contract

The four shared methods have the same interface as
`../../rvtts_lpc/model/ADAPTER_CONTRACT.md`. This project owns copies of their
uncompiled quantized caches and of the original embedding/norm assets.

| Method | Inputs | Output |
| --- | --- | --- |
| fargan_init | float32 [1,32], int64 [1,44], zero float32 [1,964] | float32 [1,964] |
| fargan_frame | float32 [1,996], int64 [1,44] | float32 [1,1124] |

For each synthesis call, apply the original wrapper's pitch correction to feature
dimension 18: subtract the call's frame mean, divide by population standard
deviation (minimum 1e-5), clamp to [-2.5,2.5], then multiply by 0.3615 and add
0.2120. Preserve the other 19 features. The period is
`floor(0.5 + 256 / 2**(pitch + 1.5))`, with a minimum of one.

The 32 input values are the 20 features followed by the 12-value pitch embedding
at index `clamp(period-32,0,223)`. The generated `fargan_params.h` holds that
embedding. `frame_input()` is the host reference. The 44 pitch indices start at
`256-previous_period-2`, increment each time, and subtract the previous period
when they reach 256. Clamp negative indices to zero. `pitch_indices()` is the
host reference. Use the first frame's period for initialization and the first
frame; subsequently use the preceding frame's period.

Pass 964 zero float values to init for its initial state. Call init once, then concatenate each 32-value input with the returned state to
call frame. The frame returns 160 raw excitation samples followed by its new
964-value state. The state layout is pitch history 256, conditioning history
128, convolution history 164, and GRU states 160/128/128. Do not reset state
between frames. No graph is divided into hand-assigned CPU and NPU subgraphs;
the ExecuTorch partitioner chooses placement for each complete neural method.

Apply output de-emphasis `y[n]=excitation[n]+0.85*y[n-1]` with one persistent
float memory per synthesis call. Clip/round the final waveform to PCM16. Preserve
the source wrapper's 80-sample initial delay removal and two repeated final
feature frames for flushing; its final frame contributes only 80 samples.
Consequently N input frames produce (N+1)*160 PCM samples in the source wrapper.
If the text pipeline splits long utterances into independent synthesis calls,
reset initialization/de-emphasis and calculate pitch statistics for each call.
The conversion fixtures compare raw complete neural frames before that output
trimming, with de-emphasis applied. A frame represents 10 ms at 16 kHz.

`model_test_data.h` includes held-out fixtures for all six methods. These
fixtures validate individual calls; complete synthesis tests must also check
feedback state, pitch correction, flushing and PCM conversion. Synthetic
fixtures cannot establish audible speech quality.
