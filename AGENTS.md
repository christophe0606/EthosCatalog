# Repository guidelines

This repository uses executorch. Network saved in .pt2 / .safetensor format are loaded from pytorch and use the executorch flow to be compiled by vela.

## Important

Only one project should be built at a given time because there is only one ai_layer that is generated for a specific project.
So building all projects is wrong since the same ai_layer would be used by all of them.

Only one project must be enabled when building the solution.


## Node organization

- Put reusable Python node descriptions in `common/nodes/`, with one class per
  file and exports in `common/nodes/__init__.py`.
- Put reusable node implementations, including CMSIS Stream C++ wrappers, in
  `common/nodes_impl/`. Respect its existing backend hierarchy:
  - `c/`: portable C/C++ processing, CMSIS-DSP calls, portable Stream wrappers,
    and their shared support headers such as `NodeSupport.hpp`.
  - `helium/`: implementations that require Helium intrinsics. Calling
    CMSIS-DSP, which selects acceleration at build time, does not make a node
    Helium-specific.
  - `alif_e8/`: Alif E8 hardware-specific drivers and peripheral setup.
- A common node must be usable by different projects. Portability or injected
  hardware callbacks alone do not make a node reusable. Keep application-specific
  drawing, styling, animation, touch interaction, event protocols and pipeline
  policy in the owning project. Factor out a shared part only when it is useful
  across applications without imposing one application's behavior.
- Classify reusable implementations by their actual dependencies. The portable
  microphone source belongs in `common/nodes_impl/c/`; Alif-specific microphone,
  LCD presentation and touch drivers belong in `alif_e8/`. The spectrum LCD node
  and its `SendToLCD` publisher belong to `projects/template_mram/`.
- Put board-specific interfaces and configuration in `board/<board>/`, exposed
  through that board's layer. For example, microphone availability, capture
  block sizes, LCD dimensions, and the DevKit-E8 driver declarations belong in
  `board/DevKit-E8/board_nodes.h`, not in `common`. Portable Stream nodes use
  injected callbacks and parameters rather than including a board header.
- Keep `common/stream/` for shared Stream runtime configuration and interfaces.
  Do not put node implementations in `common/stream/nodes/` or create another
  parallel implementation tree.
- Keep nodes as self-contained and independent as possible. Declare a node's
  private C backend interface in its own C++ header inside an `extern "C"`
  section, and define node-specific state types there, such as
  `sliding_buffer_t` in `SlidingBuffer.hpp`. Guard C++ linkage and wrapper code
  so the C implementation can include its owning header for declarations and
  types without importing Stream's C++ runtime.
- Keep processing results and retained application state in the node that owns
  them. Do not allocate spectrum magnitudes, peak frequencies, or similar data
  in `hardware_params.cpp` or pass their storage through `HardwareParams`.
  Pass node-owned data explicitly to application callbacks that consume it.
- Do not expose private processing functions through a shared `common/nodes.h`
  or other public umbrella interface. Shared support headers must not import
  unrelated node interfaces. Audio tests must declare the C interfaces and
  state layouts they use independently on the test side.
- OSPI will migrate to CMSIS Stream and use the same node organization. Until
  then, keep any raw processing declarations it needs local to that project;
  remove those transitional declarations when its migration is complete.
- Put project-specific Python nodes in `projects/<project>/nodes/`, with exports
  in that package's `__init__.py`. Put their implementations and node-specific
  adapters in `projects/<project>/nodes_impl/`. Preserve any backend subfolders
  used by that project. Do not promote a demonstration or model-specific node
  into `common` merely because it could technically be reused.
- `EthosGain` is a project-specific demonstration: its Python description belongs
  in `projects/template_mram/nodes/`, and its C++ wrapper and ExecuTorch adapter
  belong in `projects/template_mram/nodes_impl/`.
- `LCD` and `SendToLCD` are project-specific spectrum nodes. Their Python
  descriptions belong in `projects/template_mram/nodes/` and their C++ wrappers
  in `projects/template_mram/nodes_impl/`. Spectrum diagnostics belong to that
  project as well. Keep only UI-independent LCD driver operations shared.
- When relocating nodes, update Python imports and `folder` properties, project
  files, include paths, host checks, and documentation. Regenerate graph outputs
  through the project's `app.py`; verify generation from both the repository
  root and the project folder.

## Refactoring validation

- Run the relevant existing host checks and rebuild the affected firmware after
  a refactoring.
- After rebuilding a refactoring, load and run that updated build on the board.
  A successful build or host test alone does not complete verification.
- Use matching firmware and debugger symbols for the selected target and build
  configuration. Verify the startup self-test, pipeline error state, and live
  audio/LCD progress, including deadline and dropped-event counters.
- Before finishing, resume the target and leave the firmware running. If board
  verification is blocked, report the concrete blocker and do not claim it passed.


