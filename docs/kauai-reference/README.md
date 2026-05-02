# Kauai reference

Plain-text API reference docs originally from
[`kauai/doc/`](../../kauai/doc/), now in markdown so they render in
GitHub and link cleanly from the rest of `docs/`.

## What's here

| Doc                                            | Source                  | Coverage                                      |
|------------------------------------------------|-------------------------|-----------------------------------------------|
| [`chunky-files-api.md`](chunky-files-api.md)   | `kauai/doc/chunk.txt`   | `ChunkyFile` + `ChunkGraphEnumerator` API.    |
| [`file-api.md`](file-api.md)                   | `kauai/doc/file.txt`    | `FileObject` low-level file class + `FileLocation`. |
| [`groups-api.md`](groups-api.md)               | `kauai/doc/groups.txt`  | `DynamicArray` / `AllocatedArray` / `GeneralGroup` / `AllocatedGroup` / `StringTable_GST` / `AllocatedStringTable`. |
| [`memory-api.md`](memory-api.md)               | `kauai/doc/mem.txt`     | `HQ` handles, `HqAlloc`/`FreeHq`/`PvLockHq`.  |
| [`glossary.md`](glossary.md)                   | `kauai/doc/glossary.txt`| Old Hungarian-style class abbreviations.      |

The plain-text originals stay in [`kauai/doc/`](../../kauai/doc/) as
the historical record — both copies should agree, but if they drift,
the plain-text source under `kauai/doc/` is authoritative for content
(this directory adds formatting and current type names).

## What's NOT here

[`kauai/doc/chomp.doc`](../../kauai/doc/chomp.doc) and
[`kauai/doc/kauai.doc`](../../kauai/doc/kauai.doc) are **binary
Microsoft Word 6.0** files from 1994. Modern markdown ports are not in
this tree because conversion needs Word or pandoc with the legacy
`.doc` filter. If you want the contents:

- Open the file in Microsoft Word.
- Or run `pandoc -f doc -t markdown kauai/doc/chomp.doc -o chomp.md`
  (requires pandoc built with the legacy MS-Word reader).

`chomp.doc`'s metadata suggests it documents the chomp tool itself.
`kauai.doc` titles itself "Util (Util.h)" — likely a general kauai
overview / porting guide.

## Naming-modernisation status

The codebase is partway through a rename pass replacing the original
3-5 letter Hungarian-style type names (`TGOB`, `MACTR`, `APP`, …) with
descriptive English (`TextGraphicsObject`, `RollCallActorEntry`,
`Application`, …) — see [`CLAUDE.md`](../../CLAUDE.md). The kauai docs
in this directory use **current** type names; if you find an old name
in the code that doesn't appear here, it's been renamed (search the
git log: `git log --grep "rename "`).

The [`glossary.md`](glossary.md) page lists the old abbreviations with
their modern equivalents where known.

## See also

- [`../code-map/kauai-framework.md`](../code-map/kauai-framework.md)
  — what each kauai source file does and which library (kauai-core /
  kauai) it belongs to.
- [`../file-formats/chunky-files.md`](../file-formats/chunky-files.md)
  — friendlier introduction to the chunky file model with diagrams.
- [`../file-formats/`](../file-formats/) — the on-disk file formats
  the kauai APIs read and write.
