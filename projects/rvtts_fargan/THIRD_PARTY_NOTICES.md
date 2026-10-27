# rvTTS hybrid runtime source

The original [rvTTS](https://github.com/ArmstrongSubero/rvTTS) revision is
`b30af35f5366197f1a8845ef55af01a6c7c39a64`, by Armstrong Subero, Apache-2.0.
Its license is preserved in `model/upstream/LICENSE`. The shared text frontend,
dictionary decoder and packed word-vocabulary sources are referenced directly
from `../rvtts_lpc/`; their Apache license and provenance are preserved in that
project's `upstream/LICENSE` and `THIRD_PARTY_NOTICES.md`.

FARGAN's neural and synthesis functions are by Amazon, Mozilla, Octasic and
Jean-Marc Valin under BSD-3-Clause. Selected original files are preserved
unchanged in `model/upstream/`, with their notices and `OPUS-COPYING`.
`model/official_provenance.json` records exact array matches against Opus 1.5.2.
Generated CPU synthesis bodies retain those full notices in
`nodes_impl/fargan_cpu_types.inc`. `prepare_cpu.py` records the one scratch-size
adaptation and the separate int16 weight conversion. New project code is
Apache-2.0; it does not relicense the preserved upstream contributions.

CMSIS-DSP, CMSIS Stream, codec, LCD and peripheral drivers are referenced from
their existing CMSIS packs and project interfaces. Imported libraries and packs
are neither copied into this project nor modified. No GPL code, CH32 vendor
driver or restricted preprint is included.
