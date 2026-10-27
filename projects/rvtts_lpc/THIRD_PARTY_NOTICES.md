# rvTTS LPC runtime source

Selected portable files in `upstream/` and dictionary assets in `model/upstream/`
come from [rvTTS](https://github.com/ArmstrongSubero/rvTTS), commit
`b30af35f5366197f1a8845ef55af01a6c7c39a64`, by Armstrong Subero.
The supplied Apache-2.0 license is retained in `upstream/LICENSE` and
`model/upstream/LICENSE`. Existing source notices are preserved.

`LPC_x86_Source/` supplies the frontend, normalization, fallback reciter,
duration model, pronunciation lookup and LPC vocoder. The portable
`TTS_DEMO_DUO/src/pron_dict_packed.c` (retained as `pron_dict_packed_v2.c`)
decodes the supplied PDK2 blob. Its matching header is `pron_dict_v2.h`;
only the include spelling changes. Both original decoders are reference-only.
`nodes_impl/pron_dictionary.c` derives from the x86 decoder, accepts PDK2 and
uses its direct bucket offset index. It preserves exact lookup data; every
entry and bucket boundary is independently checked by `validate_runtime.py`.
`TTS_DEMO_DUO/src/word_vocab_packed.c` supplies the packed word-embedding lookup;
its matching public header is retained as `word_vocab_packed.h`. The original
filesystem-based vocabulary backend and host `main.c` are reference material
and are excluded from the firmware build. `word_vocab.h` adds declarations for
the packed initializer without changing lookup semantics.

No CH32 platform driver, vendor peripheral file, GPL source or restricted
preprint is included. Alif drivers are referenced through installed CMSIS packs,
under their supplied license, and are not copied or modified here.
