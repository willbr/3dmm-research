# Native V3DMM support

The SDL Studio can open v1 `.vmm` movies, install their embedded Portable VXP
packages, browse the assets and save bundled movies. It reads the formats
directly; the old manager, patched retail executable and DLLs are not required.
See [the investigation](v3dmm-research.md) for the format and Ghidra evidence.

## Use

Open a `.vmm` through the movie portfolio or pass its path on the command line.
Missing embedded Portable expansions are installed automatically. Their assets
appear in the normal browsers under the shared `Virtual3dmm/v3dmm` source,
source ID **6**. Original unsigned chunk numbers are retained.

Use **Options > Expansions > V3DMM packages...** to install a standalone `.vxp`
or enable/disable an installed package. Changes in this window are saved
immediately, independently of the parent Options dialog's OK/Cancel buttons.
Restart to apply manual installation or enable changes. Automatically imported
VMM packages are available to that movie immediately.

Save a bundled movie, or a new movie referencing source 6, with a `.vmm`
filename. Saving first writes an ordinary private chunky movie, packages the
original portable ZIP bytes and external library names, then replaces the
destination atomically. Ordinary movies continue to save as `.3mm`. The movie
wire structs, version and event types are unchanged.

`3dmm` denotes the base content. Zero-size `Nickelodeon`, `Doraemon` and
`3DMM Expansion Pack` dependencies require the corresponding installed, enabled
library in [Options > Expansions](expansion-manager.md). Missing libraries and
disabled required packages produce a named error. Library content is never
embedded in the saved VMM.

## Packages and storage

Supported VXPs have `Type=Portable`, Name and Author fields, and matching
`.cfg`, `.3cn` and `.3th` members. This includes the normal output of 7gen.
Library and Testing VXPs, custom mount/source overrides and archives with
subdirectories are outside this implementation. Ancillary package files are
retained in the original ZIP but are not executed or installed as programs.

The store is `content/packs/v3dmm` beside `settings.ini`, or below
`THREEDMM_PACK_DIR` when set. It retains each original ZIP in a SHA-256 directory,
materializes validated chunky content under generated filenames and records
content hashes. `catalog.json` records installation order and enabled flags.
Imports and catalog updates use staging and atomic publication. Movie inputs
and original VXP files are read only. Decoding checks ZIP CRCs, chunky records
and child links, with limits on counts and expansion sizes.

Installed package names satisfy VMM dependencies, as in the legacy manager.
A different standalone VXP with an already installed name is rejected. Exact
duplicate content filenames use the first installed mount; the virtual filename
list is sorted. Ambiguous filename casing across packages is rejected.
Chunk collisions retain the existing resource manager's first-match behavior.
Packages activated during successful movie opens remain available for that
process; failed opens restore the previous active source lists. Installations
are persistent even if the inner movie later fails to load.

Saving includes the document's active portable package set and enabled external
library dependencies rather than pruning dependencies. There is currently no
package removal UI or conversion of a mod-dependent movie to stock `.3mm`.

## Implementation

SDL3's [storage interface](https://wiki.libsdl.org/SDL3/SDL_Storage) and
[IO streams](https://wiki.libsdl.org/SDL3/SDL_IOStream) allow application-defined
backends; they do not supply a ZIP mount implementation. The port reuses miniz
for bounded, streaming reads of ZIP ranges inside a VMM. Materializing the
chunky files lets the existing engine continue to seek ordinary physical files.
An explicit archive-list callback gives the tag manager and thumbnail browser
the same ordered portable source without introducing a global filesystem shim.

The format library is in `libraries/v3dmm/`, persistent storage in
`src/studio/vxp_store.cpp`, and document/source integration in
`src/studio/v3dmm_runtime.cpp`. The engine's `MovieContainer::Document` adapter
keeps host package management outside the movie wire format.

## Validation

Build `v3dmm-test` and run it, or run CTest's `v3dmm` test. It covers embedded
range parsing, byte-preserving round trips, materialization, malformed/truncated
inputs, unsafe names, extraction budgets, persistent preferences and failed
publication. `v3dmm-test --inspect <movie.vmm>` inspects dependencies and package
metadata without installation.

`tests/studio_v3dmm_workflow.py` takes the Studio executable, a base ISO, a blank
movie, a local prop/actor VXP and an artifact directory. It uses private settings
and store directories, authors a source-6 actor, saves through the real movie
portfolio, compares actor state and viewport pixels after a fresh-process
reopen, verifies ordinary `.3mm` authoring, and tests package disable and
standalone import through the actual Options controls and file portfolio.

Windows x64 portable SDL validation includes the blank VMM, real 7gen-style
prop authoring and Batman's 18 embedded packages. Actor state and viewport
pixels match at the start of all 15 Batman scenes after native save/reopen.
Native metadata inspection also covers CHASE's 595 dependencies, including
591 embedded archives.
Native macOS/Linux execution and differential playback against patched retail
v3dmm remain to be validated. A Debug assertion (`utilint.cpp`, "long too big")
blocks the Batman first-scene end-frame seek, including when opening its inner
chunky movie directly, so complete playback is not claimed. Crash recovery of
bundled documents is not yet validated; private chunky autosaves are working
copies, not portable exports.
