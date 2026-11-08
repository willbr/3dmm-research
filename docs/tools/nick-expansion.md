# Reconstructing the Nickelodeon expansion

The importer uses Kauai/engine chunk libraries to recreate the community
Nickelodeon expansion from the Nickelodeon retail ISO and the base 3DMM ISO.
Reproducible changes are implemented as chunk transformations. A local fixup
package supplies only the remaining differences in decoded image chunks.

The completed import preserves **every decoded image pixel**, image header,
chunk ID, child link, name, flag and non-image payload of the reference pack.
Physical file layout, compression and archive writer stamps can differ. This
replaces the earlier whole-archive binary-patch implementation.

Only code, functional ID/colour rules and synthetic fixtures are checked in.
Original images, palettes, models, dialogue, expansion archives and image-fixup
packages are not bundled with the source. `/expansions/` and `/tmp/` are ignored.
The fixups contain reference-derived image information and stay local.

## Build

Python 3.9+ with its standard library and the C++ `nick-expansion` tool are used:

```sh
cmake --build build/studio-portable-x64 --target nick-expansion
```

The tool links `three-mm-expansion` and the existing `engine-core` / `kauai-core`
libraries. It has no Studio window or device dependency. Windows builds need
the usual MSVC developer environment. The core retains its native narrow path
repertoire and 255-byte path limit.

## Generate the local image fixups once

Use a known reference directory containing the ten `NICK*.3CN` / `NICK*.3TH`
archives. No reference executable or installer is run.

```powershell
python tools/recreate_nick_expansion.py build-fixups `
  --iso dist/NICK3DMM.ISO `
  --base-iso dist/3DMOVIE100.iso `
  --reference "C:\Program Files (x86)\Microsoft Kids\N3DMM EXPANSION" `
  --fixups expansions/nickelodeon-image-fixups.zip
```

For another build directory, supply `--converter PATH_TO_NICK_EXPANSION`.
The parent directory of the fixup/output path must already exist. Existing
fixup files and output directories are refused.

The builder verifies that the transformed candidate's entire non-image state
matches the reference. Differences in models, audio, IDs, names, flags, links
or other metadata cannot be hidden in an image fixup. It exports decoded
`MBMP`, `TMAP` and `GLCR` chunks through the libraries and records a SHA-256 for
every image chunk, including those requiring no correction.

For each differing image it stores the smaller compressed XOR or modular-add
residual against the generated candidate. Different image lengths are supported
by zero-padding/truncating the predictor. Corrections are addressed by archive
index and chunk type/number, independent of physical offsets or compression.
There are no geometry, sound or arbitrary file patch instructions. All residuals
are round-tripped, then a saved candidate with the reference images is reopened
and compared against the reference before the package is published.

## Reconstruct the expansion

```powershell
python tools/recreate_nick_expansion.py import `
  --iso dist/NICK3DMM.ISO `
  --base-iso dist/3DMOVIE100.iso `
  --fixups expansions/nickelodeon-image-fixups.zip `
  --output "expansions/recreated-chunks/N3DMM EXPANSION"
```

This step needs no installed reference expansion. Both ISOs are read directly
with the read-only ISO 9660 reader. No mounting, registry access, 7-Zip or original
executable is required. Studio's active content root is not modified.

The wrapper verifies disc-file digests, rule version, canonical non-image state,
baseline image hashes, residual hashes and every corrected image hash. It then
reopens the saved archives through the core libraries and verifies the non-image
state and all decoded image hashes again. The final directory is published only
after these checks pass. A mismatch aborts without publishing an approximate
pack. `reconstruction.json` records provenance and output archive hashes.

## Implemented chunk rules

- Preserve source content IDs, model geometry, animation data, sound and fonts.
- Normalise the legacy sound labels' curly apostrophes.
- Remove the historical material/prop browser entries, preserving shared children.
- Renumber graphical-object chunks and update all incoming links.
- Set the historical solid material colour ranges and generic colour labels.
- Reproduce costume labels, the changed texture link and numeric texture labels.
- Replace the common part of each background palette while retaining its scene
  colours in slots 156–245.
- Generate an indexed image candidate without altering transparency or row
  padding. The historical editor palette's three special slots are derived from
  disc colours rather than stored as a replacement palette.

The historical image conversion is not fully known. Some source textures are
byte-identical but have different reference results; some reference textures
also change dimensions/header fields. Nearest-colour conversion alone is not
claimed to reproduce these. Local image residuals preserve their exact result.
As further rules are established, fewer image chunks should need residuals.

The author's published [Pencil++ 2 source](https://github.com/frank-weindel/3dmm-pencil-2)
documents that the image editor plugins were not included. It is useful context;
no third-party plugin code or palette files were copied into this implementation.

## Verification

For the supplied disc/reference:

- Ten archives contain 11,082 chunks.
- Of 1,015 image/palette chunks, 201 need no correction and 814 need local fixups.
- The fixup package is 7,980,517 bytes, versus 19,584,423 bytes for the previous
  whole-archive patch profile.
- A fresh import using only the ISOs and fixups was independently compared with
  the installed reference: **zero image/palette or other decoded differences**.

This proves archive equivalence, including historical image bytes; it is not an
end-to-end movie playback test or automatic installation of the expansion.

```powershell
build/studio-portable-x64/nick-expansion.exe compare `
  "expansions/recreated-chunks/N3DMM EXPANSION" `
  "C:\Program Files (x86)\Microsoft Kids\N3DMM EXPANSION"
```

Comparison ignores physical layout/compression and checks all IDs, names, flags,
links and unpacked payload bytes. Exit 0 means equality; exit 3 means image-only
differences; exit 1 means an error or non-image differences.

Synthetic tests need no licensed assets:

```sh
cmake --build build/studio-portable-x64 --target nick-expansion nick-expansion-test
python -B tests/test_recreate_nick_expansion.py
ctest --test-dir build/studio-portable-x64 -R '^nick-expansion-reconstruction$' --output-on-failure
```

Tests exercise actual library reading/writing, shared graph children, material
and label rules, indexed transparency/padding, changed image dimensions, exact
save/reopen comparison, deterministic image residuals, import without a reference,
preserving inputs/existing outputs, invalid ISO records, wrong source/state/image
digests, corrupted residuals and rejecting non-image chunk/path requests.
