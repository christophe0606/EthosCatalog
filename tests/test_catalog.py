# Copyright 2026 Arm Limited and/or its affiliates.
# SPDX-License-Identifier: Apache-2.0
"""Keep unfinished ports out of the generated project catalog."""
from pathlib import Path
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))
from update_catalog import read_metadata, update_catalog


class CatalogTests(unittest.TestCase):
    def test_unfinished_ports_are_excluded(self):
        temporary_root = ROOT / "tmp"
        temporary_root.mkdir(exist_ok=True)
        directory = tempfile.TemporaryDirectory(dir=temporary_root)
        root = Path(directory.name).resolve()
        try:
            (root / "documentation").mkdir()
            (root / "documentation/readme_header.md").write_text("# Demos\n")
            for name, setting in (("ready", ""), ("unfinished", "catalog: false\n")):
                project = root / "projects" / name
                project.mkdir(parents=True)
                (project / "README.md").write_text(
                    "---\ncategory: speech\ndescription: TTS demo\n" + setting + "---\n"
                )
            output = update_catalog(root).read_text()
            self.assertIn("projects/ready/README.md", output)
            self.assertNotIn("unfinished", output)
            readme = root / "projects/unfinished/README.md"
            self.assertEqual(read_metadata(readme), ("speech", "TTS demo"))
            readme.write_text(readme.read_text().replace("catalog: false", "catalog: 'false'"))
            with self.assertRaisesRegex(ValueError, "catalog must be a boolean"):
                update_catalog(root)
        finally:
            assert root.is_relative_to(temporary_root.resolve())
            directory.cleanup()


if __name__ == "__main__":
    unittest.main()
