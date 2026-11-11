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

Use **Options > V3DMM** to manage portable packages. The left list contains
**Installed packages**; the right shows **Applied to this movie**. Select an
installed package and click **>** to apply it, or select an applied package and
click **<** to remove it from this movie. **>>** applies all installed packages;
**<<** removes all applied packages. Removing a package leaves it installed.
**Install VXP...** adds a standalone `.vxp` to the installed list.

Package changes take effect immediately, even if Options is cancelled. Save
the movie to retain its applied set. Opening a VMM activates exactly its own
declared portable packages, rather than carrying packages over from another
movie. Automatically imported VMM packages are available immediately too.

Save a bundled movie, a movie with explicitly applied packages, or a new movie
referencing source 6, with a `.vmm` filename. Saving first writes an ordinary
private chunky movie, packages the
original portable ZIP bytes and external library names, then replaces the
destination atomically. Ordinary movies continue to save as `.3mm`. The movie
wire structs, version and event types are unchanged.

`3dmm` denotes the base content. Zero-size `Nickelodeon`, `Doraemon` and
`3DMM Expansion Pack` dependencies require the corresponding installed, enabled
library in [Options > Expansions](expansion-manager.md). Missing or disabled
required libraries produce a named error. Library content is never
embedded in the saved VMM.

## Packages and storage

Supported VXPs have `Type=Portable`, Name and Author fields, and matching
`.cfg`, `.3cn` and `.3th` members. This includes the normal output of 7gen.
Library and Testing VXPs, custom mount/source overrides and archives with
subdirectories are outside this implementation. Ancillary package files are
retained in the original ZIP but are not executed or installed as programs.

Package loading policy must follow the original v3dmm's observable behaviour.
A future SQLite catalog or validation cache is an implementation detail: it
must preserve legacy dependency-name matching, the separate role of CFG
shortnames, applied-package order, filename/chunk collision precedence,
embedded-package fallback and missing-dependency behaviour. Content hashes may
identify cached installations internally; they must not turn legacy name-based
dependency matching into a requirement for an identical package version.
The current eager whole-catalog SHA-256 validation is our implementation policy,
not a demonstrated v3dmm compatibility requirement. Optimize it without changing
which packages a movie resolves to. Where reference behaviour remains uncertain,
including automatic activation of ambiguous display names, use the original
manager/VFS and oracle captures to establish it before changing resolution.

The store is `content/packs/v3dmm` beside `settings.ini`, or below
`THREEDMM_PACK_DIR` when set. It retains each original ZIP in a SHA-256 directory,
materializes validated chunky content under generated filenames and records
content hashes. `catalog.json` records installation order and legacy enabled
flags, which provide defaults for ordinary `.3mm` movies. A VMM's declared
package set takes precedence over these defaults.
Imports and catalog updates use staging and atomic publication. Movie inputs
and original VXP files are read only. Decoding checks ZIP CRCs, chunky records
and child links, with limits on counts and expansion sizes.

Installed package names satisfy VMM dependencies, as in the legacy manager.
A different standalone VXP with an already installed name is rejected. Exact
duplicate content filenames use the first applied mount; the virtual filename
list is sorted. Ambiguous filename casing across packages is rejected.
Repeated exact filenames inside a VXP are accepted, including differing bytes.
The reader reproduces the sort and binary lookup of v3dmm 0.14's PhysicsFS
1.0.60 DLL to select one record per name, including the configuration. This is
neither a first-entry nor a last-entry rule. Extraction and installed manifests
use the selected original ZIP index. All records still count toward budgets;
selected content retains CRC validation. Case-distinct colliding filenames
remain outside the shared VFS's supported subset.
Chunk collisions retain the existing resource manager's first-match behavior.
Each movie activates its own selection; failed opens restore the previous
active source lists. Installations are persistent even if the inner movie
later fails to load.

Saving includes the document's active portable package set and enabled external
library dependencies rather than pruning dependencies. Removing a package
does not remove actors or sound references that use it. Package uninstallation
and conversion of a mod-dependent movie to stock `.3mm` are not implemented.

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

Studio and Marcus share the read-only content VFS in `libraries/content/`.
Its mount tables, source aliases, lookup, enumeration, mount precedence and
provider-based streams are implemented in C89. The public C API is
`three_mm/content_vfs.h`; `content_vfs.hpp` is a thin C++ filesystem adapter for
the existing engine. Positions use two 32-bit words so the C89 core can address
large files without C99 integer types. Files are exposed under logical product
namespaces, independent of a movie's numeric source IDs. Legacy chunky readers
can still obtain physical paths while using the common mount selection.

The VMM/VXP decoder, installed-pack store and host integration remain C++.
Studio activates its per-movie portable set in the VFS and uses the same archive
enumeration for base content, fixed libraries, thumbnails and portable content.
Marcus uses that mount policy with read-only Studio preference snapshots and
session-private materialization, without taking over Studio's package manager.
`content-vfs` and `content-vfs-c89` validate the core and its C89 boundary.

## Validation

Build `v3dmm-test` and run it, or run CTest's `v3dmm` test. It covers embedded
range parsing, byte-preserving round trips, materialization, malformed/truncated
inputs, unsafe names, extraction budgets, persistent preferences and failed
publication. `v3dmm-test --inspect <movie.vmm>` inspects dependencies and package
metadata without installation.

Duplicate selection has frozen regression cases in `v3dmm-test`. For a direct
comparison, build the x86 `physfs-zip-probe` target in `tools/retail_oracle`, then
run `tests/v3dmm_zip_oracle_test.py --probe <probe.exe> --dll <original-physfs.dll>
--native <v3dmm-test.exe> --output <artifact-directory>`. The script pins the
original DLL hash and compares 60 generated packages, with conflicting content,
duplicate CFGs, shuffled entries and unrelated padding. No UI is driven.
The adapted sorting implementation retains the upstream PhysicsFS license;
its source is in the [official 1.1.0 archive](https://icculus.org/physfs/downloads/physfs-1.1.0.tar.gz).

`tests/studio_v3dmm_workflow.py` takes the Studio executable, a base ISO, a blank
movie, a local prop/actor VXP and an artifact directory. It uses private settings
and store directories, authors a source-6 actor, saves through the real movie
portfolio, compares actor state and viewport pixels after a fresh-process
reopen, verifies ordinary `.3mm` authoring, and tests standalone import,
two-list selection, apply/remove, bundled saves and per-movie package sets
through the actual Options controls and file portfolio.

Windows x64 portable SDL validation includes the blank VMM, real 7gen-style
prop authoring and Batman's 18 embedded packages. Actor state and viewport
pixels match at the start of all 15 Batman scenes after native save/reopen.
Native metadata inspection also covers CHASE's 595 dependencies, including
591 embedded archives.
The [original v3dmm oracle](v3dmm-oracle.md) now supplies Windows frame/state,
sound-request and mixed-PCM comparisons. Its first complete Batman seek pass
matched compared engine state on all 1,362 frames, with remaining pixel
differences. Native macOS/Linux execution and complete audiovisual parity
remain to be validated. A Debug assertion (`utilint.cpp`, "long too big")
blocks the Batman first-scene end-frame seek, including when opening its inner
chunky movie directly, so complete playback is not claimed. Crash recovery of
bundled documents is not yet validated; private chunky autosaves are working
copies, not portable exports.
