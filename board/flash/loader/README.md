# DevKit-E8 OSPI flash loader

This optional FlashOS adapter is independent of the MRAM and OSPI templates.
Open `FlashLoader.csolution.yml` with CMSIS and build **DevKit-E8**. This
solution builds an algorithm only; its ELF is not an application to load/run.
The main catalog solution also offers `DevKit-E8@FlashLoader`, selecting only
this utility project, so the algorithm can be rebuilt without switching solutions.

From the repository root, publish the built algorithm with:

```powershell
uv run scripts/publish_e8_flash_loader.py
```

Then pass `--algorithm board/flash/DevKit-E8_IS25WX.FLM` to
`scripts/create_model_flash.py` for a selected OSPI project. See the
[Alif DevKit-E8 flashing guide](../../../documentation/alif-e8-flashing.md#program-an-ospi-model)
for the manual programming sequence.
`template_mram` embeds its PTE in MRAM and does not use this loader.
`template_ospi` supplies the preparation firmware in its `ProgramModel` build;
see [the OSPI template](../../../projects/template_ospi/README.md).

The loader uses RAM, position-independent code and R9-relative data. Its
`Reset_Handler` aliases `Init` to satisfy the CMSIS default AC6 entry setting;
there is no application startup/vector table. The calling project must prepare
OSPI clocks, pins/reset, XIP-off, cache/MPU-off and cleared stack limits before
entering the algorithm. The baud divisor assumes the board's normal AXI clock.

The descriptor bounds model programming to the first 64 MiB of the verified
128 MiB IS25WX01G. Init checks the complete JEDEC ID `9D 5B 1B` and extended
ID `01` (uniform 128 KiB sectors) before accepting that partition. Its FlashOS
host block is 4 KiB; ProgramPage splits
each block at the device's true 256-byte page boundaries, with separate write
enable, page-program and ready/status checks for every physical page. This
reduces debugger calls for large models without issuing a device transaction
across a physical page boundary. EraseSector uses 128 KiB sectors: the ISSI
IS25LX01G/IS25WX01G Rev A1 datasheet, pp.35,38, specifies that geometry for
extended ID `01`, four-byte-address program `84h`, and sector erase `DCh`.
The dedicated partition includes padding within each erased sector; do not
store unrelated persistent data there. Phonon's PTE and both assets start at
sector boundaries. Chip erase is unavailable. The smaller 4 KiB erase remains
supported by the device but is too slow for this model: p.83 gives a typical
80 ms per 4 KiB erase, versus 280 ms per 128 KiB erase.
The main solution's `memory` entry selects this local algorithm for the first
64 MiB, with ITCM at 0x00000000 and a 256 KiB algorithm workspace. A separate
read-only memory entry describes the remaining 64 MiB of fitted flash. Rebuild
the selected application to regenerate its programming configuration; replacing
an algorithm path alone cannot enlarge a pack's original 32 MiB coverage.
On October 21, 2026, the enlarged/coalesced loader programmed Phonon's
44,675,472-byte PTE, 12,582,912-byte voice prefix and 2,048-byte BOS asset.
Release b-34/d-35 verified complete target CRCs `a5f23dc5`, `9d869927` and
`dc5d06f4`, including addresses beyond 32 MiB. The published loader SHA-256 was
`d3d92dca0ef035a294da17820caf0abc26a057d5c49f4db7fa43890cef82a6cc`.
The full programming request exceeded its 300-second transport response timeout;
the server subsequently released the probe, and target CRCs established success.
Wait for an operation to release the probe before starting another operation.
The older template validation below applies to its previous 32 MiB configuration.
Check the published FLM hash when recording evidence: source changes alone do
not update `board/flash/DevKit-E8_IS25WX.FLM`.

`FlashOS.h` is from Arm CMSIS 5.9.0 under Apache-2.0. `ospi_private.h` and the
polling/driver sequences adapted in `FlashPrg.c` retain the Alif license linked
in their source headers. Installed packs are never modified. On October 15,
2026, the published algorithm successfully programmed the 3104-byte
`template_ospi` PTE on DevKit-E8; the application verified its CRC and passed
inference. See the template's [validation record](../../../projects/template_ospi/README.md#hardware-validation).
