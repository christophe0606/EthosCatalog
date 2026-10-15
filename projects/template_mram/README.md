# MRAM microphone spectrum template

The onboard PDM microphone feeds 512 signed mono samples at 48 kHz to a quantized
Ethos-U85 network implementing `y = 2*x`. A 2048-sample window advances by 512
samples (75% overlap). Independent DC removal, Hann window, FFT and magnitude
stages produce a calibrated one-sided spectrum. The network preserves negative
samples and has no ReLU or bias. Its float interface quantizes the normalized
PCM range [-1, 1]; output range is approximately [-2, 2].

Capture enables only `ARM_PDM_MASK_CHANNEL_4`; channel 5 is disabled. The board
driver writes one 16-bit sample for each enabled channel, so these buffers contain
mono PCM, with no left/right interleaving. `PCMToFloat` therefore produces mono
audio for the gain and FFT stages. If stereo capture is enabled later, represent
each left/right pair as one stereo sample and add an explicit stereo-to-mono
node before the mono processing chain.

`app.py` describes the CMSIS Stream graph. `hardware_params.cpp` owns the
persistent model pools, NPU-visible input and LCD buffers. `app_main.cpp` checks a
signed ramp against the expected gain and requires actual Ethos PMU activity
before starting capture. `gain_test_passed == 1` records that check.

- Stream dataflow: CMSIS `osPriorityRealtime`, 32 KiB stack, blocking microphone input.
- Stream events: CMSIS `osPriorityHigh`, 8 KiB stack, nominal 20 fps LCD updates.
- `SendToLCD` transfers complete spectra through asynchronous Stream `spectrum`
  events. Three fixed reference-counted payload slots bound memory use. Drawing
  and VSYNC run in the event thread; exhausted slots drop a publication.

The dataflow chain is MicrophoneSource -> PCMToFloat -> EthosGain ->
SlidingBuffer -> RemoveDC -> HannWindow -> FFT -> Magnitude -> SendToLCD.
The LCD has an event input only. Signal processing assumes continuous realtime
input and uses scalar `int16_t` PCM and `float` sample FIFOs, with sample counts
configured in Python. Node docstrings describe audio, packed RFFT and magnitude
semantics. Sliding history starts with zero padding; no gap-recovery or validity
packets travel through the graph. The generated graph uses a nine-step synchronous
schedule and memory optimization, with two aligned shared FIFO buffers totaling
16,384 bytes. See [shared nodes](../../common/stream/README.md).

The LCD owns its animation and touch state. Tap the plot to toggle log/linear;
the slider changes display scale from 0.5x to 2x. This display control is separate
from the fixed 2x Ethos sample gain. Framebuffers and inference pools occupy
DMA/NPU-visible SRAM0. Buffer sizes, thread stacks and linker sections belong
to this project; reusable stages and driver setup are in `../../common`.

From the repository root:

```powershell
uv pip install -e ./thirdparty/CMSIS-Stream/PythonPackage
uv run projects/template_mram/app.py
uv run scripts/create_ai_layer.py --project template_mram
```

Graph generation also works as `uv run app.py` from this project folder.
`generated/` contains the scheduler, parameters, includes, JSON metadata and
`app.dot`. Graphviz creates `app.pdf` alongside it. If `dot` is unavailable or
conversion fails, the script prints a warning and continues successfully.
Regenerate after editing the graph. The solution uses the local pack at
`thirdparty/CMSIS-Stream/ARM.CMSIS-STREAM.pdsc`.

After changing model or calibration policy, add `--quantize`. Select
**DevKit-E8@Debug** or **DevKit-E8@Release** and use **CMSIS: Build Solution**,
then **Load & Debug** or **Load & Run**. The model is embedded in MRAM and needs
no separate programming. Connect the standard MIPI LCD. UART4 (SW4 to U4,
115200 8N1) prints the gain self-test result once; live data is shown on the LCD.

Inspect `stream_stats` (including `error`, source/frame counts, LCD event counts
and timing/deadline counters), `gain_test_passed`, `microphone_drops` and
`microphone_overflows` in the debugger. `pipeline_error` also records errors.
The LCD uses asynchronous events because rendering and VSYNC have their own
cadence. The dataflow signal-processing stages always consume/produce their
configured samples. The [earlier board validation](../../documentation/cmsis-stream-validation.md)
records the previous packet-based implementation; its gap-recovery observations
do not describe this scalar graph.

`model/benchmark.yml` and `model/benchmark_uart.txt` record the current gain
network: median NPU 0.0050875 ms and median forward 0.151685 ms at 400 MHz.
The benchmark runs one warmup and ten checked forwards before starting
capture and drawing. `forward()` timing excludes tensor setup, output copying,
validation, UART, capture and display; it includes float-boundary quantization
and PMU hooks. `measurements.memory` records the model size, Ethos weights/arena
region sizes and observed ExecuTorch method/temp pool peaks. Physical memory
types and clocks are under `platform.memory`.

The record identifies RTX5 5.9.1 on Cortex-M55 under `platform`. `estimates.vela`
holds estimated SRAM and MRAM bandwidth separately from measured results, with the raw
`model/benchmark_vela_summary.csv` and model/configuration hashes for provenance.
See the [shared benchmark format and validator](../../schema/README.md).

Update the benchmark and its source captures in place after each run. Git keeps
the history; the project stores only the latest benchmark.
