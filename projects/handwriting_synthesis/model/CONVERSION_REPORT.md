# Conversion report: pretrained handwriting synthesis

## Result

Conversion and host validation succeeded; ready for hardware benchmarking.
The original trained checkpoint was converted to PyTorch, post-training
quantized to Ethos-supported **a16w8**, and compiled by ExecuTorch/Vela into
`../ai_layer/model.pte` with **five genuine Ethos-U85 delegates**. No training
or substitute model was used. Firmware/board accuracy and timing remain to be
measured; this report does not claim a completed on-board demo.

The complete original model has 3,632,431 inference parameters. The PyTorch
float weights are in `handwriting_float.safetensors`. Each of five methods has
an exported quantized `.pt2` and companion `.safetensors` cache, created **before**
Vela. The PTE is 3,355,648 bytes at OSPI 0xC0000000 inside the 32 MiB partition.
`../ai_layer/generation.json` binds the network settings, all graph/tensor
caches and PTE hashes. `ADAPTER_CONTRACT.md` defines the full recurrence.

## Provenance and license

Original author: Sean Vasquez (`sjvasquez`).
Source: <https://github.com/sjvasquez/handwriting-synthesis>, pinned revision
`5f58984c3bc793bd8930bad7012cd2008d4a4a66`.
Checkpoint: `model-17900`, 43,589,184-byte data shard plus 1,240-byte index.
The four retained original files and SHA-256 hashes are in
`original/manifest.json`; `fetch_original.py` restores/verifies exactly those
files. No full upstream checkout, dataset or historical dependency installation
was used. Original files remain verbatim. The user-directed missing-license
exception and dependency notices are recorded in `../THIRD_PARTY_NOTICES.md`.
The original work has no stated reuse license and is not claimed Apache-licensed.

## Tensor mapping and float equivalence

`checkpoint_reader.py` reads the public uncompressed SSTable/protobuf wire
formats, checks block CRC32C and tensor CRC32C, and rejects unsupported formats.
It extracts raw inference kernels/biases; RMSProp slot tensors and counters are
excluded. The checkpoint contains no EMA inference tensors; upstream demo
constructs its model with parameter averaging disabled.

