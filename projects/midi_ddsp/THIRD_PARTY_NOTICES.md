# MIDI-DDSP third-party provenance

The `model/reference/` files retain their upstream copyright/license notices.
They are selected architecture reference files from
[magenta/midi-ddsp](https://github.com/magenta/midi-ddsp) and the neural layer
implementation `ddsp/training/nn.py` from
[magenta/ddsp](https://github.com/magenta/ddsp). These sources are Apache-2.0;
the license is reproduced in `model/reference/LICENSE`. They are not installed
as a Python package and do not import any original dependency in the converter.

The CPU DSP oracle also includes the unmodified `ddsp/core.py` and its license
under `dsp/reference/`, pinned to DDSP commit
`676aa9d8016e9b0e962d02cb9133081439e4a435`. Its hashes and comparisons are recorded
in `dsp/` provenance and `dsp/reference_validation.json`. This is a separate
two-file Apache-2.0 reuse; the original DDSP package/dependency tree is not
installed. The project-owned NumPy DSP oracle is in `dsp/reference_synth.py`.

`model/model.py` reconstructs the inference equations in PyTorch, following those
sources and the original TensorFlow checkpoint names. `model/tf_reference.py`
independently executes the checkpoint through TensorFlow/Keras operations to
check the framework conversion. No training, distilled network or replacement
instrument model is used.

The original pretrained archive is published by the upstream repository at
<https://github.com/magenta/midi-ddsp/blob/models/midi_ddsp_model_weights_urmp_9_10.zip>.
It is 48,429,473 bytes with SHA-256
`46bae44c16c3399c24f8f0ba2557fc0432bc12aa23088606d465b22fec2cd05e`.
The importer selects the expression checkpoint `expression_generator/5000` and
the MIDI inference/reverb tensors of `synthesis_generator/50000`. It omits the
audio-analysis SynthCoder weights, which are not invoked by MIDI inference.
The learned reverb is preserved as an asset in the imported float tensors; DSP
implementation and validation remain separate from the neural export.

The repository declares Apache-2.0 for its sources. The binary archive has no
separate model license notice. Local conversion/evaluation is being performed;
asset redistribution terms remain unresolved as recorded in `model/ANALYSIS.md`.
The upstream URMP dataset is not included or downloaded.

The converter uses the already installed PyTorch, TensorFlow, NumPy,
safetensors and ExecuTorch environment. The independent host score-feature
oracle in `player/` uses already-installed pandas (BSD-3-Clause) to execute
AST-selected upstream dataframe helper functions. No new dependency tree is
installed. The conversion does not install or include
`pretty_midi`, its GPL SoundFont, FluidSynth, CREPE, librosa, note-seq, music21,
or the original training/analysis package and its dependency tree.

`model/import_report.json` records each imported tensor shape and SHA-256 hashes
of the downloaded reference source bytes. Reference source version pinning is
currently by those exact content hashes; URLs refer to the public repositories.
