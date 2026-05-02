# Glossary (Hungarian-style class abbreviations)

> Markdown port of [`kauai/doc/glossary.txt`](../../kauai/doc/glossary.txt).
> The plain-text source is authoritative.

The original kauai code used 3-5 letter Hungarian-style class names.
3DMMForever is partway through a rename pass replacing those with
descriptive English names (see [`CLAUDE.md`](../../CLAUDE.md) — one
type per commit, commit message `rename OLD -> NEW`). The list below
is **historical**: the abbreviation column is what the code looked
like in 1995; the modern name (where known) is what it's called in the
codebase now.

If you see a name in the second column without a third-column entry,
it likely hasn't been renamed yet — `git log --grep "rename"` is the
fastest way to confirm.

## Classes by header

| Header   | Hungarian | Description                                                | Modern name (if renamed) |
|----------|-----------|------------------------------------------------------------|--------------------------|
| chdoc.h  | doc       | chunky document                                            | (TBD)                    |
| chdoc.h  | doce      | chunky editing document                                    | (TBD)                    |
| chdoc.h  | doch      | hex editor document                                        | (TBD)                    |
| chdoc.h  | docg      | group editor document                                      | (TBD)                    |
| chdoc.h  | doci      | item hex editor document                                   | (TBD)                    |
| chdoc.h  | docpic    | picture display document                                   | (TBD)                    |
| chdoc.h  | dclb      | line-based document                                        | (TBD)                    |
| chdoc.h  | sel       | selection                                                  | (TBD)                    |
| chdoc.h  | dcd       | display for chunky document — displays a `DOC`             | (TBD)                    |
| chdoc.h  | dch       | display chunk in hex — displays a `FileByteStream`         | (TBD)                    |
| chdoc.h  | dcgb      | display for group chunk                                    | (TBD)                    |
| chdoc.h  | dcgl      | display for `DynamicArray` or `AllocatedArray` chunk       | (TBD)                    |
| chdoc.h  | dcgg      | display for `GeneralGroup` or `AllocatedGroup` chunk       | (TBD)                    |
| chdoc.h  | dcst      | display for `StringTable_GST` or `AllocatedStringTable` chunk | (TBD)                 |
| chdoc.h  | dcpic     | display for picture chunk                                  | (TBD)                    |
| chdoc.h  | tscg      | test script chunk gob                                      | (TBD)                    |
| docb.h   | docb      | base class for documents (`doc`)                           | `DocumentBase`           |
| docb.h   | dte       | document tree enumerator                                   | (TBD)                    |
| docb.h   | ddg       | document display gob                                       | (TBD)                    |
| docb.h   | dmd       | document MDI window                                        | (TBD)                    |
| docb.h   | dmw       | document main window                                       | (TBD)                    |
| docb.h   | dsg       | document scroll gob                                        | (TBD)                    |
| docb.h   | dssp      | document scroll window splitter                            | (TBD)                    |
| docb.h   | dssm      | document scroll split mover                                | (TBD)                    |
| stream.h | bsm       | byte stream in memory (entire stream is in memory)         | (TBD)                    |
| stream.h | bsf       | byte stream on file (parts of the stream may be in files)  | `FileByteStream`         |
| cache.h  | smep      | swappable map entry priority                               | (TBD)                    |
| cache.h  | smec      | swappable map error code                                   | (TBD)                    |
| cache.h  | sml       | swappable map of `DynamicArray`                            | (TBD)                    |
| cache.h  | smg       | swappable map of `GeneralGroup`                            | (TBD)                    |
| cache.h  | smlhq     | cache of HQs                                               | (TBD)                    |
| cache.h  | smlpo     | cache of POs                                               | (TBD)                    |

## Renames already landed elsewhere

The following type renames have happened across the codebase but were
not in the original glossary. Documented here so future readers can
search for either name:

| Old (Hungarian) | New name                  | Notes                                       |
|-----------------|---------------------------|---------------------------------------------|
| `TGOB`          | `TextGraphicsObject`      | Text-rendering gob.                         |
| `MACTR`         | `RollCallActorEntry`      | A roll-call entry in a movie's actor list.  |
| `APP`           | `Application`             | Application base class.                     |
| `DMGLOB`        | `DebugMemoryGlobals`      | Debug-only memory tracking globals.         |
| `FDSC`          | `FrameDescriptor`         | Frame metadata in scene playback.           |
| `ChunkTag`      | `ChunkTagOrType`          | 4-byte chunk type tag (`ulong`).            |

The full list of renames is in `git log --grep "^rename "`. Run
periodically — the rename pass is ongoing.

## Hungarian prefixes (kept, not renamed)

Field and local-variable Hungarian prefixes are intentionally retained
across the rename pass. Common ones:

| Prefix    | Meaning                                          |
|-----------|--------------------------------------------------|
| `p`       | pointer                                          |
| `pv`      | `void *`                                         |
| `cb`      | count of bytes                                   |
| `cv`      | count of values                                  |
| `iv`      | index of value                                   |
| `bv`      | byte offset                                      |
| `fp`      | file position                                    |
| `cno`     | chunk number                                     |
| `ctg`     | chunk tag                                        |
| `chid`    | child chunk id                                   |
| `sz`      | zero-terminated string                           |
| `stz`     | length-prefixed + zero-terminated string         |
| `fni`     | filename info                                    |
| `flo`     | file location                                    |
| `bo`      | byte order                                       |
| `osk`     | OS kind                                          |
| `hq`      | generic memory handle                            |
| `kid`     | child chunk identification                       |
| `cki`     | chunk identification (ctg + cno)                 |
| `grf*`    | group of flags (e.g. `grfcfl`, `grffil`)         |
| `khid*`   | constant handler id                              |
| `kctg*`   | constant chunk type tag                          |
| `kbom*`   | constant byte-order mask for an on-disk struct   |

## See also

- [`README.md`](README.md) — index for the kauai reference docs.
- [`../code-map/kauai-framework.md`](../code-map/kauai-framework.md) —
  what each kauai source file does (uses modern names).
- [`CLAUDE.md`](../../CLAUDE.md) — the rename pattern and what's in
  flight.
