# Chunky files (`.CHK`)

A **chunky file** is a kauai container format: a directed acyclic graph
of typed binary blobs ("chunks") on disk, with random access by tag and
number. Almost every binary file 3DMM reads or writes is a chunky file —
including `.3MM` movies, the shipped `.CHK` asset bundles, and chomp's
build outputs.

## The chunk model

Every chunk has:

- A **type tag** (`ChunkTagOrType`, `ulong`) — a 4-byte ASCII identifier
  like `'MVIE'`, `'BMDL'`, `'SND '` (note the trailing space when the
  mnemonic is shorter than 4 characters). Tags used by 3DMM are the
  `kctg*` constants in [`inc/soc.h`](../../inc/soc.h); kauai's own
  helper tags are in [`kauai/src/chunk.h`](../../kauai/src/chunk.h).
- A **chunk number** (`ChunkNumber`, `ulong`) — assigned by the chunky
  code when the chunk is added (or specified explicitly, but that
  overwrites any existing chunk with the same number).
- **Flags** — including a "loner" bit (top-level vs child-only), a
  compression bit, and a "forest" bit for cross-graph references.
- **Payload bytes** — the chunk data, optionally compressed.
- **Child links** — typed references to other chunks, each tagged with a
  `ChildChunkID` (`chid`) so the parent can address its children
  positionally regardless of the children's chunk numbers.

A chunk is identified by `(ctg, cno)`, packed in
`ChunkIdentification`. A child reference adds the `chid` and is packed in
`ChildChunkIdentification`.

```
+--------------------+         +--------------------+
| ('MVIE', 1)        |--chid 0->| ('SCEN', 7)        |
|                    |--chid 1->| ('SCEN', 12)       |
| MovieFilePrefix    |--chid 2->| ('MSND', 4)        |
+--------------------+         +--------------------+
                                       |
                                       v chid 0
                                +--------------------+
                                | ('SSE ', 9)        |
                                |  scene sound event |
                                +--------------------+
```

Children are owned by `chid`, not by position — when chunks are copied
between files, the `chid` survives even though the `cno` may change.
A chunk can be a child of multiple parents (with different `chid`s);
deleting the last reference deletes the chunk.

## On-disk shape

Every chunky file ends with an **index** mapping `(ctg, cno)` to a
`(file offset, byte count)` pair, plus the per-chunk flags, name, and
child list. The index is built up in memory while a chunky file is open
for writing, and rewritten on `FSave`. While a writeable file has
unsaved adds, its on-disk index is invalid — closing without saving
leaves the file corrupt.

A file opened read-only (or with `fcflAddToExtra`) writes any newly-added
chunks to a sidecar **temp file** until `FSave` is called, at which point
everything is consolidated.

## Reading a chunk

```cpp
PChunkyFile pcfl = ChunkyFile::PcflOpen(&fni, fcflNil);
DataBlock blck;
if (pcfl->FFind(kctgMvie, cnoMvie, &blck) && blck.FUnpackData()) {
    MovieFilePrefix mfp;
    blck.FReadRgb(&mfp, sizeof(mfp), 0);
    // ... walk children with FGetKidChidCtg, etc.
}
pcfl->Close();
```

Higher-level loaders typically:

1. Find the root chunk by `(ctg, cno)`.
2. Read its payload into a fixed `*OnFile` struct, byte-swapping with the
   chunk's `kbom*` mask if the file's byte order differs.
3. Walk children with `FGetKidChidCtg` to find sub-chunks (scenes,
   actors, sound events…) and recurse.

## Writing a chunk

```cpp
PChunkyFile pcfl = ChunkyFile::PcflCreate(&fni, fcflWriteEnable);
ChunkNumber cno;
DataBlock blck;
pcfl->FAdd(sizeof(MovieFilePrefix), kctgMvie, &cno, &blck);
blck.FWrite(&mfp);
pcfl->FAddChildPv(kctgMvie, cno, /*chid=*/0,
                  pSceneBytes, cbScene, kctgScen, &cnoScene);
pcfl->FSave(kctgSoc);  // root creator tag for .3MM is kctgSoc
pcfl->Close();
```

## Chunk graph enumeration

`ChunkGraphEnumerator` walks a chunk and all its descendants in pre+post
order, useful for serialising a sub-graph or auditing references. See
[`chunky-files-api.md`](../kauai-reference/chunky-files-api.md) for the
full API.

## Compression

Chunks may be compressed (RLE / KauaiCodec). The `FUnpackData` call on a
`DataBlock` lazily decompresses if needed. The encoder is always C; the
decoder has an x86 inline-asm fast path
([`kauai/src/kcd2_386.c`](../../kauai/src/kcd2_386.c)) plus a portable C
fallback used on x64. Round-trip equivalence is covered by `codec-test`
([`kauai/test/codec-test.cpp`](../../kauai/test/codec-test.cpp)).

## Where chunky data is built

3DMM ships several `.CHK` files (e.g. `3dmovie.chk`, `building.chk`,
`shared.chk`, `help.chk`, `helpaud.chk`). Each is built from `.cht` /
`.chh` source via the `chomp` tool — see
[`chunky-source.md`](chunky-source.md).

## See also

- [`chunky-files-api.md`](../kauai-reference/chunky-files-api.md) — full
  ChunkyFile / ChunkGraphEnumerator API reference (port of the original
  `kauai/doc/chunk.txt`).
- [`chunky-source.md`](chunky-source.md) — `.cht` source and the chomp
  build step.
- [`chunk-type-reference.md`](chunk-type-reference.md) — all `kctg*`
  type tags grouped by subsystem.
