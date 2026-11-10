# Generated renderer fixtures

The 332 binary chunks and one generated text manifest formerly checked in here
are reproducible from existing original assets. `manifest.json` preserves their
byte counts and SHA-256 hashes, the input asset hashes, extraction recipes and
the last repository revision containing the fixtures. Generated files live in
ignored `tmp/bren-rasterizer-fixtures/`.

## Generate and verify

Build the existing `inspect-chunks` tool and run the Python 3.9+ generator:

```powershell
cmake --build build/studio-portable-x64 --target inspect-chunks bren-rasterizer-test
python -B tools/rasterizer_fixtures.py generate `
  --inspect-chunks build/studio-portable-x64/inspect-chunks.exe
python -B tools/rasterizer_fixtures.py check
```

Use the corresponding build directory and executable name on other platforms.
Both subcommands accept `--output tmp/NAME`. Generation verifies the original
input hashes, extracts into a fresh temporary directory, and checks all 333
fixture hashes before writing the destination files. It preserves unrelated
destination files and does not update reference hashes. The actor manifest is
normalised to LF so Windows and Unix builds produce the same bytes.

The [golden-image tool](../golden/README.md) uses these generated fixtures by
default and checks every fixture hash before rendering. Its `--fixtures` option
selects a different generated directory. A missing or changed fixture fails
before running the renderer, preventing missing textures from silently falling
back to grey materials.

## Sources and extraction

- `content-files/tmpls.3cn`: `inspect-chunks dump-all TMPL 0x1020` and
  `dump-all TMPL 0x2010` produce the two complete template graphs. The
  `actor-manifest TMPL 0x2010` command produces the body-part text manifest.
- The same archive supplies the two standalone props through `dump BMDL` with
  the actual chunk IDs recorded in `manifest.json`.
- `src/studio/bmp/homelogo.bmp`: its original 256 BGR0 palette entries are
  wrapped in the preserved GLCR array header. This exactly reproduces the
  palette previously extracted from built `utest.chk`, without building that
  archive or needing a separate retail copy.

The prop filenames are historical labels. The old `extract-bmdl` path calls
`ChunkyFile::FGetCkiCtg`, which supplies a type-relative chunk ID but selects the
data block by the absolute index. Thus the files labelled 2000 and 3500 actually
contain BMDL chunks `0x1b8` and `0x794`. Generation uses those verified IDs
directly and preserves the original fixture hashes. This migration does not
change that engine API or its callers.

All 333 fixtures are reproduced for historical completeness. Only 36 are used
by the current renderer recipes; the other 297 were captured as part of the
complete template graphs during the port. None need separate binary storage in
the source checkout. Removing them saves approximately 1.44 MiB per checkout;
the old blobs remain in Git history.

The fixture generator and golden tool's tests can be run together:

```powershell
python -B -m unittest discover -s tests -p "test_rasterizer_*.py"
```
