# Copyright 2026 Arm Limited and/or its affiliates.
# SPDX-License-Identifier: Apache-2.0
"""Fetch/verify the public checkpoint without installing upstream dependencies."""
from pathlib import Path
from urllib.request import urlopen
import argparse,hashlib,zipfile

URL='https://raw.githubusercontent.com/magenta/midi-ddsp/models/midi_ddsp_model_weights_urmp_9_10.zip'
SHA256='46bae44c16c3399c24f8f0ba2557fc0432bc12aa23088606d465b22fec2cd05e'
SIZE=48429473


def fetch(folder):
    folder=folder.resolve();folder.mkdir(parents=True,exist_ok=True)
    archive=folder/'midi_ddsp_model_weights_urmp_9_10.zip'
    if not archive.is_file():
        temporary=archive.with_suffix('.zip.part')
        with urlopen(URL,timeout=60) as response,temporary.open('wb') as output:
            while block:=response.read(1024*1024):output.write(block)
        if temporary.stat().st_size!=SIZE or hashlib.sha256(temporary.read_bytes()).hexdigest()!=SHA256:
            raise ValueError('Downloaded checkpoint archive size/hash differs from the evaluated upstream artifact')
        temporary.replace(archive)
    if archive.stat().st_size!=SIZE or hashlib.sha256(archive.read_bytes()).hexdigest()!=SHA256:
        raise ValueError('Cached checkpoint archive size/hash mismatch')
    with zipfile.ZipFile(archive) as data:
        for item in data.infolist():
            path=(folder/item.filename).resolve()
            if not path.is_relative_to(folder):raise ValueError('Unsafe checkpoint archive path')
            if not item.is_dir() and ('.data-' in item.filename or item.filename.endswith('.index')):
                data.extract(item,folder)
    return folder/'midi_ddsp_model_weights_urmp_9_10'


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cache-dir',type=Path,required=True,help='Directory for the original ZIP and TensorFlow checkpoints')
    args=parser.parse_args();print(fetch(args.cache_dir))