TensorFlow `[inputs,outputs]` kernels are transposed into PyTorch Linear
`[outputs,inputs]`; biases are copied exactly. TF LSTM i,j,f,o gate order,
forget bias 1, old/new attention-window dependency order, softplus attention,
kappa/25 increment, beta clipping, null character, and GMM parameter order are
preserved. TensorFlow1.6 LSTM defaults were checked against its
[official source](https://raw.githubusercontent.com/tensorflow/tensorflow/r1.6/tensorflow/python/ops/rnn_cell_impl.py).

**The original TensorFlow runtime was not executed.** `validate_float.py`
independently implements the original source equations using NumPy and original
checkpoint arrays, and compares 300 identical forced-input steps (seed 191,
`Independent numerical reference.`) against the PyTorch orchestration.
Maximum GMM-logit absolute error was 2.4796e-5; maximum recurrent-cell state
error 3.8147e-6; maximum decoder-distribution error 4.0984e-5. Exact per-state
results are in `float_equivalence.json`. This checks source-equation/checkpoint
mapping, not bitwise equivalence of TensorFlow random sequences.

## Post-training quantization and error

Int16 symmetric activations (-32767..32767), per-output-channel int8 symmetric
weights, quantized biases and float method boundaries. No training or QAT.
Full MinMax observers use all 4,861 float-generated recurrent steps from eight
calibration sentences, seeds 101..108, bias 0.5, with a 50% activation-range guard.
The text and trajectory inputs are synthetic; no IAM training/test dataset was
copied. Initial HistogramObserver clipping produced rare large errors and was
replaced by MinMax; only the final MinMax artifacts are retained.

Evaluation uses three **different sentences**, seeds 801..803, bias 0.5, and 775
sampled forced-input steps per method drawn every third step from the float
trajectories. Compare the saved pre-Vela quantized graph with float on identical
inputs. These are raw gate/logit units, not pixel errors or recognition scores.
Normalized RMSE is 100*RMSE/sqrt(mean(float_output^2)).

| Method | MAE | RMSE | Max absolute | Normalized RMSE |
| --- | ---: | ---: | ---: | ---: |
| lstm1 |0.0030033|0.0047503|0.13534|0.4070%|
| attention |0.0037006|0.0052248|0.07278|0.1834%|
| lstm2 |0.0045730|0.0067351|0.23329|0.4213%|
| lstm3 |0.0044124|0.0065211|0.28850|0.3834%|
| gmm |0.0068936|0.0128611|0.12800|0.4403%|

`conversion_metrics.json` and `network.yml` store exact conditions and values.
`model_test_data.h` supplies actual held-out input and expected quantized output
for every dense method, output LSBs, and four-LSB board tolerances. Those tolerances
compare host quantization to Vela fixed-point output; they do not bound float
model error. Hardware checks are required after compilation.

Closed-loop generation is also run independently through float and quantized
models with the same sentence/seed. Because distributions and categorical
choices change, matching complete trajectories point-for-point would be invalid.
All three quantized trajectories stayed finite and produced legible handwriting
on visual review of paired previews. Text output accuracy was not scored by OCR.

| Sentence | Float points | a16w8 points |
| --- | ---: | ---: |
| This is a test. |287|320|
| Animated handwriting |594|607|
| A new sentence replaces the old one. |1440|1025|

See `conversion_samples.svg` and paired `sample_0.npz` through `sample_2.npz`.
The first quantized sample has an extra trailing stroke; the third float sample
reaches its 40*length budget with trailing scribble. These are stochastic
termination/quality limitations also present in the original mechanism, and
are not evidence of an OCR accuracy guarantee. Representative text is readable;
no held-out human handwriting recognition dataset was evaluated. Style priming
is not part of this UART demo; unprimed sampling is an original supported mode.

## Compiler and memory

ExecuTorch 1.4.1, Vela 5.1.0, target `ethos-u85-256`,
`Ethos_U85_SRAM_OSPI`, `Shared_Sram`, repository Vela INI.
Each dense method lowers to **one fully delegated NPU operation**, zero
nondelegated matrix operations. CPU boundary operators are only quantize and
dequantize; CPU recurrent gates, attention and sampling are application code.
Total dense work per stroke is 3,627,480 MACs.

| Method | PTE nonconstant planned bytes | Vela SRAM scratch |
| --- | ---: | ---: |
| lstm1 |9600|4.06KiB|
| attention |2864|1.00KiB|
| lstm2 |9600|4.84KiB|
| lstm3 |9600|4.84KiB|
| gmm |2400|1.03KiB|

PTE planned buffers total 34,064 bytes if all methods retain separate arenas;
max per method 9,600 bytes. `pte_metadata.json` records actual serialized method
metadata. Vela scratch, method metadata/heap, CPU state (~9,932 bytes), decoder
scratch, UART/stroke buffers, RTOS and LCD buffers are additional; these are not
final firmware RAM figures. Two 800x480 RGB565 framebuffers need 1,536,000 bytes.
Build artefact tools and board benchmarking must confirm combined fit.

Do not report the sum of five dense-call times as full inference time: a full
stroke step adds CPU gates/window/sampling, and a sentence contains hundreds or
thousands of steps. See adapter contract benchmark units.

## Reproduce

From the repository root using its configured Python environment:

```powershell
.venv/Scripts/python.exe projects/handwriting_synthesis/model/fetch_original.py
.venv/Scripts/python.exe projects/handwriting_synthesis/model/validate_float.py
.venv/Scripts/python.exe scripts/create_ai_layer.py --project handwriting_synthesis --quantize
.venv/Scripts/python.exe scripts/validate_networks.py projects/handwriting_synthesis/model/network.yml
```

Validation completed: original CRC/SHA checks, float equation comparison,
PTQ held-out dense error measurement and closed-loop sampling, visual preview,
network schema, Python syntax, actual five-method Vela lowering. No board access
or firmware builds were performed by the conversion stage.
