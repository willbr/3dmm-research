# 3DMMForever documentation

Three doc clusters live here, plus the existing `superpowers/` plan +
spec scratch (which is read-only history; don't edit those files).

## [`file-formats/`](file-formats/)

What's in a `.3MM`, what a chunky file looks like on disk, what the
`kctg*` tags mean, the BMDL model layout, and how sound flows through
the engine. Start here if you're touching anything that reads or
writes a file.

## [`code-map/`](code-map/)

Where the code lives. Per-area pages for kauai, engine, studio, and
the BRender wrapper, plus the `-core` / gui library split and the
embedded MCP server. Start here if you're trying to find where to add
a feature or fix a bug.

## [`kauai-reference/`](kauai-reference/)

Markdown ports of the original
[`kauai/doc/`](../kauai/doc/) plain-text API references — `ChunkyFile`,
`FileObject`, the collection classes, the `HQ` memory API, and the
glossary of old Hungarian-style class names.

## See also

- [`../README.md`](../README.md) — top-level project README (build,
  status, architecture overview).
- [`../CLAUDE.md`](../CLAUDE.md) — conventions for human and AI
  contributors.
- [`../FONTS.md`](../FONTS.md) — Comic Sans hydration instructions.
