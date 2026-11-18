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
The SQLite catalog and validation cache are implementation details: they
must preserve legacy dependency-name matching, the separate role of CFG
shortnames, applied-package order, filename/chunk collision precedence,
embedded-package fallback and missing-dependency behaviour. Content hashes may
identify cached installations internally; they must not turn legacy name-based
dependency matching into a requirement for an identical package version.
The former eager whole-catalog SHA-256 scan was our implementation policy,
not a demonstrated v3dmm compatibility requirement. The optimized path preserves
which packages a movie resolves to. Where reference behaviour remains uncertain,
including automatic activation of ambiguous display names, use the original
manager/VFS and oracle captures to establish it before changing resolution.

The store is `content/packs/v3dmm` beside `settings.ini`, or below
`THREEDMM_PACK_DIR` when set. It retains each original ZIP in a SHA-256 directory,
materializes validated chunky content under generated filenames and records
content hashes. `catalog.sqlite3` records package metadata, CFG shortnames,
selected member filenames, installation order and legacy enabled flags,
which provide defaults for ordinary `.3mm` movies. A VMM's declared
package set takes precedence over these defaults.
An existing `catalog.json` is migrated once in a SQLite transaction, retaining
its order and enabled flags without hashing the whole library. The old JSON
and all package files remain untouched; SQLite is authoritative afterward, so
later edits to the old JSON do not change the active catalog. Invalid metadata
for an unrelated package does not prevent listing or resolving a different pack.
Repaired metadata is retried when that package is explicitly resolved.

Catalog loading reads cached metadata, without opening each VXP. Marcus and SDL
Studio call the shared store's `Resolve` only for applied packages. A newly
imported package is fully checked; migrated packages are checked on first use.
Verified archive/content files reuse persisted size and modification-time
fingerprints. Changed files are hashed again and must match their installation
hashes. `Resolve(hash, true)` forces verification, including same-size edits
whose timestamps were deliberately preserved; fingerprints alone do not detect
that case. Hash identity remains an internal installation/cache key, not a VMM
dependency-version requirement. Database transactions and WAL allow helper
processes and both hosts to share validation results and catalog updates.

Imports use private staging and catalog updates use SQLite transactions. Movie inputs
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
`mm/content_vfs.h`; `content_vfs.hpp` is a thin C++ filesystem adapter for
the existing engine. Positions use two 32-bit words so the C89 core can address
large files without C99 integer types. Files are exposed under logical product
namespaces, independent of a movie's numeric source IDs. Legacy chunky readers
can still obtain physical paths while using the common mount selection.

Studio and Marcus use `mm/vmm_wire.h` from the C89 engine for VMM v1
reading and writing. It bounds directory reads, preserves legacy dependency
names, and validates embedded archive ranges and the inner movie marker. Two
32-bit words carry offsets across the 4 GiB boundary.
`mm/vxp_package.h` supplies their shared C89 VXP member validation, budgets,
PhysicsFS duplicate selection and Portable CFG parsing. Both metadata reads and
extraction use the selected central-directory indices. Miniz supplies ZIP
directory rows, decompression and CRC checks. File paths, streaming copies and
the installed-pack store use the existing host adapters.
Studio activates its per-movie portable set in the VFS and uses the same archive
enumeration for base content, fixed libraries, thumbnails and portable content.
Marcus uses that mount policy with read-only Studio preference snapshots and
session-private materialization, without taking over Studio's package manager.
`content-vfs` and `content-vfs-c89` validate the core and its C89 boundary.

`mm/vmm_dependencies.h` supplies the shared C89 resolver. It checks external
libraries, looks up installed display names, validates embedded names on
fallback and produces the ordered per-movie package selection. Studio checks
all external libraries before importing and keeps the first resolved entry
for each display name. Marcus also checks CFG shortnames for repeated embedded
declarations and materializes uncached packages privately. Catalog access and
installation use host callbacks.

The C89 VFS stages the replacement portable source, selects the first file for
each exact name and checks the aggregate size of the winning files before
publication. A casing conflict, unreadable file or excess size leaves the active
source intact. Both hosts supply filesystem size checks. Studio also validates
its native filename representation and retains archive-first inspection.

## Validation

The package-store build uses the pinned, public-domain SQLite 3.50.4 amalgamation
through `cmake/SQLiteDependency.cmake`. No system SQLite installation is required.
The store regression covers legacy migration, rollback/retry, preserved order
and enable flags, independent updates from two connections, changed used files,
forced verification and unreadable unused packages. `marcus-content` also checks
that an unrelated missing installed archive does not prevent VMM resolution.

Build `v3dmm-test` and run it, or run CTest's `v3dmm` test. It covers embedded
range parsing, byte-preserving round trips, materialization, malformed/truncated
inputs, unsafe names, extraction budgets, persistent preferences and failed
publication. `v3dmm-test --inspect <movie.vmm>` inspects dependencies and package
metadata without installation.

`engine-vmm-wire` links through the C toolchain and checks the byte layout,
truncated directories, output failures and offsets above 4 GiB. It passed
AddressSanitizer on Windows x86 and x64. The original blank VMM, a Marcus
storyboard VMM and Batman's 18-archive VMM round-tripped byte for byte.
All six VMM files from the Studio authoring and package workflow matched the
pre-conversion files byte for byte. Original v3dmm reopened a new Studio save
with the embedded prop and two sound cues. The 60 compared frame states and
sound requests matched. Image and PCM differences still appear in that comparison.
The actor-event and scrolling-text fixtures matched the previous builds across
228 Studio and Marcus seeks and 180 viewport hashes. For those fixtures, 456
Studio actor records matched the retained retail and v3dmm captures.
Evidence is in `tmp/c89-vmm-wire`.

`engine-vxp-package` exercises the C89 package reader independently of miniz,
including byte-preserving CFG parsing, unsafe names, casing conflicts, CFG
read failures and unsigned budgets above 4 GiB. It passed AddressSanitizer on Windows
x86 and x64. All metadata and ordered directory records matched the
pre-conversion reader across the 609 embedded Batman and CHASE packages. The
original PhysicsFS DLL agreed on all CFG choices and 120 content reads across
60 generated packages. Studio's authoring and package workflow produced the
same six VMM files byte for byte. The Studio and Marcus seek fixtures matched
228 states and 180 viewport hashes from the previous builds. Their 456 Studio
actor records matched the retained retail and v3dmm captures. Evidence is in
`tmp/c89-vxp-package`.

`engine-vmm-dependencies` checks the resolver with a C-only provider,
including catalog updates during resolution, repeated shortnames and a missing
external library after an embedded declaration. It passed AddressSanitizer
on Windows x86 and x64. Studio's save and reopen workflow reproduced all six
VMM files byte for byte. Original v3dmm reopened a new Studio save and matched
the 60 compared frame states and both sound requests. Image and PCM differences
remain in that comparison. The playback seek fixtures also retained all 228
Studio and Marcus states and 180 viewport hashes from the previous builds.
Evidence is in `tmp/c89-vmm-dependencies`.

The portable mount tests in `content-vfs` exercise first-mount precedence,
inspection order, budget carry across 4 GiB, overflow and preservation of the
previous source on failure. The tests passed under AddressSanitizer on Windows
x86 and x64. Studio's six workflow VMMs remained byte-identical. The 228 host
seek states and 180 viewport hashes matched the previous builds, and 456
Studio actor records matched the retained retail and v3dmm captures. Original
v3dmm reopened the new save with matching frame states and sound requests.
Image and PCM differences remain. Evidence is in `tmp/c89-portable-mounts`.

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
