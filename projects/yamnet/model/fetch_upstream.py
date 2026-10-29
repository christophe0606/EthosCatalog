# Copyright 2026 Arm Limited and/or its affiliates.
# SPDX-License-Identifier: Apache-2.0
"""Re-fetch only pinned upstream artifacts and verify every saved SHA-256."""
from pathlib import Path
import hashlib, json, urllib.request


def main():
    folder=Path(__file__).resolve().parent/'upstream'
    manifest=json.loads((folder/'provenance.json').read_text())
    for name,asset in manifest['assets'].items():
        target=folder/name
        if target.exists() and hashlib.sha256(target.read_bytes()).hexdigest()==asset['sha256']:
            continue
        data=urllib.request.urlopen(asset['url'],timeout=120).read()
        if hashlib.sha256(data).hexdigest()!=asset['sha256']:
            raise ValueError(f'Upstream asset hash mismatch: {name}')
        target.write_bytes(data)
        print(name,len(data),flush=True)

if __name__=='__main__':main()
