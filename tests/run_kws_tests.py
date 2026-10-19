# SPDX-License-Identifier: Apache-2.0
"""Host comparison to the original ML Eval Kit implementation and clip.

No datasets are downloaded. Run after generate_mfcc.py. Requires host clang.
Supply the KWS example directory with --reference and the installed CMSIS packs
with --pack-root or CMSIS_PACK_ROOT.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]
REFERENCE_REPOSITORY = "https://github.com/Arm-Examples/mlek-cmsis-pack-examples"
REFERENCE_AUDIO_PATH = "kws/src/sample_audio.cpp"


def parse_args(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--pack-root", type=Path, default=os.environ.get("CMSIS_PACK_ROOT"),
                        help="Installed CMSIS pack root; defaults to CMSIS_PACK_ROOT")
    parser.add_argument("--reference", type=Path, required=True,
                        help="KWS example directory containing src/sample_audio.cpp")
    args = parser.parse_args(argv)
    if args.pack_root is None:
        parser.error("provide --pack-root or set CMSIS_PACK_ROOT")
    args.pack_root = args.pack_root.expanduser()
    args.reference = args.reference.expanduser()
    if not (args.reference / "src/sample_audio.cpp").is_file():
        parser.error("--reference must contain src/sample_audio.cpp")
    return args


def main(argv=None):
    args = parse_args(argv)
    out = ROOT / "out/kws-tests"
    out.mkdir(parents=True, exist_ok=True)
    dsp = args.pack_root / "ARM/CMSIS-DSP/1.18.0"
    ref = args.pack_root / "ARM/ml-embedded-eval-kit-uc-api/22.8.0-Beta"
    api = ref / "source/application/api"
    includes = [ROOT, ROOT / "projects/kws_micronet_l/generated", dsp / "Include", dsp / "PrivateInclude",
                args.pack_root / "ARM/CMSIS/6.3.0/CMSIS/Core/Include", api / "common/include",
                api / "use_case/kws/include", ref / "source/math/include", ref / "source/log/include"]
    flags = ["-O2", "-D_CRT_SECURE_NO_WARNINGS", "-D__ARM_FEATURE_DSP=1", "-D__GNUC_PYTHON__",
             *[f"-I{path}" for path in includes]]
    sources = [
        "TransformFunctions/arm_rfft_fast_init_f32.c", "TransformFunctions/arm_rfft_fast_f32.c",
        "TransformFunctions/arm_cfft_init_f32.c", "TransformFunctions/arm_cfft_f32.c",
        "TransformFunctions/arm_cfft_radix8_f32.c", "TransformFunctions/arm_bitreversal2.c",
        "CommonTables/arm_common_tables.c", "CommonTables/arm_const_structs.c",
        "BasicMathFunctions/arm_mult_f32.c", "BasicMathFunctions/arm_dot_prod_f32.c",
        "StatisticsFunctions/arm_mean_f32.c", "StatisticsFunctions/arm_std_f32.c",
        "StatisticsFunctions/arm_var_f32.c", "StatisticsFunctions/arm_power_f32.c",
        "ComplexMathFunctions/arm_cmplx_mag_f32.c", "ComplexMathFunctions/arm_cmplx_mag_squared_f32.c",
        "FastMathFunctions/arm_cos_f32.c", "FastMathFunctions/arm_sin_f32.c", "FastMathFunctions/arm_vlog_f32.c",
    ]
    objects = []
    for source in [ROOT / "common/nodes_impl/c/mfcc.c",
                   ROOT / "projects/kws_micronet_l/generated/mfcc_tables.c",
                   *[dsp / "Source" / name for name in sources]]:
        obj = out / (source.stem + ".obj")
        objects.append(str(obj))
        subprocess.run(["clang", "-std=c11", *flags, "-c", str(source), "-o", str(obj)], check=True)
    exe = out / "test_kws_mfcc.exe"
    subprocess.run(["clang++", "-std=c++17", *flags, str(ROOT / "tests/test_kws_mfcc.cpp"),
                    str(api / "common/source/Mfcc.cc"), str(ref / "source/math/PlatformMath.cc"),
                    *objects, "-o", str(exe)], check=True)
    source = args.reference / "src/sample_audio.cpp"
    raw = source.read_text().split("= {", 1)[1].split("}", 1)[0]
    samples = [int(value.strip(), 0) for value in raw.split(",") if value.strip()]
    assert len(samples) == 16000
    import numpy as np
    pcm = out / "down.pcm"
    pcm.write_bytes(np.array(samples, dtype="<i2").tobytes())
    fixture = ROOT / "projects/kws_micronet_l/generated/speech_test.h"
    result = subprocess.run([str(exe), str(pcm), str(fixture), str(out / "down_mfcc.bin")],
                            text=True, capture_output=True)
    print(result.stdout)
    print(result.stderr, end="")
    result.check_returncode()
    record = {
        "reference_pack": "ARM::ml-embedded-eval-kit-uc-api@22.8.0-Beta",
        "reference_mfcc_sha256": hashlib.sha256((api / "common/source/Mfcc.cc").read_bytes()).hexdigest(),
        "audio_source": REFERENCE_REPOSITORY,
        "audio_path": REFERENCE_AUDIO_PATH,
        "audio_sha256": hashlib.sha256(source.read_bytes()).hexdigest(),
        "result": result.stdout,
        "tolerance_lsb": 1,
    }
    (ROOT / "projects/kws_micronet_l/model/mfcc_validation.json").write_text(
        json.dumps(record, indent=2) + "\n", encoding="utf-8")
    ui = out / "keyword_ui.obj"
    subprocess.run(["clang", "-std=c11", "-O2", "-c",
                    str(ROOT / "projects/kws_micronet_l/nodes_impl/keyword_ui.c"), "-o", str(ui)], check=True)
    exe = out / "test_kws_nodes.exe"
    subprocess.run(["clang++", "-std=c++17", "-O2",
                    "-I" + str(ROOT / "tests/stream"), "-I" + str(ROOT / "thirdparty/CMSIS-Stream/platform"),
                    "-I" + str(ROOT / "thirdparty/CMSIS-Stream/platform/posix_runtime"),
                    "-I" + str(ROOT / "common/stream"), "-I" + str(ROOT / "projects/kws_micronet_l"),
                    "-I" + str(ROOT / "projects/kws_micronet_l/nodes_impl"),
                    "-I" + str(ROOT / "projects/kws_micronet_l/generated"),
                    str(ROOT / "tests/test_kws_nodes.cpp"), str(ui), "-o", str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
