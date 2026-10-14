# DevKit-E8 OSPI flash loader

This optional FlashOS adapter is independent of the template application.
Open `FlashLoader.csolution.yml` with CMSIS and build **DevKit-E8**. This
solution builds an algorithm only; its ELF is not an application to load/run.

From the repository root, publish the built algorithm with:

```powershell
uv run scripts/publish_e8_flash_loader.py
```

Then pass `--algorithm board/flash/DevKit-E8_IS25WX.FLM` to
`scripts/create_model_flash.py` for a selected OSPI project. See the root
[README](../../../README.md) for the manual programming sequence. The template
embeds its PTE in MRAM and does not use this loader.

The loader uses RAM, position-independent code and R9-relative data. Its
`Reset_Handler` aliases `Init` to satisfy the CMSIS default AC6 entry setting;
there is no application startup/vector table. The calling project must prepare
OSPI clocks, pins/reset, XIP-off, cache/MPU-off and cleared stack limits before
entering the algorithm. The baud divisor assumes the board's normal AXI clock.

`FlashOS.h` is from Arm CMSIS 5.9.0 under Apache-2.0. `ospi_private.h` and the
polling/driver sequences adapted in `FlashPrg.c` retain the Alif license linked
in their source headers. Installed packs are never modified. No hardware
validation of this cleaned solution has been performed yet.
