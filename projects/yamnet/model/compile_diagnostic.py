# Copyright 2026 Arm Limited and/or its affiliates.
# SPDX-License-Identifier: Apache-2.0
"""Project-only scheduling experiment; no installed compiler files are modified.

Use after selecting a saved quantized YAMNet cache. The ordinary ai-layer flow
still performs automatic partitioning. The separate diagnostic record binds
the flags and compiler version to the actual emitted PTE and pre-Vela caches.
"""
import argparse
import importlib.metadata
import json
from pathlib import Path
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[3]
HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(ROOT / 'scripts'))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--disable-chaining', action='store_true')
    parser.add_argument('--disable-fwd', action='store_true')
    args = parser.parse_args()
    flags = [f'--{name.replace("_", "-")}' for name, value in vars(args).items() if value]
    assert flags, 'Choose at least one scheduling diagnostic flag'
    import create_ai_layer
    from executorch.backends.arm.ethosu import EthosUCompileSpec

    def spec(solution, project, network):
        vela = network['vela']
        return EthosUCompileSpec(
            target=f"{solution['mlops']['npu']['type'].lower()}-{solution['mlops']['npu']['macs']}",
            system_config=vela['system_config'], memory_mode=vela['memory_mode'],
            config_ini=str((project / vela['ini']).resolve()), extra_flags=flags)

    create_ai_layer.compile_spec = spec
    tempfile.tempdir = str(ROOT / 'tmp')
    sys.argv = [sys.argv[0], '--project', 'yamnet']
    create_ai_layer.main()
    generation = json.loads((HERE.parent / 'ai_layer/generation.json').read_text())
    report = {'diagnostic_only': True, 'vela': importlib.metadata.version('ethos-u-vela'),
              'extra_flags': flags, 'generation': generation}
    (HERE / 'compiler_diagnostic.json').write_text(json.dumps(report, indent=2) + '\n')


if __name__ == '__main__':
    main()
