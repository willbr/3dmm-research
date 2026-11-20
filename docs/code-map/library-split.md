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

`mm-engine-c89` in `libraries/engine/` contains the first implementation slices
of the [shared C89 engine migration](../../plan.md#shared-c89-engine-migration).
Its public header is `mm/playback.h`. It owns playback/startup command
sequencing, wait/fade/scene-advance decisions, fractional movie-frame pacing,
the idle-render gate and retail scrolling policy. Its only library dependency
is the C runtime; no Kauai, BRender, SDL, C++ or native platform headers enter
the public interface or implementation.

`mm/movie_session.h` exposes the standalone C89 movie session. It loads and owns
the chunky index from a borrowed `MMReadStream`, validates the first `MVIE`, and
reads `SCEN` headers by the root's authored slot. It uses the shared KCDC/KCD2
decoders for packed records. `engine-native-chunky-test FILE --movie` supplies
a real native file stream and prints root versions and ordered scene ranges.
A borrowed `MMMovieChunkView` applies the same root and scene directory policy
to a host's current chunk graph. `mm_movie_session_open_graph` retains the
validated root and the graph provider. Studio owns that session in movie
state and resolves scene slots using fresh access to the editable graph.
When save changes the root ID, Studio attaches a new session after validation.
Marcus's `Archive::ReadMovieDirectory` retains the session after reading the
authored scenes and source directory. The session moves with the archive into
Preview. The read adapter continues to apply the archive's decode budget.
`mm_movie_session_camera` validates and reads the scene's start and frame event
groups, evaluates camera cuts at an authored frame, and decodes the specified
background's `CAM` chunk. Source zero uses the movie's own index. For positive
sources the host supplies a borrowed stream for the requested asset's archive.
`MMMovieEventView` borrows an event group or prefix for the shared last-event
selector. The standalone session reads wire event rows. Marcus seeks include
the target frame and read the current camera cues, including edits. Studio
rewind finds the last camera or sound in the processed prefix before the
target frame, then the existing dispatcher replays the target frame's effects.
All three readers use the same C89 exact child lookup to select a `CAM` at
its authored slot. Studio and Marcus resolve its slot-zero `MBMP` and `ZBMP`
through the same C89 binding in the archive that owns the camera. Their
renderer adapters load, cache and present the selected records.
`mm_movie_session_actor` follows the scene start group's actor links. It reads
the ACTR header and the slot-zero PATH and GGAE children through the same graph
reader as Studio and Marcus. The reader accepts decoded streams from either
the standalone index or an open native graph. The shared track decoder writes
native-order route points and runtime-width events into the caller's storage,
including unopened tags for sound and costume events. It validates event rows
before constructing the runtime group. The standalone query then replays the
actor evaluator from a fresh state. It uses action tables from the resolved
template or the shared 3D text action metadata. The template cache uses the
same C89 TMPL rest-angle and flag decoder as the standalone evaluator. Both
paths use the same C89 ACTN header reader and owning GGCL/GLXF tables.
The snapshot loads the matrix table on first body use and keeps it for the
current action. The native cache loads both tables at admission and converts
optional GLMS entries to C-owned sound tags. Both paths read cels, parts
and matrices through the C accessors. The actor query returns route position, action,
cel and fixed-point root pose without creating body, costume or audio objects.
`mm_template_read_shape` acquires GLPI then GLBS through each provider.
`part_indices.c` decodes both tables into owned arrays of signed 16-bit
indices in native byte order. The live template cache and standalone session
use the same reader and index accessors. The same C89 traversal computes the
number of sets up to the highest authored set index.
`mm_template_read_costumes` uses that count to read GGCM into a C-owned
collection. It checks row boundaries and ID counts and retains the authored
word order. Studio, Marcus and the snapshot use the same count and ID
accessors. The snapshot retains the table for later costume resets during the
seek. A missing link triggers legacy discovery. A failed read of a linked
table propagates the error. The live cache appends discovered choices and
3D text's generated default directly to the C collection. Its native adapter
supplies the allocator and debug memory tracking.
Studio, Marcus and the standalone evaluator use `mm_actor_template_link`
to recover the embedded template number when a movie edit or import changed
it. An external source keeps its authored template number and uses the
ordered archive search.
Studio and `mm_preview_actor_seek`, which serves Marcus and the standalone
session, both run `mm_actor_playback_seek` from `mm/actor_timeline.h`. It
coordinates seek commands, step resolution, entrance tests, frame events and
display effects. It carries Studio's position dirtiness across intermediate
frames and applies the profile's quick-backup sound behavior. Host services
refresh the lifetime when needed and supply costume, pose and sound effects.
Studio's current-frame sound replay also walks the C actor's event cursor in
the shared driver.
Studio's actor embeds the same `MMActorState` as the C evaluator. The seek,
frame and event drivers work on that live record, so native effects observe
its state directly.
`mm_movie_session_seek_live` drives the scene-level seek in Studio and Marcus.
It begins the frame, evaluates actors in scene order and runs the final phase
only when every actor succeeds. Studio dispatches live events first and handles
captions and prerendering in finish. Marcus selects its live camera in finish.
After finish succeeds, the driver commits the cursor before calling either
host's publication callback. Studio still advances its edit cursor during
event replay. Its prerender can draw the prepared pose while the session
is finishing the same scene. Rejected seeks preserve prior publication when
the cursor never moves. Failed effects retain the pose prefix as partial.
`mm_movie_session_seek` uses the same transaction for an immutable archive.
It reads the scene and start group once, resolves the camera and evaluates
all actors in authored order. It publishes an owned snapshot on success.
`mm-movie-content` supplies a C resolver for either query. It reads the movie's
source-name table, resolves its aliases through the existing content VFS and
searches mounted `.3cn` archives by exact chunk type and number. Archive
opening remains a platform service. `engine-native-chunky-test FILE --seek
SCENE FRAME RETAIL_CONTENT_DIRECTORY` mounts the retail directory and prints
the complete frame snapshot.
SDL Studio and Marcus use the same GST link lookup and C89 source-table reader
for movie-authored sources. Both host adapters preserve the legacy text
conversion. Studio merges names into the existing tag manager and Marcus
passes the ordered rows to its content mounts. Archive enumeration by filename
extension uses the C89 VFS in both the host VFS adapter and the standalone
content resolver.
`mm/resource_lookup.h` supplies the ordered archive search for the standalone
resolver, SDL Studio's resource-manager fetch and file lookup, and Marcus
preview's asset selection. A miss advances to the next eligible archive and
an open or fetch error stops the search. Each provider supplies its own
eligible archives and read/cache operation. Marcus uses its movie cache for
source zero and the mounted cache list for positive source IDs. The standalone
resolver opens archives lazily and returns the one owning the exact chunk key.
`mm_movie_session_body` adds the on-stage body's parent and set indices,
local part matrices and authored model bindings. Ordinary templates use their
selected cel and linked matrix table. Embedded 3D text uses the shared text
generator and resolves its font metrics and glyphs through the same content
provider. Body replay applies template defaults and each action or costume
effect in frame order. Each part exposes the authored cel directive, the
effective model after any retention or replacement, and its material and
archive key. Generated text defaults use the text material without an
authored asset ID.
The wire evaluator and the live `Template::FSetActnCel` path used by Studio
and Marcus both use the binding returned by `mm_movie_cel_part`. It contains
the resolved model key and fixed-point local matrix. Regular templates resolve
the exact `BMDL` child through `mm_movie_cel_model`. The live path uses cached
action tables and keeps the font glyph lookup for generated 3D text. It stages
every binding and model reference before publishing step distance or updating
the body. An authored keep-model directive leaves the costume or earlier cel
model in place and still applies the selected part matrix.
`mm_body_apply_set` maps a custom costume's consecutive slots to matching
parts in shape order. The same mapping drives generic material assignment,
including generated text, and native costume application through `Body`.
The session and the live template cache use `mm_movie_costume_link` to find
the exact `CMTL` child by authored ID. Both resolve the material and optional
model through `mm_custom_material_part`. The live cache fetches renderer
objects from those keys when loading a costume and holds them across
subsequent applications. A slot without a model preserves the current one.
`mm_template_bind_defaults` selects the first authored costume ID in each
set in both the wire evaluator and the live template path. The live path
prepares retained references before updating the body. The wire path
prepares the new snapshot through the same selector.
`mm_template_scan_costumes` visits `CMTL` links in child order when the
template has no `GGCM` table. Studio and Marcus populate the complete
costume table for the editor. The standalone snapshot uses the first
costume discovered for each set.
`mm_movie_session_body_meshes` resolves the owning template or font archive
and loads each distinct active `BMDL` into owned arrays of positions, mappings,
colours, normals and indexed faces. It composes part world transforms from
the fixed-point actor pose and local hierarchy. Studio's actor runtime and
Marcus preview publish `MMActorBodyPose` after evaluating motion and any
preview transform edits. The native body and standalone mesh path both use
`mm_actor_pose_root` to compose the root matrix. The renderer receives that
matrix directly. The standalone `--mesh` probe prints appearance bindings,
world matrices and geometry fingerprints.
Live bodies own their root and local matrices, parent links, retained
models, and material bindings in `MMBodyShape`. The body runtime forwards
changes to the draw tree. Body duplication and reshape read the C state,
and Marcus uses its matrices when computing object and costume bounds.
The draw tree and bindings share one allocation.
Both renderers submit the C89 body's root, hierarchy, models, materials,
and transforms through `mm_body_draw`. Studio applies the bindings to the
attached draw tree before rendering. Marcus retains each body while it
builds the temporary draw and picking trees from the bindings. The ordered
submission preserves sibling order under the renderer's head insertion.
The live render frame publishes the chosen projection camera and the
ordered body bindings in one `MMMovieRenderScene`. The C owner retains
each body through the draw or pick. Studio collects the attached bodies in
world order. Marcus preserves its visibility order and publishes the
camera after applying its view and depth fitting. Draws borrow the body-list
allocation from the retained `MMMovieSession` when available. The session
keeps capacity between draws. Each draw retains and releases its own bodies,
including during nested submissions.
Studio and Marcus still use their existing archive owners, playback resource
providers and renderers.

`mm_movie_session_seek_with_bodies` evaluates the selected camera and
each actor's effective body bindings in one traversal of the scene start
group. Its result owns the actor records and ordered body and part arrays
independently of the session and content files. The `--frame-bodies` probe
prints camera, appearance and mesh data through that result. The lightweight
seek continues to return camera and actor state.

`mm_movie_scene_reader` reads an exact SCEN header and validates its GGST and
GGFR event groups using the current movie graph. Studio and Marcus use it to
load live scenes. The reader also accepts a graph for resource previews and
Studio undo snapshots. Each group owns its decoded bytes until release. The
session's camera and actor queries share the event decoder with the live loaders.

`mm_movie_session_seek_graph` uses the same evaluator with the live chunk
graph and a provider for external asset graphs. The C evaluator owns the
decoded payload bytes it keeps across calls. The ChunkyFile provider supplies
the physical extent and packed bit. `mm_chunk_read_payload` retains the file,
decodes packed blocks through the registered codec, lends a bounded stream
to the receiver, then releases the file or decoded memory. Fixed movie
records require the exact decoded size. Studio uses this path for the live
movie graph, actor ID edits, and scene metadata and transition edits.
Marcus uses it to evaluate live background camera chunks. Embedded assets
use the movie's graph, and the name adapter lends the serialized chunk name
synchronously.

The Archive movie directory and inspector share the extent lookup and codec
provider with that graph path. Their C89 cache retains the payload across calls
at the same index and checks both the packed length and the declared decoded
length before calling the codec. It invalidates the dependent string table
before releasing an old block and publishes the new index after decoding and
native diagnostic acceptance. Page reads, actor headers, and movie and source
tables consume bounded C streams. The Archive GST reader validates the dense
layout, owns the decoded strings and rows in C89, and keeps extra bytes in wire
order. It uses the existing platform callback for cross-width text conversion.
Archive converts the returned page text to UTF-8 for its callers.

The GST fixture checks both header byte orders, the four supported encodings,
empty tables, page bounds, and access after the source bytes change. The movie
data, string table and collection checks pass under AddressSanitizer. Page
output, including UTF-8 text and opaque row bytes, matches the previous reader
for the Archive fixture and retail `3DMOVIE.CHK`. Evidence is in
`tmp/c89-archive-strings`.

Cache tests cover position hits, invalidation order, exact budget boundaries,
short prefixes, decode failures and retry. The Archive API fixture verifies
directory reads after a failed decode-budget check and preserves the previous
directory when a malformed scene cannot be read. The CLI dump and the suite
of actor, scene, media, text and background previews pass. The C89 cache
and storage tests also pass under AddressSanitizer. The 181 standalone
and 200 focused host checks pass. The frozen native traces and saved movie
match byte for byte, along with the 300 frame and mesh snapshots and 300
graph/stream evaluations. Both hosts match 456 prior seeks and 360 viewport
hashes and log 480 frame commits matching retail. Retail reopens the Studio
actor edit with matching state on four seeks. All eight VMM workflow checks
pass, and Marcus matches the retained VMM viewport hash. Validation is in
`tmp/c89-archive-blocks`.

The block-owner fixture exercises raw and packed extents, the exact-size
check, decoder and receiver failures, a missing key and the legacy empty
extra-cache extent. AddressSanitizer verifies file and handle cleanup.
The actor and scene fixtures cover header edits and transition read/write
ordering. The live graph and standalone stream readers agree across 300
frames in five real movies. The 31 native actor and resource traces, four
cache and renderer traces, and 480-byte saved fixture match byte for byte.
The 181 standalone tests, 195 focused host tests, five AddressSanitizer
checks, and SDL runtime compile check pass. Both hosts match 456 prior seek
observations and 360 viewport hashes, with 480 frame commits matching retail.
Retail reopens the Studio actor edit with matching state on four seeks. Both
hosts reproduce the ordinary and motion sound cues. Studio passes all eight
VMM workflow checks, and Marcus matches the retained VMM viewport hash.
Validation is in `tmp/c89-graph-blocks`.

`mm_movie_camera_graph` evaluates the selected CAM in its owning background
graph. Studio and Preview pass the slot chosen by their live timelines
and apply its `MMBackgroundCameraBinding` directly. The binding contains
decoded pose and exact colour and depth keys. During application the same
payload supplies placement points. Studio applies the BRender world effects.
Preview owns its placement points in C storage and can apply a temporary
camera while retaining the active placement cursor.

`MMRenderFrame` carries the world, camera and colour/depth targets at the
renderer boundary. Marcus's C render controller allocates the targets and
releases its world before the pixelmaps. Picking uses the same owner for
its temporary world and attached camera. Rendering and picking collect live
camera and body descriptors through the same C code. The pose retains the
source bodies and any standalone model until the temporary world is gone.
Studio passes a borrowed frame over the live world and the working targets
owned by `MMWorldBuffers`.
The scaled export path uses the frame's world and camera with separate
export pixels.

| Caller / dependency | Playback boundary |
| --- | --- |
| Studio `Movie::Play`, `Movie::FCmdRender` | Apply the C89 startup and render commands directly. C89 orders clock start, user pause and resume, alarm dispatch, and stop cleanup. |
| Marcus `playback.cpp` and `player.cpp` | Poll the C89 controller for frame pacing, transitions, waits and scroll steps. Use the same frame and scene advance decision as Studio, with optional scene looping. |
| `retail_scroll.cpp` | Supplies the native scrolling binding. |
| Host clocks, views, audio and scene effects | Execute commands and supply facts at the existing query points; retain device and resource ownership. |
| `engine-core`, `engine`, Archive/Preview, Kauai/BRender wrappers | Still require C++ outside the converted slice; these remain migration work. |

The C interface has caller-owned stack records and explicit initialization.
Studio stores deferred rendering and the paused movie-clock time with the
C89 pacer. A deferred alarm resumes with the saved clock time. A failed
alarm retry preserves its pending pacer deadline, fade state, and cleanup
order. Normal stop and document destruction share fade
volume restoration.
The Marcus polling controller advances at most one authored frame per poll.
It excludes paused time and time spent in a transition, caption scroll or
authored wait from ordinary frame pacing. Explicit rate settings use the shared
millisecond pacer. The host supplies the frame's sound cues and wait, measures
caption overflow, reports pending audio and releases the outgoing image when
the transition completes.

The C-only checks cover start and resume, transition completion, scrolling
before sound waits, click and timed waits, and capped and uncapped advancement.
Across the actor, dissolve and scrolling-text fixtures, SDL Studio and Marcus
committed 480 frames in retail's frame sequence. The Studio seek actor records
also matched V3DMM. Evidence is in `tmp/c89-polled-playback`.

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
cmake --build build/engine-c89 --target engine-string-value-test
cmake --build build/engine-c89 --target engine-string-format-test
cmake --build build/engine-c89 --target engine-string-ops-test
cmake --build build/engine-c89 --target engine-memory-test engine-error-stack-test
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
cmake --build build/engine-c89 --target engine-actor-paste-test
cmake --build build/engine-c89 --target engine-actor-clipboard-test
cmake --build build/engine-c89 --target engine-actor-create-test
cmake --build build/engine-c89 --target engine-movie-sound-test
cmake --build build/engine-c89 --target engine-body-geometry-test
cmake --build build/engine-c89 --target engine-model-resource-test
cmake --build build/engine-c89 --target engine-material-resource-test
cmake --build build/engine-c89 --target engine-texture-resource-test
cmake --build build/engine-c89 --target engine-bitmap-file-test
cmake --build build/engine-c89 --target engine-depth-bitmap-test
cmake --build build/engine-c89 --target engine-masked-bitmap-test
cmake --build build/engine-c89 --target engine-world-buffers-test
cmake --build build/engine-c89 --target engine-world-camera-test
cmake --build build/engine-c89 --target engine-world-render-test
cmake --build build/engine-c89 --target engine-world-export-test
cmake --build build/engine-c89 --target engine-background-camera-test
cmake --build build/engine-c89 --target engine-background-resource-test
cmake --build build/engine-c89 --target engine-background-mesh-test engine-background-preview-test
cmake --build build/engine-c89 --target engine-scene-owner-test
cmake --build build/engine-c89 --target engine-sound-resource-test engine-sound-payload-test
cmake --build build/engine-c89 --target engine-audio-runtime-test
cmake --build build/engine-c89 --target engine-preview-render-test engine-preview-pick-test engine-preview-camera-test engine-preview-live-test
ctest --test-dir build/engine-c89 --output-on-failure
```

For an MSVC AddressSanitizer build, configure a separate build directory with
`-DCMAKE_C_FLAGS=/fsanitize=address` and build the engine tests from a Visual
Studio developer prompt with AddressSanitizer installed. CMake copies the
compiler's sanitizer DLL beside the test executables so they can also run from
Explorer or a regular terminal.

The root build also registers `engine-c89` and `engine-c89-syntax`; the latter
uses strict C89 diagnostics when Clang or GCC is present. Existing
`engine-core-link`, `movie-save-load`, `marcus-player` and `marcus-playback`
tests cover the facades and runtime integration. Playback corpus evidence and
its limits are recorded in [playback regression](../tools/playback-regression.md).

### Movie-data reading slice

`mm/read_stream.h` adds borrowed, positional decoded-chunk input,
checked memory input and bounded substreams. Every read checks offset and size
without overflowing; nested slices retain their parent's bounds. Memory and
slice contexts point to their caller-owned objects, which must remain in place
and outlive their borrowers. Filesystem and content selection still use the
existing VFS/host path; this API adds no separate filesystem or archive cache.

`mm/movie_data.h` implements the following in C89:

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

`mm/chunky_file.h` owns CHN2 header and GG index decoding, compact and
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

`mm/string_table.h` implements dense and allocated string-table mutations:
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

`mm/text.h` owns fixed lossy Mac/Windows byte maps, portable CP1252/Mac
Roman decoding, UTF-16 byte order, portable substitution/case/comparison rules,
and conversion dispatch. `platform_text.cpp` supplies Windows `CP_ACP`/`CP_MACCP`
callbacks and native locale/case services. It remains sensitive to the actual
Windows ACP; fixed CP1252 is not substituted on that host. String-table physical
walks and width-changing reconstruction are C89 and use the same conversion
service as ordinary String readers. Internal length-first/signed-character
sorting and user locale comparison remain distinct host comparison callbacks.

Kauai's counted UTF-8 and movie source-name encoding share `text.c`.
Conversions validate scalar ranges and surrogate pairs, preserve embedded
zeros, and keep the valid output prefix on malformed input. C++ sinks append
to native containers. The movie source-name sink writes to a bounded buffer.
The migrated converter matched the original across all Unicode code points,
all two-byte input sequences, and 250,000 generated input pairs.

`engine-strings-test` covers storage moves and edits, metadata, free-slot reuse,
allocation failures, search positions, endian offsets, one/two-byte strings,
legacy map hashes, surrogate substitution and short output buffers. Native
`string-compat` adds dense/allocated save/reopen checks in both byte orders and
Mac/Windows encodings, UTF-16BE loading and duplicate independence. Text-service
checks compare native callbacks directly with Win32 and force portable policy
on Windows. The standalone platform-check project links the same C text module.

### Counted-string value operations

`mm/string_value.h` owns counted-string copying, assignment, set/delete/append/
insert, exact equality, 32-bit number parsing and control expansion. Both character
widths use the same C89 implementation over borrowed 257-unit buffers. Native
`String` delegates these operations and supplies its existing debug copy checks;
insertion still shifts the tail before those checks observe it. The C API does
not allocate or translate encodings.

Append and insert retain partial results on overflow. Insertion preserves the
legacy retained-length calculation even when that truncates more than the
requested tail. Number parsing retains repeated signs in automatic-base mode,
acceptance of a digit equal to the radix, empty hexadecimal prefixes, trailing
spaces and 32-bit wrapping. Explicit bases do not gain automatic-base sign rules.
Escaping retains CR-to-`\n`, literal LF, tab/quote/backslash expansion and partial
publication only after an escape has been emitted. Failed C parsing leaves the
output untouched; native `String` retains its zero/debug-trash failure output.

`engine-string-value` checks both widths against a fingerprint captured from
the original native code, with guard units, wide non-ASCII values, partial
expansion and copy-observation ordering. Native `string-compat` freezes the same
mutation/parse/escape corpus and optionally writes its defined results through
`MM_STRING_VALUE_SNAPSHOT`. Formatting now uses the C implementation described
below. Native varargs extraction, encoding adapters, direct buffer access and
final `String` facade removal remain host/facade dependencies.

### Counted-string formatting

`mm/string_format.h` owns format parsing, qualifier order, field widths, signed/
unsigned/hexadecimal conversion, four-byte tag conversion, padding, output limits
and partial-prefix publication. It uses the same one/two-byte borrowed buffers
and copy diagnostics as `MMStringValue`. A C argument provider supplies 32-bit
values or borrowed text with an explicit count. Native `String` retains typed
varargs extraction and native String/zero-terminated-string validation, copying
and ending its `va_list` within the adapter. No varargs or C++ types cross the C
formatting interface.

The C formatter retains the existing rejection of positional `%<N>` notation,
order-sensitive zero/left padding, explicit signs on all field types, hexadecimal
width masking and tag zero-byte substitution with byte 1. A field that exactly
fills capacity fails before writing that field; literals may fill all 255 units.
Arguments are fetched before capacity is checked, and malformed-format diagnostics
observe the old count and already-written prefix before final publication. Width
accumulation now uses defined 32-bit wrapping; original signed-overflow cases are
outside the native equivalence claim.

`engine-string-format` repeats the original Native/SDL format corpus for both
widths, with guard units and mixed pointer/numeric arguments. Additional C cases
check provider failure, fetch-before-overflow ordering, diagnostics before count
publication, sign/padding before copy diagnostics, width arithmetic and wide
non-ASCII characters. Native `string-compat` retains the same frozen fingerprint
and optional `MM_STRING_FORMAT_SNAPSHOT`, including malformed-format observations
in Debug. Native ABI/text services and final String facade removal remain work.

### String validation, ordering and text-service control

`mm/string_ops.h` owns counted-string validation, unbounded zero-terminated
length, exact equality, length-first internal ordering, case-insensitive equality
in 255-unit chunks, user comparison/fallback, legacy character flags, in-place
translation and ANSI convenience import/export. Internal ordering retains native
character signedness; it does not become Unicode or user-locale collation.
User equality still copies both blocks, uppercases them, then compares their
bytes, stopping at the first difference. User collation still accepts only
`-1/0/+1` and falls back to internal ordering on any other provider result.

Native String wrappers retain argument/memory diagnostics and service binding.
The terminated-counted check retains its native count-access diagnostic before
reading the terminator. ANSI conversion retains the original 255-unit capacity
including the terminator, import failure clearing count/first character, export
failure clearing only output byte zero, and wide export's UTF-16LE source marker.
`KauaiText::Services()` exposes the same borrowed native code-page providers used
by the existing C text conversion functions.

`mm/text.h` now also owns the persistent `MMTextCaseMaps` record, identity-array
initialization, upper/lower callback order and map application. The host retains
the synchronized first-use publication guard. Full-buffer case mapping remains
separate from cached byte mapping. Native comparison's empty-count handling,
signed-32-bit count guard and ranked-result conversion also use C; the host
supplies only the Windows locale/flag calls. Portable policy remains distinct.

`engine-string-ops` freezes the original signed-byte primitive corpus, checks
wide unsigned ordering/validation, callback/failure order, chunk boundaries,
comparison fallback, provider/count limits, map initialization/publication data,
translation direction and ANSI conversion success/failure/guard bytes. Native
`string-compat` records primitive and user/encoding results through
`MM_STRING_OPS_SNAPSHOT` and `MM_STRING_USER_SNAPSHOT`. User snapshots depend on
the actual host code page and locale and are compared within that environment.
Native allocation/object diagnostics, raw-view/facade removal, typed ABI/OS
providers and final C-only engine integration remain work.

### Fixed memory and error stacks

`mm/memory.h` owns fixed allocation/resize/free transactions, caller-visible
pointer publication, clear/garbage fill, growth-only failure injection, counters
and liberation retry/reentrancy control. Native allocators supply raw heap,
mutex, diagnostic-header/list and error-reporting providers. A shared C context
holds the liberation guard. Liberation runs unlocked; nested failures suppress
liberation and only positive freed-byte results retry. Resize failures preserve
pointer, data and footer. Physical relocation providers receive an opaque address
token captured before reallocation; the C core never reads a freed pointer value.

`MMMemoryStats` owns all seven counter/failure fields directly, including the
BASE/HQ categories. Native methods retain their existing lock conventions and
delegate arithmetic to C. Windows retains checked 32-bit `long` counter fields
for the public native ABI. Diagnostic arithmetic is defined modulo 32 bits;
native comparisons cover operations without signed overflow. Allocation and
resize statistics use the memory mutex; free retains caller synchronization.

`mm/error_stack.h` owns plain/debug twenty-entry records and count, adjacent
deduplication, oldest-entry eviction, stable flush, reads/pop/clear and reporting/
locking/output sequencing. Source strings are borrowed. Every diagnostic push
reports before locking, including duplicates; overflow reports while still full
and locked. Pop publishes or trashes output before unlocking. Native virtual
dispatch, mutexes and formatted warning services remain providers. Factory views
borrow the owning C records. Native object/counter sizes retain their original
trace values; these are in-memory records, not stored movie structures.

`engine-memory` checks relocating/failing allocators, retry deltas, unlocked
reentrant liberation, size/injection order, failure preservation, clear/garbage
growth, footer handling, pointer/stat publication and default libc services.
`engine-error-stack` checks both layouts, source/line retention, warning/lock
order, duplicate/overflow/flush/pop and underflow publication. Native
`memory-runtime` compares original allocation/error/counter observations through
`MM_MEMORY_RUNTIME_SNAPSHOT`. Fixed-memory diagnostic graphs now use the C service
below; HQ handles also use the C policy described below. Final facade integration
remains conversion work.

### Raw heap and object allocation

`mm/heap.h` owns raw allocation/resize/release, checked header-plus-payload size
arithmetic and logical size queries. Zero-byte requests retain one physical
payload byte; failed resize preserves the allocation, payload and recorded size.
The default uses libc and a fundamental C89 alignment profile. Native hosts pass
the existing `std::max_align_t` header width as a layout constant, retaining native
alignment and header size. C reads/writes the private size field with `memcpy`.
Optional raw providers allow deterministic failure checks. Headers never reach
movie files; a block retains its allocator and layout throughout its lifetime.

`mm/object_memory.h` owns BASE new/delete transaction policy: cleared storage,
failure injection, object-then-memory lock ordering, metadata-link-before-counter
publication, OOM reporting after unlock, unlink/counter/poison/release sequencing
and checked prefix addition. Allocation does not invoke memory liberation or
retry. Native callbacks supply the aligned header profile, source/thread/stack
services and diagnostic dispatch; the C graph below owns metadata and physical
list operations. Counters borrow the existing C state;
free retains the original caller-synchronized counter convention. C callback
lifetimes exclude unwinding; native constructors/destructors run outside them.

`engine-heap` forces allocation/reallocation failures and checks size boundaries,
zero handling, preservation and default libc services. `engine-object-memory`
checks injection countdowns, raw failure, lock/hook/counter order, clearing and
complete-block poisoning before release. Both are strict C89 tests. The headless
`heap-object-runtime` fixture captures native heap alignment, sizes, payloads,
raw and constructed BASE allocations, reference/deletion behavior, diagnostics
and counter observations via `MM_HEAP_OBJECT_SNAPSHOT`. Native/SDL Debug and SDL
Release traces match their respective original implementations. Virtual destruction
and final C-only facade integration remain conversion work.
BASE and fixed-memory diagnostics use the C graphs below. Defined C prefix-
overflow/null-free handling does not establish native equivalence for previously
invalid inputs.

### BASE diagnostic graph

`mm/object_debug.h` owns persistent raw/constructed list heads, source/line/size/
thread/stack metadata, links and sixteen-bit marks in C records. BASE's kind word
also lives in a C state record. Native wrappers supply an aligned header profile
with the existing padding; BASE and derived-object sizes retain baseline values.
The graph owns heads and links, while the allocation transaction owns storage.
Graph and record addresses remain fixed while linked because previous links
point to owning slots. Context initialization requires an empty graph.

C89 now controls link/unlink validation, constructor attachment, object-kind/
reference/flag checks, marking and unmarking, thread filtering and lost-object
scans. Constructor attachment probes the raw head before locking, then refreshes
it after locking; an empty raw list retains the legacy bootstrap behavior for
global constructors. That initial probe requires caller-serialized publication
for concurrent use; this conversion does not establish arbitrary concurrent
construction safety. Recursive locking and native validation order remain.

Lost scans count candidates first, traverse oldest first, and recheck marks
after callbacks, so marking descendants or cycles suppresses later reports.
Allocated mark stubs dispatch only for unmarked records owned by the current
thread; local objects always dispatch. Native hooks provide pointer checks,
diagnostic formatting/breakpoints, thread IDs, native validation and virtual mark calls.
Callbacks may mark descendants but must not unwind, move or delete graph records
during traversal. Constructors and destructors execute outside C callbacks.

`engine-object-debug` checks slot links, raw-head refresh, attachment, recursive
lock depth, source metadata, thread filtering, cyclic marking, reporting order
and readable metadata failures. Defined C mark wrap is checked independently of
native signed-overflow equivalence. The headless `object-graph-runtime` fixture
captures original public-API behavior through `MM_OBJECT_GRAPH_SNAPSHOT`, including
out-of-order raw attachment and failed C++ constructor cleanup. Original traces
match Native/SDL Debug and SDL Release. BaseLinkedList attachment is described
below; virtual destruction and final C-only host integration remain work.

### Fixed-memory diagnostic graph

`mm/memory_debug.h` owns the persistent allocation head, doubly linked records,
source/line/total/thread/stack metadata, marks, magic values and footer policy.
Native wrappers supply the aligned header profile and stack capture; records live
directly in C storage rather than being copied through native read/write hooks.
Creation retains the allocator's cleared or poisoned marks. The graph owns links;
the fixed allocator owns storage. Source strings and the suspend counter are
borrowed, with explicit context initialization and lifetime constraints.

C89 controls link/unlink, resize preparation/publication, header/footer and
claimed-size checks, expensive/suspended validation, marking and mark/report scans.
Scans query the current thread before locking, visit newest first and filter by
allocation owner. Reporting does not mark records; callbacks may mark later
records. Marking has no thread filter. Native hooks supply recursive mutexes,
pointer checks, heap capacity, diagnostics and legacy extra diagnostic groups.
Callbacks cannot unwind or destroy/move traversed records. Storage relocation and
publication require caller serialization with graph mutations.

Resize preparation captures a typed pointer representation before realloc. After
success, C compares address tokens as bytes without reconstructing or dereferencing
an invalidated pointer. Moved records unlink/relink at the head. Successful
unchanged-address resize republishes fresh head/neighbour aliases under the lock
without reordering: even in-place realloc invalidates the old pointer values.
Pointer representations must support byte-token matching for this host profile.
Footer copies handle unaligned addresses; diagnostic headers never reach movies.

`engine-memory-debug` integrates the diagnostic graph with fixed transactions,
forcing moved, unchanged-address and failed resizes. It frees old storage before
repair, poisons external aliases for the unchanged-address case, verifies list
order and metadata/mark retention, and checks suspension, footer/size errors,
thread filtering and release poisoning. The headless `memory-graph-runtime` and
`memory-graph-expensive` fixtures compare original native metadata, diagnostics,
scans and lifetime behavior through `MM_MEMORY_GRAPH_SNAPSHOT`. The latter compiles
only the allocator facade with expensive checks, leaving the shared core unchanged.
Allocator-dependent resize report order is asserted against observed movement,
then sorted for capture comparison. Native/SDL Debug exercise both profiles; SDL
Release disables native diagnostics but runs the C policy tests. BaseLinkedList
attachment is described below; final C-only facade integration remains work.

### HQ handle storage and lifetime

`mm/handle_memory.h` owns HQ allocation, resizing, copying, freeing, size/pointer
queries, lock counts, validation and mark forwarding. Logical size and lock count
live directly in `MMHandleState`; debug allocations add a C magic field. Native
wrappers supply the existing aligned prefix and fixed-memory providers, retaining
the HQ diagnostic counter category, flags, priorities and source metadata. Null
providers use libc, so the C implementation links without Kauai C++ dependencies.
All operations use one C implementation; Release pointer queries now also call it.
Headers remain private runtime storage, never movie records.

Failed allocation reports the HQ error before clearing the public owner. Failed
resize preserves its owner, contents, size and counts; a locked resize reports
without calling the raw provider. Free clears the owner before reporting locked
storage and still releases it, preserving native behavior. Unlock at zero reports
and remains zero. Copy allocates distinct storage without changing source locks.
Expensive validation checks magic first, then the lock range and underlying fixed
allocation. HQ magic checks remain active when fixed pointer checks are suspended.
Callbacks borrow synchronous views and cannot unwind or destroy the operation.
Callers supply synchronization and retain one layout/provider family per lifetime.

`engine-handle-memory` forces allocation/resize failures and moved growth/shrink,
checks raw-request sizes/flags/priorities, counter publication, copy independence,
locked behavior, nil/metadata validation and libc defaults. Defined C overflow
handling and lock-count wrap are separate from native invalid-input equivalence.
The headless `handle-runtime` and `handle-expensive` fixtures compare original
public HQ and opaque `MMHandleServices` behavior through
`MM_HANDLE_RUNTIME_SNAPSHOT`. The latter overrides only the HQ/fixed facades with
expensive validation; Native/SDL Debug exercise both profiles, while SDL Release
disables native diagnostics. The historical Macintosh handle implementation is
not the selected portable backend. BaseLinkedList attachment is described below;
virtual destruction and final C-only host/facade integration remain work.

### Intrusive linked-list storage and attachment

`mm/linked_list.h` owns persistent `MMLinkedListNode` next/previous links and a
borrowed owner token. C implements initialization, unlink-before-insert attachment,
detach and ordered validation. `BaseLinkedList` embeds this record and delegates
construction, attachment, destruction-time detach and validation. Its public
`PbllNext` accessor returns the typed owner. `FileObject` stores a C node head,
using typed accessors for registry lookup, marking, close and shutdown traversal.
Head and internal next slots have one C pointer type; no derived-owner pointer
slots are reinterpreted as base-owner or C-node slots. The extra owner field
changes private runtime object sizes, without affecting stored formats.

Attaching first repairs the old predecessor and successor, then publishes the
new predecessor and head. Repeated detach and moving to the current head are
valid. Callers keep nodes, heads and owners live at fixed addresses while linked,
and supply synchronization. Native file operations retain their existing mutex.
Diagnostic hooks report the original link messages and address/check order; they
must not mutate links or unwind. Readable corrupt links report and continue the
original transaction. Invalid addresses, attaching into one's own next slot and
concurrent unsynchronized mutation are outside the contract.

`engine-linked-list` links only C and checks head/interior/tail insertion, moves
between lists, repeated detach, backlink repair, owner identity, ordered address
and corruption diagnostics, and repeated mixed moves. `linked-list-runtime`
captures stack and reference-counted destruction, list moves and repeated head
attachment through `MM_LINKED_LIST_SNAPSHOT`; it also exercises the real temporary
file registry. `linked-list-expensive` compiles the BASE/list facade with expensive
validation against the existing core. The normal native capture is frozen before
conversion. Verification and retained-trace evidence are recorded in the
[playback regression log](../tools/playback-regression.md).

Virtual destruction and final C-only host/facade integration remain conversion
work. This list extraction does not finish the shared backend migration.

### File-owner state and I/O policy

`mm/file_owner.h` owns persistent flags, error severity, open/ever-open/written
state and the physical handle. C controls permission upgrades and recovery,
close/final temporary-file deletion, flush, seek, size, resize, read/write and
flag/temp mutation. `FileObject` embeds this record and delegates to it in both
hosts. The native facade supplies physical file primitives over its current
Filename, recursive mutexes, validation and error/warning callbacks. No C++ types
or libraries are needed by the C controller. The record's fields replace native
bitfields; this changes private runtime sizes without changing stored formats.

Upgrades close the old handle, retain old permissions, try the new permissions
and restore the old handle on failure. A failed restoration latches critical
error. Validation, recursive locking and diagnostic size queries retain their
original order. Error callbacks observe the state before I/O error escalation;
permission errors observe the completed recovery attempt. Write attempts publish
the written flag before the physical primitive. Read and write error thresholds
remain distinct; seek still runs before a blocked transfer when error severity
permits. Debug reads query size before seeking and report EOF before transferring.
Zero-byte I/O returns without opening or locking even at critical error.

Flush failure remains ignored. Final close deletes an ever-opened temporary
path, including repeated final-close attempts. `set_flags` retains the original
sticky marking behavior; even `ClearMarks` cannot clear that mark through this
helper. This is preserved behavior, not a new policy. Callers keep the state,
provider context and native path live, do not copy an open owner, and serialize
operations. Synchronous callbacks cannot unwind, destroy the owner or mutate it.
Native argument diagnostics remain advisory; C callers supply valid signed
32-bit counts/positions. Debug subtraction uses defined word wrap separately
from native signed-overflow equivalence.

`engine-file-owner` links only C and tests physical failures, permission recovery
and double failure, flags, state publication, error thresholds, nested lock and
validation order, EOF/debug-size failures, temporary deletion, zero-byte I/O and
optional diagnostics/external locking. `file-owner-runtime` captures native
behavior before conversion through `MM_FILE_OWNER_SNAPSHOT`, including real OS
sharing rejection and restored-handle reads. `file-owner-expensive` overrides
the file facades with expensive validation against the shared core. See the
[playback regression log](../tools/playback-regression.md) for evidence.

File/name factory and registry coordination and name transactions are described
next. Filename representation, native primitives, virtual destruction and final
C-only host/facade integration remain conversion work. Historical `filemac.cpp`
is not built by the current Windows/POSIX hosts.

### File factories, registry control and name transactions

`mm/file_ops.h` now controls cached open/reuse, fresh creation, temporary-file
factory routing, registry lookup, mark/zero-reference/shutdown scans and membership
checks. It uses the preceding C owner controller for flags, permission recovery,
opening and closing. Explicit provider contexts bind registries; no C global
registry is hidden in the implementation. `FileObject`'s temporary native binding
still supplies the process registry, object allocation/constructor/destructor
primitives and typed Filename services. Both hosts call the same C policy.

Lookup borrows without retaining. Cached open retains once after flags succeed,
and does not add a file-open error when permission recovery fails. Duplicate
creation diagnoses without adding a create error. New-owner open failure destroys
the partial owner before reporting the factory error. Constructors publish an
initialized owner with one reference and registry attachment. Cleanup saves next
before destroying an unmarked zero-reference owner; shutdown closes without
destroying or unlinking. Sticky mark clearing remains unchanged.

C also owns rename, three-leg name swap, rollback and replacement control.
Physical renames do not mutate the logical name records; copy callbacks publish
names after success or failed recovery. Two-object operations enter a then b and
leave a then b, matching native order. Unique-name failure leaves handles open;
failed recovery publishes surviving physical locations before reopening. Swap
deliberately ignores reopen results and can succeed with critical closed owners.
Rename fails if reopen fails. Replacement swaps with another open owner and marks
that owner temporary only after success. Nonmissing or unknown destination status
attempts deletion, ignores its result and proceeds to rename. These are preserved
native policies, not newly normalized behavior.

Tokens, provider contexts and scratch names stay live; scratch storage cannot
alias an owner/input name. Callback effects are limited to the designated
construct/destroy/copy/physical operations; callbacks cannot unwind or mutate
other owners/links. Locks must be recursive and callers serialize competing
transactions. Native adapters retain entry validation and scratch-object lifetime;
C performs post-transaction validation. Directory checks are advisory and are
skipped when diagnostics are disabled. The native binding is still a C++ facade,
not a completed C-only FileObject implementation.

`engine-file-ops` uses independent pure-C registries, file records and a physical
name/content model. It exercises all 32 failure combinations across the three
swap moves and two recovery legs, cached upgrades and failed construction/open,
temporary naming, rename/reopen failures, replacement and ignored deletion
failures, ordered unlocking and poisoned-owner cleanup. `file-ops-runtime`
captures original public behavior through `MM_FILE_OPS_SNAPSHOT`, including real
rename rejection, content-preserving swaps, factory reuse, registry order and
allocation injection. `file-ops-expensive` overrides the file facades with
expensive validation. See the [regression log](../tools/playback-regression.md).

Native object construction/destruction, default registry binding, Filename/path
representation and services, virtual dispatch and final C-only host/facade
integration remain conversion work. This does not complete the backend migration.

### Filename value, type and leaf rules

`mm/filename_value.h` now owns persistent file-type state and implements clearing,
extension recognition, type derivation/cleaning/change and leaf replacement/set/
extraction. The path view borrows the same 257-unit counted buffer used by the
preceding C String module. `Filename` embeds `MMFilenameState` in place of its
scalar FileType and rebuilds borrowed views after copies. Native runtime width
is unchanged. Native callbacks provide code-page uppercasing, separator syntax,
copy diagnostics, validation and general-error reporting; no SDK/C++ types enter
the C API. The same cleaner now serves FileNameEnumerator's existing filters.

Recognition scans at most five units, including the dot. One-byte units round
trip through signed char; two-byte units accept ASCII and `FF80..FFFF`, preserving
the original cast test. Cleaning visits low bytes first and ignores higher bytes
after a zero. C word arithmetic retains 32-bit values without signed shifts.
Type-change failure preserves the path and type; the historical `>=255` capacity
check retains room for a dot even for an empty extension. Equal types succeed
first. Inner leaf-change overflow leaves the parent directory published, while
public set-leaf clears that intermediate path/type before reporting failure.
Extraction retains the native zero-terminated leaf read. Directory/type/path
diagnostics remain advisory. Scratch extension copies use the copy provider too.

State, buffers, input/output units and diagnostic owner tokens stay live. Inputs
and outputs do not alias the path. Callers serialize mutation; synchronous hooks
cannot unwind or mutate the operation. The native fixed String storage remains a
temporary representation adapter; the C implementation can use ordinary C arrays.
No path canonicalization or filesystem policy is hidden in the value module.

`engine-filename-value` covers both unit profiles, every possible byte/wide
extension unit, zero-byte type cleaning, high-bit words, capacities, diagnostics,
copy hooks and failure publication. `filename-value-runtime` captures original
native values through `MM_FILENAME_VALUE_SNAPSHOT`. `filename-value-expensive`
compiles the Filename facade with expensive validation. Original normal and
expensive algorithms are captured separately: the latter diagnoses existing
noncanonical values such as trailing dots. Messages/stage order remain matched
to their original profile. Baseline reconstruction and evidence are in the
[regression log](../tools/playback-regression.md).

Path construction/search, filesystem dispatch, navigation and unique/temp naming
also use the C controllers described below. Default cache owner/configuration
bindings, native BASE/frame integration, object lifetime and final C-only
host/facade integration remain conversion work. This does not complete the
backend migration.

### Owned filename storage and validation

`mm/filename_owned.h` owns the persistent type and 257-unit counted path in
`MMFilenameOwned8`/`MMFilenameOwned16`. Both profiles coexist in one C library.
The records contain no pointers, allocations or C++ lifetime; rebuild borrowed
views after relocation. Native `Filename` embeds the matching C record and no
longer owns a `String`. It retains BASE inheritance, native object metadata and
the existing API. Its explicit copy constructor/assignment copy BASE metadata
as before, then call the C copy controller. Object size remains unchanged in
the captured hosts.

Initialization clears count/terminator without overwriting the tail. Copy
publishes type before path diagnostics and copies only count+2 units, preserving
unused destination storage. Construction checks source/destination; assignment
checks destination/source/destination, retaining self-copy diagnostics. Export
copies counted units into caller storage. Native dialogs import their selected
path into the C record before deriving its type; physical file opens/deletes
read the same record directly through the facade.

Count, terminator and embedded-null validation also use C. Filename validation
retains allowed-kind checks, nil/name consistency, canonical full-path comparison,
trailing directory separators and leaf-pointer checks, including diagnostic order
and early returns. The native adapter supplies filesystem queries, memory checks,
assertion messages and its existing assertion-depth guard. Invalid wide counts
are diagnosed without scanning outside the supplied buffer. These remain
diagnostics rather than a new repair or canonicalization operation.

`engine-filename-owned` links only C and checks both profiles, relocation,
untouched tails, truncation, copy publication and malformed/failed validation.
Native normal/expensive fixtures capture copied reference counts, object sizes,
paths/types, self-copy diagnostics and errors against the original representation.
Strict C89, C-only cross builds, Linux execution and AddressSanitizer cover this
boundary; exact results and limits are in the
[regression log](../tools/playback-regression.md). Native BASE/object/default
bindings and final facade removal still require conversion. Enumeration's
current/suspended frames now use the plain C records described below.

### Filename path and navigation rules

`mm/filename_path.h` implements path build/search, existence status, delete/
rename dispatch, equality/same-directory, volume forwarding and directory
navigation. It shares the preceding filename value and counted String modules.
Physical canonicalization/search, path equality, filesystem operations, volume
queries and diagnostics remain synchronous native providers. The C controller
uses stack buffers and owns no heap storage or globals; callers supply live,
initialized, distinct owner/scratch views with matching one- or two-byte units.

Build forces directory mode for empty paths or tails consisting only of dots,
and suppresses a default extension when the original input ends in a dot.
Build/search failures clear the value before reporting general failure. Existence
removes one trailing directory separator except for a one-unit root, preserving
the existing Windows drive-root behavior. Missing paths return no without an
error; other lookup errors return maybe. Type mismatch precedes hidden/system
diagnostics. Delete retains its advisory open-file assertion; without an assertion
provider it skips registry lookup. Rename leaves logical names unchanged.

Downward navigation constructs a scratch directory, tries creation on any status
other than yes when requested, and publishes only after success with the move
flag. Failed creation preserves the caller's value. Upward navigation canonicalizes
an appended `..`, requires a shorter path, and optionally returns the removed leaf
and moves the owner. Failure preserves both outputs and adds no error. Scratch
copy hooks preserve native owner metadata along with the path/type; native object
layout is unchanged. Callback lifetimes, serialization and mutation restrictions
are documented in the header.

`engine-filename-path` exercises both unit profiles, provider ordering, capacities,
optional diagnostics/copy hooks, status precedence and failure publication using
only C. `filename-path-runtime` captures native paths relative to a private fixture
root, return values, values observed during errors, diagnostics and error order.
`filename-path-expensive` compiles the Filename facade with expensive validation.
Both profiles are compared with their corresponding pre-conversion captures;
details and retained regression evidence are in the
[regression log](../tools/playback-regression.md).

### Filename unique and temporary naming

`mm/filename_naming.h` owns the shared sequence in `MMFilenameNamingState` and
controls unique/temp naming over the preceding C filename/path/String modules.
Each unique call reads the native clock once and advances a sequence modulo
65,536, preserving the low 16 bits used by legacy `Temp%04x` formatting. It tries
at most twenty candidates, skips the exact previous leaf before applying an
extension, and accepts only a missing result. Existing/maybe results and failed
setters continue the sequence; the final failed iteration increments it once
more before clearing the name and reporting general failure. Existing errors
remain visible. A capacity failure can clear the parent and then recover to a
relative candidate on retry, matching the original code and diagnostics.

Temporary naming initializes a supplied cached-directory view on first provider
success, then copies it and runs the same unique controller with the current
temporary type. Provider failure preserves the caller/cache and reads no clock.
Later environment changes do not invalidate the cache. Unique failure clears
only the caller. Copy hooks preserve native owner metadata; a null hook copies
C type/path values. Neither operation reserves or creates a physical file.

The C entry points have no hidden globals or heap allocation. Callers supply
initialized, live, distinct views, matching character units, shared state/cache
and serialized access. Clock/temp-path primitives use the existing path-provider
context. Native `Filename` supplies one default C sequence and the existing
`_fniTemp` owner; `vftgTemp` remains the public compatibility configuration input.
Those default bindings and native object lifetime remain integration work.

`engine-filename-naming` checks all 65,536 sequence starts in both unit widths,
clock/sequence wrapping, old-leaf comparison, status/error precedence, retry
exhaustion, setter recovery, cache publication/failure and optional hooks using
only C. Native normal/expensive fixtures use the existing clock hook to capture
deterministic pre-conversion sequences, collision exhaustion, capacity recovery,
temp-provider failure, configuration changes and cached environment behavior.
They compare corresponding original profiles; the
[regression log](../tools/playback-regression.md) records evidence and limits.

### Filename legacy case resolution

`mm/filename_case.h` controls explicit legacy content case lookup. Initial native
success returns without rewriting the path/type or classifying directory/hidden/
system attributes. Lookup errors fail quietly. A missing path also fails on an
insensitive host. Sensitive hosts require a leading `/`, prefer each exact
component, then accept exactly one ASCII case match from directory iteration.
Splitting uses `/` literally; non-ASCII units and backslashes remain literal.
Path/type publish only after all components succeed. Every enumeration attempt
closes its iterator, including end/error/null handles, and ambiguity stops after
the second match. Existing/missing/error precedence and error-stack behavior
match the original resolver.

The C controller borrows initialized filename views and String copy/validation
hooks, uses stack buffers and has no heap allocation or globals. Providers supply
info, raw 1024-unit directory names, iterator operations and an optional native
terminated-name length service. That service retains native String diagnostics
for oversized names; the default C scanner returns their full length. Long
entries cannot be truncated into false matches. Matching and copies support
one- and two-byte units. Providers own handles until close, callbacks cannot
unwind or mutate the operation, and callers serialize owner mutation.

`Filename` supplies native providers and the platform sensitivity flag. Native
Windows exact/missing behavior is exercised directly. A controlled runtime
fixture compares the original C++ sensitive-host algorithm, adapted only at its
String/value/validation/provider boundaries, with the C controller. It covers
multiple components, exact preference, ambiguous/no matches, initial/iteration
errors, partial resolution, trailing separators, literal backslashes, non-ASCII
bytes and long entries. The C-only test adds both unit widths, Unicode units,
capacity limits, iterator lifetime and optional hooks. The
[regression log](../tools/playback-regression.md) records source hashes, adapter
details and the lack of native POSIX execution evidence. Enumeration uses the
C controller below. Default bindings, native representation/object lifetime and
final facade integration remain.

### Filename enumeration ownership and traversal

`mm/filename_enum.h` owns filter state, the twenty inline type slots, larger
filter allocations, recursion storage and initialized/recurse/depth flags.
Cursor state is also a C record, containing the iterator, root mask/position and
entry directory/hidden/system/link flags. The C controller implements initialization,
cleanup, enumeration, filtering, root walking, descent, skip and pre/post events.
Its stack uses the existing C collection storage and array modules with five
initial frames and the legacy growth hints. The native DynamicArray stack object
is removed. Fixed-memory allocators retain native filter and collection tags;
the default C entry points can use the C heap.

Filters are copied, then cleaned backwards using the shared C type service.
They gate directory descent too; root entries bypass filters. Hidden/system
entries skip before output copying. Dot directories and filtered entries can
still change the output on a false return, while output flags stay unchanged.
Post events precede closing/popping the child. Skip closes/pops without emitting
that child's post event. Linked directories are emitted with both flags and
are not descended. Recursion allocation failure also emits both flags and reports
general enumeration failure. A failed child wildcard setup restores the parent
frame without closing its iterator. Root-build assertions remain advisory.

State is owning and cannot be copied or moved. Callers provide a fixed current
frame and distinct output/scratch filename views, matching units, raw entry
storage and a byte snapshot format. `mm/filename_frame.h` supplies matching
`MMFilenameFrame8`/`MMFilenameFrame16` records for native and standalone callers.
The current frame and recursion snapshots contain C-owned filename storage,
copy metadata, cursor and entry buffer, with no C++ Filename object or vtable.
Initialization sets nil path, count-one/local metadata and zero cursor/entry;
unused path storage remains untouched. Views are rebuilt after relocation.

Frame metadata preserves legacy value-copy reference counts and diagnostic kinds;
it does not own references or register a debug-graph object. The C copy helper
publishes metadata before type/path and retains prefix-only path copying. Native
bindings expose actual Filename BASE fields when copying to/from external names.
Path validation handles C frame values directly; native BASE validation remains
for native objects. Optional metadata fields can be omitted by a host.

The native enumerator embeds only C state/frame records beneath its BASE facade
and marks allocations directly. Its internal layout changes; Filename layout and
wire formats do not. Native object lifetime and default context/facade integration
remain work. C storage removes the DynamicArray object allocation;
native allocation ordinals/object-allocation error codes are not established by
the regression traces.

Providers supply iterator/root operations, optional terminated-String diagnostics,
fixed-memory allocation and validation/error hooks. They cannot unwind, reenter
or mutate the operation. Callers serialize state/frame mutation. Flags publish
as a native 32-bit word through byte copying, preserving long/int profiles without
reading uninitialized caller storage or delaying publication past callbacks.

`engine-filename-enum` covers both unit widths, ownership/cleanup, filters,
output mutation, roots, links, skip/post ordering, initial/growth/type allocation
failures, failed wildcard rollback and active reinitialization. It now exercises
both public frame profiles, initialization, relocation, optional metadata and
reference-count restoration across recursion. Native normal/
expensive fixtures capture the original physical-tree traversal, output values,
flags, errors and diagnostics; separate frame fixtures track copied and mutated
reference counts through deep recursion, root walking and active reinit.
The [regression log](../tools/playback-regression.md)
records source identities, matched traces and verification limits.

### Windows physical file services

`mm/native_files.h` and `native_files_win.c` implement Windows OS file/path
services in C89. The separate `mm-native-files` target links the native system
libraries and has ANSI and UTF-16 configurations. It is consumed by C-only tests
without Kauai, engine C++ or the C++ runtime. `platform_files_bridge.cpp` forwards
the existing C++ API, translating only options, enums and boolean metadata.
The old Windows C++ implementation is removed. POSIX uses the same interface and
adapter with the C89 `native_files_posix.c` implementation described below.

The C backend retains handle ownership, access/sharing/default-ACL/create modes,
signed 32-bit size/seek/resize limits, exact unsigned 32-bit I/O transfer counts,
read-only/no-clobber rename/delete behavior, missing-parent status classification,
path comparison, native path/search/temp/cwd/executable/leaf queries, recursive
directory creation, roots/volumes/media suppression and directory iteration.
Failed resize can move the Windows file position before failure. Path-buffer/
leaf/error publication follows the original wrappers. Directory entry writes
retain the SDK extent and leave the remaining 1024-unit buffer tail untouched.
Directory creation uses C heap storage and the existing ownerless shell call.
Process cwd/error-mode changes require host coordination.

The standalone platform-services project also consumes this C backend. Separate
ANSI/UTF-16 C-link tests, strict C89 checks including the real Windows SDK, native
original/converted snapshots and AddressSanitizer verify this boundary. Native
traces include ANSI non-ASCII and UTF-16 Greek filenames. The
[regression log](../tools/playback-regression.md) records source identities,
profile hashes, verification scope and remaining native/default facade work.

### POSIX physical file services

The POSIX variant of `mm-native-files` builds `native_files_posix.c` as C90 with
extensions disabled. It owns file descriptors, advisory locks, complete
interrupted/short transfers, path buffers and directory streams. C allocations
replace C++ strings, vectors and directory objects. Both hosts continue through
the same C++ forwarding adapter, which only translates records and results.
The standalone engine and platform projects can link the native C API with a
C-only executable. OS declarations remain behind the implementation: Linux uses
the POSIX 2008 feature level; macOS keeps its SDK's executable-path service.

Lexical normalization preserves byte spelling, trailing separators and symlink
independence. Search retains colon-separated empty components and `PATH`;
temporary paths retain `TMPDIR` and the `/tmp` fallback. Cooperative locks are
acquired before truncation. No-clobber renames retain the link/unlink sequence,
read-only guards, dangling-link behavior and rollback attempt. POSIX resize
does not move the file cursor on failure. Directory owners survive an entry
error and close their stream and path explicitly. Metadata publication retains
the original partial outputs when a symlink's target query fails.

The old POSIX C++ implementation is removed. Strict GCC/Clang C89 checks,
Linux Debug/Release original-versus-converted snapshots, C-only execution and
the standalone platform gates verify this boundary. The
[regression log](../tools/playback-regression.md) records exact evidence. These
checks do not establish macOS runtime behavior or complete the remaining
native object/default-context integration and whole-engine conversion.

### Collection storage foundation

`mm/collection_storage.h` owns the two buffers underneath `GroupBase`,
covering arrays, groups, string tables and the chunky-file live index. It handles
allocation lifetime, growth hints, nested pointer locks, duplication and ordered
header/first-section/second-section transfers. `GroupBase` no longer stores HQ
handles. Its adapters pass plain buffers, allocator callbacks and bounded
`DataBlock` read/write callbacks into C89.

The standalone C default uses the C heap. Existing hosts instead supply the
fixed-memory allocator to retain memory-liberation callbacks, request priorities,
debug poisoning, allocation failure injection and the `dmaglHq` diagnostic
category. Fixed-memory transactions and counters now use the C services described
above. Native debug representations and final allocator integration remain
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

`mm/collections.h` and `collections.c` implement dense/allocated arrays
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
`mm_strings_wire_system` retains the requested Mac/Windows family at the
host string width, including requests with a different Unicode width.

`mm/data_stream.h` and `data_stream.c` own memory-stream storage lifetime,
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

`mm/codec.h` and `codec.c` implement both formats without Kauai or C++
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

`mm_packed_encode` shares the eight-byte big-endian envelope, codec
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

`mm/piece_table.h` and `piece_table.c` own FileByteStream's piece storage
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

`mm/block_io.h` and `block_io.c` own DataBlock byte/HQ I/O routing,
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

`mm/block_owner.h` and `block_owner.c` now hold DataBlock's owning state.
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
legacy string translation and GST output. Runtime extras use MMRollCallActor;
wire extras remain 28 bytes. Opaque tag references are transferred, never retained
or closed by table mutation/destruction. Movie transactions and existing edit
rollback retain responsibility for those references.

Reading reuses the legacy dense GST header rules rather than Archive's stricter
validator. Same-width legacy maps stay in C; width-changing text uses the host
converter. Failed reads may leave partial loading state and must be discarded.
Failed writes restore native offsets and release locks. Mutation failures preserve
logical rows while allowing successful partial capacity growth.

The C movie state embeds the table and owns its lifetime. Studio's roll-call
edits, autosave, and movie-merge rollback use that table directly. Native
services provide file I/O, text conversion, tag operations, and allocation
diagnostics for its buffers.

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
ownership is described next; event marshalling/playback/editing and final facade
removal remain work.

`engine-scene-data-test` covers both byte orders, signed frame limits, event-size
and sound-count rules, metadata read/write failures, six replacement failure stages,
creation allocation failures and cleanup order. The existing native scene check
still exercises actual archive allocation failures and retained old child links.
Thirty-four standalone C checks, forty-seven focused native checks, MSVC
AddressSanitizer, seventeen binary snapshots and all 51 CLI baselines pass. The
new scene/name/header/event snapshot is 526 bytes. No Marcus UI tests were added.

### Scene ownership and lifecycle

`scene_owner.c` owns the frame and start event tracks and manages references to
the background, thumbnail and background sound. It also owns the C collection
storage for the actor roster, caption roster and extra selection. Scene
construction and load start with empty rosters. Start-event dispatch fills
them as it applies each event and removes the new roster entry if its seek
fails. Actor and caption edits, chopping, selection, and sound resolution
read those same rows. Their entries borrow objects held by start events.
Studio enumerates actors through indexed scene access.

The parent movie remains borrowed. Native pointer fields for the remaining
objects are borrowed views. Creation, loading and edit replacement sites
transfer and release references through the C owner. SceneData supplies
metadata, and the C runtime described below owns the event cursor and effect
mask.

C controls construction order, cursor-reset timing, optional loaded notification,
partial factory teardown, forward start-object release, frame sound-tag cleanup,
container/background/thumbnail/sound release order, reverse completed-prefix read
cleanup and light/actor/text hiding before close. Closing a retained scene clears
the caller's handle without destroying its resources. Actor/text roll calls and
selection entries are borrowed; their objects belong to start events. Owned
background/undo sound packets close only opened tags, while embedded frame packets
close every tag and never free the group's payload separately.

`scene_track.c` stores the live headers and variable payloads in C collections
with the legacy ordering and buffer growth. Event reads, mutations and sound
resolution use this storage. The scene creator and save transaction write
encoded event tracks with the original group header and location table.
Typed adapters provide the native actor, caption, background and tag APIs.
The loader's uncommitted objects follow the event read transaction.

Reverse read cleanup now compares sound size with variable payload bytes, fixing
the original extra fixed-header-byte comparison that rejected valid packets and
skipped closing their tags. Malformed payloads are diagnosed before unsafe tag/
object reads; a text lifetime beginning at signed MIN diagnoses failure instead
of subtracting one with overflow. Other valid lifecycle/release behavior remains
frozen. Services cannot reenter lifecycle or replace the same scene's owners.

Original/repeated native and original/converted SDL debug traces match 7,888 bytes,
SHA-256 `820411D4385C1BE0A624D4A76AD2384887E0A974C4F29786B354F39057D8F2C8`,
FNV-1a `0EE1BC65`. The SDL release before/after trace matches 8,688 bytes,
SHA-256 `12CCD982D3A4E714550999609A5CD9AC97A167FB692DCD8BD3D6E1A59450B6E2`,
FNV-1a `2A12D054`; release omits allocation injection. Fixtures freeze construction
failure/notification behavior, defaults, group layouts, retained closes, owned
actor destruction order and tag-cache reference balance. Actor destruction probes
are deliberately unstaged partial actors, with validity assertions suspended only
around their teardown. Actual actor/text/light behavior is additionally covered
by C callback tests, existing integration tests and playback gates. Supplemental
native tests exercise completed frame-packet cleanup through the real service
bridge. Full validation and coverage limits are in `docs/tools/playback-regression.md`.

After moving the rosters, the current Release ownership and load traces still
match their frozen references. Debug ownership and load traces match all
fault-free cases and every case that succeeds in both builds. Removing the
empty native arrays shifts four ownership and eight load fault-injection
cases. The dispatch, content and edit traces match the immediately previous
build byte for byte.

Moving the event tracks into the owner removes two native allocations during
scene creation and load. Saving also drops the two temporary native group
allocations. The x64 Release scene traces and the Debug event, content, edit,
runtime and sound-edit traces match their previous reference.
The Debug traces change in four ownership, eight load and eight store
allocation failure cases. Cases that succeed in both builds produce identical
records.

### Scene frame movement, replay and prerendering

`scene_runtime.c` owns persistent event-cursor/effect-mask/background-sound-frame
state, forward/backward frame sequencing, same-frame replay, actor/text forcing,
range extension and two-pass actor prerender decisions. The native Scene stores
this C record directly, and remaining native editing/dispatch code accesses it
without a shadow cursor or effect mask. SceneData supplies the live C metadata.

Backward seeks unplay a completed prefix before replaying destination events in
reverse order. Forward seeks suppress sounds and pauses for intervening frames;
replay always suppresses camera changes. Failed play/unplay callbacks leave the
cursor at the failed event and report the existing goto error. Actor/text seek
and replay failures return without that extra error or view invalidation.
Callbacks observe metadata and completed cursor changes immediately. Object
counts remain dynamic; event groups must not be mutated/reentered during event
dispatch. Optional actor sound outputs preserve incoming values and changes on
failure across the native bool/C int bridge.

Prerendering retains both render-need queries per actor, previous-prerender flag
handling and repeated show operations. Changing actors are hidden around the
snapshot, then shown; static actors become hidden/prerendered. End-prerender
always restores the World background before showing previously hidden actors.
Native services provide collection access, typed actor/caption calls, World
snapshot operations and movie/client notifications. Single-event play/unplay
policy now uses the C dispatcher below; Scene still has native dependencies.

Original/repeated native and original/converted SDL release traces match 5,664
bytes, SHA-256 `1F686B330EB64C1D2A56290EF99AFDD9E170DA322507951C268ECCD353B58F7C`,
FNV-1a `3434DECD`. The real Scene fixture covers frame/replay notifications and
background MIDI cache state with empty actor/text lists. C-only tests cover
nonempty lists, callback failures, signed frame extremes, dynamic counts,
camera-event range movement and two-pass prerender ordering. Existing integration
tests and retail/host captures cover actual actors/captions. Full validation and
comparison limits are recorded in `docs/tools/playback-regression.md`.

### Scene event dispatch

`scene_events.c` owns single-event play/unplay and start-event dispatch. It uses
the existing C owner and runtime records directly. MIDI sound events duplicate
their background cache before disposing the previous owner, even when sounds
are disabled. Cache duplication failure preserves the old owner/frame; clearing
the cache during rewind leaves its old frame value untouched. Pause suppression
still notifies the client before deciding whether to read/apply the delay.

Background events fetch a new reference, set camera zero, supply a cut transition
when none exists, adopt the reference and publish the weak tag in that order.
Failed camera setup releases the fetched reference without changing ownership or
transition. Tags preserve all native fields through the C view, including unused
cache fields on non-cache source tags. Actor/text events append borrowed objects
then seek them; failed seeks remove the appended entry, retaining an actor's scene
binding. Object ownership continues to belong to start events, not the roll call.

Rewind searches strictly before the current frame. It restores the first earlier
sound event of any style, ignoring replay failure; previous-camera failure is
diagnosed but returns success. Camera zero is used when no earlier camera exists,
and failure of that fallback returns failure. Camera suppression still checks
that a background exists. Start dispatch preserves a completed prefix on failure;
actors-only dispatch skips non-actor payload access. Callbacks are synchronous
and may not reenter dispatch or mutate its event groups.

The native Scene adapters supply typed payload access, collection/reference
primitives, actor/caption calls, background resources and movie/client effects.
The published weak background TAG remains native Scene state. SceneSoundEvent
packet helpers and sound editing/undo now use the C implementations below.
Loading now uses the C transaction below. Caption/background implementations and
final facade removal remain. This is
not a C-only Scene implementation. Native debug and release traces freeze the
real pause/sound dispatch path and MIDI allocation failures; C-only tests cover
all event kinds, callback failure phases, ownership/publication order, start
prefixes and rewind exceptions. See `docs/tools/playback-regression.md` for
validation and retail comparison limits.

### Scene sound packets

`scene_sound.c` owns sound-packet allocation/construction, full and group
duplication, tag append ownership, byte-count validation, wire decoding/encoding
and per-tag playback. `SceneSoundEvent` inherits the 16-byte C metadata record
without duplicate fields. Its native tag/child tail is an adapter view, 32 bytes
per entry on x64; wire records stay 20 bytes. The temporary native facade is in
`inc/scene_sound.h`; the portable C API header is
`libraries/engine/include/mm/scene_sound.h`.

Construction and full duplication retain every tag. Group duplication retains
only cache-source tags or clears all cache pointers, reacquiring group data
after allocation. Append retains only the new tag and transfers the old prefix;
the caller frees the old allocation without closing that prefix. The former
unchecked group-copy allocation failure now returns NULL to existing callers
that already handle failure. Signed/size arithmetic and packet spans are checked
before unsafe copies. Invalid/truncated decode input is rejected before mutation;
valid swapped input is normalized before allocation even if allocation fails.
Decode leaves caches unopened/NULL. Encode writes native order, preserves the
full four-byte loop value and zeros the reserved wire cache word. Trailing bytes
remain accepted. The unused native runtime-tag byte-swap method was removed;
active byte swapping uses the fixed wire layout instead of native pointer size.

Playback resolves cache-source tags, skips resolution failures, stops on a failed
fetch, ignores enqueue failure and releases every fetched reference. Queue and
start offset use the original index even after skipped tags. Tool selection is
captured on entry; the other header fields remain live. Typed adapters provide
allocation, tag copy/reference/resolve/fetch, group access, priority and queue
primitives. `Movie::FResolveSndTag` already delegates resolution policy to
`mm_movie_sound_resolve`; its native facade remains an integration dependency.
Read/open and tag-collection/save transactions now use the C layers below.
Weak background tag state, native tag representation and final
facade removal remain conversion work.

Native traces freeze field values, reference counts, wire bytes and allocation
failures without freezing uninitialized native padding. C-only tests additionally
cover group relocation, bounded malformed inputs, unaligned/opposite-order
decoding, failed group allocation, ownership transfer, skipped-tag queue/offset
behavior and resource cleanup. Full gates and retail comparison limits are in
`docs/tools/playback-regression.md`.

### Scene sound editing and undo

`scene_sound_edit.c` owns current-frame sound search, insert/replace/queue/remove,
query, volume, preview, background offsets/playback and sound-frame indicators.
It also owns sound-undo packet/style state and swap control. The native
`SceneUndoSound` facade is declared in `inc/scene_sound_undo.h`; common MovieUndo
targets/navigation, undo-list/object lifetime, typed groups and resource/queue
operations remain host services. This completes a policy slice, not a C-only
Scene or undo system.

Insertion marks dirty and advances the event cursor before insertion, rolling
the cursor back on failure. Searches stop at the current-frame boundary. Queue
append transfers the old packet prefix and retains only the appended tag. Failed
group replacement releases the temporary prefix exactly once, preserving the
group's references. Remove-with-undo now handles an older-frame prefix and cleans
temporary state on failure. Failed undo capture releases its temporary packet;
undo clear may destroy its object, so C accesses no state afterward. These are
bounded corrections to unsafe native failure paths, covered separately from
the frozen valid-operation trace.

Preview continues after failed fetches, whereas packet playback aborts. Input
indices still determine queue position. Queued preview uses requested volume and
loop values while the stored packet retains its prior metadata. Query returns
borrowed tag copies and publishes list/metadata before releasing its temporary
packet; missing/failing queries leave volume/loop untouched. List-resize failure
cleans both temporary owners. Background offsets preserve signed 32-bit frame
subtraction and the original 1000/6 clock with C89 quotient/remainder arithmetic;
overflow is diagnosed before returning the low unsigned word. Volume edits leave
the cached background packet unchanged, and indicator scans ignore suppression.

Native debug poisoning of unused built-in child IDs is retained; release now
initializes that unused word to zero. Unsupported queued restore rejects the
formerly uninitialized-packet path. Neither affects valid serialized sound data.
Native traces freeze sound edits, allocation failures, references and repeated
undo swaps; C-only tests cover callback ordering, relocation, ownership failures,
query publication, clock boundaries and object destruction during undo clear.
Strict C89, sanitizer, host and retail capture evidence is recorded in
`docs/tools/playback-regression.md`.

### Scene loading and sound resolution

`scene_load.c` owns the load transaction formerly in `Scene::PscenRead`: metadata
read before construction, actor/text list allocation, metadata publication,
optional thumbnail loading, frame/start group construction and the final name
reload. It uses the C scene owner and sound packet policies. Typed file lookup,
validated group reads, actor/caption decoding, tag opens and representation
conversion remain synchronous host primitives.

Fixed headers, actor/caption child IDs and background tags decode into local
values. Background tags start with a NULL cache. Camera and pause variable bytes
pass through unchanged even in opposite-order groups, matching the native loader.
Sound wire decoding normalizes its private payload before allocation. Each tag
opens before insertion transfers the packet's references into the runtime group.
Failed opening/insertion closes the newly opened prefix in reverse order and
frees the packet before releasing the wire group. Failure then unwinds completed
start/frame prefixes using the existing C owner cleanup and destroys the scene.
Thumbnail lookup/decode failures remain optional; final name loading has no
failure result. Validated input groups and metadata remain required by this
internal service contract.

`mm_scene_resolve_sounds` captures the actor count, resolves actors first,
then locks frame storage, captures its event count and resolves sid-zero sound
tags in order. Failure retains previously resolved tags. The former native
frame-resolution failure left its group locked; every acquired lock now releases
on success or failure. Native supplemental checks and C callback tests cover
that correction separately from the frozen load trace.

The native load trace covers both byte orders, real caption decoding, absent
groups/children, malformed sound data and allocation faults. C-only checks cover
every fallible transaction callback, ownership transfer, reverse cleanup,
optional thumbnails, actor/text loads, preserved raw payloads and resolution
prefix/lock behavior. Full host gates and retail capture limits are recorded in
`docs/tools/playback-regression.md`. Tag collection/save now uses the C layer
below. Other editing, caption/background policy and final C-only integration
remain conversion work.

### Scene tag collection and save transactions

`scene_store.c` owns the traversal formerly in `Scene::FAddTagsToTagl` and the
save transaction formerly in `Scene::FWrite`. Collection reads starts before
frames, gathers actor tag lists, invokes background tag collection, remembers
the last successful background for camera children and decodes sound packets
without opening their tags. Caption starts and pauses are ignored. Unexpected
kinds diagnose/skip. Failure preserves the destination tag-list prefix, releasing
the current actor list or sound allocation before the source wire group. Invalid
fixed-entry widths and wrong fixed-size child/background/camera payloads now
fail safely instead of reaching the native unchecked copy paths. Sound decoding
retains its established trailing-byte policy.

Saving publishes nil before allocating the new scene, preserving the allocator's
output even on failure. Each sound is independently duplicated with references;
user sounds are adopted only when their child is absent and their chunk number
exists in the destination. Existing child bindings and absent sound chunks remain
accepted. Packet bytes are encoded/inserted before releasing the duplicate.
Actor/caption child counters are independent, incrementing after insertion.
Background tags save in place and their wire cache words are zero. Such tag
changes survive later failure. Event groups write before thumbnail generation;
the thumbnail is reread from C owner state after that callback. A failed thumbnail
write/adoption deletes its unadopted chunk before deleting the partial scene.
Success releases frame/start temporary groups; failure deletes the scene then
releases start/frame groups. Existing C SceneData policy supplies event-child and
header/name persistence.

Native adapters supply typed groups, chunk operations, actor tag discovery and
serialization, caption serialization, background tag collection and thumbnail
generation. These remain explicit native integration dependencies; this is not a
C-only Scene implementation. Native traces freeze both wire byte orders, partial
tag lists and child entries, sound adoption, real captions and allocation faults.
C-only tests cover callback failure phases, retained references, independent
child counters, mutable backgrounds, thumbnail replacement/cleanup, output
publication, partial collection, null/error actor lists and malformed spans.
Strict C89, sanitizer, host and retail evidence is in
`docs/tools/playback-regression.md`.

### Scene pause, camera and background editing/undo

`scene_edit.c` owns pause swaps, current/future camera coalescing, background
replacement, the empty-scene predicate, public edit/undo transactions and
persistent pause/background undo payload/swap control. `inc/scene_edit_undo.h`
holds the temporary native undo facades; common MovieUndo targets/navigation,
undo-list/object lifetime, typed groups and background resource operations remain
host services. Background play and camera effects reuse the C event dispatcher.

Pause replacement/removal preserves the native dirty behavior. Nil without an
existing pause still inserts an event. Camera edits publish the old value before
put/play callbacks, retain destructive prefixes after failed replacement/play,
remove redundant future cameras without changing the current cursor, and keep
the legacy public undo-failure retry of the requested camera. The empty-scene
predicate uses defined signed-word frame subtraction and accepts only camera/
sound frame events with one start event.

Background replacement publishes the old tag before group mutation, turns off
lights and drops the old owner before fetching its replacement. Failed play
attempts to restore the event and background, retaining dirty state and any
failed-restoration prefix. Name assignment, stale frame removal, cursor reset,
client scene-change and optional loaded notification preserve their order.
First-background output is invalid/unowned; native debug poisoning is retained,
while release initializes that formerly unused output. Its unused start frame is
now zero. Public background edits require an existing background, matching the
native debug restriction; core creates the first background. Background undo
without a current background fails instead of consuming an uninitialized camera.

Successful background changes now close frame-owned sound tags before deleting
stale events; the cached background-sound owner remains intact. Camera preflight
tags now close after caching and before editing. Native supplemental reference
checks and C-only ownership tests cover both corrections outside the frozen
behavior trace. Undo flushes before clearing on failure, and no state is accessed
after clear, which may destroy the undo object. Background undo switches scenes
without seeking; camera/pause undo seek their saved frame. Saved tags remain
borrowed, as in the original undo objects.

The native trace uses real background/light/camera/RGB/depth resources, pause and
camera edits, public background edits, repeated undo swaps and allocation faults.
C-only checks cover failed play/restoration, callback publication, masks,
notification suppression, transaction failure phases, partial state and immediate
undo destruction. Host/retail evidence and limits are in
`docs/tools/playback-regression.md`. Actor/caption/selection editing, other Scene/
Movie operations, background resource policy, thumbnail generation and final
C-only integration remain conversion work.

### Scene actor and caption editing/undo

`scene_content.c` owns Scene-level actor/caption add/remove, actor on-stage/paste
transactions, actor lookup, caption indexing, bulk Movie roll-call traversal,
movie rebinding and persistent actor/caption undo ownership/control. Start groups
own object references; actor/caption and selection lists borrow them. Typed
adapters supply group primitives, actor/caption internals, generic selection/UI
effects and common MovieUndo navigation/list/object lifetime. New start events
write zero to their unused frame field.

Actor replacement retains/binds/seeks before publishing both pointers, selects
before hiding/releasing the old actor, then invalidates range, dirties and updates
the sound indicator. New actors insert before retaining/registering/dispatching;
failures retain earlier actor ID/binding changes while removing new group/list/
roll-call references. An actor whose body allocation failed now rejects before
commit. Removal clears primary placement and matching selection extras before
Movie roll-call removal, hide and release. Replacement also removes old-ID extras
so headless selection cannot retain stale actor pointers. Bulk registration rolls
back its completed prefix in reverse order using actor names; a new actor uses
its template name. Paste retains the extra range/seek/update ordering.

Caption binding precedes insertion. Failed caption dispatch now releases its new
reference. Removal clears selection before searching, erases the owning/borrowed
entries and seeks before the lifetime start to remove display remnants. Public
add/remove retain their different commit-failure behavior; a successful edit is
not rolled back merely because adding its undo record fails. Invalid lifetime or
missing-object paths now fail with bounded cleanup. Caption lookup is bounded;
undo rebinds by index after scene changes and keeps the legacy asymmetric seek
restoration and nonfatal final seek after adding a caption.

Actor undo add records the ID assigned by roll-call insertion; replacement swaps
an independently duplicated current actor and releases it if installation fails.
Actor remove commit failure now releases its undo record. Caption undo no longer
releases a borrowed index lookup on failed removal. Owned undo payloads release
before publishing NULL; failure flushes before clear and touches no state after
clear, which may destroy the object. Movie rebinding updates the authoritative C
owner before publishing native views, fixing the previous stale-owner reversion.

Native traces cover prepared real actor bodies/templates, replacement and add
undo cycles, real hidden captions, reference counts and container fault settings.
Native supplemental checks cover failed body allocation, failed caption playback,
stale selection extras and movie rebinding outside the frozen trace. C-only
fault/ownership tests cover the remaining protocol branches, including caption
undo navigation and identity rebinding. Full gates and retail limits are recorded
in `docs/tools/playback-regression.md`. Selection and chopping are covered below;
actor/caption internals, other editing, other Scene/Movie operations and final
C-only integration remain.

### Scene selection

`scene_selection.c` owns actor/caption primary selection state and selection
control: plain select/clear, count/index/membership, toggle with primary
promotion, select-all, name-tag queries and edits, shared-tag selection and
visible centroid arithmetic. Selection lists and primary pointers borrow object
references. Native services provide counted display-name access, body/view
queries, highlight/caption effects and Movie menu/client notifications. Native
typed primary fields are published views of the C state.

Studio's pointer selection uses the same C controller. On press it preserves
the hit order, body-part index, default-tool behavior and existing group when
clicking a member. A plain pick of a tagged actor recalls its named group.
Shift holds a reference to the pressed actor and waits for release within three
pixels on each axis before toggling. Movement beyond that tolerance preserves
the group for a selected member. For a live nonmember, it selects that actor
and captures single-actor undo before applying a transform. This restores undo
for retargeted rotate, resize and squash gestures. A deleted target or a
different scene cancels the pending selection. Studio supplies hit, click,
cursor and redraw effects.

Plain actor selection unhighlights/clears extras and deselects a caption only
with an actor-mode view. No-view and text-mode calls retain extras, including
their membership when a null primary makes the count zero. Toggling searches
extras before installing a missing primary; removing a primary promotes the
first extra without highlighting it again. Allocation or append failure retains
the completed prefix and newly allocated empty storage. Caption selection keeps
the actor pointer, preserves invalidate/menu/highlight ordering and may return
early after clearing extras without publishing/notifying the caption again.

Tag matching retains ASCII folding/token boundaries, whole-token suffix checks,
counted byte/UTF-16 query units and the native shared-seed 63-unit truncation.
Direct queries retain acceptance of non-ASCII and punctuation units. Select-all
and tag operations retain partial selections on allocation failure. Centroids
skip null/missing-body/offscreen actors and preserve selection-order 16.16 sum
wrapping and division; no visible selection leaves outputs untouched. Invalid
selection indices now return null from the C API; native debug range assertions
remain.

The group-tag commands read display names through the same counted native units
as tag selection. `Ctrl+G` scans the current scene for numeric tokens and appends
the decimal successor to each selected name. Decimal comparison and carry
support tags beyond machine integer range, subject to the 255-unit name limit.
`Ctrl+Shift+G` removes one trailing tag and one preceding space when present.
C89 saves each old name before applying its replacement and registers a single
rename undo. An edit failure keeps earlier renames and clears undo. A failed
registration still marks the movie dirty.

The native headless trace extends the Scene-content fixture with selection
transitions, names and centroid outputs. C-only checks exercise all view modes,
effect/notification ordering, caption early returns, allocation/append failures,
tag boundaries/truncation/text units, group-tag failure ordering and centroid
rounding/overflow. The 31,048-byte Scene-content trace and the 12 native actor
traces matched their baselines. SDL Studio verified tagging, numeric group recall,
stripping, undo, redo and save/reopen for seven actors. A separate Studio run
verified a 31-digit successor and undo/redo for eight actors. Retail reopened the
seven-actor movie with matching IDs, actions, cels and positions at frame 7.
Both stock actors' tag names survived a save in retail and another open in SDL
Studio. After a retail save, the custom-material fixture produced the same
missing `MTRL:2` error on both Studio builds.

The pointer tests cover body-part precedence, the inclusive three-pixel
boundary, target references, changing scenes, removal and undo allocation
failure. Fourteen routed Studio workflows had matching selections and edited
poses. The three corrected Shift-drags undid and redid the intended actor's
edit. Their post-redo saves matched the previous build's equivalent plain
drags with working undo. All six direct saves matched the previous build's 72
chunk entries, links and payloads per movie. An initial post-redo move save
showed different actor and scene end-frame caches. Repeating that move on the
previous build reproduced both cache values, and the repeated save matched
the new build. Retail reopened all six edited movies with matching actor poses
and positions at frame 7. Removing the armed target during a Studio press
preserved the other actor's selection and pose.

Fresh SDL and Marcus seek and playback runs (`20261120T033629Z-98dd97ea`)
matched all 60 scene and frame markers from the retained retail capture in
each mode. Each port's seek images matched its previous build. Camera indices
and all 12 SDL playback sound events matched retail.

Capture suite `20261120T014251Z-6d409d24` matched 60 scene/frame markers from
retail in each port's seek and playback runs. Both ports' seek image hashes
matched their previous builds, and SDL's 12 playback sound events matched
retail.

### Scene chop and snapshot undo

`scene_chop.c` owns forward/backward actor/caption traversal, Scene-event pruning,
frame limits and cached-background-sound cleanup. Forward pruning starts at the
current cursor; backward pruning moves the most recent camera to the current
frame and retains all current-frame events before recomputing the cursor. Caption
forward trim removes future captions or extends an overlapping caption to
infinity; backward trim removes expired captions or changes their start frame.
Typed services implement actor trim, caption lifetime/display effects, container
primitives and dirty notifications. Actor/caption removals use their C policies.

Removed sound-event tags now close before erasing group storage. Failed caption
lifetime queries diagnose/skip instead of using uninitialized endpoints; failed
removals that leave the object in place advance instead of looping indefinitely.
These corrections are separate from the frozen successful native trace. The
background-frame discard retains native debug poison/release no-op behavior.

The public chop transaction saves/commits a snapshot undo before editing and
releases its temporary undo reference before core. Persistent snapshot state owns
the resource cache and chunk number in C. Save retains the cache on a failed
write for later disposal. Do and undo share the C swap: read/play/switch, retain
the old Scene, insert the restored Scene, close its temporary reference, delete
the old snapshot, rewind/write the previous Scene, close it and remove its old
list entry. Failed writes/removals preserve the native destructive prefixes;
failed final switch/seek remain nonfatal. Close and release stay distinct typed
operations. Failure flushes before clearing undo and ends the long operation
through copied Movie context without accessing the possibly destroyed record.

Native evidence covers real actor trims, camera pruning, public transactions and
repeated actual empty-Scene snapshot save/swap. The synthetic embedded-background
replay and visible-caption paths need more than the headless fixture; C-only
tests cover snapshot failure phases and caption protocol/effect order. Native
supplemental checks verify sound-tag resource-reference balance in both chop
directions. No Marcus UI tests were added. Scene property/playback control is
covered below. Scene thumbnails/dirty propagation, other Movie operations and
final C-only integration remain conversion work.

### Scene property editing and playback lifecycle

`scene_control.c` owns Scene name/transition undo transactions and persistent C
title/transition undo payloads. The old value is captured before committing undo;
the local undo reference releases before publishing new metadata. Both name and
transition commit failures now release that reference. Transition editing now
stores the old transition instead of leaving the undo payload at its allocation
default. Core setters retain their existing absence of extra dirty/menu/client
effects. Name copies preserve counted native byte/UTF-16 units without rewriting
text.

Name undo switches Scene without seeking; transition undo switches and seeks the
saved frame. Both resolve current Scene state after navigation, swap C payloads
and flush. Failure flushes before clearing undo, which may destroy the payload;
no state access follows clear. `scene_data.c` now also controls transition-file
read/modify/write, preserving full metadata/name loading and header-before-name
publication, including partial write failures. Native adapters supply typed
String/enum/file conversions and common undo/navigation/list effects.

Playback startup refreshes actor lifetimes in list order and stops at the first
failure. Captions scroll when they are visible story text or nonstory text whose
lifetime starts at the current frame, then startup forces prerendering. Missing
caption views diagnose/skip instead of dereferencing null. Shutdown ends
prerendering before cleaning caption views. Typed services retain actor/caption
internals, view scrolling/cleanup and the existing C prerender entry points.

The native trace covers explicit name/transition payload swaps, public edits,
transition-file round trips and playback lifecycle calls with real actors.
Supplemental native checks verify corrected public transition undo/redo. C-only
tests cover commit ownership, destructive failure cleanup, counted names, caption
startup branches, missing views, shutdown ordering and endian/partial file writes.
Scene thumbnails/dirty propagation are covered below. Other Movie operations and
final C-only integration remain conversion work. No Marcus UI tests were added.

### Scene thumbnail generation and dirty propagation

The saved-thumbnail path resolves the slot-zero THUM link through the shared
chunk graph and reads the decoded payload through a C-owned block. Studio loads
it into the C bitmap record held by its native resource object. Marcus uses the
same decoder and rectangular indexed blit, then expands the pixels to opaque
RGBA with the saved-thumbnail palette. Missing palettes use the active preview
palette. The stored-size and dimension limits stay in the preview path.
The saved-thumbnail comparison matches metadata and RGBA byte for byte across
2,289 images. The rectangular blit also matches the previous renderer across
13,440 combinations of source rows, placement, clipping and mask flags. The
comparison and sanitizer logs are in `tmp/c89-scene-thumbnails`.

`scene_thumbnail.c` owns thumbnail update/cache access and dirty propagation.
It captures selected primary objects and sound time, mutes sound, checks for
background/view/dirty state, creates indexed source/thumbnail ports, sets the
thumbnail palette and disables sound/pause effects. It seeks to the first frame,
deselects through the existing C selection policy, draws, restores the primary
selection and scales the image to 144 by 81 pixels. The old owner releases before
bitmap construction; even a failed construction publishes the empty owner before
unlock. The accessor returns a borrowed thumbnail after attempting the update.

Typed services retain offscreen-port creation, MovieView drawing, palette setup,
scaling, bitmap construction and pixel locking. GraphicsEnvironment objects use
stack placement storage in the native bridge, retaining/locking ports through
local port release, frame/mask restoration and reverse-order environment cleanup.
The C policy explicitly delimits that lifetime without introducing heap
allocations for graphics environments.

All exits restore sound at the saved time. Initial-seek failure releases ports
and restores masks while retaining the failed seek's partial frame change. After
rendering, a saved frame restores only in `(first,last]`; restoration failure is
nonfatal. Selection mode is queried separately for deselect and restore, keeping
native primary-only restoration and existing extra-selection effects. Dirty
propagation ignores false and detached Movie owners; attached Movies use the
existing SetDirty primitive.

Native/SDL traces include two actual rendered/decoded thumbnail images, reference
counts across replacement, clean-cache reuse, frame/effect-mask restoration and
dirty marking through an offscreen engine MovieView. C-only tests cover every
gate/allocation/seek/bitmap failure branch, selection mode changes, publication/
unlock order, graphics-environment references and sound restoration. No Marcus UI
tests were added. Scene query/group gates and the bridge audit are covered below.
Background resource controllers are covered below. Other Movie operations and
final C-only integration remain conversion work.

### Scene queries and loaded-group acceptance

`scene_events.c` now owns the first-background-start lookup and click-to-actor
traversal. Background lookup borrows the tag/cache, stops at the first match and
leaves output untouched on absence, even when a live background object exists.
It performs no fetch/retain/release/playback. Picking calls the existing world
backend first, preserving its body-part output. A miss skips actor traversal and
diagnostics; a hit returns the first actor owning that body. Orphaned hits retain
the original diagnostic/null result without restoring the part output or changing
selection. Native services supply typed world picking and body ownership queries.

`scene_data.c` now owns loaded-group acceptance and failure release. Collection
headers/locations are already decoded; event fixed/variable payloads stay in their
original byte order. Full mode validates native/foreign markers and start/frame
layout before transferring the owned group and publishing optional order. Failure
releases exactly once and leaves output untouched. Header-only mode checks fixed
width for tag collection while retaining its unknown-event/payload acceptance;
using the full event validator there would alter collector diagnostics/prefixes.
Native adapters supply decoded group reads and typed short-order publication.

Native/SDL traces cover first/absent background lookup, borrowed-cache reference
counts, a real world-picking miss/body-part output, native/foreign group round
trips, schema rejection, missing output and backend read failure. C-only tests
cover duplicate background starts, hit/miss/orphan/empty actor traversal, first
ownership match, output prefixes, both host endian layouts, full/header-only
acceptance, optional order and exact failure release. Positive/orphan picking is
tested through the C protocol; the native trace freezes the real backend miss.

### Scene native bridge audit

The branch/loop audit of `src/engine/scene.cpp` and `scene_data.cpp` checked public
methods, undo methods, service factories and debug paths. Remaining native code
still matters to final integration; moving policy does not remove its typed
storage, object lifetime or UI/resource dependencies.

| Native responsibility | Policy boundary/current status |
| --- | --- |
| Scene creation/close/owners and frame/replay/prerender | C owner/runtime/event modules; native fields publish borrowed typed views |
| Sound/editing, actor/caption/selection, chop and property undo | C policy/state modules; native undo factories/common Movie services and per-object primitives remain |
| Queries and loaded-group acceptance | C first-match/traversal/validation/release; native picking output and short/enum/tag conversions remain |
| Scene load, tag collection, save and metadata/events | C traversal/transactions/codecs; native chunk lookup, decompression, collection-header/location decoding and typed I/O remain |
| Thumbnail generation and dirty propagation | C ordering/ownership/cleanup; native graphics environments, drawing/palette/ports and Movie SetDirty remain |
| Native output guards, actor/text dispatch and optional lifetime outputs | Typed adaptation/publication; no independent Scene edit/replay choice |
| Native debug MarkMem/AssertValid/diagnostic switches | C++ object/debug instrumentation, including typed pointer traversals; not converted into C policy |

Background resource controllers are covered below. Remaining work includes native actor/caption internals,
Movie control/editing/undo, native representation/facade removal and final C-only
integration. No Marcus UI tests were added.

### Background cameras and actor placement points

`background_camera.c` owns the shared 76-byte CAM header and appended 12-byte
placement-point decoder. Both Studio Background and Marcus PreviewBackground
use it; native CameraPosition fields in Preview are renderer views. Decoding
checks the order marker, exact header/tail layout and bounded reads, and publishes
outputs only after success. Explicit signed-word conversion preserves scalar
bits independently of native integer widths and endianness.

Studio Background stores camera count/current index, position, placement cursor,
mouse matrix/yaw and placement points in a persistent C record.
C sequences lights, chunk reads, RGB/depth lookup, background installation,
placement replacement, mouse-matrix construction and world camera/dirty effects.
The native services provide file and reference operations and individual BRender
Euler, rotation and translation primitives. Studio allocation failure still
selects the camera with only its default point. A failed point read releases the
new buffer after the image installation and cursor reset.

Marcus uses the same C owner for its active and staged placement buffers and the
same default/appended point cycle. Permanent selections publish staged points
after both images succeed. Temporary selections preserve the placement cursor.
Camera projection lookups borrow a decoded CAM stream for the duration of the
read. The projection cache retains the decoded result and still requires only
the colour image to calculate aspect ratio.

Both hosts decode foreign-order appended points at their stream offsets and
reject partial points. Preview also checks clip planes and image admission.
The resource controller below reads background headers, sounds, palettes and
lights and manages cache and light lifetime. It is followed by the preview
image and mesh controller.

`engine-background-camera` exercises both byte orders, scalar/angle extremes,
maximum stream sizes, unchanged failure outputs, callback order, allocation/read
failures, cleanup, cycling and reuse. The 31,048-byte Studio scene-content trace
matches the previous implementation, including foreign-order placement data from
the real archive. The headless Marcus insertion and storyboard checks pass.
Camera and background preview fingerprints match the previous build. Results
are in `tmp/c89-camera-placements`.

### Background resource loading, caching and light lifetime

`background_resource.c` owns BKGD/BDS/GLCR/GLLT wire decoding, contiguous camera
enumeration, Studio resource construction/factory control, tag-list/cache order,
palette ownership and light attach/enable/disable/remove control.
Studio Background embeds the C owner for SDK actor/light allocations, palette,
default-sound metadata and light flags. Its String is a temporary native view of
the existing C-backed string storage. Both hosts use C-owned payload blocks to
read the records and fixed-width arrays. C89 retains Studio's native-order colours
and owns the temporary light rows. Studio's `FGetPalette` materializes an
independent native list from the retained colours. Native callbacks supply file
primitives, debug diagnostics, allocation and typed SDK field operations.
`_Ccam` and `_SetupLights` no longer have native implementations.

Marcus PreviewBackground now calls the same C resource loader. The two profiles
preserve the hosts' existing contracts and ordering: Studio counts cameras before
loading metadata, reads the name and optional default sound, retains its palette
and tolerates an optional palette read failure. Preview resolves its display
palette, admits only fitting four-byte colors and at most sixteen lights, publishes
visual entries and cameras, then selects camera zero. Both use one header/light/
palette conversion and enumeration implementation. SDK/native representations
are temporary host representations. Preview vector storage and image/mesh control
have since moved to the C owners below.

Factory estimates publish before allocation, partial failures invalidate outputs
before teardown, and successful factories publish objects before final size.
Disposal retains partial-array cleanup and off/free/free/palette-release order.
Read-failure diagnostics precede temporary-light-list release, as in the native loader.
Leave-on suppresses light removal without losing the enabled state. BDS wire
pointer slots are ignored; sound tags stay unopened with null caches. The stored
32-bit loop flag is decoded explicitly. Invalid order markers, malformed palette/
light strides and allocation-size overflow now reject before unsafe access.
Valid formats and the legacy tolerant palette-read path remain unchanged.

The C background resource fixture reads arrays in both byte orders and checks
rejected strides, failed transfers and retained palette bytes. Both builds pass
the fixture, and the background, collection and storage checks pass under
AddressSanitizer. The preview fingerprints match across 12 backgrounds and 188
camera palettes. See `tmp/c89-background-lists` for build and comparison logs.

Background caching builds all four BDS/GLLT/GLCR/CAM-zero child tags before any
cache effect, then caches parent-without-children and each child-with-children.
Like the native code, this does not close child tags; references acquired by the
existing child-tag service retain their existing lifetime. No new ownership
normalization or wire versions are introduced by the conversion.

The Scene-content fixture freezes foreign-order palette/default-sound loading,
independent palette copies, light flags/control and the preceding camera paths.
`engine-background-resource` covers both stream/host byte orders, partial reads,
load/factory/cache failures, cleanup prefixes, ownership and publication order,
profile admission and light lifetime. Full validation evidence and coverage
limits are recorded in `docs/tools/playback-regression.md`.

### Preview background images, geometry and mesh lifetime

`background_preview.c` owns camera/light metadata buffers, decoded camera and
image state, RGB/depth buffer replacement, bitmap-reference cleanup and the
two-entry mesh LRU. Camera headers publish before clip validation, dimensions
before RGB allocation, and the active index after both image draws. Missing
depth lookup retains completed RGB work and the earlier active index. Image
allocation failures now release fetched bitmap references rather than leaking
them through native vector exceptions. Malformed bounds reject before signed
subtraction/negation overflow. Native PreviewBackground exposes borrowed typed
views; it no longer owns persistent vectors or cache-replacement decisions.

`background_mesh.c` owns depth reconstruction, nearest-colour flood fill for
infinite backgrounds, discontinuity subdivision, ordered vertex/triangle
generation, pixel-centre UVs and bounds. Persistent and scratch storage use the
existing C allocator contract. It preserves signed 16-bit depth interpretation,
integer viewport half-sizes, the `.02`/eight-percent continuity threshold,
left/right/up/down flood-fill order and triangle/vertex insertion order.
Nonfinite input/computed bounds reject before unsafe float conversion.

C also controls indexed texture/material/model construction, rollback and SDK
registration lifetime. Models have at most 4096 faces, bounding SDK signed-16-bit
intermediates even for disconnected geometry. Preparation publishes only after
every model succeeds. Actor allocation failure retains the cached preparation
and attached prefix owned by the caller's frame. Cache misses build before
eviction; hits move to the most recent end. BRender callbacks only allocate,
register, free, convert scalar representations and assign typed fields.

The remaining native mesh/background classes are temporary service/type adapters.
Borrowed buffer views and bounds mirror C owners. Preview's broader frame
rendering and projection/depth fitting now use `preview_render.c`; other native
engine/host integration still needs conversion. These owners do not establish a
complete C-only Preview engine.
Existing analytic/native renderer tests and exact geometry/image traces cover
these paths. C-only tests inject storage/SDK/actor/image/cache failures and check
ownership, rollback, retained prefixes, bounds and LRU behavior. Coverage and
retail comparison limits are recorded in `docs/tools/playback-regression.md`.

### Preview picking

`preview_pick.c` owns projection admission, letterbox/output-to-raster coordinates,
pick-camera construction, singular-camera rejection, stable body copying and
actor ownership lookup. It also owns prepared-face scratch lifetime, nearest-hit
and equal-depth replacement, orthographic distance normalization, authored-image
depth occlusion and indexed texture transparency with transformed, wrapped UVs.
Rendering and picking share the same stable body-order/tree-copy implementation.
The C owner releases the temporary world before its owner/face buffers on every
exit and publishes no hit after an allocation or SDK failure.

Native adapters supply typed BRender camera/inverse/projection and pick calls,
face copying and pivot-plane repair using SDK fixed arithmetic, material/texture
field views and the SDK UV transform. Source geometry and textures stay borrowed.
No native picking evaluator or picking owner map remains. Camera interaction is
covered below, together with asset/scene admission and source/session setup.
Presentation and final facade integration remain work.

The headless Scene Preview fixture records per-build original/converted hit,
distance and error traces across authored/output, orbit, free, axis and
orthographic cameras, seeks and invalid projections. It checks that picking
leaves rendered pixels unchanged. C-only tests cover transparent holes, padded
texture/base coordinates, negative UV wrapping, opaque/nonindexed maps, ties,
unowned mesh occlusion, clip limits, source depth, letterboxes and allocation/SDK
failures. A fixed-point SDK inverse failure originally caused unwritten ray matrices and
bogus orthographic hits. The SDK now skips failed inverses; a direct test covers
singular models/cameras/bounds, orthographic determinant underflow and recovery.
Original and converted controllers agree with that same guard; raw unguarded
traces remain recorded separately. Evidence and coverage
limits are recorded in `docs/tools/playback-regression.md`.

### Preview camera interaction and guide metadata

`preview_camera.c` owns shot-to-inspection camera matching, letterboxed FOV and
focus/movement-speed fitting, orbit panning and world-limit clipping, reset and
placement/shape anchor updates. It also owns posed bounds corner accumulation,
camera index admission, same-camera I/O suppression and camera selection's
failure/publication order. Native adapters supply typed body bounds, camera and
bitmap views, reference operations and math primitives. Host math callbacks
preserve float rounding; bounds accumulate in double before conversion to float.

The guide projection cache now uses a persistent C owner for descriptors and
read statuses. It reads camera headers and colour bitmap bounds without selecting
a camera or decoding depth/scenery. Both successful and failed reads are cached.
Reset invalidates entries while retaining capacity. Allocation failure retains
storage but publishes no new count, so a second-buffer allocation failure can be
retried safely. The previous two native vectors could leave the status vector
shorter than the descriptor vector and access it out of bounds on retry.
Fetched bitmap references release before bounds validation. Malformed bounds
reject before signed subtraction can overflow.

The headless Scene fixture records matching, perspective/orthographic panning,
roll/zoom/aspect changes, clamping, invalid inputs, guide queries, bounds and
placement/shape edits with subsequent render output. C-only tests cover atomic
pan rejection, metadata failure caching, reset, partial allocation recovery,
bitmap reference balance and camera selection failures. Admission control is
covered below, together with source/session setup. Presentation and final facade
removal remain work. Evidence and limits are recorded in
`docs/tools/playback-regression.md`.

### Preview live scene, prop and timeline control

`preview_live.c` owns persistent scene/prop definitions, opaque actor handles,
camera cue lists and original snapshots. Its status owns the selected asset,
readiness, frame range, current frame, actor count and selected camera.
Native effects and render adapters read that status. The public `PreviewInfo`
reference receives each published state. Replacement backgrounds select a
camera on the candidate before swapping resources and updating live status.
Native code exposes typed borrowed
actor views rather than a second owning vector. Added props reconcile by the
matching number prefix; changed fields of retained definitions do not recreate
or move the retained actors. Suffix actors are created and sought before replacing
the previous suffix. Failure releases the current actor, then the pending prefix;
successful replacement releases the old suffix in forward order.

Definition snapshots precede scene then prop assignment. Ordinary reconciliation
failure restores both lists and retains the live actors. Allocation/foreign
exceptions retain the completed assignment prefix and completed creation/seek
effects. Removing the active virtual scene clears its live actors before typed
asset cleanup. The original actor prefix publishes before camera snapshotting;
adoption transfers a foreign object only on success.

Scene-range edits use the same `mm_scene_move_first` opening-camera policy as
Studio, including the original prefix rotation order. Camera edits reset to the
original cues/range, reapply range extension, replace equal-frame cues in input
order (last edit wins), stable-sort, then seek the clamped current frame. Sort
scratch failure falls back to an allocation-free stable sort. Original cue lookup
retains wire order and selects its last qualifying record.

Seek control preserves completed actor prefixes after failure and publishes frame
and stats only after actor and camera effects succeed. Renderer diagnostics are
cleared/checked for frame seeks, while reconciliation retains its original
diagnostic policy. Allocation exceptions during camera-edit preparation remain
distinct from errors handled by the final nested frame seek.

The headless Scene fixture freezes definition reuse/replacement/removal/rollback,
virtual scene lifetime, range edits, camera edits, seeks, actor state and rendered
output against all three original builds. C-only fault checks cover ownership,
retained prefixes, assignment/snapshot failures, retry, stable-sort fallback and
read/allocation/foreign-exception publication ordering. PreviewActor control and
ownership are covered below; resource/track representations remain typed provider
dependencies. Admission and source/session control are described below;
presentation and final C-only facade integration remain work. Evidence is
in `docs/tools/playback-regression.md`.

### Preview asset and scene admission

`preview_admission.c` controls dependency budgets, selection of templates/models/
backgrounds/scenes/actors, virtual scene selection, template action discovery and
rest orientation, pose validation and geometry statistics. The typed native host
supplies immutable chunk descriptors, resource fetches, validated event groups,
normalized fixed/payload fields, actor providers, SDK primitives and display text.
Admission uses the existing C live owner for actor adoption, camera cues, snapshots,
reconciliation and seeking. Its temporary actor-number list uses C storage.

Packed dependency headers are read before limit rejection. Limits remain inclusive
at 64 MiB stored/decoded per chunk and 512 MiB decoded per archive. Header failures
do not mark a chunk; limit failures mark the violating descriptor. Template actions
stop at the first failed/nonpositive cel count. The first matching nonempty chunk
name wins, with the original fallback text retained by the display adapter. The
initial pose precedes X, post-Y and post-Z rest rotations. Geometry counts include
isolated and visible actor bodies, excluding their two auxiliary nodes; a standalone
model replaces those counts.

Scene admission retains wire actor/camera ordering, the last start background and
opening camera, the offstage-actor rejection, and evaluated standalone actor ranges.
Early failures retain completed metadata/resource prefixes. Ordinary actor-load
failure clears prior actors after saving the resolver error; final dependency
failure clears the asset and marks the selection. Selection allocation failure
clears the asset. Other exceptions retain completed prefixes and cross the typed
boundary only after C releases groups and unadopted actors. This also closes the
old start-group leak when actor-number allocation throws. Pose exceptions retain
state; an ordinary pose-effect failure clears ready. Stored actor resolvers capture
the session owner by value.

The headless API fixture freezes template action names/counts, geometry metadata,
rendered initial/last poses, raw models and selection/pose errors against all three
original builds. Existing scene/live/camera/pick/render/background fixtures cover
timeline and render integration. C fault checks cover effect exceptions, C-storage
failures, group cleanup, adoption, partial loads, virtual scenes and pose readiness.
PreviewActor playback control is covered below. Resource representations,
presentation and final facade removal remain native dependencies. Source/session
control is covered below. Evidence and
platform limits are in `docs/tools/playback-regression.md`.

### Preview actor loading, replay and placement

`preview_actor.c` owns the Preview actor header, evaluated state, saved orientation
context, edit shape, original placement and template/body handles. The native
PreviewActor exposes borrowed typed resource views, validated track-table services,
resource resolution/binding, SDK operations and cue/timeline presentation. It no
longer owns a second evaluated state, shape or playback/placement controller.
Resource disposal releases the body before the template; the existing C-backed
track facade disposes its authored tables afterward.

Loading retains header/route/event/resource prefixes on failure, accepts invisible
roll-call actors after route/event reads, resolves embedded templates only for the
local source, validates node references after body creation, and computes old-header
lifetimes through the shared C actor timeline with the 100,000-frame work bound.
Creation admits ordinary prop templates and inserts a stationary route plus the
original entrance/action/zero-step events in order. Failed insertion retains the
completed table prefix until actor disposal.

Live frame and seek control call the same C frame/seek/effect/sound/pose evaluators
as Studio. Ordinary effect failures follow the shared sequencer's retry/publication
rules; exceptions return through the typed provider after completed state/effects.
The controller retains cue clearing/display gates, costume resets, entrance/held-
rotation orientation resets, quick backup and full replay ordering. Shape edits
compose onto the saved orientation without changing authored animation. Relative
translation and absolute offsets validate all axes before publication. Untouched
retail placements bypass editor coordinate limits and requantization; absolute
zero-axis offsets retain their original fixed bits. Posing uses shared C fixed
matrix arithmetic and retains SDK operation order.

The C-only actor fixture covers construction/load failure prefixes, resource release
order, corrupt routes, old lifetimes and resolver exceptions, replay/backup/hide,
provider failures, orientation resets and placement/shape behavior. Existing headless
and Marcus integration fixtures retain their original outputs. Native effect/resource
primitives, track representations, visibility clock/cue containers, timeline extraction/
display and final host/facade removal remain dependencies. Source/session control
is covered below.
Evidence and platform limits are in `docs/tools/playback-regression.md`.

### Preview sessions, runtime lease and content registry

`preview_session.c` owns the process-wide Preview runtime lease, each session's
running state/root cache, orbit state and ordered opaque content-source handles.
Native code supplies file and path handling, display palette publication,
cache and renderer services, and error text. The former native source vector,
source IDs, runtime flag and replacement, registration and lookup loops are
removed.

Opening first validates transfer input, then prepares a candidate, consumes the
input archive and checks resources/budgets before touching the active runtime.
Preview scans the shared chunk index and reads stored compressed headers
through C89, applying the per-chunk and aggregate decoded budgets before any
payload decoding. It reads the common `GLCR/0` palette from the archive cache
as an exact array of 256 four-byte entries. Preview and Studio background
loading share the colour-word byte-order conversion. Preview resolves display
colours before checking the `TMAP/0` key. Only a candidate for an idle owner
starts BRender/depth rendering; another owner's live lease rejects the attempt.
Root cache/shade setup precedes lease transfer, optional orbit copying, owner
publication and immediate disposal of the previous session. Failed candidates
release their resources without clearing a different owner's lease. Completed
global SDK effects are not rolled back.

Source registration validates running/positive ID, suppresses exact ID/path
duplicates, opens/checks/caches a new source and appends ownership only after C
storage succeeds. Failure releases the temporary source while retaining the
registered prefix. Lookup gives root-cache sid zero priority, then searches matching
registered IDs in insertion order until a requested chunk is found. File/path
operations and missing-source formatting remain typed services.

Disposal clears live assets and tables, releases sources forward, root cache,
shared material/shade resources, the depth backend and BRender in order. Error
formatting for ordinary failures precedes candidate disposal; allocation/foreign
exceptions cross the typed boundary afterward. The headless session fixture freezes
replacement isolation, source duplicates/order, ownership handoff, empty sessions,
framing preservation, errors and rendered output against all three original hosts.
C fault checks cover every fallible open/add stage, palette byte order/admission,
lease transfer/rejection, release order, registry storage exhaustion and retry.
Before-and-after Preview startup probes agreed on 10 resource sets, including
the original `3DMOVIE.CHK`, both palette byte orders, missing or malformed
resources, compressed-header failures and the per-chunk budget.
Capture suite `20261120T071914Z-b613b82c` matched 60 retail frame identities
in seek and playback on SDL and Marcus. SDL matched 12 retail sound requests,
and each host matched the 60 seek images from its previous build.
Native resource/track/archive/path representations, asset-cleanup primitives,
live property/font/argument materialization, text editing/undo, display/output representations and
final C-only host/facade removal remain dependencies. Metadata, media and stored
caption persistence are covered below.
Evidence and platform limits are in `docs/tools/playback-regression.md`.

### Preview timeline replay and presentation cue traversal

`preview_metadata.c` controls private actor timeline extraction, marker classification,
span splitting, replay/output budgets, scene presentation group traversal, actor and
scene sound-cue mapping, pause validation and stable cue ordering. Typed native
services supply validated group/header reads, cloned track storage, shared actor
effects, caption storage, display labels/font names/numeric formatting and public
output containers. Native code no longer has a second timeline or scene-cue evaluator.

Timeline extraction uses the same C frame/step/effect evaluators as live playback,
with a private cloned track, metadata-only effects and suppressed sound output.
Frame caches mutate only that clone; live actor/cue/render state remains unchanged.
The 100,000-frame budget is measured from the actor's first frame. Marker capacity
is checked before classification, including ignored sound rows once the 100,000
marker limit has been reached. Spans split on visibility gaps, action/entrance
restarts and freeze changes. Two-word end frames retain `INT32_MAX+1` without C99
integer types. Failure retains completed output prefixes and releases the clone.

Presentation reads start then frame groups, retaining actor/caption mode gates,
inactive-actor handling, pause bounds and queue flags from packet index. Actor cue
flags preserve the legacy ternary interpretation; live cues and authored sound
sheets use the same C mapper. Actor event groups release before their containing
scene group, including allocation/foreign exceptions from output providers. This
closes the former raw-group leaks on output-vector exceptions. Public cue ordering
sets origin identity before a stable frame/scene-before-actor merge sort. Scratch
failure uses an allocation-free stable merge with bounded recursion, preserving
ties without a quadratic insertion fallback.

The headless metadata fixture freezes full timeline labels/details/spans, captions/
properties, pauses, sound sheets/audio bytes, frame cues and rendered/live-state
isolation across batch, event, scene-audio, actor-audio and text-acid documents.
The regular asset-preview harness generates and verifies all five documents. C-only
fault checks cover clone/group ownership, partial output, budgets, extreme frames,
live isolation and sort stability with/without scratch for counts zero through 64.
Caption/audio loader control is covered below. Display formatting/name-cache and
output/cue representations, physical providers and final C-only host/facade
integration remain dependencies.
Evidence and platform limits are in `docs/tools/playback-regression.md`.

### Preview caption and audio loader control

`preview_media.c` owns caption input/scratch lifetime, text limits and conversion
sequencing, UTF-16 property validation, font-argument admission and stable property
ordering. Typed native services supply rich-text/text-box persistence, serialized
font-name decoding, UTF-16 conversion and public descriptor storage. The C controller
releases scratch before the input owner on every exit, then the facade rethrows
native exceptions. Failures retain completed output prefixes. Sort scratch failure
uses a stable in-place merge with bounded recursion; successful sorting moves each
property together with its formatted font name.

Audio loading admits one MSND envelope, validates its byte-order marker and invalid
flag, treats invalid/missing children as silent success, and caps decoded payloads
at 64 MiB. MIDI uses C-owned bytes and the shared C parser directly, removing the
Preview path's native MidiStream/parser/HQ dependency. Empty streams and malformed
tails retain parser-exhaustion success. The million-channel-event and 24-hour
budgets precede channel filtering; events at exactly 24 hours remain valid. MIDI
scratch exhaustion retains decode failure, while caption scratch exhaustion keeps
the former vector allocation exception classification.

Public sound lookup also runs in C: silent cues bypass readiness and source
publication, imported-scene children take precedence over the first matching movie
child, and the source field publishes only after successful decoding. Failed child
lookups cannot overwrite the original sound number. The typed lookup tolerates a
ready session without a backing archive and reports a missing source.

The headless media fixture freezes native/foreign envelopes, malformed/empty MIDI,
time boundaries, ANSI/UTF-16 captions, font arguments, stable ties and failure
prefixes against three original hosts. It runs in the regular asset-preview harness.
C-only tests exercise effect/allocation failures, cleanup, conversion/publication
prefixes, sort fallback, MIDI budgets and source remapping. Stored rich-text/text-box
persistence, live chunk saving and text document transactions are covered below.
Live property/font/argument materialization, text editing/undo and display/output
representations, physical providers and final C-only host/facade removal remain
dependencies. Evidence and platform limits
are in `docs/tools/playback-regression.md`.

### Stored rich-text and text-box persistence

`text_store.c` implements RTXT/TBOX wire codecs, the stored rich-text owning record,
read replacement transactions, file/collection release order and write rollback.
Native `RichTextData`/`TextBoxData` methods marshal values and supply file/block,
counted-string and C-backed collection primitives. Studio's rich-text document
reader and Marcus's caption loader use the same stored-text reader; root creation
also serves Studio's live document writer. Native descriptor fields remain temporary
representations for existing consumers.

TBOX requires exactly 36 bytes, either byte order, a first frame other than
`INT32_MIN` and a zero/one story flag. It ignores OS/reserved fields, accepts a
backward lifetime or `INT32_MIN` end, and only resolves a rich-text child when
requested. Successful reads publish `first-1` as the current frame. Writes normalize
reserved bytes and byte order without adding range/bounds policy.

RTXT admits at least its 16-byte header, font heights 4 through 255 and widths
1 through `kcbMax-1`. Unreadable/empty fonts retain the empty-name/current-system
fallback. TEXT's raw system marker, property/argument byte orders, argument fixed
words and payload bytes remain unchanged. A borrowed text location is retained
before reading its marker; GLMP must have eight-byte entries, optional AGPA must
have four-byte fixed data. Failure releases only the temporary file/property/argument
owners. Success releases the old owners in that order and publishes the complete
replacement. Native exceptions rethrow after C cleanup.

Stored writes validate before touching the output identity. Root creation publishes
RTXT before allocation, nil after add/write failure, and removes a partial root
after failed header/font writes. Child order remains TEXT, GLMP, then nonempty
AGPA. C owns the acquired copy destination through text copying and releases it
before tree rollback. Later failure removes the entire new tree and publishes nil.
Native wrappers use an internal untouched identity marker so they never read an
uninitialized caller output while preserving early-failure outputs.

The headless stored-text fixture freezes decoded state and complete written chunk
payloads, both byte orders, malformed input, font fallback, retained state and round
trips. Argument bytes are explicitly aligned so the trace does not capture uninitialized
legacy collection padding. C-only fault tests cover all read/write effects, negative
exception results, partially returned owners, copy cleanup, output publication and
one-/two-byte counted fonts. The regular asset-preview harness also runs the fixture.
Live chunk saving, text-box adoption and text document transactions are covered
below. Editing and help-document argument extensions, native descriptor/output
representations, physical providers and final C-only facade removal remain work.
Evidence and platform limits are in
`docs/tools/playback-regression.md`.

### Live rich-text chunk save and text-box adoption

`text_live.c` controls live rich-text root/TEXT/GLMP/AGPA creation, stream-window
sequencing, save-error publication, optional text redirection and text-box adoption/
metadata rollback. Native facades supply borrowed live groups/streams, file/block
primitives and diagnostics. Root wire encoding/creation uses `text_store.c`.

Live saves publish RTXT before validating the header. The output record tracks
whether the number changed so native callers can preserve an untouched, potentially
uninitialized number after early validation failure. Root creation failures retain
the shared nil-number behavior. Later child rollback retains the created number,
unlike stored-data writes. TEXT always writes its native system marker and moves
past it before copying, including empty text; the stream sentinel is excluded.
GLMP and nonempty AGPA output use native order/system. A failed move diagnoses and
continues like the original `AssertDo`; ordinary save errors publish after rollback.
Negative exception results clean up before native rethrow without ordinary errors.

Redirection runs after the tree has committed. Ordinary replacement failure
remains best effort, and an exception retains the saved tree. Acquired locations
release on every exit. Failed location lookup diagnoses and returns failure without
using a trashed native view. The native primitive still supplies its materialized
text view; it need not alias a later writable chunk view.

Text-box saves reset the child ID to zero before saving. Failed save retains the
save primitive's own effects. Adoption failure deletes the unlinked rich-text root;
metadata failure removes the adopted child link. Neither the child-ID reset nor
partial metadata output is rolled back. Unexpected root types retain the legacy
diagnostic/continue policy.

The existing engine movie save/load fixture records live-save output, font/style
arguments, empty/single/embedded-null/CRLF/ANSI text, both redirect modes, stream
bytes after materialized-view writes and subsequent text-box saves. Allocated-group
payloads are recorded logically to exclude uninitialized alignment padding. Native
and SDL font-table ordinals differ, so each host is compared with its own original
trace. C-only tests cover ordinary/allocation/foreign failures at every effect,
number/error publication, post-commit redirection, partially returned locations,
empty content/arguments, both byte orders/native text-unit sizes and adoption/
metadata cleanup order. Document transactions and materialization are covered below.
Text editing and help-document argument extensions, native descriptor/output
representations, physical providers and
final C-only host/facade removal remain work. Evidence and platform limits are in
`docs/tools/playback-regression.md`.

### Text document file saves and resource lifecycle

`text_document.c` controls plain/rich file-save transactions, base initialization/
loading, rich initialization, document factory cleanup and stream/file/archive/
property/argument release. Native host slots retain the current typed storage;
C controls reference transfers and publication. File/stream/collection operations,
font-table queries and virtual dispatch remain host services. The default load
hook now uses C; subclass load/read hooks retain their dispatch behavior.

Base initialization opens the file before validating a borrowed stream, retains
the stream before publishing it, and appends a native CR sentinel to that same
stream. A virtual load hook may replace that owner; sentinel insertion uses the
current stream after the hook returns. Failure leaves partial owners for destruction. Default
loading retains a mutable file location across translation, allows the translator
to replace its owner, replaces the full stream, and releases the final location
on every exit. Empty files skip translation; empty translated output skips replace.

Rich opening holds its archive privately until the root read succeeds. Ordinary
failure releases that archive before reporting a read error; partial live stream/
group effects remain for object destruction. New documents use eight-byte property
entries, grow by ten and use height twelve independently of application text size.
Factories release partial objects after failed initialization. The chunk-read
factory reports an ordinary read error before release; allocation failure does not
report a read error. Negative exception results unwind before native rethrow.

Both file-save profiles select the current name and force adoption for normal
saves. Copies preserve current owner and dirty state. Plain name queries reject
temporary files; direct normal save still accepts an existing temporary owner.
Plain output omits the sentinel and writes a native UTF-16 BOM for two-byte units.
Plain stream redirection and rich chunk redirection precede renaming; ordinary
redirect failure remains best effort. Rename failure releases the temporary owner
without rolling back prior stream redirection. Success clears temporary status,
then either releases a copy or releases the old owner, transfers the new owner and
clears the dirty bit. Only rich ordinary failures report save errors. Destruction
releases properties, arguments, archive, then base stream/file in the original order.

The expanded engine movie save/load fixture freezes copy/adopt/normal saves,
empty and embedded-null/CRLF/ANSI content, dirty/name behavior, borrowed-stream
reference/sentinel changes, reopen, and fresh rich-document creation. C-only tests
cover every fallible effect, partial returned/published owners, translation owner
replacement, redirection before failed rename, BOM/endian rules, factory error/
release ordering and plain/rich name policies. Materialization is covered below.
Editing/undo, native representations, physical providers,
document/view/parent lifecycle and final C-only host/facade removal remain work.
Evidence and platform limits are in `docs/tools/playback-regression.md`.

### Rich-text materialization and default font arguments

`text_materialize.c` controls stored-to-live rich-text loading, property/fixed-word
byte normalization and default font argument opening. Native adapters supply
typed collections, font-table and string primitives, owner slots and virtual
argument dispatch. Stored reads use the shared `text_store.c` transaction;
base initialization and translation reuse the document services.

Base initialization precedes reading. Defaults and the mapped default font publish
before text translation. A separate retained location follows translated owner
replacement, and releases before property/argument ownership transfers. Copying
is requested only when translation retains the original file identity. The legacy
`CpMac()-1` value remains the replacement byte count, including two-byte units.
Properties transfer before byte normalization; arguments transfer before their
descending traversal. Free slots are skipped, and each visited fixed word is
normalized before the virtual hook. Failure retains the published defaults, text,
collections and normalized prefix for later document destruction. Exceptions clean
temporary owners before returning to the native rethrow boundary.

Default argument opening queries size even for unknown kinds. Font entries require
the six-byte font-ID/system prefix, use its native short system without applying
the collection byte order, and decode the following serialized string while locked.
Malformed-data diagnostics occur before unlock; font mapping and runtime-ID storage
occur after unlock. Decode exceptions unlock without an ordinary diagnostic.
Help-document subclasses still receive their virtual hooks and retain their own
object/group argument policy.

The engine movie save/load fixture freezes ten success/failure cases against the
original native/SDL paths, including foreign properties/fixed words, translation
failure, unknown argument kinds, subclass rejection and free slots. Logical group
payloads exclude legacy padding. C-only checks cover fallible effects and cleanup,
partial publication/normalization, translated owner identity, copy flags, both
text-unit sizes and byte orders, and font lock/diagnostic/map ordering.
Editing/undo, help-document argument extensions, native output/descriptor storage,
physical providers, document/view/parent lifecycle and final C-only host/facade
removal remain work. Evidence and platform limits are in
`docs/tools/playback-regression.md`.

### Rich-text property maps and argument references

`text_properties.c` controls sorted property lookup/range fetching, masked range
application, boundary adjustment after text edits, argument interning and reference
counts. Native hosts supply typed collection primitives, cache slots, borrowed
change records and paragraph-end queries over the already edited stream. Change
records are converted one at a time without adding an allocation to native edits.

Lookup preserves predecessor/insertion-index and next-boundary behavior; missing
fetches leave outputs untouched. The native wrapper retains debug poisoning for a
missing map entry. Applying changes clears both property caches, processes changes
backward, merges masks, removes redundant boundaries and restores the value after
the changed range. Reference ownership moves between entries and the saved revert
value; each input change reference is consumed once. Releasing an unused batch
processes its references forward. Callers still reserve twice the change count;
ordinary insert failure retains the legacy diagnostic-and-continue policy.

Boundary adjustment uses the new stream after replacement. Character properties
at the deleted interval collapse to the insertion end. Paragraph properties that
begin at the edit stay at its start; others move to the next paragraph unless a
surviving boundary already covers it. Object entries in the half-open deleted
range release their arguments and disappear. Entries after the interval shift.
Partial mutations remain when a paragraph/insert service returns an exception;
rethrow occurs after C returns, with no rollback added.

Interning scans backward, skips free slots and matches payload bytes and property
kind before incrementing a shared reference. New records keep one-based references
and native slot-reuse behavior. Built-in properties use arguments only for fonts;
even client/object kinds use arguments, including client paragraph kinds. Invalid
indices diagnose without mutating the group, while the legacy count/overflow
assertions remain diagnostic services. A newly created argument group publishes
immediately and remains document-owned after later insertion failure.

The headless engine fixture freezes original native/SDL lookup outputs, property
maps, logical argument bytes/counts, cache invalidation and stream contents across
masked applications and boundary edits. C-only tests compare masked changes against
a character-by-character value model and edit adjustment against an independent
per-kind reconstruction. They also cover all 256 kind classifications, backward
matching, holes, reference transfers and fallible creation/insertion/paragraph
queries. Character/paragraph formatting and application are covered below.
Property copying and rich-text/generic undo are covered below. Help-document
argument extensions, native output
storage, physical providers, document/view/parent lifecycle and final C-only
host/facade removal remain work. Evidence and platform limits are in
`docs/tools/playback-regression.md`.

### Character and paragraph formatting

`text_format.c` controls character/paragraph delta packing, batch generation, font
argument descriptors, cached formatting fetches, paragraph bounds and formatting
apply transactions. Native adapters marshal value records and supply font-table,
paragraph-navigation, collection and virtual undo/invalidation primitives. The
shared string codec writes font names; the C property module supplies interning,
references and masked map edits, including a direct C change-array entry point.

Style deltas keep short height differences, byte offsets and font-style masks.
Paragraph deltas keep per-field masks and signed-short wrapping. Color differences
retain the native black/clear origins and 32-bit arithmetic. Font arguments retain
the native four-byte runtime ID, two-byte system and counted-string payload.
Batches emit in increasing kind order. Ordinary font failures release the emitted
prefix; exceptions preserve partial effects and native mask/count publication.

Fetches intersect each built-in property's range. Cache hits skip map/font work;
misses initialize defaults and decode only present values. Native achar signedness
determines offset-byte extension, including the unsigned wide-character behavior.
Font records outside the six- through 522-byte range warn and retain the default
font; valid-size records supply their runtime ID without decoding the stored name.
Paragraph additions explicitly retain short wrapping. Partial cached values/ranges
publish before native exception rethrow.

Paragraph bounds retain expand/contain behavior and query order over CR/LF text.
Apply publishes paragraph bounds before virtual undo setup. Empty paragraph ranges
succeed without edit effects. Inputs/defaults are marshalled after that virtual
hook; property application reads the current stream length after reservation.
Ordinary delta/reservation failures cancel undo; reservation failure also releases
generated argument references. Success applies changes, commits undo, bumps the
combine counter, validates and invalidates views/marks dirty. Identical character
formatting still follows this transaction. Exception results retain partial effects
without adding rollback. Rich-text undo control/records are covered below.

The headless engine fixture freezes delta words/masks, batches, native font bytes,
cache hits/ranges, boundary selection, malformed font-record sizes and public
formatting calls with virtual undo probes. C tests cover both unit sizes/orders,
maximum counted font names, offset signedness, short wrapping, field masks,
partial cache/range outputs, exception/ordinary failure ordering and input changes
made by undo hooks. Text access/replacement and document/object copying are
covered below. Help-document argument extensions, native output/descriptor storage, physical
providers, document/view/parent lifecycle and final C-only host/facade removal
remain work. Evidence and platform limits are in
`docs/tools/playback-regression.md`.

### Text access, navigation and replacement

`text_access.c` controls the 512-character stream cache, edit invalidation,
character/block reads, paragraph/word navigation and counted literal search.
Native host adapters supply borrowed cache storage, primitive stream reads,
classification/case mapping and virtual character/cache dispatch. Reads re-fetch
the current view after virtual cache hooks, retaining subclass behavior.

Cache filling keeps overlapping front/back data and preserves the original range
publication order around reads. Invalidation keeps the longer surviving end,
choosing the front on a tie. Failed reads retain partial cache/data effects.
Navigation preserves CR/LF handling, ignored line feeds, paragraph boundary rules,
word-delimiter behavior and diagnostics for isolated line feeds/missing final CR.
Case-insensitive search lowercases the caller's pattern even on miss and retains
the low-byte membership bitmap used for wide characters. Native wrappers retain
debug poisoning of failed search outputs; the C operation leaves them unchanged.

`text_replace.c` controls plain/rich raw, file-location and stream replacement,
including formatting, translated-file ownership, nested virtual dispatch, undo
setup/cancellation/commit, map adjustment, validation and notification order.
Base replacement scales character positions to native byte counts, invalidates
through the virtual hook and notifies/marks dirty. Its translated file remains
owned through notification. Copying is requested only if translation preserves
the original owner. Rich unformatted file replacement translates before its size
limit/undo checks and releases its retained view before adjusting properties.
The nested base operation still performs its own translation and copy decision.

Formatted replacement generates deltas before undo, selects contained old
paragraph ranges, reserves space before setup, and calls virtual unformatted
replacement. Character and paragraph batches apply afterward over the new stream;
notifications retain the expanded undo extent. Nested dirty/notification/undo
calls remain observable. Ordinary failures cancel only established undo and
release unconsumed batches, without rolling back prior text/dirty changes.
Exceptions retain partial edit/reference effects while releasing owned file views.
Native range assertions remain at typed change-array application boundaries.

The formatted file wrapper still forwards its original byte count as the core
insertion count after translation, with the original copy-identity test. This
legacy unit/count behavior is explicit in the C API and tests; it is not silently
repaired or used to claim complete wide/translated formatted-edit equivalence.

The headless engine fixture freezes cache contents/bounds and virtual call counts,
search/navigation results and mutated patterns, direct base and public rich
replacement, file/stream paths, formatted edits, and real undo/redo. C tests cover
both text-unit sizes, generated cache/edit/search models, replaced virtual cache
views, ordinary/exception failures, translated owner identity/lifetime, nested
replacement and reference transfers. Formatting and replacement share the C test
host without changing its existing checks. Document/object copying is covered
below, followed by rich-text/generic undo. Remaining document/view lifecycle,
physical/classification/case providers and final C-only host/facade removal remain
work. Evidence and platform limits are in `docs/tools/playback-regression.md`.

### Rich-document copying and embedded objects

`text_copy.c` controls rich-document replacement, property-range copying and
embedded-object fetch, dispatch, insertion and property edits. The native host
supplies borrowed streams/maps, argument bytes/locks, allocation, virtual undo/
replacement/render hooks and typed validation. The C property and formatting
modules supply argument interning, reference control, masked range application
and character batches. Native wrapper signatures and subclass dispatch remain.

Line text, rich text and byte-stream views share the clipboard controller in
`text_copy.c`. It orders reversed selections, owns acquired clipboard
references, and excludes the terminal character for line and rich text.
Rich paste tries the rich format first and uses the insertion style when
pasting plain text. The controller separates undo, caret and redraw steps
from the native view callbacks and preserves the successful prefix on errors.
Studio's three view types produced matching cut, copy, clear and paste traces
before and after the migration.

Property copying clears the live destination map while advancing its index,
retaining the legacy shift/skip behavior. Source ranges are clipped and their
last value extends across a longer destination range; different default font
heights are not rebased. Ordinary reserve/import failures warn and continue.
Property imports read size before locking; the object-copy pass locks first.
Locks release after interning even on exception results. Every object kind is
imported as an argument, including odd client kinds classified inline elsewhere.
Failed object insertion releases the imported reference directly.

Document replacement sets undo before protecting the final paragraph marker,
calls virtual stream replacement, copies character/contained paragraph/object
properties and commits before notification. Ordinary replacement failure cancels
undo; exceptions retain prior text, properties and references without rollback.
Object queries concern built-in kind 192, publish position/size before allocation
and report found even when allocation fails. The caller owns any allocation output
published before an exception. Rectangle/draw queries use exact-position dispatch.

Object insertion stages its argument and optional character batch before reserve.
Legacy reserve failure releases only the object reference; failed replacement
releases the character batch first, then the object and undo. Ordinary reserved
map insertion failure diagnoses but continues to commit/notify. An unacquired
argument is represented by zero rather than reading native uninitialized storage.
Object property edits retain the map entry/index found before undo setup, replace
the reference and release the old one before commit. Identical payloads still edit.

The headless engine fixture freezes logical maps, argument payloads/counts, text,
formatting and object queries before/after copies and edits on native/SDL hosts.
C tests cover both unit sizes, import lock order, partial effects, live-map clearing,
range extension, final-marker protection, odd object kinds, reference cleanup,
allocation publication and ordinary/allocation/foreign failures. Native allocator
failure equivalence is not claimed. Rich-text/generic undo is covered below.
Help-document argument extensions, native output/descriptor
storage, physical providers,
remaining document/view lifecycle and final C-only host/facade removal remain
work. Evidence and platform limits are in `docs/tools/playback-regression.md`.

### Rich-text undo records and transactions

`text_undo.c` owns text undo suspension/combination counters, pending-record
setup/cancel/commit and rich snapshot record construction, combination, disposal
and undo/redo replay. Native text documents and undo objects store plain C
`MMTextUndoControl`/`MMTextUndoRecord` data directly. Adapters retain typed
allocation, virtual document/selection hooks and generic document undo-list
services; they no longer duplicate the rich-text transaction/record policy.

Setup increments suspension even for nested, disabled or empty actions. Ordinary
record creation failure resumes undo; exceptions retain suspension. Cancel releases
pending only after the virtual resume reaches zero. Commit first tries the last
completed rich record, clearing redo on combination; otherwise it adds to the
generic stack. Ordinary stack-add failure is ignored before releasing pending,
as in the original. Exception results retain pending and prior stack effects.

Factories sort the replaced range, publish its position/count into the new
record and create an internal, undo-suspended snapshot before copying. Adjacent
insertions with the same combine counter extend the earlier inserted count.
Repeated Delete appends saved text; Backspace prepends it and moves the position
only after successful copying. Other combinations with inserted text are rejected.

Replay snapshots the text being replaced, suspends/hides the target selection,
applies the old saved text or deletes inserted text, resumes and selects/shows/
collapses the restored range. It swaps snapshot ownership and inserted count only
after those hooks finish. Ordinary snapshot-copy failure retains the legacy early
resume, even before target suspension; document-factory failure does not resume.
Ordinary edit failure releases the private snapshot before resume. Exceptions
retain prior text/counter/record effects without added resume/rollback, but private
construction/replay owners release before rethrow. This cleanup boundary does
not claim equivalence to native exception leaks.

The headless fixture freezes original native/SDL text, properties, undo/redo
counts, pending/suspension state and selection events across typing, repeated
Delete/Backspace, object edits, cancellation, disabled/nested/empty transactions,
reversed ranges and direct record combination. C tests use content-bearing
snapshot owners and check transfer/release order, partial publication/effects,
invalid records, failure paths and repeated undo/redo. Generic undo lists are
covered below. Help extensions, native representations/providers,
remaining lifecycle and final C-only host/facade removal remain conversion work.
Evidence and platform limits are in `docs/tools/playback-regression.md`.

### Generic document undo lists

`document_undo.c` controls document undo admission, undo/redo dispatch, redo/all
clearing, maximum-count trimming and disposal. `MMDocumentUndo` owns a C collection
buffer of record pointers, a live done cursor/limit and lazy-preparation state.
The native `DynamicArray` undo facade is removed from both Movie and text documents;
their prior-item accessors now read the same C-owned stack. Initialization retains
the ten-record default, 128/16-byte growth hints and debug tail poisoning. Prepared
empty stacks remain distinct from stacks that have never allocated.

Admission returns success without allocation/reference changes when disabled.
Otherwise it prepares storage, calls virtual redo clearing, drops one oldest entry
when at the current limit and attempts append. Ordinary allocation failure drops
the oldest record and retries; exception results stop before this fallback. The
new record gains a reference only after append. The done cursor publishes last,
after reference acquisition. Undo/redo call the borrowed record virtually and
advance the live cursor only on success, preserving changes made by that hook.
No new clamp repairs a cursor invalidated by a successful reentrant callback.

Clear pops records newest first and resets done after releasing the last one.
Redo clearing leaves done alone. Shrinking the limit trims redo above the larger
of done/new limit first, then removes oldest completed records, decrementing done
before each release. Disposal clears records and frees C storage before document
child teardown. Partial collection/reference/cursor effects remain on exceptions.

Native adapters retain allocator/refcount/record dispatch, virtual clear hooks and
debug validation/marking. Their temporary allocator binding catches exceptions
inside resize/free callbacks and restores its previous context on every exit,
including nested operations. C checks status before treating a storage failure as
ordinary OOM. Removing the native collection object changes allocation granularity;
exact native allocator-failure equivalence is not claimed.

The headless engine fixture freezes original native/SDL list order, lazy state,
limits, undo/redo counts, references and virtual/destructor events. C tests compare
3,000 generated operations with an independent list/reference model, and cover
allocation fallback, exception stops, release/publication order, partial reference
effects and reentrant replay/clear hooks. Native undo objects/physical allocators,
help extensions, native string/display/UI representations and final C-only host/facade
removal remain conversion work. Document lifecycle is covered below. Evidence and
platform limits are in
`docs/tools/playback-regression.md`.

### Document registry and lifecycle control

`document_lifecycle.c` controls registration/unlinking, live subtree enumeration,
internal/clipboard status, release/destruction sequencing, view closure, root/file
lookup and close/save dispatch. Native documents and the enumerator store plain C
registry/node/iterator data directly. Legacy tool parent access now uses the public
accessor; no native parent/sibling/child fields duplicate the C graph.

Registration prepends roots/children or inserts after a sibling anchor. Unlinking
retains the removed node's links. Enumeration combines pre/post for leaves, emits
root flags at entry/final post and preserves subtree-skip behavior and unchanged
node output on miss. Close queries visit dirty nodes in postorder. Force ignores
ordinary save failure but still honors explicit cancel; assume-yes skips the query.
Root lookup/close traversal observes live sibling links. Generic save retains
normal/save-as/copy name selection, adoption, ordinary write-error reporting and
title notification only after non-copy success.

Release decrements the native reference first, then marks freeing and disposes
undo before closing children. If view closure leaves the same first child, it
diagnoses/releases that child and retries the live list. Destruction guards a
clipboard document with two retains before clearing it, unlinks it and releases
view-list storage. View closure protects its document, re-reads the live first
view/count and releases the containing window when present, otherwise the view.
One window may remove several views. The final balancing release may destroy the
document; C does not touch it afterward. Exceptions preserve partial reference/
tree/save effects without new compensation.

Native adapters retain typed reference/delete, undo disposal, view/window/clipboard,
filename, dialog and title primitives. The headless fixture freezes original
native/SDL links, traversal/skip outputs, internal/dirty values, close choices,
save/name dispatch, root lookup and explicit release events. C tests compare
80 generated 24-node trees and their subtree visits against an independent model;
they also exercise child fallback/removal, batched view closure, clipboard guards,
query/save failures, force/cancel behavior, registry order and partial effects.
View-list storage and title/name policy are covered below. Native strings/display/
UI representations, physical and clipboard/window/dialog providers, help extensions
and final C-only host/facade
removal remain conversion work. Evidence and platform limits are in
`docs/tools/playback-regression.md`.

### Document view storage and title policy

`document_views.c` owns borrowed view-list storage, first-match lookup/removal/
promotion, first-view active selection, window activation, title traversal and
clipboard/file/untitled name selection. `MMDocumentViews` owns the C collection
buffers and logical count/preparation state. Native view `DynamicArray` storage is
removed. Untitled counters/cache now reside in C registry/node data; legacy topic
naming uses the shared counter helper. Native string formatting remains a provider.

The common document view also delegates command availability and clipboard
sequencing to C89. Paste probes reject empty and self-owned clipboard content
before calling the view's format handler. Cut publishes the clipboard copy and
releases its local reference before clearing the selection. Studio translates
command IDs and supplies the native clipboard and virtual document operations.

Entries retain insertion order and duplicates without gaining/releasing references.
An empty prepared list keeps its storage. Removal/promotion use the first match;
missing promotion retains its diagnostic. Native failed-find poisoning remains in
the typed wrapper. Ordinary append failure preserves the existing list; it never
uses undo-list eviction. Defaults retain pointer width, 128/16-byte growth hints
and debug unused-tail poisoning.

Active selection tests only the first view. Window activation chooses the first
view with a containing window. Title updates read each document's name in preorder,
including documents without views, then re-fetch each view from the live list.
Callbacks may remove/append views; the existing index-shift/skip behavior remains.
Shared windows receive each view's update. Clipboard names take priority over file
names; untitled numbers allocate lazily, stay cached and use explicit 32-bit wrap.
Partial name/title/counter effects remain on failures without rollback.

View and undo collections share the typed allocator exception wrapper. It restores
prior bindings after nested operations. Read-only presentation calls do not bind
allocator state, so activation may destroy its document without a later wrapper
access to freed storage. Physical window activation, title writes, filename lookup
and string emission remain native services.

The headless fixture freezes original native/SDL view order, active selection,
lookups, document reference counts, names and traversal events using in-memory
GOBs without windows, activation or drawing. C tests compare 4,000 list operations
against an independent model and cover allocation/exception failures, duplicates,
borrowed references, live title-list mutation, owner destruction during activation,
name priority/cache/wrap and partial output. Native string/display/UI representations,
physical/clipboard/window/dialog providers, help extensions and final C-only
host/facade removal remain conversion work. Evidence and platform limits are in
`docs/tools/playback-regression.md`.

### Clipboard ownership and transport

`clipboard.c` owns the active document reference and the current, importing,
exporting, and delayed-import state. Replacement publishes the new identity
before renaming and releasing the old document. A format availability query
can leave an imported document deferred. Acquiring it or opening its view
fetches the delayed data. A missing Windows data handle leaves that import
pending for another request.

SDL imports a complete text snapshot and normalizes LF and CRLF to CR.
C89 also trims native text at the first zero unit, checks and pads export
sizes, and flushes the current format before accepting the next one. Native
callbacks handle SDL clipboard I/O, native code pages, Win32 open/lock/transfer
calls, and document factories and references.

Clipboard text materialization and document export also run in C89. An import
reuses a text document when possible and creates a rich-text document when
needed. Deferred imports keep the document identity and defer replacement
only above 1,024 bytes. Export omits the terminal character and ends the
session even when allocating its payload buffer fails.

The SDL Studio probe matched before and after for external text import,
mixed line endings, local document identity, staged export, and clearing.
C tests cover Windows format probing and delayed import, reentrancy guards,
reference ordering, allocation failures, document replacement, and export
sizing. The native document probe matched at the defer threshold and during
materialization into an existing text document.

### Help document data and editing policy

`help_text.c` owns help topic initialization, 28/36-byte wire decoding/encoding,
forest/plain chunk read sequencing, string expansion and topic publication,
save/adoption rollback, object/group argument normalization, picture/button payload
construction, grouping/query and dirty publication. `MMHelpTopic` replaces the
native stored `Topic`; the public native structure is marshalled at the boundary.
Native methods supply physical file/block access, string lookup/emission, virtual
rich-text/undo/effect hooks and existing C property/argument services through
`help_text_host.h`. Native constructors retain the resource cache needed by drawing.

Foreign headers swap the first six topic words only; the sound tail deliberately
retains the legacy unswapped behavior. Older headers default the sound pair to
zero/nil. Object argument normalization publishes a swapped tag before rejecting
an unknown or truncated subtype. Grouping sorts ranges before undo, reads the name
after undo, consumes argument references through the shared property map and keeps
partial effects on exceptions. Group queries publish end/target before name decode,
ignore ordinary name decode failure and publish group/start last.

Expansion marks the document internal and leaves undo suspended. It skips inserted
placeholders, wraps decimal IDs at 32 bits, warns on missing strings and continues
after ordinary replacement failure. Topic metadata publishes only after the read
and expansion succeed. Save publishes the root type before allocation, and deletes
child before parent on failed adoption. Exceptions retain other partial effects;
private file/payload owners and group locks now release before native rethrow.

The headless fixture freezes preconversion Native/SDL text, properties, arguments,
group outputs, object payloads and saved chunk trees. C tests cover both byte
orders, old headers, partial normalization/publication, failure and exception
ordering, owned forests, expansion and rollback. Embedded-object display policy is
described below. `TopicGraphicsObject`, balloons, buttons, native string/display/UI representations
and final C-only host/facade removal remain conversion work. Evidence and platform
limits are in `docs/tools/playback-regression.md`.

### Help embedded-object layout and drawing policy

`help_display.c` owns native-order object-header dispatch, picture/button resource
selection, bitmap-child lookup, baseline geometry, edit-control font/measurement
sequencing, fill/draw/boxing order and cursor publication. `MMHelpDisplay` replaces
the native hidden-button field with plain C state. `help_display_host.h` supplies
typed cache/file/bitmap, font measurement and graphics primitives shared by the
Native and SDL adapters. Physical rasterization and font metrics remain providers.

Only object kind 192 is handled; arguments shorter than eight bytes and unknown
types leave bounds/cursor unchanged. Button bitmap lookup uses child `0x10000`.
Bitmap bounds align their left/bottom to zero. Drawing aligns to the live cursor
and baseline, fills nonclear backgrounds, optionally draws, releases the bitmap,
then boxes and advances. Hidden buttons still resolve, measure, fill, frame and
advance. Boxing reads style after release and foreground after setting the pen;
cursor advancement reads the live cursor after framing. Edit controls always
frame/fill, including zero/negative widths, and publish their cursor before pen/
frame/fill. The graphics environment supplies clipping; the separate caller clip
remains unused. Coordinate arithmetic explicitly wraps at 32 bits.

Fallible provider exceptions stop with partial graphics/cursor effects retained.
Private bitmap references release on exceptional exits before native rethrow,
fixing the original exceptional ownership leak. Bounds callbacks publish their
native outputs before reference release, retaining native observation order.

The offscreen engine fixture captures original per-host bounds, cursor results and
pixels for picture/button/edit/unknown/missing resources, hidden buttons, boxed
styles, background colors, nonzero bitmap origins and clipping. C tests cover
both byte orders, geometry and event order, failure/exception stops, partial owners,
late style/color/cursor reads and coordinate wrap. `TopicGraphicsObject`, balloons,
buttons, native string/display/UI representations, physical graphics/font/cache
providers and final C-only host/facade removal remain conversion work. Evidence and
platform limits are in `docs/tools/playback-regression.md`.

### Help topic interaction and script lifetime policy

`help_interaction.c` owns mouse tracking/release matching, bad-key admission,
mouse-move hot-text flags, hit/script/balloon dispatch, point-query sequencing,
cursor ancestor selection and script setup/run/release/identity checks.
`MMHelpInteraction` replaces native tracked-group/topic/flags/base-handler fields.
The base handler ID is still populated during native control construction, which
remains conversion work. `help_interaction_host.h` supplies typed virtual command,
text/point, cursor, resource, interpreter and world lookup services.

Tracking publishes adjusted command flags before querying groups, retains partial
group outputs on misses, captures flags after the virtual cursor hook, and ends
tracking before release hit testing. A hit requires both group and topic equality.
Moves adjust a local flag value without rewriting the command. Bad keys admit
handler IDs from base+1 through base+256 and ignore the virtual script's ordinary
false result. Point queries retain inherited hit testing and text lookup order.
C cursor traversal skips the topic itself and chooses the first Kidspace ancestor,
adding the child flag; otherwise it selects the application's default cursor.

Scripts clear optional return output before lookup, narrow character to its low
byte, and pass five signed 32-bit parameters. Missing scripts/interpreters leave
the topic logically alive. Ordinary run failure clears return output again.
World/grid/identity are captured after interpreter creation and before running.
Interpreter release precedes identity lookup; script release follows it. Running
may destroy the topic: subsequent services use captured data, and a false virtual
run prevents hit dispatch from accessing the owner again. Private script/interpreter
owners now release before native exception rethrow; other partial effects remain.

The headless engine fixture uses in-memory GOBs, a command-manager tracking probe
and an interpreter probe. It freezes original Native/SDL command mutations, group
matching, dispatch parameters, setup failures and actual topic destruction during
run, without OS capture/cursor/window interaction or Marcus UI tests. C checks
cover exception stops, partial owners/outputs, live command mutation, owner
destruction, cursor ancestry and publication/release order. Topic control creation,
balloon/button lifecycle, native string/display/UI representations, physical
cursor/graphics/font/cache providers and final C-only host/facade removal remain
conversion work. Evidence and platform limits are in `docs/tools/playback-regression.md`.

### Help balloon and button lifecycle policy

`help_balloon.c` owns topic override merging, balloon document loading/hiding,
duplicate-ID replacement, balloon/button factory sequencing, topic replacement/
rollback, sound-variable assignment order, target anchoring, parent pinning, topic
centering and button point/click dispatch. `MMHelpBalloon` replaces the native
topic-view pointer; `MMHelpButton` replaces native group/topic fields. Typed
document/resource, GOB/control creation, variable, geometry and graphics providers
remain in `help_balloon_host.h`.

Overrides skip nil balloon/script IDs, zero handler/target IDs and incomplete sound
pairs, while offsets add with explicit 32-bit wrap. Existing matching balloon IDs
release before replacement; a different handler type diagnoses/fails. Optional
REVIEW same-source reuse retains the original branch. Private document and
pre-initialization node owners release on exceptional exits. Once initialized,
nodes are committed: enter-state/hit callbacks may destroy them, so failure never
releases them again. Initial hit uses captured identity for the subsequent lookup.

Topic creation publishes the new view before sound assignments. Ordinary failure
releases the current new view and restores the old one; exceptions retain partial
state. Success releases the old view before natural-size and anchor work. Topic
metadata is re-read at its native stages. Anchoring uses a target's registration
point or rectangle center, maps through global/parent coordinates and subtracts
the balloon registration point. Layout applies native pinning without resizing,
including oversized rectangles, then centers the topic using late preferred sizes.
Signed halves truncate toward zero. All coordinate arithmetic wraps at 32 bits.

Buttons publish group/topic before initialization and align left/bottom after
entering their initial state. Point tests first use inherited hit testing, then
map into the parent topic and require group/topic equality. Clicks snapshot ID
before parent checking, read flags late and avoid owner access after virtual hit.

The headless fixture captures original Native/SDL GOB trees, geometry, document
references, replacement, embedded button/edit construction, saved-topic loading
and button queries/clicks. C tests cover override combinations, partial owners,
failure/exception prefixes, optional reuse, rollback/publication timing, live
metadata, parent pinning/centering and button owner destruction. Topic control
construction, physical/native string/display/UI/resource representations and final
C-only host/facade removal remain conversion work. Evidence and platform limits
are in `docs/tools/playback-regression.md`.

### Help topic control construction

`help_topic.c` owns Help topic base initialization, group scanning, maximum-group
handler reservation/publication, embedded-control header decoding, geometry,
group/target fallback, handler collision choice and edit/button creation order.
The factory now uses shared `mm_text_document_create` sequencing with no read-error
publication. `help_topic_host.h` supplies typed document, font, point, runtime
variable and control-constructor primitives.

The group scan uses the initial document captured before base initialization and
re-reads its length each iteration. Base ID resets only after scanning, publishes
before `_gidBase` assignment, and stays published on failure. The button resource
cache is captured once; object and group lookups use the current document. Ordinary
scan/group false results are ignored, while object-fetch false ends successfully.
Missing payload allocations and unsupported/truncated records skip to the next
object. Private payloads release before geometry and control callbacks, including
all exceptional exits; partial controls and public state are not rolled back.

Control words use native byte order and ignore extra bytes. Edit geometry uses
baseline plus character offset, one-pixel horizontal insets and empty-font vertical
metrics; clear edit backgrounds become white. Explicit button targets take
priority, with nil targets supplied by the group. Group IDs use base+group unless
occupied; ungrouped/colliding controls allocate a fresh handler ID. Arithmetic wraps
at 32 bits. Shared factory cleanup now releases a private topic object on an
initialization exception before native rethrow.

The headless fixture freezes original Native/SDL control trees, geometry, relative
handler IDs, target dispatch and document references with shared groups, collision
fallback, offsets and background styles. C checks cover both byte orders, extra/
short/unknown records, missing and partial owners, scan/group/fetch false semantics,
handler failures, publication, cache capture and control-prefix failure. Together
with the preceding Help slices, native `kidhelp.cpp` retains typed provider
binding, marshalling, physical reference operations and validation. Needed native
string/display/resource representations and final C-only host/facade integration
remain work. Evidence and platform limits are in `docs/tools/playback-regression.md`.

### Shared rich-text line and character-run layout

`rich_text_layout.c` owns the formatter shared by Studio text views and Marcus
captions. Plain C run/paragraph/line state controls character buffering, break
opportunities, white-space overhang, ignored characters, forced long-word breaks,
tabs, embedded objects, indentation, justification, baseline and paragraph spacing.
Its C provider supplies document/style access, font setup/measurement, character
classes and embedded-object bounds. The caller supplies storage for 128 native
one- or two-byte character units; formatting allocates no memory.

`kauai/src/rich_text_layout.h` now binds native document/graphics providers and
Marcus caption fonts to this C implementation. It retains typed character/style
storage, copies the run style and marshals native rectangles/lines. Object queries
can mutate the run style even on a miss. Provider failures stop C immediately;
the binding catches C++ exceptions at the boundary and rethrows after C returns.
The first line fields publish before the final paragraph query, preserving partial
output on failure. Reinitialization and EOF retain the original object/break flag
behavior; a fresh C run starts with defined zero flags.

Coordinates use explicit wrapping 32-bit arithmetic, and stored line fields wrap
at signed 16 bits before subsequent spacing operations. Negative division truncates
toward zero. Spacing products use C89 word arithmetic without a 64-bit integer
type. A frozen preconversion byte/UTF-16 fixture checks callback order, measured
prefixes, buffered text, runs, lines and provider-failure prefixes. C-only tests
check control flags, boundaries, objects, overflow/spacing and failures; evidence
and coverage limits are in `docs/tools/playback-regression.md`. Typed property,
font, graphics and drawing providers and final C-only host integration remain work.

### Text-box state and frame/view policy

`text_box_runtime.c` owns frame/view sequencing, scene attachment, lifetime
queries, visibility, core show/hide, selection, bounds/type changes, dirty routing
and scroll/text-selection dispatch. Stored bounds, lifetime, type and child ID
now live directly in `MMTextBoxState`; the native `TextBoxData` adapter no longer
duplicates them or marshals a temporary metadata record for persistence. Borrowed
scene and selection fields live in `MMTextBoxRuntime`. Native rectangles are
constructed only at typed boundaries. Marcus caption visibility and first-frame
scrolling use the same C predicates as Studio text boxes.

Frame changes snapshot the old view before publishing current frame. Existing
views retain their order; only a newly created border is deactivated during
playback. Visibility is re-read after creation. Hide publishes the exclusive end
before dirty notification and queries scene selection after frame/view effects.
Show retains the original strict `end < current` extension test, including its
no-extension result at equality. Scene attachment reads the new scene's frame
twice, publishing current then first, and attaches the scene last. Bounds publish
after dirty notification with a late read of the caller's native rectangle.
Lifetime queries reject only `end < first`; equal bounds form a valid empty
lifetime. The infinite-end sentinel resolves through the scene's current last
frame. Arithmetic wraps explicitly at 32 bits.

`text_box_runtime_host.h` supplies native scene/document/view primitives and catches
exceptions before returning to C. Partial state remains on failure; first lifetime
output survives a failed last-frame query. The native headless fixture freezes
viewless loaded metadata, lifetime, type, selection, bounds and frame behavior.
C providers cover view creation/reuse/release, order, callback mutation and failure
prefixes, including freeing state during frame-change release under AddressSanitizer.
Factory/edit/undo/clipboard control is covered below. Physical view implementation
and Marcus paint-order storage remain conversion work. Evidence and limits are in
`docs/tools/playback-regression.md`.

### Text-box document transactions, undo and clipboard

`text_box_edit.c` owns new/read factories, whole-document duplication, type/color
and show/hide edit transactions, movie/parent undo registration and clearing,
scene-index lookup, selection property application and formatting-mask queries.
It also owns clipboard reference adoption/disposal and paste placement, insertion,
selection and mouse attachment. `text_box_undo.c` owns replay and wrapped-record
disposal. All five native undo classes now store their saved type, rectangle,
lifetime, background or wrapped document record in `MMTextBoxUndo`; clipboard
references live in `MMTextBoxClipboard`.

The text-box clipboard view controller in `text_box_edit.c` chooses inline
versus whole-box operations, determines paste availability, and forwards full
actor paste to the movie view. It rejects route-only clips in text mode and
collapses the inline selection after a pointer copy. Studio supplies command
IDs, rich-text view calls, and native clipboard and cue services. A clipboard
document adopts the duplicate before a whole-box cut hides the source, so a
failed hide releases the duplicate through the clipboard document once.
Studio's before-and-after traces matched for inline cut, copy and paste,
actor forwarding, and whole-box paste and cut through undo, redo, and
save/reopen. Retail rendered the two saved movies identically. The pasted
caption appeared within its box at frames 7 and 14 and was absent at frame 6.

Factories initialize before reading input bounds, attach scene/type before clearing
the background, and set one-level document undo last. Read errors publish the bad-
file error only for an ordinary metadata failure. Duplication publishes its new
document before suspension/replacement; failure releases before nulling output.
Defaults copy before undo resumes, then background and type copy. Lifetimes and
scene ownership are not copied. Exceptions retain the original completed prefix
without adding owner cleanup/rollback to those paths.

Edit operations register undo before applying. Show retains its success-only movie
undo clearing; early hide delegates to scene removal. Wrapped edit registration
keeps the original movie-before-parent order and asymmetric failure cleanup.
Replay switches scene, seeks and resolves the indexed text box. Initial ordinary
failures flush then clear movie undo; that clear may destroy the running record.
Type/color saved values swap before selection, size/lifetime after selection.
The final hide-replay seek still ignores an ordinary false result. Wrapped edit
false returns without the initial-failure cleanup. Disposal clears parent document
undo only when the owning scene is current, then releases its retained record.

Selection changes build the original cleared properties and complement masks in
C; selection queries preserve first-character formatting and clear only font,
height, bold and italic consistency bits. Paste snapshots workspace dimensions
before duplicating, shifts to `(177,78)`, intersects the client workspace and
normalizes empty rectangles to zero. Coordinate arithmetic wraps at 32 bits.

`text_box_edit_host.h` retains typed allocation, reference, document, scene, view,
color/property marshalling and access to the shared C MovieUndo context. The native
fixture freezes duplication, clipboard adoption, no-view queries and real type/color/text
undo/redo without windows. C probes cover other view-dependent paths, geometry,
partial publication and ordinary/exception prefixes; clearing movie undo frees
the running C record under AddressSanitizer. Physical views,
Marcus paint-order storage and final C-only host/facade integration remain
work. Evidence and coverage limits are in `docs/tools/playback-regression.md`.

### Movie and actor undo context/storage

`movie_undo.c` owns common undo context initialization, registration publication
and clearing/notification. Every native MovieUndo now embeds
`MMMovieUndoContext` directly: a borrowed movie handle and saved scene/frame.
Actor and composite undo states borrow that stable context instead of rebuilding
and publishing native-field copies on each call. Text-box and scene undo adapters
read the same C context through typed getters.

Registration publishes movie, queries/publishes scene, validates, then queries the
scene again before reading/publishing frame. An absent second scene preserves
the existing frame. Commit and clear can destroy the record; notification follows
without further record access. MovieSceneUndo retains its separate operation
target index; registering the common context must not overwrite that index.

Actor undo keeps its snapshot reference, ID, flags, last frame and counted name
in `MMActorUndoState`. Names reuse `MMMovieTitle` storage for up to 255 native
one/two-byte units, including embedded zeros. The actor undo controller works
directly with that record and counted roll-call names. `MMActorMoveGroup` embeds
its child storage and `MMActorRenameGroup` embeds its counted-name table.
Actor/move toggle snapshots the base context on entry, so child
callbacks cannot change the context used by later operations in that toggle.
Sooner/later redo temporarily replaces the shared frame, dispatches virtual undo
and restores the saved frame.

The common tests check publication order, frame preservation, distinct operation
indices and commit/clear destruction. Actor/group tests cover direct persistent
storage, counted names, temporary-frame restoration and child context mutation.
Original actor/group/movie traces remain the integration reference on Native
Debug, SDL Release and SDL Debug. Native object lifetimes, physical views and
final C-only facade integration remain work.

### Movie scene-container control and undo

`movie_scene.c` owns the current scene reference/index and the control flow for
close, switch, new/insert, remove, undoable remove and scene undo/redo. Native
Movie methods validate typed inputs and supply Scene, file, reference, sound,
client, undo-list and memory-pressure services. Scene storage and native undo
objects remain temporary representations; the callback operations use the C
Scene and collection modules described above.
Composition still publishes its reordered index directly into this state through
the temporary native facade; its broader transaction has not been converted.

Switching autosaves before closing, stops looping wave sounds, and retries the
previous scene after read/start/first-frame failure. Scene publication precedes
the scene-change callback; the index publishes afterward. Read/start exhaustion
notifies with an empty owner, whereas first-frame exhaustion has already notified
with the scene. Child IDs move backward for insertion and forward for removal.
Successful insertion ignores archive-save and roll-call-add failures, updates
dirty/client state, then plays or flushes preview sound.

Failure prefixes deliberately retain the original behavior: failed adoption
keeps the incremented scene count; failure opening the inserted child decrements
it, removes the child, saves and attempts another scene. Removing the final scene
returns before marking dirty. A failed undo allocation returns success without
removing the scene; read/start/hide work for an unopened scene has already occurred.
Undo-list commit failure releases both owners, and failed
core removal clears the committed undo. Scene redo creates a new scene and sets
its saved background, while undo of removal inserts its retained snapshot.
Failure flushes sound before clearing the undo list, which may destroy the record.

The existing headless Movie save/load fixture checks insertion/deletion at scene
boundaries, content identity as child IDs shift, nil/same-scene switching,
undo/redo and callback publication order against its original native and SDL
debug/release trace. C-only tests exercise transaction failures, old-scene retry,
partial publication, reference balance and undo failure/notification order.
No Marcus UI tests were added. Background-driven creation, common Movie edits/
undo, autosave/save and composition are covered below. Final facade integration
remains work. Verification and retail coverage
limits are recorded in `docs/tools/playback-regression.md`.

### Movie autosave, save publication and garbage collection

`movie_save.c` owns save flags and the saved-file reference, temporary-file and
zero-cache working-resource setup, scene/roll-call/source replacement, rollback,
roll-call pruning, save-as selection and named/VMM save publication. Native Movie
methods provide typed file/path/block, Scene, registry, client, tag and packaging
services. Public save state and reference decisions use C; physical files and
container sessions retain their host representations. Native construction still
publishes loaded name/read-only metadata into the C state. DocumentBase's common
dirty flag remains a host field written by the C controller.

Old children survive until title writing succeeds. Earlier failure removes new
source, roll-call and scene links in that order; actor pruning remains committed.
After old children are removed, physical save failure retains the new graph.
C releases the source-name block after ending the long operation and before a
nested autosave returns to format selection, packaging or save publication.
Named saving releases a matching saved-file reference before saving and attempts
reacquisition even on failure. Direct binding failure retains the previous
name-valid/read-only flags, while autosave clears name-valid before reacquisition.
Ensuring resources publishes the borrowed resource output before a later
temporary-file failure. The original optional BUG1848 retry is C-controlled and
bounded to one retry; it remains disabled in the current native configuration.

VMM format selection flushes current scene references before tag inspection.
Failed flush/tag collection conservatively requires a container. A bundled/named
container requires VMM; ordinary native movies retain the retail save path. VMM
saving writes a private working archive first and commits the public filename
after successful host packaging. Packaging failure restores the display title
while retaining the completed working-file save. File/path and ZIP packaging
implementations are unchanged.

C also collects tags across contiguous stored scene child IDs, gathers unused
materials/templates before deleting any, and owns the deletion-list storage.
Only deletions from the current archive toss its resource cache. Empty-tag
scanning now uses an explicit match flag, removing an uninitialized temporary
read while preserving the intended deletion set. Closing-time sound collection
runs even after asset collection failure. Sound children are traversed backward;
shared sounds survive, and unattached sound chunks are purged without a prompt.
Native DynamicArray blacklist and unused native control wrappers were removed.

The existing headless Movie save/load fixture captures flags, file state and
ordered chunk graphs across sixteen native/VMM save scenarios, including
packaging failure, clean autosave, close-time autosave, copy/remembered names and
working-file conversion. Its packaging service is a deterministic test double;
it verifies control/publication, not ZIP contents. C-only checks exercise each
replacement failure, irreversible prefixes, output/reference publication, retry
limits, conservative format selection, deferred asset deletion, cache scope and
sound purge decisions. No Marcus UI tests were added. Common Movie editing/undo,
background-driven creation, further edits and composition are covered below.
Final C-only host/facade integration remains work. Evidence and coverage limits are
recorded in
`docs/tools/playback-regression.md`.

### Movie roll-call ownership, actor editing and common undo

`movie_rollcall.c` owns actor lookup, row/name queries, rename notification,
browser classification, actor-ID publication, tag refresh and reference-count
updates. Its native services read/write the existing C-owned roll-call table
directly and marshal SDK strings/tags. Actor IDs remain assigned after failed
insertion. Existing rows increment before tag refresh; different tags close the
old owner before duplicating the new one. Rename and successful add always notify;
removal notifies only when the reference count reaches zero. Last-reference
deletion closes the tag before erasing the row. ChangeActrTag retains its shallow
replacement contract, with ownership arranged by its caller.

Failed insertion now closes the tag opened for its uncommitted row. The original
native path leaked that cache reference. Successful behavior and the assigned-ID
failure prefix remain unchanged. Opening a tag without copying is a must-succeed
service, retaining the native diagnostic. Counter arithmetic uses explicit
32-bit word wrapping through the shared C arithmetic helpers.

`movie_edit.c` owns selected-actor queries, choose/insert/onstage control,
rotation/scale/pull/time/costume/sound edit sequencing, camera/pause/transition/
background-sound dispatch, actor/path paste, background-driven scene creation and
common undo registration/notification. Underlying Actor/Scene operations already
use the C controllers described above; native services provide typed operations,
view placement, client notifications and undo factories/list primitives.

Missing first views reject choose/paste before actor mutation. Offstage choice
duplicates with reset, brings the actor onstage, selects it, refreshes roll call,
starts placement, transfers the snapshot owner, then selects again and invalidates.
Ordinary actor paste places all subroutes. Path paste duplicates without reset,
rereads selection after nested operations, and restores only after undo creation
fails; failed route application retains its own completed prefix. Zero squash
does not edit or mark dirty. Time shifts use shared fixed arithmetic and update
the latest undo frame only for a matching actor/time undo. Nullable sound/costume
tags and tri-state flags retain their SDK meaning. Missing sound metadata is
fetched selectively and released before Scene sound editing.

Background-driven creation preserves cache/long-operation order, default-title
publication, empty-scene overwrite, preview sound before undo creation, and new
scene removal after background/undo failure. Completed prefixes are not rolled
back implicitly. Common undo binds movie/scene/frame before committing and setting
the client mode; an absent scene leaves the saved frame unchanged. ClearUndo
clears the host list before publishing its disabled state. Native undo objects
and list ownership remain temporary host representations.

The existing headless Movie save/load fixture freezes native/SDL actor state,
roll-call rows/names/cache references, dirty flags and client/undo notifications
across sixteen scenarios. C-only tests cover failure sequencing, snapshot/ref
transfer, roll-call append cleanup, selection changes during path paste, sound
defaults and scene creation rollback. No Marcus UI tests were added. Movie
caption/text/template/material transactions and composition are covered below.
Final facade/host integration remains work. Verification and retail coverage limits
are recorded in
`docs/tools/playback-regression.md`.

### Movie material, 3-D text and caption transactions

`movie_content.c` owns material insertion, 3-D text authoring/insertion and selected
actor template replacement, caption insertion/removal/hiding and indexed caption
selection. It reuses `movie_edit.c` for actor insertion/removal and snapshot
services, and `movie_rollcall.c` for the new/old actor-ID handoff. Native services
marshal strings, tags and caption bounds and provide C-backed resource/template/
Actor/Scene primitives, the client default font and interactive mouse attachment.

Material insertion publishes its tag only after writing succeeds, including the
failed-open prefix. Earlier failure leaves the C output unchanged; native wrappers
retain their debug poisoning. 3-D text creates its template before preparing the
working file, captures that file before switching to temporary storage, releases
the template after writing and opens the authored tag against current resources.
Failed edits retain already-written chunks. Temporary templates now release after
resource/temporary-file/write failure, and failed actor insertion closes its opened
template tag; the original native paths leaked those references.

Changing all-space or empty 3-D text removes the selected actor without reading
the font input; tabs and other characters remain content. Nonblank replacement
duplicates the actor, changes its
template, assigns a fresh actor ID/roll-call row, then removes the old reference
without deleting its row. Failed row insertion restores the snapshot while keeping
the consumed Movie ID limit. Template-change failure retains its completed prefix.
Tag/snapshot cleanup and notification/dirty order follow the original transaction.

Caption insertion releases its factory reference after add/select/attach. Failed
add now returns immediately after release, fixing the original subsequent null
selection/mouse-attachment call. Indexed selection now advances through stored
captions and selects the requested visible caption. The old implementation never
advanced the storage index, returned before selection or looped indefinitely.
Negative/missing visible indexes preserve selection. These are intentional fixes
to broken editor paths, not claims of retail failure equivalence.

The existing headless Movie save/load fixture freezes material/3-D-text and caption
hide/remove results across eight scenarios using a synthetic positive-source font
and placement background. It constructs the caption insertion prefix without mouse
attachment; C tests cover full insertion, failures and visible-index selection.
No Marcus UI tests were added. Movie composition is covered below; final C-only
host/facade integration remains. Verification and coverage limits are in
`docs/tools/playback-regression.md`.

### Movie composition and imported actor remapping

`movie_compose.c` owns descriptor collection and insertion rollback, thumbnail/
cache reference transfer, reverse composite disposal, storyboard application,
temporary imported Movie cleanup, roll-call row ownership transfer and actor-ID
remapping. One C collection owns the two descriptor arrays and their counts.
The Studio sorter reads scene rows and uses C89 to move a row, stage a removal,
or set a transition. Native services provide file, thumbnail, scene and client
primitives.

Descriptor collection retains the BUG1929 temporary-file-before-autosave order
when enabled. Working resources are a precondition for that legacy branch.
Collection captures the original scene count, reads unopened transitions and
thumbnails from file, borrows/retains the current thumbnail, and transfers one
reference into each inserted descriptor. Storage is reserved before descriptor
collection. A failed add releases the thumbnails it acquired, removes its scene
prefix, and preserves the prior descriptors and insertion position. Autosave's
effects are retained.

Application keeps just the cloned root numbers in temporary C storage. A
composition of local scenes needs no clone table. The engine clones the external
Movie graphs and merges their roll calls before adopting, renumbering or deleting
scenes and writing transitions. Imported actor-start and registration errors remain
diagnostic and non-fatal; tool notifications and Scene release still occur.
Current-scene removal uses the C Movie scene-owner helper. Final count/ID-limit,
dirty, SceneNuked, view and roll-call publication retain their original order.
Completed scene/file/actor prefixes are not implicitly rolled back on failure.
Copied Movie roots and their retained caches release in reverse order.

Failed roll-call merge now deletes its uncommitted cloned Movie root, fixing the
original path's omission from its cleanup prefix. Kept current scenes now follow
their live child slot, excluding deleted descriptors, rather than their raw
descriptor index. This fixes the documented case of deleted descriptors appearing
before a kept current scene; the old host could publish an invalid current index.
A headless native/SDL check exercises that case. These fixes do not claim retail
editor/failure equivalence.

Roll-call merging transfers each loaded tag owner into its appended row, offsets
IDs with explicit 32-bit wrapping and clears imported reference counts. Failure
closes all loaded owners exactly once and removes its appended rows; actor-file
edits already completed are retained for the caller to discard with the clone.
The C chunk walker and C-owned visited-ID storage replace the native enumerator
and temporary DynamicArray. Shared actors adjust once, including nested actor
chunks: the native loop computed a skip flag but never passed it to its walker.
Traversal error flags remain non-fatal as before; visited-set or actor-write
failure aborts. Removing a C++ temporary allocation changes allocator-attempt
ordinals, not the successful traversal contract.

The headless Movie save/load fixture freezes eight composition scenarios plus
four native/foreign-order shared/nested actor-remap scenarios before conversion.
C tests cover collection/apply failure prefixes, cleanup, reference transfer,
non-fatal imports, wrapped limits, traversal allocation errors and reverse disposal.
Final native resource and object facade removal and C-only host integration
remain. Verification and coverage limits are
in `docs/tools/playback-regression.md`.

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
groups and releases only partial output on failure. The shared graph readers
transfer typed tables on success and distinguish invalid event data from
allocation failures. Decoding leaves tags unopened with null runtime cache
pointers. Encoding zeros wire pointer slots and ignores cache identity.

`actor_persistence.c` loads the live Studio actor into its C runtime and track.
It resolves an embedded template's ACTR link with the header tag as fallback,
loads both tracks before opening tags, opens the template tag before event tags,
and creates motion sound storage before fetching the template. An older actor
with a first frame and no stored last frame refreshes its lifetime after the
template is available. On failure the actor owner releases the partial record.
Studio and preview share the typed track decoders, and scene reframing uses the
same C path decoder and storage.

The C store reads the current runtime and C track directly. It refreshes the
lifetime, writes or links the template, writes ACTR and PATH, encodes and writes
GGAE with embedded material links, then links available scene sounds. The caller
owns cleanup of the candidate archive after a partial save. Native services
supply resource lookups, chunk creation, byte I/O and diagnostics.

`engine-actor-data-test` covers old/current headers, partial failed reads, invalid
byte-order outputs, ID overflow, empty/foreign routes, all event layouts, Add padding,
tag fields, allocation/insertion failures at every row, and partial live load
cleanup after track I/O, sound storage, template fetch and lifetime failures.
The live load test also covers the embedded-template fallback and 40-byte
header recovery. Thirty-five standalone
C checks, forty-eight focused native checks, MSVC AddressSanitizer, eighteen binary
snapshots and all 51 CLI baselines pass. The new header/all-event snapshot is 2,896
bytes. No Marcus UI tests were added.

### Actor track state and embedded tag ownership

`actor_track.c` owns the route/event lifecycle for `MMActorTrack`. Studio's
actor owner embeds that record. Marcus preview keeps the authoritative
handles in its C actor and borrows typed views for editing and export.
The C++ `ActorTrack` fixture is used only in tests. Studio and Marcus load
PATH and GGAE into C89-owned rows. The C tables hold reservations and support mutation and
cloning. Studio's actor saves and Marcus's animation exports write the
legacy array and group layouts through the C writers. The standalone
archive editor shares the PATH layout and uses the same GGAE table and
writer. The track owner calls the typed C tables directly for allocation,
growth, cloning and disposal. Studio and Marcus use one C table reader for
actor evaluation. The native host supplies heap and block I/O services
and retains or releases resource-cache handles on request.

C creates event tables with 20 fixed bytes, ten initial rows and twenty variable
bytes, and route tables with 16-byte rows, 32 initial rows and minimum growth 32.
Creation and cloning publish only after success, then dispose old state. Clone
copies route first and acquires embedded tag references immediately after copying
event bytes. Allocation failure leaves the destination and its references intact.
Self-clone and self-swap are safe. Empty and partially populated tracks are accepted.

C identifies Costume tags only when the custom flag is zero, and Sound tags for
all Sound rows. It copies unaligned tag bytes to aligned local storage before
access. Only sid-zero, non-null caches own references. The reader opens event
tags in order and closes the completed prefix in reverse on failure. Release
clears cache pointers in their rows and tolerates repeated close calls.
Studio and Marcus use the same per-row retain and close operations for
costume and sound edits. Cleanup closes tags before releasing
event tables, then route tables. Adoption transfers a handle and disposes the old
table; adopting the current handle is a no-op. Callbacks cannot mutate/re-enter the
participating tracks.

The actor route, subroute, stage and recording traces match the previous build
byte for byte. Sixteen of 128 Debug actor ownership cases change when the
requested native allocation fault targets the replaced path handle. Cases that
succeed in both builds produce identical records.

Moving the event table changes sixteen of 128 Debug ownership cases and
two of 120 creation cases that targeted native allocations. The edit,
sound-edit, route, subroute, stage and recording traces match the prior
build byte for byte. The all-event persistence fixture exercises both
byte orders through the C writer and reader.

Marcus derives animation markers and spans from a C track clone held for
one synchronous metadata replay. C releases the clone on success, allocation
failure or a failed presentation callback. Source events and live pose stay
intact. For authoring drafts, C installs cloned route, event and motion-sound
tables and a duplicate body. Closing a draft restores the original handles
and evaluated state. Live event-track replacement clones the route, decodes
the new event table and replays the frame. Failure restores the original
track, actor state and costume. The portable C session owns the same
typed route and event tables and uses a checked route view during playback.
An out-of-range node read sets the bad-file error and returns a zero point
to the evaluator. The session disposes the track after the evaluation.

The ownership, creation, all-event persistence, clone, edit, stage
and recording snapshots match the immediately previous build byte for byte.

`engine-actor-track-test` uses the live C tables and covers every event kind,
unaligned tags, malformed widths, copy failure and reverse prefix cleanup,
allocation failures, tag reference order, adoption and partial tracks. The
preview metadata test uses those same tables to exercise clone allocation
failures, callback failures and replay limits. Thirty-six
standalone C checks, forty-nine focused native checks and MSVC AddressSanitizer
pass. Nineteen binary snapshots and all 51 CLI baselines match, including a new
192-byte native track/clone snapshot and the prior eighteen snapshots. No Marcus
UI tests were added.

### Actor value state and route evaluation

`actor_state.c` owns state initialization, transform reset, route rewind, origin
updates and pure value-event application over the public `MMActorState` C
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
adapter asserts the fixed-point ABI and native route/event layouts.
Studio's `ActorFrameState` adds native convenience methods to the stored
`MMActorState`. Route points and locations use the C types. Native edit
transactions can still stage copies of the C record for rollback and
callback publication. Native body effects and table services remain.

`engine-actor-motion-test` covers fixed-point boundary cases, subroute termination,
clamping, output aliasing, rewind preservation, all pure event kinds, deferred runtime
kinds and invalid layouts/locations. Thirty-seven standalone C checks, fifty focused
native checks and MSVC AddressSanitizer pass. Native checks compare 4,096 arithmetic
pairs against BRender and match new 33,384-byte route/arithmetic and 4,142-byte state
snapshots. All twenty-one binary snapshots and 51 CLI baselines match. No Marcus UI
tests were added.

### Actor frame command sequencing

`actor_frame.c` owns one-frame sequencing in `MMActorFrame`. C advances the
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

`actor_seek.c` owns control state in `MMActorSeek`: backward/midpath rewind,
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

Studio's C actor owner embeds the C sound list alongside the authored track.
Creation and loading reserve sound storage directly. Duplication and undo clone
or swap it in C, and teardown disposes it after releasing the template. Marcus
preview uses the same C sound list for playback and draft edits. Native resource
and audio implementations remain migration work.

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
insert/merge/apply operation used by actor edits. The shared C table binding
reserves storage, inserts and deletes rows, writes and copies payloads, and
retains and closes row tags. SDL Studio, Marcus preview costume and animation
drafts, and the C actor copy and paste paths use that binding. Host services
provide scene notifications and resource and renderer effects during event
application.

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

`actor_placement.c` also owns Studio's pixel deltas, arrow-key depth,
mouse projection and depth scaling. Studio supplies pointer coordinates, key
states, clock samples, the active XY or XZ basis, and the camera matrix. C
converts pixel motion to signed 16.16, adds or subtracts depth at 100 pixels per
second, applies the recording or editing scale, and applies the existing
minimum depth multiplier. Placement moves use the C actor owner with the
appropriate scene or subroute flag, orientation and optional floor clipping.

The placement record retains the active actor and owns any replacement snapshot.
Begin temporarily removes the actor's roll-call reference. Forced end restores
that reference using the retained actor after scene selection has been cleared.
On acceptance, C registers a scene add or replacement undo. A rejected
placement restores the old snapshot or removes the newly placed actor. Failed
undo allocation releases the replacement snapshot once. Failed registration
preserves the accepted edit and clears undo. Studio retains cursor capture,
tool switches and view notifications.

`engine-placement-test` checks single-coordinate authored points, camera query
counts, floor/text policy, the exact 25-unit boundary, fractional midpoint
rounding, matrix translation, scalar wrapping, mouse projection and placement
ownership on allocation and registration failure. The native save/load test
compares 4,096 authored/fallback/text/clipping/matrix combinations against the
original BRender formula. All 12 native actor traces and the Scene-content trace
matched. Routed Studio pointer commands verified subroute and whole-scene
placement, replacement, cancellation and out-of-view rejection against the
old build. Rejection removed a new actor and restored an existing actor's
snapshot. The new build also handled removal during active placement. All
75 chunk entries, links and payloads matched in each of the three saved movies.
Retail opened those movies with matching positions and poses at frame 7. All
eight Studio group-transform workflows also matched the previous build.

Fresh SDL and Marcus seek and playback runs (`20261120T023247Z-9e1af531`)
matched all 60 scene and frame markers in each mode against the retained
retail `actor-event-edit` capture. Each port's 60 seek image hashes matched
its previous build (`20261120T020904Z-3802926f`), with camera indices matching
retail. The 12 SDL playback sound events also matched retail.

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
begin/move/end operations. `MMActorRecording` carries insertion location,
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

Studio's record gesture also uses `actor_record.c`. C reapplies the current
action and cel through the actor edit transaction, keeps the rollback snapshot,
and initializes the insertion timestamp after the action edit. It owns the
166 ms route sample cadence, 498 ms cel cycle cadence, freeze/ground flags, and
temporary rerecord mode selected by Shift and Command. Releasing Command stops
cel cycling and restarts the sample clock. Replace mode trims the tail after
beginning a route. Cursor capture, redraw, and Studio sound and tool controls
are host services. A failed action edit leaves no snapshot and suppresses
subsequent route sampling. Ending a gesture without a selected actor and closing
the view both release any retained snapshot.

`engine-record-test` covers thresholds and timestamp wrap, record-step failures,
segment splitting, removed stubs, lifetime failure, reservation short-circuiting,
written/untouched outputs, frame-only waits, sampling, clipping, smoothing overflow,
gap extension, seek rollback and aliased-output assignment masks. It also checks
tag ownership through merging/deletion, Add boundaries, epsilon joins, end failure
cleanup and poisoned table handles after restoration. Gesture checks cover both
cadence boundaries across timestamp rollover, cycling-to-recording order,
temporary rerecord, missed action/route starts, snapshot release, and the small
mouse delta filter for keyboard-directed movement. A 439,824-byte native-before
trace across 640 scenarios matches exactly, including complete recording sequences
and real undo restoration. An additional native aliased-output check passes.

Routed Studio motion fill, cel cycling, record and rerecord workflows matched
the previous build, including undo/redo state and all saved chunk entries, links
and payloads. Record and rerecord advanced the playhead from frame 7 to frame 9.
Redo retained the action transaction's frame 7, and reopening at frame 9
reproduced the edited pose. Retail reopened all four movies with matching actor
positions and pose matrices over frames 7 through 9. All 12 native actor traces
and the Scene-content trace matched the previous build. The C89 recording test
passed on x86 and x64.

Fresh SDL and Marcus runs (`20261120T024627Z-031ce727`) matched the retained
retail `actor-event-edit` capture's 60 scene and frame markers in both seek and
playback. The 60 seek image hashes per port matched the previous build. Camera
indices and all 12 SDL playback sound events also matched retail.

Transform/time edits are described next. Undo, queue/runtime integration,
body/resource implementations and final facade removal remain unfinished.

### Actor transform and time edits

`actor_transform_edit.c` owns local/world rotation, normalization, uniform scale,
stretch, route tweaks, composed movement and sooner/later shifting. Rotate,
scale, stretch, tweak and move publish the actor owner's state in C89.
Matrix composition, path-orientation removal, transform fill, event sequencing,
bounds, ground policy, lifetime/range decisions and partial failures execute in
the C-only library.

Local rotations retain pre-X/Y/Z order; world rotations retain swapped X/Z
post-rotation and BRender low-precision normalization. Zero world rotation returns
before reservation or normalization. Reverting still reserves three events even
with no requested flags. Scale/stretch bounds have exclusive upper endpoints;
scale's tweak-removal and position-refresh condition compares the new absolute
scale with the clamped factor, preserving the native comparison rather than
substituting the previous scale. Rotation/revert retain diagnostic-and-continue
ordering after a supposedly reserved event fails; scale/stretch/tweak return
failure with preceding state/table mutations retained.

Studio's drag controller in `movie_edit.c` builds move, tweak, axis rotation,
resize and squash requests. It reads the modifier state captured at pointer down,
uses the frozen pivot for group transforms and dispatches single-actor edits
through the C movie editor. It preserves BRender's 0.001 multiplier and Y-axis
sign, including wrap on conversion to a 16-bit angle. A resize or squash plays
the corresponding UI sound on its first nonzero tick and when direction changes.
The controller commits the pending undo at the original edit boundary and calls
the view services in the original order. Studio supplies cursor, sound and
redraw callbacks.

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
The C actor owner publishes the resulting runtime state and owns the selection
highlight flag. The Studio controls send signed steps to the C movie editor,
which resolves the target frame and updates the latest Sooner/Later undo's redo
frame after a successful shift. Missing scene or selection leaves the edit and
undo history untouched.

The Sooner/Later tool uses the same C snapshot capture as the drag tools. On
mouse release it registers one reset actor snapshot before the first shift.
Subsequent shifts reuse that entry. Allocation or registration failure clears
history, reports the edit as non-undoable and releases any local undo handle.
Studio controls the buttons and redraws the frozen actor highlight.

Native callbacks supply BRender distance/normalization primitives, scene/body
effects, cached-origin writes and host scrollbar notification. BRender remains
the existing external C dependency; those callbacks do not retain edit policy.
Project-owned body/resource/scene implementations and native ownership adapters
still need conversion.

`engine-movie-edit-test` checks drag angle and scale calculations, sound direction
changes, single-actor undo order, and the cursor/redraw effects after a failed
edit. `engine-placement-test` checks pixel sign and wrap, keyboard elapsed-time
handling, and opposing arrow keys. All 262,144 keyboard-depth samples matched
native BRender arithmetic across the low 16-bit elapsed-time range and all four
arrow-key combinations.

`engine-transform-edit-test` covers exclusive bounds, the scale/tweak comparison,
reservation/failure order, zero world rotation, revert, primitive normalization,
moved publication, clipped movement, translation precedence, text/orient handling,
partial orientation failure, costume ownership, gap shifting, failed lifetime
queries and failed seeks. The native-before trace has 1,032,004 bytes across 1,152
scenarios and matches exactly, including all transformed matrices, event payloads,
origin caches, frame/range values and results. C actor-owner and movie-edit tests
cover time snapshot ownership, allocation and registration failure order,
relative stepping at the signed-frame boundaries, and absent scene/selection.
All 12 actor traces, the Scene-content trace and the 61,552-byte native Movie
edit trace matched the previous build. All eight Studio group-transform
workflows also matched after sharing the snapshot capture.

Twenty-six routed Studio transform workflows matched the previous build. They
covered single-actor and group movement, subroute movement, tweak, rotation on
all three axes with and without Command, resize, squash and direction reversals.
Undo, redo and reopen matched in every case. The 72 chunk entries, links and
payloads matched in each saved movie. Raw file differences occurred only in
unused index padding. Retail reopened all 26 movies with matching evaluated
positions, action/cel values and fixed-point actor pose matrices at frame 7.

Fresh SDL and Marcus runs (`20261120T031529Z-78f351e8`) matched all 60 scene
and frame markers in each seek and playback mode against the retained retail
`actor-event-edit` capture. Each port's seek images matched its previous build.
Camera indices and all 12 SDL playback sound events matched retail.

Four Studio Sooner/Later workflows matched the previous build. They exercised
three earlier shifts, three later shifts, mixed directions and exiting without
a shift. Undo returned to frame 7 in each case. Redo returned to the final
shifted frame, and all 72 chunk entries, links and payloads matched in each
saved movie. The raw save differences were 7 to 12 bytes of unused index row
padding per movie. Retail reopened all four movies with the same actor poses and
positions on the destination frame and both neighbors. Studio also handled
both queued shift commands with no actor selected.

Fresh SDL and Marcus runs (`20261120T030115Z-aee3751c`) matched all 60 scene
and frame markers in each seek and playback run against the retained retail
`actor-event-edit` capture. Seek image hashes matched the previous build in
both ports. Camera indices and all 12 SDL playback sound events matched retail.

Queue/runtime integration, body/resource implementations and final facade
removal remain unfinished.

### Actor ownership and restoration

`actor_owner.c` owns runtime initialization/disposal, duplication, reset, value
copying, track and sound swaps, and undo restoration. `MMActorOwner` stores the
runtime, authored track and motion sounds. Its runtime holds evaluated state,
recording state, lifetime, origin and resource handles. Creation, scene binding,
paste and undo update it directly. The native actor supplies typed access to
resources and scene and view primitives.

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

`engine-actor-owner-test` injects allocation failures in the live C track and
sound tables and checks clone cleanup, output timing, resource reference balance,
reset, independent copies, restore swaps and ignored seek failure.
The twelve native ownership, creation, undo, persistence, edit and
recording traces match the prior build byte for byte.
Native traces cover 128 ownership scenarios and 32 real movie/scene undo cases,
including undo creation, undo/redo replacement, name swapping and removal.
They establish ownership integration behavior. The undo controller is described
next; body/resource/scene policy and final facade removal remain unfinished.

### Actor undo controller

`actor_undo.c` owns actor undo creation, snapshot reference lifetime, undo toggle
and redo frame switching. `MMActorUndoState` persistently holds a shared base
context pointer, snapshot handle, identifier, last frame, flags and counted name.
The factory returns the initialized C record alongside its native registration
handle. C retains the snapshot and populates its ID, sound flag and optional name
before registration. The native object supplies primitive movie/scene operations
and exposes the roll-call name as borrowed counted units. C copies the name
before renaming, then stores the captured value even when rename fails. The C
scratch value preserves native code units, including embedded zeros.

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
set/dispose and allocation/registration failure during creation. It also checks
name swaps when the roll-call buffer is overwritten, including a two-byte
255-unit name and embedded zeros. Twelve native actor traces stayed byte
identical, including the 3,240-byte undo trace (32 cases), the 2,100-byte
composite undo trace and the 94,208-byte ownership trace (128 cases). SDL Studio
route paste, undo, redo and save/reopen were verified. Retail reopened the saved
route with matching poses at frames 7, 10 and 14. Capture suite
`20261120T011928Z-4cdc20d9` matched the retained retail capture's 60 scene/frame
markers in each port's seek and playback runs. Both ports' seek image hashes
matched their previous builds, and SDL's 12 playback sound events matched retail.

### Composite actor undo

`actor_group_undo.c` owns composite storage and its lifetime. Both tables start
empty and allocate their buffers on first append. Move groups use the shared
dense array and retain before append, releasing the reference if growth fails.
Their toggle rereads the count each iteration, skips NULL rows, publishes the
entry movie/scene/frame context onto each child immediately before virtual undo,
and returns when a child fails and may have destroyed the group. Destruction
releases child references before their storage.

Rename groups use the same dense counted-string storage as the movie roll call.
Each row contains a counted name and a 32-bit actor ID. Replay captures the
initial row count, skips missing actors, copies the outgoing name before the
roll-call mutation and stops when a rename fails. A failed allocation to save
the outgoing name preserves the already-applied rename and the original entry.
Duplicate actor rows replay in insertion order. Both redo adapters delegate to
virtual undo.

Native callbacks supply child reference lifetime and virtual dispatch, roll-call
lookup and rename, and Kauai buffer allocation. The C tests cover append failures,
an undo that destroys its group, appended children during traversal, embedded
zeros, two-byte names, duplicate IDs and partial rename or storage failure.
Eleven of the twelve native actor traces matched byte for byte. The 2,100-byte
composite trace changed in the two debug factory cases that targeted the removed
table-object allocations. Its 16 child-dispatch scenarios and eight movie rename
scenarios matched. SDL Studio tagged and untagged seven actors, undid and redid
both operations, and reopened the saved movie with the same names. Retail opened
the saved movie with matching actor IDs, actions, cels and positions at frame 7.
The native movie fixture verified both actor identities and scales through a
single two-actor undo and redo. The headless Studio drag probe produced zero
actor moves on both builds.

Capture suite `20261120T013059Z-f3e178a8` matched the retained retail capture's
60 scene/frame markers in each port's seek and playback runs. Both ports' seek
image hashes matched their previous builds. SDL's 12 sound events matched retail.

### Actor drag undo capture

The drag record in `actor_group_undo.c` owns pending single and composite undo
handles and the frozen group pivot. It reads the selected actors from the C
scene selection and captures detached snapshots through the C actor owner.
Each child takes the reference returned by duplication. The group becomes
pending after all children are captured. Failure releases the captured prefix
before clearing the movie's undo list. The visible-selection centroid supplies
the rotate, resize and squash pivot, with the origin as the fallback.

The view commits on the first edit that its tool reports as successful and
discards the remaining snapshots at the end of tracking or on destruction.
The shift-drag retarget path discards the old snapshots before capturing the
new actor. Capture failure also releases an allocated single undo whose actor
could not be duplicated.

Twelve native actor traces matched the prior build byte for byte. The native
movie fixture injected five allocation failures, checked template references,
discarded a single snapshot and verified grouped undo and redo. A headless SDL
probe queued held-pointer commands directly to the movie view. A click left
both observed poses unchanged, and a drag moved both actors. A single undo
and redo restored the corresponding poses. Production Studio reopened the
result. Retail reported matching actor poses and animation at frames 7 and 14.
Capture suite `20261120T015504Z-857097fe` matched 60 retail scene/frame markers
in each port's seek and playback runs. Both ports' seek image hashes matched
their previous builds, and SDL's 12 playback sound events matched retail.

### Actor group transforms

Studio's drag commands call `actor_transform_edit.c` to visit the selected actors
and run the same C owner operations as single-actor edits. Translation uses
the requested delta and route flags. Rotation applies the fixed-point orbit
around the frozen pivot, with tool X mapped to world Z and tool Z mapped to
world X. Resize and squash move body positions as well as changing each body.
The Y factor for squash is the reciprocal of the X and Z factor. The loop
retains each tool's success predicate and continues past failed individual
edits. Studio supplies pointer deltas, cursor and sound effects, and redraws.

Headless Studio runs routed the same pointer commands to both builds for move,
subroute move, tweak, rotation on all three axes, resize and squash. Both drag
directions matched through editing, undo, redo and production Studio reopen.
For the eight saved movies in the primary run, all 72 chunk entries and their
links and payloads matched per file. The raw differences were seven to twelve
bytes of chunk-index alignment padding. Retail reopened each movie with
matching pose matrices and animation for the two observed actors at frame 7.
The native live-actor fixture also used the group resize before undoing and
redoing it. All 12 native actor traces matched byte for byte.

Capture suite `20261120T020904Z-3802926f` matched 60 retail scene/frame markers
in each port's seek and playback runs. Both ports' seek image hashes matched
their previous builds, and SDL's 12 playback sound events matched retail.

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

`actor_copy.c` operates directly on the C89 actor owner and its typed event
and path tables for full-actor and route-only copy. Full copy selects
earlier events from the current subroute, rewrites action cels to the current
cel, resolves/writes source sound tags before deciding whether to retain each
sound, filters Remove and older Tweak/RotateHere events, resets earlier event
locations and inserts/merges them. Future events retain payloads, adjust route
indices/offsets/holds and stop at the next Add for a subroute-only copy once the
destination has events. The active BUG1950 setting is passed explicitly.

Source payloads remain locked across insertion, including the action's stack
replacement, and unlock before merge or failure destruction. Successful source
sound-tag rewrites remain even when a later operation fails. Destination tag
ownership uses the shared C insertion, merge and destruction implementations.
The shared C actor factory creates the unattached destination from the source
template tag. Copy publishes the result after creation and releases a partial
actor before clearing the result. It snapshots the source playback state
before creation and writes back resolved sound payloads to the source track.

Full copy keeps the current node's original coordinates and subtracts the
current offset from its stored distance. Route-only copy interpolates the first
point and recomputes distance to the next node with the existing external C
BRender primitive, retaining epsilon 1 for zero length. Endpoints, subroute
boundaries, bulk copy (including zero rows), and reservation remain distinct
between the operations. Creation and both copy paths use the same C actor-owner
rewind, including the recording-mode reset.

Studio supplies actor allocation and resource callbacks, movie sound-tag
resolution, BRender distance and diagnostics. Copy uses typed C tables for
array operations and calls the shared C event editor directly.

`engine-actor-copy-test` exercises the actual C owner and tables, including
payload locks, source mutation before filtering, storage and sound-resolution
failures, merged costume references, result publication, optional BUG1950
filtering, future offsets/holds, full versus interpolated points, epsilon and
endpoint paths. The initial 405,760-byte native-before trace matched, but a later
clipboard audit found all 320 copies failed at template creation: cache keys
include the reader, so prefetching a test reader did not satisfy the factory's
normal template reader. That trace proves negative paths only. The corrected
fixture writes a valid template/action graph and asserts every expected outcome.
Its 449,472-byte trace matches the original C++ copy implementations from
`c789578c^` exactly: 128 normal successes, 128 injected allocation failures,
32 invalid-child full-copy failures and 32 invalid-child route-copy successes.
It records owning material/sound tags, tables, rewound state and reference counts.
The original trace remains preserved rather than silently relabeled. Fixture
sound chunks support tag mapping without decoding audio; the playback corpus
supplies actual audio checks.

The typed-owner cutover reproduced the native copy, route paste, actor paste
and clipboard traces byte for byte. In the `actor-event-edit` playback case,
SDL Studio and Marcus matched the retained retail seek and playback frame
sequences and seek camera indices across 60 frames. Each port reproduced its
prior seek image hashes, and SDL matched retail's 12 sound events.

### Actor route paste

`actor_route_paste.c` takes the C89 actor owner and a typed clipboard path. It
uses the shared C route splitter, event editor, floating step and pose evaluator.
It reserves before mutation, publishes the split cursor before insertion, deletes
from the current node using the native count, removes current-subroute events
without cursor updates and shifts later-subroute event indices. It translates
copied points relative to the current point. The first-segment distance retains
epsilon 1. The Step/Freeze order and delayed dirty, range and body notifications
are preserved.

Failures keep preceding edits for the caller's undo snapshot to unwind. Reserved
point insertion failure diagnoses and continues, matching native AssertDo; other
insertion, metadata and floating-step failures return immediately. The optional
STATIC policy is explicit: it queries action flags and floats only static actions,
while the normal build always floats. The split temporary's previously
uninitialized distance is initialized to a nonzero epsilon before the existing
splitter; the temporary node is removed/overwritten before the resulting route.

Studio route editing and Marcus preview animation drafts share C bindings for
path reservation, point writes, and event table writes. Studio supplies BRender
distance, diagnostics, scene and body notifications, and action metadata.

The 167,168-byte native reference trace covers 256 scenarios, including empty and
short clipboard paths, offsets and holds, later subroutes, terminal nodes,
material and sound tag references, and state and transforms. It matched byte for
byte alongside 11 other actor-edit traces. `engine-route-paste-test` uses actual
C path and event storage to check the allocation and partial failure paths,
tag closure, cursor and terminator ordering, optional STATIC metadata, and
clipboard data. In SDL Studio a copied route moved the target actor after paste.
Undo, redo and save/reopen reproduced the expected motion. Retail reopened the
saved movie with matching positions and exact fixed-point poses at frames 7,
10 and 14. On the 60-frame `actor-event-edit` case, both ports matched retained
retail frame sequences and seek camera indices and reproduced their own prior
seek images. SDL also matched the 12 retail sound events.

### Full actor paste

`actor_paste.c` operates on the C89 actor owner and its typed path and event
tables. It owns optional BUG1888 text-template transfer, tag/template ownership,
lifetime changes, placement, frame translation, sound filtering and adoption,
and camera-facing text entrances. It uses the shared C scene binding,
state-origin and event editor. Missing user sounds and failed sound saves
diagnose and remove their rows while revisiting the shifted row. Failed child
adoption returns immediately without publishing the locally rewritten sound
tag. Earlier frame writes, resource transfers and deletions remain. Final
route translation and dirty notification occur only on success.

Paste updates the live runtime record so body creation and cursor changes
from event removal are visible immediately. Text transfer duplicates before
saving and closes the duplicate on save or fetch failure. On success it
closes the old tag and releases the old template before replacing the
template pointer. Camera facing queries the replacement template's text flag.

The 22,272-byte native-before trace covers 64 cases in private movies with actual
serialized 3D-text templates, same/cross-resource paste, authored placement,
frame offsets, owning sound tags, consecutive missing-sound removals, untouched
media sounds, invalid first events, row data and resource reference counts. It
matches exactly. The fixture retains its existing test body to isolate paste
policy; it does not prove text rig creation or rendering. Dummy sound chunks
exercise tag adoption without audio decoding. Playback captures supply the
render/audio checks separately.

`engine-actor-paste-test` exercises real C track storage. It checks template
autosave/save/fetch failures, reference counts, replacement-before-binding order,
changed text flags, frame arithmetic, the scene sound indicator, cursor updates
after removal and partial state after a failed child adoption. Studio supplies
autosave and chunk transfer, movie sound and child lookup, body and template
resources, placement, scene notifications and diagnostics.

The C owner reproduced all 12 native actor-edit traces, including copy,
full paste, route paste and clipboard. In SDL Studio the 3D-text copy,
paste and placement workflow survived undo, redo and save/reopen. Retail
reopened the saved movie with the pasted actor on stage at frame 7 and matching
action, cel, position and part matrices. SDL Studio and Marcus matched the
retained retail scene/frame sequences and seek camera indices in the 60-frame
`actor-event-edit` case. Each port reproduced its own previous seek images,
and SDL matched the 12 retail sound events.

### Actor clipboard control

`MMActorClipboard` owns the actor handle, route flag and native counted name.
`actor_clipboard.c` calls the C actor owners for frame-relative copy and reset
duplication, and uses the C counted-string operations to build the name.
Studio supplies the clipboard document, localized labels and movie/view hooks.
The movie roll call accepts the stored name's counted units directly.

The movie-view clipboard controller in `movie_edit.c` routes Edit menu
commands, toolbar tools and pointer clicks. It chooses inline text forwarding,
immediate actor or text-box paste, or a tool that acts on the next click. It
publishes the actor copy before a cut, temporarily disables scene sound
during deletion, and clears modifiers after keyboard cut and copy. Studio
maps native command IDs, queues native commands, and supplies the clipboard
document and view callbacks. The controller releases every document reference
it acquires, including actor clipboard probes on missed clicks and paste
attempts without an active scene.

The prefix comparison is raw and case-sensitive, scans to the prefix's first
zero and does not require a following word boundary. Assignment and append
retain counted embedded zeros. Failed append retains truncation. Creation
assigns the copied actor after naming succeeds, preserving the known native
name-overflow leak: the empty clipboard is released while its unassigned local
actor is leaked. The C fixture checks those references using real actor
tables and releases the unassigned copy during test cleanup.

The 41,344-byte native-before trace spans 64 successful clipboard creations with
both scopes, case/empty/embedded-zero prefixes, preexisting prefix variations,
stored names, copied/source actor tables/state and balanced references. It
matches exactly, including native full-paste rejection without a MovieView and
route-paste rejection without a selected target. The C fixture exercises one-
and two-byte names, reset duplication, route and full-paste dispatch,
duplicate/paste/rename failures, and removal using the current actor ID before
releasing the duplicate. Studio's full 3D-text and route clipboard workflows
passed undo, redo and save/reopen. Retail reopened both saved movies with
matching poses. The native paste fixture also exercises cross-resource template
and sound transfers. A separate SDL cross-movie UI probe added zero actors on
both the retained older build and the rebuilt Studio.

Before and after the movie-view migration, Studio produced matching actor,
route and text-box clipboard traces for keyboard commands, armed tools and
pointer clicks. Retail reopened the movies saved after full-actor and route
paste with matching poses. The C fixture covers inline-text routing,
clipboard format priority, and reference release on rejected paste paths.

All 12 native actor-edit traces matched after the migration. In the 60-frame
`actor-event-edit` case, SDL Studio and Marcus matched the retained retail
frame sequences and seek camera indices and reproduced their prior seek
images. SDL also reproduced the 12 retail sound events.

### Actor template replacement and render decisions

Studio template replacement in `actor_owner.c` fetches a template, adjusts
the live text body, selects the current cel, and repositions with the old
template before publishing the new reference and tag. Costume rows for
removed body sets are deleted in reverse order through the shared event
editor so tag references and cursors stay consistent. A failed adjustment
discards the candidate. Failed cel selection retains the adjusted body and
candidate reference, following the original Studio ownership behavior.

Marcus preview's C actor owns its template and body handles. Text replacement
in `preview_actor.c` binds a fetched text template, adjusts a duplicate
body, and poses it at the current frame. Failure restores the entire actor
record before releasing the candidate body and template. Isolated action
previews borrow the same body slot and restore it when rendering finishes.

Studio uses the C timeline to decide whether an actor invalidates a
prerender. Sounds do not require a redraw, and Freeze and Step events at
the terminal frame only affect subsequent frames. The pull tool still adds
the route's Y translation when reporting depth.

### Actor creation and scene binding

`actor_create.c` owns factory failure release, template/tag acquisition order,
track-before-sound initialization, rewind/recording reset, action-zero metadata
and scene/body binding order. It calls the shared C route rewind and owner
disposal paths. `_FInit`, `_FCreateGroups`, `PactrNew` and `SetPscen` are native
facades over this controller. Allocation, template fetching/metadata, existing
table/sound owners, body creation and world binding remain primitive adapters.
Template/body implementations and native object representations still require
conversion; this does not make them final host dependencies.

Failed initialization retains partial fields/storage for owner disposal. Failed
body creation retains the assigned scene with a NULL body. Repeated scene
binding returns immediately and never recreates the body; a missing body or
different scene diagnoses the existing broken precondition. A valid repeated
binding does not add references or bind twice. Direct group/initialization APIs
leave cleanup to their caller; only the factory releases a failed actor.

The 10,120-byte native-before trace spans 120 cold/warm-cache cases with a valid
serialized template/action graph, invalid template data, missing action cels,
BASE allocation failures, successful body/world binding, failed body creation,
repeat binding, actor state/tables and reference balance. Each expected factory
outcome is asserted. There are 29 successful creations, including seven body
failures that retain their scene, and 91 factory failures. The trace matches
exactly. Zero-part bodies isolate binding policy; model/rig construction is not
proved by this fixture. `engine-actor-create-test` separately checks acquisition
and publication ordering, short-circuiting, partial flags on failure, real C owner
disposal, direct-call cleanup responsibility, recording reset and retained failed
bindings. No Marcus UI tests were added. Movie sound integration control is
described next. Template construction, remaining body/scene/movie/resource/
runtime implementations and final facade removal remain unfinished.

### Movie sound integration

`movie_sound.c` owns sound tag resolution/saving, scene/movie child lookup and
adoption, first-free IDs, cross-movie graph copying, physical MIDI/WAV import
dispatch, failure cleanup, close-time GC marking and unused-sound discovery.
The private C++ child/ID helpers are removed. Native adapters supply C-backed
graph/tag operations, autosave/temp switching, physical sound-format factories,
file save, filenames and diagnostics. Decoder/encoder implementations, sound
garbage collection itself, native representations and physical file adapters
still require conversion; these are not final backend host services.

Equal chunk numbers preserve the existing tag cache even with a different
selected cache. Changed numbers open locally before closing/replacing the old
tag. Media tags obtain the selected file before returning unchanged. Adoption
publishes a candidate ID before its potentially failed graph edit. Free slots
count all children but probe only sound links; movie-wide adoption into another
file still chooses IDs from the working file. Existing enumeration failures
remain distinct: child lookup returns false; movie adoption diagnoses and stops
with success. Missing current scene in sound copy diagnoses and continues, since
that lookup's output was unused. Failed adoption deletes the copied root while
retaining the output number; failed save leaves the adoption. Only successful
physical-file import marks GC. Unused scanning includes unlinked sounds and
checks fewer than two parent references, optionally requiring any child at zero.
Its trusted-enumeration failure was undefined after the native assertion; C
initializes that otherwise undefined number to zero before diagnosing/continuing.

The 15,580-byte native and 15,592-byte SDL before traces each cover 68 cases and
match their respective converted implementations exactly. Graph/tag cases assert
success across child gaps, unrelated child types, repeated adoption, cache
identity, missing children, resource ownership, graph copying and unused checks.
Four physical-file cases add valid/invalid WAV and MIDI. SDL imports both valid
formats; the Windows fallback AudioMan stub rejects WAV import, so that native
configuration verifies the existing rejection and successful MIDI import. This
is an explicit platform coverage limit, not a claim that the stub works. C tests
also cover primitive failures, cross-file ID selection, open-before-close,
failed-adoption output, save failure, refreshed working state versus captured
destination, error suppression and GC ordering. No Marcus UI tests were added.
Template construction, sound-format factories, remaining body/movie/scene/
resource/runtime policy and final facade removal remain unfinished.

### Body construction and shape ownership

`body_create.c` owns the shared highlight initialization decision, body factory,
hidden-state initialization, duplicated set ownership, highest-set-plus-one
counting, allocation order and root/highlight/part hierarchy construction. C
also owns shape disposal: hide first when visible, release models in part order,
remove materials in set order only when actor storage exists, free actor and
material storage, then release the duplicated set list. Partial initialization
remains published without rollback; the factory releases its failed body while
direct initialization callers retain cleanup responsibility. Disposal clears
ownership pointers while retaining counts, matching native behavior.

Native services adapt allocation, BRender actor/material fields and links,
reference release and renderer notifications. Body creation and reshaping
read borrowed parent and set views. Each body owns a C89 copy of its part
sets. The native Body embeds `MMBodyShape` with actor and material allocation
handles, the set-list reference, part and set counts, and the hidden count.
One borrowed state accessor exposes this record to the C code. C value snapshots
retain the previous publication points around allocation, visibility and cleanup;
initialization explicitly clears a fresh owner, not a live one. Typed actor,
set-list and custom-material views read that same C owner. Model/material
resource implementations, ThreeDText construction, representation adapters
and the remaining native Body facade remain C++ dependencies. Both hosts use the
converted construction/disposal through their engine-core Body facade; this is
not yet the final C-only backend boundary.

The 88,136-byte native trace covers 162 factory cases across empty, single-part,
chain, branch, disconnected and sparse-set hierarchies. It includes 150 successes
and 12 allocator-injected failures, canonical actor links/fields/matrices,
visible-body disposal and restored allocation counts. C output matches exactly.
The release SDL round-trip additionally passes all 162 body cases without debug
allocator injection. C tests cover cold/warm shared material, highlight/factory/
list/material/actor allocation failures, partial state publication, direct versus
factory cleanup and model/material/storage release order. AddressSanitizer,
strict C89 syntax, 63 standalone checks, 77 focused native checks, 49 snapshots,
51 CLI baselines and headless scene playback pass. The persistent-storage follow-up
retains separate original Debug/Release traces and repeats the body integration
and playback checks; evidence is in `docs/tools/playback-regression.md`.
No Marcus UI tests were added.

### Body duplication, restoration and reshaping

`body_edit.c` owns duplicate allocation/reference sequencing, original visibility
restoration, receiver identifier repair, preserved hidden counts, reshape rollback
and the order in which materials/models/matrices/highlighting are restored.
Duplication hides a visible original, copies bindings, allocates/rebases actors,
retains models, duplicates the set list, allocates/copies material storage and
copies bounds. Generic materials retain one reference per part; custom materials
retain one per set. Both success and failure re-show the original before failed
copy disposal. A copy starts hidden once regardless of its source's hidden count.

Restoration retains native whole-object swapping, then rewrites only receiver
actor identifiers and preserves its original hidden count. Reshape duplicates
before destruction, reuses C shape construction/disposal, restores visibility
before material/model/matrix edits, and rolls back through that same restoration
operation when construction fails. Native adapters still provide BRender pointer
rebasing, whole-object swapping, metadata/bounds/field copies, material internals
and renderer notifications. These and the native Body representation are
temporary dependencies, not the final C-only backend boundary.

The 1,880,872-byte native trace matches C exactly across 1,404 cases: 1,278
successes and 126 allocation-injected failures. Cases include empty/one/six-part
sources, growing/shrinking targets, generic/custom materials, shared and missing
models, canonical hierarchy links (including previous-link slots), part/root
matrices, bounds/hit state, renderer attachment, nested hiding and highlighting.
Resource reference counts and native allocation counts return to their starting
values. The SDL release fixture covers the same cases without debug injection.
Standalone C tests independently cover every duplicate/reshape allocation stage,
reference balance, original-show-before-failed-copy-release ordering, restoration
and successful/failed shape state. Body geometry/render policy now uses the
following C module; resource/representation adapters and final native facade
removal remain unfinished.

### Body materials, visibility and costume ownership

`body_runtime.c` owns visibility counts and notification decisions, highlight
updates, model replacement, matrix/translation sequencing, generic/custom
material replacement and material queries. Reassigning the same model returns
without reference or notification changes. Generic materials retain per part;
custom materials retain before removing the old costume, supporting self-use.
Accessory models update before their part material; absent accessory models
leave the existing part model intact. Unused trailing custom entries remain
allowed. Custom removal releases the owner before clearing raw part handles,
without dereferencing handles whose owner may have just been destroyed.

Showing publishes the decremented hidden count before attaching/notifying.
Hiding publishes its increment before clearing bounds, detaching and notifying.
Nested hidden counts and repeated highlight/material notifications retain native
behavior. Body geometry/render policy is now C as described below. Renderer
field adapters, external BRender primitives, resource object internals and the
native representation remain dependencies.

`MMBodyCostume` now owns snapshot entries and references in C. Capture drops
the previous snapshot before allocation; failed replacement leaves it empty.
Application preserves exact-shape checking or the explicit smaller-count rule.
The unused count of an empty owner is defined as zero; native debug used poison
bytes there. Inactive getter outputs retain native invalidation through a small
adapter, and pointer output bridges copy representations without typed reads of
possibly uninitialized caller slots.

The 427,368-byte native trace matches C exactly across 90 material/visibility
scenarios, including accessory models, same-object assignments, nested hiding,
matrices, renderer notifications, unused getter outputs, costume replacement and
different-shape application. It records 72 successful replacement captures and
18 allocation failures, with reference/allocation counts restored. The SDL
release fixture covers 90 successful captures without debug injection. C tests
also verify failed replacement clearing, release/assign/retain order and no
dereference of raw handles after their final custom owner is released.

### Body geometry, render callbacks and picking

`body_geometry.c` owns persistent `MMBodyGeometry` bounds/last-visible/hit
state, render-bound accumulation, prepare callbacks, visible-rectangle/center/
position queries, highlight exclusion/restoration, world bounds and hit-selection
policy. Body construction initializes this plain C state; duplication and restore
retain the earlier C controllers' copy/swap sequencing. Native rectangle/matrix/
actor bridges expose values and field access, not another geometry evaluator.
`BrActorToBounds` traversal and `BrBoundsToMatrix34` remain external BRender
primitive services. Native object/actor-tree and model/material representations
remain temporary dependencies of the unfinished C-only backend.

Preparing a visible body saves its current rectangle and then clears it.
Already-empty (including inverted) current rectangles remain untouched, and
queries fall back to last-visible bounds. Rendered empty rectangles do not
contribute to unions. Highlight actors are temporarily changed to the host's
none type for bounds traversal and restored before matrix callbacks. World
bounds retain all eight native corners and the original +/-10000 initial
extrema; C uses the existing full-product 16.16 accumulator plus exact wrapped
translation, matching native `BR_MAC4`. Rectangle centers preserve wrapped
32-bit sums followed by truncation toward zero, using unsigned magnitudes
rather than relying on C89's implementation-defined negative division.

`BodyWorld` supplies synchronous renderer traversal. Closest-hit state is now a
caller-owned `MMBodyPick`, replacing the three process-wide Body picking
fields. A strictly closer visible hit wins, so the first equal-depth hit remains.
Root/highlight hits may select a body with set -1. Point-in-body search marks
found for a highlight hit but continues looking for a real part. An unsuccessful
closest-body query leaves the optional caller set output unchanged. C tests
exercise two independent pick contexts and exact callback publication/order.
No Marcus UI tests were added.

The native 76,608-byte trace matches exactly over 144 real BRender body cases,
SHA-256 `0EA33C3F9F3B7D6F22B857B6FBAB336B0EDA558DF19F1FD4F22B1C79AD519327`.
It spans 1/3/6-part hierarchies, six root transforms, highlighting, visibility
rectangles, local/world bounds, render unions, centers/positions, actor-to-set
lookup and search/closest-hit callbacks. The committed fixture automatically
checks frozen FNV-1a `ECFB9185` on little-endian hosts. Prior 1,880,872-byte
body-edit and 427,368-byte body-runtime traces remain exact. Standalone C and
AddressSanitizer checks, both hosts, 88 focused native checks, 51 CLI baselines
and retail/SDL/Marcus actor-events/actor-audio comparisons are recorded with
coverage limits in `docs/tools/playback-regression.md`.

### Model resource ownership, wire I/O and prelighting

`model_resource.c` owns the persistent `MMModelResource` renderer handle,
construction/factories, packed/unpacked estimates, prepared/unprepared loading,
partial ownership, disposal, writing, DAT import control, extent queries and
glyph adjustment. The C89 `mm-brender-resources` library binds BRender allocation,
geometry, DAT import and prelighting for both hosts. `modl.cpp` supplies object
and block/file services and routes diagnostics. Header and face codecs read
48-byte headers and 32-byte faces. The 32-byte vertex records keep their native
opaque representation. Runtime faces may be larger because their material
field is a pointer. Serialized material slots and implicit face padding are
zero. Explicit `_pad0` bytes remain preserved. The live loader, standalone
mesh reader and writer share the vertex and face section layout.

Loading preserves radius-based preparation selection and the original ordering
of group publication, allocation, binding and renderer registration. Failure
retains partial ownership until disposal. Factories release failed objects
before invalidating outputs. Legacy code normalized its header order marker
before testing it again, so foreign vertex and face payloads remain unswapped.
Malformed negative signed wire counts fail before native allocator/read
assertions; bounded reads reject truncations. Trailing bytes remain accepted.
Glyph arithmetic preserves floor halves for negative fixed-point differences,
wrapped sums/subtractions, the -655 spacing threshold and repeat adjustments.

`model_lighting.c` owns the original camera/light/material specification and
prelight sequencing. The former direction input remains ignored. Every enabled
light is disabled, the world frees its attached camera/lights, and material and
both pixelmaps are freed on every returning path. This fixes the old temporary
resource leaks without changing the frozen prepared geometry. Native debug
still diagnoses more than one light; C mocks cover counts 1 through 9 and every
creation failure. The native model allocator now supplies a nonempty pointer-
sized identifier string so BRender reserves room for the owner pointer. Owner
encoding/decoding uses representation copies in C. Imported DAT models with
short identifiers receive a resource-owned slot before the model owner is
stored. DAT testing also exposed an uninitialized
non-count chunk-handler argument in BRender; initializing it to zero removes
the runtime-check failure. Count-bearing chunks retain their read argument.

`engine-model-resource-test` links with C alone and covers wire byte orders,
truncations, callback failures/publication, partial ownership, writer failures,
glyph boundaries and lighting cleanup. `model-resource-test` exercises real
BRender factories, DAT import and temporary allocation balance. Its frozen
277,360-byte native trace matches across 504 debug load attempts, 404 successful,
SHA-256 `29157E1B47FE4DA9E1B36DF511026715326661B816075D887F4EECAD986F51D3`;
the test enforces FNV-1a `78C575E5` on native debug little-endian hosts. Real
retail/SDL/Marcus captures and the native-model caption control are recorded
with coverage limits in `docs/tools/playback-regression.md`. Material/texture
resources and final native facade removal remain migration work.

The C89 renderer binding reproduces the full 277,360-byte native model trace,
and DAT import passes with both a one-character identifier and a longer name.
The renderer archive references C BRender, engine and runtime symbols. Headless
actor renders exercise stock and spare costume materials. The standalone probes
agree with the preceding build on 300 frame snapshots and 300 model and material
snapshots. Native graph and stream providers match on 300 frames. All 181
standalone checks, 191 focused host checks, 11 C89 syntax checks and five
AddressSanitizer checks pass. The 31 native traces and the 480-byte saved
fixture match the preceding build. The hosts match 456 prior seek observations,
360 viewport hashes and 480 retail frame commits. Retail reopens the Studio
actor edit with matching state on four seeks. Both hosts reproduce the
ordinary and motion cues in the actor audio fixture.
Studio passes all eight VMM authoring and reopen checks, and Marcus reproduces
the viewport hash for the retained VMM fixture. Build hashes, traces and
captures are in `tmp/c89-model-renderer`.

### Materials, custom materials and shade-table ownership

`material_resource.c` owns persistent `MMMaterialResource` renderer handles
and the shared `MMMaterialShade` texture reference. It controls creation,
factory estimates, header/texture/transform loading, writing, disposal,
shade-table replacement and PIX/BMP import sequencing. `custom_material.c`
owns persistent material/model arrays, body-part counts and set identifiers,
construction/retains, child loading, factory estimates and disposal/queries.
The C89 renderer library allocates BRender materials and binds colour maps,
shade maps and texture transforms. The material controller resolves texture
and shade references to borrowed pixelmap views before mapping them.
`mtrl.cpp` supplies object references, chunk and file services, texture lookup
and PIX paths. The custom-material graph comparisons remain in the shared
template controller.

Material headers remain 20 bytes, custom headers 8 and texture transforms 28.
Both host orders and the narrow zero-order Windows material-header exception
retain their native policy. A non-native texture-transform marker always causes
swapping before its debug diagnostic. Transform children are considered only
when a texture child exists. Writing preserves native child creation/adoption
cleanup, including retained partial texture chunks after a texture writer fails;
it does not add serialization of texture transforms. Material factories
invalidate outputs before releasing failed owners; custom factories release
first. Direct loading retains partial ownership except transform failures,
which release the texture and renderer material locally.

Custom slot counts still use the highest signed child ID across every child
type, with missing material/model slots left empty. Arrays allocate in native
order and preserve partially acquired references on failure. Unrepresentable
array/cache sizes fail before overflowing signed allocation arithmetic. Native
debug validation still expects complete material slots in successful facades.
The renderer identifier now uses a nonempty pointer-sized placeholder so
BRender allocates the owner slot, and typed copies avoid aliased pointer reads.
Failed PIX wrapping no longer publishes a freed/unwrapped pixelmap to material
disposal. Successfully imported PIX allocations now use the C-owned texture
lifetime described below.

`engine-material-resource-test` links with C alone and covers endian codecs,
legacy markers, sparse child IDs, partial arrays/references, invalid counts,
factory ordering, shade replacement, imports and write/adoption failures.
`material-resource-test` freezes 102,316 native bytes over 588 material/custom
load attempts plus construction, writing and real PIX import, SHA-256
`F31D5481A64AC6D49CA63F6E5FDD288D4C4C70246F196760B158B1E519775225`.
The native debug little-endian check enforces FNV-1a `113D98C1` without masking.
The existing body runtime fixture now fills the C-owned model array directly.
Both host builds, C89/ASan checks, CLI comparisons and retail playback evidence
are recorded in `docs/tools/playback-regression.md`. No Marcus UI tests were added.

The C89 renderer conversion reproduces the material, texture and model traces
against `7e750846`, along with all 31 native traces and the 480-byte saved
fixture. The stock actor render and the spare costume material render match
byte for byte. Standalone frame and mesh snapshots and graph/stream
evaluations each agree across 300 frames. All 181 standalone checks,
194 focused host checks, 11 C89 syntax checks and seven AddressSanitizer
checks pass. The hosts match 456 prior seek observations, 360 viewport
hashes and 480 retail frame commits. Retail reopens the Studio actor edit
with matching state on four seeks. Both hosts reproduce the ordinary and
motion cues in the actor audio fixture.
Studio passes all eight VMM authoring and reopen checks. Marcus reproduces
the retained VMM viewport hash. Build hashes, traces and captures are in
`tmp/c89-material-texture`.

### Texture storage, palette matching and standalone files

`texture_resource.c` owns persistent `MMTextureResource` descriptors,
pixel buffers and imported renderer owners. It controls construction, cache
factories/estimates, 20-byte header codecs, bounded loading/writing, chunk
creation and standalone file signatures, packing, reports and failure cleanup.
`texture_import.c` owns bitmap import, source palette/cache lifetime and nearest
colour selection. The C89 renderer library publishes and synchronizes the
BRender pixelmap view and releases imported PIX resources through their
original pixelmap. The native `TextureMap` supplies object allocation, file
and bitmap-reader services. Synchronization before serialization, size
queries and disposal preserves changes through the exposed renderer view.
The standalone engine policies build without BRender or C++.

Loading preserves the legacy swap-on-any-non-native-marker policy and opaque
pixel payloads. Truncated reads and negative/unrepresentable layouts fail before
invalid native operations; writes retain partial-prefix and partial-chunk
behavior. Cache estimates still use the supplied packed block's byte count.
Palette matching retains the final equal-distance candidate, empty destination
palette behavior, optional-cache fallback and contiguous width*height processing
of the bitmap reader's padded storage. It deliberately does not repack or flip
those rows. The old temporary DynamicArray cache is now a C-owned integer array;
its allocation-failure topology changes, not its successful pixel results.

Pixel constructors take buffers only on success. Imported PIX wrapping takes
the original renderer allocation only on success and frees that owner through
`BrPixelmapFree`, preserving its resource tree rather than freeing an interior
pixel pointer. Failed bitmap construction now releases decoded pixels instead
of leaking them. The bitmap reader/encoder (`FReadBitmap`/`FWriteBitmap`) now
delegates to the C implementation below. Physical file services remain host
dependencies; the uncompiled
`NOT_YET_REVIEWED` inverse-table code remains unchanged.

`engine-texture-resource-test` links with C alone and covers headers/orders,
every read truncation, layout limits, ownership transfer, failure cleanup,
palette ties/cache fallback, source indices and standalone file/packing failures.
The native `texture-resource-test` freezes 22,000 bytes over 192 load attempts,
constructors, standalone writes, PIX wrapping and bitmap imports with padded
rows and destination palettes of 0/1/2/7/256 colours or no mapping. SHA-256 is
`DC154CACA679AD177694778EDCEF74C2256BAD6EF81A23AA6C80DA9CE4C5FB43`;
debug little-endian builds enforce FNV-1a `C5DC711A`. Extra native checks require
balanced BRender allocations after imported-owner disposal, caller ownership
after failed wrapping, and a real packed-file decode with mutable-view metadata.
The material trace remains exact without manual imported-pixelmap cleanup.
Full checks and retail comparisons are in `docs/tools/playback-regression.md`.

### Indexed bitmap files and RLE8

`bitmap_file.c` owns eight-bit BMP header/layout validation, conditional palette
admission, RGB pixel reads, RLE8 decoding, canonical BMP writing, row reversal
and partial-owner/output cleanup. `FReadBitmap` and `FWriteBitmap` in
`kauai/src/mbmp.cpp` retain native file, allocator and palette-object callbacks.
The masked-bitmap codec in the rest of that file delegates to the C implementation
below as well.

The reader preserves on-disk padded rows and reports orientation separately.
Palette validation and its 1,024-byte read occur only when requested. Metadata
queries can succeed without pixel/palette data; pixel reads still enforce their
own bounds. RGB image-size zero uses the computed padded layout and permits
trailing bytes. RLE runs use padded widths, including transparent EOL/delta/EOB
fills and odd-literal padding. Malformed RLE now frees the destination allocation,
and an EOL past the destination fails before writing. Unrepresentable dimensions
are rejected before signed arithmetic overflows. Successful writer bytes retain
all palette bytes, positive height, image-size zero and optional row reversal.

The native fixture freezes 633,064 bytes over 1,728 read attempts and 30 emitted
files, including output combinations, allocation failures, negative height,
RLE runs/deltas, overlapping palette/pixel offsets and conditional palette rules.
SHA-256 is `231FC86D957EBF2AE588106203BF1A150D3D914CE1F5BA84F9B1E062FCB663AC`;
debug little-endian builds enforce FNV-1a `C71364BE`. The C-only fixture adds
truncated headers/pixels, malformed RLE canaries, layout limits, balanced
allocations/locks and write-failure deletion checks. Full build and playback
evidence is in `docs/tools/playback-regression.md`.

### Depth-buffer storage and drawing

`depth_bitmap.c` owns persistent depth bounds, row/byte counts and pixel storage,
construction/contiguous renderer imports, cache estimate/load control, 12-byte
headers, exact-size payload reads, chunk writing, clipping and full/half drawing.
The native `ZBMP` facade supplies object, allocation and block services.
Bounds intersection, region scanning and span addressing execute in C. Pixel
bytes remain mutable through `Prgb()` without a second owner.

Reading deliberately ignores the stored byte-order/system markers and decodes
signed shorts in host order, matching the original reader. Pixel payloads remain
opaque. Empty serialized layouts are accepted; negative layouts and overflowing
sizes fail before allocation. Writes retain native-order header fields and
partially created chunks on failure. New buffers fill with `0xff`; renderer
imports copy contiguous bytes, ignoring the renderer's stride.

Drawing without a clip or region does nothing. Reference offsets move the
clipping bounds but do not change absolute source/destination addressing. Half
mode doubles only the bottom bound and scans every other row from the actual
clipped top, including odd tops. These native behaviors are preserved. Invalid
arithmetic or spans stop before the offending copy. Earlier valid spans remain
copied.

`depth-bitmap-test` freezes 65,880 bytes over 256 load attempts, 288 constructor
attempts and 216 drawing combinations. Two original traces and the converted
trace match SHA-256 `C9CA1CDA746BD0DB2ADA91D4AC6B196A6DB915C0255E81A804CD0F9FEEC735A2`;
debug little-endian builds enforce FNV-1a `1BE82F14`. The C-only fixture covers
both host byte orders, truncations, empty/negative layouts, failure cleanup,
factory estimates, partial writes, region clipping and bounded drawing.
Full gates and retail capture limits are in `docs/tools/playback-regression.md`.

### Masked-bitmap ownership, encoding and drawing

`masked_bitmap.c` owns persistent contiguous header/row-table/RLE buffers,
construction/cropping, 255-byte run continuations, mask-only encoding,
foreign-to-host normalization, bounded row validation, hit testing, cache
factories, serialization and BMP import orientation/cleanup. Persistent HQ
handles and their resize sequence are removed from `MaskedBitmapMBMP`. Native
facades provide typed allocation, block and BMP services. The C89 drawing path
handles unscaled indexed pixels, rectangular and region clips, 1-bit masks,
and scaled masked bitmaps. The drawing adapters supply surface, palette and
region views. The C reader validates every encoded row before drawing and
decodes only the rows it samples.

The shared RLE loop leaves transparent indexed pixels untouched. Packed masks
clear transparent bits and set opaque bits inside the clipped bitmap bounds.
Run writes stop at the last byte in each row, including at a byte-aligned clip
edge. The standalone fixture covers both byte orders, multiple region spans and
packed mask edge bits. All 67,200 native drawing cases match the previous
renderer byte for byte. Evidence is in `tmp/c89-masked-blits`.

Scaled drawing preserves pixel-centre sampling, GDI's near-size copy rule and
BLACKONWHITE reduction of source indices and coverage before palette translation.
Transfer mode applies mask OR and colour AND to each clipped pixel, including
transparent cells. `pixel_raster.c` shares packed format access, nearest-colour
selection and indexed palette mapping with the pixel canvas.
The before-and-after comparison matched 24,192 scaled drawing cases across
indexed, true-colour and packed targets. Standalone checks cover malformed rows,
near-size transfer, reduction and sample coordinates whose products exceed
32 bits. Reports are in `tmp/c89-scaled-masked`.

`mm_pixel_copy` also handles packed surface copies across all six DIB depths,
raw 32-bit copies and indexed-to-indexed copies. It preserves raw same-format
samples, converts RGB555 and indexed colours, snapshots aliased palette and
pixel data, and shares clipping and pixel-centre sampling with masked drawing.
Kauai passes the region view and checks storage ranges using host addresses.
C-only callers can omit the address check to request a source snapshot.
The legacy near-size true-colour-to-8-bit approximation still uses the host.
All 39,204 copy cases matched the previous build across 40,184,100 bytes,
including return values, aliased storage, custom palettes, disjoint region
spans and extreme rectangle extents. Evidence is in `tmp/c89-packed-copy`.

`dib_frame.c` validates bounded 40-byte DIB headers and decodes their padded
top-down or bottom-up rows into BGRX32. `pixel_raster.c` draws decoded frames
using edge sampling measured from the bottom for 24-bit input and pixel-centre
sampling for 32-bit input. It also expands palette indices to native-order
XRGB32, leaving pixels untouched for missing indices. Exact-palette snapshots
validate every source pixel before publishing any index and choose the first
index for duplicate colours. The shared raster loop scans the C89 region
in the original band order. All 134,720 DIB and snapshot
cases matched across 20,675,560 bytes. Windows comparisons also passed across
3,024 DrawDib cases and 4,458 decoded frames from 24 AVI files. Evidence is in
`tmp/c89-dib-raster`.

`pixel_scroll.c` intersects the scroll rectangle with surface bounds and
checks that both endpoints are visible. Row and pixel traversal follow the
movement direction to preserve overlapping source samples without a surface
snapshot. Region coverage uses clipped 1-bit masks. The leftward monochrome
path still processes full bytes before partial-byte edges in each region band.
All 63,632 scroll cases matched return values and storage bytes, including
row padding, extreme offsets and irregular monochrome regions. The Windows
GDI oracle passed 111,780 scroll comparisons. Evidence is in
`tmp/c89-pixel-scroll`.

`pixel_paint.c` resolves solid and patterned Boolean brushes, converts
monochrome ink using the original palette and reference rules, paints framed
rectangles and nonzero-winding polygons, and applies thresholded or blended
glyph coverage. Polygon intersections retain exact rounding across the full
signed coordinate range. Oval drawing also uses C89 brush spans. All 35,331
before-and-after cases matched across 60,936,207 bytes. The GDI font test
reports the same System font metrics mismatch with both versions, after its packed glyph
drawing comparisons pass. Evidence is in `tmp/c89-pixel-paint`.

`ellipse_geometry.c` owns fixed-point and large-ellipse subdivision, exact
row intersections, and filled and framed oval painting. It shares upward
edge rounding with polygon filling and uses only the clipped row range for
rasterization. Kauai supplies surface and region views. Its legacy vector
geometry interface adapts to the same C implementation. All 15,101 outline
queries matched the previous implementation, including extreme aspect ratios
at the maximum extent. The Windows GDI oracle passed 16,808 region comparisons.
Allocation failure probes preserve both the row output and the painted surface.
Evidence is in `tmp/c89-ellipse`.

`indexed_display.c` handles raw indexed 1-by-2 and 2-by-2 stretching, including
the source crop and destination clip used by native Windows and SDL. It keeps
the original destination origin and scan-band row pairing. A paired write
stops at the physical surface boundary. The same module owns the display
palette's static identity entries, animation count, and alias-safe updates.
All 311,040 stretch cases matched the legacy code with a final-row boundary
guard. The unguarded code differed in 23 overlapping-buffer cases. All 6,480
crop plans and 9,216 palette updates matched outright. The Windows palette
oracle also matched 2,408 sequential updates. Evidence is in `tmp/c89-indexed-display`.

`pixel_storage.c` checks row alignment and byte counts for all six packed depths
and owns zero-initialized offscreen buffers. DIB frame validation shares the
layout checks. A failed resize leaves the current buffer in place. Kauai's
`PixelBuffer` adapter maintains its atomic allocation counters.

`damage_map.c` owns the packed SDL damage mask and groups identical adjacent
rows into scan bands. It uses the packed pixel mover to scroll damage and clears
the newly exposed strips without allocating. It clears pending damage only
after the rectangle visitor succeeds. Before-and-after comparisons matched
18,000 damage states, 10,000 buffer states and 134,720 DIB and snapshot cases.
Allocation-failure checks covered buffer replacement, damage resize and damage
copy. Evidence is in `tmp/c89-pixel-storage`.

`region_scan.c` traverses Kauai's existing clip-region bands and returns
horizontal edges relative to the clip origin. It preserves empty gaps,
partial-band advances, and the end-of-scan values. Its Boolean sweep processes
coincident edges together and emits bands for union, intersection and ordered
difference. The scan-only extraction used the Kauai allocation and scaling
path. The before-and-after probe matched 23,409,140
bytes of band heights, endpoints and bounds across 13,300 mixed Boolean cases,
scaling, translations, retained scans and clipped scans.

Studio and Marcus matched 456 pre-change seek observations and 360 viewport
hashes. Timed playback matched 480 retail frame commits, and retail reopened
the actor edit saved by Studio. The Windows `font-gdi` check reports the same
System bitmap font metrics mismatch in the saved pre-change executable and
the rebuilt executable. Evidence is in `tmp/c89-region`.

`region.c` now owns clip-region bounds and stored bands. It publishes Boolean
results after the band builder succeeds, merges identical adjacent rows, and
reduces rectangular results to bounds alone. Scaling keeps the legacy rounding
for absolute vertical positions and relative horizontal edges. It rewrites
exclusive bands without growing the buffer and preserves the original bands
for an active scanner by building a replacement. Allocation failure leaves
the region intact. The Kauai wrapper marshals rectangles and supplies its
collection allocator and debug memory marking. The Windows adapter manages
native region handles. The expanded comparison matched 24,347,696 bytes of
bounds and scan output across 15,300 mixed Boolean and scaled cases. Evidence
is in `tmp/c89-region-owner`.

World buffers now store both dirty regions directly in C. Initialization
uses inline rectangles. Failed mode changes preserve existing scan bands, and
successful changes mark the new buffer extent. Depth drawing and world RGB
cleanup scan the C regions directly, preserving half-depth row parity and
depth cleanup before RGB locks. The world scales and offsets screen damage
before passing a borrowed region to Kauai for window invalidation. Kauai
translates that view into window coordinates for the dirty-region union.
The native depth, buffer, camera, render and export snapshots matched
13,898,362 bytes. Evidence is in `tmp/c89-world-regions`.

Raster entry points consume C clip regions directly for masked drawing, brush
and glyph painting, surface copies, DIB draws, scrolling and indexed doubling.
The C89 rectangle visitor also supplies clipped bands to SDL and GDI. It
releases the retained scan when a consumer stops early, so an SDL clip-list
allocation failure can fall back to the software rasterizer. Indexed doubling
uses the full region band for even-phase row pairs and caps the second write
at the physical destination. Seven before-and-after probes matched
407,406,464 bytes across 675,319 raster cases, 6,480 stretch plans and 9,216
palette updates. Evidence is in `tmp/c89-raster-regions`.

`graphics_view.c` owns logical and port rectangles, mapped visibility and the
active clip. It preserves truncation for coordinate transforms and the
legacy remainder rounding for pen and font sizes. Empty mapping rectangles
become unit rectangles and changing the destination reopens the clip. C89
also computes pattern origins, the two exposed scroll strips and the shared
six-point line footprint. Kauai supplies the port base and publishes typed
points and rectangles. The comparison matched 80,000 view states against
legacy geometry arithmetic and 8,515,584 bytes across 768 native drawing
cases. Evidence is in `tmp/c89-graphics-view`.

`graphics_transition.c` schedules wipe and slide strips, iris clip bands,
and the two palette phases of a fade. It keeps the legacy palette and flush
order, including a final slide copy after installing a palette on true-colour
ports. Kauai supplies the clock, offscreen snapshot, temporary clip and
drawing services. The native before-and-after comparison matched 92,183,200
bytes across 640 cases and 10,628 intermediate clock snapshots on indexed and
true-colour ports. The C fixture checks fallback ordering and timestamp wrap.
SDL Studio and Marcus matched 480 retail frame commits, including dissolve
playback, and reproduced all 360 viewport hashes from the prior builds.
The V3DMM seek and authoring checks passed. Evidence, including the build
and input hashes for playback, is in `tmp/c89-transitions`.

The same controller now runs both tiled and indexed-pixel dissolve. C89 retains
the seeded permutation modulo 65537, the row-padding positions and the order of
pixel copies, flushes and palette animation. The host supplies the initial
random index, locks the indexed surfaces and blits snapshots to the target.
The C fixture checks padded rows, palette and flush order, cleanup after a
failed lock, and fill-only dissolve without a source buffer.

The native comparison matched 345,164,224 bytes across 832 cases and 11,996
intermediate snapshots. It included scaled tiles, fill-only transitions, and
surfaces larger than one permutation period. The baseline comparison copy
initializes the unused source stride read by the legacy debug build during
fill-only dissolve. The 181 standalone checks, 210 focused host checks, and
x86 and x64 AddressSanitizer checks passed.

Studio SDL and Marcus matched 480 retail frame commits and passed the V3DMM
workflow checks. In a separate visual-only dissolve playback capture, all three
players matched the 60 authored frames. Retail recorded 234 distinct images on
entry to the incoming scene, and each host recorded one. The combined media
runner stopped at its pre-play audio silence check for all three players.
Evidence is in `tmp/c89-dissolve`.

Studio SDL now submits the flushed indexed dissolve snapshots through its
normal rate-limited window presenter. The submission reads the bound target
buffer directly during the existing paint pass. Marcus applies the same C89
permutation to the authored-camera RGBA image at output resolution. It uses
the last image displayed by that camera as its outgoing surface. C89 supplies
both hosts' four-second dissolve and two two-second phases for fades.
The native comparison retained all 345,164,224 bytes over 832 cases.

For the visual checks, a temporary fixture starts the incoming scene on a
different camera. Both hosts matched retail's outgoing and incoming images and
all 60 authored frames for dissolve and fades through black and white. The
distinct-camera dissolve produced 174 distinct SDL images, up from one in the
frozen baseline. Marcus produced 376 and retail produced 234. The sample
counts reflect each host's capture cadence. With the original fixture, Studio
submitted 169 images during the effect and Marcus submitted 376. Each repeated
one distinct image while retail showed temporary colour changes. The
standalone, focused host, SDL and AddressSanitizer checks passed, and
the playback gate matched 480 retail frame commits. Evidence is in
`tmp/c89-transition-presentation`.

The retail dissolve snapshot now uses the shared C89 RGB555 remap for colours
from the active palette. The remap chooses the entry nearest the centre of the
same bin, resolving ties in palette order. Studio uses it during the indexed
snapshot copy. Marcus passes its outgoing scene's palette to the RGBA snapshot
remap before revealing the incoming image.

A temporary retail probe captured the outgoing indices, the dissolve snapshot,
and the palette. The palette stayed constant and 3,030 pixels changed during
the snapshot copy. The C89 result matched all 166,464 retail snapshot indices,
and 1,536 entries tested with GDI across six palettes. The original fixture
produced 168 distinct SDL snapshots, 374 in Marcus, 233 in retail and 234 in
V3DMM. In each capture, every pixel used either its remapped outgoing colour
or its final colour. Once a pixel reached its final colour it held it through
the end of the dissolve. The fade checks matched retail's endpoint images and
the playback gate matched 480 retail frame commits. Evidence is in
`tmp/c89-dissolve-colours`.

Marcus now composites dissolves across different background palettes using
the outgoing snapshot indices and the incoming raster indices. The C89
background loader retains the authored palette range so the controller can
overlay it on the outgoing display palette when preparing the target.
Interpolation uses the authored channel values before reserved-colour
normalization. The incoming rendering palette remains available for colour
lookup when the renderer provides RGBA or a caption replaces a raster sample.

A temporary movie dissolves between two stock backgrounds with 89 changed
palette entries. The comparison derives the possible blended palettes from
the original background resource and checks both possible pixel indices at
each position. It found a matching palette phase for every pixel in 232 retail,
233 V3DMM, 172 SDL Studio and 371 Marcus captures. Each host matched retail's
endpoint images and 60 authored frames. The frozen Marcus build fit two
endpoint samples out of 373. Evidence is in `tmp/c89-transition-palettes`.

Encoding trims empty edge rows/columns, retains interior empty rows, emits zero
continuations between long runs, and stores no opaque pixel bytes in mask mode.
Empty output retains the translated reference point. Row lengths must fit
signed 16 bits. C allocates one exact buffer after sizing, and reinitialization
preserves the old owner on failure. Writing sets canonical host-order metadata
before checking the destination size, matching native mutation order.

Reads preserve memory-source consumption and file-source retention, including
header-validation/object-creation failures after a successful byte transfer.
Fixed allocation or byte-read failures retain the source. This allocation
topology differs from adopting an existing HQ; the original successful bytes
and block effects are frozen separately. Row validation now runs in release
builds too, rejecting malformed lengths, overruns and trailing data before a
renderer or hit test can walk them.

The native fixture freezes 210,848 bytes over 800 constructors, 24 direct/factory
loads and 24 real BMP imports, including mask/upside-down modes, crops, foreign
headers/row tables, packed resources, block consumption and hit probes. SHA-256
is `A9AF8AC0605D7612E5175AFEA856765DC880627BFBC85A19B6996B9337B4D6F2`;
debug little-endian builds enforce FNV-1a `65C24F63`. Supplemental native failure
checks require balanced BASE/HQ/Pv allocations. The C-only fixture covers both
host orders, long runs, malformed rows, transaction failures, exact-size writes
and import/source ownership. Full gates are in `docs/tools/playback-regression.md`.

### World buffer ownership and background transactions

`world_buffers.c` owns World colour/depth/stretch buffers, dirty regions and the
retained background cache, mode dimensions, renderer-view descriptions,
construction/disposal ordering, mode rollback and background loading/control.
Native World fields and BRender pixelmaps borrow this state. Typed host services
provide allocation, locks, region creation, SDK scene setup and graphics blits;
camera/picking, render and export policy are described next; final representation
removal remains work.

Scale values stay 1/2/4, with a 4,096-pixel limit per rendered axis. Half flags
reduce dimensions only at scale 1; the extra stretch port remains exclusive to
half-Y/full-X at scale 1. Working RGB retains exactly one renderer lock until
replacement/disposal. Mode changes hold old buffers until allocation and
background reload succeed, then release old owners in native order. Failure
restores resources, descriptions, dimensions and flags without invalidating the
old buffers. No-op/invalid modes allocate nothing.

Background loading preserves fetch/priority/release order, dirty notification,
retain-before-release of the cache, full-mode depth transfer and nearest depth
sample selection in reduced/scaled modes. Partial colour writes remain if later
depth work fails. The reduced-mode temporary-colour allocation now checks the
new port rather than the existing destination, avoiding a null-port crash.
Depth span arithmetic is bounded before copies; no depth interpolation occurs.

Two original native traces, the original SDL trace and the converted trace match
71,376 bytes, SHA-256
`507B79FD5C425A459A0D16D15E87996AF4557EE82E491179EEC1DACC46D0FC19`.
Little-endian fixtures enforce FNV-1a `35BEFB80`. Real GDI/SDL port tests cover
buffer descriptions, modes, dirty bounds and background colour/depth bytes.
The C-only fixture covers initialization failures, mode rollback, cache and lock
lifetime, temporary-port failure and partial background writes. Full gates and
capture limits are in `docs/tools/playback-regression.md`.

### World camera and picking

`world_camera.c` owns persistent camera matrix/near/far/FOV and nearest-pick
state, camera publication/working-region dirty ordering, pick reset/dispatch,
coordinate conversion, actor-root owner traversal and rendered-bound conversion.
The native camera is an SDK mirror. Typed adapters retain SDK field access,
the actual SDK intersection query, region mutation and client callback dispatch.
Temporary native pick fields mirror the C result. Render/export policy is
described below; final SDK/object representation removal remains work.

Camera changes dirty the working region without setting the changed latch,
matching the original behavior. Half-resolution picking truncates negative
coordinates toward zero; higher scales multiply before subtracting pixelmap
origins. There is no viewport clamp. Equal-depth hits keep the first actor,
and a depth of scalar MAX is not selected. Rendered SDK bounds are inclusive;
the client rectangle is exclusive and retains the original negative-coordinate
rounding expression. Overflow now fails before SDK dispatch/client publication.

Two original native traces, the original SDL trace and converted native/SDL
traces match all 44,472 bytes, SHA-256
`1C21E2C115C4777BC39977DCA67A05A55A27866FD7E880F4053C0D367436C63A`.
Little-endian fixtures enforce FNV-1a `AD744449`. The fixture records optional
camera outputs, actual SDK hits, selection ties, origin offsets, full/half/scaled
modes and negative rendered bounds. C-only tests cover publication order,
integer limits, output preservation on failure, pick reset and actor-root routing.
Full validation and capture limits are in `docs/tools/playback-regression.md`.

### World rendering, material scopes and dirty-region copies

`world_render.c` owns the persistent changed latch, render sequencing, model-style
traversal, temporary material snapshots and restoration, clipped dirty RGB and
depth copies, prerender snapshots, background restoration dispatch and screen
damage transforms. Native World fields borrow the C latch. Typed services
supply actor fields, SDK rasterization, material-registry access, graphics-port
locks and blits, and application marking.

Immediate NONE roots receive begin callbacks before cleaning. Working dirt is
unioned into screen dirt before clearing; after render and material restoration,
root bounds populate working dirt and screen dirt is unioned again. The changed
latch clears last, including when callbacks mark it during rendering. Bounds
callback availability is checked for each root. Style traversal leaves the root
and bounded models unchanged while still visiting their descendants.

Material scope storage belongs to C and restores forward enumeration order.
Flattening preserves textured materials unless textures are also removed;
texture removal uses the retail solid grey ramp (base/range 15). Allocation or
snapshot-capacity failure restores the applied prefix and skips the SDK render,
leaving the world dirty for retry. Prerendering commits no snapshot after render
failure. The debug supersampled exporter uses the same C scope and restores its
callback/lock before returning failure. Export control is now C as described next.

RGB cleanup clips against buffer dimensions, validates actual pitches/spans and
copies only scanned spans, unlocking working then background even on failure.
It then releases scanner references/locks before any dirty-region mutation.
Successful earlier copies remain on malformed later spans. Drawing preserves
the original negative offset and ignores the supplied clip rectangle as before;
host blits perform clipping. Unrepresentable scaled actor rectangles/draw offsets
fail before publication/blitting. Screen region arithmetic remains a host service.

Two original native runs, the original SDL run and converted native/SDL runs match
528,048 bytes, SHA-256
`3C1559759C1D70B0ED937F918C3E6773805AB615640CE2CED9CE3A1B759D5EEC`.
Little-endian fixtures enforce FNV-1a `4FC7CD7D`. They record actual colour/depth
buffers, callback order, dirty regions, material state during/after rendering,
all eight style-option combinations, full/half/scaled modes, prerendering and
offset drawing. C-only tests cover rollback, locks, spans, style exclusions,
callback changes, output policy and snapshot failure. Full validation and capture
limits are in `docs/tools/playback-regression.md`.

### World bitmap export and palette ownership

`world_export.c` owns current-buffer/scaled bitmap export sizing, the 4096-axis
scale ceiling, native-view background fill, cache fetch/reference order,
temporary resource ownership, renderer descriptor construction, style/material
scope sequencing, render-bounds callback suppression/restoration, flush/lock
ordering, palette ownership and writer dispatch. Typed native services retain
graphics, file/palette-object and SDK rasterizer primitives. World's export
facades remain debug-only; the C API and C-only tests build in all configurations.
Native SDK/object/region representations and final facade removal still keep
the whole backend unfinished.

Requested scales 1..16 decrement until both axes fit, so a 640x480 view requested
at 16x exports at 6x (3840x2880). Scaled exports ignore live half/scale modes,
retain the original tight SDK row, leave live buffers/camera/dirt untouched and
write from the actual graphics-port pitch. No-cache ports retain default RGB
zero/depth FF fill. Background depth sampling intentionally ignores cached
origins, matching the native exporter's direct-row policy. The shared
`mm_world_depth_resample` now supports export factors 1..16, while live
buffer modes remain restricted to 1/2/4. Copies preserve opaque two-byte samples
without interpolation, validate spans and retain only the completed prefix on
failure. Current export writes the existing buffer without rendering.

Palette copies release after unlock on success and failure; current-buffer export
previously leaked them. Failed palette snapshots and malformed pixel/depth spans
now return failure before unsafe writes/rasterization. Scaled exports restore
materials and the bounds callback, flush/unlock, then write, releasing RGB before
depth in native order. Writer-level file semantics remain with the shared bitmap
writer; the export controller does not introduce atomic file replacement.

Two original native runs, an original SDL debug run and converted native/SDL
debug runs match all 13,188,586 bytes, SHA-256
`456793441243CD84817FB55F5BC5EF40B031B177D4040B459BC35CD8B2C2F246`.
Little-endian fixtures enforce FNV-1a `E235ADA9`. The 243 exports cover six live
modes, all eight render options, scales 1/2/4/16, depth occlusion, no-cache fill,
non-power-of-two scale clamping and live-state preservation. Supplemental native
and SDL debug allocation injection checks 66 attempts apiece, including the
current-buffer palette leak fix. C-only checks cover all scale factors,
transaction failure phases, prefix bounds, ownership, locks and callback/material
restoration. Full validation and coverage limits are in
`docs/tools/playback-regression.md`.

### Template construction, headers and default costumes

`template_create.c` owns resource factory selection/size estimates/failure cleanup,
template header reads/writes, ordinary template initialization, owned group
disposal and body/default-costume construction. Parents, sets and costume groups
now persist directly in `MMTemplateState`, alongside rest orientation, flags and
set/costume/action counts. The native Template embeds that owner and initializes
it explicitly; one borrowed state view replaces native-field read/write
marshalling. Typed list/group accessors, getters and debug validation read the
same C record. Local C snapshots retain the earlier publication points.
Parents, sets and costume groups publish in native order. Direct initialization
failure retains partial ownership, while the factory releases its failed object.
The C parent and set decoder converts opposite-endian indices. The costume
reader retains the authored byte order and decodes counts and IDs on access.
Costume and action counts retain dense-prefix lookup behavior. When GGCM is
absent, the fallback enumerates all costume children once per set using a
temporary zero-budget resource cache.

Default costumes are all fetched before any body mutation. Failed fetching
releases the fetched references without applying a partial costume. Body creation
uses the shared C body implementation and preserves virtual default-costume
dispatch. ThreeDText generation and action/accessory evaluation use the C modules
below. Resource object internals, String adapters and final Template
representation/facade removal remain dependencies.

Header writes define the formerly uninitialized two-byte padding as zero.
The name write deliberately targets TMPL even when given another chunk type;
failure deletes the newly added chunk while retaining its published number.
The legacy GGCM builder's first 50 matches retain native behavior; the 51st
now diagnoses/fails instead of overflowing its stack array. These defined
normalizations are explicit; unsupported native overflow/padding is not claimed
to match. Native block lifetimes remain scoped through temporary field/I/O
adapters, while construction/byte-order/failure policy is C89.

The normalized 306,048-byte before/after trace matches across 1,344 native cases,
with 613 successful loads and 478 bodies. Only undefined header padding is masked;
defined wire fields, groups, counts, names, body state and injected failures
match. Fixtures cover empty/nonempty templates, opposite byte order, missing
name/header/tables, generated GGCM rows, missing default resources and ownership
cleanup. Standalone C tests additionally exercise factory output invalidation,
virtual dispatch, direct-call partial ownership, header/name failures, dense gaps,
opposite host order and the fallback's 50/51 boundary.

The part-index storage change matched 31 native traces and the 480-byte
save/reopen fixture against `58942077`. The standalone probe matched 300
frame snapshots and 300 model and material snapshots, and the native
graph and stream providers matched on 300 frames. All 181 standalone
checks, 189 host checks and eight AddressSanitizer checks passed.
The two hosts matched 456 earlier seek observations and 360 viewport hashes.
Their timed playback matched 480 retail frame commits, and retail reopened
the Studio actor edit with the same actor state on four seeks. Both hosts
also passed the actor audio fixture. Captures and binary hashes are in
`tmp/c89-part-indices`.

### Template actions and accessory relationships

`template_runtime.c` owns template child-resource routing, action names, cel
wrapping/application, distance/count/flag/sound queries, costume indexing,
accessory detection/parent-set search and model-child comparisons. Template and
CustomMaterial facades call this shared C89 implementation in both hosts.
Native services provide field/container access, resource factories, retained
references, virtual action/model fetches and body setters. ThreeDText retains
its virtual fetch dispatch. ActionDefinition state/loading and primitive getters
now use the C action implementation described below. Resource object/container
adapters and final native facade removal remain dependencies.

Cel application fetches every model before publishing distance or changing the
body. Nil model IDs preserve accessory models while their matrices still update.
Failure releases acquired models without applying a partial pose. Cleanup clears
model slots in part order, frees temporary storage, then releases the action.
Action queries retain their individual output-publication order. Opaque scalar
output storage is copied by representation to bridge Windows long and C int
without incompatible typed pointer aliasing.

Accessory comparison checks the dense child-ID prefix, including the first
missing ID in the second costume; chunks beyond a shared gap are ignored, as
in native code. Parent-set search returns the first matching set/part in native
order and leaves its output untouched when none exists. The parent indices must
meet the native preconditions. A template without a resource cache has no
accessories, including ordinary transient ThreeDText instances.

The native fixture records 405 pose/query cases plus five accessory graph
variants. The 241,552-byte before/after trace matches exactly; independent C tests
cover model/allocation/fetch failure, zero parts, negative/extreme cel wrapping,
nil distance output, reference/publication order and sparse child graphs.
Playback evidence and remaining coverage limits are recorded in
`docs/tools/playback-regression.md`.

### Action definitions and optional sound ownership

`action_definition.c` owns the cel group, matrix array, optional sound
tags and flags in `MMActionDefinition`. It implements adoption of generated
tables, resource factory estimates and cleanup, header and table loading,
disposal, and getters. GGCL location entries use
native byte order. Cel and matrix payloads retain the authored byte order,
and accessors decode each requested record. The native facade supplies block
I/O, allocation, diagnostics and the TAG conversion at its sound query
boundary. Generation writes the cel parts and matrices directly into native-order
C tables. A successful action allocation takes both tables, and the text
builder releases them on failure.

Header flags and partial table ownership publish in native order. Direct
initialization failure leaves acquired tables owned by the caller; factories
release the partial object before invalidating outputs. Estimate-only reads do
not unpack/allocate. The final estimate intentionally retains the native omission
of optional sound-list size. Wire sound tags remain 16 bytes, ignore the stale
wire pointer slot and use a NULL borrowed runtime cache; their tags own no cache
references. Matrix/cel/part output layouts remain 48/8/4 bytes.

Disposal releases cels, matrices and optional sounds in that order. Sound
queries clear only the output type for high-bit no-sound markers and absent
tags. Invalid cel rows and matrix indices fail the checked accessors. Template
cel evaluation releases any models already fetched and returns failure before
publishing distance or changing the body.

The 2,268-case native trace matches 258,592 bytes without masking, covering
duplicated groups, native/opposite byte order, empty actions, missing/short/invalid
chunks, partial initialization, sound marshalling and allocator failures. The
baseline fixture manually detached/released leaked sound lists; the converted
fixture instead verifies destructor cleanup with allocation counters. Additional
checks exercise negative sound markers. Existing motion-sound corpus playback
is checked against retail; no dedicated retail marker-edge capture is claimed.

Standalone C test executables now undefine NDEBUG in every build configuration.
All 68 checks were rerun with assertions enabled; earlier Release pass counts
alone did not prove execution of assert-based checks. Debug MSVC and ASan
coverage were separate. Playback evidence, explicit changes and remaining
coverage limits are recorded in `docs/tools/playback-regression.md`.

The owning-table conversion matched 31 native traces and the 480-byte
saved fixture against `a0414aab`. It matched 300 standalone frame snapshots,
300 model and material snapshots, and 300 native graph and stream evaluations.
All 181 standalone checks, 189 focused host checks, the 10-check engine and
Marcus C89 syntax gate, and seven AddressSanitizer checks passed. The
instrumented standalone probe also reproduced all 300 frame snapshots.
Studio and Marcus matched 456 previous seek observations, 360 viewport hashes
and 480 retail frame commits. Retail reopened Studio's edited movie and
matched the actor state on four seeks. The actor audio fixture confirmed
silence before Play and both the ordinary and motion cues in each host.
Studio passed all eight VMM authoring and reopen checks. Marcus rendered
the retained VMM fixture with the same viewport hash.
Build identities, traces and captures are in `tmp/c89-action-tables`.

### Three-dimensional font resources

`font_resource.c` owns `MMFontResource` count/maximum/metric storage,
resource estimates, loading, authoring, disposal, metric queries and glyph lookup.
ThreeDFont is now a temporary facade around that C state. Native services provide
primitive allocation, blocks, child lists/graph operations, retained model fetches,
model bounds and diagnostics. ThreeDText generation now uses the C implementation
described below, along with C-owned string/tag orchestration and localized
action-name policy in `text_resource.c`.

Loading publishes character count before exact-length validation, maximum height
after validation, then each buffer before its read. Direct failure retains
partial ownership. Font factory failure invalidates outputs before releasing the
partial object; successful size publication precedes object publication. Packed
estimates assume 256 metric pairs; final estimates include owner plus both metric
arrays. The header's byte order is decoded, but foreign metric arrays deliberately
remain unswapped: native tested the already normalized header marker, so retaining
the array bytes preserves the actual legacy values rather than silently fixing
them. Dedicated foreign-font retail rendering parity is not established.

Authoring scans all child IDs and remembers the last 'i'. Empty input creates one
zero-metric slot. A zero-width space uses that 'i' model's dimensions. The supplied
native name remains ignored. Partial chunks/adoptions remain after failure;
output publication/invalidation follows array cleanup. C now releases the fetched
model when adoption fails, fixing the old leaked reference. Unrepresentable
counts/IDs/cache sizes fail before overflowing native 32-bit calculations. Glyph
lookup retains its unfiltered child-type search and missing-glyph diagnostic.

The 1,248-case native comparison matches 210,880 bytes without masking, including
packed/opposite-order loads, partial owners, sparse/empty font authoring, missing
models and allocation failures. Partial authoring payload bytes are undefined
and are not recorded; chunk sizes and adopted edges are recorded on failure,
and all successful wire bytes are compared. Baseline cleanup manually removed
the old leaked model reference; converted checks require no extra model reference
and balanced allocations. Additional checks reload authored fonts and resolve
their glyphs. All 69 standalone C checks run with assertions enabled. Full gate
evidence and remaining coverage limits are in `docs/tools/playback-regression.md`.

### Three-dimensional text generation

`text_generation.c` owns parent/set/costume list construction, atomic replacement
of generated lists/material references, cel/matrix/action generation, the action
cache, glyph fetch control, body adjustment/default costumes and generated-owner
disposal. `MMTextGenerated` holds the material, cached action and cache ID;
these are no longer separate C++ owner fields. Native services supply object and
table allocation, retained font providers, metric and glyph access, body
operations and template field views. The standalone evaluator calls the same
matrix generator for one cel and writes its outputs directly to the body.
The template view now borrows the same persistent `MMTemplateState` used by
ordinary templates. Text generation fills C89 parent and set lists with
root parents and set zero. Generated list replacement publishes directly into
the template state at the existing release, clear and replace points.
The generated-resource owner still holds the material and action references
and preserves the cache publication order.
The resource controller below owns
string/tag construction, change/save/load orchestration and localized names.
Remaining resources and final native facade removal still keep the full C89
backend unfinished.

`text_generation_math.c` owns all ten shapes and sixteen animation formulas,
reusing the shared C fixed-point/BAM/matrix primitives without a native BRender
dependency. It preserves the original Pi constants, negative arithmetic halves,
exclusive upper clamps, pre/post multiplication order, zero-angle rotations,
stretch-before-shape calculation and Circle-Z's final offset. Cel matrices remain
48 bytes and part specs remain two signed shorts. Byte characters preserve the
legacy unsigned-character font lookup; valid counts/indices/scalar sums follow
the native bounds.

List initialization acquires every replacement owner before publishing anything;
failure releases locals in native order and leaves existing members untouched.
Success replaces parents/sets/costumes/material in order, releases the cached
action and publishes counts/flags. A reused material is retained before replacing
its old owner. Cache misses release the old action before building; failure leaves
NULL/id -1. Cache hits retain without rebuilding, and cache ID publishes after
retain. Disposal releases material then action and retains the old ID, as native.

The 4,378,609-byte native trace matches without masking across all 800 shape/action
combinations for five strings, including empty normalization and high byte values.
Every generated cel/part/matrix and selected bound body poses are recorded. A
further 1,008 cases exercise construction, change rollback/body adjustment, cache
misses, direct builders, missing fonts and allocation injection. C-only tests cover
ownership/publication order, failure phases, cache invalidation/hits, zero-length lists,
glyph failures, body boundaries and math invariants. All 70 standalone C checks
run with assertions enabled; full gate evidence is in
`docs/tools/playback-regression.md`.

Direct generation and action adoption preserve all 800 generated pose cases,
29 other native traces and the 480-byte saved fixture against `4d21f8cb`.
The two allocation traces record 19 cases that now succeed after the copy
or scratch allocation was removed. In two further cases the missing-font
lookup now runs and still leaves the action cache empty. The verifier compares
new successes with the corresponding no-fault results. A C-only test compares
single-cel output against the generated table for every shape and action.
All 181 standalone, 189 focused host, 10 C89 syntax and seven AddressSanitizer
checks pass. Standalone frame and mesh snapshots and native graph/stream
evaluations each agree across 300 frames. The instrumented probe also matches
the 300 standalone frame snapshots. Both hosts match 456 prior seek
observations, 360 viewport hashes and 480 retail frame commits. The actor
audio fixture confirms silence before Play and both cues in each host.
Retail reopens the Studio actor edit with matching actor state on four seeks.
Build identities and captures are in `tmp/c89-generated-actions`.

### Three-dimensional text resources

`text_resource.c` owns construction, duplication, change rollback, info queries,
font-provider selection, template/child resource loading and saving, embedded-tag
enumeration and localized action-name policy. `MMTextDescriptor` holds shape
and font tag; `MMTextNames` holds the optional retained names table. Both
hosts use the same C controller through the temporary ThreeDText facade. Services
still provide native String primitives, object creation, block/chunk operations,
font providers and string-table entry access; those adapters are dependencies of
the unfinished full C-only backend.

Construction/change normalize empty strings to one space; loading does not.
Even a no-op change rebuilds generated owners. Failed changes restore text,
shape and font after the atomic generated-list initializer fails. Duplicates
receive fresh generated resources and inherit provider hooks only after success.
Runtime tag copying preserves cache bits without owning them; wire tags have a
zero pointer pad and load with a NULL cache. The 24-byte child record permits
trailing data and preserves native opposite-order handling. Direct read failures
retain published base/descriptor fields. Child save failures retain the parent
and output number; payload-write failure deletes only the child. Tag enumeration
clears error before allocation and publishes failure before releasing its list.
Localized lookup retains the first duplicate ID, warns only for a missing entry
in a present table, and clears the output on failure.

A 2,400-case native trace matches all 279,277 bytes without masking, SHA-256
`6C80CF373A4F7F8B6B749EF4CDA6BD7B92EAC799E58F882F9D96B64794EC9E94`.
It covers optional change masks, allocation failures/rollback/cache references,
provider inheritance, packed/foreign/short/missing child loads, factory/direct
loads and tag enumeration, and successful wire bytes/failed-save chunk graphs.
Undefined partial-save payloads are excluded. C-only checks additionally cover
both host byte orders, error-before-release, parent retention, custom-provider
failure without fallback, malformed-order diagnostics and name publication.
Full validation and playback limits are recorded in
`docs/tools/playback-regression.md`.

### Movie sound resources and dispatch

`sound_resource.c` owns MovieSoundMSND loading/factories, metadata queries,
MIDI/WAVE envelope and child writing, MIDI import orchestration, invalidation,
tool priorities/classes and playback dispatch. `MMSoundResource` holds the
weak source cache, child identity, style and volume. Separate C silent/invalid
slots preserve the native String placement and 328-byte x64 cache estimate
without packing pointers. Native adapters still supply object/String/block/
chunk/file/HQ transfer and sound-device operations. MIDI parsing, sound payload
factories and physical WAVE import policy now use the following C modules;
temporary native facades still remain in the unfinished full C89 backend.

The reader retains native foreign-header semantics: only the order/system
markers swap; style and volume remain in native byte order because the original
reader used the background mask. Direct failures retain already published state;
invalid sounds leave child/silent state untouched. Short headers now fail before
the original out-of-range read/assert. Factory estimates publish before allocation
and failed outputs invalidate before release. Cache links remain weak. Writers
zero the envelope's three undefined padding bytes. Parent-add failure preserves
its output behavior; later failures delete the parent and child but retain the
published number. Invalidation writes the envelope and removes the child before
publishing the in-memory flag. MIDI import releases its parsed stream on every
path and reports bad-sound only for parse failure.

Dispatch preserves signed queue identity, loop class/repeat limits, invalid-sound
early return, silent-sound stopping and queue behavior. MIDI suppression compares
the preceding child type/number and active instance, as native did, without
including cache identity. The last-MIDI state publishes after device operations.

The corrected native trace matches all 99,001 bytes across 1,320 cases, SHA-256
`E95D011B6E0F40EDFAF0D5E338DD845C15BD3569E3773B3FB4786E6AAD1CD899`.
It records actual child payloads by chunk ID, excluding only undefined envelope
padding. C-only tests cover both host byte orders, every writer cleanup phase,
failed-output publication, short headers and dispatch policy. A further 96
headless host dispatch combinations and repeated-MIDI checks verify adapters.
Full evidence and playback coverage limits are in
`docs/tools/playback-regression.md`. The pause after this slice has ended;
sound payload/import work is completed below. Body rendering remains migration
work.

### Sound payloads, MIDI parsing and WAVE import

`midi.c` owns the persistent `MMMidiParser`, variable-length timing,
running status, peeks, meta/SysEx handling and SMF import. C-owned scratch
buffers hold track parsers/events and the merged millisecond stream. Formats
1/2 merge/sequence tracks; other formats retain the first active track.
Track-count acceptance, equal-time order, SMPTE 29-to-30 handling, sequential
tempo carry and the legacy tempo-only `last` timestamp update are preserved.
Two-word integer scaling and unsigned wrapping avoid C99/64-bit dependencies.
Unsupported timing overflow fails instead of asserting/using undefined signed
arithmetic. The native facade retains object references, HQ allocation/locking,
physical reads and raw block serialization; it does not parse events or merge
tracks. These are temporary facade/allocator adapters, not a finished C-only
host integration.

`sound_payload.c` owns MIDI, SDL sampled-audio and classic AudioMan resource
factory sequencing, estimates, output publication and failure cleanup. Opaque
object/block/stream callbacks retain native cache/device types outside C.
The AudioMan path transfers its stream to a successful object and releases it
on failure. The SDL path reads encoded bytes before object allocation and
releases partial objects after decoder/allocation-size failures.

`wave.c` owns RIFF views, compressed-format selection/validation, legacy-format
recognition, fact trimming, PCM headers and complete MS ADPCM encoding. Predictor
error sums use two unsigned words and preserve the native tie order and exact
bytes. The compressed reader keeps first fmt/data and last fact; legacy-format
recognition still returns at the first fmt, and the separate fact inspector
keeps its last-fmt policy. Final odd-chunk pad omission remains accepted where
it was before. External SDL, GSM and MP3 decoders/resamplers remain C callback
or codec adapters. Import selection preserves original compatible bytes when
encoding would not save space, retains the 346-byte fast path and uses PCM for
up to 1,500 normalized samples because of AudioMan's short-ADPCM duration bug.
The owning audio buffer accepts signed 32-bit byte sizes without imposing the
collection subsystem's 128 MiB group limit.

`wave_import.c` owns physical import transactions, name/temp-file sequencing,
resource writing and cleanup. SDL import calls the common C encoder through
external decode/resample services. The classic native controller keeps
AudioMan/ACM as external codec services, writes RIFF/fmt/fact/data in C and
preserves the original source-size comparison against RIFF payload length
(excluding its eight-byte header). Callback tests cover each failure phase and
publication boundary. Physical filename/file APIs and device codecs remain
host adapters; final facade removal is separate migration work.

The frozen pre-conversion native trace matches all 314,180 bytes (160 MIDI
imports with parser peeks and 80 PCM/ADPCM pairs), SHA-256
`C98FC38D594FD6DA2B36C312CBB888004F6AB6457A3BFD02ABAFA5C19149644A`.
`sound-payload-test` checks its frozen FNV-1a fingerprint automatically on
little-endian hosts. `engine-sound-payload-test` links with C alone and covers
malformed/truncated data, allocation faults, factory ownership and all native
RIFF-write/import failure phases. Debug resource injection now succeeds for
90 MIDI imports rather than 84 because native parser/list allocations were
removed; successful payloads retain their separate frozen trace. See
`docs/tools/playback-regression.md` for host checks, retail comparisons and
coverage limits. No Marcus UI tests were added.

### Studio command dispatch

`command_dispatch.c` owns the pending queue, its command group references,
the priority-sorted filter table and the in-progress filter cursor. Tail
enqueue and head push retain their original order. Filters at the same level
run newest first. The dispatcher adjusts the cursor when a handler adds or
removes filters and rereads the active target after callbacks. Enable queries
retain their separate rule of visiting filters without applying the routing
mask. Burial clears pending commands for a destroyed handler and releases their
groups. C89 also controls the modal checks for dispatch and enable queries,
walks the requested graphics object's ancestry, and releases the active group
after routing even if a handler fails. Native code supplies the typed active
command, graphics parent access and member-function calls.

The input sequence in `command_macro.c` uses queued commands before asking
for a tracking sample. During macro playback it clears the live payload before
substitution and passes the stop command through. Both recording and replay
suppress key polling. Modal rejection reports the original command before
releasing its group, then permits eligible filters to observe it. Each handler
still passes its own modal check. Native callbacks handle platform samples,
replay I/O and modal feedback.
The native fixture preserves the order of queue, tracking and modal callbacks
and checks payload disposal during rejection and replay. The C-only fixtures
also check modal ancestry and cleanup after a failed handler. In playback suite
`20261120T055541Z-57928d5c`, both ports matched the 60 retail frame identities
and their own previous seek images. SDL matched all 12 retail sound requests.
The authored pause held the same frame for 696 ms, compared with 705 ms in the
earlier SDL capture and 732 ms in retail.

The command map data starts with C89 binding records. The resolver searches
derived and base tables for the first mask-compatible explicit binding, then
uses the most-derived eligible default. An explicit empty binding still hides
base entries. The same module decides when an explicit command is enabled by
default and when a native enable callback must answer. Dynamic handler ID
allocation uses an unsigned 32-bit cursor, restarts at an occupied ID, and
skips the high-bit boundary marker when the cursor wraps.

`engine-command-dispatch-test` covers C-only routing, ownership, failed growth,
recursive dispatch, C89 map precedence, ID collision and wraparound, and host
error recovery. The native core regression checks inherited binding selection,
default command enabling and the HID collision sequence before and after the
conversion. It also covers queue and filter order, filter edits during dispatch,
key retrieval and recorded-command replay.
After the binding and ID conversion, suite `20261120T054334Z-47fa8b78` matched
the 60 retail playback frames on SDL and Marcus and the 12 retail sound requests
on SDL. Each host's 60 seek images matched its pre-conversion capture.
An authored 33-tick pause held the same frame for 702 ms before the dispatcher
conversion and 705 ms afterward. Retail held that frame for 732 ms.
In capture suite `20261120T051214Z-92bf58a2`, SDL and Marcus matched the 60
retail frame identities for `actor-event-edit` in seek and normal playback,
both from a fresh process and after replay. Each port matched its previous 60
seek images. SDL matched the 12 retail sound requests in both playback runs.

`command_macro.c` owns the recording and replay state, the retained archive
reference, and the GLCM command rows. It groups consecutive matching commands
by their fields and handler identity. Commands carrying groups retain separate
GG children. C89 handles the repeat cursor, target burial, command admission,
archive cleanup and completion notification order. The GL header and 32-byte
rows retain their native byte order and platform tag. The reader checks the row
width and rejects grouped commands with a repeat count greater than one.
Kauai supplies chunk I/O, GG serialization and HID lookup.

`engine-command-macro-test` covers recording, replay, target burial, aborts,
invalid platform, width and repeat fields, and rollback after write failure.
The Kauai core regression reopens the saved archive and replays five commands
through the native dispatcher. Its 204-byte GLCM and GG snapshot matches the
pre-conversion snapshot on x86 and x64. In capture suite
`20261120T053329Z-aae75c30`, normal movie playback matched the 60 retail frame
identities on both ports and all 12 retail sound requests on SDL. The 60 seek
images for each port matched its pre-conversion capture.

### Studio application time and alarm clocks

`clock.c` owns the scalable application clock, the alarm clock registry and
scheduled alarm storage. Application time retains wrapping 16.16 scaling and
continuity across scale changes. Alarm clocks project milliseconds to 60 Hz
ticks, keep pending alarms when restarted, and dispatch tied alarms newest
first. Late dispatch retains the original clock-slip policy. Input resets,
target removal and schedule changes made during dispatch also run in C89.
Kauai supplies the system timestamp and allocator. Command delivery remains
native and reports whether a direct callback destroyed the clock.

`engine-clock-test` exercises the C89 clock without Kauai, including wraparound,
allocation failure and a callback that frees the active clock. The deterministic
Studio command trace matches the pre-conversion build byte for byte. SDL and
Marcus replayed all 60 captured frames in retail order, and SDL matched the
12 retail sound events for the same fixture. An authored 33-tick pause added
about 531 ms in SDL and 562 ms in retail, measured against each build's ordinary
frame interval. SDL's total time on the paused frame changed from 701 ms to
702 ms across the conversion.

### Sound queues, mixing and streamed MIDI runtime

`sound_manager.c` owns device dispatch, sound-class filtering, activation and
suspend nesting, sound ID wrapping and named/anonymous queue selection. Queue
reuse keeps the original reverse search and last-empty-queue retention.
`sound_queue.c` owns reverse instance/class scans, nested pause/resume, stopped
entries, completed-prefix cleanup and packed system-volume scaling. It retains
the original inverted front-priority test rather than changing queue behavior.
The native `sndm.cpp` classes supply typed object/reference, C-backed collection
and virtual device/locking callbacks. Their temporary collection facades remain
part of the final host-integration work.

`audio_mixer.c` owns persistent sampled-audio queues, playback positions, loop
counts, grouping, transactional synchronization, mixing, clipping and statistics.
PCM and sample identity are borrowed until a successful replacement/removal; the
C++ adapter retains shared sample references and serializes calls. Rendering
allocates nothing. Queue order and final clipping preserve the native mixer.
`audio_clock.c` supplies unsigned two-word arithmetic so offsets, counters and
MIDI delays retain 64-bit range without C99 integer types.

`midi_mix.c` owns cached stream construction, state-event reconstruction when
seeking, queue progression, priority/tie arbitration, preemption, loop skips,
background expiry and completion/restart control. The persistent C mixer state
tracks outstanding buffers and stop/wait phases. Host callbacks perform mutex
release/wait/reacquire and device buffer transfers; notifications retain their
original lock boundaries. `midi_render.c` owns persistent frame-clock sequencing,
tempo/fraction carry, repeated buffers, volume and returned-buffer lists. It
borrows immutable event storage and performs no allocation or client completion
notification during rendering. SoundFont synthesis, SDL resampling/output,
WinMM/AudioMan device implementations and thread primitives remain host code.

`engine-audio-runtime-test` links with C alone and covers arithmetic boundaries,
allocation failure, transactional PCM replacement, queue state, manager dispatch,
MIDI timing, large seek batches and cleanup. Frozen pre-conversion native traces
cover 4,000 sampled mixer steps (`D1FA52C3`), MIDI rendering (`28316524`) and 2,200
cached MIDI events with every seek position (`6B4B5C94`). Existing synth, SDL
device and asynchronous MIDI tests exercise the real adapters. Fingerprints use
their documented native little-endian/word-width conditions; they are separate
from retail waveform/timing comparisons. No Marcus UI tests were added.

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

`tag_list.c` owns the dependency list's payload and location buffers.
Studio builds the list in C89 during movie open, save-format selection, and
asset cleanup. The native tag manager supplies cache I/O.
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

`mm_chunk_construct_forest` owns destination construction. Copied imports
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

`reference.c` now owns common reference initialization, retain/drop arithmetic,
final-release dispatch and marked-file release policy. Native BASE embeds an
`MMReference` instead of its own scalar count, and its retain/release methods
delegate to C. The counter remains four bytes on the Windows hosts, enforced by
a native size assertion. Cached/marked objects can be retained from zero.
File release keeps marked zero-count owners open; invalid release diagnoses
before clamping to zero. Destroy callbacks can free the owner and counter, with
no subsequent C access. A dedicated C suite checks that behavior under ASan,
including a destruction callback that frees itself and releases another owner.

Resource objects expose a borrowed C counter, and the cache holds a direct
pointer to its owner's C counter. Resource objects link to the C cache record.
`resource_cache.c` updates both counters and handles zero-reference retention,
detach, priority changes, and disposal directly. A detached object can own the
last cache reference, which it releases during destruction. During cache disposal,
C clears the links of surviving attached objects. Document and graphics
overrides use the same C drop primitive. Native allocation, final destructors,
debug tracking, physical view cleanup and COM audio adapter counters remain
outside this common C reference owner.

The `engine-c89-checks` build target gathers every engine C test executable from
the module's target list. Root `core-checks` depends on it, preventing registered
C tests from remaining missing or stale when individual dependencies are omitted.

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

The manager stores typed C cache pointers and calls their C query functions
directly. Its factory transfers one reference to the cache owner. The manager
releases it through C on disposal or failed insertion. On a miss, the file
provider supplies the physical extent and packed bit. The cache owns the
`MMOwnedBlock` and releases its file or memory after the operation. Preload's
estimate and final read use the same block, including mutations made during
the estimate. Recursive reads open separate blocks. Cache hits and scope
rejections skip block construction.

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

`crf.cpp` supplies native class allocation, destructors, state accessors,
diagnostics and typed readers. It forwards public cache operations to C,
including release through the BASE interface. The host compares reader
identities as typed function pointers and calls each reader through a borrowed
`DataBlock` view. The view updates the C owner in place. The chunk provider uses
the same extent and packed-bit lookup as the native `FFind` path. GenericHQ and
generic-object wrappers remain native adapters. Container adapters, source
discovery and engine objects remain migration work.

`engine-resource-cache-test` covers recursive reads, relocating storage,
speculative allocation fallback, initial/growth failure, cleanup/lifetimes,
eviction scoring and manager precedence. MSVC AddressSanitizer passes. The native
`resource-cache-test` retains all 1,624 frozen baseline bytes, including cascading
releases, distinct readers, scoped lookup and error stops. All eight native
snapshots and all 51 CLI baselines remain exact. Twenty-five standalone and
thirty-eight focused native checks pass; no Marcus UI tests were added.

The cache dispatch change preserves the 1,624-byte native cache trace, the
31 actor and resource traces, and the 480-byte save fixture. The model,
material and texture traces match the previous build. Five real movies
match across 300 standalone frame and mesh snapshots and 300 graph/stream
evaluations. SDL Studio and Marcus reproduce all 456 previous seek
observations and 360 viewport hashes, and 480 frame commits match retail.
Retail reopens the Studio actor edit with matching state on four seeks.
Both hosts play the ordinary and motion sound cues, Studio passes eight
VMM workflow checks, and Marcus matches the retained VMM viewport hash.
All 181 standalone checks and three AddressSanitizer checks pass. The 195
focused host checks include 11 C89 syntax checks and two native tests that
passed on a serial rerun with an isolated local temp directory.
Build identities, traces and captures are in `tmp/c89-cache-dispatch`.

The lifetime change adds a C fixture that frees a cache owner when its last
detached object is released. It passes under AddressSanitizer alongside the
reference and block ownership fixtures. The frozen cache, model, material and
texture traces, 31 actor and resource traces, and the native saved movie remain
exact.
The five graph/stream fixture runs and 300 standalone frame and mesh snapshots
also match. The 181 standalone and 195 focused host checks pass, including
11 C89 syntax checks in the host configuration. The hosts reproduce 456 seek
observations and 360 viewport hashes, and match 480 retail frame commits.
Retail reopens the Studio actor edit with matching state on four seeks. Both
hosts reproduce the ordinary and motion sound cues. Studio passes eight VMM
workflow checks, and Marcus matches the retained VMM viewport hash. Evidence is
in `tmp/c89-cache-lifetime`.

The block ownership fixture starts preload from a packed file extent and
replaces the extent with a memory handle during estimation. The final read sees
that handle. The cache releases the handle after success, estimate failure and
read failure. AddressSanitizer passes the fixture. The four frozen cache and
renderer-resource traces, 31 actor and resource traces, and saved movie fixture
match byte for byte. The 300 frame and mesh snapshots and the 300 graph/stream
evaluations also match. Both hosts reproduce 456 seek observations and 360
viewport hashes and match 480 retail frame commits. Retail reopens the Studio
actor edit with matching state on four seeks. Both hosts play the ordinary
and motion sound cues. Studio passes eight VMM workflow checks, and Marcus
matches the retained VMM viewport hash. All 181 standalone checks, three
AddressSanitizer checks and the SDL runtime compile check pass. The 195
focused host checks include 11 C89 syntax checks and one material test that
passed three isolated reruns after failing to create a temp file in the
parallel run. Evidence is in `tmp/c89-cache-blocks`.

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

`mm_chunk_save_copy` now owns the save-a-copy traversal and commit order.
It pins the source index, checks source/compacted destination positions, copies
each payload, inserts the corresponding record, unpins on every exit and only
then commits the destination. Unlike whole-file relocation, even empty chunks
invoke copy and retain the running destination offset. The native transfer
primitive preserves the original fixed record (including expanded runtime IDs)
and variable children/name bytes, replacing only the C-supplied offset/size/flags.
Destination creation, failure disposal and physical I/O remain host services.

`mm_chunk_heap_allocate` chooses the first fitting free range and consumes
its beginning, deleting exact fits. Append reuses physical trailing bytes before
extending the file. `mm_chunk_heap_free` merges either/both adjacent holes
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

`mm_chunk_save` owns the save sequence shared by Studio and Marcus. It
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

`mm_chunk_write_index` now owns the CHN2 index-commit policy shared by
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

`mm_piece_table_write` in `piece_table.c` owns piece selection, both storage
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

`mm/file_text.h` and `file_text.c` own FileLocation text conversion policy:
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
diagnostic callbacks. `MM::Archive` keeps `engine-core` private and exposes
typed records, owned page results and structured errors. The original core
entry points remain available to existing runtime code. This first public
boundary covers read-only archive access; movie playback/editing APIs and
independent concurrent engine instances remain separate work.

`archive_reader.c` owns the Archive reader's payload and string-table cache,
the two decode budgets, and record dispatch for MVIE, SCEN, ACTR and TBOX.
Caption decoding resolves its exact RTXT child reference from the shared
chunk index. Archive inspection and Preview's movie graph reads share the
decoded payload cache. The C++ facade adapts file access, output containers,
diagnostics and native name conversion. The C-only chunky test verifies
big-endian record reads and an invalid caption link, and the archive API
checks budget errors and cache reuse across chunks.
Before-and-after `3mm dump` runs agreed on 20 inputs with 28,270 identical
chunk rows, and both readers rejected a fixture with swapped index rows.
The original-content preview suite passed. Capture suite
`20261120T065315Z-9f39ab6a` matched 60 retail frame identities on SDL and
Marcus in seek and playback, and 12 retail sound requests on SDL. Both
hosts also matched all 60 of their previous seek images.

The C89 archive reader can open a physical stream and own its validated
index. It reads stored extents, keeps one decoded chunk and its parsed string
table, and supplies the same record, name and movie-directory operations as
the live reader. Both paths share the pre-v3 name conversion rules. The
normalized `3mm dump` command and Marcus's actor, prop, material, font,
background and sound catalogues use physical files through the C89 native
file services. Marcus object copy uses the same reader for the source table.
Standalone C reads passed on original backgrounds, sounds, templates and
materials. The normalized dumps matched the previous reader across 20
archives with 28,270 identical chunk rows. Four CHN2 v1 and v2 fixtures
matched the previous names and payloads in both byte orders. The archive API
fixtures pass through both open paths. Capture suite
`20261120T073350Z-18abf67d` matched 60 retail frame identities in seek and
playback on SDL and Marcus, and 12 retail sound requests on SDL. Both hosts
matched all 60 of their previous seek images.

The C89 archive reader now also owns the movie directory transaction and its
retained session. It reads authored scene links and source rows through the
cached graph. A rootless archive yields an empty directory. A movie with no
source table gets the retail source, and a malformed table or an aborted
visit leaves a previously retained session usable. Marcus hands the same
reader to Preview, which borrows the session for scene and frame queries.
The standalone C test covers cancellation, retry, sparse scenes, duplicate
source IDs, source-table budgets and release. Before-and-after directory
probes agreed on 26 files under the default and constrained budgets,
including repeated reads and reads after close. The original-content preview
suite passed. Capture suite `20261120T070611Z-95c31305` matched 60 retail
frame identities on SDL and Marcus in seek and playback, and 12 retail sound
requests on SDL. Both hosts matched all 60 of their previous seek images.

`MM::Preview` now supplies a second public boundary for read-only 3D asset
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
`core_platform_bridge.cpp`, `platform_text.cpp`, `cmd_core.cpp`, `clok.cpp`, `crf.cpp`, `file.cpp`,
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

Studio preference policy now uses the C89 `mm/settings.h` implementation:
typed byte payloads, get/set/default behavior, missing/error reporting and handle
cleanup accept a C backend table. `native_settings_win.c` retains the original
registry keys, 32-bit view and read-only value enumeration. `native_settings_posix.c` supplies locked,
atomic file replacement under XDG/Application Support configuration roots, with
typed opaque payloads and corruption checks. Explicit native contexts own
temporary-number/mutex state; two-word arithmetic preserves 64-bit numbering
without C99 integer types. `settings_store_bridge.cpp` translates existing
`settings_store.h` callers and contains callback exceptions until C cleanup has
finished. C-only consumers use `mm-native-settings` and caller-owned policy
records; Windows enumeration borrows buffers through a callback. Studio selects
the matching backend. The INI frontend and default native bindings remain adapters.
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
Windows SDL and POSIX. `pixel_stretch.cpp` adapts the original indexed
double-stretch entry points to the shared C89 renderer. Exact-byte portable-port
tests run on Windows and headless Linux alongside the offscreen GDI comparisons.
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

`libraries/engine/src/avi_movie.c` supplies AVI 1.0 demuxing, indexed Video 1
decoding, palette changes, delta-frame seeking and PCM assembly in C89. It
owns bounded input, the validated packet index, per-frame palettes, PCM and
one indexed reference image. It sizes the index and PCM from the validated
lists, and validates each delta before modifying the reference image. The
Kauai adapter borrows the PCM and supplies the destination BGRX buffer.
`avi-corpus-win-test` uses Windows VfW as a compatibility oracle. All 24
original clips (4,458 frames) match its decoded pixels, forward/backward seeks,
fractional timeline conversion and exact PCM on x86 and x64. `avi-movie-test`
also checks synthetic palette changes, nested records, nonzero stream starts,
malformed packets, truncation and size/time limits. The C-only test exercises
allocation failure at every ownership stage and verifies that a failed read
preserves the current movie and its borrowed PCM.
After the decoder conversion, Movie Techniques matched the earlier SDL build's
130 decoded frames, 477,736 queued and consumed PCM bytes, and settled completion
image. In movie capture suite `20261120T062346Z-97e3e9a7`, SDL and Marcus matched
60 retail frame identities and their earlier seek images. SDL matched all 12
retail sound requests.

`video_playback.c` owns Studio's native and SDL stream scheduling. It handles
start, stop, seek, command-filter registration order, redraw suppression and
frame bounds. Silent native streams use Kauai's wrapping millisecond clock.
For SDL, C89 also computes the PCM seek window and advances frame time from the
device's consumed input bytes. It preserves the output-buffer latency and
drains the audio tail before finishing. Silent SDL streams use monotonic time.
The video player supplies timestamps, decoding, device I/O and presentation.
`engine-video-playback-test` checks clock wrap, seek alignment, failed-start
cleanup, audio drain and final-frame duration without a host SDK. The native
video host and SDL video checks passed. Both SDL Studio builds completed the
original Movie Techniques clip with all 130 frames and 477,736 queued and
consumed PCM bytes. The completed scene matched pixel for pixel after the
original project script settled.
Movie capture suite `20261120T061013Z-46c4ba63` matched 60 retail frame
identities on SDL and Marcus, and 12 retail sound requests on SDL. Each host
also matched its 60 previous seek images.

The same C89 module coordinates Studio's Win32 AVI child window. It owns the
open and close sequence with wave sound suspension, pause and restart, child
placement, first show and palette refresh. The Win32 adapter issues MCI commands
and maps the graphics object to its parent window. MCI supplies the current
position and handles keyframe seeks. The playback test checks callback order,
partial open cleanup, failed restart and redundant placement. The original
`GOSTU.AVI` produced matching open, seek, placement, pause, completion and
close results before and after the conversion on x86 and x64.
`CNTDWN5.AVI` also matched on x86, and `PROJ1.AVI` matched on x64.
SDL Studio's Movie Techniques run matched its previous 130 decoded frames,
477,736 queued and consumed PCM bytes, and settled completion image.
Capture suite `20261120T063807Z-a1324f59` matched 60 retail frame identities
in seek and playback on each host and 12 retail sound requests on SDL. Each
host also matched its 60 previous seek images.

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

File backends: C89 `mm-native-files` through `platform_files_bridge.cpp` on
Windows and POSIX. `stub.cpp` is selected
only on Windows. See [file/path policies](platform-files.md).

Account lookup and user-data roots: C89 `mm-native-users` through
`platform_users_bridge.cpp`. `native_users_win.c` retains WNet/provider errors
and shell-folder discovery; `native_users_posix.c` retains effective-uid account
lookup, bounded retries and native environment discovery. `native_user_paths.c`
shares Linux/macOS path selection and bounded output. C-only consumers use
caller-owned records from `mm/native_users.h`; native UI/object integration and
removing the temporary C++ value adapter remain conversion work.

Plus codegen: `kcdc_386.h`, `kcd2_386.h` on x86 only (kauai codec asm).

Linked dependency: `Threads::Threads`. Raw allocation uses `mm/heap.h`; recursive
mutexes, coalescing events, thread-local diagnostic tokens and clock hooks/ticks
use C89 `mm/native_runtime.h` in `mm-native-runtime`. Windows supplies critical
sections, condition variables, TLS and QPC; POSIX supplies pthread primitives and
the same monotonic clock epoch as the existing host. Explicit C contexts own
their keys/counter/hook. Native clock scaling uses exact two-word/96-bit arithmetic
from `mm/runtime_math.h`, with a fast path for common QPC frequencies.
`core_platform_bridge.cpp` translates C++ calls/exceptions and retains the
process-lifetime default binding needed by global destructors. The legacy class
headers retain standard-library types only to size/check unused ABI reservation;
they construct no std mutex/condition-variable objects. This keeps diagnostic
allocation profiles until native facades are removed. C-only consumers use
explicit owned handles/contexts; their backend has no C++ dependency.
Files/text use native or portable backends;
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

`mm-brender-resources` builds the C89 model, material and texture bindings
against `mm-engine-c89` and `BRender::Libraries`.
`brender-core` uses `kauai-core`, `BRender::Libraries` and `mm-brender-resources`.
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

Links `kauai-core`, `brender-core`, `mm-engine-c89` and `mm-brender-resources`.
Public headers are in `inc/`.
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
Both save paths build the wire GST directly from C89 roll-call storage.
Native code allocates the destination chunk and supplies the positional
writer and graph edits.
Loading borrows the decoded GST stream and builds the live table in C89.
Native code locates and unpacks the chunk and supplies character conversion
and tag operations. Load rollback and movie teardown close references from
the C89 rows.
Studio's roll-call queries and edits also use the typed C89 table. The native
adapter translates between counted character buffers and Studio strings and
supplies actor, tag, and notification services.
Autosave prunes unused actor rows from the typed table. Scene composition
loads imported rows into a temporary C89 table and transfers their tag
references to the destination when the merge succeeds.
The C movie state embeds the live roll-call table. The movie owner initializes
and disposes it, including tag references, and publishes a replacement only
after loading succeeds. Native services provide storage allocation, tag
operations, and archive I/O.
Headless saves, Studio autosave, and scene persistence also share C89 chunk
title I/O. C encodes and decodes the existing counted strings and removes
empty names. Kauai reads and writes the encoded bytes, invalidates the
chunk's runtime ID after a name write, and supplies text conversion.

The archive adapter supplies its live C89 chunk editor for movie-root links,
appended scenes, empty roll calls, and storyboard metadata. The scene writer
stages and adopts both replacement event groups before releasing either old
link. If staging fails, it drops the new groups. Portable source tables and
background start groups also adopt the new link before releasing the old link.
Native source registration keeps its unlink-before-adopt order. Kauai still
allocates chunks and supplies payload readers and writers.

New scenes and actors move from their allocated numbers to their requested
numbers through `mm_chunk_move`. The writable index supplies runtime ID
access for moves and copies. A same-type move preserves the runtime ID and
retargets parent links. Scene saves use the writable index for sounds, actors,
captions, thumbnails, and cleanup of a failed candidate scene.

The same C89 index handles actor registration, placement and track changes,
scene reframing, camera and sound edits, caption replacement, and rich-text
body children. It also wires the paint and depth chunks for Marcus
storyboards. `MMMovieArchiveEditor` supplies payload services and the live
index to these paths. Caption body replacement adopts the new body before
releasing the previous link, including when another box shares that body.
The movie and embedded-asset adapters build their read views from that same
index. The view captures the current row count and borrows the row and child
readers. Callers reacquire it after changes to row positions. C89 uses the
same key and child searches for live reads and mutations, with unreadable
records reported through the existing chunk status values.
Studio passes one `MMChunkEdit` per archive to its scene, save, and sound
services. Scene reordering, composition, saved-root refresh, autosave, and
asset collection look up the entries they edit through that index. The
sound service uses it to find slots, adopt sounds, and remove a copied sound
if adoption fails. Copy and import choose a movie slot from the working
archive and link the sound in the destination archive. A failed physical
save retains the sound link.

`mm/movie_runtime.h` owns the live Studio document's client, world, sound
queue, and thumbnail palette references. C89 orders blank startup and named
open through the existing movie and save owners. For VMM, it opens and binds
the extracted working file while using the original path for the title and
read-only check. Failed named opens drop the temporary archive reference and
tear down the partial document before ending the long operation. On close,
C resumes paused audio, drops the movie session and saved file, stops
playback, restores volume when needed, and releases the scene and roll call
before the client, world, sound queue, and palette. Native code supplies
container preparation, object factories, and renderer and audio effects.

Studio writes an actor through `mm_actor_store` when saving a scene or taking
a scene undo snapshot. The C89 writer stores the header, route, and events,
links embedded templates and actor sounds, and attaches each embedded costume
material to the replacement actor through the C89 chunk editor. A foreign
material can be copied or reused in the destination archive. The wire event
uses that archive's chunk number and the live event retains its original tag.
Reusing a material preserves its
loner flag. If a newly copied material cannot be adopted, the C89 writer
discards it. Dropping a failed candidate scene releases its adopted children.

Before a scene is detached, `mm_movie_archive_preserve_scene_assets`
projects which chunk references will be removed. The preview follows a shared
child only after accounting for all of its parents. Once the preview succeeds,
the C89 editor marks the endangered background and material roots as loners. Their
subtrees remain reachable for event tags in older scenes and for scene undo.
The save sweep can still remove unused material loners and leaves graph-owned
resources attached.

Scene-sorter imports rebind document costume tags while visiting the cloned
actors for roll-call remapping. The source archive supplies material identity
even when its chunk number overlaps another movie's material or its material
is outside the cloned graph. C89 adopts the material under the cloned actor
and writes a replacement track when the number changes. It publishes the new
track link before releasing the old link, preserving tracks shared by another
actor. The adapter exposes the destination's live C89 editor and supplies
source archive and payload access. If writing or linking a replacement track
fails, C89 deletes the staged track and leaves the old track linked.

That traversal also resolves document-owned background tags in cloned scenes.
The source archive supplies the background subtree, including standalone
backgrounds outside the cloned movie graph. C89 attaches the subtree to the
scene and changes the number in the start event when the copy was renumbered.
Scene saves use the same asset-linking routine. A loaded scene retains its
resource cache for local background tags. Dependency collection reads external
background children for precaching and leaves local child resolution to the
loaded scene.

The scene sorter installs sound links on the cloned scenes before publishing
them. C89 visits actor and frame cues and uses the live editor to attach a
missing scene slot to the matching slot in the cloned movie's sound bank.
Existing scene links keep precedence, copied sound roots keep their loner flags,
and external and silent actor cues keep their authored representation. This
keeps the sound identity across chunk renumbering and
allows retail playback to resolve each cue from its scene.

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

The C runtime owns the mutable route and event tables and embedded TAG
references. Track cloning deep-copies both tables and acquires independent
references for opened TAGs. Track swaps support undo restoration while runtime
state copying preserves object identity. Table mutations preserve validated
layouts and close owned TAGs before removing or replacing their rows.

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

`mm_actor_frame_run` sequences a frame from explicit resolved-step and
playback-mode inputs. It delivers each entrance query and event command to the
adapter with the current actor state.
Core owns entrance ordering, route/event clipping, initial freeze/action timing,
ordinary-sound skipping versus motion-match registration, cel advancement and
final pose/cel/sound directives. It owns no renderer, audio queue or application
services. `mm_actor_frame_display` preserves motion sounds, body positioning
and cel selection in that order. Intermediate frames update pose. Lifetime
checks use refreshed host metadata after entrance queries. Studio performs
best-effort effects after a nonfatal frame error. Preview stops and also
propagates errors from motion sound and cel services.

The driver returns the result after the last command. Queries may refresh event
frame caches while the table structure stays fixed. On a nonfatal event failure,
the scan advances and the applied-event cursor stays on the failed event for
retry. Entrance failure skips final cel and pose effects.

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

`mm_actor_seek_run` drives the C89 actor seek protocol for Studio and Preview.
It handles same-frame seeks, entrance checkpoint rewind, quick backup and
fallback, intermediate and final frames, and accumulated evaluation failures.
The Studio adapter clears
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
