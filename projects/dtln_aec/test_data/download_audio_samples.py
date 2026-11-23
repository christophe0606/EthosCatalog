# Copyright 2026 Arm Limited and/or its affiliates.
# SPDX-License-Identifier: Apache-2.0
"""Download the microphone and loopback WAVs used by the recorded SDS demo."""

from hashlib import sha256
from pathlib import Path
from tempfile import NamedTemporaryFile
from urllib.request import urlopen


UPSTREAM = (
    "https://raw.githubusercontent.com/breizhn/DTLN-aec/"
    "9d24e128b4f409db18227b8babb343016625921f/audio_samples"
)
AUDIO_DIR = Path(__file__).resolve().parent / "audio_samples"
WAV_SHA256 = {
    "9mkQhVtzTEy2hDk-6u2Sww_farend_singletalk_mic.wav":
        "c0831f3a53c7290f0a3a2723e27003dc8397b1d58487ee741ca1dfbaacbf9944",
    "9mkQhVtzTEy2hDk-6u2Sww_farend_singletalk_lpb.wav":
        "27e5070d27505d88d49c8a392420e9d48956a83c55b516d43e24ee8643691d48",
    "DLhjtuwiEkS-68TsUVvW5g_nearend_singletalk_mic.wav":
        "fe8f8fea830c9f6b9c1d0c962428eb5ef19e39fb83181d60c298a6665fb831b5",
    "DLhjtuwiEkS-68TsUVvW5g_nearend_singletalk_lpb.wav":
        "edee77963e460610de7a8f42a2611f07c6a3f31596790973529ff4ba1d654520",
    "DMTgmZwtgUilp4omPK7-OQ_doubletalk_mic.wav":
        "311f3a40cac2a864b07e8dadb709b04fed4d52d135f71fd666c2c494ef3ab25d",
    "DMTgmZwtgUilp4omPK7-OQ_doubletalk_lpb.wav":
        "73c041bbeb16177cee6a7b9e70344379837d6e766b4ac838dffc54b47fc0ef3a",
}


def download_samples(audio_dir):
    audio_dir.mkdir(parents=True, exist_ok=True)
    for filename, expected_digest in WAV_SHA256.items():
        destination = audio_dir / filename
        if destination.exists():
            if sha256(destination.read_bytes()).hexdigest() != expected_digest:
                raise ValueError(f"Checksum mismatch for {destination}. Move it aside and rerun.")
            print(f"Verified {filename}")
            continue

        url = f"{UPSTREAM}/{filename}"
        with urlopen(url, timeout=30) as response:
            data = response.read()
        if sha256(data).hexdigest() != expected_digest:
            raise ValueError(f"Checksum mismatch for {url}")

        temporary = NamedTemporaryFile(dir=audio_dir, delete=False)
        temporary_path = Path(temporary.name)
        try:
            with temporary:
                temporary.write(data)
            temporary_path.replace(destination)
        finally:
            temporary_path.unlink(missing_ok=True)
        print(f"Downloaded {filename}")


if __name__ == "__main__":
    try:
        download_samples(AUDIO_DIR)
    except (OSError, ValueError) as error:
        raise SystemExit(f"DTLN audio download failed: {error}") from error
