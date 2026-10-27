# LPC CPU optimization

`lpcvoc_optimized.c` is a project-owned derivative of rvTTS's Apache-2.0
LPC vocoder. The imported implementation and tables remain unchanged in
`../upstream/`; `lpcvoc_reference.c` builds that source under separate symbols
for numerical and timing comparisons.

The 49-tap pulse-dispersion FIR uses overlap-add: a nonzero excitation pulse
adds its weighted future contributions to the existing state ring. Each sample
then consumes and clears one ring slot. This computes the same convolution
while avoiding a complete circular dot product at every sample. Four F32 lanes
use Helium FMA when the target implements MVE floating point; bounded scalar
tails handle the split at the ring boundary. The state structure has the same
size, but its dispersion ring has new internal meaning, so a live state must
not be switched between implementations.

The inverse DCT and autocorrelation sums use CMSIS-DSP 1.18.0
`arm_dot_prod_f32`. That release's MVE tail performs full-width loads, so the
wrapper passes only a multiple of four and handles the final elements itself.
The recursive synthesis/postfilter, noise generator, gain and pitch models
retain their source behavior.

`validate_dsp.py --dsp-root <installed CMSIS-DSP release>` compiles the native
scalar CMSIS dot and compares complete utterances with the original source.
The 12 cases cover 2 to 1,256 frames, recurrent continuation and pitch/voicing
extremes. `host_validation.json` reports a maximum difference of one PCM count
and at least 90.64 dB SNR on cases with a nonzero difference. The predeclared
acceptance limits are 64 PCM counts maximum and at least 60 dB SNR.

`lpcvoc_check.cpp` repeats the source comparison with the actual target MVE
implementation before the demo starts. It times one warmup and three runs of
157 frames for both implementations, reports median ticks and rejects the
demo if the same numerical limits are exceeded. Startup fixture timings do
not include UART, LCD, text processing, feature inference or I2S playback;
complete demo measurements are recorded separately.

The feature engine also copies the 528,144-byte ExecuTorch PTE from OSPI into
an aligned SRAM0 buffer at startup, checks the copied CRC, and loads its four
methods from that buffer. Both command streams and weights use the U85 SRAM
AXI ports (`NPU_MEM_ATTR_2/3=0x20`; Arm 102685 r0p0 pp.59,63). The existing
board driver cleans CPU-written memory before NPU ownership and completes the
required barriers. The PTE bytes and quantization are unchanged; the original
OSPI-based Vela schedule is conservative for SRAM. The project AI layer is
regenerated when changing storage configuration. Board timing of this storage
change is recorded separately from the CPU vocoder speedup.

Optimization guidance came from the repository's Helium wiki pages
`patterns-cmsis-dsp-helium-migration`, `techniques-helium-vector-reductions`,
`techniques-helium-predication-and-tail-handling`, and
`techniques-profile-guided-helium-optimization`. The selected CMSIS-DSP release
and the physical board measurements determine the implementation and its
performance claims.
