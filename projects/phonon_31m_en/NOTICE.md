# Third-party notices

The Phonon 31m English trained weights, tokenizer and supplied default voice
come from Gradium's [model repository](https://huggingface.co/gradium/phonon-31m-en),
snapshot `52433c46975bfa13144c56204c9f4ee1c5b148aa`. Its model card explicitly
licenses the weights, tokenizer and voices in that repository under Apache-2.0.
The published q8 checkpoint is the production source; q4 is never downloaded.

The PyTorch architecture adaptation follows Gradium's
[`xn-ptts` v0.4.0 runtime](https://github.com/gradium-ai/xn-ptts/tree/f07d7952db0c6127d5e4bc3f41c82b3304b6b5aa),
including its attribution to Kyutai's Pocket TTS and Mimi codec. The upstream
runtime is dual licensed MIT OR Apache-2.0; this project selects Apache-2.0.
The source files consulted have no individual copyright notices. The exact
upstream Apache license is retained in [LICENSES/Apache-2.0.txt](LICENSES/Apache-2.0.txt).
The pinned model card and config are retained under `model/upstream/`.

The GGUF reader is implemented locally with Python's standard library, NumPy
and PyTorch. No third-party GGUF package, GPL code or q4 implementation is used.
Host validation also uses the already installed Apache-2.0 SentencePiece,
Hugging Face Tokenizers and Safetensors libraries. These host tools are not
firmware components. PyTorch, ExecuTorch, torchao and Vela follow the
repository's existing dependencies and preserve their own installed licenses.

Validation-only original-float weights may be downloaded from the same pinned
Apache-2.0 model snapshot to measure the q8 release's numerical error. They
are not substituted for the q8 production source. All downloaded weights and
compiled caches are excluded from source control by `model/.gitignore`.

## Alif E8 platform code

The Apache-2.0 licensing of the model conversion and new Arm contributions
does not relicense the vendor platform implementation. `phonon_psram.c`
contains a bounded adaptation of the Ensemble 2.2.1 `ospi.c` HyperBus receive
routine, Copyright (C) 2023 Alif Semiconductor. Its file-level SPDX expression
records both applicable licenses. The inherited `ospi_startup.c` notice also
records reused Alif sample initialization. The configured RTE BSP
`ospi_xip_user.h` retains Copyright (C) 2024 Alif Semiconductor and its terms.

Those portions and the existing Alif board/flash adapters remain governed by
the [Alif Software License Agreement](LICENSES/Alif-Software-License.txt),
Rev. 10/2022, from [Alif's official license page](https://alifsemi.com/license/).
It permits source/binary redistribution subject to its conditions, including
retaining notices/terms and use only with Alif products. These portions are
not Apache-licensed. Installed BSP components retain their separate pack terms.
This port targets only Alif E8; it does not claim the whole board implementation
is portable Apache-only software. The unused Alif-derived spectrum UI copy was
removed rather than redistributed as part of this failed experiment.
