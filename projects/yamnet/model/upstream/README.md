# Official YAMNet source assets

Files in this directory are copied without modification from the pinned public
TensorFlow Models research/audioset/yamnet source, except this note and the
provenance manifest. The source and pretrained weights are Apache-2.0; see LICENSE.
Copyright 2019 The TensorFlow Authors; repository license copyright Google LLC.

The manifest records public source URLs, the exact commit and SHA-256 hashes.
`../fetch_upstream.py` re-fetches only these few files and the original H5 weights.
The H5 weights have not been compiled by Vela. No AudioSet training media or
separately licensed ontology has been copied.

The PyTorch architecture and folded weights in ../model.py are adaptations of
this source. Batch-normalization folding and layout changes are documented there.
