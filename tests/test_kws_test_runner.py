# SPDX-License-Identifier: Apache-2.0
"""Portable reference selection and provenance for the KWS host test runner."""
from contextlib import redirect_stderr, redirect_stdout
import hashlib
import io
import json
import os
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest.mock import patch

import run_kws_tests as runner

ROOT = Path(__file__).resolve().parents[1]


class KwsTestRunner(unittest.TestCase):
    def setUp(self):
        (ROOT / "tmp").mkdir(exist_ok=True)
        self.temp = tempfile.TemporaryDirectory(prefix="kws-runner-", dir=ROOT / "tmp")
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.reference = self.root / "external examples/keyword project"
        self.audio = self.reference / "src/sample_audio.cpp"
        self.audio.parent.mkdir(parents=True)
        self.audio.write_text("const short audio[] = {" + ",".join(["0"] * 16000) + "};\n")
        self.packs = self.root / "installed packs"

    def test_reference_is_required_without_an_assumed_sibling_checkout(self):
        stderr = io.StringIO()
        with redirect_stderr(stderr), self.assertRaises(SystemExit) as result:
            runner.parse_args(["--pack-root", str(self.packs)])
        self.assertEqual(result.exception.code, 2)
        self.assertIn("--reference", stderr.getvalue())

    def test_pack_root_must_be_supplied_or_exported(self):
        stderr = io.StringIO()
        with patch.dict(os.environ, {}, clear=True), redirect_stderr(stderr), \
                self.assertRaises(SystemExit) as result:
            runner.parse_args(["--reference", str(self.reference)])
        self.assertEqual(result.exception.code, 2)
        self.assertIn("--pack-root or set CMSIS_PACK_ROOT", stderr.getvalue())

    def test_explicit_paths_override_environment_and_accept_spaces(self):
        with patch.dict(os.environ, {"CMSIS_PACK_ROOT": str(self.root / "other packs")}):
            args = runner.parse_args(["--pack-root", str(self.packs), "--reference", str(self.reference)])
        self.assertEqual(args.pack_root, self.packs)
        self.assertEqual(args.reference, self.reference)

    def test_pack_root_defaults_to_exported_environment(self):
        with patch.dict(os.environ, {"CMSIS_PACK_ROOT": str(self.packs)}):
            args = runner.parse_args(["--reference", str(self.reference)])
        self.assertEqual(args.pack_root, self.packs)

    def test_invalid_reference_fails_before_compilation_or_output_creation(self):
        catalog = self.root / "catalog"
        with patch.object(runner, "ROOT", catalog), patch.object(runner.subprocess, "run") as run, \
                redirect_stderr(io.StringIO()), self.assertRaises(SystemExit) as result:
            runner.main(["--pack-root", str(self.packs), "--reference", str(self.root / "missing")])
        self.assertEqual(result.exception.code, 2)
        run.assert_not_called()
        self.assertFalse(catalog.exists())

    def test_generated_report_uses_public_source_and_hashes_without_local_paths(self):
        catalog = self.root / "catalog"
        model = catalog / "projects/kws_micronet_l/model"
        model.mkdir(parents=True)
        mfcc = self.packs / "ARM/ml-embedded-eval-kit-uc-api/22.8.0-Beta/source/application/api/common/source/Mfcc.cc"
        mfcc.parent.mkdir(parents=True)
        mfcc.write_text("// Test reference implementation\n")
        result_text = "MFCC: test comparison passed\n"

        def successful_command(args, **kwargs):
            return subprocess.CompletedProcess(args, 0, stdout=result_text, stderr="")

        with patch.object(runner, "ROOT", catalog), \
                patch.object(runner.subprocess, "run", side_effect=successful_command), \
                redirect_stdout(io.StringIO()):
            self.assertEqual(runner.main(["--pack-root", str(self.packs),
                                          "--reference", str(self.reference)]), 0)
        text = (model / "mfcc_validation.json").read_text(encoding="utf-8")
        record = json.loads(text)
        self.assertEqual(record["audio_source"], "https://github.com/Arm-Examples/mlek-cmsis-pack-examples")
        self.assertEqual(record["audio_path"], "kws/src/sample_audio.cpp")
        self.assertEqual(record["audio_sha256"], hashlib.sha256(self.audio.read_bytes()).hexdigest())
        self.assertEqual(record["reference_mfcc_sha256"], hashlib.sha256(mfcc.read_bytes()).hexdigest())
        self.assertEqual(record["result"], result_text)
        self.assertEqual(record["tolerance_lsb"], 1)
        self.assertNotIn(self.root.name, text)
        self.assertNotIn(self.reference.name, text)
        self.assertEqual((catalog / "out/kws-tests/down.pcm").stat().st_size, 32000)


if __name__ == "__main__":
    unittest.main()
