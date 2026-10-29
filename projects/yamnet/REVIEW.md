# YAMNet review

Parent disposition: **PASS**. Generated .clangd editor metadata is now excluded
by projects/yamnet/.gitignore, resolving the packaging note below. Final serial
verification reached 270 inference/LCD results with zero capture, deadline,
overflow, request or pipeline errors; firmware stayed running and unchanged.

The port is review-ready and has no blocking source, model, license, memory or
catalog findings. The final tested b-91/d-92 Release image remains running;
this review did not touch the board or firmware.

The shared, table-injected `LogMel` node belongs in `common/nodes` and
`common/nodes_impl/c`. YAMNet's sample alignment, patch policy, request/ack
protocol, network wrapper and LCD presentation are project-owned. This is an
appropriate reusable/project boundary. Direct OSPI execution matches the
verified configuration and CRC evidence, with no full PTE copy in SRAM.

The board evidence supports the stated classification-demo scope: 14 automatic
Ethos delegates, CPU first/depthwise convolutions, 40/40 PTQ top-one agreement,
39/40 original-Keras top-one agreement, and original top classes in board
top-five for all 40 synthetic cases. Strict 0.005 score parity remains a
separately disclosed failure; no labelled AudioSet accuracy is claimed. The
uninterrupted live run recorded 80 inference/LCD updates and zero capture
drops, overflows, deadline misses, request errors or pipeline errors. Window
coalescing is counted separately. Live cadence and memory/stack measurements
are documented.

New handwritten C/C++/Python source and test files include Arm copyright and
Apache-2.0 SPDX headers. Copied TensorFlow sources retain their original
copyright and Apache headers; the full Apache license, pinned revision, public
URLs and hashes are present. No GPL reuse or imported pack/library edits were
found. Conversion failures, OSPI configuration, MobileNet comparison and final
evidence remain documented; the root catalog includes YAMNet with
`catalog: true`.

Packaging note: `projects/yamnet/.clangd` contains absolute paths to the current
workspace's generated Release compilation database. It is editor metadata, not
a source or build input. Remove it from a submitted project if the workspace
generator does not require it, to avoid machine-specific paths. This cleanup
needs no firmware rebuild or board rerun.
