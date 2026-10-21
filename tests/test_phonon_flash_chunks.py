# Copyright 2026 Arm Limited and/or its affiliates.
# SPDX-License-Identifier: Apache-2.0
"""Protect independent programming operations from overlapping erase sectors."""
import hashlib
import importlib.util
from pathlib import Path
import tempfile
import unittest

import yaml

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location(
    "phonon_flash_chunks", ROOT / "projects/phonon_31m_en/model/prepare_flash_chunks.py")
chunks = importlib.util.module_from_spec(spec)
spec.loader.exec_module(chunks)


class PhononFlashParts(unittest.TestCase):
    def setUp(self):
        (ROOT / "tmp").mkdir(exist_ok=True)
        temporary = tempfile.TemporaryDirectory(dir=ROOT / "tmp")
        self.addCleanup(temporary.cleanup)
        self.directory = Path(temporary.name).resolve()
        self.destination = self.directory / "parts"

    def image(self, name, address, size):
        path = self.directory / name
        path.write_bytes(bytes(range(256)) * (size // 256) + bytes(range(size % 256)))
        return {"file": str(path), "type": "bin", "load": "image",
                "load-offset": address, "pname": "M55_HP"}

    def test_parts_reconstruct_source_and_preserve_programming_setup(self):
        image = self.image("model.bin", 0xC0000000, 3 * chunks.ERASE_BYTES + 18)
        run = {"output": [image], "programming": [{"algorithm": "bounded.FLM"}],
               "debugger": {"connect": "attach", "load-setup": {"pre-reset": "off"}}}
        result = chunks.prepare_parts(run, self.destination, 2 * chunks.ERASE_BYTES)
        parts = result["operations"]
        self.assertEqual(len(parts), 2)
        source = Path(image["file"]).read_bytes()
        reconstructed = b"".join((ROOT / item["file"]).read_bytes() for item in parts)
        self.assertEqual(reconstructed, source)
        for item in parts:
            data = (ROOT / item["file"]).read_bytes()
            self.assertEqual(item["address"] % chunks.ERASE_BYTES, 0)
            self.assertEqual(item["sha256"], hashlib.sha256(data).hexdigest())
            prepared = yaml.safe_load((ROOT / item["run"]).read_text())["cbuild-run"]
            self.assertEqual(prepared["programming"], run["programming"])
            self.assertEqual(prepared["debugger"], run["debugger"])
            self.assertEqual(prepared["output"][0]["load-offset"], item["address"])
        self.assertEqual(run["output"], [image])

    def test_rejects_shared_erase_sector_before_writing_parts(self):
        first = self.image("model.bin", 0xC0000000, chunks.ERASE_BYTES + 2)
        second = self.image("voice.bin", 0xC0020000, 2)
        with self.assertRaisesRegex(ValueError, "share an erase sector"):
            chunks.prepare_parts({"output": [first, second]}, self.destination, chunks.ERASE_BYTES)
        self.assertFalse(self.destination.exists())

    def test_rejects_misaligned_image_and_chunk_sizes(self):
        image = self.image("model.bin", 0xC0000002, 2)
        with self.assertRaisesRegex(ValueError, "128 KiB boundary"):
            chunks.prepare_parts({"output": [image]}, self.destination, chunks.ERASE_BYTES)
        for size in (0, -chunks.ERASE_BYTES, chunks.ERASE_BYTES + 1):
            with self.assertRaisesRegex(ValueError, "positive multiple"):
                chunks.prepare_parts({"output": [image]}, self.destination, size)
        self.assertFalse(self.destination.exists())


if __name__ == "__main__":
    unittest.main()
