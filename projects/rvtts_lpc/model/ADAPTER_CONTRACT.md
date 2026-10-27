# Shared inference adapter contract

All method boundaries use contiguous float32 tensors. Methods are persistent
and loaded once; use separate `load_method`/`execute` calls on the same module.
The original blobs supplied as OSPI assets hold input embeddings and norm data;
the neural projections and convolutions execute from the quantized PTE.

| Method | Input shape | Output shape | Calls for N frames |
| --- | --- | --- | --- |
| prosody_pre | [32,184] | [32,64] | ceil(N/32) |
| prosody_gru | [1,256] | [1,128] | N |
| prosody_head | [32,128] | [32,6] | ceil(N/32) |
| acoustic | [1,100,170] | [1,32,17] | ceil(N/32) |

Build 184 prosody input values exactly as `upstream/prosody_model_int8.c` does,
including previous/next distinct phone runs, duration, utterance positions and
word embeddings. The frame rate is 100 Hz; one feature frame feeds 160 mono PCM
samples at 16 kHz. For the last pre/head block, pad unused rows with zero and
ignore their outputs.

Store all N pre outputs before scanning. Zero both GRU states at each utterance.
For scan step t, the `prosody_gru` input is `[pre[t], pre[N-1-t], fw_state,
bw_state]`, each component 64 values. The first 64 output values are the new
forward state and the last 64 the new backward state. Store them at t and
N-1-t respectively. Head inputs concatenate the saved forward and backward
state for each original frame. The head output contains physical energy,
pitch, voicing, then three clamped raw log variances. Temperature zero uses
the first three values directly. Optional sampling follows the original
normalization and log variance formula outside the neural graph.

Build the acoustic 169-value input exactly as the upstream source does,
normalizing the three conditioning values with acoustic norm statistics.
For a tile starting at frame s, copy frames s-34 through s+65 into positions
0–99. Only valid utterance frames receive data and validity channel 169=1;
all positions outside the utterance contain 170 zeros. Do not copy model input
from a different tile or adjacent utterance. The method masks every hidden
layer outside the utterance and returns only its center 32 frames. Ignore final
outputs beyond N. Copy the 17 outputs to feature dimensions 1–17 and conditioning
energy/pitch/voicing to dimensions 0/18/19.

`run_methods()` in `model.py` is the executable reference for scan and tile
assembly. `model_test_data.h` has independent held-out method fixtures and
boundary tolerances for board validation. `quantized_fixtures.npz` has the same
inputs/outputs in a host-readable form. Complete synthesis timing must include
input construction, scan, head, tile assembly, vocoder, PCM conversion and
required state copies. Report playback, UART and LCD costs separately when they
are excluded from the inference interval.
