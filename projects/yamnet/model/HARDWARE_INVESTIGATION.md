# YAMNet hardware investigation

This report retains unsuccessful trials as well as successful checks. A compiler
export or host comparison does not establish completed inference on the board.

[ETHOS_CONVOLUTION_REPRO.md](ETHOS_CONVOLUTION_REPRO.md) provides the exact
logical operator/weight/output shapes, padding/stride/groups, strongest failed
trials and controls, artifact recovery links and next isolation steps. The
specific failing command has not been proven; the CPU fallback scope must not
be mistaken for a list of individually reproduced failing operators.

## Initial integration and memory experiments

The a16w8 classifier passed original-Keras float parity and host PTQ checks
(CONVERSION.md), and Vela emitted a full delegate. Original OSPI PTE SHA256:
`4d4b522d1d7b3a8a28fddd5b93c648499d8a48d77b57214640c2da455cbeffb3`.
The first OSPI preparation b21/d23 reported ready1/stage5/errors0, but model
programming did not produce a final success response before the short timeout.
The following firmware rejected its model CRC(-311); no OSPI inference occurred.

The compressed model permitted an MRAM experiment. b27/d28 linked ROM1945880B,
leaving approximately151KiB of its2MiB region. Its first NPU invocation stalled
with no completed invocation: STATUS1, QREAD1084/QSIZE10204, interrupts0. Driver
PMU counts kept advancing while the application waited on the NPU semaphore.
CPU fault flags were clear. MRAM EXT attributes default0x04 and normal0x24
(b29/d30) gave the same symptom. A diagnostic full PTEcopy into SRAM with QCONFIG0,
REGION0=0 and ATTR0/1=0x20 (b31/d32) also remained at that position. The SRAMcopy
PTEhash was434da6112355e3dd8be9443a6fe4820adf2bdf79850800dd2c42cdaed6a56782.
These trials did not produce latency or numerical results.

QREAD was near a DEPTHWISE command in the compiled stream. The command queue is
pipelined: this does not prove an earlier convolution completed, which operation
is blocked, or a hardware/compiler defect.

A comparison with working speech projects found they copy the PTE to SRAM and
use QCONFIG2/REGION0=3/ATTR2/3=0x20. b33 built this routing with a copied-buffer
CRC, but it was not run. The user requires direct external-model execution and
avoiding the memory cost of a full SRAMcopy; further SRAMcopy trials were stopped.
The final application must meet that preference.

## Definitive direct OSPI trial

Restored direct pointer model_pte at0xC0000000, QCONFIG2, REGION0=3, REGION1=0,
ATTR2/3=0x24, and the same startup/cache path as handwriting_synthesis and
template_ospi. No full model SRAMcopy is retained. Regenerated original OSPI PTE.
ProgramModel b34/d35 readiness verified1/stage5/errors0 with matching symbols.
The flash tool with60000ms timeout explicitly completed successfully in14s,
programming1576960bytes (including alignment).

Release b36/d37 initially inherited stale ProgramModel debug symbols despite the
Release target selection and updated launch.json. The matching Release session
d38 was confirmed with get_device_info. Firmware's full model CRC passed
(model_verified1,pipeline_error0). A temporary benchmark gate prevented any NPU
call before debugger startup/reset finished. Only after the session was stable
was yamnet_start_gate set1. More than20seconds later no NPU invocation completed:
STATUS1,QREAD1060/QSIZE10012, weights0xC0002810 (directOSPI), arena509360B at
0x02010000. No timing is claimed. This run eliminates the earlier missing-flash
and stale-symbol problems, and avoids startup resets interrupting inference;
it still does not establish which operation or platform issue causes the stall.

## Quantization trials

