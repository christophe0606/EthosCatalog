#!/usr/bin/env python3
# Copyright 2026 Arm Limited and/or its affiliates.
# SPDX-License-Identifier: Apache-2.0
"""Emit standalone M55 assembly for the historical/current matrix kernels.

This compiles an isolated kernel, not firmware, and does not access the board.
Compiler products remain in the repository's tmp directory. The firmware's
compiler flags and complete call context can produce different machine code.
"""
from pathlib import Path
import shutil
import subprocess

HERE = Path(__file__).resolve().parent
BUILD = HERE.parents[2] / "tmp/rvtts-reference-fargan/kernel-inspection"
PREFIX = """#include <stdint.h>
#include <stddef.h>
#include <arm_mve.h>
#define CPU_MVE 1
#define MAX_INPUTS 688
#define assert(x) ((void)0)
void arm_absmax_no_idx_f32(const float *,uint32_t,float *);
typedef struct {
    const int16_t *weights;
    const float *scale, *bias, *float_weights;
    int nb_inputs, nb_outputs;
} LinearLayer;
"""


def main():
    BUILD.mkdir(parents=True, exist_ok=True)
    compiler = shutil.which("clang")
    if compiler is None:
        raise RuntimeError("clang is required for standalone assembly inspection")
    cases = {
        "r48_four_rows": HERE / "cpu_benchmarks/r48_fargan_cpu.c.txt",
        "r50_two_rows": HERE / "cpu_benchmarks/r50_fargan_cpu.c.txt",
        "candidate_packed_four_rows": HERE.parent / "nodes_impl/fargan_cpu.c",
    }
    for name, source in cases.items():
        text = source.read_text()
        kernel = text[text.index("static void compute_linear("):text.index("static void compute_activation(")]
        kernel = kernel.replace("static void compute_linear(", "void compute_linear(", 1)
        unit, assembly = BUILD / f"{name}.c", BUILD / f"{name}.s"
        unit.write_text(PREFIX + kernel)
        subprocess.run([
            compiler, "--target=arm-none-eabi", "-mcpu=cortex-m55",
            "-mfloat-abi=hard", "-O3", "-ffast-math", "-ffreestanding",
            "-S", str(unit), "-o", str(assembly),
        ], check=True)
        print(assembly.relative_to(HERE.parents[2]))


if __name__ == "__main__":
    main()
