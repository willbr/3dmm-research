# 3mm archive tools

Build the read-only dump tool from an initialized compiler environment:

```console
cmake --build build --target three-mm
build/3mm.exe dump --json movie.3mm > movie.json
```

The tool links the public [`ThreeMM::Archive`](../../libraries/archive/README.md)
API, which keeps `engine-core` and legacy dependencies private. It uses no application,
window, audio device, SDL3 or ImGui runtime. It also accepts other Kauai chunky
archives, such as `.3cn` files; decoded fields are limited to supported records.

## Dump contract

`3mm-dump-1` emits chunks ordered by unsigned `(type, number)` and children
ordered by `(id, type, number)`. Types, chunk numbers and child IDs are eight-digit
hex strings. Flags, names, child references and complete unpacked chunk payloads
remain visible. Movie versions, scene frame ranges/transitions, actor IDs/frame
ranges and caption bounds/lifetimes also use the existing core data readers.

The output omits physical file offsets, free-space maps, source paths and runtime
pointers. Different heap layouts with the same graph produce the same dump.
IDs are never renumbered, and stored payload bytes are never rewritten. Thus a
save that allocates new chunk IDs can produce a different dump even when the
movie plays identically. Packed flags and original byte-order/encoding details
inside unpacked payloads also remain significant. This first version provides a
strict graph/data baseline, not a comparison that ignores every representational
difference between equivalent movies.

`name_units_hex` preserves application string code units; `name_code_unit_bits`
specifies their width. This avoids guessing that legacy bytes are UTF-8 or
silently replacing undecodable characters. Names still pass through Kauai's
legacy string reader and its existing encoding policies. The complete
payload bytes preserve text and unknown record data independently of decoded
fields. JSON uses ASCII with LF newlines and no locale-sensitive floating values.

Input opens without write permission. Output is staged in a temporary file and
published to stdout only after all chunks have been read successfully. Errors go
to stderr; exit status is 0 on success, 1 on read/decode/output failure, and 2 on
invalid arguments. A failure while publishing stdout can leave partial output;
always check the exit code. Large archives can produce large JSON files because
payloads use hexadecimal rather than a lossy summary.

This is not a complete movie validator. It checks readable chunk data, child
targets and supported record headers; unknown payloads stay opaque. It does not
resolve external assets, validate all scene/actor events, or expose every
container-header detail. A matching dump does not establish original-editor
compatibility. Keep wire-layout/version checks and historical-editor tests as
separate gates.

## Regression checks

`library-checks` builds the tool and synthetic fixture generator. The
`three-mm-dump` CTest checks JSON values, repeatability, different physical
layouts, preserved frame/ID differences, compressed payloads, caption references,
malformed records, invalid arguments,
missing/truncated files and unchanged input hashes. Both x86 and x64 run it.
Fixtures are generated in the build tree and require no original game assets.
The checked-in `tests/fixtures/three-mm-dump-v1.json` baseline pins the complete
synthetic graph dump on both architectures, including exact payload bytes.

Future commands can add movie-aware comparison, validation, dependency reporting
and extraction using the same core readers. Keep any further normalization in
read-only tooling; the `.3mm` writer remains independent of the dump schema.
