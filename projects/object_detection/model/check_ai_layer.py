#!/usr/bin/env python3
# Copyright 2026 Arm Limited and/or its affiliates.
# SPDX-License-Identifier: Apache-2.0
"""Check create_ai_layer in an isolated temporary solution; no firmware build."""
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / 'scripts'))
from project_config import selected_project
from quantized_model import sha256
from create_ai_layer import executorch_pack, prepare_quantized_model


def main():
    _, solution, project, config, network = selected_project(project='object_detection')
    before = {key: sha256(project.parent / network[key]) for key in ('graph', 'safetensors')}
    # The normal cached path must not modify the converted artifacts.
    prepare_quantized_model(ROOT / 'EthosCatalog.csolution.yml', project, network)
    version, pack = executorch_pack(solution, ROOT)
    (ROOT / 'tmp').mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='detection-layer-', dir=ROOT / 'tmp') as directory:
        temp = Path(directory)
        model_dir = temp / 'projects/object_detection/model'
        model_dir.mkdir(parents=True)
        for key in ('model', 'quantize', 'graph', 'safetensors'):
            shutil.copyfile(project.parent / network[key], temp / 'projects/object_detection' / network[key])
        for name in ('network.yml', 'yolo-fastest_192_face_v4.tflite'):
            shutil.copyfile(project.parent / 'model' / name, model_dir / name)
        shutil.copyfile(project, model_dir.parent / project.name)
        shutil.copyfile(ROOT / 'EthosCatalog.csolution.yml', temp / 'EthosCatalog.csolution.yml')
        (temp / '.cmsis').mkdir()
        shutil.copyfile(ROOT / '.cmsis/ensemble_vela.ini', temp / '.cmsis/ensemble_vela.ini')
        (temp / 'scripts').mkdir()
        for name in ('project_config.py', 'quantized_model.py', 'validate_networks.py'):
            shutil.copyfile(ROOT / 'scripts' / name, temp / 'scripts' / name)
        (temp / 'schema').mkdir()
        shutil.copyfile(ROOT / 'schema/network.schema.yml', temp / 'schema/network.schema.yml')
        # Vela's Python tempfile writer needs a writable directory in restricted
        # Windows sessions; keep every transient file inside the workspace.
        env = dict(os.environ, CMSIS_PACK_ROOT=str(pack.parents[2]), TEMP=str(ROOT/'tmp'), TMP=str(ROOT/'tmp'))
        # The real CLI generates all layer files here, including the --quantize
        # subprocess. The original project ai_layer and CMSIS selection are untouched.
        subprocess.run([sys.executable, str(ROOT / 'scripts/create_ai_layer.py'),
                        '--solution', str(temp / 'EthosCatalog.csolution.yml'),
                        '--project', 'object_detection', '--quantize'], cwd=temp, env=env, check=True)
        layer = temp / 'projects/object_detection/ai_layer'
        assert not (temp / 'ai_layer').exists()
        generated = json.loads((layer / 'generation.json').read_text())
        assert generated['delegates'] == 1, generated
        assert generated['operators'] == [], generated
        assert generated['pte_sha256'] == sha256(layer / 'model.pte')
        for name in ('model_pte.c', 'model_pte.h', 'ai_layer.clayer.yml'):
            assert (layer / name).stat().st_size > 0
        copied = {key: sha256(model_dir.parent / network[key]) for key in before}
        assert copied == before, 'The --quantize hook modified the imported cache'
    after = {key: sha256(project.parent / network[key]) for key in before}
    assert before == after
    report = {'project': 'object_detection', 'target': 'ethos-u85-256',
              'cache_sha256': after, 'config_sha256': sha256(config),
              'pte_sha256': generated['pte_sha256'], 'pte_bytes': generated['pte_bytes'],
              'delegates': generated['delegates'], 'cpu_operators': generated['operators'],
              'quantize_is_noop': True, 'executorch': version,
              'full_generator_cli_passed': True, 'project_ai_layer_modified': False,
              'scope': 'Host create_ai_layer CLI through ExecuTorch and Vela; no firmware build/run'}
    report['sources'] = {name: sha256(ROOT / 'scripts' / name)
                         for name in ('create_ai_layer.py', 'quantized_model.py', 'project_config.py')}
    report['check_sha256'] = sha256(Path(__file__))
    Path(__file__).with_name('lowering_validation.json').write_text(json.dumps(report, indent=2)+'\n', encoding='utf-8')
    print(json.dumps(report, indent=2), flush=True)


if __name__ == '__main__':
    main()
