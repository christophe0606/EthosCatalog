---
name: helium-wiki
description: Find Cortex-M software optimization guidance in a local wiki. Use for Helium/MVE, DSP, compiler optimization, memory placement, and performance questions.
---

# Helium Wiki

Find software optimization guidance for Cortex-M systems, including Cortex-M52, Cortex-M55, and Cortex-M85. Use the concept map, summary tables, and search scripts to locate relevant pages, then read those pages before answering.

## Start with the concept map

[wiki/README.md](wiki/README.md) is the entry point into the wiki. It groups pages by optimization concept rather than by their storage folders. Each section has a table containing page IDs, linked paths, and one-line descriptions.

For a broad question, start with the section covering the user's problem, such as vector loops, DSP data, memory placement, or execution latency. Follow the page links or retrieve the listed IDs with `get_page.py`. Pages also contain `related` IDs for further navigation.

## Browse the summary tables

[queries/](queries/) contains generated summary tables that serve a similar discovery purpose to the scripts: use them to find candidate pages by feature, language, technique, or page ID without running a search.

| Table | How to find information |
| --- | --- |
| [by-hardware.md](queries/by-hardware.md) | Look up a hardware feature such as `helium`, `cache`, or `data-tcm` and follow the linked relevant pages. |
| [by-languages.md](queries/by-languages.md) | Look up `asm`, `c`, or `cpp` and follow the linked relevant pages. |
| [by-techniques.md](queries/by-techniques.md) | Browse linked technique titles and compare tags, architecture, confidence, and reproducibility. |
| [by-patterns.md](queries/by-patterns.md) | Look up a pattern page ID and follow its linked title. |

Files are named `by-<folder>.md` for folders under `wiki/`. A table or row may be empty when no relevant pages are indexed. Table links point into `wiki/`; read the linked page for the explanation, evidence, and applicable conditions.

## Use the navigation scripts

Run commands from the installed `helium-wiki/` directory containing this `SKILL.md`. Use `uv run`; the scripts resolve their data from their own location and need no environment variables. Returned paths such as `wiki/techniques/example.md` are relative to this directory.

The scripts read local files and make no HTTP requests. 

### Search page headers with query.py

[scripts/query.py](scripts/query.py) ranks pages using natural-language keywords and YAML frontmatter, including titles, tags, descriptions, and page aliases. It returns titles, paths, and key metadata. Use it to discover pages by topic or filter candidates by their metadata.

```bash
uv run scripts/query.py "how to enable caches on Cortex-M55"
uv run scripts/query.py --tag helium --type techniques --limit 5
uv run scripts/query.py --architecture cortex-m85 --confidence source-reported --compact
uv run scripts/query.py --language c --paths-only
```

Filters: `--type`, `--tag`, `--repo`, `--language`, `--architecture`, `--symptom`, and `--confidence`. Different filters and repeated values are combined with AND. Tag filters also inspect architecture fields; language and symptom filters also inspect tags. Missing optional metadata does not match a filter.

`--limit` controls the maximum result count, `--compact` prints one line per result, and `--paths-only` prints only matching paths. Generated [data/aliases.yaml](data/aliases.yaml) maps page IDs to aliases from wiki headers; these aliases participate in keyword search. For terms occurring in a page's body, use `grep_wiki.py`.

### Read a page with get_page.py

[scripts/get_page.py](scripts/get_page.py) retrieves a page by its YAML `id` or its skill-relative Markdown path. Wiki page IDs match their Markdown filenames without the `.md` extension. By default it prints the frontmatter and body.

```bash
uv run scripts/get_page.py techniques-armv8m-cache-management
uv run scripts/get_page.py wiki/languages/languages-c-float16-half-precision.md --body-only
```

Use `--body-only` for the Markdown body or `--frontmatter-only` for the YAML block. Add `--follow-sources` to append the first 500 characters of each available local source, resolved by path or page ID. Source following is one level deep; URL entries are reported without being fetched. IDs are searched in `wiki/` and in `sources/` when that folder exists.

### Search page bodies with grep_wiki.py

[scripts/grep_wiki.py](scripts/grep_wiki.py) searches Markdown bodies under `wiki/` and local source PR descriptions under `sources/` when present. It excludes YAML frontmatter and returns paths, original file line numbers, and optional context lines.

```bash
uv run scripts/grep_wiki.py "SCB->CCR" --only wiki --context 3
uv run scripts/grep_wiki.py "cache alignment" --any
uv run scripts/grep_wiki.py "MVE|_Float16" --regex --only wiki
```

Queries are case-insensitive literal phrases by default. Multiple query arguments must all occur on the same line. `--any` splits literal queries into words and matches any word; with `--regex`, it matches any supplied expression. `--only wiki`, `--only sources`, or `--only all` selects the folders. `--context N` adds N lines before and after each match, merging overlapping context.

Matching lines use `path:line:text`; context lines use `path-line-text`. Exit code 1 means no matches, while 2 indicates an error.

## Answer from the pages

Cite the relevant page IDs and paths. Use each page's confidence, reproducibility, architecture, and version-sensitive fields to describe the scope of its guidance. Include available code snippets when useful, and distinguish source-reported claims from independently verified results. The concept map and summary tables guide navigation; the pages provide the details supporting an answer.
