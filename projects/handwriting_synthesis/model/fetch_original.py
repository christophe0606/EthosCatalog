#!/usr/bin/env python3
# Copyright 2026 Arm Limited and/or its affiliates.
# SPDX-License-Identifier: Apache-2.0
"""Download only the pinned original model files, checking committed hashes."""
import hashlib
import json
from pathlib import Path
import urllib.request

BASE='https://raw.githubusercontent.com/sjvasquez/handwriting-synthesis/5f58984c3bc793bd8930bad7012cd2008d4a4a66/'
FILES=['rnn.py','rnn_cell.py','checkpoints/model-17900.index','checkpoints/model-17900.data-00000-of-00001']

def main():
    directory=Path(__file__).resolve().parent/'original'
    manifest=json.loads((directory/'manifest.json').read_text())
    for name in FILES:
        destination=directory/name; destination.parent.mkdir(parents=True,exist_ok=True)
        if not destination.exists():
            with urllib.request.urlopen(BASE+name) as response:
                data=response.read()
            if hashlib.sha256(data).hexdigest()!=manifest['files'][name]['sha256']:
                raise ValueError(f'Upstream bytes changed for {name}')
            destination.write_bytes(data)
        if hashlib.sha256(destination.read_bytes()).hexdigest()!=manifest['files'][name]['sha256']:
            raise ValueError(f'Local original changed for {name}')
        print(name,destination.stat().st_size)

if __name__=='__main__': main()
