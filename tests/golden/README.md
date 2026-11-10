# BRender reference captures

`manifest.json` preserves the SHA-256, dimensions, byte count and capture commit
of all 58 images previously stored here. The images are generated locally under
ignored `tmp/`; they are no longer tracked. The small `fmac.*.txt` math references
remain in this directory.

The captures were collected during the x86 assembly to x64 C renderer port.
They are historical references, not a newly approved baseline for today's
renderer. Eighteen captures are still 256x256 while the other forty and the
current harness use 1024x1024. A size or pixel difference must fail comparison;
the script never updates expected hashes automatically. Existing tests and CI
did not consume these images, so this change does not remove an automated
comparison.

## Generate and check

Python 3.9+ and the existing `bren-rasterizer-test` executable are required.
Build the harness in the appropriate compiler environment, for example:

```powershell
cmake --build build/studio-portable-x64 --target bren-rasterizer-test
python tools/rasterizer_goldens.py generate --arch x64 `
  --exe build/studio-portable-x64/bren-rasterizer-test.exe
python tools/rasterizer_goldens.py check --arch x64
```

On other platforms, use the corresponding build directory and executable name.
`--arch` labels the supplied executable; it must match the architecture you
built. Recipes in the manifest supply the scene arguments and the committed
`tests/data/` fixtures. `BREN_TEST_PALETTE` is set explicitly so colour PPMs are
also produced, regardless of the caller's environment. Each scene's diagnostic
output is saved beside its images as a `.log` file.

Use repeatable `--scene` arguments to select scenes, `--output tmp/NAME` for a
separate output directory, or `generate --check` to render and compare in one
command. Checking exits nonzero for missing, malformed or different images,
including historical resolution differences. Extra generated PPMs without a
historical entry are available for inspection but are not reference-checked.

## Recover the exact historical images

For visual comparisons or to inspect the old baseline without rebuilding an
old renderer:

```powershell
python tools/rasterizer_goldens.py restore --output tmp/bren-reference
python tools/rasterizer_goldens.py check --output tmp/bren-reference
```

This reads the original image blobs from the manifest's `reference_revision`
using `git show` and verifies every hash before writing. It requires that commit
and its blobs to be available locally; shallow clones may need to fetch the
recorded revision. It does not download or execute an old binary. The
`captured_in` commit on each entry records when that particular image last
changed; recipes describe the current harness's equivalent scenes.

Preserve the independent reference when changing tests. Running the renderer
under test and accepting its output as expected data on every run would hide
regressions. Deliberate baseline updates require reviewing rendered differences
against the x86 assembly/reference revision and explicitly updating the manifest.
Normalising the historical resolutions is a separate baseline review.

Migration validation rebuilt both harness architectures and rendered every
recipe. All forty 1024x1024 captures matched their preserved hashes; the eighteen
256x256 captures correctly reported resolution differences. All 58 historical
images were also recovered from Git and verified. The tool's failure-handling
tests can be run with:

```powershell
python -B -m unittest discover -s tests -p test_rasterizer_goldens.py
```

Removing the files saves approximately 81 MiB in new checkouts. Their original
blobs remain recoverable in Git history; this change does not rewrite history.
