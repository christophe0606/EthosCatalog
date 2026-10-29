# Explicit CPU postprocessing

The graph now separates model execution, CPU interpretation and display:

`YAMNet -> YAMNetPostprocess -> EventLCD`

The network node runs the PTE, checks its output interface and NPU completion,
and publishes its 521 raw sigmoid scores. The CPU postprocessor rejects
nonfinite/out-of-range values, selects the top five, and resolves class labels.
No softmax or temporal smoothing is added. EventLCD receives ordered immutable
label strings and scores; it formats text/percentages, draws and presents.
Neither EventLCD nor its renderer selects classes or reads the class map.

The network keeps its output storage immutable until the postprocessor returns
a matching completion acknowledgment. Only then does it acknowledge the
publisher's in-flight request. Invalid scores cannot release a successful
completion or publish a partially classified frame.

## Verification

Release **b-93**, session **d-94**, uses firmware SHA256
`bae961899bc2d94ead0ad3e0e50df6f3056f5f906080321f0c82d7472bd46ef1`.
The model SHA256 remains
`bd5c2b892839f8d42467f284a448cbc35a2510a4930959ae1dd5e4987deb86b4`.
OSPI storage, quantization and the PTE did not change.

Native tests exercise the actual network/postprocessor/LCD event chain,
immutable input and bounded requests, duplicate requests and wrong
acknowledgments, known class labels, stable tie ordering, unchanged independent
scores, and rejection of NaN, infinity and scores outside [0,1]. Both graph
tests pass, including the requirement that no direct network-to-LCD edge
exists. Generation succeeds from repository root and the project directory.
The rendered graph shows a black CPU postprocessing node between the colored
network method and the brown LCD node.

The rebuilt image was loaded with matching Release symbols. Startup passed
the frontend check (maximum error 4.77e-7), all 40 classification cases and ten
warmed forwards. Classification matches remain 40/40 versus saved PTQ and
39/40 versus original TensorFlow. Strict score parity remains separately
FAIL(-405), as documented for the original port.

An uninterrupted live run reached **20 inference/LCD updates** and **3,264
captured audio blocks**, with 3,262 valid feature frames. Deadline misses,
capture drops, overflows, request errors and pipeline errors were all zero;
45 pending windows were intentionally coalesced. The final forward/NPU sample
was 1550.137 / 60.736 ms. Unused audio/event stack space was 31,824 / 14,176
bytes; method/temp pool peaks remained 829,016 / 298,752 bytes.

CMSIS memory tools report 1,859,056 bytes of MRAM load including padding,
3,108,880 bytes in SRAM0 including both framebuffers, and 258,696 bytes of
DTCM RW state. The application fits without a full-model SRAM copy.

[Raw UART](model/postprocess_live_uart.txt) and
[machine-readable evidence](model/postprocess_validation.json) preserve this
run. The older [live validation](LIVE_VALIDATION.md) and benchmark remain
historical evidence for b-91/d-92 and the same model. The current Release is
left running with the serial port released.
