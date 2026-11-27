# Copyright 2026 Arm Limited and/or its affiliates.
# SPDX-License-Identifier: Apache-2.0
"""Size the model arenas and select EnCodec delegate payloads to stage in SRAM."""

import argparse
from pathlib import Path
import struct
import zlib

from executorch.exir._serialize._flatbuffer_program import _flatbuffer_to_program
from executorch.exir._serialize._program import _get_extended_header
from executorch.exir.schema import DataLocation


RECURRENT = (
    ("encode_hidden0", 1, 0), ("encode_hidden1", 1, 0),
    ("decode_hidden0", 1, 0), ("decode_hidden1", 1, 0),
)
SPLIT_CONVOLUTIONS = (
    ("encode_front", 0, None),
    # This convolution took 195.65 ms from OSPI in the Size-scheduled PTE.
    ("encode_front", 1, 9),
    # Per-delegate d-43 measurements identified these remaining hot reads.
    ("encode_front", 1, 6),
    ("decode_back", 0, 6),
    ("decode_back", 1, 9),
)
FUSED_CONVOLUTIONS = (
    ("encode_project1", 0, 0), ("decode_project1", 0, 0),
    ("decode_front", 1, 1),
)
BANDED_CONVOLUTIONS = (
    ("encode_front", 0, 0), ("encode_front", 0, 1),
    # Stage the larger encoder codebook projection to meet the encoder period.
    ("encode_back", 0, 1), ("decode_front", 1, 1),
)
SRAM_BANK_BYTES = 4 * 1024 * 1024
ALIGNMENT = 32
PAGE_BYTES = 4096
VELA_RECORD = struct.Struct("<16sI12x")


def aligned(value, alignment):
    return (value + alignment - 1) // alignment * alignment


def scratch_size(segment):
    offset = 0
    while offset + VELA_RECORD.size <= len(segment):
        name, size = VELA_RECORD.unpack_from(segment, offset)
        start = offset + VELA_RECORD.size
        if start + size > len(segment):
            break
        if name.rstrip(b"\0") == b"scratch_size" and size == 4:
            return struct.unpack_from("<I", segment, start)[0]
        offset = start + aligned(size, 16)
    raise ValueError("Invalid delegate scratch size in Vela payload")


def manifest(data):
    header = _get_extended_header(data)
    if not header or not header.is_valid() or not header.segment_base_offset:
        raise ValueError("Expected an ExecuTorch PTE with extracted backend segments")
    program = _flatbuffer_to_program(data[:header.program_size])
    methods = {plan.name: plan for plan in program.execution_plan}
    delegates = {}
    scratch_bytes = 0
    for plan in methods.values():
        delegates[plan.name] = []
        for delegate in plan.delegates:
            if delegate.id != "EthosUBackend" or delegate.processed.location != DataLocation.SEGMENT:
                raise ValueError(f"{plan.name} must use extracted Ethos-U delegate segments")
            segment = program.segments[delegate.processed.index]
            offset = header.segment_base_offset + segment.offset
            end = offset + segment.size
            if offset < header.program_size or not segment.size or end > len(data):
                raise ValueError(f"{plan.name} delegate payload is outside the PTE")
            scratch_bytes = max(scratch_bytes, scratch_size(memoryview(data)[offset:end]))
            delegates[plan.name].append((offset, segment.size))
    convolution_counts = tuple(len(delegates.get(name, [])) for name in ("encode_front", "decode_back"))
    convolution_placements = {
        (1, 1): FUSED_CONVOLUTIONS,
        (3, 4): BANDED_CONVOLUTIONS,
    }
    placements = RECURRENT + convolution_placements.get(convolution_counts, SPLIT_CONVOLUTIONS)
    segments = []
    staging_bytes = [0, 0]
    for name, bank, index in placements:
        candidates = delegates.get(name, [])
        if not candidates or ("_hidden" in name and len(candidates) != 1):
            raise ValueError(f"{name} has an unexpected number of Ethos-U delegates")
        if index is not None and index >= len(candidates):
            raise ValueError(f"{name} is missing staged delegate {index}")
        offset, size = max(candidates, key=lambda entry: entry[1]) if index is None else candidates[index]
        label = name if index is None or len(candidates) == 1 else f"{name} delegate {index}"
        segments.append((label, offset, size, bank))
        staging_bytes[bank] = aligned(staging_bytes[bank], ALIGNMENT) + size
    for bank, size in enumerate(staging_bytes):
        if size > SRAM_BANK_BYTES:
            raise ValueError(f"{size} staged bytes exceed SRAM{bank} capacity of {SRAM_BANK_BYTES}")
    plans = [plan.non_const_buffer_sizes[1:] for plan in methods.values()]
    if any(len(plan) != 1 for plan in plans):
        raise ValueError("Expected one planned activation buffer per method")
    arena = aligned(max(plan[0] for plan in plans), PAGE_BYTES)
    temp = aligned(scratch_bytes + 32 * 1024, PAGE_BYTES)
    return segments, staging_bytes, header.program_size, arena, temp


def main():
    directory = Path(__file__).resolve().parent / "ai_layer"
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--pte", type=Path, default=directory / "model.pte")
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    data = args.pte.read_bytes()
    segments, staging_bytes, program_bytes, arena_bytes, temp_bytes = manifest(data)
    output = args.output or args.pte.with_name("staging_manifest.hpp")
    lines = [
        "// Copyright 2026 Arm Limited and/or its affiliates.",
        "// SPDX-License-Identifier: Apache-2.0",
        "// Generated by make_staging_manifest.py.",
        "#pragma once",
        '#include "model_pte.h"',
        '#include "staged_model_loader.hpp"',
        "",
        "namespace encodec {",
        f"static_assert(MODEL_PTE_CRC32 == 0x{zlib.crc32(data):08x}U, \"Regenerate the staging manifest\");",
        f"inline constexpr std::array<std::size_t, 2> stagedBytes{{{staging_bytes[0]}U, {staging_bytes[1]}U}};",
        f"inline constexpr std::size_t programBytes = {program_bytes}U;",
        f"inline constexpr std::size_t activationBytes = {arena_bytes}U;",
        f"inline constexpr std::size_t tempBytes = {temp_bytes}U;",
        f"inline constexpr std::array<BackendSegment, {len(segments)}> stagedSegments{{{{",
    ]
    for name, offset, size, bank in segments:
        lines.append(f"    {{{offset}U, {size}U, {bank}U}}, // {name}")
    lines.extend(["}};", "}", ""])
    output.write_text("\n".join(lines), encoding="utf-8")
    print(f"Staged {len(segments)} delegates, {staging_bytes[0]} bytes in SRAM0 and {staging_bytes[1]} in SRAM1")


if __name__ == "__main__":
    main()
