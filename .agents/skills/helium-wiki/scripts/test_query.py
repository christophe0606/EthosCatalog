#!/usr/bin/env python3
# /// script
# requires-python = ">=3.10"
# dependencies = ["PyYAML>=6.0"]
# ///
"""Run from the skill root: uv run scripts/test_query.py."""

import contextlib
import io
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

import query


class QueryTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.alias_file = self.root / "aliases.yaml"
        self.alias_file.write_text(
            "tags:\n  armv8.1-m: [helium, mve]\n"
            "architectures:\n  armv8.1-m: [helium]\n  cortex-m55: [m55]\n"
            "languages:\n  cpp: [c++]\n"
            "types:\n  techniques: [technique]\n",
            encoding="utf-8",
        )
        self.aliases = query.load_aliases(self.alias_file)
        self.pages = [
            self.page(
                "gemm", title="Fuse gate-up GEMM", type="kernel",
                tags=["nvfp4", "memory-bound"], repo="cutlass",
                language="cute-dsl", symptoms=["memory-bound"], confidence="verified",
            ),
            self.page(
                "gates", title="Gate-up processing", type="kernel",
                tags=["nvfp4"], repo="other", language="cpp",
            ),
            self.page(
                "helium", title="Helium cache placement", type="techniques",
                architectures=["armv8.1-m", "cortex-m55"], tags=["cache"],
            ),
            self.page(
                "legacy", title="Vector code", type="techniques",
                tags=["helium"], architectures=[],
            ),
            self.page(
                "languages-c-float16", title="C half precision", type="languages",
                tags=["half-precision"],
            ),
        ]

    def page(self, page_id, **header):
        header.setdefault("id", page_id)
        header.setdefault("confidence", "source-reported")
        return query.Page(self.root / f"{page_id}.md", header)

    def search(self, text="", **filters):
        return query.search_pages(self.pages, text, filters, self.aliases)

    def ids(self, matches):
        return [match.page.id for match in matches]

    def test_natural_language_ranks_query_coverage(self):
        matches = self.search("how to fuse gate-up GEMM")
        self.assertEqual(self.ids(matches), ["gemm", "gates"])
        self.assertGreater(matches[0].score, matches[1].score)

    def test_title_match_ranks_above_same_keyword_in_tags(self):
        title = self.page("title", title="GEMM implementation")
        tag = self.page("tag", title="Matrix helper", tags=["gemm"])
        matches = query.search_pages([tag, title], "GEMM", {}, self.aliases)
        self.assertEqual(self.ids(matches), ["title", "tag"])

    def test_tag_alias_matches_architecture_and_alias_spelling(self):
        self.assertEqual(set(self.ids(self.search(tag=["helium"]))), {"helium", "legacy"})
        self.assertEqual(set(self.ids(self.search(tag=["armv8.1-m"]))), {"helium", "legacy"})
        self.assertEqual(self.ids(self.search(architecture=["M55"])), ["helium"])
        self.assertEqual(self.ids(self.search(architecture=["helium"])), ["helium"])

    def test_strong_topic_match_can_beat_incidental_complete_coverage(self):
        strong = self.page(
            "strong", title="Cache memory placement", aliases=["memory cache"],
            tags=["memory", "cache"],
        )
        incidental = self.page("incidental", title="Enable helper", tags=["memory", "cache"])
        matches = query.search_pages(
            [incidental, strong], "enable memory caches", {}, self.aliases
        )
        self.assertEqual(self.ids(matches), ["strong", "incidental"])
        self.assertGreater(matches[0].score, matches[1].score)

    def test_aliases_are_loaded_from_file_and_can_be_extended(self):
        self.alias_file.write_text("tags:\n  cache: [fast-storage]\n", encoding="utf-8")
        aliases = query.load_aliases(self.alias_file)
        matches = query.search_pages(self.pages, "", {"tag": ["fast-storage"]}, aliases)
        self.assertEqual(self.ids(matches), ["helium"])

    def test_overlapping_alias_groups_are_transitive(self):
        aliases = query.AliasMap({"tags": {"first": ["second"], "second": ["third"]}})
        page = self.page("example", tags=["first"])
        matches = query.search_pages([page], "", {"tag": ["third"]}, aliases)
        self.assertEqual(self.ids(matches), ["example"])

    def test_generated_page_aliases_are_loaded_without_merging_shared_aliases(self):
        self.alias_file.write_text("first: [shared, UniqueOne]\nsecond: [shared, UniqueTwo]\n", encoding="utf-8")
        aliases = query.load_aliases(self.alias_file)
        pages = [self.page("first", title="First"), self.page("second", title="Second")]
        self.assertEqual(self.ids(query.search_pages(pages, "UniqueOne", {}, aliases)), ["first"])
        self.assertEqual(set(self.ids(query.search_pages(pages, "shared", {}, aliases))), {"first", "second"})

    def test_generated_page_aliases_do_not_inflate_header_alias_weights(self):
        self.alias_file.write_text("first: [SpecialNickname]\n", encoding="utf-8")
        generated = query.load_aliases(self.alias_file)
        page = self.page("first", title="First", aliases=["SpecialNickname"])
        self.assertEqual(query.keyword_weights(page, generated), query.keyword_weights(page, query.AliasMap()))

    def test_page_aliases_are_not_equivalent_to_all_page_metadata(self):
        self.alias_file.write_text("first: [SpecialNickname]\n", encoding="utf-8")
        aliases = query.load_aliases(self.alias_file)
        page = self.page("first", title="First", tags=["cache"], architectures=["cortex-m55"])
        self.assertEqual(query.search_pages([page], "", {"tag": ["SpecialNickname"]}, aliases), [])
        self.assertEqual(query.search_pages([page], "", {"architecture": ["SpecialNickname"]}, aliases), [])

    def test_combined_and_repeated_filters(self):
        matches = self.search(
            tag=["nvfp4", "memory-bound"], type=["kernel"], repo=["cutlass"],
            language=["cute-dsl"], symptom=["memory-bound"], confidence=["verified"],
        )
        self.assertEqual(self.ids(matches), ["gemm"])
        self.assertEqual(self.search(repo=["missing"]), [])
        self.assertEqual(self.ids(self.search(language=["c++"])), ["gates"])
        self.assertEqual(self.ids(self.search(type=["technique"], tag=["cache"])), ["helium"])

    def test_missing_optional_fields_and_language_id(self):
        self.assertEqual(self.ids(self.search(language=["c"])), ["languages-c-float16"])
        self.assertEqual(self.search(symptom=["compute-bound"]), [])

    def test_keyword_alias_and_plural_normalization(self):
        self.assertEqual(self.ids(self.search("caches on m55")), ["helium"])

    def test_read_only_headers_and_skip_malformed_frontmatter(self):
        (self.root / "valid.md").write_text(
            "---\nid: valid\ntitle: Cache guide\ntype: techniques\n---\n"
            "ExclusiveBodyKeyword\n", encoding="utf-8",
        )
        (self.root / "bad.md").write_text("---\ntags: [broken\n---\n", encoding="utf-8")
        pages, warnings = query.load_pages(self.root)
        self.assertEqual([page.id for page in pages], ["valid"])
        self.assertEqual(len(warnings), 1)
        self.assertEqual(query.search_pages(pages, "ExclusiveBodyKeyword", {}, self.aliases), [])

    def test_invalid_alias_configuration_is_reported(self):
        self.alias_file.write_text("tags:\n  cache: wrong-scalar\n", encoding="utf-8")
        with self.assertRaisesRegex(ValueError, "list of strings"):
            query.load_aliases(self.alias_file)

    def test_result_paths_are_relative_to_the_skill_root(self):
        page = query.Page(query.WIKI_ROOT / "techniques" / "example.md", {"title": "Example"})
        output = query.render_results([query.Match(page, 0, 0)], paths_only=True)
        self.assertEqual(output, "wiki/techniques/example.md")

    def test_main_limit_and_output_modes(self):
        with patch.object(query, "load_pages", return_value=(self.pages, [])), patch.object(
            query, "load_aliases", return_value=self.aliases
        ):
            output = io.StringIO()
            with contextlib.redirect_stdout(output):
                result = query.main(["--tag", "nvfp4", "--type", "kernel", "--limit", "1", "--paths-only"])
            self.assertEqual(result, 0)
            lines = output.getvalue().splitlines()
            self.assertEqual(len(lines), 1)
            self.assertTrue(lines[0].endswith("gemm.md"))

            output = io.StringIO()
            with contextlib.redirect_stdout(output):
                result = query.main(["GEMM", "--compact"])
            self.assertEqual(result, 0)
            self.assertEqual(len(output.getvalue().splitlines()), 1)
            self.assertIn("Fuse gate-up GEMM", output.getvalue())
            self.assertIn("kernel", output.getvalue())

            output = io.StringIO()
            with contextlib.redirect_stdout(output):
                result = query.main(["--repo", "missing", "--paths-only"])
            self.assertEqual(result, 0)
            self.assertEqual(output.getvalue(), "")

    def test_reject_nonpositive_limit_and_keywordless_query(self):
        with contextlib.redirect_stderr(io.StringIO()):
            with self.assertRaises(SystemExit) as error:
                query.build_parser().parse_args(["--limit", "0"])
        self.assertEqual(error.exception.code, 2)
        with self.assertRaisesRegex(ValueError, "searchable keywords"):
            self.search("how to")


if __name__ == "__main__":
    unittest.main()

