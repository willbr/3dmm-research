# Reconstructing the Nickelodeon expansion

`tools/recreate_nick_expansion.py` reconstructs the ten community Nickelodeon
expansion archives from a Nickelodeon retail ISO and a local conversion profile.
It uses Python 3.9+ and the standard library. It reads ISO 9660 directly; no
mounting, original executable, Windows installer, registry access or 7-Zip is
needed. The resulting `.3CN` and `.3TH` files are ordinary expansion assets.

This is a **lossless reference-derived delta importer**, not an independently
reverse-engineered palette conversion algorithm. A reference installation is
needed once to generate the profile. Later reconstruction uses only the ISO and
profile. The profile contains edited image/metadata data from that reference;
it is generated locally and is not bundled with the project source. The ISO
alone is insufficient for this implementation.

## Generate the conversion profile once

Use the directory containing the installed expansion's `NICK*.3CN` and
`NICK*.3TH` files. The parent directories of the profile/output must exist.

```powershell
python tools/recreate_nick_expansion.py build-profile `
  --iso dist/NICK3DMM.ISO `
  --reference "C:\Program Files (x86)\Microsoft Kids\N3DMM EXPANSION" `
  --profile expansions/nickelodeon-conversion.profile.zip
```

All ten reference files must be present. `Uninstall.exe` and other files are
excluded. The profile records each source/output SHA-256 and size. Its ZIP
members contain copy/add deltas built using content-defined blocks, allowing
unchanged data to be reused even when archive offsets have changed. ZIP entry
metadata and instruction ordering are deterministic. The builder reconstructs
each file to check the delta before publishing the profile.

## Reconstruct from the ISO

```powershell
python tools/recreate_nick_expansion.py import `
  --iso dist/NICK3DMM.ISO `
  --profile expansions/nickelodeon-conversion.profile.zip `
  --output "expansions/recreated/N3DMM EXPANSION"
```

The reference installation is not read during import. Source files, deltas and
all ten outputs are verified before the staged directory is published. A disc
with different asset bytes is refused; its filename or volume label is not a
compatibility check. Invalid ISO extents, unsafe filenames, duplicate members,
oversized data, invalid instructions and corrupted/mismatched profiles fail
without publishing an output directory. Existing output/profile paths are
refused. The original ISO and reference installation are opened read-only.

`reconstruction.json` beside the generated files records the profile's source
and output hashes. The importer does not install/register the expansion or
modify the running Studio. The generated directory can be used wherever the
existing `N3DMM EXPANSION` asset directory is expected; the active content root
and source registration still need to point to it. Source IDs are not invented
or changed by this tool.

## Why a palette lookup is insufficient

Comparison of the supplied ISO and installed community pack found:

| Disc file | Expansion file | Differences |
| --- | --- | --- |
| `SNDS.3CN` | `NICKSNDS.3CN` | Byte-identical |
| `TDFS.3CN` | `NICKTDFS.3CN` | Byte-identical |
| `TMPLS.3CN` | `NICKTMPLS.3CN` | Same 6,546 chunk IDs; texture, material, name and child-link changes |
| `BKGDS.3CN` | `NICKBKGDS.3CN` | Same 532 chunk IDs; palette and bitmap changes |
| `MTRLS.3CN` | `NICKMTRLS.3CN` | Same 103 chunk IDs; texture, material and name changes |
| `ACTOR.NTH` | `NICKACTOR.3TH` | Thumbnail changes |
| `BKGDS.NTH` | `NICKBKGDS.3TH` | Thumbnail changes and graphical-object renumbering/link changes |
| `MTRL.NTH` | `NICKMTRL.3TH` | 171 to 93 chunks; removed entries, changed thumbnails and links |
| `PROP.NTH` | `NICKPROP.3TH` | 117 to 114 chunks; removed entry and changed thumbnails |
| `SOUND.NTH` | `NICKSOUND.3TH` | 18 voice-label changes |

The material textures do not follow a single index-to-index lookup table. The
original editing/dithering process has not been established. Replacing that
process with a guessed colour conversion would change the reference assets.
The conversion profile preserves the historical result, including its choices
of entries, IDs, references, names, images and compression.

## Validation

The supplied disc/reference produced a 19,584,423-byte profile and reconstructed
83,047,383 bytes across ten archives. All ten files were independently compared
byte for byte against the reference and matched. This establishes archive
equivalence; it is not an end-to-end movie playback test.

Synthetic tests require no original assets:

```sh
python -B tests/test_recreate_nick_expansion.py
ctest --test-dir build/studio-portable-x64 -R '^nick-expansion-reconstruction$' --output-on-failure
```

Tests cover deterministic profiles, exact reconstruction, import without a
reference installation, shifted source-block reuse, preserving inputs/existing
outputs, malformed ISO records, missing files, path traversal, damaged patches,
wrong source/output hashes and delta bounds. Failed imports leave no published
output or staging directory.
