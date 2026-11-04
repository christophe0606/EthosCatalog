# Model analysis: MIDI-DDSP

**Status: corrected conversion is ready for board benchmarking; final audio
quality and real-time fit remain unverified. Storage target is OSPI.** See
[CONVERSION_REPORT.md](CONVERSION_REPORT.md). The practical method contract is [DEPLOYMENT.md](DEPLOYMENT.md)
and conversion failures/corrections are in
[CONVERSION_ISSUES.md](CONVERSION_ISSUES.md).

The pretrained [MIDI-DDSP model](https://github.com/magenta/midi-ddsp) is one
shared multi-instrument TensorFlow/Keras model for 13 URMP instruments: violin,
viola, cello, double bass, flute, oboe, clarinet, saxophone, bassoon, trumpet,
horn, trombone, and tuba. The pretrained archive is 46.2 MB. The upstream
loader selects `synthesis_generator/50000` and `expression_generator/5000`.
These checkpoints are saved model weights, not optimizer or discriminator
state. The synthesis checkpoint also contains an audio-feature `SynthCoder`
branch; the default MIDI path predicts synthesis parameters directly, so the
audio-only branch may be left out after float-output equivalence is verified.

The model runs at 250 frames/s (64 samples at 16 kHz per frame), predicting
amplitude, 60 harmonic coefficients, and 65 noise bands for CPU DDSP synthesis.
Its note-expression generator is bidirectional across the MIDI note sequence,
then autoregressive for six expression controls. The frame pitch context is
also bidirectional (256 units per direction), followed by autoregressive pitch
sampling. The twenty residual dilated convolutions use global normalization
over time and channels for the complete segment. Exact arbitrary single-frame
causal inference is therefore unavailable. The current faithful schedule uses
1,000-frame/four-second segments, retains both context passes, original sampling
and global statistics, and streams the resulting controls through bounded audio
DSP. Such segments differ from whole-song context and normalization; boundary
quality and buffering latency require validation. MIDI-DDSP models each part
as a monophonic stream, so polyphonic
playback requires stateful voice allocation. Set the voice cap only from a
complete real-time benchmark.

The app MRAM code region is 2 MiB, too small for the published archive. The
verified OSPI path has a 32 MiB model partition and SRAM-resident writable pools.
The corrected reusable-method PTE compiled to 6,218,656 bytes with 38 delegates;
CPU embedding/reverb assets occupy 2,536,448 bytes. Its serialized harmonic
planned buffer is 3,072,000 bytes and maximum reusable delegate scratch is
1,216,000 bytes. These compiler results support an OSPI/banked-SRAM candidate,
not linked memory or board success. Review found and corrected upstream
expression clipping and rest-relative preprocessing omissions, then reran all
39 TF/PyTorch comparisons and all thirteen local PTQ checks successfully.
The long closed-loop validation is still running. PTE hashes, errors and memory
appear in generated reports; no accepted player, real-time timing or voice cap
follows from compilation or synthetic local accuracy alone.

Upstream source declares Apache-2.0. The binary weight branch has no separate
asset notice, so retain provenance and resolve artifact/URMP redistribution
terms before publishing weights. Exclude the optional FluidSynth fallback and
`pretty_midi`: although its Python code is MIT, its bundled `TimGM6mb.sf2` is
GPL-2. Use a project-owned MIDI reader or a GPL-free alternative. The detailed
inventory and source provenance are in
[THIRD_PARTY_NOTICES.md](../THIRD_PARTY_NOTICES.md).
