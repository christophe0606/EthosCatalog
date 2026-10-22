# Handwriting-synthesis original-work notice

Original project and pretrained model: **Sean Vasquez (`sjvasquez`)**,
<https://github.com/sjvasquez/handwriting-synthesis>.
Pinned revision: `5f58984c3bc793bd8930bad7012cd2008d4a4a66`.

No stated reuse license or separate copyright notice was found for the original
repository source and pretrained checkpoint at that revision. Attribution here
preserves the identified author; no copyright ownership or license grant is
inferred. This original work and derived model weights are **not represented as
Apache-2.0 licensed**. Absence of a license does not mean absence of copyright.
The user explicitly directed this local port to continue with that fact recorded.
This does not supply an upstream rights grant.

`model/original/rnn.py`, `model/original/rnn_cell.py` and the two checkpoint files
are retained verbatim. `model/original/manifest.json` records their URLs, lengths
and SHA-256 hashes. Unrelated demo/song-lyric examples, training data, style
samples, and original dependencies are not retained. TensorFlow is not installed
or invoked by this conversion. New project-owned conversion and firmware code
uses Apache-2.0 headers; those headers do not relicense the original model.

The existing conversion environment uses PyTorch (BSD/Apache/MIT family),
ExecuTorch 1.4.1 (BSD-3-Clause), torchao 0.18.0 (BSD-3-Clause), NumPy 2.5.4
(BSD-3-Clause with permissive bundled notices), safetensors 0.6.2 (Apache-2.0),
PyYAML 6.0.3 (MIT), and ethos-u-vela 5.1.0 (Apache-2.0). Their installed package
license files were inspected. No original historical TensorFlow training
requirements or new dependency packages were installed. These dependencies
remain supplied through the repository environment/packs and retain their
own notices; no dependency source files are copied into this project.

The model follows Alex Graves, *Generating Sequences With Recurrent Neural
Networks*, <https://arxiv.org/abs/1308.0850>. This citation does not substitute
for the missing original implementation/checkpoint license.
