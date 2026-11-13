# Library split

Every kauai/engine/BRender-wrapper library is split into a `*-core`
(CLI / headless / x64-clean) and a gui (`*`) variant. This page
documents what's in each, and why each file lives where it does.

> **Source of truth:** [`CMakeLists.txt`](../../CMakeLists.txt).
> The lists below are mirrored from there — if they drift, CMake wins.

## Completed application boundary

The core targets now own foundational services, stored movie/scene/actor/caption
data, asset and Body state, actor frame/seek evaluation, movie playback decisions
and pending-sound ownership. Runtime targets own documents/views, World binding,
scene/object resource effects, native clocks, devices and application callbacks.
Evaluation controllers borrow values and issue ordered host commands; linking
core does not require application, document/view or World replacements.

The application boundary and first portable core build are implemented. Core
retains global host configuration and BRender types; this is not a reentrant,
installed SDK or a complete GUI-free scene/movie player. The SDL3/ImGui archive
inspector supplies a portable UI host. Studio also has
SDL playback/editing and device adapters, with a Windows build that exercises
its portable GUI runtime; see the [current verification](../tools/studio-sdl.md).

## C89 engine backend

`three-mm-engine-c89` in `libraries/engine/` contains the first implementation slices
of the [shared C89 engine migration](../../plan.md#shared-c89-engine-migration).
Its public header is `three_mm/playback.h`. It owns playback/startup command
sequencing, wait/fade/scene-advance decisions, fractional movie-frame pacing,
the idle-render gate and retail scrolling policy. Its only library dependency
is the C runtime; no Kauai, BRender, SDL, C++ or native platform headers enter
the public interface or implementation.

| Caller / dependency | Boundary after the first slice |
| --- | --- |
| Studio `Movie::Play`, `Movie::FCmdRender` | Apply commands through the `MoviePlayEvaluator` / `MoviePlaybackEvaluator` C++ facades; policy state is the shared C record. |
| Marcus `playback.cpp` | Uses `RetailScrollPlayback`, a facade over the C scrolling adapter and the same C playback evaluator. Marcus's other presentation/audio logic remains C++. |
| `movie_playback.cpp`, `retail_scroll.cpp` | Translate calls/replies only; the replaced policy implementation lives in C. |
| Host clocks, views, audio and scene effects | Execute commands and supply facts at the existing query points; retain device and resource ownership. |
| `engine-core`, `engine`, Archive/Preview, Kauai/BRender wrappers | Still require C++ outside the converted slice; these remain migration work. |

The C interface has caller-owned stack records and explicit initialization.
Evaluators borrow live playback state, so scene effects can change waits
between command replies. Apply and acknowledge commands in order, then stop
at `FINISHED`. Protocol misuse returns failure. Timestamps use checked-width
32-bit C89 types with wrapping arithmetic, preserving legacy rounding without
C99 integers or a dependency on the Kauai arithmetic runtime.

Build and test this slice independently using only a C compiler:

```console
cmake -S libraries/engine -B build/engine-c89 -G Ninja -DCMAKE_C_COMPILER=clang
cmake --build build/engine-c89 --target engine-c89-test engine-movie-data-test engine-chunky-test engine-strings-test engine-storage-test engine-collections-test engine-data-stream-test engine-codec-test engine-piece-table-test engine-block-io-test engine-block-owner-test engine-file-policy-test engine-chunk-save-test engine-chunk-heap-test engine-chunk-edit-test engine-chunk-graph-test engine-chunk-ops-test engine-metadata-test engine-forest-test engine-loading-test engine-chunk-storage-test engine-lifecycle-test engine-container-test engine-resource-cache-test engine-chunk-owner-test engine-chunk-table-test engine-tag-test engine-source-test engine-source-names-test engine-movie-owner-test engine-rollcall-storage-test engine-movie-title-root-test engine-scene-data-test engine-actor-data-test engine-actor-track-test engine-actor-motion-test engine-actor-frame-test engine-actor-timeline-test engine-actor-pose-test engine-actor-effect-test engine-actor-sound-test engine-sound-edit-test
cmake --build build/engine-c89 --target engine-event-edit-test
cmake --build build/engine-c89 --target engine-action-edit-test
cmake --build build/engine-c89 --target engine-costume-edit-test
cmake --build build/engine-c89 --target engine-route-edit-test
cmake --build build/engine-c89 --target engine-subroute-edit-test
cmake --build build/engine-c89 --target engine-placement-test
cmake --build build/engine-c89 --target engine-stage-edit-test
cmake --build build/engine-c89 --target engine-record-test
cmake --build build/engine-c89 --target engine-transform-edit-test
cmake --build build/engine-c89 --target engine-actor-owner-test
cmake --build build/engine-c89 --target engine-actor-undo-test
cmake --build build/engine-c89 --target engine-group-undo-test
cmake --build build/engine-c89 --target engine-actor-transaction-test
cmake --build build/engine-c89 --target engine-actor-copy-test
cmake --build build/engine-c89 --target engine-route-paste-test
ctest --test-dir build/engine-c89 --output-on-failure
```

The root build also registers `engine-c89` and `engine-c89-syntax`; the latter
uses strict C89 diagnostics when Clang or GCC is present. Existing
`engine-core-link`, `movie-save-load`, `marcus-player` and `marcus-playback`
tests cover the facades and runtime integration. Playback corpus evidence and
its limits are recorded in [playback regression](../tools/playback-regression.md).

### Movie-data reading slice

`three_mm/read_stream.h` adds borrowed, positional decoded-chunk input,
checked memory input and bounded substreams. Every read checks offset and size
without overflowing; nested slices retain their parent's bounds. Memory and
slice contexts point to their caller-owned objects, which must remain in place
and outlive their borrowers. Filesystem and content selection still use the
existing VFS/host path; this API adds no separate filesystem or archive cache.

`three_mm/movie_data.h` implements the following in C89:

- Eight-byte MVIE prefix decoding in either byte order, with the existing
  signed version predicate and separate bad-file/bad-version/I/O results.
- Twenty-eight-byte roll-call record decoding, signed actor IDs, actor-limit
  updates and source-ID validation. The stored pointer slot is ignored. Failed
  decoding leaves the output and caller's actor limit unchanged.
- Archive's existing dense GST layout checks: stride/count arithmetic, string
  starts, length-prefixed strings, null code units, encoding and entry offsets.
  A C89 bitset replaces the previous `vector<bool>`; no C99 or 64-bit arithmetic
  is required. The caller's existing decode budget remains authoritative.

The public records contain decoded values, not native structs to write to disk.
`inc/engine_read_stream.h` temporarily adapts `DataBlock` to this C interface.
`MovieData::FVerifyVersion` and `FReadRollCall` call the C decoders. Archive uses
the returned movie header and C GST validator; Marcus uses those same version
and actor-record checks when opening a movie. It now rejects unsupported MVIE
versions and bad byte-order markers before selecting scenes, matching Studio's
version policy. Standalone scene fixtures remain supported.

| Remaining dependency | Responsibility retained in C++ |
| --- | --- |
| `ChunkyFile`, `DataBlock`, codecs | Chunk graph mutation/save policy, free maps, embedded-forest decoding, decompression and payload storage. Index reading, graph validation, live chunk/child queries and the underlying array/group algorithms now use C89. |
| `StringTable_GST`, platform text services | Object lifetime, wire adapters and native code-page/locale services. GST header policy, storage ownership, ordered I/O, mutation, packed storage conversion and portable text rules now use C89. The host still supplies memory diagnostics and DataBlock callbacks. Studio's roll-call loader retains its existing GST acceptance rules; Archive's stricter checks are not imposed on it. |
| `TagManager`, `ChunkyResourceFile` | Opening/closing runtime tags and resource references, including rollback after failed loads. |
| `MovieData`, Studio document | Title/scene enumeration, transactional object replacement, editing and serialization. |

The C-only build now includes `engine-movie-data-test`. Run that target along
with `engine-c89-test` before CTest. Tests cover nested stream bounds and I/O
failures, both endian encodings, the version matrix, historical negative actor
IDs, ignored pointer bytes, transactional failures and malformed GST layouts.
The existing Archive, engine-core, save/load and headless Marcus tests exercise
the adapters and resource ownership. The shared backend is still incomplete.

### Chunky container reading slice

`three_mm/chunky_file.h` owns CHN2 header and GG index decoding, compact and
expanded records, unsigned chunk/child searches, typed enumeration and graph
validation. Both Studio and Marcus reach it through `kauai-core`. The C library
has no dependency on Kauai. Its temporary owned wire index is copied into the
writable C chunk table, then released. Subsequent queries use C callbacks over
that live table, including after edits; there is no second index cache.

The decoder preserves separate header/index byte orders, old flag layouts,
expanded-to-compact checks made before byte swapping, and legacy name bytes.
The C materializer sequences pre-v3 name conversion through the text host and
discards oversized names. Graph
validation retains aggregate reference totals and reachability policy rather
than imposing a new per-node reference-count rule. An iterative walk handles
deep graphs without recursive stack growth.

Mutable groups, free maps, writes, embedded-forest decoding, stored-name policy
and payload codecs now have the C implementations described below. Native
facades and remaining storage/cache policy still prevent a C-only container.
The existing type-relative `FGetCkiCtg` payload ordinal is preserved pending a
separate retail behavior check.

`engine-chunky-test` covers endian/version/layout combinations, malformed
bounds, failed reads, transactional index replacement, unsigned keys, live
mutations, DAGs, cycles, missing targets and a 10,000-node chain. It also runs
under the strict C89 syntax check.

### String-table storage and text conversion slice

`three_mm/string_table.h` implements dense and allocated string-table mutations:
insert/add, length-changing replacement, delete, move, free-slot reuse and tail
trimming. It preserves packed-string order, adjusts native 32-bit offsets and
keeps caller extra bytes opaque. Exact searches skip free slots; sorted searches
retain midpoint and insertion-position behavior through the existing comparison
callback. Offset byte swapping never touches extras.

`VirtualStringTable` supplies borrowed buffers and a reserve callback over the
C-owned collection storage described below. C owns mutation rules and updates
the logical counts; the adapter retains object lifetime, GST wire adaptation and
`DataBlock` callbacks. A failed ordinary mutation retains data/counts, even if one
allocation grew. As before, a failed loading translation can leave partial
state and the loader discards that object. No stricter Archive GST acceptance
policy is applied to Studio.

`three_mm/text.h` owns fixed lossy Mac/Windows byte maps, portable CP1252/Mac
Roman decoding, UTF-16 byte order, portable substitution/case/comparison rules,
and conversion dispatch. `platform_text.cpp` supplies Windows `CP_ACP`/`CP_MACCP`
callbacks and native locale/case services. It remains sensitive to the actual
Windows ACP; fixed CP1252 is not substituted on that host. String-table physical
walks and width-changing reconstruction are C89 and use the same conversion
service as ordinary String readers. Internal length-first/signed-character
sorting and user locale comparison remain distinct host comparison callbacks.

`engine-strings-test` covers storage moves and edits, metadata, free-slot reuse,
allocation failures, search positions, endian offsets, one/two-byte strings,
legacy map hashes, surrogate substitution and short output buffers. Native
`string-compat` adds dense/allocated save/reopen checks in both byte orders and
Mac/Windows encodings, UTF-16BE loading and duplicate independence. Text-service
checks compare native callbacks directly with Win32 and force portable policy
on Windows. The standalone platform-check project links the same C text module.

### Collection storage foundation

`three_mm/collection_storage.h` owns the two buffers underneath `GroupBase`,
covering arrays, groups, string tables and the chunky-file live index. It handles
allocation lifetime, growth hints, nested pointer locks, duplication and ordered
header/first-section/second-section transfers. `GroupBase` no longer stores HQ
handles. Its adapters pass plain buffers, allocator callbacks and bounded
`DataBlock` read/write callbacks into C89.

The standalone C default uses the C heap. Existing hosts instead supply the
fixed-memory allocator to retain memory-liberation callbacks, request priorities,
debug poisoning, allocation failure injection and the `dmaglHq` diagnostic
category. Those host diagnostics and fixed-memory services remain C++ migration
work; this does not mark all of Kauai's allocator converted. Debug marking now
marks the fixed allocations directly, without an HQ prefix.

The C growth policy first attempts the larger growth-hint allocation at speed
priority, then the exact request at normal priority. Two-section operations
retain successful first-section changes if growing the second fails. Shrinking
is best effort and precedes growth. A failed shrink now retains its actual
capacity; the old wrapper incorrectly updated the recorded size after ignoring
the failed resize. Oversized speculative requests are skipped using checked
arithmetic. No collection header or movie wire layout is changed.

Locks protect a section's address from resizing. Ordered I/O locks each active
section only for its callback and releases that lock on failure. Completed
transfers remain visible when a later callback fails. Copies preserve only the
requested prefixes and inherit source growth hints after successful allocation.

`engine-storage-test` exercises a relocating allocator, speculative fallback,
partial two-section failures, failed shrink, limits, locks, independent copies,
wire order and each I/O failure boundary. Core link tests check the actual host
failure hooks, capacity accounting, lock lifecycle, group duplicate independence
and balanced allocation statistics. Standalone C and C++ host builds include
the same public header.

### Array/group mutation and wire headers

`three_mm/collections.h` and `collections.c` implement dense/allocated arrays
and groups over the C storage foundation. The native classes publish logical
counts from borrowed views; mutation inputs must not alias the storage being
edited. C owns insert/delete, replacement, resize/pop, moves/swaps, first-free
slot reuse, tail trimming, variable-byte insert/delete/transfer, merge and group
entry copying. Fixed bytes and variable bytes remain opaque to these algorithms.
Group locations retain the native two-word layout and four-byte data padding.

Allocated arrays retain unused bitmap bits. Dense null insertion retains the
existing untouched-byte behavior; debug poisoning remains distinct from release
storage. Group reservation keeps the legacy conservative budget. Failed
reservations retain logical contents and counts, even if a first buffer grew.
The original physical byte-transfer and padding algorithm is preserved. The
tests do not establish retail behavior for the inherited zero-fixed,
empty-destination aligned-block-transfer edge case.

`collection_headers.c` decodes/encodes GL12, AL16 and GG20 headers in either byte
order with checked products and bounds. It retains the existing header policy:
AL negative free counts pass header decoding, dense/allocated GG modes must
match, and an empty allocated GG can be written but its reader rejects it.
Header decoding adds no bitmap or location-graph validator. The native facade
retains the raw pre-swap OS-marker output. Array contents and GG fixed/variable
payloads are not byte-swapped; GG location words are swapped and restored around
writes, including failure paths. GST20 now uses the same C header codec with
its own acceptance rules, described below.

`engine-collections-test` uses independent wire encodings, bitmap boundary/slot
reuse checks, allocation failures and 12,800 deterministic model steps across
dense/allocated groups with fixed prefixes from zero through seven bytes. The
same test passes under MSVC AddressSanitizer. Native core checks exercise
byte-transfer/merge, dense/sparse array state, both endian save/reopen paths,
opaque payloads and the raw OS-marker result. C-only and strict C89 builds pass.

Native collection objects, typed pointer access, diagnostics and callback
adapters remain C++.

### GST headers, byte streams and block decoding

`collection_headers.c` also owns GST20 encoding/decoding: checked stride/count
products, dense/allocated mode, free count and exact payload length. It retains
empty allocated GST acceptance, independently of GG's empty allocated rejection.
Archive's stricter string-payload validator remains separate. Native adapters
swap offsets and translate text around writes, restoring both on failure.
`three_mm_strings_wire_system` retains the requested Mac/Windows family at the
host string width, including requests with a different Unicode width.

`three_mm/data_stream.h` and `data_stream.c` own memory-stream storage lifetime,
growth/fallback, replacement and bounded read/write. The allocator remains the
same host fixed-memory adapter used by collections, including HQ diagnostic
accounting. Both allocation attempts retain NORMAL priority and the legacy
nonzero shrink request's growth-hint behavior. Stream pointers are borrowed
under explicit locks. Live file-piece metadata is borrowed through callbacks;
C lookup/fetch retains EOF behavior and fills a failed read segment with 0xa3,
continues reading later segments and reports failure.

DataBlock range checks, signed boundary shifts and unpack transactions are C.
File-limit shifts retain the original delta bound, rather than imposing a new
bound on the resulting end. A failed file decode keeps the file reference and
releases only its temporary handle; a failed memory decode retains its owning
handle. Packed memory subranges remain unsupported and now reject safely while
retaining storage. Successful full-memory unpack also removes the original
assignment-in-assert typo that failed in debug builds. Packed format/decoded-size
parsing, codec selection/query, conversion dispatch and wrong-size detection
are shared C policy. KCDC/KCD2 algorithms and encoding are now C services,
described below.

`engine-data-stream-test` covers independent GST fixtures, overflow/range edges,
failed decode ownership, packed dispatch failures, relocating/failing allocation,
3,000 deterministic stream edits and failed piece reads. It passes under MSVC
AddressSanitizer. Native core tests exercise real full-memory pack/unpack,
rejected memory/file decodes, memory edits/locks/writes and mixed memory/file
pieces. Native string checks cover both endian orders, requested encoding-width
normalization and empty allocated GST save/reopen.

### KCDC/KCD2 algorithms and packed encoding

`three_mm/codec.h` and `codec.c` implement both formats without Kauai or C++
dependencies. Encoding preserves dictionary links, nearest/longest-match
selection, offset cost preferences, literal ordering, bit packing and tails.
The two temporary arrays use checked four-byte entries and NORMAL allocation
priority. The host retains its original fixed-memory allocator diagnostics;
the standalone API can use the C heap or an explicit allocator. Every failure
releases the scratch arrays and leaves the published count untouched.

Decoders read bounded low-bit-first fields instead of unaligned word lookahead.
They retain flags/tail checks, overlapping back references, all four offset
tiers, KCD2's split last literal byte and format-specific termination. Raw decode
can succeed with zero output; packed envelopes still require the advertised
positive decoded size. This conversion preserves the existing length-prefix
rules rather than changing the format or repairing legacy encoder edge cases.
The generated x86 assembly sources remain available to their generator tools;
`KauaiCodec` now dispatches to the shared portable C implementation on every
architecture.

`three_mm_packed_encode` shares the eight-byte big-endian envelope, codec
selection/query, input-size-minus-one estimate and minimum-saving policy.
It retains pre-conversion failure counts and publishes callback counts on
conversion failure. Native registration order and custom codec callbacks remain
supported; algorithm dispatch is no longer implemented in `codkauai.cpp`.
`CodecManager` object/registration lifetime and primitive HQ allocation/lock/resize
services remain C++ host services. The HQ conversion transaction itself now
uses C89 policy, described below.

`engine-codec-test` checks a frozen native hash covering 768 capacity/format/input
cases and decoded results, independent wire fixtures for each offset tier,
scratch-allocation failures, envelope queries/errors and 12,000 malformed inputs.
The same checks pass under MSVC AddressSanitizer. `codec-test --snapshot PATH`
also records exact native output for before/after binary comparison; its
existing thirteen round trips remain intact. Core link tests exercise real
allocator failure hooks and both formats through DataBlock pack/unpack.

### Owning file-piece tables

`three_mm/piece_table.h` and `piece_table.c` own FileByteStream's piece storage
and file-reference lifecycle over C89 groups. The native `GeneralGroup` object
is no longer required. Memory bytes and opaque file identities share one table;
host callbacks retain/release files and supply positional reads/writes, temporary
files and copies. File callbacks are not allowed to edit/re-enter a table.
Cross-table copies require the same file ownership domain.

C implements split/merge, memory replacement, file-copy versus shared-reference
insertion, cross-table whole/partial-piece copying and insert-before-delete
rollback. File pieces below 2048 bytes retain the memory-read policy. Allocation
uses the existing collection allocator, growth hints, padding and failure hooks;
locked storage can still be edited when no relocation is needed. The native
wrapper no longer allocates a separate group object. Inputs to ordinary memory
replacement must not alias storage. A same-stream memory append that fits the
existing storage is covered; general overlapping self-copies have no new
compatibility guarantee, and whole-entry copies still reject shared storage.

Compaction writes to a temporary file before replacing the original table,
retains original bytes/references on I/O or allocation failure and applies the
existing best-effort storage shrink. Empty, small-memory, mixed and single-file
cases are covered. The single-file fast path now checks entry zero: the old
native condition called `Cb(1)` for a table with only one entry and asserted.
Compacting an already single-file stream now safely keeps that file reference.

`engine-piece-table-test` checks the frozen pre-conversion native snapshot hash
for 180 mixed memory/file/cross-stream edits (601,476 bytes), independent byte
models, whole-piece copying, allocation failures at twelve positions for three
operations, temporary/read/write/copy failures, rollback and balanced references.
It also runs under MSVC AddressSanitizer. Native core checks save/compare the
same edit snapshot and exercise repeated single-file compaction and same-stream
memory append. No Marcus UI checks are added.

FileByteStream destination windowing now also uses C89, described below,
with the existing lock and window restoration protocol. General overlapping
self-copy guarantees remain outside this slice.

### Block I/O and opaque handle transactions

`three_mm/block_io.h` and `block_io.c` own DataBlock byte/HQ I/O routing,
memory/file copy dispatch, FileLocation copying, handle detach/normalization,
codec HQ transactions and block-packing decisions. The native view is borrowed;
the host adopts returned handles and releases prior file references only after
successful file-to-memory conversion. Primitive HQ operations and physical file
I/O are callbacks. The public interface has no C++ or platform types.

File-to-file copying retains the full-buffer SPEED allocation attempt followed
by a 1024-byte stack fallback. Copying stays forward and retains partial writes
on I/O failure. Same-file overlap remains allocation-dependent, as before;
this conversion does not impose snapshot semantics on the fallback. Memory
copies require non-overlapping ranges. Source packed access requires explicit
permission; destination packed flags are preserved, including the existing
destination-memory write behavior.

Handle reads allocate before I/O, balance locks and release temporary handles
on failure. Taking a memory slice consumes its view, shifts bytes to offset
zero and attempts an exact shrink; taking a file block leaves its file view
and returns a new handle. Codec transactions query while the source is locked,
unlock before allocation, convert with both handles locked, unlock both, attempt
shrink and commit only after success. Failed query/allocation/conversion retains
the original handle. Best-effort shrink retains the physical size on failure
and reports the existing host diagnostic. Packing a sliced memory block retains
its normalized handle even if encoding fails; failed file packing leaves its
original file reference and releases the temporary handle.

`engine-block-io-test` checks the 80-case memory/file/packed/slice matrix,
zero-length handle versus empty-block behavior, relocation, lock cleanup, query/
allocation/conversion failures, shrink failure, partial read/write errors,
forward overlap with both allocation paths and packing transactions. It also
passes under MSVC AddressSanitizer. Native core checks compare 99,296 bytes
captured before conversion and exercise real HQ allocation failure, copy-buffer
fallback, sliced/failed packing and both codecs through HQ and file blocks.

### Owning DataBlock state and materialization

`three_mm/block_owner.h` and `block_owner.c` now hold DataBlock's owning state.
The C record owns one file reference or opaque handle; it is initialized once
and cannot be copied/moved. File/handle services and their contexts are borrowed
for its lifetime. I/O descriptors borrow that record and expire on mutation.
The native DataBlock is a facade over this state, rather than a separate set of
C++ file/HQ/range/packed fields.

C owns clear, borrowed-file retention, independent-handle adoption, state
replacement, temporary selection and file materialization. Temporary selection
retains the strict below-8-MiB memory attempt, then file fallback; failure keeps
the old state intact and success preserves its packed flag. Temporary file
selection does not extend the file before writing. Materializing a memory
slice creates a file with just its logical bytes and leaves the block unchanged.
Materializing a file retains its existing file/range. Empty and zero-memory
blocks return no file, while zero-length file blocks retain their file.
Failure releases any new temporary file and leaves the caller's output intact.

The owner also routes pack/unpack and handle-taking through the C transaction
helpers, commits changed memory views and releases prior file references only
after successful conversion. Failed sliced packing retains its normalized
owning handle, as before. Native services retain physical file creation/I/O,
reference-count primitives, HQ operations, diagnostics and memory marking.

`engine-block-owner-test` checks a frozen native hash across 24 size/force/packed
combinations (17,240 bytes), including both sides of the 8-MiB boundary, sliced
materialization, zero-length distinctions and returned-file lifetime after
clearing the block. Allocation/temporary/write/read/codec failure checks cover
unchanged prior owners, unchanged output on failure, balanced locks/references
and successful/failed conversion commits. The same check runs under MSVC
AddressSanitizer. Native core checks compare the complete pre-conversion bytes
and exercise the real failed-memory-to-file fallback.

### Source catalogue, lookup and manager construction

`source.c` owns source records and catalogue storage, persistent opaque path
handles and resource-manager references. It replaces TagManager's DynamicArray
of native SourceFileStructure values. Work paths are caller-owned scratch copied
from persistent paths; C publishes at the same mutation points as before. Failed
path creation or array growth releases partial owners. Invalidation scans in
reverse; disposal releases managers forward, then path handles and storage.
Catalogue records/managers returned to callers are borrowed.

C owns local-content caching and local/media source selection. Provider denial
precedes cached-manager access. Local-only misses never prompt or open media;
already-open media managers can be reused locally. Media-enabled lookups verify
media before returning cached managers. Retry after a local installation resets
the cached local miss when no manager is open; existing managers retain their
path. Explicit archive sets bypass content-directory probes and directory file
enumeration. Ordered manager construction releases the current file and partial
manager on any open/add/init failure.

C also owns local/media path retrieval and file-search sequencing, preserving
legacy output quirks: an AVAILABLE media-provider shortcut leaves the existing
output unchanged; failure obtaining a local directory ends file search without
trying media. Final output is cleared only after both leaf searches fail.
Native callbacks provide path values, source availability, physical directory
and drive operations, file I/O and UI prompts. Source-name storage and discovery
policy now use C as described below; the full engine migration is incomplete.
Path handles currently wrap native Filename pairs, without native
source-policy fields. Callbacks may append other sources but cannot invalidate
the active acquired record; disposal callbacks cannot re-enter.

`engine-source-test` checks provider denial before cached access, local-only
misses, retry installation, archive sources, cached misses and media managers,
probe/manager failures, path/array allocation failure, relocating storage,
invalidation, locked-owner guards, output quirks and forward cleanup order.
Builder checks cover archive/directory selection and create/init/open/add failure.
MSVC AddressSanitizer passes. All 60 native prompt-trace bytes match, as do the
previous eleven snapshots and 51 CLI baselines. Twenty-nine standalone and
forty-two focused native checks pass. No Marcus UI tests were added.

### Movie ownership and roll-call persistence

`movie_owner.c` owns MovieData's resource/table references, movie number, actor-ID
limit, title and scene count. Native MovieData now inherits this C state and exposes typed
borrowed handle accessors, with no duplicated native ownership fields. C performs
empty initialization, transactional load/replace, contiguous scene counting and
save ordering. Failed initialization/load preserves the old movie. Save retains
legacy partial publication: after writing/adopting a new roll call and removing
the old child, a name/save failure leaves that replacement visible. Only successful
explicit save makes a temporary file persistent.

C owns roll-call row conversion, extra-width checks, tag-open/append sequencing,
partial-failure cleanup and output publication. It closes a just-opened tag before
earlier rows on failed insertion, releases the wire table before unwinding runtime
rows, and preserves caller ID limits on failure. `movie_data.c` writes 28-byte actor
extras in either byte order, with a zero pointer slot. Roll-call storage, decoding
and header writes now use the owning C table described below. Title/root/version
policy now uses C as described below; physical I/O and typed tag/file thunks remain
intermediate host dependencies.

Studio uses the same state and roll-call paths. Its existing early resource release
during Movie destruction remains explicit, before closing the scene and actor list;
the general MovieData destructor closes actors before releasing resources.
`engine-movie-owner-test` covers transactional failure, reference lifetime, cleanup
order, failed row insertion, both byte orders, invalid rows/extra widths and partial
save publication. Native persistence checks exercise real files, embedded tags,
repeated save/load and failed-load preservation.

### Owning roll-call storage

`rollcall_storage.c` owns runtime and wire roll-call names/extras in two C buffers.
It replaces persistent and transient StringTable_GST objects in MovieData's actor
paths. C owns append/rename/delete/extra updates, dense GST reading, offset swaps,
legacy string translation and GST output. Runtime extras use ThreeMMRollCallActor;
wire extras remain 28 bytes. Opaque tag references are transferred, never retained
or closed by table mutation/destruction. Movie transactions and existing edit
rollback retain responsibility for those references.

Reading reuses the legacy dense GST header rules rather than Archive's stricter
validator. Same-width legacy maps stay in C; width-changing text uses the host
converter. Failed reads may leave partial loading state and must be discarded.
Failed writes restore native offsets and release locks. Mutation failures preserve
logical rows while allowing successful partial capacity growth.

The temporary native RollCallTable class provides BASE allocation/diagnostics and
typed String/TAG conversions for existing callers. Its only table state is the C
record; it has no GST base class or native table-policy implementation. Studio's
roll-call edits and movie-merge rollback use the same C table. Physical file/text
conversion services and final facade
removal remain migration work.

`engine-rollcall-storage-test` checks relocating buffers, partial allocation
failure, rename/delete/extra mutation, both header byte orders, untouched extras,
foreign-width translation, failed read/write and locked disposal. The native
405-byte mutation/GST snapshot and fourteen earlier snapshots match. Thirty-two
standalone C checks, forty-five focused native checks, MSVC AddressSanitizer and
all 51 CLI baselines pass. No Marcus UI tests were added.

### Movie titles, root creation and version checks

`movie_title.c` owns MovieData's counted title in the C state, with native one/two-
byte units and no encoding normalization. Initialization/load commit title together
with the other movie fields; failures preserve the old title. C also owns filename
extension stripping, default-name choice and notification policy. A leading dot
counts as an extension boundary. Without a filename, a nonempty title is retained
without notification. Default text, filename leaves and UI notification are host
callbacks; native adapters copy their results into C state. Save-dialog callback
mutations are published even when the dialog cancels, preserving legacy behavior.
Studio now uses the existing copied-name query instead of a borrowed native title.

`movie_root.c` owns first-movie selection, version-error classification and root
creation, including explicit 8-byte prefix encoding. Selecting the first movie
publishes its number before header validation; null number still selects the first
movie. Failed verification preserves the header. Failed root writes delete the
added chunk before publishing nil; failed addition retains callback output without
extra cleanup. Existing version predicates remain in `movie_data.c`.

Native file/block operations, locale/code-page conversion, localized text lookup,
and frontend presentation remain host adapters. There is no persistent native
MovieData title or duplicate native root/version policy. The engine still has
other C++ ownership, editing/evaluation and final integration work.

`engine-movie-title-root-test` checks both unit widths, bounded/overlapping title
copies, default/notification order, extension edge cases, both prefix byte orders,
selection publication and I/O/add/write failures. Movie ownership tests also check
title preservation on failed replacement. The native save/load check covers a
cancelled save callback mutating the title. Thirty-three standalone C checks,
forty-six focused native checks, MSVC AddressSanitizer, sixteen binary snapshots
and all 51 CLI baselines pass. No Marcus UI tests were added.

### Scene state and event persistence

`scene_data.c` owns SceneData's frame cursor/range, transition and counted name.
The native SceneData adapter inherits this C record without duplicate fields.
Header decoding/writing and scene read/write/create policy use C. A failed read
preserves all old state; a successful read sets the cursor to firstFrame minus one.
Frame/transition acceptance and the 16-byte header remain unchanged. Header writes
precede names and are not rolled back on name failure.

C validates scene event fixed widths, signed payload lengths, type/size combinations
and sound tag-array lengths in either byte order. It retains legacy acceptance
of payload values and frame ordering. Event-list persistence validates both groups before file changes,
adopts both new children before removing old links, and clears loner flags only
after success. Failure cleanup proceeds frame then start, removing adopted links
or newly created chunks as appropriate. Scene creation allocates both temporary
groups before publishing nil, releases them in order, and removes a successfully
created root on later failure.

Live Scene, Archive and Preview continue through the native adapters to the same
C state/validation/persistence. Native group objects still wrap C collection
storage, and physical file/group operations remain host callbacks. Runtime scene
objects, event marshalling/playback/editing and final facade removal remain work.

`engine-scene-data-test` covers both byte orders, signed frame limits, event-size
and sound-count rules, metadata read/write failures, six replacement failure stages,
creation allocation failures and cleanup order. The existing native scene check
still exercises actual archive allocation failures and retained old child links.
Thirty-four standalone C checks, forty-seven focused native checks, MSVC
AddressSanitizer, seventeen binary snapshots and all 51 CLI baselines pass. The
new scene/name/header/event snapshot is 526 bytes. No Marcus UI tests were added.

### Actor header, route and event persistence

`actor_data.c` replaces the actor persistence policy formerly in native ActorData.
It reads 40/44-byte headers, expands the old missing-last-frame layout, swaps scalar
bits and performs checked signed ID adjustment without C99 arithmetic types. As in
the native reader, read failures may modify header output; invalid byte order leaves
read/expanded bytes visible, and the old last-frame sentinel is assigned only after
byte-order validation. Scalar/header payload values remain accepted as before.

C validates route widths and normalizes foreign-order scalar bits. Empty routes
never request a first-row pointer. Invalid routes are released. Event code validates
all thirteen type/size combinations, preserving signed payload lengths and existing
value/frame-order acceptance. C owns foreign-order swaps and wire/runtime conversion;
Add uses three 32-bit scalar swaps plus three 16-bit angle swaps, leaving padding
opaque. Runtime Costume/Sound records use public C structs, with native ABI assertions.

Reading consumes wire groups on every outcome, normalizing before allocation and
releasing wire before partial runtime storage on failure. Writing borrows runtime
groups and releases only partial output on failure. Tags stay unopened/unretained;
read sets runtime cache pointers to null, while write zeros wire pointer slots and
ignores cache identity. Native file/block/group services and typed interfaces remain
temporary adapters. Track/tag ownership now uses the C state described below;
actor evaluation/editing remain work.

`engine-actor-data-test` covers old/current headers, partial failed reads, invalid
byte-order outputs, ID overflow, empty/foreign routes, all event layouts, Add padding,
tag fields, and allocation/insertion failures at every row. Thirty-five standalone
C checks, forty-eight focused native checks, MSVC AddressSanitizer, eighteen binary
snapshots and all 51 CLI baselines pass. The new header/all-event snapshot is 2,896
bytes. No Marcus UI tests were added.

### Actor track state and embedded tag ownership

`actor_track.c` owns the route/event handles in `ThreeMMActorTrack`, replacing
native ownership fields and their lifetime policy. The temporary ActorTrack facade
exposes typed borrowed table accessors; actor evaluation/editing, preview and test
fixtures use those accessors. Native services perform primitive allocation, table
copying/locking and cache retain/release operations.

C creates event tables with 20 fixed bytes, ten initial rows and twenty variable
bytes, and route tables with 16-byte rows, 32 initial rows and minimum growth 32.
Creation and cloning publish only after success, then dispose old state. Clone
copies route first and acquires embedded tag references immediately after copying
event bytes. Allocation failure leaves the destination and its references intact.
Self-clone and self-swap are safe. Empty and partially populated tracks are accepted.

C identifies Costume tags only when the custom flag is zero, and Sound tags for
all Sound rows. It copies unaligned tag bytes to aligned local storage before
access. Only sid-zero, non-null caches own references. Close clears cache pointers
in their rows and tolerates repeated calls. Cleanup closes tags before releasing
event tables, then route tables. Adoption transfers a handle and disposes the old
table; adopting the current handle is a no-op. Callbacks cannot mutate/re-enter the
participating tracks. Runtime evaluation/editing and physical table adapters remain.

`engine-actor-track-test` covers every event kind, unaligned tags, custom flag
values, unopened/positive/invalid-source tags, allocation failures, reference and
cleanup order, replacement, self operations and partial/empty tracks. Thirty-six
standalone C checks, forty-nine focused native checks and MSVC AddressSanitizer
pass. Nineteen binary snapshots and all 51 CLI baselines match, including a new
192-byte native track/clone snapshot and the prior eighteen snapshots. No Marcus
UI tests were added.

### Actor value state and route evaluation

`actor_state.c` owns state initialization, transform reset, route rewind, origin
updates and pure value-event application over the public `ThreeMMActorState` C
record. Rotation, stretch, scale, step, freeze, tweak and move events execute in C.
Resource/stage/sound events return NeedsRuntime without payload validation or state
changes, preserving the native host boundary. Invalid pure layouts and zero-step
locations leave state unchanged. Rewind preserves stage visibility, current frame,
cached action metadata, translations and the current-rotation flag.

`actor_route.c` owns route advancement, Action/Step event clamping, subroute terminal
handling, location-to-position evaluation and alias-safe interpolation. A route
location addresses a valid segment, and each subroute terminates at a zero-distance
node. Arrival at a terminal node and a subsequent attempt to move beyond it remain
distinct. Zero steps accumulate dwell frames. Only primitive table reads are host
callbacks; C performs the arithmetic and branching.

`actor_fixed.c` implements wrapping 32-bit addition/subtraction, 16.16 multiplication
rounded toward negative infinity, and division with the original small-divisor
reciprocal/saturation policy. It uses unsigned 16-bit partial products and binary
long division, with no C99 integer types or floating-point approximation. The native
adapter asserts fixed-point ABI and every copied route/event/transform layout.
Native ActorFrameState still translates its value fields into/out of the C record;
this temporary facade, host effects and physical
table services must be removed as migration continues.

`engine-actor-motion-test` covers fixed-point boundary cases, subroute termination,
clamping, output aliasing, rewind preservation, all pure event kinds, deferred runtime
kinds and invalid layouts/locations. Thirty-seven standalone C checks, fifty focused
native checks and MSVC AddressSanitizer pass. Native checks compare 4,096 arithmetic
pairs against BRender and match new 33,384-byte route/arithmetic and 4,142-byte state
snapshots. All twenty-one binary snapshots and 51 CLI baselines match. No Marcus UI
tests were added.

### Actor frame command sequencing

`actor_frame.c` owns one-frame sequencing in `ThreeMMActorFrame`. C advances the
route, prescans entrances, emits entrance queries and event commands, accepts host
acknowledgements and computes final result flags. Host effects still execute
between Next and Complete; typed native commands/results and state translation
are temporary facades. The host may refresh event frame caches and state but
cannot structurally edit the borrowed tables or increment the event cursor.

C retains ordinary versus ordered entrance handling, initial freeze latching,
Action cel suppression, terminal-route cel rules and X/Z-based rotation selection.
Ordinary sounds are skipped during hidden frames or sound suppression while
motion-match registrations still produce commands. Displayed suppressed sounds
still set soundInFrame. Ordinary failure marks failure and advances the scan while
retaining nextEvent; following commands deliberately target that retained cursor.
Entrance failure aborts without final body/pose/cel work.

Next rejects an unanswered entrance query without changing storage/output, while
an event command can be fetched repeatedly until acknowledged. Complete rejects
an empty pending slot without changes. All policy executes in C; callbacks only
read table metadata/rows. State conversion helpers are shared with native value/
route adapters. Pose, host effects and editing remain migration work.

`engine-actor-frame-test` exercises entrance order, protocol rejection, repeated
requests, both entrance abort paths, ordinary retry ordering, all sound display/
suppression combinations, recording/rejoining, freeze/action latches, vertical
motion and terminal-route behavior. Thirty-eight standalone C checks, fifty-one
focused native checks and MSVC AddressSanitizer pass. Twenty-two binary snapshots
and all 51 CLI baselines match, including a new 170,230-byte native frame trace
covering command/state/result/effect records during sequential playback and seeks.
No Marcus UI tests were added.

### Actor timeline, metadata steps and seek control

`actor_timeline.c` owns raw/playback step policy, lifetime calculation and event
frame-cache updates, stalling detection, backward steps and quick backup. Metadata
resolvers return unscaled steps; their output starts at zero and failure preserves
any partial write. Playback scaling occurs only after successful resolution. C owns
the branches and 16.16 arithmetic; native services read metadata or table rows,
write fixed event records and present diagnostics.

Lifetime publishes each attempted frame and updates caches immediately. Failure
retains partial caches and the last attempted frame. Its signed maxFrames check
uses C89 two-word arithmetic, retaining the native 64-bit subtraction behavior at
frame limits. No-entrance tracks still return firstFrame without applying the
budget. Stalling preserves the separate node/offset comparisons. The enabled
retail BUG1960 stationary-event and BUG1906 full-delta backup policies are retained.

A failed backward step restores cel/frame/location and leaves output unchanged.
Successful probes restore those state values while publishing their computed
location separately. Quick-backup failure retains completed earlier steps and
vertical transitions request full replay. Optional publication services keep native
observer views current before metadata/diagnostic callbacks and lifetime writes;
callbacks see the decremented cel during a backward-step metadata request. The
native adapter uses value conversion for distinct 32-bit long/int types.

`actor_seek.c` owns control state in `ThreeMMActorSeek`: backward/midpath rewind,
quick effects, hide/costume commands and frame evaluation requests. Hosts still
apply commands and acknowledge them. Hide/costume failures remain best-effort;
frame failure accumulates while later frames run, and quick failure stops. The
before-first forward-frame compatibility quirk is retained. Protocol rejection
leaves control/state/output unchanged. Native value/command facades and
resource/effect/editing integration remain migration work.

`engine-actor-timeline-test` covers resolver writes/failures, callback observations,
cache updates/bounds, stalling, step/probe rollback, partial quick-backup progress,
vertical fallback, midpath rewind, seek commands and protocol failure. Thirty-nine
standalone C checks, fifty-two focused native checks and MSVC AddressSanitizer pass.
Twenty-four binary snapshots and all 51 CLI baselines match, including new 4,438-byte
timeline/cache/callback and 26,564-byte seek command/state/effect traces. No Marcus
UI tests were added.

### Actor pose and fixed matrix/angle math

`actor_pose.c` owns pose evaluation, route orientation and vector rotation. It
retains the retail BUG1899 entrance-orientation rule, rest X/Y/Z rotation order,
stretch and uniform scaling, and the final unscaled path rotation saved to state.
Weighted tangents use 4/2/1 contributions without crossing earlier subroutes.
The inherited signed-component norm comparison, exclusive upper clamp, zero-angle
rotations and independently optional outputs remain observable behavior.

`actor_math.c` implements wrapped 16.16 multiply-accumulate using two-word integer
products, matrix composition and BAM trigonometry. Original BRender sine/atan
tables and integer interpolation preserve signed 16-bit fraction truncation and
the 65534 zero-angle cosine. It needs no floating point, libm, native arithmetic
callback or shared scratch matrices. Original Argonaut notices are retained.

The native pose facade supplies table views and explicit value conversions for
the distinct native long/C int types. It maps aliased vector/matrix/axes outputs
and preserves publication order; it no longer implements pose or math policy.
Native state/table facades, resource/effect/editing logic and final backend
integration remain migration work.

`engine-actor-pose-test` covers integer boundaries, matrix rounding, route weights,
optional and aliased outputs, current/authored orientation and scaling. Forty
standalone C checks, fifty-three focused native checks and MSVC AddressSanitizer
pass. Independent comparisons cover all 65,536 sine/cosine angles, 4,096 MAC/atan
cases and 128 matrix chains. Twenty-six binary snapshots match, including new
561,152-byte math and 111,872-byte pose traces with 256 varied poses and alias
cases. All 51 CLI baselines match. No Marcus UI tests were added.

### Shared actor event effects

`actor_effect.c` owns cached event-frame writes and dispatch of pure and runtime
events. Action changes clear motion sounds before cel setup, publish action/cel
before count lookup, preserve failed count outputs, then update route location
before querying flags. Failed flags retain the prior flag value. These ordered
partial changes follow Studio's retail-compatible behavior in both hosts.

Entrance policy saves the costume, applies defaults, and restores the saved
costume only if cel setup fails. It then clears motion sounds, updates origin,
resets scale/orientation, applies the authored angles and updates stage state.
Material policy selects custom/ordinary fetch, rejects missing resources, applies
the result and releases the fetched reference. Body presence gates exit hiding;
logical stage state changes independently. Sound dispatch reports enqueue failure.

Studio's `_FDoAevCore` and Marcus's `PreviewActor::ApplyEvent` execute primitive
template/body/resource/audio commands and publish borrowed native observer views.
The C code owns their ordering and failure decisions. Marcus's timeline reader
uses the same C state policy in metadata-only mode, querying action metadata
without body/costume/sound effects. Preview visibility remains a presentation
adapter. `_LoadAddOrien` also delegates to C for runtime and editing callers.
The private native value/table adapter is shared with the archive preview target.

`engine-actor-effect-test` injects failures at action and entrance stages, checks
partial state and observer values, exercises custom/ordinary reference cleanup,
metadata-only evaluation, frame caching and pure events. Forty-one standalone C
checks, fifty-five focused native/preview checks and MSVC AddressSanitizer pass.
Twenty-seven binary snapshots match, including a new 13,824-byte entrance trace
compared independently with retained native BRender arithmetic across 256 cases.
All 51 CLI baselines match. A headless scene-preview check also verifies repeated
timeline reads preserve the live pose, sounds and rendered pixels. No Marcus UI
tests were added. Native sound/motion-match
policy, primitive resource implementations, editing and final integration remain.

### Actor runtime sound policy and motion storage

`actor_sound.c` owns motion-sound records using C collection storage, with the
original two-record growth quantum. Records borrow track TAG references. Clear,
indexed reads/writes/deletion, first cel/style replacement, append, transactional
clone and ownership swap execute in C. Copy inputs can alias an existing record;
cloning keeps independent storage without acquiring additional TAG references.

The same C sound-event and motion-playback functions drive Studio and Marcus.
Binding events store motion sounds without playing them twice. C selects matching
cels using the original signed remainder, resolves imported runtime tags, chooses
ordinary/loop/matcher priority, and orders fetch, queue and release. A missing
motion resource records failure but permits later matches; resolution or queue
failure stops. Failed motion resolution retains resolver writes in the list,
whereas ordinary events publish only after successful resolution. Every fetched
resource is released after successful or failed queue insertion. Nonpositive cel
counts fail safely. Marcus uses cue-only services with its cached action count;
resource resolution and device playback remain presentation adapters.

`ActorSoundList` is a temporary typed native view over the C owner. Actor creation,
loading, duplication, undo swap, editing accesses and destruction use that owner;
Marcus's former vector is removed. Native value copies preserve distinct native
long/C int types and TAG alignment. No native DynamicArray owns motion records.
Sound editing/query control, primitive resource/audio implementations and final
backend integration remain migration work.

`engine-actor-sound-test` injects allocation, resolution, fetch and queue failures,
checks partial tag writes and releases, and covers ordinary/cue-only dispatch,
negative and minimum cels, metadata failures and empty-list queries. Forty-two
standalone C checks, fifty-six native/preview checks, MSVC AddressSanitizer and
27 regression-harness checks pass. Twenty-eight snapshots match, including a new
43,328-byte comparison of 256 native DynamicArray/C list operations with cloning,
deletion and clear. All 51 CLI baselines and the headless scene/timeline check pass.
No Marcus UI tests were added. A dedicated generated actor-audio corpus case
extends the existing scene-audio case with ordinary queues, two motion cels,
binding replacement and sound-child remapping across scenes.

### Actor sound editing and event deletion

`actor_sound_edit.c` owns sound setting, default insertion decisions, volume edits,
ordinary/motion queries, deletion, future motion-event removal, current-frame sound
detection and bulk scene-tag resolution. It preserves partial query outputs after
each successful append, output-table ownership on failure, earlier motion-binding
lookup, action/frame stopping rules and the second metadata query after deleting
motion events. Failure at that second query retains prior event deletions and
leaves the motion list and dirty notification untouched.

`actor_edit.c` owns general event deletion order: close tags, delete the table row,
adjust only cursors strictly after it, publish state, then update the scene sound
indicator when the removed sound belongs to its current frame. Native callbacks
provide aligned tag access, table primitives and scene notifications. Query output
publication preserves native caller observations without reading uninitialized
output arguments. General event insertion and merging now also execute in C,
as described below; undo remains a temporary native editor integration.

Queue identity now uses a shared C helper. The inherited signed low word can
collapse style identities when its sign bit is set; that behavior is retained.
Default motion-sound insertion also retains its original positional parameter
binding. Sound-setting reservation failure now releases the fetched sound,
fixing the original reference leak while leaving the failed edit's data unchanged.
Other partial edits and best-effort preview ordering are retained.

`engine-sound-edit-test` covers partial append/resolve outputs, query ownership,
metadata failures, event/list edits, cursor publication, tag-resolution failures,
default binding, queue collisions and resource release on every setter failure.
Forty-three standalone C checks, fifty-seven native checks and MSVC AddressSanitizer
pass. Twenty-nine snapshots match, including a frozen 7,456-byte trace from fifteen
real-actor scenarios captured before conversion. All 51 CLI baselines and the
headless scene/timeline check pass. Native comparisons exhaust all 655,360
low-word/style/actor combinations against the original queue expression. No
Marcus UI tests were added. General editor/queue integration and final C++ facade
removal remain work; the full backend migration is incomplete.

### Actor event insertion and merging

`actor_edit.c` owns general event insertion, merge scanning and the
insert/merge/apply operation used by actor edits. `actor_sound_edit.c` calls it
directly; the native `insert_apply` callback is removed. Native services supply
table insertion/payload access, tag ownership, scene notifications and an adapter
to the already shared C event evaluator. These services are still temporary
dependencies, not completion of the C backend.

Insertion preserves the distinct strict/equal cursor comparisons and NULL-payload
reservation behavior. Merging retains event-specific scan and early-return rules,
signed-low-word sound queue identity, motion-list removal and result publication
before deleting the merged-away row. Move accumulation wraps at 32 bits. The
enabled retail BUG1870 policy applies only the caller's original delta to subroute
translation, without reapplying accumulated moves or refreshing route origin.
Failed event application retains prior edits and partial state. Range invalidation
observes the old extent before each first/last assignment.

Costume replacement intentionally fixes a reference ownership bug in the old
same-location merge: close the displaced tag and retain the replacement before
deleting its source row. Logical event data stays identical. Standalone C tests
and real actor/resource tests verify both references through final table teardown.

`engine-event-edit-test` covers ownership, reservation/insertion failure, cursor
boundaries, notification/result ordering, rotation scans, Move behavior and partial
apply failure. Forty-four standalone checks including strict C89 syntax, fifty-eight
focused native checks and MSVC AddressSanitizer pass. Thirty snapshots match,
including the 7,232-byte native trace frozen before this conversion over forty-five
event-edit scenarios. All 51 CLI baselines and headless scene/timeline checks pass.
No Marcus UI tests were added. Action/fill/subroute/recording edits, undo,
queue/runtime integration, resource/body implementations and facade removal remain.

### Actor action and transform fill editing

`actor_action_edit.c` owns action selection, step reservation/insertion, action
fill and transform fill. Native `FSetActionCore` and `FSetStep` translate values
and services. The C action editor calls the existing C sound editor and event
evaluator directly. Metadata queries, template-kind lookup, table access and
scene/lifetime notifications remain primitive native services.

Action changes reserve before querying metadata, query static flags even when no
previous action event exists, remove old motion events only on an action change,
fill before applying the new action, then insert default sounds. Lifetime
invalidation precedes freeze and optional authored-step restoration. Static/text
actors and completed subroutes retain their distinct step behavior. Failures
retain completed mutations and partial state, including failed event application.

Action fill compares action identity independently of cel and stops at stage
entrances or a third action. It preserves final subroute freezes. Transform fill
retains its original comparison scan excluding `first - 1`, the transition from
old to new payload comparison, event masks and route-location boundaries. Its
comparison pointers are borrowed under one table lock; no value-copy replacement
changes their aliasing as rows are deleted. Locking and payload views remain
temporary table services.

`engine-action-edit-test` covers sixty-four action/static/text/end/freeze combinations,
seven ordered failure stages, transform termination and final-freeze preservation.
All forty-five standalone C-only Clang checks (including strict C89 syntax),
fifty-nine focused native checks and MSVC AddressSanitizer pass. All thirty-one
snapshots match, including a 196,600-byte native trace frozen before conversion
across 528 transform-fill/action-fill/action-change scenarios. The 51 CLI baselines
and headless scene/timeline check pass. No Marcus UI tests were added.

Costume fill and material lookup, subroute/recording edits, general undo,
queue/runtime integration and final C++ facade removal remain unfinished.

### Actor costume editing and fill

`actor_costume_edit.c` owns costume setting and fill. Native `FSetCostumeCore` and
`_PrepCostFill` now translate values and primitive services. C owns custom body-set
lookup/release before reservation, comparison against the current body costume,
the transition to comparison against the incoming costume, deletion, insertion,
application and resource cleanup. The event editor/evaluator is already shared C.

Material and custom-costume fetches return owning references; the current body
material is borrowed. Pointer identity includes NULL equality, and custom flags
retain their exact values instead of normalizing nonzero values. The transition
event may be fetched twice. Release ordering stays explicit: current candidates
are released custom-first/material-second; owned comparison resources are released
material-first/custom-second before the final candidate. Add events terminate
fill; unrelated body sets are skipped. Failed lookups retain the original pointer
comparison behavior, and failed insertion/application retains prior fill deletions.

Native callbacks still supply body material queries, custom-material/template
lookup, resource fetch/release and table/scene primitives. These project-owned C++
resource/body implementations remain temporary dependencies.

`engine-costume-edit-test` checks the exact transition lookup/release sequence,
borrowed versus owning references, NULL matches, raw custom flags, 42 combinations
of current/incoming type and fetch failure, early custom resolution failure,
reservation ordering and partial failed edits. All 46 standalone C-only Clang
checks (including strict C89 syntax), 60 focused native checks and MSVC
AddressSanitizer pass. All 32 snapshots match, including the 47,536-byte native
trace frozen before conversion across 160 scenarios with real materials, custom
costumes, final body identities and resource reference counts. All 51 CLI baselines
and headless scene/timeline checks pass. No Marcus UI tests were added.

Subroute/recording edits, general undo, queue/runtime integration, resource/body
implementations and final C++ facade removal remain unfinished.

### Actor route and forward deletion editing

`actor_route_edit.c` owns route-point insertion, event relocation after node
insertion/deletion, forward/backward event searches, reachable-subroute lookup,
single-frame Add deletion, truncation, and private/public forward deletion.
Hide/reset/rewind ordering now also executes in C. Native adapters supply table
primitives, body visibility/default-costume operations, owner flags, scene dirty
notifications and diagnostics; project-owned table/body/resource implementations
remain temporary C++ dependencies.

Insertion preserves terminal zero-distance nodes and one-unit epsilon clamping,
including updates to the caller's point. Failure leaves it unchanged. Event
relocation wraps scalar offsets and deletes events attached to removed terminal
nodes through the shared C event editor. Add-frame deletion preserves nodes still
used by a preceding event or its positive offset. Reachable-subroute lookup reuses
the C stalled evaluator, retaining its existing comparisons.

Truncation writes the shortened segment before computing the new endpoint, moves
the next node, then updates cached events backwards. This order is deliberately
retained. Forward deletion distinguishes the current subroute from all later
routes, publishes dirty state before reset, assigns false `alive` before default-
costume reset, and does not invalidate the frame range for reductions. Enabled
retail BUG1870/BUG1932 behavior is retained. Public deletion reserves and adds
terminating freeze/step events only when onstage, then keeps the latest tweak
last. Expected-operation failures retain partial edits and their diagnostic order.

`engine-route-edit-test` checks failed insertion, epsilon/wrapped arithmetic,
truncation endpoint ordering, absence of range invalidation, alive/reset observer
ordering, terminal reservation/application failures and tweak ordering. All 47
standalone C-only Clang checks (including strict C89 syntax), 61 focused native
checks and MSVC AddressSanitizer pass. All 33 snapshots match, including the
158,436-byte trace frozen before conversion across 480 real actor route-edit
scenarios. All 51 CLI baselines and headless scene/timeline checks pass. No Marcus
UI tests were added.

Stage entrance/removal integration, event propagation across subroutes, backward
deletion, recording, undo, queue/runtime integration, remaining body/resource
implementations and final C++ facade removal remain unfinished.

### Actor backward deletion and neighboring subroutes

`actor_subroute_edit.c` owns backward deletion, current-path orientation storage,
orientation weighting/angle averaging, body positioning, lifetime-cache decisions
and initialization-event propagation from previous/later subroutes. Native
methods translate values and services. C calls the existing route/event editor,
pose evaluator, lifetime evaluator and action editor directly. A temporary seek
adapter runs the already C-owned seek evaluator and retains partial state and
owner-side flags on failure.

Backward deletion retains enabled BUG1870/BUG1866 behavior, current rotation
replacement, action-cel updates, merge/cursor ordering, offstage node removal,
alive publication before reset, and final lifetime/invalidation order. Matching
held-frame locations retain the original second offset subtraction. Failed
lifetime metadata reports diagnostics but does not abort the edit. Recording
returns the cached last frame while leaving lifetime dirty. Failed non-recording
queries retain partial caches/last frame and leave independent output untouched.

Previous propagation scans non-costume types backwards and costumes forwards,
retains its costume-mask early return, and keeps same-frame pruning plus seek/
delete/seek failure order. Later propagation stops on exact route-location
boundaries, positions only after successful transform copies, and finishes by
selecting the default action. Copied tags intentionally gain an owning reference
before merge/application, fixing the original borrowed-copy bug. Event data stays
identical; failure retains the inserted owning row.

Angle weighting retains wraparound compensation and the exclusive upper weight
bound `[6553,65536)`. Unweighted updates write all axes and preserve translation/
padding. Primitive BRender distance math remains external; all weighting and
selection policy is C. Native metadata, body and resource implementations remain
temporary dependencies.

`engine-subroute-edit-test` covers lifetime failure/recording outputs, held-frame
subtraction, alive/reset observation, angle bounds/padding, owning copies through
apply failure, costume-mask early return and both seek-failure boundaries. All 48
standalone C-only Clang checks (including strict C89 syntax), 62 focused native
checks and MSVC AddressSanitizer pass. All 34 snapshots match, including the
282,784-byte trace frozen before conversion across 576 real-actor scenarios.
Additional real-resource checks verify ordinary tagged copies and complete
teardown in both directions, including failed edits. All 51 CLI baselines and
headless scene/timeline checks pass. No Marcus UI tests were added.

Stage entrance/removal/placement, recording, remaining transform/time edits,
undo, queue/runtime integration, body/resource implementations and final C++
facade removal remain unfinished.

### Actor placement

`actor_placement.c` owns the original `_GetNewOrigin` decisions. The background's
authored point is used unless all three coordinates are zero. Only that sentinel
queries the camera through a primitive callback, projects 25 units ahead (or the
clipping midpoint when the far plane is closer), and puts Y on the floor. Text
adds ten scalar units after either path; authored nonzero heights are retained.
Fixed-point projection uses the shared C BRender-compatible MAC and wrapped
addition. No BRender object or C++ type crosses the public interface.

The native adapter supplies the authored point, camera matrix/clipping planes
and template-kind flag. Its camera callback remains lazy. Studio and Marcus
link the same C implementation. Stage entrance/removal orchestration now uses
the shared stage-edit module below.

`engine-placement-test` checks single-coordinate authored points, camera query
counts, floor/text policy, the exact 25-unit boundary, fractional midpoint
rounding, matrix translation and scalar wrapping. The native save/load test
compares 4,096 authored/fallback/text/clipping/matrix combinations against the
original BRender formula. Playback captures check existing movie behavior;
they do not themselves exercise interactive actor placement.

### Actor stage entrance and removal

`actor_stage_edit.c` owns `FAddOnStageCore`, `FRemFromStageCore` and
`_FDeleteEntireSubrte` policy. Entrance reserves event storage before one route
point, then obtains placement, subtracts the cached route origin and assigns
camera yaw. Lifetime caches extend before editing; the final range notification
is deferred until successful completion. Existing C helpers delete obsolete
route tails, insert/relocate points, apply Add, inherit initialization events
from the appropriate neighbor, position the body, set zero step and freeze.
Completed mutations remain on failure, as in the native implementation.

Offstage removal performs no reservation. Removing at the entrance frame deletes
the entire subroute, hides the body, best-effort restores default costume and
rewinds recording/state before seeking the saved frame. Empty tracks retain the
invalid frame/first-frame sentinel. A successful seek can reveal an earlier
onstage subroute, which then receives Remove. Seek failure preserves its partial
state. Whole-subroute deletion does not introduce an extra lifetime invalidation.

Native callbacks provide table capacity, the already C-owned placement result,
cached origin/yaw and diagnostics. Subroute services still bridge primitive
body/resource/scene operations and the existing C seek evaluator. Valid runtime
tables are required; successful point reservation guarantees the next insertion.
An impossible insertion failure retains the native diagnostic/continuation order.

`engine-stage-edit-test` covers reservation short-circuiting, exact capacity
requests, apply/insert/reservation failure boundaries, deferred range invalidation,
both directions of costume ownership, empty-track removal, revealed earlier
subroutes and seek failure. The subroute tests share their existing table/service
fixture through `engine_subroute_edit_fixture.h`. A 129,568-byte trace captured
before conversion covers 256 native entrance/removal scenarios, including text,
recording flags, early/late range caches and metadata failures. The converted
results match exactly after ignoring only the Add payload's two unused padding
bytes. Recording is described in the next section. Remaining transform/time
edits, undo, queue/runtime integration, body/resource implementations and final
facade removal remain unfinished.

### Actor route recording

`actor_record.c` owns recording validity, authored record-step sizing and the
begin/move/end operations. `ThreeMMActorRecording` carries insertion location,
timestamp, mode/rejoin/inserted flags, gap and raw mouse history. Native fields
remain temporary mirrors published before metadata, scene and restoration
operations. The final backend still needs to remove those C++ owner adapters.

Beginning clears insertion status, handles nearby Remove events, splits the
current segment when necessary, refreshes dirty lifetime caches and accumulates
future subroute gaps. It preserves the previous timestamp. Movement reserves
events before a point, smooths with wrapped scalar arithmetic and division toward
zero, changes freeze state, queries the authored/scaled action step and either
waits, records in place or inserts a sampled point. Ground clipping keeps the
original step distance. Timestamp subtraction wraps at 32 bits. Output observers
run at the original assignments before scene seeks/restoration; an assignment
mask preserves callers that alias the two native bool output pointers.

Finishing preserves BUG1961/BUG1973: it stops gathering at Add, chooses the first
closest later node without crossing a terminal point, substitutes epsilon for a
zero join distance, deletes displaced action/tweak/freeze/step/Remove events,
moves/merges other events and applies action fill. Replace/static-tail paths keep
their distinct freeze/step ordering. Failures restore before clearing mode/rejoin
flags. Restoration can replace table handles; C never reads those handles again.

Native services retain primitive BRender distance queries and scene seeks.
`_RestoreFromUndo` now delegates its ownership transaction to `actor_owner.c`,
described below. The underlying scene/body/resource implementations remain
temporary native dependencies. Recording decisions execute in the C-only library.

`engine-record-test` covers thresholds and timestamp wrap, record-step failures,
segment splitting, removed stubs, lifetime failure, reservation short-circuiting,
written/untouched outputs, frame-only waits, sampling, clipping, smoothing overflow,
gap extension, seek rollback and aliased-output assignment masks. It also checks
tag ownership through merging/deletion, Add boundaries, epsilon joins, end failure
cleanup and poisoned table handles after restoration. A 439,824-byte native-before
trace across 640 scenarios matches exactly, including complete recording sequences
and real undo restoration. An additional native aliased-output check passes.
Transform/time edits are described next. Undo, queue/runtime integration,
body/resource implementations and final facade removal remain unfinished.

### Actor transform and time edits

`actor_transform_edit.c` owns local/world rotation, normalization, uniform scale,
stretch, route tweaks, composed movement and sooner/later shifting. Native actor
methods now adapt state and host services. Matrix composition, path-orientation
removal, transform fill, event sequencing, bounds, ground policy, lifetime/range
decisions and partial failures execute in the C-only library.

Local rotations retain pre-X/Y/Z order; world rotations retain swapped X/Z
post-rotation and BRender low-precision normalization. Zero world rotation returns
before reservation or normalization. Reverting still reserves three events even
with no requested flags. Scale/stretch bounds have exclusive upper endpoints;
scale's tweak-removal and position-refresh condition compares the new absolute
scale with the clamped factor, preserving the native comparison rather than
substituting the previous scale. Rotation/revert retain diagnostic-and-continue
ordering after a supposedly reserved event fails; scale/stretch/tweak return
failure with preceding state/table mutations retained.

Movement publishes the requested moved flag before reservation/clipping. Ground
clipping can stop all physical movement while that flag remains true. Orienting
movement removes prior path angles in Z/Y/X order, uses the shared C vector and
weighted entrance policies, applies bounded inverse angular throttling, restores
entrance orientation and updates/inserts current rotation events. Entire-scene
translation takes precedence over entire-subroute translation; otherwise Move
events retain the enabled BUG1870 policy. C then refreshes the cached origin,
marks dirty and positions only when the resulting delta is nonzero.

Sooner/later edits absorb subroute gaps in the native traversal order, publish
first/last caches and the shifted actor frame, invalidate the range and seek the
shifted scene frame. Failed lifetime queries report a diagnostic but editing
continues. Failed seeking retains those edits and skips scrollbar notification.

Native callbacks supply BRender distance/normalization primitives, scene/body
effects, cached-origin writes and host scrollbar notification. BRender remains
the existing external C dependency; those callbacks do not retain edit policy.
Project-owned body/resource/scene implementations and native ownership adapters
still need conversion.

`engine-transform-edit-test` covers exclusive bounds, the scale/tweak comparison,
reservation/failure order, zero world rotation, revert, primitive normalization,
moved publication, clipped movement, translation precedence, text/orient handling,
partial orientation failure, costume ownership, gap shifting, failed lifetime
queries and failed seeks. The native-before trace has 1,032,004 bytes across 1,152
scenarios and matches exactly, including all transformed matrices, event payloads,
origin caches, frame/range values and results. Undo, queue/runtime integration,
body/resource implementations and final facade removal remain unfinished.

### Actor ownership and restoration

`actor_owner.c` owns runtime initialization/disposal, duplication, reset, value
copying, track/sound swaps and undo restoration. `ThreeMMActorRuntime` is a plain
C record; the native actor marshals its current representation and supplies
resource references, C-backed track/sound operations and scene/view primitives.
The native class is still an intermediate representation, not the final backend.

Duplication publishes its output before cloning tables and retains the shared
body/template and owning template tag only after both clones succeed. Failure
releases the partial actor before clearing that output. Reset detaches the scene,
releases the body, then runs the C route reset while retaining rejoin state.
Undo restoration temporarily borrows the live body/scene into the snapshot,
hides the actor, copies/switches runtime tables, restores snapshot bindings and
seeks the original live frame. Failed seeking retains its partial changes and
still invalidates views. The sound tag keeps its existing borrowed semantics.
Disposal closes template/event tags before releasing body and track, then
releases the template and disposes sound storage explicitly in C.

`engine-actor-owner-test` checks allocation/clone failure cleanup, output timing,
resource reference balance, reset, restore swaps and ignored seek failure.
Native traces cover 128 ownership scenarios and 32 real movie/scene undo cases,
including undo creation, undo/redo replacement, name swapping and removal.
They establish ownership integration behavior. The undo controller is described
next; body/resource/scene policy and final facade removal remain unfinished.

### Actor undo controller

`actor_undo.c` owns actor undo creation, snapshot reference lifetime, undo toggle
and redo frame switching. `ThreeMMActorUndoState` holds plain movie/snapshot
handles, identifiers, frames and flags. The native undo object marshals these
fields and supplies primitive movie/scene operations and temporary String scratch
storage. C controls name capture, ignored rename failure and captured-name swap;
the existing String implementation is already C-backed. These native object
representations and movie/scene implementations remain temporary dependencies.

Toggle preserves scene-switch versus seek-failure handling: only the latter
clears undo, then returns without touching an owner that clearing can destroy.
It retains the outgoing actor before removal/replacement, publishes changed IDs
before insertion, retains name/ID mutations on insertion failure, and releases
the incoming snapshot before clearing its pointer. Range invalidation precedes
transferring/resetting the outgoing actor. Sound undo plays the queue; other undo
flushes it. Redo preserves virtual undo dispatch and restores its temporary frame
even on failure, without overwriting snapshot/ID changes made during the toggle.
Creation clears failed registration before release; success releases its local
undo reference before detaching/resetting the snapshot.

`engine-actor-undo-test` checks callback order, references, name capture/rename
failure, changed IDs, view/text selection, absent actors, removal, scene/seek/add
failure, owner destruction on seek failure, redo failure/frame restoration,
set/dispose and allocation/registration failure during creation. Both the
3,240-byte native undo trace (32 cases) and 94,208-byte ownership trace (128 cases)
remain identical. Composite undo is described next. Remaining edit/clipboard
wrappers, movie/scene/body/resource implementations, queue runtime and facade
removal remain work.

### Composite actor undo

`actor_group_undo.c` owns move/rename composite creation, entry disposal, child
references, traversal and name toggling. Move groups retain before append and
release on failed append. Their toggle rereads the count each iteration, skips
NULL rows, publishes movie/scene/frame onto each child immediately before virtual
undo dispatch, and stops at the first failure without reading a group that the
child may have destroyed. Destruction releases child references before entries.

Rename groups capture their count once, read a stored name and actor ID, skip
actors absent from roll-call, stop on failed rename and swap in the captured
current name. Failed storage of that current name is deliberately ignored, as
in the native implementation; preceding name/table changes remain. Duplicate
actor rows retain their original ordered behavior. Both redo adapters delegate
to virtual undo, retaining the same toggling policy.

Native callbacks marshal owner fields, entry records and two String scratch
values, and provide C-backed array/string storage and primitive movie/child calls.
The native objects are still temporary representations. The C test checks
allocation/append failure, pointer/reference timing, NULL rows, child context,
failure that destroys the group, missing actors, partial rename and storage
failure, duplicate IDs and repeated toggles. A 2,100-byte native-before trace
covers 16 child-dispatch scenarios, eight factory attempts and eight real-movie
rename scenarios with three toggles each; it matches the port exactly. Actor
transactions are described next. Copy/paste/clipboard, queue/runtime integration,
scene/body/resource policy and final facade removal remain unfinished.

### Actor edit transactions

`actor_transaction.c` owns public action, stage add/remove, normalization and
costume transaction policy: snapshot duplication, core edit, rollback, undo
registration and local snapshot release/optional transfer. Native callbacks
adapt fixed operation arguments and call the already migrated C core edits.
Action snapshot outputs are assigned only after successful undo registration;
failure leaves them untouched. Failed core/undo registration restores first.

Deletion keeps its special case for a subroute's Add frame. Otherwise it saves
the event cursor, seeks back, deletes forward, publishes alive, reads back the
possibly changed frame and seeks forward. Only that normal path updates scene
sound indication. Lifetime failure diagnoses and continues; failed subroute
deletion or seeking diagnoses/releases without restoring partial state. Failed
undo registration restores state while keeping any prior alive-output write.
Duplication failure alone trashes the optional native debug alive output.

Two pre-existing failure quirks remain explicit: costume ignores duplication
failure and can pass NULL into snapshot operations; removal restores but does
not release its local snapshot when its core edit fails. The C tests preserve
those call/reference sequences using nonallocating mocks. They are not claimed
as corrected allocation/ownership behavior. Native costume tests use valid body
sets and material/custom fixtures, rather than unsupported bodyless inputs.

`engine-actor-transaction-test` covers core/registration failure, output transfer,
rollback/release order, the legacy quirks, Add-frame deletion, partial seeking,
changed-frame readback, optional alive outputs, sound indication and lifetime
diagnostics. The 52,704-byte native-before trace has 112 scenarios (80 action/
stage/normalization/deletion cases and 32 valid costume cases), including real
undo registration, resource balance and native allocation failures. It matches
exactly. Native object/resource/scene representations remain intermediate;
copy/paste/clipboard, movie/scene/body/resource policy, queue runtime and facade
removal still need conversion.

### Actor and route copying

`actor_copy.c` owns full-actor and route-only copy policy. Full copy selects
earlier events from the current subroute, rewrites action cels to the current
cel, resolves/writes source sound tags before deciding whether to retain each
sound, filters Remove and older Tweak/RotateHere events, resets earlier event
locations and inserts/merges them. Future events retain payloads, adjust route
indices/offsets/holds and stop at the next Add for a subroute-only copy once the
destination has events. The active BUG1950 setting is passed explicitly.

Source payloads remain locked across insertion, including the action's stack
replacement, and unlock before merge or failure destruction. Successful source
sound-tag rewrites remain even when a later operation fails. Destination tag
ownership uses the existing C insertion/merge/destruction implementations.
Creation publishes the output; failure releases the partial actor before
clearing it. Source actor state is read-only except those sound payload writes.

Full copy keeps the current node's original coordinates and subtracts the
current offset from its stored distance. Route-only copy interpolates the first
point and recomputes distance to the next node with the existing external C
BRender primitive, retaining epsilon 1 for zero length. Endpoints, subroute
boundaries, bulk copy (including zero rows), reservation and rewind sequencing
remain distinct between the operations.

Native callbacks marshal typed payloads/tables and provide array operations,
unapplied C event insertion/merge and C rewind. Native actor/template creation
and movie sound-tag resolution are still temporary C++ engine dependencies, not
final host interfaces or converted resource policy. Paste/clipboard, scene/body/
resource/runtime implementation and final facade removal remain unfinished.

`engine-actor-copy-test` checks locks, source mutation before filtering, partial
resolution/insert failure, resource cleanup, result publication, optional BUG1950
filtering, future offsets/holds, full versus interpolated points, epsilon and
endpoint paths. The 405,760-byte native-before trace spans 320 synthetic runtime
scenarios in private movies, including both copy scopes, cached templates,
owning material/sound tags, allocation/resolve failures, tables, rewound state
and reference counts. It matches exactly. Fixture sound chunks support tag
mapping without decoding audio; the playback corpus supplies actual audio checks.

### Actor route paste

`actor_route_paste.c` owns route paste and calls the existing C route splitter,
event removal/insertion, floating-step and pose implementations directly. It
reserves before mutation, publishes the split cursor before insertion, deletes
from the current node using the native count, removes current-subroute events
without cursor updates and shifts later-subroute event indices. Clipboard points
are translated relative to the current point and remain unchanged. First-segment
length retains epsilon 1; end Step/Freeze ordering and delayed dirty/range/body
notification are preserved.

Failures keep preceding edits for the caller's undo snapshot to unwind. Reserved
point insertion failure diagnoses and continues, matching native AssertDo; other
insertion, metadata and floating-step failures return immediately. The optional
STATIC policy is explicit: it queries action flags and floats only static actions,
while the normal build always floats. The split temporary's previously
uninitialized distance is initialized to a nonzero epsilon before the existing
splitter; the temporary node is removed/overwritten before the resulting route.

The native actor now marshals state and supplies array reservation, external C
BRender distance and diagnostics. Existing adapters still supply native scene/
body/resource representations; they are not final backend boundaries. The
167,168-byte native-before trace covers 256 scenarios, including empty/short
clipboard paths, offsets/holds, later subroutes, terminal nodes, owning material/
sound tags, state/transforms and reference balance, and matches exactly.
`engine-route-paste-test` additionally checks reservation/split/event/apply
failures, partial edits, tag closure, cursor/terminator ordering, optional STATIC
metadata, diagnostic-and-continue insertion failure and unchanged clipboard data.
Full actor paste, clipboard control, movie/scene/body/resource/runtime policy
and final facade removal remain unfinished.

### Source names and directory discovery

`source_names.c` owns counted source names and SID extras in C storage, replacing
TagManager's persistent StringTable_GST object. First SID wins; insertion order
and per-row merged/long/short lookup precedence are preserved. Case equality is
a host locale service. Splitting retains the distinction between counted text
and zero-terminated slash scanning, including embedded zeros and empty halves.
GST merging preserves previous rows on failure and swaps only incoming SID extras
when required. Wire writes preserve native text/SID payloads and swap only headers
and offsets, restoring offsets even after I/O failure. Provider calls use a stack
copy so adding source names during a callback cannot invalidate the query.

Movie saving writes this C table directly; Studio's managed-source enumerator
uses count/SID queries. The borrowed native table getter has been removed.
Read-side GST decoding still uses a temporary native string-table adapter.

`source_discovery.c` owns provider short circuits, local long-before-short search,
media short-before-long search, skipping floppies, and verifying previously opened
media paths without relocating them. Both fallback descents retain the same path
candidate as legacy. Native Filename operations, drive enumeration, media-error
suppression and UI request construction remain host services.

`engine-source-names-test` covers one/two-byte names, duplicate IDs, ambiguous
lookup precedence, embedded zeros, merge failure, swapped IDs, provider mutation,
failed wire writes and directory callback order/output preservation. Thirty C-only
checks and forty-three focused native checks pass, as does MSVC AddressSanitizer.
The 192-byte source-name snapshot and all twelve prior snapshots match; all 51
CLI baselines match. No Marcus UI tests were added.

### Tag ownership, dispatch and owning lists

`tag.c` owns embedded tag open/copy/save/redirect/duplicate/close policy. Copy
failures retain legacy results: failed open clears cache identity; failed save
leaves the tag unchanged. Redirect retains the destination before releasing the
old cache. Optional publication mirrors native tag fields at callback points;
close clears after release. Nonembedded tags ignore cache pointers. Native open
and disabled file-cache adapters avoid reading unopened cache fields.

C dispatches child lookup and resource fetch through embedded caches or source
managers, including local-only versus media-enabled requests and missing-tag
callbacks. Source catalogue, names, lookup, retry and directory-search policy now
use C above; physical directory/drive adapters remain native. C owns cache clearing: resolve
media before file reopen, ignore reopen failure, and zero/restore memory budgets.
File caching remains disabled, matching the previously unconditional return.

`tag_list.c` owns payload/location buffers and replaces TagList's GeneralGroup.
Tags are borrowed without retaining caches. Sort/dedup uses sid/type/number and
ignores cache identity; the first tag wins and child-cache flags only become true.
Children sort by type then slot and retain duplicates. Cache passes stop on the
first failed main cache, child lookup or child cache. They pass child tags through
without an implicit close, matching the existing native reference behavior.
Callbacks cannot mutate/re-enter the list. Runtime TAG and its 16-byte wire form
remain distinct native adapters; the new C tag value carries no C++ types.

`engine-tag-test` checks copy failures, ownership/publication order, redirect,
source/media dispatch, missing hooks, comparison, clear-cache order, allocation
failures, list ordering, duplicates and cache-pass stops. MSVC AddressSanitizer
passes. All 336 frozen native tag-baseline bytes match; previous snapshots and
51 CLI baselines remain exact. Twenty-eight standalone and forty-one focused
native checks pass. Source-name/discovery adapters and engine-object integration
remain the next work; full backend conversion is incomplete.

### Owning chunk tables and compact/expanded records

`chunk_table.c` owns the writable index record and its payload/location buffers.
There is no GeneralGroup object or BASE lifetime for the index. C collection
algorithms implement insertion, deletion, variable-byte edits, moves and growth;
the table publishes count/size metadata and owns allocator cleanup, including
partial creation. Locks pin the payload buffer as in native GG. Failed mutations
preserve logical entries, though capacity and borrowed-pointer addresses may change.

Compact 20-byte and expanded 32-byte records are checked C layouts. C implements
packed sizes/flags, masked flag updates, 16-bit count storage, expanded runtime-ID
preservation, canonical entry/child access and transient-flag clearing. Borrowed
C access views supply the existing search/graph/save/edit policies. Record fields
and child/name bytes retain native order after materialization.

C writes the exact GG header, payload and locations. As with native GG, alternate
byte order converts only the header/location words; fixed/variable payload order
is supplied by the caller. Location words are restored after every write failure,
and active buffers are unlocked. Native `chunk.cpp` retains thin argument/result
adapters, file I/O and legacy text conversion; it no longer allocates/calls a
GeneralGroup or implements packed record methods. Full native container facade
replacement, source-name/discovery adapters and engine objects remain migration work.

`engine-chunk-table-test` retains a frozen native hash over 2,188 bytes covering
both widths, reverse insertion, variable edits, moves, deletion, locking and wire
output. Both debug-trash modes match. It checks relocating allocation, initial/
growth failure, fallback, locked-owner guards, opposite-order writes and failures
at header/payload/location stages. Record checks cover masked flags, size/count
truncation, runtime-ID preservation and child bounds. MSVC AddressSanitizer passes.
Twenty-seven standalone and forty focused native checks pass. All ten binary
snapshots and 51 CLI baselines remain exact. Expanded native index runtime remains
unverified; native statistics/expanded-index variants compile with `/Zs`.

### Persistent container owner state

`chunk_owner.c` owns the persistent container record: main/extra file and index
references, free-space maps, compact runtime cache, flags and deferred-map
positions. `ChunkyFile` retains this C record and typed borrowed getters; its
native storage structs, bitfields and duplicated reopen-state publication are
removed. The owner initializes explicitly, is unique and cannot be copied or
moved. Expanded-index owners omit compact runtime initialization/disposal.

Reopen and disposal operate directly on the live C file/index/map record.
Callbacks see the cleared/new/restored state at the existing publication points.
Failed reopen restores prior owners and runtime cache, while retaining last-read
deferred-map positions as before. Mark/add/read flags survive reopen unchanged.
Save transaction views borrow aliases to owner references, without retaining or
creating independent owners. C publication updates only file/map/end/invalid/add/
unread fields; index, mark/read flags, deferred positions and runtime cache survive.
The existing save policy publishes before releasing replaced references.

C also owns option latching, storage-policy views and file error aggregation /
reset. Errors query main then extra; resetting lowers only levels above the
requested value and retains the original callback order. Native services provide
physical file flags, I/O, index creation and reference-count primitives. The
Index storage and record handling are converted above. Native container/class,
file/name and object adapters remain; this does not complete the engine backend.

`engine-chunk-owner-test` checks successful reopen and read/validation failure
with live state aliases, guard denial, compact-cache restoration, expanded-owner
state, partial-owner cleanup and repeated disposal. It checks option-read
callback visibility, save publication preserving unrelated state, lock guards,
and 1,000 error-level/reset combinations with callback ordering. MSVC
AddressSanitizer passes. All nine native snapshots and 51 CLI baselines remain
exact. Twenty-six standalone and thirty-nine focused native checks pass.
Expanded-index runtime integration remains unverified; native statistics and
expanded-index variants compile with `/Zs`.

### Container registry and forest destination construction

`chunk_context.c` owns container registry links through explicit independent C
contexts and caller-owned link records. No allocation or callbacks enter list
insertion/removal. Links borrow owner tokens, insert newest first and detach
without retaining owners. Attached link/context addresses must remain stable;
detach is idempotent and duplicate attachment is rejected. Hosts remove links
after associated-resource disposal and before freeing owner storage.

`ChunkyFile` now derives directly from BASE and uses C link state, replacing its
BaseLinkedList storage and native registry head. Its temporary native facade
supplies a default process context; the C API supports independent contexts.
`PcflFirst`/`PcflNext` keep their borrowed traversal contract. Existing C marked/
zero-reference cleanup policy traverses those links. Persistent owner state is
converted above. Native class allocation, file/name primitives and other object
adapters remain to be converted.

`three_mm_chunk_construct_forest` owns destination construction. Copied imports
create a temporary file and reserve the 128-byte prefix. Borrowed imports retain
the source file, publish offset+length as the logical end and set invalid-main /
add-to-extra before reading. Every failure releases the partial owner, including
resources left by a failed reader. Native callbacks provide object/index/file
primitives and adapt the existing C forest reader; no parallel construction
policy remains. Invalid signed file extents are rejected before allocation.

`engine-container-test` checks independent registries, head/middle/tail removal,
repeated detach, cross-context transfer, duplicate attachment and corrupt-link
guards. Forest checks cover allocation/index/temp/resize/read failure and borrowed
source references, partial maps/runtime storage and cleanup order. The same checks
pass under MSVC AddressSanitizer. The native registry test preserves all 120 frozen
bytes across insertion, removal, marked zero-reference retention and cleanup.

### Resource caches, object lifetime and manager lookup

`resource_cache.c` owns sorted cache tables, reader-key lookup, preload/fetch,
estimated/actual size checks, release ages, priority/age/size eviction and budget
changes. Its C object record owns resource identity and attachment state. C
policy handles zero-reference retention, explicit detach, toss priority and
objects outliving their cache. Cache disposal clears surviving objects' owner
pointers before releasing the file. Signed-16-bit priority storage is preserved.

`resource_manager.c` owns the cache list and reverse disposal. Searches retain
forward precedence: preload stops on YES or MAYBE, fetch stops on an object or
error, and cached find checks only the first containing file, omitting scope on
the final cache lookup. Query callbacks may append caches; the initial count
bounds each search. Allocation failure releases newly created cache references
and leaves the caller's insertion-position output unchanged.

Both tables use C-owned storage. Entries are copied before callbacks; readers
may load other keys and detach/destruction callbacks may remove other entries.
Insertion searches again after readers, and zero-budget scans restart after
detach. Same-key recursive reads and disposal re-entry are invalid. Failed fetch
insertion still returns a detached object with its owner reference and an error;
failed preload insertion releases the fresh object. Reader success transfers a
fresh reference and a nonnegative charge; negative charges are rejected.

`wide.c` supplies signed two-word arithmetic for resident totals and purge scores,
retaining accounting above 32 bits without `long long` or `stdint.h`. The frozen
Python arbitrary-precision oracle covers 65,536 pairs, with explicit signed
boundaries in the C test. Native and standalone checks hold 24 resources charged
at 192 MiB each without allocating those payloads.

`crf.cpp` remains a native facade for class allocation, virtual/reference
primitives, diagnostics, file blocks and readers. Reader identity is passed as a
function pointer and cast back before host comparison/calling; C never converts
function pointers to data pointers or orders them. GenericHQ and generic-object
wrappers remain native adapters. Native container adapters, source-name/discovery adapters and engine objects remain
migration work.

`engine-resource-cache-test` covers recursive reads, relocating storage,
speculative allocation fallback, initial/growth failure, cleanup/lifetimes,
eviction scoring and manager precedence. MSVC AddressSanitizer passes. The native
`resource-cache-test` retains all 1,624 frozen baseline bytes, including cascading
releases, distinct readers, scoped lookup and error stops. All eight native
snapshots and all 51 CLI baselines remain exact. Twenty-five standalone and
thirty-eight focused native checks pass; no Marcus UI tests were added.

### Container construction, lookup and disposal

`chunk_container.c` owns open/create sequencing over native object/file/index
primitives. Existing opens apply options before adding a reference; failure
retains the existing owner and returns no new reference or open-error diagnostic.
Creation rejects existing owners, forces write-enable, creates index then file,
reserves the 128-byte prefix and publishes the logical end. Every failed new
construction releases its partial owner and attached resources before reporting
the open/create error. Initial option failure on a new owner remains diagnostic
only, matching legacy release behavior. Optional new-owner diagnostics preserve
the original validation points without adding full checks to existing opens.

Registry matching uses borrowed file tokens, skips invalid-main wrappers and
retains the extra-file collision diagnostic. Name resolution and native shell
allocation remain host primitives. Disposal clears/publishes the owner view,
then releases main file/map, extra file/map, index and runtime cache in legacy
order. Repeated disposal of an empty state is safe. Locked map/cache owners are
rejected before mutation. Shared main/extra file tokens consume two references;
free maps remain unique owners.

`engine-container-test` checks allocation/index/file/resize/read/validation failure
stages, including readers that leave partial resources, existing opens and
creation rejection, initial-option diagnostics and callback order. Disposal tests
check shared file references, partial/empty states, repeated cleanup, compact/
expanded-style cache ownership and lock guards. MSVC AddressSanitizer passes.
All 597 native construction snapshot bytes match over ordinary/temp/relative-temp
creation and saves; native checks also cover repeated open, corrupt-file rejection
and temporary deletion. All previous snapshots remain exact. Native statistics
and expanded-index variants compile; expanded-index runtime remains unverified.

Native class allocation and file/name adapters still exist. Registry links,
forest destination construction, persistent owner state and index storage now
use C above; full engine objects and caller integration remain necessary.

### Container options, flags and reopen ownership

`chunk_lifecycle.c` owns container-to-file option mapping and sticky mark/add/read
options. Even a zero option mask maps to deny-write. Physical flag changes are
attempted before a noncritical lazy-map read and before options are latched;
failed flag changes leave options unchanged. Native services retain the physical
file flag implementation. The C registry walker selects zero-reference unmarked
objects for destruction and reads the next token before destroying the current
one. C release policy retains marked zero-reference containers.

The C reopen transaction detaches the current index/storage, explicitly swaps
the compact runtime cache, retains the main file, publishes the detached state
and invokes index loading/validation. Success disposes the former owners; failure
restores them and disposes rejected owners. Publication precedes cleanup callbacks.
Deferred-map offset/size fields outside the transaction retain their last-read
values, matching the legacy rollback limitation. Other container options remain
unchanged. Native callbacks provide index materialization/validation, reference
primitives and facade synchronization. An expanded index supplies no compact cache.

`chunk_edit.c` now also owns packed/forest/loner mutation. Packed/forest changes
invalidate runtime IDs only when their bit changes. Enabling loner preserves the
ID; disabling an existing loner delegates to the shared graph deletion policy.
Native fixed-record storage still preserves native-only fields.

`engine-lifecycle-test` checks 262,144 option/state/mask/file-flag/failure
combinations, registry unlink/next-before-delete behavior, release limits and
reopen success/read failure/validation failure. Allocated free maps and runtime
caches prove restored ownership and complete cleanup with and without a compact
cache. Writable/invalid/locked-owner guards leave owners untouched. The extended
edit check covers no-op/changed flag invalidation and loner deletion. Both pass
MSVC AddressSanitizer. Native integration preserves the 56-byte lifecycle snapshot
and checks copy reuse/invalidation, read-only reopen versus writable denial,
mark retention, repeated open and final unmarked closure. All prior chunk,
routing, loading, string and forest snapshots remain exact. Statistics and
expanded-index variants compile; expanded-index runtime remains unverified.

Construction and associated-resource disposal now use C above. Native class/
native container/object adapters still prevent a standalone C backend.

### Storage selection and read-through caching

`chunk_storage.c` owns main/extra allocation selection, cache copy/publication,
failed-copy extent release and read-through fallback. Nonempty forced/add-to-extra
allocations create extra storage lazily; failed reservations retain a newly
created file. Empty allocations always choose main at zero without reading a
free map or creating extra storage. Nonempty main allocations require the lazy
map read to have been attempted. Native reserve callbacks use the C heap and
publish its logical end; physical files remain host services.

Caching copies the original main payload before publishing offset/on-extra.
Failure frees the new extra extent and retains the old entry; it does not free
the original main payload. Only offset/on-extra change, preserving packed/forest
flags and runtime IDs. Fetch tries caching when read-from-extra is enabled,
falls back to the recorded main/extra location on failure, and can retry later.
Optional callbacks retain cache/fetch trace ordering and native diagnostics.

The legacy empty-cache quirk is preserved: ensure returns the current extra-file
token, possibly NULL, without setting the empty chunk's on-extra bit. A native
fetch can consequently assert if read-from-extra selects an empty chunk before
any extra file exists; this slice does not silently repair that behavior.

`engine-chunk-storage-test` checks routing over add/force/unread/empty combinations,
size guards, creation/reservation/copy failures, extent release, metadata
preservation, repeated caching, fetch fallback/retry and trace ordering. MSVC
AddressSanitizer passes. All 1,740 native routing baseline bytes match across five
read-only/write/read-through/add-to-extra modes and subsequent saves. Native
checks also verify graph-copy reuse across a cache move. The previous chunk,
loading, string and forest snapshots remain exact.

The native statistics and expanded-index variants compile with `/Zs`. The
expanded variant exposed a pre-existing duplicate mask name; its alias now uses
`kbomCrp`, as the compact branch does. This is compile coverage, not expanded-index
runtime validation. Container option/flag/reopen policy now uses C above; native
native container/object adapters still require conversion.

### Index materialization and owning free maps

`chunk_loading.c` owns free-map records and their range storage through an
explicit allocator. `ChunkyFile` no longer owns native DynamicArray free maps;
save/reopen transitions transfer unique C pointers and dispose detached owners.
Maps remain nullable, distinct from an allocated empty map. Lazy loading clears
the unread state before callbacks, including failure, and discards malformed
maps without retrying. Reads preserve raw range values and the existing array
header/width/length policy; they do not add a new overlap validator.

Serialization writes the 12-byte array header followed by the complete body,
omitting an empty body. Native-order output uses the C storage directly;
opposite-order output uses C scratch storage. Failed writes retain partial output
and ownership. The heap's first-fit/coalescing algorithms now operate on these
C map primitives. Native services retain physical I/O and allocation diagnostics.

Index materialization reuses an owning C scratch buffer for native child records
and encoded names. Modern names retain their bytes. Pre-v3 names receive a bounded
terminator and native SZS conversion; names of 256 bytes or more are discarded,
as before. Native callbacks convert text and insert a fixed record plus variable
bytes into the writable C chunk table. No C++ vector or exception path is needed
for row assembly. A failed import leaves partial destination cleanup to its host.

`engine-loading-test` checks both map byte orders, independent exact wire bytes,
empty/null maps, lazy state publication before reads, malformed headers/widths/
lengths, failed reads/writes, allocation failures and SPEED fallback. It checks
compact/expanded index materialization, child packing and pre-v3 name lengths
0/1/255/256/516, insertion/conversion/scratch failures and balanced disposal.
The index fixtures select matching native byte order; existing expanded-index
pre-swap compatibility checks remain unchanged. MSVC AddressSanitizer passes.
Native integration preserves all 2,192 lazy-load snapshot bytes over valid,
swapped, invalid-width/order/truncated and absent maps, including allocation
positions and subsequent saves. Existing chunk, string and forest snapshots
remain exact too.

### Embedded forest export/import

`chunk_forest.c` owns the 24-byte embedded descriptor wire format, preorder
serialization through the C graph walker, size-only traversal, parent-stack
ownership and import/adoption sequencing. Shared descendants serialize once per
tree occurrence, so importing a forest does not reconstruct their original
sharing. Names and original chunk numbers are omitted. Descriptor writes precede
payload transfer and retain partial output on failure. Per-record overflow guards
reject unrepresentable signed file positions/sizes before that record's I/O.

Imports decode either byte order per descriptor. OSK and root child IDs remain
opaque/ignored; negative child counts remain leaves, while incomplete positive
counts fail. Empty forests succeed even at an offset beyond physical EOF,
matching native behavior. Copy imports preserve only PACKED; borrowed-data imports
preserve PACKED|FOREST. The native facades still create/dispose destination
containers/files and provide raw payload transfer and index insertion primitives.
The C import stack replaces a native DynamicArray object; failures release stack
storage and leave partial destination cleanup to its host owner.

`engine-forest-test` freezes the native 118-byte export (FNV32 `2c02e752`) and
checks copy/borrow flag policy, payload offsets, parent/slot reconstruction, both
byte orders, size-only traversal, read/add/adopt/write/copy failures, malformed
descriptors and signed limits. A 200-level tree checks stack growth, relocating
allocations, SPEED fallback and failed-growth cleanup. MSVC AddressSanitizer
passes. Real native memory/file/container integration retains all 1,594 baseline
bytes over valid, swapped, truncated, invalid-length/count/byte-order and empty
forests. Existing chunk and serialized-string snapshots remain exact too.

### Runtime IDs, chunk names, serialized strings and codec registry

`chunk_metadata.c` owns the compact-index sorted runtime-ID cache and chunk-name
resize/copy/decode policy. Zero runtime IDs remove entries without allocating;
failed insertions preserve logical entries, and empty allocated capacity is
retained. `FReopen` uses an explicit owning-cache swap to preserve rollback
ownership. Expanded-index runtime IDs remain fixed-record host primitives.
Names follow the 12-byte child records; shrinking precedes overwrite, failed
growth preserves the old name and decode holds a balanced pin. Native group
primitives already use the shared C collection algorithms.

`string_wire.c` supplies `String::CbData`, `GetData`, `FWrite`, `FSetData` and
`FRead` policy: OSK/count/terminator encoding, bounded conversion, malformed data
rejection and absolute ending offsets. Strict reads reject trailing bytes;
failed reads clear the string without publishing an ending offset. Writes keep
the two header/body operations and their partial-write behavior. Code-page
conversion and DataBlock I/O are host callbacks; early unpack failure retains
the native unchanged-string behavior.

`codec_registry.c` owns codec token storage and references. Lookup tries the
default codec first, then registrations newest first. Duplicate registrations
own separate references; failed registration retains none. Disposal releases
the default first, then registrations in reverse order. Native codec dispatch,
reference primitives and allocation diagnostics remain callbacks. Neither the
registry nor compact runtime cache requires a native DynamicArray object.

`engine-metadata-test` checks sorted cache updates/deletes, owner swaps, allocation
failures/SPEED fallback, pinned name storage and failed growth, codec ordering and
reference balance. Its frozen 480-case string snapshot is 29,532 bytes (FNV32
`54d0f109`), matching the real native memory and file-backed paths over four
encodings, maximum lengths, malformed/truncated data and nonzero offsets.
Additional cases cover read/write failure stages and UTF-16/big-endian output.
The test passes MSVC AddressSanitizer. Native checks retain all 572,367 chunk
snapshot bytes and all serialized-string snapshot bytes. Reopen cache ownership
is checked by the lifecycle transaction above, including forced C read/validation
failures. Forced failure in the real native file/index services is not covered.

### Chunk swaps, moves and graph copy/clone

`chunk_ops.c` owns payload swaps (offset/size/on-extra/packed/forest only), child
prefix swaps, identity moves with sorted parent-link repair, graph copy/clone,
runtime-based matching and a process-global runtime-ID counter. Moves retain a
runtime ID only within the same type; data swaps invalidate both IDs, while
child swaps preserve the legacy cache behavior. Native callbacks provide aligned
group-byte/record movement and payload transfer. Runtime-table/name policy now
uses the C modules above; those record moves already use C89 group algorithms.

Copy owns a sorted C source-to-destination number map and uses the C graph walker.
It preserves source numbers when available, copies packed payloads and forest
flags, converts names through existing host text services, retains sharing within
the copied subgraph and suppresses pre-existing reuse for clones. Matching scans
candidate numbers in order and preserves traversal/RTI/child-count/slot rules,
including the early no-match result when a source node has no runtime ID. It
does not add a stronger graph-isomorphism check.

The map's native DynamicArray object allocation is removed. Failure preserves
the original partial cleanup: a first map-insert failure can leave a new loner;
a later growth failure can leave the not-yet-linked node after root cleanup.
Name/adoption failures delete the appropriate newly copied node before root
cleanup, and reused nodes are retained. This is not an atomic copy transaction.
Failed operations do not publish a C output number; the native facade retains
its failed-output poisoning. Runtime-ID exhaustion no longer performs signed
overflow; payload copying can continue without assigning another cache ID.

All 572,367 native baseline bytes match, including repeated sharing, clones,
same-file operations, changed descendants, payload/child swaps and referenced
identity moves in both directions. Standalone C checks verify sharing topology,
flags, metadata, debug/release reference scans, runtime preservation/invalidation,
maximum candidate IDs, best-effort runtime assignment and payload/name/link/
traversal/map failures, including the retained partial-cleanup cases. They pass
MSVC AddressSanitizer. Remaining container primitives still need migration.

### Graph traversal and child-link editing

`chunk_graph.c` owns the live graph walker, pre/post/root/error events, skip
behavior and lazy stack storage. The native `ChunkGraphEnumerator` now embeds
this C owner and only translates records, invalid output diagnostics and debug
memory marking. Stack frames contain identities/cursors rather than index
positions, supporting mutation during deletion. Traversal still visits shared
nodes separately for each link; it adds no global visited-node normalization.

The C stack reserves ten initial frames through the existing collection growth
policy and releases storage on completion, root skip, reset and disposal. It
removes the native DynamicArray object allocation. A failed reservation/growth
still yields PRE|POST|ERROR for that child and continues with siblings. Descendant
search preserves the zero-target-reference shortcut and match-before-error
ordering. Graph/context lifetime remains borrowed; the walker takes no file
references and cannot be copied.

`chunk_edit.c` also owns child adoption, duplicate-link handling, insertion before
count-limit checks, rollback on parent/child reference overflow, loner updates,
unlink plus final-reference deletion, and child-ID reordering. Native callbacks
transfer child bytes through existing C89 group storage. Duplicate adoption is
an unchanged success even when clear-loner is requested; changing a child ID
preserves ordering by (slot,type,number). Cycle-risk rejection retains the
existing descendant policy, including unknown results after allocation failure.

All 568,649 native baseline bytes match. Added cases record traversal order,
flags and parent identities across skip modes, active reset, shared subgraphs,
child-ID moves and a 48-node chain, alongside previous saved-file/heap/deletion
outputs. Invalid root parent/slot fields are normalized in the event snapshot. Standalone C tests compare traversal with an independent recursive
reference, force first-allocation and growth failures, relocate allocations,
check SPEED-to-NORMAL fallback, reset/early completion and cleanup. Child tests
cover idempotence, insertion failure, both count limits and rollback, cycle-risk
and unknown results, ordering conflicts and cascading unlink deletion. Both pass
MSVC AddressSanitizer. The graph operations above now share swap/move/copy/clone
policy too.

### Chunk insertion, replacement and deletion policy

`chunk_edit.c` owns first-unused chunk-number selection, blank-index insertion
before storage allocation, failed-add rollback, create-or-replace selection,
payload commit, runtime-ID invalidation selection, reference decrement and the
deletion visitor's skip/descent/removal decisions. It preserves names, children,
parent counts, loner/packed/forest flags and native-only runtime fields during
replacement; new fixed records are zero-initialized. Primitive services transfer
decoded values rather than retaining pointers into movable index storage.

An existing allocation failure leaves the caller's output block intact. Failed
writes free the newly reserved storage and clear the output; failed new writes
also remove the new record. Successful replacement frees the old extent before
publishing the new location, then resets its runtime ID. Storage/map operations
use the preceding C89 heap policy; native callbacks still implement block/file
I/O and record transfer over the C89 collection storage.

Deletion clears the root's loner flag, retains referenced roots, decrements
non-root references and removes unreferenced non-loners on post events. Shared
descendants and loners stop descent. Traversal errors retain the legacy fallback:
decrement children in reverse order but do not recursively delete those newly
unreferenced children. This intentionally does not introduce orphan repair.
The C89 graph walker above now supplies traversal events and owns its stack;
the graph operations above also share swap/move/copy/clone policy.

The native 550,737-byte baseline matches after conversion. It extends the heap
snapshot with named diamond/duplicate-edge graphs, replacement preserving child
lists, retained loner subtrees and cascading child deletion. Standalone C checks
inject insertion/allocation/write failures across new/existing and empty/nonempty
chunks, check exact callback/output/reset order, preserve source metadata and
verify reference/deletion/error-fallback decisions. These also pass MSVC
AddressSanitizer.

### Save-a-copy and chunk heap policy

`three_mm_chunk_save_copy` now owns the save-a-copy traversal and commit order.
It pins the source index, checks source/compacted destination positions, copies
each payload, inserts the corresponding record, unpins on every exit and only
then commits the destination. Unlike whole-file relocation, even empty chunks
invoke copy and retain the running destination offset. The native transfer
primitive preserves the original fixed record (including expanded runtime IDs)
and variable children/name bytes, replacing only the C-supplied offset/size/flags.
Destination creation, failure disposal and physical I/O remain host services.

`three_mm_chunk_heap_allocate` chooses the first fitting free range and consumes
its beginning, deleting exact fits. Append reuses physical trailing bytes before
extending the file. `three_mm_chunk_heap_free` merges either/both adjacent holes
or trims the logical tail and its adjacent last free range. It never truncates
physical storage. Interior map-creation/insertion failure drops the free space,
retaining the legacy nonfatal policy; invalid/overlapping ranges and signed
position overflow are rejected. Native callbacks provide trusted map primitives
over the existing C89 array storage. File selection, lazy map reading and actual
file growth remain native; the algorithms retain no movable-storage pointers.

The native pre/post snapshot matches all 548,265 bytes, covering saved outputs,
children/names and every live chunk's location across 1,200 deterministic
add/replace/delete operations on main and extra storage. Standalone C heap checks
compare 12,000 operations against an independent occupied-byte model, verify the
complete free map after each edit, and inject growth/map allocation failures.
Save-a-copy checks inject every copy/insertion/commit failure, require source
pinning and balanced cleanup, preserve source records, and exercise empty files,
empty offsets, flags and position limits. Both pass MSVC AddressSanitizer.

### Whole-file save relocation and recovery

`three_mm_chunk_save` owns the save sequence shared by Studio and Marcus. It
chooses a direct index commit or a temporary output, checks the complete compact
layout, copies nonempty chunks from main/extra storage, then updates offsets and
clears on-extra flags. Empty chunks become offset zero. Packed bytes and other
runtime flags are preserved. The operation adds no allocation or second index;
trusted live-index primitives fill complete records and cannot fail or re-enter.
It retains no pointers into movable index storage across callbacks.

C also selects reference releases, temporary status, index commit and rename or
name-swap completion. Early file failures discard the output and leave state and
index intact. After a later index/naming failure, a valid old main is restored
with logical end 128, and the copied data becomes temporary extra storage with
every nonempty chunk marked on-extra. Without a valid old CHN2 main, the new
temporary remains attached. Both paths support a later save retry.

The native facade resolves target filenames, supplies file/map primitives and
serializes the index through the preceding C89 commit policy. An optional state
publication callback mirrors C ownership changes before detached references are
released or services run, keeping native debug/lifetime tracking valid. This
mirror does not acquire another owning reference. Existing target removal and
filesystem rename/swap behavior remain host services; no atomic-save guarantee
or new replacement protocol is implied.

Thirty native saved-file outputs match all 13,190 pre-conversion bytes, including
interior free maps, shortened indexes, save-a-copy, read-only save-as, modified
extra data and explicit-current-name saves. Standalone C checks inject temporary
creation/resize, each payload copy, index, rename and swap failures, verify
reference/map cleanup and untouched early-failure state, and retry after late
failures. They cover valid/invalid old mains, temporary status, empty files,
empty chunk offsets, retained flags and signed file-position overflow, including
under MSVC AddressSanitizer. Save-a-copy and heap decisions now use the separate
C89 algorithms above. The mutation policy above shares insertion/replacement/
deletion decisions too; remaining graph operations and host services still need
migration.

### Chunk index persistence

`three_mm_chunk_write_index` now owns the CHN2 index-commit policy shared by
Studio and Marcus: checked index/free-map positions, the exact 128-byte v5/
back-v4 prefix, byte order and zeroed reserved fields. It writes the index,
optional free map, then the prefix. The native `_FWriteIndex` supplies collection
serialization callbacks, which already use C89 collection headers/storage, and
physical file writes. This removes native struct layout from prefix writing.

The operation retains physical trailing bytes and does not flush, truncate or
promise an atomic write. Failed index/map callbacks suppress prefix publication;
a failed prefix callback can already have changed bytes. Optional output metadata
is published only on success. Invalid extents, missing required callbacks and
signed-32-bit file-position overflow are rejected before any callback.

Native regression snapshots freeze twenty complete outputs across five payload
sizes: an interior free map, shortened index, compacted save-a-copy and read-only
save-as. All 10,570 pre/post bytes match. Standalone C checks independently
reproduce the twenty native prefixes (FNV32 `0x012f9b07`), round-trip both byte
orders and map-presence cases, inject every commit-stage failure and check size
limits, untouched guards and the maximum signed file position. They also pass
under MSVC AddressSanitizer.

The whole-file save sequence above now shares relocation/recovery policy too.
Runtime-table/name storage and free-map loading now use the C modules above.
These slices do not complete the container editor or whole-engine conversion.

### Piece write windows and file-text conversion

`three_mm_piece_table_write` in `piece_table.c` owns piece selection, both storage
locks, per-piece DataBlock destination windows, memory/file copy dispatch and
restoration after success or I/O failure. The native stream adapter delegates
to it over the existing C-owned DataBlock record. Source file references stay
owned by the pinned table; positional copy callbacks retain the existing SPEED
buffer/fallback policy.

The legacy writer selected each chunk using total length instead of remaining
length. Some partial final-piece windows could therefore grow past the caller's
destination. C preflight now rejects that unsupported window before locking or
writing. Supported windows retain the original byte results and their final
destination view. This is a safety guard, not a new general partial-write or
overlapping-self-copy guarantee.

`three_mm/file_text.h` and `file_text.c` own FileLocation text conversion policy:
the even-length BOM probe, host-ordered signature interpretation, requested/
inferred encoding precedence, same-encoding no-op, temporary-file redirection,
count queries, output growth and reference cleanup. Character conversion is a
callback over the existing shared text/native code-page services. BOM range
stripping remains visible if later work fails. Nonpositive count queries still
skip their chunk. No UTF-8 detection, newline normalization or malformed-text
repair is added.

Input uses one full-size SPEED allocation above 512 bytes, with a 512-byte
stack fallback. The old fallback used a 1024-byte read limit for that 512-byte
buffer; the C fallback is bounded to the actual buffer. Output keeps its
1024-byte stack area and release-before-exact-NORMAL-allocation growth.
Temporary files and scratch buffers are released on failure; the old file
reference remains until all writes succeed.

`engine-file-policy-test` checks the frozen native snapshot hash for 240 text
cases and 16 supported memory/file write windows (117,346 bytes), plus pinned
locks, restoration, partial-I/O errors, overshoot rejection, temporary/read/write/
conversion/allocation failures, bounded fallback reads, UTF-16 output growth
and explicit signature/no-op behavior. It also passes under MSVC AddressSanitizer.
Native core tests compare the complete pre-conversion snapshot using the real
code-page and file services.

Primitive memory/file services, native container/object adapters and
engine objects still prevent a complete C-only backend.

## Why the split exists

The shared [public archive API](../../libraries/archive/README.md) now insulates
the CLI and inspector from the legacy headers, macros, reference counting and
diagnostic callbacks. `ThreeMM::Archive` keeps `engine-core` private and exposes
typed records, owned page results and structured errors. The original core
entry points remain available to existing runtime code. This first public
boundary covers read-only archive access; movie playback/editing APIs and
independent concurrent engine instances remain separate work.

`ThreeMM::Preview` now supplies a second public boundary for read-only 3D asset
views. Its standard C++ header exposes resource source IDs, action/cel and frame
selection, camera controls and owned RGBA images. BRender actors, engine bodies,
costumes, templates, fonts and background depth stay private. Scene previews
drive the extracted frame/seek/pose evaluators rather than linking Studio's
playback runtime. 3D text accepts an injected font resolver; its original Studio
TagManager fallback remains available. Memory-buffer bitmap drawing and its
region clipping now belong to `kauai-core`, while native graphics remain in the
GUI adapter. See the [preview contracts](../../libraries/archive/README.md).

CLI tools (`extract-bmdl`, `inspect-chunks`, `geometry-test`,
`codec-test`, `actor-render-test`) want to call the real loaders without
dragging in the entire UI runtime. The `*-core` libs let them link a
focused subset:

- Core targets link without any GUI/runtime libraries or replacement GUI symbols.
- `kauai_core.h`, `bren_core.h`, and `soc_core.h` are public core entry points.
  They do not import `frame.h`, `kidframe.h`, or the World definition.
- Public core entry points do not import Windows SDK headers. GDI implementations
  live in the GUI adapter. Core libraries and archive tools build independently
  on Windows, Linux and macOS. See [text/type boundaries](platform-text.md).

The gui (`*`) lib re-exports its `*-core` counterpart `PUBLIC`-ly, so
existing consumers (`studio`, `ched`, `ft`, `ut`, …) link against
`kauai` / `engine` / `brender` exactly as before and see every symbol
they used to.

## kauai-core

Files in `kauai-core` (see `CMakeLists.txt`):

`base.cpp`, `chse.cpp`, `chunk.cpp`, `codec.cpp`, `codkauai.cpp`,
`core_platform.cpp`, `platform_text.cpp`, `cmd_core.cpp`, `clok.cpp`, `crf.cpp`, `file.cpp`,
`ellipse_geometry.cpp`, `pixel_canvas.cpp`, `pixel_stretch.cpp`, `dialog_resource.cpp`,
`file_native.cpp`, `filename.cpp`,
`groups.cpp`, `groups2.cpp`, `lex.cpp`, `mssio.cpp`, `mbmp.cpp`,
`midi.cpp`, `mididev2.cpp`, `midi_worker.cpp`, `picture_data.cpp`, `rich_text_data.cpp`, `region.cpp`, `scrcom.cpp`, `screxe.cpp`,
`sndm.cpp`, `stream.cpp`, `util.cpp`, `utilcopy.cpp`,
`utilerro.cpp`, `utilglob.cpp`, `utilint.cpp`, `utilmem.cpp`,
`utilrnd.cpp`, `utilstr.cpp`, `memwin.cpp` (portable HQ storage despite its name).

The pixel canvas now copies packed 1/4/8-bit indexed, RGB555, BGR24 and BGRX32
surfaces without SDK types or native objects, with palette conversion, clipping
and overlap snapshots. Studio uses it beneath the original `GraphicsPort` API.
Near-size true-color-to-8-bit copies retain the native palette conversion;
the portable API rejects that policy transactionally rather than guessing its
approximate lookup. RGB555/BGR24 rectangles, ovals, polygons, thick lines and
highlights also use the portable span geometry, with native byte comparisons
covering clipping, patterns and distinct brush/highlight inversion semantics.
Scrolling now covers all six packed formats without a palette. Studio selects
the original monochrome clip-band/byte-edge ordering explicitly; the general
API retains snapshot semantics. Frozen native fixtures also run on POSIX.
Four-bit rectangles, ovals, polygons and thick lines now use the same span
geometry and shared palette-aware brush resolver. Raw monochrome highlights
also remain native-free. Solid monochrome brushes now use explicit SDK-free background/reference state;
mixed two-color patterns retain native realization. Four-bit text now uses
portable packed coverage, with palette translation and the existing font host.
The FreeType path requires no native context or framebuffer mirror. BGR24 text uses a lossless RGB
snapshot, the font color renderer and portable packed copies. The SDK-free
`Face::DrawPacked` implementation in `font_canvas.cpp` now owns RGB555/BGR24/BGRX32
conversion, clipping and transactional output; the Windows adapter only supplies
font selection, placement, colors and destination storage. Monochrome
text uses portable coverage with explicit background/reference state; its
patterned box border and legacy picture playback still have native paths.
Solid monochrome brush realization now retains its original background state
across text background changes, until brush selection replaces or invalidates it. RGB555 text is
portable with FreeType, preserving its unused storage bit; the GDI provider
retains native RGB555 smoothing for compatibility; malformed reversed scroll bounds retain their native fallback. See [Studio's SDL bridge](../tools/studio-sdl.md) for validation
coverage and the remaining work.

Monochrome, four-bit indexed and RGB555/BGR24 masked artwork now draw through
the core RLE transfer. Monochrome mask bits remain raw, while indexed artwork
colors use the two-color destination palette; the host retains the black
background state established by the original two-pass blit.
It retains the original mask-OR/color-AND operation and BLACKONWHITE reduction
of source indices before palette translation. The host adapter passes the
bitmap, palette, clip region and persistent stretch mode; no native drawing
context or temporary bitmap mirror is required for these destinations.

Studio startup option parsing is now an SDK-free component in
`inc/studio_startup.h`, shared by native/SDL adapters and independently tested
on Linux. Current-directory and executable-path discovery now use the `KauaiFiles`
backend, implemented for Windows, Linux and macOS. Movie filename lookup now
uses that boundary too: Windows retains native short-name expansion and matching;
POSIX checks literal paths. The SDK-free `launch_context.h` owns raw argument
text or copied argument vectors independently of window globals.
`app_entry_win.cpp` now contains Windows command-line acquisition and bootstrap,
separate from `ApplicationBase`; `app_entry_posix.cpp` supplies an SDK-free
`main` that passes copied argv to `FrameMain`. Studio selects the raw or argv
parser, preserving literal POSIX paths and supporting `--`. A headless entry
test runs the real main adapter with a stub application on Windows/Linux and
cross-compiles for macOS. Full Studio initialization/resource loading still
needs platform adapter work; this is not evidence of a full POSIX Studio launch.
SDL startup product names now come from SDK-free shared constants in
`inc/studio_product_names.h`; native resource tables use those same definitions.

Studio preference policy now lives in the SDK-free `settings_store.h/.cpp`:
typed byte payloads, get/set/default behavior, missing/error reporting and handle
cleanup accept a backend table. `settings_store_win.cpp` retains the original
registry keys and 32-bit view. `settings_store_posix.cpp` now supplies locked,
atomic file replacement under XDG/Application Support configuration roots, with
typed opaque payloads and corruption checks. Studio selects the matching backend.
Native tests use an isolated disposable registry key; Linux runs real filesystem
tests including failed writes and concurrent defaults. The shared policy
and POSIX implementation cross-compile for macOS, whose runtime remains
unverified. This resolves preference persistence, not the remaining Studio
startup/resource dependencies. See the [current handoff](../tools/studio-sdl.md)
for storage details and compatibility evidence.

`platform_users.h` now separates native account lookup and writable profile-root
selection from Studio. Windows preserves network-provider username behavior and
the LocalAppData/Microsoft Kids/Users fallback; POSIX queries the effective uid
and uses XDG data/Application Support roots. Saved HomeDirectory and installed
profiles retain their priority. Recursive parent creation and cwd changes use
`KauaiFiles`; the POSIX preference backend shares parent creation. Headless
Windows/Linux checks cover these services, with macOS cross-compilation only.

`studio-content` owns installed-product enumeration and the optional SDL content
root. It depends on no GUI or engine library. Its SDK-free `studio_content.h`
returns native title/source-ID text; Studio retains SID conversion and TagManager
policy. Windows uses the existing 32-bit HKLM Products key; POSIX reads a tab-separated
manifest in the selected content root. Registration IDs must be preserved when
copying an installation. Windows registry fixtures, UTF-16 checks and real Linux
manifest tests pass; macOS is cross-compiled only. See the
[content-discovery handoff](../tools/studio-sdl.md#installed-product-discovery-update-for-3-november-2026).

The shared SDL application runtime now has an independent object-compilation
gate (`sdl-runtime-compile-check`) covering the real application base, SDL event
and paint paths, picture wrapper and cursor loader on Windows/Linux/macOS.
`frame.h` keeps optional Windows-only device/spelling declarations out of POSIX
builds. Resource callback byte counts match the core's 32-bit words. SDL painting
uses an opaque host window identity rather than a Windows global. This gate does
not link the complete GUI runtime; graphics-object and graphics-port adaptation
remain. See the [runtime handoff](../tools/studio-sdl.md#shared-sdl-application-runtime-update-for-3-november-2026).

Shared graphics-object/environment implementations now join the portable runtime
compile gate. `gfx_pixels.cpp` contains the existing CPU shape, brush and video
paths formerly embedded in the Windows graphics adapter. It shares palette/pixel
state across platforms and leaves synchronization/palette activation and native
color references to backend hooks. Windows offscreen GDI comparisons pass;
Linux/macOS compile this layer, but a complete POSIX graphics-port backend is
still required. See the [graphics runtime handoff](../tools/studio-sdl.md#shared-graphics-runtime-update-for-3-november-2026).

`gfx_port.cpp` now holds window-independent port geometry/region ownership and
the shared line footprint. `gfx_posix.cpp` provides the actual CPU port allocation,
palette ownership, locking and shape dispatch; `pixel-port-test` links this code
without the Studio event loop and runs on headless Linux. Windows also runs that
backend as a headless test, while native Studio retains its GDI adapter. Remaining
native-picture operations and root-window attachment are unfinished. See the
[POSIX pixel-port checkpoint](../tools/studio-sdl.md#posix-pixel-port-runtime-update-for-3-november-2026).

Bitmap copies, scrolling and masked artwork now share their CPU dispatch between
Windows SDL and POSIX. The original indexed double-stretch routines moved into
`kauai-core` (`pixel_stretch.cpp`); a C fallback bug in vertical stretching was
fixed to match the x86 assembly. Exact-byte portable-port tests run on Windows
and headless Linux alongside the existing offscreen Windows GDI comparisons.
Near-size true-color-to-8-bit conversion remains an explicit unsupported case
on POSIX. See the [bitmap transfer checkpoint](../tools/studio-sdl.md#portable-bitmap-transfers-update-for-3-november-2026).

`gfx_text.cpp` now shares counted text measurement, cached portable font selection
and drawing between Windows SDL and POSIX. Native selection remains in the Windows
adapter. `fontlist.cpp` holds common font-list/name operations, while
`gfx_values.cpp` owns the color/pattern values needed by boxed text without linking
the application. The portable port test now covers text in all six pixel formats
and checks that no native font work occurs. See the
[text runtime checkpoint](../tools/studio-sdl.md#portable-text-runtime-update-for-3-november-2026).

The shared `PgptNewSDLWindow` factory now binds the real CPU port to SDL's logical
canvas. `gob_window.cpp` shares root creation, attachment and lookup, while
`gob_sdl.cpp` supplies POSIX window services. Common SDL startup/closure lives in
`appbsdl.cpp`; Windows keeps only its optional native context and timing hooks.
An opt-in dummy-driver test executes port binding, integer presentation and
replacement/shutdown ownership on Linux. Full Studio startup/linking remains
unfinished. See the [root-window checkpoint](../tools/studio-sdl.md#sdl-root-window-attachment-update-for-3-november-2026).

Studio's actual startup and graphics script compiler now compile for Linux and
macOS through `studio-runtime-compile-check`. SDL creation is shared; native
Windows prerequisites stay in the Windows adapter paths. Fixed-width thumbnail
records and `TAG` aliases are covered by `studio-layout-test`, which passed on
Windows and Linux and cross-compiled for macOS. The complete non-Windows Studio
link and startup remain unfinished. See the
[Studio startup checkpoint](../tools/studio-sdl.md#studio-startup-compilation-update-for-3-november-2026).

The Studio compilation gate now also covers the editor, browsers, easels,
portfolio, scene sorter and text/scrollbar controls. Fixed-width selection and
resource-table values no longer rely on LP64 `long` layout. Studio's resource
string-table reader lives in `resource_tables.cpp` and uses only `kauai-core`;
its byte-order/schema regression test runs without the GUI on Windows and
Linux. The Debug MCP server is now included in the compilation gate; its stdio,
PNG, SDL input/state and dialog paths cross-compile for Linux/macOS. Native
Windows diagnostic tools remain conditional, and codec installation remains
outside the gate. See the [MCP checkpoint](../tools/studio-sdl.md#portable-mcp-checkpoint-4-november-2026)
and [editor checkpoint](../tools/studio-sdl.md#studio-editor-compilation-update-for-3-november-2026).

The full SDL Studio build and Clang/GCC resource pipeline are now enabled on
Mac/Linux. Linux has linked and run the original editor, selected a scene,
saved a movie and shut down cleanly. Native Linux `chomp` produces all eight
archives byte-for-byte identically to the Windows host tool. Resource lookup
can explicitly resolve unambiguous legacy case differences without changing
ordinary file semantics. Packed FILL fields, script numeric/name packing and
BRender list unlinking now use layouts/operations valid on 64-bit POSIX hosts.
See the [application checkpoint](../tools/studio-sdl.md#maclinux-studio-build-4-november-2026)
for the native Mac validation limit.

`engine-runtime-compile-check` now compiles the actual playback sources, SDL
recorder and BRender World adapter for Linux/macOS. Actor/core callbacks and
fixed-width scene pause/camera reads are aligned; the movie format is unchanged.
The full Windows native/SDL movie save/reopen regression passes, including
pause replay and actor/sound/playback adapters. POSIX runtime execution still
depends on completing the shared GUI link. See the
[playback checkpoint](../tools/studio-sdl.md#playback-runtime-compilation-update-for-3-november-2026).

The shared UI compilation gate also covers graphics scripts, Kidspace/help,
rich text, documents and controls on Linux/macOS. Stored UI records retain
their original 32-bit layout. Kidspace descriptor parsing now lives in
`kauai-core` (`kidspace_descriptor.h/.cpp`) and is tested with original-layout
bytes in both byte orders on Windows and headless Linux. Full POSIX application
link/startup remains outstanding; see the
[shared UI checkpoint](../tools/studio-sdl.md#shared-ui-runtime-compilation-update-for-3-november-2026).

High-level `GraphicsEnvironment`, font/color/pattern values and drawing data now
live behind an SDK-free `gfx.h`. The native `GraphicsPort` definition, OLY layout
and region helpers are opt-in through `gfx_port.h`. Drawing clips use core `RC`
geometry; Windows interop tokens retain their checked ABI. Value fields use
explicit 32-bit words, and pattern operations use all eight bytes directly rather
than assuming a four-byte host `long`. `graphics-api-test` runs those value checks
without the GUI runtime, including under Linux, and cross-builds on macOS. The
native/SDL graphics comparison tests continue to verify the actual rasterizer.
The port implementation still needs further isolation and a native-free backend.

`gob.h` also exposes graphics-object/tree declarations without a window SDK.
Its window tokens and invalidation rectangle pointers are opaque `CoreNative`
interop types; invalidation bodies and SDL bridge includes belong to backend
implementations. `gob-api-check` compiles the public header first on all three
targets, and `graphics-api-test` verifies the opposite include order.
SDL root layout/title/activation and canvas-to-desktop mapping use the host's
window services. Maximize/fullscreen properties query SDL's actual window state;
Studio's pre-dialog activation restores minimized windows through SDL and keeps
maximized windows maximized. Nested coordinate transforms preserve integer presentation and
odd letterbox offsets; the real-driver test checks global hits and clipped
borders. The host and its services also build independently of Studio with
`BUILD_SDL_HOST=ON`. Native window lookup, MDI and drawing remain GUI backend work.

`appb.h` also compiles without a window SDK. Callback parameter aliases preserve
the native Windows ABI, including x86 calling conventions; native runtime globals
now live in private `appb_native_win.h`. `app-api-check`, `app-sdl-api-check` and
the native interop check cover both include orders and callback signatures.

`dib_frame.cpp` adds bounded SDK-free decoding of standard uncompressed 24/32-bit
DIB video frames. `DrawDibFrame` renders them through portable pixels using the
verified format-specific sampling rules. The SDL graphics adapter uses this for
bottom-up true-color frames on 32-bit ports; top-down native quirks, indexed
targets and unsupported formats retain their prior behavior in that legacy
decoder adapter. SDL's own playback uses the portable decoder below.
Core pixel tests run on Linux and cross-build for macOS;
Windows tests compare both supported drawing and retained fallbacks against
the actual `DrawDibDraw` implementation.

The GUI video API is also SDK-free: `video_stream_host.h` describes relative
frame timing, 32-bit metadata and presentation without exposing VfW/MCI handles.
`video.cpp` retains the original command scheduling; `video_stream_win.cpp`
owns the Windows stream decoder and drawing lifetime. Native window-based
playback lives separately in `video_window.cpp`, with its state hidden from
`video.h`. `video-api-check` compiles the public header on all three targets;
the Windows runtime test uses a real AVI fixture to check nonzero frame offsets,
timing, decoded pixels and cleanup. Native Studio retains these Windows hosts;
SDL Studio selects `video_stream_sdl.cpp` for both representation modes.

`avi_movie.h/.cpp` now supplies SDK-free AVI 1.0 demuxing, indexed Video 1
decoding, palette changes, delta-frame seeking and original PCM audio bytes in
`kauai-core`. It owns bounded input and one indexed reference image; it does not
predecode entire movies or create media/window handles. This independent decoder
uses the documented format, with Windows VfW as a compatibility oracle:
`avi-corpus-win-test` checks all 24 original clips (4,458 frames), every decoded
pixel, forward/backward seeks, fractional timeline conversion and exact PCM
bytes. `avi-movie-test` covers synthetic palette changes, nested records,
nonzero stream starts, malformed packets, truncation and size/time limits.
`kauai-sdl-video` adds `video_sdl.h/.cpp`: an SDK-free public player over the
core decoder and SDL audio streams, with stop/restart, relative frame seeks,
master gain and final-frame duration. Audio-backed playback follows fixed-format
SDL input consumption, allowing one device buffer for output latency and tail
draining; silent representations use SDL's monotonic clock. The GUI host loads
through `FileObject`, caches decoded frames and draws through the SDK-free
`GraphicsEnvironment::DrawVideoFrame` API. Live videos acquire a paired
true-color redraw request so their colours survive the app's intermediate
buffer; normal indexed redraws resume after release. No native video child
window, VfW decoder or MCI audio service participates in SDL playback.
`video-sdl-test` exercises real dummy-device PCM/postmix, seeking, stop/restart,
muting and final-frame duration. `video-sdl-host-test` checks real environment
pixels, redraw-depth acquisition/release, failed opens and handle cleanup.
`--sdl-video` adds original project scripts and play-to-completion to the Studio
smoke check. The standalone player also builds independently with
`BUILD_SDL_VIDEO=ON`; the complete GUI still needs its remaining Windows bridges.

Dialog template parsing and legacy item grouping are also SDK-free core data.
The GUI runtime's `dlg.cpp` keeps the shared value model; `dialog-api-check`
compiles it and its public header without Windows headers on all three target
platforms. `dialog_session.cpp` adds SDK-free active control state, distinct from
stored dialog values. `dlgsdl.cpp` connects the original item indices and Studio
callbacks to the SDL3/ImGui modal host in `kauai-sdl-dialog`; its controller also
compiles without native SDK headers. Native resource loading and fallback UI stay
in `dlgwin.cpp`. Fifteen of Studio's sixteen compiled templates are supported;
custom portfolio template 223 remains in native builds, while SDL Studio uses a
dedicated portfolio host. Standard application alerts now
use the same SDL modal host and an SDK-free session builder, preserving the
original answer and cancellation rules. Main-thread assertions use the SDL modal
host too, preserving Ignore/Debugger/Quit, diagnostic details and crash logging.
Worker-thread, startup and failed-frontend assertions retain native fallback UI;
installation checks still use native UI. The
frontend can be built separately with `BUILD_SDL_DIALOGS=ON`; see the
[Studio SDL notes](../tools/studio-sdl.md) for behavior and verification limits.

File backends: `platform_files_win.cpp` on Windows, `platform_files_posix.cpp`
on POSIX. `stub.cpp` is selected only on Windows. See [file/path policies](platform-files.md).

Plus codegen: `kcdc_386.h`, `kcd2_386.h` on x86 only (kauai codec asm).

Linked dependency: `Threads::Threads`. Allocation, synchronization and ticks use
standard C++ facilities. Files/text use native or portable backends;
Windows toolchain default system libraries still participate in the final link.

## kauai (gui)

Files in `kauai` (see `CMakeLists.txt`):

`appb.cpp`, `chcm.cpp`, `clip.cpp`, `cmd_gui.cpp`, `ctl.cpp`,
`cursor.cpp`, `dlg.cpp`, `docb.cpp`, `gfx.cpp`, `gob.cpp`, `kidhelp.cpp`,
`kidspace.cpp`, `kidworld.cpp`, `mbmpgui.cpp`, `mididev.cpp`,
`rtxt.cpp`, `rtxt2.cpp`, `scrcomg.cpp`, `screxeg.cpp`,
`sndam.cpp`, `spell.cpp`, `text.cpp`, `textdoc.cpp`, `video.cpp`.

Plus Win32 implementations: `appbwin.cpp`, `dlgwin.cpp`, `gfxwin.cpp`,
`fnidialogwin.cpp`, `menuwin.cpp`, `gobwin.cpp`, `regionwin.cpp`,
`region_native_win.cpp`, `pic.cpp`, `picwin.cpp`, `midi_stream_win.cpp`. Plus `frame.rc` resources.

Linked deps: `kauai-core`, `3DMMForever::AudioMan`, `Msacm32`,
`Vfw32`, `Winmm`, `mpr`.

The experimental SDL Studio uses `kauai-sdl-audio` for WAVE decoding and
resampling, plus the SDK-free `kauai-audio-mixer` and `kauai-wave-codec`
libraries for mixing and legacy RIFF encoding. Its original engine WAVE import
path now calls those services while retaining the existing MSND writer.
The original recorder uses SDL capture/preview, the SDK-free
`kauai-recording-data` processing library and the existing file service for
PCM WAVE save. AudioMan and Windows ACM are excluded from the SDL runtime
link; native Studio retains them. Winmm remains for MIDI and other native
runtime paths. Actual macOS Studio execution remains unverified.
See [SDL Studio sound coverage](../tools/studio-sdl.md#remaining-work).

## brender-core / brender

| File           | brender-core | brender |
|----------------|--------------|---------|
| `bren/stderr.c`  | ✓ |   |
| `bren/stdfile.c` | ✓ |   |
| `bren/stdmem.c`  | ✓ |   |
| `bren/tmap.cpp`  | ✓ |   |
| `bren/zbmp.cpp`  | ✓ |   |
| `bren/bwld.cpp`  |   | ✓ |

`brender-core` links `kauai-core` + `BRender::Libraries`.
`brender` links `brender-core` + `kauai` + `BRender::Libraries`.

BRender core diagnostics write to stderr and retain exit code 10 on fatal
errors. The GUI World adapter installs its own handler before BRender
initialization, preserving warning/error dialogs and crash logging, and
restores the previous handler when BRender closes. Core consumers do not
need to provide a crash-log shim.

## engine-core

```
actor_data actor_track actor_route actor_state actor_pose actor_frame
actor_timeline actor_seek sound_queue movie_playback
body       modl        movie_data  mtrl        scene_data
tagl      tagman      text_box_data tdf       tdt        tmpl
```

Links `kauai-core` + `brender-core`; public headers are in `inc/`.
The headless actor render harness uses `soc_core.h` and the real
Template/Body/Material loaders, with no World or application stubs.

Body owns the BRender actor tree and model/material state. Visibility and
mutations notify optional renderer callbacks. The callbacks and their
non-owning context are copied with a duplicate; their lifetime must cover
all such bodies. Configure the binding while the body is hidden. Restoring a snapshot
also restores the binding captured in that snapshot, matching the old
World-pointer semantics. A body
without a binding can still be edited, shown and hidden headlessly.

`BodyWorld` in `engine` binds Body to World and owns attachment, screen
picking, World render callbacks and dirty notifications. Core code neither
stores nor dereferences a World pointer.

TagManager cache operations have no application dependency. GUI/runtime
callers own BeginLongOp/EndLongOp feedback. The default content resolver
`vptagm` is defined by `engine-core`, initially null; hosts configure it
before using APIs that resolve external tags. Likewise `vpcex` belongs to
`kauai-core`; clock users create a core command dispatcher before use.

Targeted command recording/replay needs a host-owned HID resolver installed with
`SetCmdHooks` from `cmd_hooks.h`. Without it, recorded targets resolve to nil;
normal pointer-targeted dispatch does not need that hook. The callback and its
registry must outlive replay/HID lookup use. Hooks are process-global and must
be cleared/replaced before their owning host is destroyed. Passing a nil burial
hook retains core dispatcher/clock cleanup. A real record/replay gate reconstructs
a handler with the recorded HID and checks both repeated deliveries through this
resolver, without ApplicationBase or GUI replacement symbols.

## engine (editing and playback runtime)

`body_world.cpp`, `actor.cpp`, `actredit.cpp`, `actrsave.cpp`, `actrsnd.cpp`, `bkgd.cpp`,
`msnd.cpp`, `srec.cpp`, `movie.cpp`, `scene.cpp`, and `tbox.cpp`.

Actor editing/playback needs movie and scene services. Background binds
camera and lights to World. Sound import/playback and recording depend on
audio devices and the application sound manager. These objects belong in
the runtime rather than relying on static archive selection to hide their
dependencies from core consumers.

Links `engine-core` + `kauai` + `brender` and re-exports core publicly.

## Why playback files are still gui-coupled

`movie`, `scene`, `tbox` need:

- `DocumentBase` (kauai gui) for the document model + close-with-save.
- `GraphicsObject` for view invalidation on edits.
- The kauai modal-pump for stop-rendering / interruption.

`Movie` now composes `MovieData` from `engine-core`. That component owns the
archive resource file, movie root number, title, actor roll call, next actor
ID and scene count. Runtime loading delegates to its headless load path;
runtime autosave shares its version/root and roll-call serialization.

`MovieData` can initialize an empty archive, load a `.3MM` chunk graph and
save it without an application, sound device or renderer. Scene and actor
payloads remain opaque at this layer. Embedded roll-call TAGs own resource
references: callers inserting such entries must open/duplicate those TAGs
and ensure their target chunks are present. Save uses ChunkyFile's staged
save path and makes the resulting backing file persistent.

`Scene` composes `SceneData` for frame range/current position, name and
transition. Core owns scene-header I/O and bounded event-group I/O; runtime
still converts actor/text-box chunk IDs into objects and resolves sound tags.
`SceneData::FCreate` creates a real empty scene header and both required event
groups without constructing a Movie or GUI. Camera, pause and sound group
wire records are validated before runtime conversion; sound-array lengths
are checked before iteration. Metadata access supports either byte order.
Raw event groups retain their original byte order for the caller to decode;
this does not establish full foreign-endian runtime playback support.

The movie round-trip test now crosses the boundary in both directions: it
saves a scene through the runtime, closes it, edits the transition through
core, saves and closes again, then reopens/loads the scene in the runtime.
Core checks cover camera, pause and sound records, foreign-endian metadata
and group headers, and malformed-length rejection without replacing saved
event lists.

`ActorData` now owns actor-header parsing (44-byte current and 40-byte legacy),
route parsing, actor-event wire validation and conversion between fixed-width
file records and runtime-width values. Actor save/load and tag enumeration
use these core codecs; resource resolution and live Actor behavior stay in
the runtime. Returned event TAGs are unopened; callers resolve/open them only
when constructing live objects. The auxiliary movie tool uses the same header
parser. Independently constructed foreign-byte-order fixtures cover headers,
actor event positions, costumes, sounds and Add-event angles. Several inherited
byte-order masks were corrected, including an Add-event mask that exceeded its
payload size; surrounding-byte checks guard that boundary.

`RichTextData` in `kauai-core` reads/writes the fixed-width document defaults,
font name, raw text bytes, property entries and optional property-argument
group. It owns file/table references and retains text encoding plus property
and argument byte order/OSK. Platform font-number mapping, text translation
for display and document/view state remain in `RichTextDocument`.
The runtime shares the stored-data reader and root writer with core.

`TextBoxData` in `engine-core` owns caption bounds, story/credit mode and frame
lifetime. `TextBox` composes it and delegates metadata I/O and visibility
predicates. The 36-byte caption header is preserved, with its story byte at
the original offset and deterministic reserved padding. Core metadata accepts
signed lifetime values used by the runtime and initializes bounds explicitly.

Core gates round-trip native/foreign rich-text property and argument tables,
retain raw caption bytes, check failed-read ownership and allocation-failure
write cleanup. Runtime integration loads a core-written caption, checks its
text, saves it through TextBox and inspects the result through core. This uses
a test-only system DC/font registry; core checks require neither.

`ActorTrack` owns the mutable route/event tables and embedded TAG references.
Runtime Actor composes it. Track cloning is transactional, deep-copies both
tables and acquires independent references for opened TAGs. Track swaps support
undo restoration; explicit runtime-state copying preserves object identity and
avoids shallow-copying owning members. The host still owns template/header
metadata and recording state; evaluated values now live in ActorFrameState. Table mutations must preserve validated
runtime layouts and close owned TAGs before removing or replacing their rows.

Core tests cover independent clones, TAG counts, unopened/custom-costume TAGs,
failed-clone isolation, repeated cleanup and wire equivalence. Runtime tests
exercise both duplicate reset modes, failure after cloning the track, restore
after edits and destruction in both source/snapshot orders.

`ActorRouteEvaluator` supplies the route portion of deterministic evaluation:
explicit frame, first-frame, resolved step and event-cursor inputs produce a
route location and interpolated position. Actor delegates its existing route
helpers to this implementation. Recording/rejoining select whether movement
stops at action/step events. Core gates cover signed starting frames, multi-node
movement, zero-step dwell time, final-node semantics, event clipping, subroute
boundaries and repeatability. Runtime gates also check recording/rejoining
selection through Actor's wrappers. These are route checks, not yet full
seek or audio playback equivalence.

`ActorFrameState` is a copyable value snapshot of the evaluated pose, action/cel
caches, frame/event cursors, visibility and freeze flags. Actor composes this
state; resource ownership, lifetime/prerender caches and recording state remain
runtime responsibilities. Core applies rotation, stretch, scale, step, freeze,
tweak and route-translation events without touching authored frame caches or
emitting effects. Runtime still stamps event frame caches and handles action,
costume, entrance/removal and sound effects. Invalid pure-event widths and
zero-step route locations preserve core state; resource-event payload validation
remains the runtime's responsibility.

`ActorPoseEvaluator` computes rest/stretch/scale and route/current rotation from
explicit template facts and evaluated values. Actor's adapter fetches rest angles
and text-template orientation policy, then applies the returned pose to Body.
Core owns tangent weighting and vector-to-angle calculation, including corrected
X-only pitch initialization, independent pitch/yaw outputs and a bank output
that no longer overwrites (or dereferences a missing) pitch output. These fixes
also correct path-angle values used by later editing operations.

Core checks cover value snapshots, pure-event behavior, rejection without state
changes, optional angle outputs, curved-route 4:2:1 tangents, subroute isolation
and noncommuting rest/stretch/rotation combinations. Runtime checks cover cache
stamping, delegated state changes, duplicate/restore after evaluation and explicit
template orientation passed to pose calculation.

`ActorFrameEvaluator` now sequences a frame from explicit resolved-step and
playback-mode inputs. It emits ordered `CheckEntrance` and `ApplyEvent` commands;
the host answers entrance queries and applies events before acknowledging them.
Core owns entrance ordering, route/event clipping, initial freeze/action timing,
ordinary-sound skipping versus motion-match registration, cel advancement and
final pose/cel/sound directives. It owns no renderer, audio queue or application
services. The runtime adapter preserves the effect order: motion sounds, body
positioning, then cel selection; intermediate frames update pose only. Lifetime
checks use refreshed host metadata after entrance queries.

Callers stop after `Finished` and read the result. Event frame caches may be
refreshed during queries, but tables must retain their structure until the frame
finishes. The inherited nonfatal failure behavior is preserved: scan advances,
while the applied-event cursor stays on a failed event; a later command can retry
that event. Entrance failure aborts without final cel/pose effects.

Core gates include explicit expected values and ordered command traces, failed
events/entrances, freeze/unfreeze/action timing, recording, failed step lookup,
dirty-state accumulation and entrance cache refresh. A shared synthetic authored
track tests headless frame state across sequential and direct evaluation, with
ordinary-sound suppression and motion-match request counters. The real runtime
uses an in-memory action and valid empty Body to compare every evaluated field
across sequential, direct, backward/forward and beyond-lifetime seeks, including
lifetime recomputation. Audio counters establish policy rather than actual
sound-manager/queue integration; synthetic seek checks do not prove equivalence
for arbitrary original movies or provide an independent legacy playback oracle.

`ActorTimeline` owns lifetime/stall calculation and backward/quick-seek
arithmetic. `ActorStepSource` supplies borrowed action/cel step metadata; core
scales those values without retaining a Template, renderer or application.
Lifetime calculation updates event frame caches incrementally and reports the
last attempted frame if metadata resolution fails. Runtime keeps lifetime dirty,
withholds the optional reported-last-frame output on failure, and defers all
calculation while recording. Backward probes restore cel/frame/location and
leave their separate output untouched on failure. A failed multi-step backup
retains earlier successful steps; vertical transitions request replay. Backups
beyond lifetime now return an explicit valid result without rendering/audio
effects, fixing an inherited uninitialized validity output.

Core gates cover signed frame starts, static and per-cel steps, scale/freeze
interaction, partial frame-cache updates on failure, cross-segment backward
movement, successful/failed probes, per-step versus whole-seek rollback,
vertical fallback and beyond-lifetime behavior. Runtime gates use real action
metadata to verify failed calculation/retry, evaluated-state preservation and
recording deferral, alongside the existing full-field seek comparisons.

`ActorSeekEvaluator` now owns outer actor seek policy: same-frame no-op,
entrance-checkpoint rewind, quick backup/fallback, intermediate/final frame
selection and accumulated evaluation failure. It emits Hide, ResetCostume,
ApplyQuickEffects and EvaluateFrame commands. The runtime adapter clears
recording on reset, preserves best-effort costume restoration, applies quick
sound/body/cel effects and shares one dirty accumulator across frame commands.
Core holds no application, body or audio ownership. Quick-effect failures stop;
ordinary frame failures are retained while later frames still evaluate.

Command gates pin hide/reset order, ignored costume reset failure, no-op and
empty tracks, quick/numeric failures, vertical fallback replay, recording reset
and sequential-versus-seek state equivalence. The existing real runtime seek
suite exercises the adapter. One inherited compatibility edge remains explicit:
a forward seek entirely before the actor's first frame increments its stored
frame once, rather than necessarily reaching the requested frame (for example,
-5 toward -2 with first frame 1 leaves -4). The new controller preserves and
checks that behavior; its correction is separate from this extraction.

`SoundPlaybackQueue` now owns pending sound requests, their resource references,
queue identity/replacement, mixed-priority ordering and preview-duration policy.
It stores explicit sound metadata and a backend resource through BASE; it never
casts that resource to a playback implementation. Enqueue acquires a reference,
dequeue transfers it, and clear/destruction release pending requests. Queueing
retains duplicate identities; nonqueued high-priority requests remove the first
stored match, while low-priority conflicts are ignored. Legacy signed-low-word
queue IDs and replacement-before-insertion failure behavior are retained.

Runtime MovieSoundQueue composes this core queue. It synchronizes and invokes
MovieSoundMSND playback, owns the Clock, handles alarm failures and stops device
sounds. Short mode requests a timeout after dispatch; long mode requests no new
alarm and preserves any existing preview alarm. Off mode flushes pending requests
without dispatch. StopAll retains pending requests, matching prior behavior.

Core checks cover reference transfer/destruction, replacement, duplicate IDs,
ordering, off/short/long decisions and allocation failures. Runtime checks use
silent real MovieSoundMSND resources and a real Clock to verify argument mapping,
drain ownership, preview alarm creation/retention, off-mode flushing and failed
alarm allocation cleanup. They establish control flow and ownership without
claiming audible output or device/backend coverage.

`MoviePlaybackState` holds playback flags, pause/fade values, transition and
frame/time counters. Movie composes it without moving view, scene, Clock or sound
ownership. `MoviePlaybackEvaluator` now sequences each render callback through
explicit queries/effects: fade, scroll/pause handling, presentation, sound/view
flush, elapsed-time alarm, scene/frame advancement and rendering. Scene effects
may update the borrowed pause state at their original points. Idle/tracking
render eligibility is also shared core policy.

The runtime adapter retains stop cleanup and sound/volume restoration. Core
emits Stop; acknowledging that command alone does not perform those effects.
Presentation and ordinary alarm failures request immediate cleanup; scene/frame
advance failures request a deferred stop. The timed-pause alarm retains its
legacy best-effort return handling. Integer fade steps and original alarm timing
are preserved, including elapsed-time clamping and timestamp rollover.

Core command traces cover normal advancement, pause changes during presentation,
scrolling delaying pauses, multi-callback click/sound/timed waits, each alarm
failure policy, deferred stop, scene replay and fade completion. Runtime checks
exercise the real idle/alarm-to-render handoff without invoking window rendering.
These checks do not constitute an end-to-end test of the native view/stop cleanup.

`MoviePlayEvaluator` now owns play-button/startup decisions: stop-request toggles,
missing scenes, end-of-movie rewind, sound-state/frame/time setup, startup failure
policy, opening refresh and final dispatch. The runtime adapter starts/publishes
the Clock before its first alarm, replays a scene before prerendering and performs
ordered failure notifications/effects. Paused toggles run host render-stop cleanup
before selecting sound dispatch from the restored settings.

Core command gates distinguish clock failure from scene-preparation failure,
rewind failure, already-stopping no-op, paused toggle and host-updated sound
settings. Native tests exercise missing-scene notification and real actor/caption-
free scene startup, clock/playing notification, long sound mode and repeated
stop-request toggles. They cancel pending rendering and restore test state directly;
window-dependent stop cleanup and background restoration remain outside that gate.

## Future improvements

The application boundary is implemented. Further work can add a complete
GUI-free scene/movie player, more background/sound asset data APIs, a narrower
evaluation target, installed SDK exports, independent engine instances or SDL/macOS
platform implementations. None is needed to consume the core targets from the
current source tree. Keep evaluation in `engine-core` until a
narrower dependency closure or a separate consumer justifies another target.
Background and sound files still combine asset data with their runtime adapters;
further extraction can be done independently of this completed boundary.

## Headless test targets and what they link

| Target                  | Link line                                      |
|-------------------------|------------------------------------------------|
| `geometry-test`         | `kauai-core`                                   |
| `codec-test`            | `kauai-core`                                   |
| `extract-bmdl`          | `kauai-core`                                   |
| `inspect-chunks`        | `kauai-core`                                   |
| `actor-render-test`     | `engine-core` (which pulls `kauai-core` + `brender-core`) |
| `brender-diagnostic-test` | `brender-core` (no application stubs)            |
| `bren-rasterizer-test`  | `BRender::Libraries`                           |
| `movie-save-load-test`  | `engine` (full gui — currently)                |

## See also

- [`kauai-framework.md`](kauai-framework.md) — what each kauai file does.
- [`engine.md`](engine.md) — what each engine file does.
- [`brender-wrapper.md`](brender-wrapper.md) — BRender wrapper layout.
- `project_cmd_split_followup.md` (project memory) — running notes on
  remaining split candidates.

## Boundary checks

From an initialized MSVC environment, configure a Debug build and run:

```console
cmake --build build --target library-checks
ctest --test-dir build --output-on-failure
```

The three `*-core-link-test` executables force-load each core archive and its
core dependencies: `/WHOLEARCHIVE` and `/OPT:NOREF` on MSVC, `-force_load` on
Apple linkers and `--whole-archive` on ELF linkers.
They supply only the documented host assertion-reporting functions.
Their translation units reject GUI/World header definitions. The engine
check also exercises cache clearing without an application and renderer
notifications across visibility, mutation, duplication and rebinding. It
also saves/reopens an archive with scene/asset graph edges and an embedded
actor roll-call reference, checks same-file saves and failed-load isolation,
and rejects a mismatched roll-call wire layout. Opposite-byte-order movie
prefix and roll-call fixtures verify byte swapping, and a failure after
opening an embedded TAG verifies reference cleanup. This synthetic graph checks
archive preservation; it does not validate scene/actor payload decoding.

`brender-diagnostic-test` checks public warning/fatal dispatch, handler
replacement/restoration, stderr output, and fatal exit code in a subprocess.
The movie round-trip check links the full runtime. Actor rendering is
registered when `TEST_MAIN_CHK` and `TEST_CONTENT_DIR/tmpls.3cn` exist; CI
without those original assets does not run that fixture-dependent check.

An independent CMake consumer checks public include/definition propagation:

```console
cmake -S tests/consumer -B out/consumer -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_C_COMPILER=cl -DCMAKE_CXX_COMPILER=cl
cmake --build out/consumer
out/consumer/core-consumer.exe
```

`BUILD_STUDIO=OFF` disables the application, chomped content, and its
installation/packaging rules. Core tests can still be built explicitly.
Both Windows x86 and x64 Debug configurations run the library gates in CI.

Kauai's Windows bootstrap preserves an inherited stdout pipe when deciding
whether to allocate a debug console. It no longer calls Studio's MCP command
parser; auxiliary GUI-linked tools therefore need no Studio startup stubs.

Debug allocation-failure gates now verify that a failed buffer resize retains
its original contents and size, and failed event-list replacement preserves
both original child links and total chunk count. The earlier assertion was
an allocator failure-path size-accounting bug; it is fixed. These tests also
caught and fixed deletion of a staged chunk after it had already been adopted.

## Platform and UI direction

The goal is to run the existing Studio through SDL3, including on macOS.
Preserve its Kauai controls, resource scripts, tools and editing behavior.
Dear ImGui can support auxiliary tools; rebuilding Studio in ImGui is not a
prerequisite for this port. Keep SDL3 and ImGui dependencies in host adapters;
the core libraries must remain usable by command-line tools and regression tests
without either. See the [Studio SDL bridge](../tools/studio-sdl.md).

Native open/save dialogs now live in `fnidialogwin.cpp` on `kauai`, while path
and file operations remain in `kauai-core`. Existing `Filename::FGetOpen/FGetSave`
methods remain declared for source compatibility, but require the GUI library.
Studio's portfolio has a separate SDK-free request boundary in
`inc/portfolio_host.h`: `portf.cpp` coordinates application commands while
native builds select `portfolio_win.cpp` for common-dialog hooks and controls.
SDL builds select `portfolio_sdl.cpp`, which connects the original artwork,
previews and invocation sounds to `sdl_file_dialog.cpp` in `kauai-sdl-dialog`.
`file_browser.cpp` on `kauai-core` handles directory navigation, ordering, file
filters and candidate paths without changing the process directory or writing
selected files. Overwrite confirmation remains Studio's original help topic;
actual file commands and movie writers stay unchanged.
`portfolio_data.cpp` on `engine-core` owns movie thumbnail
loading, bitmap fallback, isolated preview errors and legacy save-suffix helpers.
Both the production code and its pixel/file fixtures compile without SDK headers.
The public `portf.h` no longer exposes Windows handles
or callbacks. `portfolio-api-check` enforces the include boundary. The browser
and rendered SDL chooser have separate runtime tests; the Studio smoke check
also saves, previews and reopens artifact fixtures with `--portfolio-files --text`.
The entire legacy spell-checker implementation (`spell.cpp`) also lives on
`kauai`: it discovers dictionaries through the Windows registry and loads a
CSAPI DLL. A future UI can choose its own spelling service.

Completed platform steps, kept separate from file-format changes:

1. Windows allocation is replaced by portable storage with aligned private size
   metadata; legacy allocation failure injection, zeroing, debug checks and
   memory-liberation callbacks are retained. BRender uses C runtime allocation.
   `Mutex` uses a recursive standard mutex; diagnostic thread IDs are now
   process-local tokens rather than OS IDs.
2. `KauaiPlatform::SystemTicks` uses monotonic millisecond ticks modulo 2^32.
   `SetTickSource` allows deterministic tests or a host-provided source. Configure
   it while clock users are stopped, before creating/reinitializing clocks;
   switching sources does not rebase existing clocks. The default needs no
   Winmm link.

3. File I/O, paths and directory enumeration now use a header without OS types,
   with Windows and POSIX backends. Windows behavior and stored text encodings
   remain intact; POSIX sharing/case/rename policies are documented separately.

4. Text conversion and user comparison now use `platform_text.h`, preserving
   Windows behavior with a documented deterministic portable fallback. Public
   core headers no longer include SDK headers; native interop tokens retain ABI.

5. Geometry owns scanline data and an optional opaque cache lifetime. Native
   `HRGN` construction, translation and destruction live in `region_native_win.cpp`
   and `regionwin.cpp` on `kauai`. Native picture rendering/import (`pic.cpp`,
   `picwin.cpp`) also lives there. `PictureData` in core reads/writes the original
   20-byte picture envelope and preserves the opaque payload, including byte order.
   It cannot render EMF/PICT. Loading a Windows picture and saving it again retains
   its stored bytes. BMP header structures are fixed-width and SDK-independent.
6. `LegacyLong`/`LegacyUlong` explicitly retain 32-bit Kauai words on LP64 and
   remain `long`/`unsigned long` on Windows. BRender C scalars/colours follow the
   same width contract. Wire-layout assertions, exact integer multiply/divide,
   alignment-safe codec reads, aligned allocation headers and explicit nullable
   object allocation remove assumptions exposed by the portable build.

Portable validation:

```console
cmake -S . -B out/core -DCMAKE_BUILD_TYPE=Debug -DBUILD_STUDIO=OFF -DBUILD_PACKAGES=OFF
cmake --build out/core --target core-checks --parallel 4
ctest --test-dir out/core --output-on-failure
```

The workflow definitions for these checks, the existing JSON golden comparison
and an external core consumer on Ubuntu/macOS are disabled in this worktree.
Local validation cross-builds ARM64 macOS binaries; macOS execution remains
unverified. The Linux x64 binaries also run
under QEMU with UB checks enabled, including the strict original JSON baseline.
The core-only target excludes Windows GUI/movie runtime tests. Original-asset
rendering still needs the optional content fixtures described above.

Remaining platform steps:

1. Expand portable original-content and rendering coverage, including non-CP1252
   fixtures and further arithmetic/alignment paths beyond the current tests.
2. Port the existing Studio's Kauai host: window lifecycle, input, drawing,
   fonts, clipboard, cursors, dialogs and audio. Translate SDL events into the
   existing Kauai commands and retain the original Studio UI. The separate
   [archive inspector](../tools/inspector.md) remains a diagnostic tool.

The SDL main window now keeps SDL's native window procedure. Kauai no longer
subclasses it or re-dispatches native paint, input, activation and command
messages. Exposure/close requests become SDL events; logical damage feeds the
existing renderer. Default SDL/FreeType startup no longer borrows a screen DC;
the optional GDI font path retains one and releases it explicitly at shutdown.
A Windows SDL lifecycle test checks both paths, procedure identity, OS event
translation and repeated cleanup. This does
not remove the remaining GDI drawing or make the Studio binary portable yet.

SDL ports in all six packed formats own their pixels without allocating a
Windows bitmap or device context until a native operation needs one; 4/8-bit
indexed ports require an application palette for this path. Native font measurement requests only a context; native
drawing also synchronizes a bitmap mirror with
CPU storage. Indexed palette changes stay in CPU storage until needed by native
drawing. Only indexed creation requiring device-palette bootstrap still
allocates native resources eagerly. The portable drawing API itself stays free of SDK types.

`palette_state.h/.cpp` moves the truecolor-display palette rules into `kauai-core`.
SDL uses it without querying the Windows display; the native implementation
remains the comparison oracle. Reserved colors, collision adjustment, retained
entries and animation behavior are checked across all flags and lengths 0..300,
with portable runtime tests on Linux and a macOS cross-compile.

The streamed MIDI mixer now uses the SDK-free `midi_stream_host.h` contract.
WinMM device handles/callbacks and the existing stream/scheduled backends live
in `midi_stream_win.cpp/.h`; native headers use the SDK's pointer-width `MIDIHDR`.
The fixed event prefix and pointer-sized completion contract compile on Linux
and macOS too. Real Win32/Win64 mapper tests cover queue/repeat/offset/stop and
native header preparation. The streamed player/cache/queues/mixer now live in
`kauai-core` with an injected device factory and a portable timed worker.
Buffer returns signal a portable event; stop waits without polling shared state
outside its mutex. Headless mixer tests cover priority, expiry, preemption and
seek/resume on Windows and Linux, with a macOS cross-compile. The SDL build now
uses `kauai-midi-synth` (`midi_synth.cpp`, `midi_render.cpp`) and the portable
`kauai-sdl-midi` adapter (`midi_sdl.cpp`). The synth has a pinned TinySoundFont
dependency and a licensed GeneralUser GS SoftSynth bank; it preserves GM program
and percussion selection while changing timbres from WinMM. Borrowed-buffer
callbacks run on a separate worker; the audio callback only renders/queues PCM.
The original WinMM adapter remains available explicitly on Windows. The native
Studio's backend and movie/MIDI serialization remain unchanged.

Native dialogs and registry/DLL spelling integration have left the core archive,
and portable allocation/synchronization/timing plus native file backends are
implemented, and native GDI is outside core. The macOS core build does not provide
a macOS Studio application. A separate SDL3/ImGui archive inspector is available
with `BUILD_INSPECTOR=ON`; it does not yet play or edit movies. No `.3mm` record versions or
writer normalization were introduced by this extraction; the existing strict
Windows JSON fixture remains the cross-platform compatibility baseline.

The read-only [`3mm dump --json`](../tools/3mm.md) tool now supplies sorted,
layout-independent graph dumps with strict IDs and complete unpacked payloads.
It is a regression baseline rather than a complete compatibility validator.

See [text, comparison and native interop policies](platform-text.md) for encoding
limitations and the SDK-free public-header checks.

The current SDL Studio stopping point and resume priorities are recorded in the
[2 November 2026 handoff](../tools/studio-sdl.md#handoff-for-2-november-2026).

The [3 November graphics update](../tools/studio-sdl.md#progress-for-3-november-2026)
records the RGB555/BGR24 shape work, verification and remaining native paths.

The [packed scrolling update](../tools/studio-sdl.md#packed-scrolling-update-for-3-november-2026)
records the six-format scroll adapter, native monochrome policy and regression evidence.

The [four-bit drawing update](../tools/studio-sdl.md#four-bit-drawing-update-for-3-november-2026)
records the portable indexed shapes, monochrome highlight path and remaining
monochrome brush-state work.
