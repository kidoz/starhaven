---
title: StarHaven documentation standard
summary: Authoring rules for maintainable, evidence-backed, MkDocs-compatible documentation that remains useful to AI agents.
doc_type: reference
status: verified
last_updated: 2026-08-01
tags:
  - documentation
  - mkdocs
  - ai-agents
  - compatibility-research
---
# StarHaven documentation standard

This standard keeps `docs/` readable as plain Markdown, buildable by MkDocs,
useful as compatibility reference, and predictable for AI agents. It applies to
new pages and to existing pages when they receive substantial edits. It does not
require a bulk rewrite of untouched historical documents.

The metadata is intentionally compact. Published project documentation needs
only the fields that improve search,
ownership of facts, evidence tracking, and maintenance.

## Goals

- Keep one authoritative place for each fact.
- Preserve stable paths, headings, and cross-page links.
- Separate artifact facts, interpretation, and implementation status.
- Make sections self-contained enough for search and AI retrieval.
- Keep the source portable across MkDocs, repository viewers, and ordinary
  Markdown editors.
- Make documentation defects fail `mkdocs build --strict`.

## Research basis

The structure follows three upstream principles:

- [MkDocs writing guidance](https://www.mkdocs.org/user-guide/writing-your-docs/)
  defines the `docs/` layout, index-page convention, relative source links,
  navigation behavior, heading anchors, and YAML metadata support.
- [MkDocs configuration guidance](https://www.mkdocs.org/user-guide/configuration/)
  recommends strict validation for omitted pages, broken links, unrecognized
  links, absolute links, and anchors.
- [Diataxis](https://diataxis.fr/) separates tutorials, how-to guides,
  reference, and explanation so each page answers one kind of reader need.

StarHaven adds evidence and retrieval rules because binary compatibility
documentation must distinguish observed facts from inferences and unknowns.

## Information architecture

Choose the page type before choosing its headings. Do not combine all document
types in one long page.

| Type | Reader need | Preferred location | Shape |
| --- | --- | --- | --- |
| `tutorial` | Learn through a complete guided exercise | `docs/tutorials/` | ordered, end-to-end lesson |
| `how-to` | Complete a specific task | `docs/how-to/` | goal, prerequisites, steps, verification |
| `reference` | Look up authoritative facts | `docs/formats/`, future `docs/reference/` | neutral, structured description |
| `explanation` | Understand design or behavior | `docs/rendering/`, future `docs/explanation/` | context, reasoning, consequences |
| `index` | Discover related pages | directory `index.md` or site `index.md` | short summaries and links |

Existing URLs are compatibility surface. Do not move a page merely to make the
tree more symmetrical. Move or rename only with a deliberate migration plan and
redirect support.

### Content boundaries

- `docs/**` contains published product, engine, and compatibility knowledge.
- Investigation logs, rejected hypotheses, raw observations, and chronological
  research history are working records, not published reference material.

- Source comments explain local implementation decisions; they do not replace a
  format specification.
- Tests are executable evidence. Link to the relevant test or source rather than
  copying large code fragments.

When research settles a fact, update its canonical page and the
[open-question register](../open-questions.md). Do not append a second, newer
answer below an obsolete one.

## Page metadata

New pages and substantially revised pages use MkDocs-compatible YAML front
matter. Existing pages without front matter remain valid until substantially
edited.

```yaml
---
title: LOD archive format
summary: Binary layout and validation rules for MM6 standard LOD archives.
doc_type: reference
status: verified
last_updated: 2026-08-01
source_files:
  - src/core/lod/lod_archive.cpp
  - tests/test_lod_archive.cpp
tags:
  - lod
  - archive
  - mm6
---
```

Required keys:

| Key | Rule |
| --- | --- |
| `title` | Human title; semantically match the single body H1 and nav label. |
| `summary` | One sentence identifying subject and coverage without relying on the title. |
| `doc_type` | `tutorial`, `how-to`, `reference`, `explanation`, or `index`. |
| `status` | `draft`, `verified`, `partial`, `deprecated`, or `historical`. |
| `last_updated` | ISO date of the last substantive content change. |

Optional keys:

| Key | Use |
| --- | --- |
| `source_files` | Repository paths that implement or test documented behavior. |
| `tags` | Three to eight stable search and retrieval terms. |
| `replaces` | Relative path or stable page name superseded by this page. |

Use lowercase metadata keys because MkDocs treats them as case-sensitive. Use
plain YAML scalars and lists; never place executable configuration or secrets in
front matter.

When adding metadata to an old page, remove a redundant prose `Status:` line or
make it describe more specific evidence coverage rather than repeating the
front-matter value.

## Common Markdown contract

Every page must:

1. Use UTF-8 Markdown with the `.md` extension.
2. Have exactly one H1.
3. Increase heading depth one level at a time; do not jump from H2 to H4.
4. Use unique, descriptive headings. Generated heading anchors are public
   links, so do not casually rename a referenced heading.
5. Use relative Markdown links ending in `.md` for documentation sources, such
   as `../open-questions.md`, rather than site-root URLs.
6. Give fenced code blocks a language such as `cpp`, `bash`, or `text`.
7. Put a blank line before and after lists, tables, and fenced code blocks.
8. Avoid raw HTML. MkDocs cannot reliably rewrite or validate links inside it.
9. Name new files and directories with lowercase kebab-case. Keep established
   names stable even when an older name is imperfect.
10. Be included explicitly in `mkdocs.yml` navigation unless intentionally
    excluded by documented configuration.

Prefer ordinary Markdown supported by MkDocs and repository viewers. Add a
Markdown extension only when multiple pages need it, it is maintained by the
locked toolchain, and the strict build verifies it.

## AI retrieval contract

An AI agent may receive one section without its surrounding page. Write every
H2 section so it remains useful in that condition.

- Begin the page with a two-to-four sentence synopsis stating what is known,
  what is covered, and the important boundary.
- Give every reference page an early `## Scope` section.
- Prefer explicit nouns over pronouns such as “it”, “this”, and “the above” when
  the referent could fall outside the retrieved section.
- Repeat units, byte order, coordinate system, and version where ambiguity would
  be costly.
- Put field layouts and exact mappings in tables with stable column names.
- Keep evidence, interpretation, and implementation separate. A decoded field
  can be known even when StarHaven does not implement it.
- Use the existing evidence vocabulary exactly: `observed`, `inferred`, and
  `unknown`.
- Link to the canonical definition instead of copying it. If a short repetition
  is needed for local comprehension, identify the canonical page.
- Use exact searchable identifiers: archive entry names, opcodes, offsets,
  symbols, source paths, and tool commands.
- Avoid time-relative language such as “currently” or “soon” unless accompanied
  by a date, version, commit, or explicit status field.

Review a page for splitting when it exceeds roughly 500 lines, contains more
than one independently useful reference subject, or has sections requiring
different evidence. Split by stable concepts rather than arbitrary line count.
Add a short index page when a subject becomes a family of pages.

## Format-reference profile

Binary and runtime specifications under `docs/formats/` follow this order when
the sections apply:

1. Synopsis below the H1.
2. `## Scope` — included versions, files, and explicit exclusions.
3. `## Compatibility` — edition/version differences and rejection policy.
4. `## Source provenance` — non-expressive artifact identity and reproduction
   commands; never copied proprietary payloads.
5. Container or top-level layout.
6. Record layouts and field semantics.
7. Cross-record joins and runtime behavior.
8. `## Invalid-input behavior` — bounds and deterministic failure rules.
9. `## Implementation` — parser/source/test links and known implementation gap.
10. `## Open questions` — only unresolved, bounded questions, linked to the
    authoritative register.

Use `0x` hexadecimal offsets, decimal sizes with hexadecimal in parentheses when
useful, explicit signedness such as `u16` and `i32`, and explicit byte order.
Every unknown field stays represented in layouts so parsers can preserve or
reject it without guessing.

Keep chronological investigation out of published reference pages. Retain a
`Historical note` only when it prevents a known misreading from recurring.

## Format-reference template

Copy this skeleton into `docs/formats/<lowercase-kebab-name>.md`. Replace every
placeholder and remove sections that genuinely do not apply.

````md
---
title: <Format or runtime structure name>
summary: <One sentence naming the artifact and what this page specifies.>
doc_type: reference
status: draft
last_updated: YYYY-MM-DD
source_files:
  - src/<parser-or-runtime-source>
  - tests/<test-source>
tags:
  - mm6
  - <format>
---
# <Format or runtime structure name>

<Two to four sentences summarizing the result, evidence boundary, and most
important compatibility limitation.>

## Scope

This page covers:

- <included artifact, version, or behavior>.

This page does not cover:

- <explicit exclusion with a link to its canonical page when one exists>.

## Compatibility

| Variant | Support | Notes |
| --- | --- | --- |
| <MM6 edition/version> | <supported/unsupported/unknown> | <boundary> |

## Source provenance

| Field | Value |
| --- | --- |
| Product and edition | <legally obtained edition> |
| Artifact | `<relative path or archive entry>` |
| Size or record count | <non-expressive fact> `observed` |
| Digest | `<digest when useful>` |
| Research tool | <tool and pinned version> |

Reproduce against a user-owned installation:

```bash
export STARHAVEN_GAME_DIR=/path/to/MM6
./buildDir/<tool> <arguments>
```

## Container layout

State byte order, top-level framing, offsets, sizes, and alignment.

| Offset | Size | Type | Field | Evidence | Notes |
| ---: | ---: | --- | --- | --- | --- |
| `+0x00` | 4 | `u32` | `<field>` | `observed` | <meaning> |

## Record layout

Describe one record and how records are counted or terminated.

| Offset | Size | Type | Field | Evidence | Notes |
| ---: | ---: | --- | --- | --- | --- |
| `+0x00` | 2 | `u16` | `<field>` | `unknown` | Preserve without interpretation. |

## Semantics and joins

Describe relationships to other records, tables, assets, or runtime state. Link
to each canonical definition rather than duplicating it.

## Invalid-input behavior

The parser rejects, deterministically and without reading out of bounds:

- <truncation or overflow case>;
- <invalid discriminator or inconsistent count>.

## Implementation

- Parser: `src/<path>`
- Tests: `tests/<path>`
- Known implementation gap: <gap, or “None known for the documented scope.”>

## Open questions

List only bounded unresolved questions. Mirror their status in the
[open-question register](../open-questions.md).

- <question and the evidence needed to resolve it> `unknown`
````

## Source and evidence rules

- Identify proprietary samples only with non-expressive facts needed for
  reproducibility: product or edition, path, size, digest, counts, and tool
  version.
- Never commit extracted game data, executable bytes, decompiled code, bulk
  strings, or other copyrighted content.
- Prefer primary sources for tool behavior and public compatibility research.
- Record external source title, publisher, URL, access date, and pinned revision
  when the source can change.
- State what each source proves. A corroborating implementation does not convert
  an inference into an observation of the original artifact.
- State contradictions explicitly and resolve the canonical text; do not leave
  both claims active.

## Navigation and linking

`mkdocs.yml` is the authoritative navigation manifest. Explicit navigation is
intentional: it gives readers a curated order and makes omitted pages fail the
strict build.

- Add a page to `nav` in the same change that creates it.
- Remove its nav entry in the same change that removes it.
- Keep nav labels concise; put the full technical title in front matter and H1.
- Prefer two or three navigation levels. Use an index page before adding deeper
  nesting.
- Link to `.md` source paths and anchors. MkDocs validates and rewrites them,
  while they remain browsable in the repository.
- After changing a linked heading, search for its old anchor across `docs/`.

## Change workflow

1. Decide the document type and canonical page before writing.
2. Read related pages and the open-question register to avoid duplicating or
   reviving superseded claims.
3. Update metadata when the page has adopted it.
4. Replace stale statements rather than appending a chronology.
5. Update `mkdocs.yml` when pages are added, removed, or reordered.
6. Run:

   ```bash
   just docs-check
   ```

7. When a specification changes executable behavior, update or add the parser
   test in the same feature change.

## Review checklist

- [ ] The page has one purpose and one H1.
- [ ] Scope and compatibility boundaries are explicit.
- [ ] Exact claims carry `observed`, `inferred`, or `unknown` evidence status.
- [ ] Implementation status is not presented as format truth.
- [ ] Local links use relative `.md` paths and valid anchors.
- [ ] New pages appear in `mkdocs.yml`.
- [ ] Stale or contradictory text was replaced, not merely followed by an update.
- [ ] Large independent subjects were split or intentionally kept together.
- [ ] No proprietary payload or secret was added.
- [ ] `just docs-check` passes.

