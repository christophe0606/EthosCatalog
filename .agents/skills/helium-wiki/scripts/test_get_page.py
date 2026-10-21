#!/usr/bin/env python3
# /// script
# requires-python = ">=3.10"
# dependencies = ["PyYAML>=6.0"]
# ///
"""Run from helium-wiki: uv run scripts/test_get_page.py."""

import contextlib
import io
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

import get_page


class GetPageTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name) / "skill"
        self.frontmatter = (
            "---\nid: kernel-flash-attention-4\ntitle: Example page\n"
            "sources:\n  - notes.txt\n  - pr-cutlass-2472\n---\n"
        )
        self.body = "\n# Body\n\nA paragraph.\n\n---\n\nMore Markdown.\n"
        self.file = self.root / "wiki" / "kernels" / "different-filename.md"
        self.file.parent.mkdir(parents=True)
        self.file.write_text(self.frontmatter + self.body, encoding="utf-8")
        self.source = self.root / "sources" / "cutlass" / "2472.md"
        self.source.parent.mkdir(parents=True)
        self.source_text = "---\nid: pr-cutlass-2472\ntitle: Source page\n---\n\n" + "S" * 600
        self.source.write_text(self.source_text, encoding="utf-8")
        (self.root / "notes.txt").write_text("é" * 700, encoding="utf-8")

    def page(self):
        return get_page.get_page("kernel-flash-attention-4", self.root)[0]

    def run_main(self, args):
        stdout, stderr = io.StringIO(), io.StringIO()
        with patch.object(get_page, "get_page", return_value=(self.page(), [])), contextlib.redirect_stdout(
            stdout
        ), contextlib.redirect_stderr(stderr):
            result = get_page.main(args)
        return result, stdout.getvalue(), stderr.getvalue()

    def test_id_uses_yaml_field_not_filename(self):
        page, warnings = get_page.get_page("kernel-flash-attention-4", self.root)
        self.assertEqual(page.path, self.file.resolve())
        self.assertEqual(page.text, self.frontmatter + self.body)
        self.assertEqual(warnings, [])
        with self.assertRaises(FileNotFoundError):
            get_page.get_page("different-filename", self.root)

    def test_source_page_id_lookup(self):
        page, warnings = get_page.get_page("pr-cutlass-2472", self.root)
        self.assertEqual(page.path, self.source.resolve())
        self.assertEqual(warnings, [])

    def test_relative_windows_and_absolute_paths(self):
        for reference in (
            "wiki/kernels/different-filename.md",
            r"wiki\kernels\different-filename.md",
            str(self.file.resolve()),
        ):
            with self.subTest(reference=reference):
                page, _ = get_page.get_page(reference, self.root)
                self.assertEqual(page.path, self.file.resolve())

    def test_output_modes_preserve_frontmatter_and_markdown(self):
        for options, expected in (
            ([], self.frontmatter + self.body),
            (["--body-only"], self.body.lstrip("\n")),
            (["--frontmatter-only"], self.frontmatter),
        ):
            with self.subTest(options=options):
                result, output, errors = self.run_main(["kernel-flash-attention-4", *options])
                self.assertEqual(result, 0)
                self.assertEqual(output, expected)
                self.assertEqual(errors, "")

    def test_duplicate_ids_require_an_explicit_path(self):
        duplicate = self.root / "wiki" / "duplicate.md"
        duplicate.write_text(self.frontmatter + self.body, encoding="utf-8")
        with self.assertRaisesRegex(ValueError, "Ambiguous id"):
            get_page.get_page("kernel-flash-attention-4", self.root)
        self.assertEqual(
            get_page.get_page("wiki/duplicate.md", self.root)[0].path, duplicate.resolve()
        )

    def test_malformed_pages_are_skipped_during_id_lookup(self):
        bad = self.root / "wiki" / "bad.md"
        bad.write_text("---\nid: [broken\n---\n", encoding="utf-8")
        page, warnings = get_page.get_page("kernel-flash-attention-4", self.root)
        self.assertEqual(page.path, self.file.resolve())
        self.assertEqual(len(warnings), 1)
        with self.assertRaises(get_page.yaml.YAMLError):
            get_page.get_page("wiki/bad.md", self.root)

    def test_index_markdown_without_frontmatter_is_not_a_warning(self):
        (self.root / "wiki" / "README.md").write_text("# Wiki index\n", encoding="utf-8")
        page, warnings = get_page.get_page("kernel-flash-attention-4", self.root)
        self.assertEqual(page.path, self.file.resolve())
        self.assertEqual(warnings, [])

    def test_paths_stay_inside_the_skill_folder(self):
        outside = self.root.parent / "outside.md"
        outside.write_text(self.frontmatter + self.body, encoding="utf-8")
        with self.assertRaisesRegex(ValueError, "inside the skill"):
            get_page.get_page("../outside.md", self.root)
        with self.assertRaisesRegex(ValueError, "inside the skill"):
            get_page.get_page(str(outside), self.root)

    def test_missing_page_returns_failure_without_stdout(self):
        stdout, stderr = io.StringIO(), io.StringIO()
        with patch.object(get_page, "get_page", side_effect=FileNotFoundError("No page")), contextlib.redirect_stdout(
            stdout
        ), contextlib.redirect_stderr(stderr):
            result = get_page.main(["missing-id"])
        self.assertEqual(result, 1)
        self.assertEqual(stdout.getvalue(), "")
        self.assertIn("No page", stderr.getvalue())

    def test_local_source_preview_is_500_characters_not_bytes(self):
        preview = get_page.source_preview("notes.txt", self.page(), self.root)
        self.assertEqual(preview, "é" * 500)

    def test_source_ids_and_page_relative_paths(self):
        self.assertEqual(
            get_page.source_preview("pr-cutlass-2472", self.page(), self.root),
            self.source.read_bytes().decode("utf-8")[:500],
        )
        relative = self.file.parent / "nearby.txt"
        relative.write_text("Nearby source", encoding="utf-8")
        self.assertEqual(
            get_page.source_preview("nearby.txt", self.page(), self.root), "Nearby source"
        )

    def test_url_sources_never_open_network_connections(self):
        with patch("socket.create_connection", side_effect=AssertionError("Network access")) as create, patch(
            "socket.socket.connect", side_effect=AssertionError("Network access")
        ) as connect:
            for source in ("http://example.test/text", "https://example.test/text", "ftp://example.test/file"):
                with self.subTest(source=source), self.assertRaisesRegex(ValueError, "local-only"):
                    get_page.source_preview(source, self.page(), self.root)
            create.assert_not_called()
            connect.assert_not_called()

    def test_follow_sources_skips_urls_and_reads_local_files_without_network(self):
        page = self.page()
        page.header["sources"] = ["https://example.test/source", "notes.txt"]
        original_preview = get_page.source_preview
        stdout, stderr = io.StringIO(), io.StringIO()
        with patch.object(get_page, "get_page", return_value=(page, [])), patch.object(
            get_page, "source_preview", side_effect=lambda source, page: original_preview(source, page, self.root)
        ), patch("socket.create_connection", side_effect=AssertionError("Network access")) as create, patch(
            "socket.socket.connect", side_effect=AssertionError("Network access")
        ) as connect, contextlib.redirect_stdout(stdout), contextlib.redirect_stderr(stderr):
            result = get_page.main(["kernel-flash-attention-4", "--follow-sources"])
        self.assertEqual(result, 0)
        self.assertTrue(stdout.getvalue().startswith(self.frontmatter + self.body))
        self.assertIn("(Source preview unavailable.)", stdout.getvalue())
        self.assertIn("é" * 500, stdout.getvalue())
        self.assertIn("local-only", stderr.getvalue())
        create.assert_not_called()
        connect.assert_not_called()

    def test_binary_and_pdf_sources_are_reported(self):
        binary = self.root / "binary.dat"
        binary.write_bytes(b"\x00\x01\x02")
        with self.assertRaisesRegex(ValueError, "binary"):
            get_page.source_preview("binary.dat", self.page(), self.root)
        (self.root / "manual.pdf").write_bytes(b"%PDF-1.7")
        with self.assertRaisesRegex(ValueError, "PDF"):
            get_page.source_preview("manual.pdf", self.page(), self.root)

    def test_source_following_continues_after_failure(self):
        with patch.object(get_page, "source_preview", side_effect=[FileNotFoundError("unavailable"), "S" * 500]) as preview:
            result, output, errors = self.run_main(["kernel-flash-attention-4", "--follow-sources"])
        self.assertEqual(result, 0)
        self.assertTrue(output.startswith(self.frontmatter + self.body))
        self.assertIn("## Source: notes.txt", output)
        self.assertIn("(Source preview unavailable.)", output)
        self.assertIn("S" * 500, output)
        self.assertIn("unavailable", errors)
        self.assertEqual(preview.call_count, 2)

    def test_body_only_can_follow_sources(self):
        with patch.object(get_page, "source_preview", return_value="Source preview"):
            result, output, errors = self.run_main(
                ["kernel-flash-attention-4", "--body-only", "--follow-sources"]
            )
        self.assertEqual(result, 0)
        self.assertTrue(output.startswith("# Body\n"))
        self.assertNotIn("id: kernel-flash-attention-4", output)
        self.assertIn("Source preview", output)
        self.assertEqual(errors, "")

    def test_sources_support_scalar_empty_and_invalid_values(self):
        self.assertEqual(get_page.sources_from_header({"sources": "https://example.test"}), ["https://example.test"])
        self.assertEqual(get_page.sources_from_header({}), [])
        with self.assertRaisesRegex(ValueError, "list of strings"):
            get_page.sources_from_header({"sources": [False]})

    def test_output_flags_are_mutually_exclusive(self):
        with contextlib.redirect_stderr(io.StringIO()):
            with self.assertRaises(SystemExit) as error:
                get_page.build_parser().parse_args(["example", "--body-only", "--frontmatter-only"])
        self.assertEqual(error.exception.code, 2)


if __name__ == "__main__":
    unittest.main()

