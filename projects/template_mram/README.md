# MRAM template

This is the former `projects/template`, renamed to `template_mram`. Its model,
fixed test data and inference/benchmark behavior are retained. Both application
and generated model are embedded in MRAM; inference pools use SRAM0.

From the repository root:

```powershell
uv run scripts/create_ai_layer.py --project template_mram
```

Then select **DevKit-E8@Debug** or **DevKit-E8@Release** in the CMSIS UI and use
**CMSIS: Build Solution**. `flash_ospi: false` requires only normal application
loading. The board's default `app_prepare()` hook does nothing for this project.

See the [Alif DevKit-E8 MRAM loading procedure](../../documentation/alif-e8-flashing.md#load-and-run-an-mram-application)
for board setup, loading and output checks.

The existing `model/benchmark.yml` and `model/benchmark_uart.txt` retain the
measurements and original `template.Release+DevKit-E8` context from before the
rename; they are historical results, not a new benchmark of the renamed build.

The 2026-10-15 Release hardware retest with `ARM::CMSIS-Ethos-U@1.0.0`
(driver 2.0.0) passed on DevKit-E8 after adding `ETHOSU_MACS: 256` to the
U85 board layer. See [retest results](model/cmsis_ethos_u_retest.yml) and
[captured UART output](model/cmsis_ethos_u_retest_uart.txt).

To create a new project, copy this directory and follow
[Add another project](../../README.md#add-another-project).