All-int8 activation PTQ was rejected for numerical fidelity: top1 agreement30%,
mean absolute score error0.00338073 and maximum0.960565 on40 synthetic patterns.
The selected original a16w8 host result was92.5%top1, mean0.000350890,max0.0992098.
A mixed trial keeps ordinary convolutions/head atint16 but sets depthwise blocks
toint8 through quantizer module-name configuration. Its fidelity is also poor:
37.5%top1, mean0.00341040,max0.967294. It is an execution diagnostic only, and must
not be presented as a successful final port. Vela produced one delegate with112
NPUoperators, scratch396.33KiB, PTE1575280B. Prepared model flashed successfully,
with b41/d43 used for direct OSPI execution. Full model CRC passed and the
stable-session gate was released. After more than13seconds, completed
invocations remained0, STATUS1 and QREAD1364/QSIZE12720. Weights were direct OSPI
and scratch405840B. The mixed variant therefore failed both fidelity and
execution; it does not support an a16-only explanation.

## Platform control and equivalent-graph diagnostic

After these stalls, the existing KWS_Release firmware was loaded without
overwriting OSPI. d45 used its matching kws_micronet_l.axf, was continued from
main, and completed270 network cases (maximum1LSB difference), its MFCC check
(0LSB), and speech check (down probability0.9992). Serial printed Test_result:
PASS and live symbols showed kws_self_test_cases270/kws_test_passed1. This
confirms the board can execute the known working int8 depthwise network after
a normal load/reset. No KWS files were changed.

An equivalent diagnostic replaces each depthwise convolution with a group-one
convolution whose off-diagonal weights are zero and whose diagonal contains the
original per-channel3x3 kernel. Biases, strides and padding are unchanged.
This changes compute/storage scheduling, not the mathematical function; the
automatic ExecuTorch partitioner still owns deployment. On the40 original
Keras evaluation fixtures, the float variant max score error was2.68221e-6.
Static a16w8 PTQ retained92.5%top1 agreement, mean absolute error0.0003508665,
maximum0.0992098. Reloaded pre-Vela PT2 reproduced its saved quantized outputs
exactly. The sparse ordinary-convolution graph compiled to one delegate with
PTE3556496B (SHA256
`1ac6f859c85efe8aa8cc86e9046bffd2a195859d07cde68a63d31897a4b18b39`),
encoded weights3543696B, scratch996096B, and329358178MACs. The temp pool was
raised to1179648B for this arena. Vela estimates101217818cycles/253.045ms,
with3.54MB external traffic; these remain estimates. The summary/provenance
files prove command and weight stream equality with this PTE. Model-only
flash explicitly completed successfully (3559424aligned bytes). b49/d51 used
matching Release symbols and a stable launch gate. Full CRC passed, but no
invocation completed: STATUS1,QREAD1060/QSIZE10288, weights0xC0002920,
arena996096B at0x02010000. Replacing depthwise lowering therefore did not fix
the stall. This equivalent representation is retained as a rejected execution
diagnostic, not the final selected model.

## Direct OSPI platform control

The existing handwriting_synthesis PTE3355648B was explicitly flashed after
its ProgramModel preparation passed ready1/stage5/errors0. d55 used matching
Release symbols. At its verified100MHz OSPI setting (AXI400MHz/divider4), the
full CRC passed and all five dense fixtures completed with their recorded
tolerances. Live model_verified1,handwriting_test_passed1,pipeline_error0
confirmed the serial output. Weight addresses covered0xC0000230..0xC032D140.
The live NPU registers were QCONFIG2,REGIONCFG0x13,MEM_ATTR2/3=0x24, matching
YAMNet's expected routing. Thus a similarly sized model can execute directly
from OSPI on this board. No handwriting source/configuration was edited.

A first-convolution-only int8 host trial (remaining layers a16w8) retained
92.5%top1 agreement, mean score error0.000374721 and maximum0.136075.
Its metrics are retained separately. Before deploying that mixed graph, the
original unchanged a16 graph is being retried at the control's guarded100MHz
OSPI setting, isolating the clock change from quantization changes.

