#!/usr/bin/env python3
# /// script
# requires-python = ">=3.10"
# dependencies = []
# ///
"""Run from helium-wiki: uv run scripts/test_grep_wiki.py."""

import contextlib
import io
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

import grep_wiki


class GrepWikiTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name) / "skill"
        self.wiki = self.root / "wiki" / "kernels" / "example.md"
        self.wiki.parent.mkdir(parents=True)
        self.wiki.write_text(
            "---\nid: header-only\ntitle: metadata needle\n---\n"
            "# Kernel\ntcgen05.fence before\nintermediate line\nTCGEN05.FENCE after\n"
            "nvfp4 uses block_scale\nonly nvfp4\nonly block_scale\n"
            "2-CTA backward\nping-pong\nlast line\n", encoding="utf-8"
        )
        self.source = self.root / "sources" / "cutlass" / "2472.md"
        self.source.parent.mkdir(parents=True)
        self.source.write_text(
            "---\nid: pr-cutlass-2472\n---\n# PR description\n"
            "Uses ping-pong scheduling.\nSee https://example.test/source\n", encoding="utf-8"
        )

    def search(self, queries, only="all", any_word=False, regex=False):
        patterns = grep_wiki.compile_patterns(queries, any_word, regex)
        return grep_wiki.search_files(patterns, only, any_word, self.root)

    def run_main(self, args):
        stdout, stderr = io.StringIO(), io.StringIO()
        with patch.object(grep_wiki, "SKILL_ROOT", self.root), contextlib.redirect_stdout(
            stdout
        ), contextlib.redirect_stderr(stderr):
            result = grep_wiki.main(args)
        return result, stdout.getvalue(), stderr.getvalue()

    def test_literal_phrase_is_case_insensitive_and_keeps_punctuation(self):
        results, warnings = self.search(["tcgen05.fence"])
        self.assertEqual(warnings, [])
        self.assertEqual(len(results), 1)
        self.assertEqual(results[0].matches, [1, 3])
        self.assertEqual(self.search(["tcgen05Xfence"])[0], [])
        self.assertEqual(len(self.search(["2-CTA backward"])[0]), 1)

    def test_default_requires_the_entire_literal_phrase(self):
        self.assertEqual(self.search(["nvfp4 block_scale"])[0], [])
        results, _ = self.search(["nvfp4 uses block_scale"])
        self.assertEqual(results[0].matches, [4])

    def test_any_splits_a_single_argument_into_words(self):
        results, _ = self.search(["nvfp4 block_scale"], any_word=True)
        self.assertEqual(results[0].matches, [4, 5, 6])
        separate, _ = self.search(["nvfp4", "block_scale"], any_word=True)
        self.assertEqual(separate[0].matches, results[0].matches)

    def test_multiple_phrases_must_match_on_the_same_line(self):
        results, _ = self.search(["nvfp4", "block_scale"])
        self.assertEqual(results[0].matches, [4])
        self.assertEqual(self.search(["nvfp4", "ping-pong"])[0], [])

    def test_only_limits_search_to_the_selected_folder(self):
        all_results, _ = self.search(["ping-pong"])
        self.assertEqual({result.path for result in all_results}, {self.wiki, self.source})
        self.assertEqual([result.path for result in self.search(["ping-pong"], only="wiki")[0]], [self.wiki])
        self.assertEqual([result.path for result in self.search(["ping-pong"], only="sources")[0]], [self.source])

    def test_frontmatter_is_excluded_and_original_line_numbers_are_retained(self):
        self.assertEqual(self.search(["metadata needle"])[0], [])
        results, _ = self.search(["tcgen05.fence"])
        output = grep_wiki.render_results(results, skill_root=self.root)
        self.assertEqual(output,
            "wiki/kernels/example.md:6:tcgen05.fence before\n--\n"
            "wiki/kernels/example.md:8:TCGEN05.FENCE after"
        )

    def test_context_is_clipped_at_body_boundaries(self):
        results, _ = self.search(["# Kernel"])
        output = grep_wiki.render_results(results, context=3, skill_root=self.root)
        self.assertTrue(output.startswith("wiki/kernels/example.md:5:# Kernel\n"))
        self.assertIn("wiki/kernels/example.md-8-TCGEN05.FENCE after", output)
        self.assertNotIn("title:", output)
        self.assertNotIn("id:", output)
        results, _ = self.search(["last line"])
        self.assertEqual(len(grep_wiki.render_results(results, context=3, skill_root=self.root).splitlines()), 4)

    def test_overlapping_context_is_merged_without_duplicate_lines(self):
        results, _ = self.search(["tcgen05.fence"])
        output = grep_wiki.render_results(results, context=1, skill_root=self.root)
        self.assertNotIn("\n--\n", output)
        self.assertEqual(len(output.splitlines()), 5)
        self.assertEqual(output.count("wiki/kernels/example.md-7-intermediate line"), 1)
        self.assertIn("wiki/kernels/example.md:6:", output)
        self.assertIn("wiki/kernels/example.md:8:", output)

    def test_headerless_sources_and_utf8_bom_crlf_are_searchable(self):
        self.source.write_bytes(b"\xef\xbb\xbf# PR\r\nA ping-pong description\r\n")
        results, _ = self.search(["ping-pong"], only="sources")
        self.assertEqual(grep_wiki.render_results(results, skill_root=self.root),
            "sources/cutlass/2472.md:2:A ping-pong description")

    def test_missing_optional_sources_does_not_block_wiki_search(self):
        self.source.unlink()
        self.source.parent.rmdir()
        self.source.parent.parent.rmdir()
        result, output, errors = self.run_main(["ping-pong"])
        self.assertEqual(result, 0)
        self.assertIn("wiki/kernels/example.md:13:ping-pong", output)
        self.assertEqual(errors, "")
        self.assertEqual(self.run_main(["ping-pong", "--only", "sources"])[0], 2)

    def test_bad_files_are_reported_while_valid_matches_are_printed(self):
        bad = self.root / "wiki" / "bad.md"
        bad.write_text("---\ntitle: ping-pong\n", encoding="utf-8")
        result, output, errors = self.run_main(["ping-pong"])
        self.assertEqual(result, 2)
        self.assertIn("wiki/kernels/example.md:13:ping-pong", output)
        self.assertIn("missing YAML frontmatter closing delimiter", errors)
        self.assertNotIn("wiki/bad.md", output)

    def test_regex_is_explicit_and_preserves_spaces_with_any(self):
        results, _ = self.search([r"tcgen05\.fence (before|after)"], regex=True)
        self.assertEqual(results[0].matches, [1, 3])
        results, _ = self.search(["2-CTA backward", "does not exist"], any_word=True, regex=True)
        self.assertEqual(results[0].matches, [7])
        result, output, errors = self.run_main(["[", "--regex"])
        self.assertEqual(result, 2)
        self.assertEqual(output, "")
        self.assertIn("Error:", errors)

    def test_cli_context_and_no_matches_exit_codes(self):
        result, output, errors = self.run_main(["ping-pong", "--context", "1", "--only", "wiki"])
        self.assertEqual(result, 0)
        self.assertEqual(errors, "")
        self.assertEqual(output,
            "wiki/kernels/example.md-12-2-CTA backward\n"
            "wiki/kernels/example.md:13:ping-pong\n"
            "wiki/kernels/example.md-14-last line\n")
        self.assertEqual(self.run_main(["no match here"]), (1, "", ""))

    def test_empty_queries_and_invalid_arguments_are_rejected(self):
        for query in ("", "   "):
            with self.subTest(query=query):
                self.assertEqual(self.run_main([query])[0], 2)
        for args in (["text", "--context", "-1"], ["text", "--context", "one"], ["text", "--only", "other"]):
            with self.subTest(args=args), contextlib.redirect_stderr(io.StringIO()):
                with self.assertRaises(SystemExit) as error:
                    grep_wiki.main(args)
                self.assertEqual(error.exception.code, 2)

    def test_local_source_urls_are_text_and_never_open_network_connections(self):
        with patch("socket.create_connection", side_effect=AssertionError("Network access")) as create, patch(
            "socket.socket.connect", side_effect=AssertionError("Network access")
        ) as connect:
            result, output, errors = self.run_main(["https://example.test/source"])
        self.assertEqual(result, 0)
        self.assertIn("sources/cutlass/2472.md:6:See https://example.test/source", output)
        self.assertEqual(errors, "")
        create.assert_not_called()
        connect.assert_not_called()


if __name__ == "__main__":
    unittest.main()