Original a16 at100MHz also stalled (b59/d60). CRC1, clock API return0 and
ospi_flash_config.ospi_clock100000000 were verified before opening the gate.
Live QCONFIG2,REGIONCFG0x13,MEM_ATTR2/3=0x24 exactly matched the working
control. After more than20seconds, completed invocations0,error0,STATUS1,
QREAD1060/QSIZE10012. The clock change does not resolve the original stall.

First-conv8/rest-a16 at100MHz also stalled (b63/d64), despite valid CRC and
clock API return0. PTE1574272B SHA256
`a36d28a58b7630c4ccbe469a353b4441f4fcb38511206afcf1636e359daa5736`;
encoded weights1561440B, scratch509360B. Completed invocations0,error0,
STATUS1,QREAD1364/QSIZE10316. Live QCONFIG2,REGIONCFG0x13,ATTR2/3=0x24.

The next original-a16 experiment disables Vela command chaining and forwarding
using the project-owned compile_diagnostic.py wrapper. It calls the normal
automatic ExecuTorch partition/export flow, without editing imported code or
shared scripts. compiler_diagnostic.json binds flags/compiler version to its
generated PTE and pre-Vela cache hashes. This remains a diagnostic until board
execution is validated.

The scheduling-flags trial also stalled (b67/d68): CRC1, completed invocations0,
pipeline_error0, STATUS1,QREAD1060/QSIZE10012, same weights/scratch as original.
Its PTE1575600B SHA256 is
`cac65a40ccb4439e29211d090bf5024270cfec6ed96ac5dea8412b7078b3e792`.
This does not establish a Vela defect; the tested flags did not resolve it.

The next automatic-partition candidate leaves first/spatial depthwise blocks
unquantized (quantizer module configuration None), while pointwise convolutions
and the head retain a16w8. Float operations unsupported by Ethos are left to
ExecuTorch's CPU path, without hand-authored subnetworks. Host top1 agreement
is97.5%, mean score error0.000292900, maximum0.0992098.

Initial b71/d72 forward returned-401 before NPU execution. A source breakpoint
showed result.error_=MemoryAllocationFailed; both allocator peaks were0. The
serialized PTE requires807168B planned storage, allocated from the method pool,
which had been65536B. Increasing the project method pool to1MiB resolved this
concrete allocation failure. No imported runtime changes were made.

b75/d76 then completed14 delegates but stopped on the second fixture because
strict score error exceeded0.005. All finite/range/PMU checks passed. The test
was expanded to40 fixtures plus ten warmed timings, retaining errors instead
of failing before evidence collection. b77/d78 completed all50 forwards.
Each forward completed14 NPU calls without overflow. Median NPU subtotal was
60.735554ms; median full forward1482.136571ms at400MHz. Runtime peaks were
method829016B, temporary298752B. CPU spatial operations dominate latency.

All40 board top classes exactly matched saved pre-Vela PTQ. Top1 agreement with
original TensorFlow was39/40 (37/38 independent); its top class occurred in board
top5 for every pattern. Board/TF mean score error0.000713756,max0.118445352;
board/pre-Vela mean0.000688327,max0.064208984. Strict0.005 parity remains FAIL
(-405), and no bit-exact backend claim is made. These classification results
support proceeding to a documented demo-quality benchmark review. Raw scores,
timings, UART and firmware/model hashes are retained in board_validation_*.
The specific source of backend score drift remains unisolated. No further
model or imported-code changes are hidden by a relaxed score-parity threshold.

## CPU performance follow-up

The fully delegated NPU stall remains unresolved; no new NPU-convolution success
is claimed. Project-owned Helium kernels now accelerate the working CPU float
convolutions: b-100/d-101 median complete forward167.678814ms, NPU60.736304ms,
versus1482.160096ms originally. All20,840 retained board scores are bitwise
unchanged. See [HELIUM_OPTIMIZATION.md](../HELIUM_OPTIMIZATION.md) for source,
dispatch checks, raw measurements, direct OSPI preservation and live results.

All figures above describe synthetic numerical checks or explicit tool results;
no labelled-dataset accuracy or fabricated board benchmark is included.
