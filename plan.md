# 3DMMForever modernization plan

## Hard constraints

- **Backwards compatibility is non-negotiable. Retail 3D Movie Maker is the behavioral reference.** Existing movies must retain their loading, playback, editing, resource-reference and timing semantics. Do not silently repair files, substitute assets, ignore errors or introduce fallback behavior that changes retail's result. A failure reproduced in both engines is a shared failure, not a port regression to suppress. Compatibility-breaking changes belong in 3DMMPlus, not 3DMMForever.
- **`.3MM` files written by 3DMMForever must remain loadable and playable by original 1995 3DMM without losing movie semantics.** Validate legacy loading and retail save/reopen behavior before claiming compatibility.
  - Additive chunks at unused chids/ctgs only. No in-place struct growth (the loader rejects on size mismatch).
  - No new `SceneEventType` values. The 1995 reader doesn't understand them; using them in a movie breaks it.
  - **Adding new *optional* chunks is fine; making any new chunk required is forbidden** — required chunks force a `kcvnBack` bump beyond 2, and the 1995 reader's `DataVersion::FReadable` check rejects.
  - Compat-breaking work belongs in the future `3DMMPlus` fork.

## Non-goals

This plan does NOT cover:

- Modernizing the kidspace scripting language or `chomp` chunked-source pipeline (the bytecode VM ships in 1995 3DMM and matches its behavior; replacing it adds regression risk on the user-facing kidspace flow without helping the `.3MM` compat mission).
- Replacing or removing BRender (kept as-is; only ported, not rewritten).
- Replacing the 3D pipeline with Vulkan / OpenGL / Metal — see *Future / 3DMMPlus* note below.
- The SoftImage-DKIT-dependent SITOBREN tool or new file format design (those go to 3DMMPlus).
- macOS/Linux Mac toolbox compatibility — kauai's `*mac.cpp` files are not the SDL backend.

## Future / 3DMMPlus

Items deferred to the future 3DMMPlus fork rather than 3DMMForever, in rough order of attractiveness:

- **Lua-based kidspace runtime.** Replace the in-house stack VM (`scrcom.cpp` / `screxe.cpp` and their graphical variants) with Lua 5.3+. Match: stack-based, integer-friendly, coroutines map to kidspace's "fire-and-wait" patterns, ~150 KB interpreter vs. ~3-4 kLOC of in-house VM, real debugger and profiler available, MIT-licensed. Rough scope: write a `.cht`-source-to-Lua transpiler, port the ~30-50 kidspace intrinsics (`ChangeStateThis`, `FRunScript`, etc.) as Lua C functions, build a GOK ↔ Lua table binding for cross-GOK variable access, validate against the existing 500+ scripts. Not Python — Python's footprint (5-15 MB embedded), startup cost, and GC pauses make it the wrong fit for embedded UI scripting, even though it's the right choice for engine bindings (Project 2). Both languages can coexist in the same binary because they target different layers. Worth doing in 3DMMPlus because it enables modding and gives content authors real tooling; not worth doing in 3DMMForever because compat with 1995 behavior is the priority and the existing VM works.
- **Modern 3D renderer** (Vulkan / OpenGL / Metal, full color, lighting, full match-move camera animation) — the headline 3DMMPlus charter item. Requires a new file format that breaks 1995 compat and a renderer rewrite, not a port. Custom camera *poses* are achievable in 3DMMForever via UI-9's BKGD-embedding trick; what 3DMMPlus adds is *animated cameras within a shot* (continuous match-move, key-framed pans/zooms) — that requires new `SceneEventType` values which 3DMMForever's compat constraint forbids.
- **New file format** with extension capacity beyond 1995's `MovieFilePrefix` size budget. Imports `.3MM` files for backwards play.
- **Kidspace UI redesign** for modern resolutions (1920×1080+) and full color, beyond the 640×480 8-bit-palette constraint of 1995 3DMM.

These items are forward-pointers, not commitments. Listing them here makes it easier to reject scope creep into 3DMMForever — when someone proposes "wouldn't it be cool to add X," the answer is either "yes, in 3DMMForever" or "yes, in 3DMMPlus, see Future section."

## Scope decisions

### Accepted: convert the engine to a shared C89 backend

**Direction confirmed 11 November 2026:** convert the engine implementation
from C++ to C89 and use the same backend from Studio and Marcus. This replaces
the previous rejection of a C rewrite and the assumption that a C API over a
permanent C++ engine is sufficient. The existing playback regression tests
support the conversion and gate each migrated slice.

The engine owns movie data, resource resolution, evaluation, playback policy,
editing and serialization. Studio and Marcus remain host applications over
that backend. Studio's controls, easels and other UI code can remain C++;
Marcus keeps its C application code and host adapters, and uses Dear ImGui
through cimgui for its entire application GUI as part of the C89 work described
below. Convert the Kauai and BRender wrapper functionality needed by the engine,
or replace those
dependencies with C89 services. A C++ dependency hidden behind a C header is
an intermediate migration state, not completion of the backend conversion.

The detailed sequence and acceptance criteria follow below. This work takes
priority over the older Project 0-4 ordering wherever they conflict.

## Shared C89 engine migration

### Current status (18 November 2026)

- The shared content VFS core, `libraries/content/src/content_vfs.c`, is C89;
  `content_vfs.cpp` remains a C++ adapter. Reuse the existing core.
- Marcus has C89 application sources and a C API in `src/marcus/player.h`,
  but its player, playback and Archive/Preview implementations remain C++.
  Its browser and preview-process components also contain C++ host code.
- Marcus's application widgets and text input use cimgui. Its dialog,
  storyboard and transform controllers use C89. The
  [Marcus UI milestone](#marcus-ui-convert-the-entire-gui-to-imgui-through-cimgui)
  records the application boundary.
- `engine-core` already groups stored data and actor evaluation. Studio and
  Marcus use the playback command, frame pacing and scrolling policies in
  `mm-engine-c89` in `libraries/engine/`.
  The remaining core is C++ and links `kauai-core` and `brender-core`.
  The `engine` target still owns further editing and playback functionality.
- `MM::Archive` and `MM::Preview` provide useful separation, but their
  C++ interfaces are migration inputs, not the final engine interface.
- The first movie-data slice adds bounded decoded-chunk streams and C89 MVIE,
  roll-call and Archive GST-layout readers. Studio, Archive and Marcus call
  these decoders. Later slices below migrate storage, codecs and persistence;
  tag ownership and complete save/edit integration remain dependencies.
- Archive uses the shared C89 chunk index for metadata and child references.
  C89 owns its decoded payload and string table cache, preview budgets and
  header dispatch for movies, scenes, actors and captions. The Archive
  adapter supplies native file access, name conversion and public results.
- The C89 archive reader also builds the movie directory and owns the retained
  movie session used by Marcus Preview. It validates authored scene and source
  links, applies the source-table budget and supplies the retail source for a
  movie without a source table. The adapter publishes the public directory.
- Preview checks archive budgets against C89 index entries and stored packed
  headers. Its common palette, shade table, and saved scene thumbnails load
  through the C89 archive reader. The common resource archive can stay a
  physical stream across Preview replacements, and external content builds its
  resource cache after admission checks. Studio and Preview
  share the texture-map stream decoder.
- Preview resolves sound sources in the C89 session and reads movie sound
  links and decoded audio from the archive reader. Editable archives provide
  their current graph so sound lookup sees live changes.
- Preview also reads scene admission, captions, timing, and actor tracks
  from that archive graph. Caption TEXT borrows the stored bytes while the
  other caption chunks use decoded streams.
- Preview reads background lights, palettes, camera placement and colour and
  depth bitmaps through the same graph. C stages placement before reusing the
  decoded cache for images and commits it after both images load.
- The archive reader opens immutable physical streams through the C89 chunk
  index, codec and payload cache. `3mm dump` and Marcus catalogue reads use this
  path. Marcus also uses it to read source names for an object being copied.
  On movie open Marcus reads the movie directory and metadata through the same
  C89 reader. Read-only Preview caches retain the validated wire index and the
  same native C file used to parse it. Preview opens by path also use that
  reader. Live opens retain the registered working archive or the existing
  legacy file through a typed C89 cache and read the current chunk graph.
  The archive API links the Kauai file and text adapters and the C89 engine
  directly.
- Preview's renderer caches now have C89 owners for both immutable indexes
  and live archive graphs. C retains their source until the last dependent
  object is released. Typed model, texture, template and material readers
  use that cache directly. Studio also uses the C89 owner for template
  costume-table caches. Name lookup and font copies read from the same source
  as the resource payload.
- Material, costume, action, template and 3D-text readers also consume the
  cache's root block. Graph links and child payloads use the same archive source
  as the root. The legacy template costume-table builder keeps its own cache
  budget and inherits that source. Stored names, font glyphs, sound metadata,
  and event-track replacements also read from that source. Font copies
  traverse the stored index with the shared C89 chunk copier and write the
  selected subgraph to the editable preview file. The copier and the native
  block add and replace entry points use the same C payload transfer, including
  for packed chunks and copies within one editable archive.
- Marcus creates text drafts directly from the shared TMPL and TDT codecs.
  C89 binds the preview descriptor's stored block, copies the font subgraph
  through the shared chunk copier, and patches the local tag in the original
  byte order. Authored lighting, textures and placement use the shared material
  writer. Renderer objects are generated when the template loads.
- Marcus uses C89 for 3D-text name validation, clipboard name conversion and
  caption text encoding. Caption edits normalize line breaks and use ANSI
  when the native code page round-trips the text with equal byte and UTF-16
  unit counts. The caption adapter supplies native code-page services.
- Caption preview and save use the same C89 edit sequence. C89 detects no-op
  saves, selects default and property updates, and decides when to retain the
  existing rich-text body or link a replacement to the edited box.
- Marcus caption edits now load and mutate C89 rich-text property arrays and
  argument groups. Draft preview reads those same collections. The native
  adapter handles archive blocks, file locations and font conversion.
- C89 owns decoded and replacement caption text during editing. Draft preview
  borrows the normalized UTF-16 words, and save encodes them into TEXT using
  the shared rich-text writer. Formatting-only saves retain the authored
  stream and encoding.
- The shared archive editor reads and writes native caption box headers and
  resolves their rich-text links. Marcus and Studio read and write rich-text
  bodies through the same C89 archive operations. Studio supplies its live
  property and argument objects through the shared collection streams. C89
  writes the live piece table into an owned TEXT block and retains that extent
  for post-save redirection. It also adopts the body into TBOX and unwinds the
  link if metadata publication fails.
- Studio scene saves and Marcus scene creation use the shared C89 archive
  editor for SCEN headers, names and event-group links. Both new groups
  are linked before either previous group is removed. Studio's event
  tracks and Marcus's stored camera and caption edits use the same dense
  group writer, which preserves the requested byte order.
- Studio's live scene serializer builds its temporary wire tracks directly
  from the C-owned live tracks. The C89 transaction also allocates and
  links actor, caption, sound and thumbnail chunks through the shared
  archive editor. Studio supplies typed object serialization, sound-tag
  conversion and thumbnail rendering.
- Studio also loads decoded scene events directly into the C-owned tracks.
  Studio loading and Marcus preview and presentation resolve scene children
  through the same reader. Live background events store C tags from loading
  through editing, playback and serialization.
- Studio creates, releases and unwinds scene events through those same C89
  tracks. C89 locks the frame track while resolving sound tags and checks
  each packet's tag span before touching its references. Native services
  release actor and caption objects and resolve resource tags.
- Studio dispatches frame and start events from the C89 tracks. Rewind uses
  the same event search as Marcus preview. Actor and caption insertion
  and removal use the C89 start track directly, as does actor replacement.
- Studio edits camera cuts, pauses, backgrounds and queued sounds directly
  in the C89 tracks. Scene trimming and background replacement use the same
  sound-tag cleanup as scene disposal. Sound editing and saving copy track
  packets through one C89 operation that reacquires the payload after
  allocation.
- Studio seek, replay, range extension and prerender scheduling use the
  C89 frame track and actor and caption lists directly. Studio, Marcus
  preview and scene archive writes share the opening-camera traversal.
- Studio's C89 scene owner holds the frame and start tracks and the
  scene's resources directly. Failed loads unwind the rows present in
  each track. Native accessors read the owner for the movie, background,
  thumbnail and cached sound. The owner also records the active background
  tag used to resolve camera resources.
- Studio's C89 scene owner keeps the primary actor and caption selections
  with the borrowed scene rosters. Content edits, undo and thumbnail drawing
  call the C89 selection policy directly. Studio supplies visual effects and
  notifications.
- Studio's live scene metadata and playback cursor share the C89 owner with
  the event tracks and rosters. Loading fills that state directly, and the
  C89 title reader runs after both event tracks load. Saving, seek, replay,
  trimming and playback startup use the same scene state.
- Studio's live scene sound packets and stored sound edits use the same
  C89 child and tag records. Packet copies, undo, playback, and cleanup use
  those records directly. The native host supplies resource operations and
  the C89 codec writes the legacy 20-byte tag records.
- Marcus sound imports, built-in sound copies and cue edits use private C89
  archives over SDL streams. The C archive owns the live index, payload
  blocks, packed decoding and save. The host supplies historical text
  conversion and audio normalization.
- Marcus materializes scene, camera, actor, animation and costume edits
  through the same private archive. Object clipboard transfers and
  storyboard resource generation, embedding and export also use it.
- Marcus reads and saves 2-D captions through the private archive. C owns
  the caption edit transaction and retains the stored TEXT extent through
  geometry and formatting edits.
- Shared Preview writes animation and costume track drafts, 3-D text
  materials and preview font bindings through the private C89 archive.
  The native C file provider opens the drafts, and material textures use
  the same destination file services as their headers and transforms.
- The strict C89 syntax checks cover the migrated engine and native services
  and the Marcus C frontend. The standalone C-only probe exercises actual
  file open, seek and model reads. The live shared session still needs
  end-to-end verification with C-only resource and renderer integration.
- The chunky-file slice now reads CHN2 headers and GG indexes in C89 and shares
  live chunk/child lookup, enumeration and graph validation through `kauai-core`.
  It also accepts GG indexes with unpadded rows, as used by some v3dmm
  content archives.
  The subsequent slices below migrate writable groups, free maps, writes,
  embedded-forest decoding and stored-name policy.
  Studio and Marcus use the same C binding for writable row, child and name
  edits, table loading and CHN2 directory writes. The C owner binds the
  live index to both file heaps, handles deferred free maps, runtime IDs
  and cached payload locations, and runs Save and Save a Copy. The host
  supplies file operations, target names and diagnostics.
- Dense/allocated string-table mutation, searches, packed-string reconstruction,
  legacy byte maps and portable encoding conversion now use C89. Kauai supplies
  object/header adapters; Windows supplies native code-page and locale services.
- The collection storage foundation now owns `GroupBase`'s two buffers in C89,
  including allocation lifetime, growth/fallback policy, locks, duplication and
  ordered I/O. Arrays, groups, string tables and chunk indexes no longer use HQ
  handles through `GroupBase`. Host fixed-memory diagnostics and DataBlock
  callbacks remain C++.
- Dense/allocated array and group mutations and GL/AL/GG wire-header policy now
  use C89, including slot reuse, padded byte transfers and merges. The native
  classes retain object/pointer/diagnostic adapters.
- GST headers, memory-stream storage/edits, live file-piece lookup/fetch,
  DataBlock range/unpack policy and packed-envelope decode dispatch now use C89.
  Host callbacks still provide HQ ownership, file access and allocation diagnostics.
  KCDC/KCD2 algorithms and packed encoding now also use C89, preserving exact
  native encoder bytes and decoded results. Primitive host allocation/lock
  services remain C++ callbacks.
- File-piece storage/lifetime, split/merge, memory/file/cross-stream replacements,
  rollback and compaction now use an owning C89 piece table. File references and
  positional I/O are host callbacks; a native GeneralGroup object is no longer
  required by FileByteStream.
- DataBlock byte/HQ I/O routing, block/file copying, handle detach/normalization,
  codec HQ transactions and block packing now use C89 policy. Host callbacks
  retain physical allocation, locks and file I/O.
- DataBlock now owns a C89 state record, including file/HQ lifetime, adoption,
  replacement, temporary-storage selection and file materialization. Pack/unpack
  commits and prior-file releases also live in C. Native diagnostics and primitive
  services remain callbacks.
- FileByteStream destination-window orchestration and FileLocation text/BOM/
  encoding-selection policy now use C89, with bounded fallback buffers and
  explicit guards for legacy write-window overshoot. Host code-page conversion
  remains a callback.
- CHN2 index persistence now computes index/free-map positions, serializes the
  fixed prefix and commits index, optional free map and header in C89. Existing
  collection serialization remains shared.
- Whole-file save sequencing, packed chunk relocation, empty-offset handling,
  file/map reference transitions and recovery after commit/naming failure now
  use C89. Filename resolution and physical file services remain native.
- Save-a-copy now uses C89 for pinned record traversal, payload-copy/insertion
  order, destination locations and final commit. First-fit free-space allocation,
  adjacent-hole coalescing and logical-tail trimming also use C89. File selection
  and physical I/O remain native services; free-map loading now uses C89 below.
- Unique chunk numbers, low-level insertion, replace/create rollback, payload
  commit, runtime-ID invalidation selection, reference decrement and deletion
  sequencing now use C89.
- Graph enumeration and its owning stack, descendant searches, child adoption,
  count-overflow rollback, unlink/deletion and child-ID reordering now use C89.
  Native facades retain graph/index lookup and byte-transfer primitives.
- Payload/child swaps, chunk identity moves and parent-link repair, graph
  copy/clone sequencing, the owning copy-number map, runtime-based graph matching
  and runtime-ID allocation now use C89.
- Compact-index runtime-ID storage, chunk-name storage policy, serialized String
  read/write policy and codec registration now use C89. Reopen transfers runtime
  cache ownership explicitly; native code-page conversion, expanded-index fixed
  records and physical storage remain host services.
- Embedded forest export/import now uses C89 descriptor encoding/decoding,
  preorder serialization, parent-stack ownership and child reconstruction.
  Native facades retain destination file/container creation, payload transfer
  and index insertion primitives.
- Free-map ownership, storage, lazy read/discard policy and serialization now
  use C89. Index materialization owns reusable C scratch storage, assembles child
  records and sequences legacy-name conversion through the host text callback.
  Native DynamicArray free maps and index-loader C++ vectors are removed.
  Native allocation diagnostics and physical services remain adapters.
- Main/extra storage selection, zero-size allocation rules, cache copy/rollback
  and read-through fetch fallback now use C89. Cached location publication
  preserves runtime IDs and other metadata. Native callbacks provide C heap
  access, file creation/copy and fixed-record updates.
- Container option mapping/latching, mark/zero-reference cleanup, packed/forest/
  loner mutation and reopen ownership sequencing now use C89. Failed reopen
  restores the prior index, files, free maps and compact runtime cache; replaced
  or rejected owners are disposed after publication.
- Open/create sequencing, existing-owner lookup and associated-resource disposal
  now use C89, including partial construction cleanup and shared file references.
  Native class allocation, name resolution and physical file primitives remain
  adapters.
- Resource-cache tables, cacheable-object reference/detach/priority policy,
  eviction and resource-manager lookup/ownership now use C89. Two-word signed
  arithmetic preserves wide resident accounting without C99 types. Native
  callbacks retain class allocation, readers, file/reference primitives and
  virtual dispatch; these adapters still require replacement.
- Registry links now belong to explicit C contexts; forest destination construction
  and failure cleanup also use C89. The native facade still supplies the default
  process context and physical file/object primitives.
- The persistent container owner record is now C89: files/index references,
  free maps, runtime cache, flags and deferred-map offsets no longer have native
  duplicate fields. Save publication, storage-policy views and file error
  aggregation/reset also use C.
- The writable chunk index now owns C table storage and compact/expanded C
  records. Group-object allocation, mutation, locking, size/flag access and wire
  output no longer require GeneralGroup or native record methods.
- Deletion, adoption cycle checks, live copy, and graph matching now walk the
  same C89 view of the writable chunk index. C89 owns the walker and resolves
  chunk identities again as row positions change. The native archive exposes
  a borrowed editor for record and allocation primitives. Adding and replacing
  payloads attaches transfer buffers for the duration of each operation.
- Tag open/save/duplicate/close, child construction, fetch dispatch and cache
  clearing now use C89. Tag lookup uses the shared cache and resource manager
  for both editable and read-only archives. Local tag open and save call the
  shared C89 graph copier directly with borrowed file or cache bindings. The
  C tag list owns group storage and preserves legacy sort, duplicate and
  borrowed-reference behavior.
- The source catalogue now owns C records, path handles and manager references.
  Local/media decisions, cached misses, retry/install sequencing, ordered manager
  construction and file routing use C89. Source names now use owning C storage,
  including SID/name lookup, merging, splitting, provider dispatch and GST writes.
  Studio's external content tags use C89 resource managers and caches. Read-only
  sources use the validated C index and native C file provider shared with Preview.
  Media sources and open live archives keep their shared archive graph, including
  across file-cache reopen. Camera backgrounds and sound playback use those
  caches directly.
  Studio loads movie source names through the shared archive reader and autosave
  owns the source GST block through serialization and child adoption in C89.
  Local/media directory search order also uses C89. Physical filename/drive calls,
  native locale services, UI prompts, engine objects and final callers remain.
- MovieData's working archive cache, actor table, movie number, actor-ID limit,
  title and scene count live in an owning C state. Studio opens and creates the
  working cache directly. Autosave, document-local tags, scene and actor
  loading, and scene composition and undo borrow or retain that same C89 cache.
  Empty initialization, transactional load/replace, save ordering and roll-call
  tag cleanup and marshalling use C89. The owner creates roots and replaces
  roll-call links through the shared archive writer. It loads roll calls from
  the owning resource cache. Studio opens sessions for movie
  loads and scene switches from the live graph in C89. Studio's version check
  and Preview's session opening share the C89 graph root reader. Native file
  bindings and caller facades remain.
  Runtime and wire roll-call names/extras, editing buffers, GST
  loading/translation and serialization use owning C storage. The runtime
  actor table lives directly in the C movie state.
- Scene metadata and counted names now live in C state. Archive-backed header read/write,
  event-layout validation, frame/start child replacement, create sequencing and
  failure cleanup use C89. Native file/group adapters and runtime scene-object,
  playback and editing ownership still remain.
- Actor persistence now uses C89 from live Studio save and load through header
  normalization, checked ID adjustment, PATH and GGAE storage, embedded tag
  opening, template selection, and legacy lifetime recovery. The 40/44-byte
  headers, mixed-width Add angles, resource order and partial-save behavior are
  retained. Studio and preview share the typed decoders. Native services
  supply resource access, chunk creation and byte I/O.
- Actor track state, table/tag ownership, transactional creation/cloning, tag lookup
  and ordered cleanup now use C89. Studio and preview borrow typed views of the
  tables they own in C. Preview also uses C for draft cloning and event-track
  replacement and rollback. C89 binds the selected cache's event chunk and
  decodes it directly. Costume and animation drafts reuse Studio's C actor
  track writer. Native services supply cache resolution, scratch-file creation
  and saving, tag references and body/costume operations.
- The portable movie session loads and owns the same typed PATH and GGAE tables
  used by Studio and preview. Its route view checks node reads during evaluation,
  and malformed tracks preserve caller outputs and report bad-file errors.
- Actor value-state initialization, rewind/origin updates and pure events now
  use C89, together with route advancement/interpolation and guarded 16.16
  arithmetic. Studio, Marcus and the session store the same C actor state.
  Studio's normal seek, frame, event and pose calls update that state directly.
  Native resource effects and editing adapters still remain.
- Actor one-frame command sequencing now uses C89-owned sequencer state. Entrance
  pre-scan/order, acknowledgement protocol, sound suppression, cel latches, failure
  continuation/abort and result flags execute in C. Native table access and
  effect callbacks remain temporary facades.
- Actor step metadata policy, lifetime/cache calculation, stalling, backward steps,
  quick backup and outer seek command sequencing now use C89. Per-step rollback,
  partial progress, callback observations and the enabled retail fixes are retained.
  Studio seek reads its first and last frames, recording state and dirty lifetime
  flag directly from the C89 actor owner. Studio and Marcus animation edits bind
  their lifetime fields to the same C89 updater. Native template metadata,
  diagnostics and body/audio effects remain.
- Actor pose evaluation, weighted route orientation and fixed matrix/angle math
  now use C89. Native pose code supplies table views and copies matrices into
  BRender at the SDK boundary. The C implementation uses fixed-point arithmetic.
- Actor event dispatch, partial action/cache state, entrance costume rollback and
  material fetch/apply/release ordering now use shared C89 policy in Studio and
  Marcus. Marcus timeline evaluation uses the same state policy with body/sound
  effects disabled. Native template/body/resource and sound adapters remain.
- Actor runtime sound dispatch and motion-match selection now use C89, with
  C-owned motion-list storage shared by Studio and Marcus. Replacement, cloning,
  imported-tag updates, priority choice and fetch/queue/release failure ordering
  execute in C; native resource/device implementations remain.
- Studio and Marcus dispatch sound events directly from the C89 actor event
  table. Ordinary playback resolves local tags and writes them back to the
  table before fetching audio. Motion-match bindings borrow their tags from the
  track. Studio sound edits use the existing C89 event editor to update rows,
  and actor and scene sounds use the same typed movie tag resolver.
- Sound setting/default insertion, volume edits, queries, deletions, frame sound
  detection and bulk tag-resolution policy now execute in C89. General event
  deletion also uses C for cursor updates and sound-indicator ordering. Native
  services retain resource/device operations and undo integration.
- General actor event insertion, merge scans and insert/merge/apply sequencing
  now execute in C89, including cursor updates, sound-queue identity, Move
  accumulation and lifetime-range notifications. Sound editing calls this C
  editor directly; its temporary native `insert_apply` callback is removed.
  Resource/body services and final facade removal remain.
- Actor action changes, step reservation/insertion, action fill and transform fill
  now execute in C89. Action edits call the existing C sound editor and event
  evaluator directly. Metadata failure, partial edits, lifetime invalidation,
  freeze/step ordering and borrowed transform-comparison views are retained.
  Stage entrance/removal, recording, undo and queue/runtime integration remain.
- Costume setting and costume fill now execute in C89, including custom body-set
  resolution, reservation, current-to-incoming comparison, tag-aware deletion and
  resource release ordering. Native services retain material/template/body lookup;
  these resource/body implementations still need conversion.
- Route-point insertion, event relocation after node insertion/deletion, event
  searches, reachable-subroute lookup, single-frame Add deletion, truncation and
  forward deletion now execute in C89. Hide/reset/rewind sequencing is also C,
  using native table/body/default-costume primitives. Epsilon clamping, wrapped
  offsets, held-frame handling, dirty/range ordering and terminating event/tweak
  ordering retain the preceding implementation's behavior.
- Backward deletion and initialization-event propagation between subroutes now
  execute in C89. Orientation weighting, angle averaging, body-position policy
  and lifetime/recording-cache decisions are shared C as well. These edits
  call the C89 seek evaluator directly. Native services supply body/template
  metadata, primitive BRender distance math and scene effects.
  Copied tagged payloads now retain their own references before merge/application,
  fixing the borrowed-copy ownership bug without changing logical event data.
- Actor placement now executes in C89, including authored-point detection,
  camera fallback and clipping midpoint, floor projection and text height.
  The native adapter only queries the background/camera and template kind.
- Stage entrance/removal and whole-subroute deletion now execute in C89,
  including reservation order, early cache extension, neighboring-event
  inheritance, position/step/freeze sequencing, hide/default/rewind and seek.
  Removing an entrance can reveal an earlier subroute that then receives Remove.
  Scene admission, roll-call placement, explicit stage entry with undo and
  the cut command's surviving-actor path call these C89 stage operations.
  Studio binds the live actor and its scene, template and renderer effects.
  Roll-call placement captures and resets its undo snapshot through the C89
  actor owner before passing ownership to the view.
- Recording validity, record-step sizing, begin/move/end sequencing, mouse
  smoothing, route sampling, ground clipping and rejoining now execute in C89.
  Timestamp differences wrap at 32 bits; displaced event/tag handling, end-of-
  route freeze/step and BUG1961/BUG1973 behavior retain native ordering. Studio
  recording rolls back through the C89 actor owner, which restores the snapshot
  and seeks to the saved frame. Marcus advances draft recordings through the
  C89 Preview evaluator. Native services supply BRender distance and Studio's
  scene-wide seek.
- Local/world rotation, normalization, scale/stretch, route tweaks and composed
  movement now execute in C89, including orientation weighting/angular throttling,
  ground clipping, event fill and partial-failure ordering. Sooner/later subroute
  shifting is C89 too. Native callbacks retain BRender primitives, scene/body
  operations, cached origin publication and scrollbar notification. Undo,
  remaining actor/movie/scene integration and final facade removal remain.
- Actor construction/disposal, duplication, reset and undo restoration
  operate on a live C89 owner with evaluated state, recording state, lifetime,
  origin, resource handles, template tag references, and the owned track and
  motion-sound tables. C creates, clones, swaps and disposes those tables
  directly. Studio and preview use the motion-sound store without a native
  wrapper. The native actor supplies resource operations, renderer effects
  and view invalidation.
  Actor undo creation, toggle, redo and snapshot ownership
  now execute in C89 too, including name swaps, replacement/removal, failure
  ordering and sound-queue selection. Scene/body/resource implementations
  and remaining edit integration still need conversion. Composite
  move/rename undo now uses C89 for factory/disposal, child reference ownership,
  ordered context propagation/dispatch, name swaps and partial failure policy.
- Studio and Marcus preview now replace live text actor templates through
  C89. Studio adjusts its live body, selects the current cel and removes
  costume events for removed body sets. Preview adjusts and poses a duplicate
  body before committing its template and body handles. Studio's actor
  prerender decision and pull-tool world coordinates also execute in C89.
- Actor edit transactions for action changes, stage add/remove, normalization,
  costumes, sound binding and deletion now execute in C89. The transactions
  call the C89 owner and edit functions directly and sequence snapshots,
  rollback, undo registration and optional snapshot transfer. Studio's
  non-undo stage, costume and normalization entry points share those owner
  operations.
  Studio's movie controls call the C89 transform owner and the costume and
  sound undo transactions directly. Sooner and Later record the destination
  frame in their undo state after a successful seek.
  Sound binding marks undo for sound playback. Deletion reads its cursor,
  entrance frame and surviving events from the C89 actor owner and refreshes its
  lifetime through the C89 updater. Deletion and stage edits call the C89
  route editor and actor seek directly, including seeks after switching
  subroutes. C89 clones, registers, resets and releases the snapshots. All
  edits require a snapshot before they run, and a failed removal releases its
  snapshot after rollback. The remaining movie, scene, body and resource
  implementations still require conversion.
- Full-actor and route-only copy use the C89 actor owner directly. C allocates
  the copy through the shared actor factory, slices typed routes and events,
  sequences tag retention and merges, rewrites current cels and event
  locations, and rewinds or disposes the destination. Studio supplies resource
  callbacks, movie sound-tag resolution and BRender distance.
- Route paste uses the C89 owner for path splitting and splicing, tagged-event
  removal, later-subroute index shifts, floating and terminal steps, and body
  positioning. Studio route editing and Marcus preview animation drafts share
  the C bindings for path and event table mutation and reservation.
  The movie controller calls the C89 route editor, actor owner and undo factory
  directly. Paste failure keeps the edit prefix. Undo failure restores the
  snapshot onto the current selection. Movie 3D text edits share snapshot
  capture and release, with Studio handling template rollback.
- Full actor paste uses the C89 owner and typed tables for retiming, sound
  adoption and row removal, with shared scene binding and tag ownership.
  Text template transfer uses the shared C89 tag saver. Movie sound
  resolution and saving use the same tag services, and cross-file sound
  copies use the shared chunk copier before movie adoption.
  Actor paste checks, saves and adopts its sounds through those C89 services.
  Actor sound editing uses the same movie binding for local sound adoption
  and bulk scene-tag resolution.
  The scene controller calls the C89 paste operation before admitting the actor.
  Scene actor undo and redo use the C89 actor owner to clone and reset the
  snapshots for actor addition, deletion and replacement.
  Studio actor edits and Marcus preview costume and animation drafts share the
  C event table binding for row mutation and tag ownership.
- The actor clipboard owns its counted name in C and calls the shared C actor
  copy and reset-duplication paths. Studio supplies the localized label and
  movie/view hooks. The movie edit controller also routes Studio's movie-view
  clipboard commands and pointer clicks, including actor cuts, route-only
  paste and text-box format priority.
- Playback, movie-data, container-reading, string-storage and collection slices are implemented; whole-engine conversion
  remains incomplete. The new library builds independently with a C-only
  toolchain and has strict C89 and standalone C API checks. See the
  [dependency boundary and build commands](docs/code-map/library-split.md#c89-engine-backend).

### Backend contract

- Build the shared backend as C89/C90 with extensions disabled. Check its own
  sources and public headers with `-std=c89 -pedantic-errors` and mixed
  declaration/statement diagnostics. Include the headers from both C and C++.
- Use opaque handles, plain C records, explicit ownership and lifetime rules,
  integer results and explicit error reporting. No C++ types or exceptions
  cross the interface. Use explicit context creation/destruction rather than
  relying on application globals and hidden initialization order.
- Define portable integer types using C89 facilities and checked widths; do
  not require `stdint.h`, `long long` or LP64/LLP64 assumptions. Preserve wire
  widths, byte order, signed arithmetic semantics and chunk references.
- Supply host services through C callbacks/interfaces for streams, content,
  rendering, audio output, clocks, fonts and diagnostics. Keep SDL, native
  windows, dialogs, registry access and application UI outside engine policy.
  External libraries may use newer toolchains behind documented C interfaces;
  that does not permit project-owned engine logic to stay in C++.
- Both hosts call one implementation for each migrated operation. Temporary
  C++ facades translate existing calls and records without retaining a second
  evaluator or playback policy. No permanent Studio/Marcus engine forks.

### Conversion sequence

1. **Map dependencies and establish the C89 target.** Inventory `engine-core`,
   `engine`, Archive/Preview, Marcus playback and the Kauai/BRender services
   they use. Assign each responsibility to backend or host. Add a backend
   target, public C header and strict checks, with ownership and error contracts
   sufficient for the first migrated operation. Record remaining C++ backend
   dependencies explicitly; a bridge does not mark them converted.
2. **Convert the first shared behavior slice.** Move startup, render, clock,
   and caption scrolling state and decisions into C89 and
   route both Studio and Marcus through it. Keep presentation and device
   output in hosts. Retire the replaced C++ logic after regression validation.
3. **Convert engine foundations and data access.** Move required memory,
   collections, strings, errors, streams, codecs and chunky-file operations
   to C89 services. Reuse the content VFS. Convert movie/scene/actor/text/sound
   records, version checks and resource/tag lookup while preserving stored
   formats and missing-content behavior. Migrate Archive callers through the
   shared C interface instead of adding a separate Marcus reader.
4. **Convert evaluation and playback.** Migrate actor routes/tracks, state,
   poses, actions/cels, seeks, scene changes, sound queues, captions and
   transitions in dependency order. Adapt Preview and Studio rendering to
   consume the same evaluated state. Keep BRender behavior and host renderer
   adapters compatible while converting the required project-owned wrappers.
5. **Convert editing and persistence.** Move movie/scene/actor mutations,
   undo/redo data and save/load operations into the backend. Keep Studio UI
   commands as callers. Preserve retail file versions and references, including
   embedded assets; retain existing VMM/VXP support through the shared services.
6. **Cut over complete engine operations and remove legacy paths.** Start with
   the shared open and seek milestone below while work in groups 3–5 continues.
   With each cutover, switch both callers, retire the old backend path and
   verify the migrated operation with a C-only consumer. Finish with the full
   engine linking without project-owned C++ engine dependencies. Record
   external runtime and toolchain requirements.

Steps 3-5 are dependency groups, not single large rewrites. Land small,
reviewable slices with both callers updated and validation recorded for each.

### Marcus UI: convert the entire GUI to ImGui through cimgui

The ribbon, transport, settings, timeline, picker dialogs, text entry and
storyboard widgets now use cimgui in one Dear ImGui context. The C89 views use
standard widgets for controls and text input and ImGui drawing APIs for
specialized movie and timeline artwork. SDL provides platform and rendering
services. See the [current Marcus boundary](docs/tools/marcus.md#c89-boundary-and-checks).

- Scene and frame organiser selection, frame scope, zoom and thumbnail cache
  now live in C89. The organiser submits scene/frame requests through the
  existing isolated preview worker API. Camera angle and storyboard commands,
  background and actor/prop selection and filtering, and their thumbnail
  scheduling now also live in C89. The background preview adapter handles
  asynchronous jobs and disk caching. The movie browser now uses C89 for
  navigation, selection, filters, sorting, recents, favourites, preferences
  and thumbnail scheduling. Its worker exposes filesystem, ZIP and movie
  preparation services through a C interface.
- Prop transform gestures, numeric fields, edit previews, commits and restores
  now use C89. Cimgui owns the shared context and frame. Prop and storyboard
  light controls share C89 clipping and snapping through a narrow C binding
  to ImGuizmo. The C89 storyboard controller owns shot, paint, camera and light
  commands, gesture state and commit decisions. A C interface exposes draft
  storage, palette matching, preview geometry and native movie edits.
- Include each converted source and its public C interface in `marcus-c89`.
  Run the relevant existing headless checks and record validation for each
  slice. Add Marcus UI tests only when the user requests them.

Cimgui owns the application widgets and C89 owns their application state and
decisions. Dear ImGui, cimgui's generated implementation, ImGuizmo and their
backend bindings still require C++. The shared engine has its own C-only
completion gate.

### Regression gate for each slice

Use the existing [playback regression corpus](docs/tools/playback-regression.md)
and [Playback Review](docs/tools/playback-review.md) as the primary playback
safety net. Run both SDL Studio and Marcus explicitly; the runner's default
player set does not include Marcus. Keep movie/content hashes, player builds,
configuration and capture environment recorded with the evidence.

- Establish pre-conversion results for affected cases, then compare both hosts
  after conversion with the same inputs. Record the cases and behaviors checked
  against retail and the gaps that remain. Use retail as the `.3MM` behavioral
  reference and v3dmm as the additional baseline and reference for applicable
  VMM cases. Shared bugs are not evidence of retail parity.
- Exercise absolute seeks and normal playback, including replay where state
  reuse matters. Check images, actor/camera state, captions/scrolling,
  transitions, audio and timing as applicable to the migrated behavior.
- Treat new failures or unexplained differences as blockers for retiring the
  old implementation. Keep known pre-existing differences documented; do not
  silently update baselines or relax comparisons to hide conversion defects.
- Respect the corpus's marker-semantics and coverage limits. An inconclusive
  timing result or an untested feature is not a pass. Add focused headless
  coverage for affected gaps; do not add Marcus UI tests.
- Run relevant existing engine and format tests. In a validation configuration
  with `BUILD_TESTING=ON` and `REQUIRE_C89_CHECKS=ON`, run CTest with
  `-R '(c89-syntax|^marcus-c89)$'`. Use a Marcus-enabled configuration to check
  both the engine and Marcus. Record the checks that ran.
- For edit/save changes, validate edit, undo, redo, save and reopen semantics,
  wire layouts and chunk-reference integrity. Reopen the produced `.3MM` in
  retail and check the affected behavior. Compare saved movie semantics and
  allow legitimate container layout differences.

### Immediate next work and completion

**First shared cutover.** Open a retail-format `.3MM`, resolve its resources
and seek to an authored frame through one C89 movie session API used by SDL
Studio and Marcus. Reuse the native file services, chunky reader and codecs,
content VFS, resource resolution and actor/scene evaluation already in C.
Keep host presentation and device output outside the session.

1. The standalone C movie session now owns the loaded chunky index and reads
   the MVIE root and SCEN headers by authored slot. `engine-native-chunky-test`
   runs this path on real files with `--movie`. Six archived `.3MM` movies
   matched the legacy dump for root and scene metadata, and the scene frame
   ranges in three hash-matched fixtures agreed with retained retail seek
   captures. SDL Studio and Marcus now use the shared C89 movie directory
   over their loaded chunk graphs to open and select scenes. SDL Studio
   resolves live slots and Marcus reads the ordered scene headers at open.
2. Extend the session to resolve movie and retail content resources and seek
   to an authored frame. Cover actor and camera state, repeat seeks and the
   applicable missing-content behavior with real files. Document which
   retail captures exercise the path and which behaviors they cover. The C89
   session now evaluates camera cuts and resolves the selected `CAM` through
   the movie's embedded background or a caller-supplied content archive. It
   also evaluates actor route, action, cel and root pose from embedded or
   supplied templates, including procedural text actions. The C resolver now
   maps authored source IDs to mounted content and selects archives by chunk
   identity. The standalone `--seek` probe exercises actors and cameras from
   one retail content directory. The same session can now resolve each
   on-stage actor's body hierarchy, material set indices, authored model
   bindings and local part matrices, including generated 3D text glyphs.
   The body query replays default costumes, custom costumes, generic
   materials and action cels so that retained model bindings use the prior
   effective model. The standalone `--mesh` probe loads those models from the
   owning template or font archive and composes world transforms from the
   body tree. SDL Studio and Marcus now read the same authored source-table
   rows as the standalone session. Mounted archive enumeration uses one C89
   VFS extension filter. The standalone resolver, SDL Studio's resource
   manager and Marcus preview now use the same ordered C89 resource search.
   Host-specific mount setup, renderer material and texture loading, and
   rendered output remain open.
3. Switch both hosts to the same session for the covered operations and retire
   the superseded backend paths. Root, scene and source directory queries and
   ordered resource search now share the standalone policy. Marcus seek and
   Studio rewind now share the standalone event selector over their live
   timelines. Studio, Marcus and the standalone session now share the C89
   scene frame transaction, actor seek, frame event and frame effect drivers.
   One C89 actor playback driver now composes the seek, per-frame step and
   display effects. It evaluates cached entrances, carries Studio's dirty pose
   across intermediate frames and preserves each host's quick-backup behavior.
   Studio also delegates current-frame actor sound replay to C89.
   The actor event driver calls typed action, body, material, and sound
   services. Studio and Marcus share the native body and template bindings,
   and the standalone session supplies its C89 archive and body operations.
   The standalone seek returns one camera and actor snapshot after reading the
   scene start group once. Studio and Marcus now retain the validated C89
   session over their opened chunk graphs. Studio resolves live scene slots
   through it, and Marcus uses it to read the scene and source directory.
   The three paths now share embedded actor template binding and exact
   camera-slot lookup. Both renderers apply the camera binding evaluated
   by the session from the selected slot and the active background graph.
   The renderers use the supplied colour and depth keys and read placement
   points from the same decoded CAM. Live timelines still select the slot
   so authored cuts and camera edits use the same resource path.
   The standalone evaluator and live renderer bodies share cel wrapping and
   apply the same evaluated model and local matrix bindings. Live playback
   evaluates from cached action tables. Regular templates use the same exact
   model lookup as the session, and generated 3D text continues to resolve
   glyphs from its font. Part-set mapping is shared for generic and custom
   materials. The session and live costume cache resolve the same exact
   `CMTL` child and per-slot material and optional model keys before
   application. Default costumes share first-row selection from the
   authored table and costume discovery in child order for older templates.
   Studio and Marcus apply the same C89 body pose used by the standalone
   evaluator. Root matrix composition is shared with the session's mesh
   path, including route translation and any orientation offset.
   The live body record owns the composed root and local matrices, part
   parents, retained models, and material bindings. The record supplies
   the native draw tree and preserves the evaluated state through duplicate,
   restore, and shape edits.
   Studio and Marcus submit through a common C frame record for
   the world, camera and colour/depth targets. It owns the temporary render
   and picking targets in Marcus and borrows Studio's live world and buffers.
   The session can now return an owned frame with the selected camera and
   effective body bindings for all actors in a single seek. It reads from
   either standalone streams or the chunk graphs already opened by the
   hosts. The graph path can also read templates, fonts and other content
   directly from loaded archives. The live ChunkyFile graph, actor ID edits
   and scene header and transition edits now share C89 payload ownership and
   decoding. They consume a bounded stream while C holds the source file or
   decoded memory. The Archive preview directory and inspector now use a C89
   payload cache. It retains the packed and decoded size limits and supplies
   bounded streams to the directory, payload pages, actor headers, and string
   tables. C89 validates and owns the decoded string table, including page
   ranges and per-entry extra bytes. Archive presents the page as UTF-8.
   Both hosts submit live actor bodies from the C89 bindings. Marcus renders
   and picks with temporary trees built from those bindings and holds the
   body and standalone model references through the operation. Studio
   applies the bindings to its attached tree before drawing.
   Both live renderers now receive one C89 frame record with the selected
   projection camera and ordered, retained body bindings. Studio uses the
   attached world order and Marcus preserves visibility order for drawing
   and picking. The live frame is assembled from the current host state,
   including unsaved edits. Studio rendering and prerendering and Marcus
   rendering and picking now borrow reusable body-list storage from their
   retained movie sessions. Each draw holds its own camera and body references.
   Both hosts now use the C89 BRender binding to submit the published camera,
   stage lighting, actor tree and colour and depth targets to the renderer.
   The binding also performs both hosts' BRender pick queries. Studio consumes
   actor and depth hits, and Marcus Preview refines hits against prepared faces,
   transformed textures and the authored background depth.
   Body bounds and highlight transforms now use the C89 renderer binding. The
   shared fixed-point face test rejects unrepresentable ray depths, allowing
   the complete Preview picking fixture to run on x86 as well as x64 and SDL.
   Both live scene loaders now read the SCEN header and its validated start
   and frame event rows through the retained session. The same C reader also
   supports individual scene chunks and Studio undo snapshots. Live timelines
   apply those rows through native resource and editing adapters. Marcus now
   uses the C live record as the authority for selection, readiness, frame
   range, current frame and camera. Rendering and picking read that state.
   The public preview information is published from the same record. The
   retained session now coordinates live seeks and commits the cursor before
   notifying observers in both hosts. Studio prerendering can submit a
   prepared pose during the seek. Render packets carry the live publication
   alongside the selected camera and body bindings. The standalone evaluator,
   Studio and Marcus now load ACTR headers and their slot-zero PATH and GGAE
   tracks through the same graph reader. Both live hosts decode directly into
   C-owned actor path and event tables. Playback, edits, undo and native
   saves share that storage. Track creation, cloning, tag opening and
   cleanup run directly in C. Marcus reads animation metadata from a
   private C track copy so replay can update frame caches safely.
   The standalone archive editor and live saves share the PATH layout
   and the GGAE event table and writer.
   The standalone evaluator
   and the native template cache share the template header decoder and the
   C-owned ACTN, GGCL cel and GLXF matrix representation. Snapshot seeks load
   the matrix table on first body evaluation and cache it per action. Live
   actions resolve ACTN, GGCL, GLXF and GLMS through the C89 resource cache and
   decode them through C89 blocks. Decoded GLMS tags live in the C collection.
   The same C89 template reader loads GLPI, GLBS and authored GGCM tables into C-owned
   collections in both paths. Live templates resolve the root and child links
   through the C89 cache and decode their payloads through C89 blocks. The
   snapshot retains the template state, reuses
   its authored costume table across resets and computes the set count during
   hierarchy decoding. The live cache builds older templates' CMTL choices
   in child order. Generated 3D text uses the same part-index and costume
   collections. Body construction and reshaping validate borrowed index views
   in C89, and each part binding stores its own parent and set indices.
   The C89 BRender binding
   lays out, initializes, copies and relinks the live actor tree and owns the
   shared selection material. Live 3D text generates
   its cels and matrices directly into the shared C action tables, which the
   action adopts. C resolves and reads its descriptor through the live cache
   or archive graph and writes the descriptor child through C-owned block I/O.
   The standalone evaluator uses the same matrix generator
   for its requested cel. Live BRender model construction, geometry binding
   and prelighting use a C89 library. The live model loader and standalone
   mesh reader share the wire layout. The same C89 renderer library binds
   materials, colour and shade maps, and texture views. The C89 resource
   manager now dispatches directly to each C89 cache. A cache hit skips block
   construction. On a miss, the cache owns a C file or memory block through
   the full load or fetch. The typed readers borrow that block during
   preload's size estimate and final read. Cacheable
   objects now link to the C cache directly. C handles object and cache
   references, detach, eviction and priority changes through that link.
   Native object construction, final destructors and physical file adapters
   remain integration work.
   The standalone `engine-native-chunky-test`
   already links through the C toolchain with `mm-native-files`,
   `mm-movie-content`, `mm-engine-c89` and `mm-vfs`. It runs the real-file
   open and seek checks in the standalone `MMEngine` configuration. Verify
   the host paths with the regression gate above.

#### Completed slices and remaining dependencies

Playback policy, movie-data decoding, collections/strings, block/stream ownership,
container persistence, graph operations, forest transfer and resource caches now
have shared C89 implementations behind temporary native facades. Common BASE
reference storage, retain/drop and final-release decisions, plus marked-file
release policy, now also use C89. Resource caches borrow those C counters directly;
fixed allocation/resize/free transactions, counters/failure injection and
liberation retry/reentrancy control now also use C89. BASE/HQ/fixed counter fields
live directly in shared C state. Plain/debug error-stack records and duplicate/
overflow/flush/pop/reporting/locking control now also use C89. Raw heap size/
overflow/resize policy and BASE object allocation/deletion transactions now use
C89, with native alignment profiles and host services. BASE diagnostic kind,
raw/constructed graph records, constructor attachment, validation and mark/report
scans now also use C89. Fixed-memory diagnostic records/list ownership, header/
footer validation, relocation publication and mark/report scans now also use C89.
HQ size/lock/magic records and allocation/resize/copy/free/validation/mark policy
now also use C89 through the fixed-memory providers. BaseLinkedList now embeds
C-owned intrusive links and shares attach/detach/validation policy; FileObject's
registry uses a C node head with typed owner accessors. FileObject's persistent
handle/open/flags/error state, permission recovery, close/temp deletion and
seek/read/write/resize/size/flush policy now also use C89, with native file,
recursive mutex and diagnostic providers. File factory reuse/create/temp policy,
registry lookup/cleanup/membership, rename/swap recovery and replacement policy
now also use C89. Native object allocation/constructors/destructors, the default
registry binding, virtual destruction and final facade removal remain work.
Filename's persistent type and counted path now live in C-owned one/two-byte
records. Initialization, copy/export and path/name validation now use C89,
retaining native diagnostics and prefix-only copies. Clear/extension/type-cleaning/
type-change/leaf-change/set/get rules also use C89 over that storage.
Path construction/search, existence/status/error precedence, delete/rename,
equality/same-directory, volume forwarding and directory navigation/create/move
policy now also use C89 through native filesystem and diagnostic providers.
Unique/temp candidate formatting, the shared 16-bit sequence, retry/failure
publication and temp-directory cache control now also use C89. Explicit legacy
case resolution now shares C89 exact-first/unique ASCII component matching,
iterator cleanup and failure publication, with native case-sensitivity and
filesystem providers. Enumeration now owns C filter state/storage, cursor flags
and the recursion stack, sharing traversal/filter/skip/pre-post/root/link/error
policy through native iterator and filename-view providers. Default cache
owner/configuration bindings, native BASE integration and object
lifetime, and final facade integration remain.
Current and suspended enumeration frames now use plain C one/two-byte filename,
metadata, cursor and entry records. No C++ Filename/vtable remains in recursion
snapshots. C copies preserve the legacy reference-count and diagnostic-kind
payload before publishing type/path, without retaining an object.
Windows physical file/path services now also use a C89 component: OS handles,
sharing/create modes, exact I/O, attributes, lookup/search/path buffers,
directory creation/iteration and media-error/volume calls. The C++ interface is
a forwarding adapter. ANSI and UTF-16 C-only consumers use the same backend.
POSIX physical file/path services now use the same C interface and forwarding
adapter, with a C89 implementation for lexical byte paths, retrying I/O,
cooperative locks, no-clobber hard-link renames, symlink metadata and directory
ownership. Native object/default bindings and final facade removal remain.
Native account lookup and user-data roots now also use the C89 `mm-native-users`
library. Caller-owned result records preserve Windows WNet provider errors,
POSIX effective-uid lookup/retry limits and Linux/macOS path selection. Existing
callers use a value-only C++ adapter; username UI, native object integration and
final facade removal remain work.
Settings read/write/default transactions, POSIX configuration-root selection,
Windows registry access/read-only enumeration and POSIX KPS1 file persistence
now also use C89. Native contexts own the temporary-file counter and mutex;
the C++ adapter translates values, default bindings and callback exceptions.
The existing INI frontend and final native facade integration remain work.
Recursive locking, coalescing auto-reset events, thread-token ownership and
clock-source/steady-tick services now use C89 `mm-native-runtime`. Explicit
contexts own native keys, counter and clock hook; the legacy adapter retains
process-lifetime defaults, C++ exception translation and diagnostic object-size
profiles. Raw allocation continues through the shared C heap policy. Final
default-context and native object/facade integration remain work.
Counted String copy/set/delete/append/insert, exact equality,
32-bit number parsing and control expansion now also use shared C89 over borrowed
one/two-byte buffers, retaining native copy diagnostics. Format parsing, numeric/
tag conversion, padding, output-limit and diagnostic/publication control now also
use C89 through typed C argument providers. Counted validation, zero-terminated
length, exact/length-first ordering, chunked user equality, collation fallback,
character flags, translation and ANSI convenience conversion now also use C89.
Cached byte maps live in plain C state, with C initialization/application and
native comparison-result policy. Native varargs, synchronized publication,
code-page/locale providers and final String facade removal remain integration
work. Actor event
insertion/merging, evaluation, stage/recording/transform edits, ownership, undo,
copy, route-paste, full actor-paste, clipboard, actor creation/binding and movie
sound integration control and body construction/shape disposal, duplication,
restoration, reshaping, body material/visibility updates, costume ownership,
bounds/render preparation and hit-selection policy have also moved into C89.
Body actor/material storage, per-part parent/set indices, part/set counts,
hidden count, geometry and the borrowed renderer binding live in persistent C
state owned by a reference-counted C89 body. Restoration exchanges this state
while preserving the owner reference counts and assigning both actor trees to
their current owners. Studio and Marcus Preview
use one C89 BRender adapter to submit body matrices and draw handles. C89 stores
retained model and generic material owners alongside their draw handles, holds
costumes by set, and shares these operations across live edits and shape disposal.
Render admission, bounds, and part-set lookup read that state directly. C89
drives the nearest-hit and target-body queries over the host's ordered hit scan.
Actor snapshots and render scenes retain the C89 body directly. The native
adapter supplies BRender and Kauai services. Template resource factories,
header I/O, normal initialization, body creation and default-costume prefetching,
template action/model/custom resource routing, cel application/action queries
and accessory graph operations also use C89. Both hosts resolve ordinary
template actions, cel models and custom costumes directly from the template's
C89 cache binding. The cache exposes its current graph for mutable files and
its retained index for immutable archives. A single C89 template owner holds
the archive binding, counted name, parent, set and costume tables, rest angles
and counts. The 3D text form also holds its descriptor, font resolver and
generated material and action. C89 allocates the table records and their
backing storage through the supplied collection allocator.
The shared chunk editor writes template headers and names. Regular and draft
3D text saves use it to create and link the descriptor and handle rollback.
Native string and archive providers remain part of host integration.
ActionDefinition construction, header/list loading, byte-order conversion and
primitive queries use a persistent C-owned action state. C89 allocates cel,
matrix and sound table records and buffers through the collection allocator.
Live ACTN reads and generated 3D text use the same table factory.
ThreeDFont metrics, resource loading, authoring, glyph lookup and lifetime
use persistent C-owned font state. Live font loads and the standalone movie
session share metric allocation, reads and cleanup. Glyph models load through
the C89 resource cache. Authoring creates the font chunk and glyph links using
the shared chunk editor and writes the header and metrics through a C-owned block.
ThreeDText list/material construction, cel/matrix/action generation, shape/action
math, cache replacement and generated-resource lifetime now use C89 as well.
ThreeDText string/tag construction, duplication/provider inheritance, change
rollback, info queries, resource save/load/tag enumeration and localized
action-name policy now also use a persistent C-owned descriptor and controller.
MovieSoundMSND metadata loading/factories, MIDI/WAVE envelope and child writing,
MIDI import control, invalidation, tool priorities/classes and playback dispatch
now use C89 and C-owned metadata/status slots as well. Sound authoring and
invalidation edit the live archive through the shared chunk editor. MIDI event
parsing and SMF import/track merging, sampled/MIDI payload factory control,
RIFF parsing, PCM/ADPCM encoding and WAVE import control now also use C89. Cached MIDI and
SDL WAVE payloads read through C-owned blocks. Classic AudioMan resolves its
stream location through the same resource cache and supplies its COM stream
adapter. Both WAVE importers feed the prepared or original stream into the C89 sound
writer. The classic importer can pass bytes from the open source stream to
AudioMan when its pathname open fails. SDL resampling, third-party GSM/MP3
decoding, classic AudioMan/ACM codec calls and physical file/device services
remain host adapters. Sound-manager dispatch,
queue selection and pause/stop/resume policy, sampled-audio mixing, streamed
MIDI construction/seeking, priority arbitration and frame-clock scheduling now
also use C89. Mutexes, device threads, synth/decoder calls and native object
references remain host adapters; removing temporary native collection/object
facades is still part of final host integration.
Model construction/loading, prepared/unprepared wire I/O, DAT import control,
glyph geometry adjustments and prelighting lifetime/control now also use C89.
Live model loading uses C89 block decoding and bounded reads. Cache estimates
use stored lengths and update after decoding. The C89 BRender binding supplies
renderer operations. The native Model facade supplies the factory, codec and
archive bindings. C89 writes serialized model bytes through the shared chunk
editor, which also handles raw payload replacement for native callers.
Material/custom-material construction, factories, wire I/O, shade-table
replacement, texture/model reference ownership and PIX/BMP import orchestration
now also use C89. The C readers load material, transform and costume payloads
through the shared cache and fetch linked textures, materials and models with
their existing reader keys. The C author writes material and transform chunks
and links material textures and transforms through the shared chunk editor.
Native adapters supply the codec binding, object factories and BRender and
file primitives.
TextureMap now uses C89 persistent pixel/descriptor/import-owner state, header
I/O, cache factories, bitmap palette matching and standalone-file control.
Cached and direct texture reads, chunk writes, and standalone packing use the
C89 block owner and physical file services. Texture and font writers acquire
their writable blocks directly from the shared editor. Cache estimates use stored lengths.
The native pixelmap is a renderer view. Imported PIX resources are released
through their original BRender owner; bitmap import failures release decoded
pixels. Shared indexed-BMP header validation, padded pixel I/O, RLE8 decoding,
palette admission, writing and failure cleanup now also use C89. The native
bitmap facade supplies file/allocation and palette-object services.
Studio's working and background depth buffers and Marcus Preview's depth
images use the same C89 ZBMP owner. Studio's cache uses typed reference,
priority, detach and destruction operations and preserves its historical
charges. Live reads decode through C89 blocks and share the Preview stream
reader. Studio and the Marcus storyboard use the same positional depth writer.
Depth drawing clips and scans C89 regions directly.
Studio and Marcus Preview share a C89 masked bitmap owner for colour
backgrounds and scene thumbnails. Studio's masked sprites and tool bitmaps use
the same owner through typed cache and file adapters. The C owner manages
references and contiguous RLE storage, preserves stored-size probes and decoded
cache charges, and consumes memory-backed blocks after a successful byte
transfer. Native drawing borrows the C RLE view. Studio and Marcus use the
same positional bitmap writer.
World buffer/depth/region/background-cache ownership, construction order, mode
transactions, background loading and depth resampling now also use C89. The C
records are the live source for rendering, picking and export. Native adapters
bind persistent SDK pixelmaps from those records before SDK use. Graphics
operations and SDK scene setup remain host primitives. Camera matrix/clip/FOV
state, nearest-pick selection, pick-coordinate conversion, actor-root owner
traversal and rendered bounds conversion now also use C89. The render-changed
latch, begin/render/bounds sequencing, model-style traversal, temporary material
overrides, clipped dirty RGB/depth cleanup, prerendering and screen/draw control
now also use C89.
The C world buffer record directly owns its working and screen damage regions
and supplies the viewport origin for picking and the buffer owners for
prerender snapshots and background restoration.
Native adapters provide SDK, graphics-port and window invalidation primitives.
Current/scaled bitmap export sizing, background fill, resource/lock/material/
callback cleanup and palette ownership now also use C89. Both live and export
depth copying use the shared C nearest-neighbor resampler. SDK/native object
representations still remain for final integration.
The C scene owner stores the live frame and start event tracks, actor and
caption rosters, and extra selection. It manages the background, thumbnail
and background-sound references. Scene creation and saving write encoded
frame and start tracks through the C collection writer.
Construction, completed-prefix read cleanup, hide/show/close sequencing and
owned scene-sound tag disposal now use C89. Scene frame
movement/replay, actor/text force loops, frame-range extension and two-pass
prerender decisions now use C89 with persistent cursor/effect-mask state.
Single-event play/unplay and start-event dispatch also use C89, including MIDI
cache ownership, pause suppression, actor/text append rollback and camera rewind.
SceneSoundEvent packet allocation/construction, duplication/group copying,
append ownership, wire conversion and per-tag playback now use C89 too. Native
tag representations and resource/queue primitives remain adapters. Scene sound
editing/query, preview/background playback, indicators and sound-undo payload
state/swap control now also use C89. Scene load orchestration, wire/runtime event
construction and actor/frame sound-resolution traversal now also use C89. The
scene loader uses the common tag service to open and unwind embedded sound tags,
and retains the document cache for background tags. Scene sound insertion,
preview input and bulk scene relinking use the shared C89 movie sound binding
for local tags.
Scene tag-collection traversal and save transactions
now also use C89, including sound adoption, wire-group construction and ordered
thumbnail/metadata publication. Pause/camera/background editing, their public undo
transactions and persistent undo payload/swap control now also use C89. Movie
commands call the C89 background, camera, pause, transition and background sound
editors directly. C89 decides whether a background replaces the current one or
starts a new scene. Scene insertion redo and persistent pause, camera and
background undo use the same scene editor after selecting the target scene.
The scene sound editor passes typed tags through native resource lookup, and
the movie command keeps document tag resolution local to the edit. Common
movie/undo services, other editing policy, thumbnail generation and
background resource implementations remain native dependencies. Scene actor/
caption add/remove, actor paste and roll-call traversal, lookups, movie binding
and persistent actor/caption undo ownership/control now also use C89. Actor/
caption internals and native undo adapters remain dependencies. Scene selection
state and actor/caption selection, toggling, bulk/tag selection, group-tag edits
and undo registration, and visible selection centroid policy now also use C89.
Typed view/object effects remain adapters. Scene forward/backward chopping, its
public undo transaction and persistent snapshot resource ownership/save/swap
control now also use C89. Background placement, movie scene undo, and chop
undo share the C89 movie scene container for insertion, removal, and switching.
The chop transaction uses the container's selected scene when it seeks the
saved frame. The container resolves scene slots from the live movie session and
selects exact archive links for slot moves, adoption, and removal. Movie
composition uses the same typed archive operations, including lookups in
cloned movie roots.
Scene name/transition editing, persistent property undo payloads/swaps,
transition-file read/modify/write and playback startup/shutdown now also use C89.
Scene thumbnail generation, cache access and dirty propagation also use C89.
Studio and Marcus read saved thumbnails through C-owned streams. C89 decodes
masked bitmaps and reads the thumbnail palette to produce RGBA for Marcus.
The Marcus movie browser also reads its saved posters and frame-count estimates
through the C89 archive reader and thumbnail decoder over read-only SDL streams.
Both hosts use C89 masked-bitmap drawing for unscaled pixels and one-bit masks,
as well as scaled indexed, true-colour and packed palette transfers. C89 retains
the pixel-centre, near-size and BLACKONWHITE sampling rules. Region-clipped
drawing consumes C89 clip regions directly.
Packed surface copies, raw 32-bit copies and indexed-to-indexed copies use
the same C89 raster code for clipping, sampling, palette conversion and
snapshotting overlapping buffers. Kauai supplies the host's byte-range overlap
check. The near-size true-colour-to-8-bit path still uses
the host's legacy palette approximation.
C89 handles DIB header validation, BGR24/BGRX32 frame decoding and scaled
video drawing. It preserves the bottom-up edge sampling of 24-bit video
and the pixel-centre sampling of 32-bit video. Indexed expansion and exact
palette snapshots for transitions use the same C89 raster core.
Native Windows and SDL share C89 planning and drawing for raw indexed double
stretches. C89 also applies the display palette's identity colours, animation
limits and reserved-slot handling.
C89 also handles pixel scrolling across all six packed depths, including
source and destination region clipping and the original full-byte-first
rule for leftward monochrome moves.
C89 owns clip-region bands and bounds, including Boolean operations, band
compression, translation and rational scaling. Scanners retain their bands
while a region changes. Raster operations scan those regions directly, and SDL
and GDI share the C89 rectangle visitor. Kauai supplies its collection
allocator and the Windows graphics adapter manages the native region cache.
C89 validates packed row sizes for offscreen buffers and DIB frames and owns
the offscreen allocations. It also owns SDL damage masks, including scan-band
rectangle extraction and scrolling. Kauai retains its atomic allocation counters.
C89 resolves Boolean and monochrome brushes, fills and frames rectangles and
ovals, fills polygons using nonzero winding, and applies glyph coverage.
Ellipse subdivision keeps the fixed-point and large-bounds paths and shares
exact edge rounding with polygon filling.
C89 owns graphics view coordinate mapping, visibility and clipping. It also
calculates pen and font scaling, the strips exposed by scrolling, and the
six-point line footprint. Kauai publishes native clip and drawing state.
C89 controls the timing, draw order and palette updates for wipe, slide, iris,
fade and dissolve. It also owns dissolve's seeded pixel and cell permutation.
Kauai supplies the clock, random seed selection, drawing, clip and palette
services.
C89 owns the frame scheduler for Studio's native and SDL video streams and
the SDL device-clock policy for audio-backed AVI playback. Native adapters
supply decoder positions, clocks, PCM device I/O and redraw requests.
The Win32 AVI child window also uses C89 for device lifetime, sound
suspension, playback state, placement and palette refresh. The Win32 adapter
supplies MCI commands, the parent coordinate origin and window operations.
Studio's portable AVI reader and Video 1 decoder also run in C89. The backend
owns the file bytes, frame and palette index, PCM and seekable reference frame.
C89 also owns Kauai's scalable application time and the registered 60 Hz alarm
clocks used by Studio movie and sound playback. It preserves tied alarm order,
late delivery with optional clock slip, input resets and callback reentrancy.
Native code supplies timestamps, allocation and command delivery.
C89 also owns Kauai's pending command queue, ordered filters and filter cursor.
The dispatcher admits queued, tracked and replayed commands, screens modal
targets and releases command groups after routing. Native callbacks supply the
typed active command, platform input and modal feedback. C89 owns Kauai's command
macro rows, repeat and replay state, GLCM serialization, and archive transaction
order. Native callbacks supply chunky blocks, GG objects and handler lookup.
C89 also selects inherited command bindings and allocates consecutive dynamic
handler IDs. Native adapters retain the typed member-function calls.
SDL Studio submits intermediate dissolve snapshots through its window
presenter. Marcus uses the shared C89 dissolve permutation and phase lengths
to compose RGBA movie-camera frames. Both use the C89 RGB555 bin-centre mapping
for palette colours in the outgoing snapshot. The original fixture now shows
retail's temporary colour changes even though its endpoint images are identical.
For transitions between different background palettes, Marcus also uses the
shared C89 palette interpolation. It keeps the renderer's incoming indices
and the remapped outgoing indices until the reveal is complete.
The C89 playback controller now handles Marcus's frame pacing, pause and
resume, transitions, caption scrolling and authored waits. Studio and Marcus
share the decision to advance, switch scenes or finish. Marcus also uses that
decision to honor its loop setting. The Marcus host supplies audio playback,
caption layout and transition images.
Scene background-tag and click-to-actor queries and full/header-only event-group
acceptance now also use C89. The remaining Scene bridge branches were audited as
typed operations/output marshaling or debug validation, not C-only integration.
Background camera wire decoding is now shared C89 in Studio and Marcus.
C89 owns placement-point storage, publication and cycling for both hosts.
Studio's camera-switch sequencing and mouse-matrix composition control also
use C89. Marcus reads camera projections through C-owned payload blocks.
Background header/default-sound/palette/light loading, contiguous camera counting,
resource factory/partial cleanup, tag-list/cache order, palette ownership and
light attachment/lifetime now use C89 too. Marcus Preview uses the same resource
loader with its existing stricter visual admission profile. Both hosts read
background headers, sounds, palettes and lights through C-owned payload blocks.
The reference-counted C89 background owns the palette, lights and camera
placement. Studio's cache entries also retain the counted name and archive
binding. Preview borrows the placement state from the same owner type.
Studio creates a native list when a caller requests a palette. Preview
background image and metadata buffers, image selection, mesh reconstruction,
infinite-backdrop fill, SDK preparation and the two-entry mesh cache also
use C89. Native adapters expose graph and renderer services and borrowed views.
Preview frame allocation/lifetime, camera framing and stable-orbit state, body
instance ordering/tree copying, lighting/material overrides, frustum depth fitting,
raster dispatch and indexed/depth output composition now also use C89. Typed
BRender and host rasterizer/image services remain adapters. Preview picking now
also uses C89 for validation, coordinates, camera/temporary ownership, stable
body copying, nearest-hit/texture policy and
authored-background occlusion. Native adapters supply typed SDK primitives.
Camera matching/panning, orbit reset and placement/shape anchor updates, posed
bounds conversion, camera selection/publication and guide metadata cache ownership
now also use C89. Persistent live definitions, actor handle ownership and camera
cue snapshots, prop reconciliation/rollback, range/camera edits and frame-seek
failure/publication control now also use C89. Preview dependency budgets, asset/
scene admission, template action discovery/rest pose, pose validation and geometry
counts now use C89. PreviewActor header/evaluated-state/shape and body/template
handle ownership, loading/creation, live frame/seek replay, pose and placement
control now use C89 too. Preview runtime-lease/source-registry ownership, session
replacement/setup, palette admission, source registration/resolution and ordered
teardown now use C89 as well. Private actor timeline replay/span splitting, scene
presentation event/cue traversal, actor-cue mapping and stable cue ordering now
use C89 too. Caption text/property traversal, temporary buffer/input ownership,
stable property ordering, audio envelope/payload/MIDI loading and public sound
remapping now also use C89. Stored rich-text/text-box wire codecs, read transactions,
resource ownership and write rollback now also use C89. Live rich-text chunk saving,
text redirection and text-box adoption/rollback now also use C89. Plain/rich text
file-save transactions, base initialization/loading, rich initialization/factories
and stream/file/archive/property/argument release control now also use C89.
Plain initialization loads into and appends the sentinel through the C89 piece
table. Plain saves write and redirect via C89 blocks and piece tables.
Byte-stream documents share that ownership and save path and preserve all bytes
without translation or a sentinel.
Stored-to-live rich-text loading, property/argument byte normalization and default
font argument opening now also use C89, preserving virtual extension hooks.
Studio reopens rich text through the C piece table and normalizes the published
property and argument rows through C collection views. Font argument opening
locks and decodes the stored name in C89 and updates the resolved font ID in
the collection. The platform still supplies font lookup and code-page conversion.
Rich-text property lookup, masked range application, edit-boundary adjustment,
argument interning and reference-count control now also use C89. Studio's live
argument row lookup, reference writes, slot allocation and deletion run through
the C allocated group. The native document still owns and publishes the group.
Studio and Marcus share C property-array lookup and mutation. Studio reserves
property capacity through the same binding, and Marcus preserves the original
byte order of stored caption properties.
Character/paragraph delta packing, font descriptor creation, cached formatting
fetches, paragraph-bound selection and formatting-application transactions now
also use C89, preserving virtual undo/effect hooks.
Text cache filling/invalidation, paragraph/word navigation, counted search and
plain/rich raw/file/stream replacement transactions now also use C89, including
formatted replacement and translated-file ownership. Base edits write directly
to the C89 piece table through the existing stream owner.
The live text cache keeps its range and character width in C state and fills
directly from the piece table. Virtual cache and character hooks still dispatch.
The byte-stream editor also reads from the C piece table and owns its byte
cache, CR/LF scanning and visible line index in C89.
Byte-stream typing, clear, copy and paste splice the C piece tables directly.
C89 also adjusts the selection and orders caret and redraw effects.
Rich-document/property copying and embedded-object fetch, dispatch, insertion and
property edits now also use C89, retaining native partial-failure behavior.
C89 reads and locks live argument payloads during copy, object fetch, help group
queries and help drawing. Object fetch reacquires the argument after allocation.
Line text, rich text and byte-stream views share C89 clipboard selection,
format choice, reference handling, and edit sequencing.
Text clipboard export reads the current C89 piece table after buffer allocation.
Text undo counters, pending transactions and rich snapshot record construction,
combination and undo/redo replay now also use C89 with plain C control/record data.
Generic document undo admission, replay, redo clearing, limit trimming and release
now use C89 and C-owned collection storage shared by Movie and text documents.
Document registration/parent links, subtree traversal, internal/clipboard guards,
close/save decisions and document/view release sequencing now also use C89 and
plain C registry/node/iterator data.
Borrowed document view lists, first-view selection/activation, title traversal and
clipboard/file/untitled name selection now also use C89 and C-owned view storage;
untitled counters now live in C registry/node data.
The common document view uses C89 for Edit menu availability and clipboard
command sequencing, including self-paste rejection and publish-before-cut order.
Kauai's clipboard owner also uses C89 for document identity and references,
lazy and delayed imports, staged export, and text normalization. C89 also
materializes and exports clipboard text documents. Kauai and movie source
names share counted UTF-8 conversion. SDL and Win32 supply clipboard I/O
and native document operations.
Help topic metadata, wire codecs/read/save, string expansion, object/group argument
normalization and picture/button/group edit transactions now also use C89 and
plain C topic data. Help argument byte swaps write directly to C collection rows.
Help embedded-object bounds and drawing sequence now also use
C89, with plain C button-visibility state. Help topic mouse/key/hit/cursor and
script-dispatch policy now also use C89 and plain C tracking/base-handler state.
Balloon creation/replacement/layout and button construction/hit dispatch now also
use C89, with plain C topic-view and button state. Help group scanning, handler
reservation, embedded-control decoding/geometry/creation and topic factory cleanup
now also use C89. Shared Studio/Marcus character-run formatting and line layout
now also use C89 and plain C run/paragraph/line state, including wrapping, tabs,
objects, indentation, baseline and spacing policy. Physical bitmap/font/graphics
providers and final integration remain work.
Text-box stored bounds/lifetime/type now live directly in C state, with plain C
scene/selection state and C89 attachment, frame/view sequencing, lifetime queries,
show/hide core, bounds/type changes and scrolling/selection dispatch. Studio and
Marcus share lifetime visibility and first-frame scrolling predicates. Text-box
factories, duplication, edit/undo registration and replay, property selection and
clipboard ownership/paste now also use C89 and plain C undo/clipboard state.
Studio's text-box clipboard controller uses C89 for command enablement, inline
text versus whole-box edits, actor paste forwarding, and clipboard ownership
when cutting or copying a box.
MovieUndo's shared movie/scene/frame context, actor undo snapshot references,
flags and counted names now live in persistent C state. Actor undo creation
populates that record directly in C89, and its controller captures and swaps
counted roll-call names. Composite actor undo owns its child and counted-name
tables in C89. Studio drag undo captures its snapshots through the shared
selection and actor owner and keeps its pending handles and frozen pivot in C.
Group translation, tweak, orbit rotation, resize and squash use the same C89
actor-owner operations as the single-actor tools. Studio's drag controller
constructs the transform requests and sequences undo, cursor, sound and redraw
effects in C89.
Studio's pointer selection also uses C89 for hit precedence, group recall,
Shift-click tolerance, target lifetime, and refreshing undo when a drag changes
the selected actor.
Studio projects and depth-scales drag input in C89. The placement controller
owns the active actor and replacement snapshot, restores the roll-call
reference on forced end, and registers add or replacement undo on acceptance.
Studio's record gesture keeps its rollback snapshot, frame and cel cadence, and
temporary rerecord mode in C89. It calls the C89 actor owner directly for
recording start, route trimming, cel cycling, frame and point samples, and
recording finish. The action browser and recording startup use the same C89
action transaction to edit the actor and register undo. The gesture takes its
rollback reference after the transaction resets the registered snapshot.
Studio supplies native effects, clock and UI notifications.
Sooner/Later now uses shared actor snapshot capture, C89 frame stepping and
direct updates to the C89 undo record. The actor owner publishes shifted state
and controls the time-edit highlight flag.
Physical views and final integration remain work.
Typed resource/track/archive/path providers, display formatting/output containers,
native string/display/UI representations and
remaining host lifecycle/facade removal remain work.
These are completed slices
within dependency groups 3-5; those groups are not complete as a whole.

The sound conversion is complete for shared resource, payload, import and
runtime policy (`a17d107d`, `66dc67c1`, and the preceding resource/dispatch
slice). A fresh sound closeout audit is recorded in
`docs/tools/playback-regression.md`; device/codec/thread adapters and final
C-only host integration retain the limits described above.
Scene-specific sound packets, sound editing/undo and Scene load/resolution control
now also use C89, as do Scene tag collection/save and pause/camera/background
editing/undo. Final native facade integration remains conversion work.

The sound resource/payload/import/runtime, body geometry/render-control and
model/material/custom-material/texture-resource, indexed-BMP, depth-buffer and
masked-bitmap ownership/codec, and World buffer/background/camera/picking/render/
export and Scene ownership/lifecycle, frame/replay/prerender, event-dispatch,
sound-packet, sound-editing/undo, loading/resolution, tag-collection/save,
pause/camera/background-editing/undo, actor/caption Scene-editing/undo,
selection, chop/snapshot-undo, property/playback-lifecycle and thumbnail/dirty
slices and Scene query/group-loading policy are complete. Movie scene switching,
close/new/insert/remove transactions, undoable removal and scene undo/redo dispatch
now also use C89, including current-scene reference/index control and failure
retry/rollback order. Scene insertion and removal share a C89 undo record.
An insertion retains its background cache and a removal owns a private scene
archive. Removal and chop share the C89 snapshot writer, copy, and release
path. C89 copies the saved scene graph and rebinds its embedded references
before either undo installs it. Studio loads the installed scene for playback.
Scene insertion, removal, and composition use the C89 editor to move, adopt,
and unlink live scene links.
Composition also uses it to release temporary imported roots while keeping
the scenes linked to the destination movie. Movie autosave/save transactions,
temporary-resource setup, saved-file ownership/flags, VMM save-format/publication
policy, scene-tag collection and asset/sound garbage-collection control now
also use C89. Autosave publishes
and rolls back links through the C89 editor on the live index. It creates the
movie root and writes the actor roll call and source-name table through the
shared archive writer. Garbage collection uses the same editor to remove
obsolete assets and sound trees. Actor and scene
serialization and Scene Sorter imports use it to attach embedded backgrounds,
materials, templates, and sounds and discard failed copies. Scene removal,
composition, and autosave use it to preserve embedded backgrounds and materials
after a complete preview of the references they are about to release. Studio and Marcus
share the C89 VMM v1 reader and writer for dependency names, embedded archive
ranges and the inner movie. The same C89 package reader validates VXP directory
rows, extraction budgets, PhysicsFS duplicate selection and Portable CFG data.
Both hosts resolve VMM dependencies through one C89 controller. It handles
external library checks, catalog reuse, embedded name validation and ordered
package selection. The writable catalog and private materialization policies
preserve each host's existing handling of repeated declarations.
The C89 VFS selects the effective portable files and validates their aggregate
disk budget before publishing a replacement source in either host.
The C89 save planner owns the canonical external library names, the ordering
and first-name selection for an active Studio catalog, and the replacement of
stale generated storyboard packages while retaining authored declarations.
The C89 publisher sequences exclusive staging, candidate writing, atomic
replacement and cleanup for Studio VMM saves and Marcus Save Copy. Marcus
also uses the C89 format rule for explicit and inferred 3MM and VMM suffixes.
The working-copy and export paths now use one C89 editor to update storyboard
background references in scene-start events. C89 validates and serializes
editable storyboard metadata, reads legacy versions, and handles archive links
for embedded storyboards and the portable source table. C89 validates the
portable source ID and preserves the table's byte order and string encoding
when adding its declaration. Scene-start and source-table replacements link
the new child before removing the old one, so a failed link keeps the old child
available.
Marcus generates storyboard backgrounds in C89. The writer reads the retail
black scene palette and insertion anchors, maps paint inks to indexed pixels,
and writes the linked palette, lights, depth, cameras, paintings, and muted
default sound. The host supplies archive I/O and BRender fixed-point transforms.
The storyboard draft owns editable paint planes and its staged and accepted
snapshots in C89. Preview borrows the live planes. Apply reads the validated
snapshot, and a failed stage preserves the previously staged edit.
Storyboard export now allocates portable chunk numbers and rewrites the
whole index in C89. The pass checks existing source-6 indexes, reorders
renumbered rows and child links, and preserves payload bytes and the free map.
Managed expansion import and Portable VXP installation now validate chunky
archives through the C89 index, child-graph and payload readers. The installer
accepts valid empty VXP thumbnail indexes, as produced by Marcus storyboard
export. The movie session shares the same stored-payload decoder.
Portable VXP ZIP reading and writing now use a C binding to miniz. C89
selects and names extracted members and defines the generated storyboard CFG
and member order. Marcus also uses that reader for movie ZIPs. C89 owns the
movie and font catalog, member limits, legacy name disambiguation, and the
size and CRC checks for both decoders. Physical file and path I/O, the installed
catalog and publication of installed packages on disk remain host services.
The Marcus sound timeline now owns its cue spans, active voices and frame
holds in C89. The sound sheet, movie clock, waveform mapping and playback
resume use that schedule. The preview helper and player adapt decoded cue
metadata and the matching-build timing snapshot to the C schedule.
Marcus now reads authored caption links, scene pauses, and scene and actor
cues directly through the C89 movie graph readers used by Studio. The graph
reader also loads stored caption bounds, text and formatting through the
same wire decoders Studio uses. It preserves the raw TEXT read and shares
caption conversion with the edit preview. Font conversion and publication
into the preview API use host services.
The snapshot codec now runs in C89. It validates scene IDs and frame
ranges, bounds timing records, and exchanges explicit scene, hold and cue
fields. The helper and player supply the file stream and commit the
decoded schedule after the full snapshot is accepted.
The shared C89 schedule owns the source cues, clip lengths, scroll holds
and pauses used for live analysis and timing imports. Frame-range edits
rebuild it from those records, including cues previously outside the scene.
The schedule also builds the sound sheet's tracks and cue extents, computes
chapter and movie progress, and resolves time seeks to scene and frame.
Marcus supplies display names and performs the resulting seek.
The C89 engine also owns live Marcus playback voices, track mute and solo,
loop and queue progression, and MIDI preroll and rendering decisions.
Marcus and Studio share PCM accumulation and clipping. The Marcus schedule
and live mixer use the same rule for stopping loops at scene cuts. Device
access, decoded clip caching and SoundFont synthesis use host services.
Marcus resolves user-sound links and reads the selected audio payload through
the C89 movie graph. Studio playback, Marcus preview, and the built-in sound
catalog share the MSND header decoder. Studio's cached sounds resolve the live
key and child through the C89 cache, including read-only wire indices.
Invalidation uses the same graph and requires a mutable cache binding. The
hosts supply content sources, names and native output objects.
Studio and Marcus import standard MIDI into C-owned bytes and pass sampled
and MIDI payloads to the same C89 sound writer. The writer owns the MSND
envelope, slot-zero child and rollback. The native child payload uses a
borrowed C block writer. Marcus continues to use the shared audio decoder for
WAV and MP3.
Studio resolves sound links, finds referenced sounds and scans for unused
sounds directly through the C89 graph. Studio and Marcus share the free-slot
calculation for scene and movie sound adoption. Studio still takes candidate
slots from its working file when it adopts into a different archive.
Studio save and close use the same C89 unused-sound scan. C89 walks the movie's
sound links in reverse order, keeps sounds referenced by more than one parent,
and resolves the movie row again after a deletion shifts the live index.
The Studio host supplies the purge prompt and applies link deletions.
The save controller also reads the saved movie root, old scene and table
links, scene slots for tag collection, and local material and template entries
through the C89 chunk index. Scene slots for tag collection stop at the first
gap, with exact type lookup at each slot.
Inside each scene, the C89 graph now resolves the start and frame event groups,
actor links used for tag collection, and sound links used during save. Native
scene saves use the same chunk types as the shared session and archive editor
when they identify event groups for replacement.
Studio tag collection also loads its event groups through the shared movie
session reader. That path checks the wire layout and row bounds before visiting
events and retains the collector's handling of unknown event kinds.
Actor dependencies for the scan come directly from the C89 movie graph and
actor event decoder. The collector resolves imported template links and local
3D text font tags, then stages costume and sound dependencies before inserting
any of the actor's tags.
The scene collector inserts background, camera, and sound dependencies into
the C89 tag list. It uses the same wire sound parser as playback admission
and cue extraction, with the collector's existing trailing-byte tolerance.
Studio opens, selects a save format, and checks local assets against the same
C89 movie dependency list. The movie save code owns the list through its
collection and disposal. Native Studio supplies archive and cache I/O.
Studio autosave and headless movie saves serialize their roll-call rows from
the C89 table. C rebuilds the wire names in actor order, clears cache pointer
fields, and discards the new chunk if writing or adoption fails.
Studio and headless loads decode the roll call into the C89 table. The shared
tag service opens local rows against the document cache and closes them on
failure or movie teardown. The host supplies archive I/O, platform text
conversion and cache reference operations.
Studio's roll-call lookup, rename, add, remove, and tag refresh use the C89
table directly. The same code handles roll-call changes during actor placement
and 3D text replacement. Newly authored material and 3D text template tags
retain the active document cache through the same C89 service.
Studio autosave prunes the C89 table and releases the removed rows' tags through
the common service. It marks cached materials and templates for eviction using
their typed readers when deleting from the working archive. Scene import reads
source rows into a temporary C table, appends counted names and adjusted IDs,
and closes the imported tags if the merge fails.
The movie state initializes and owns the live C89 table. Studio, headless
load and save, and the shared transactions use that same embedded table.
Movie and scene titles use the C89 string codec for chunk names. The native
archive adapter provides encoded bytes, code-page conversion, and diagnostics.
Studio actor saves and scene undo snapshots use C89 to write the ACTR header
and tracks and attach their embedded templates, costume materials, and scene
sounds. C89 owns the PATH and GGAE output blocks and renumbers embedded
templates before linking them. Costume events store the destination material
number and retain the material subtree before the old actor is removed.
Scene and actor cues share a C89 writer for embedded sounds. It copies the
payload to the destination archive and writes the mapped chunk number at the
authored scene sound slot. The private undo archive can carry the same cue
data as the live working archive.
The scene chop undo record owns its private C89 archive directly. C89 replaces
the saved scene chunk and releases the archive. Studio binds a resource cache
when restoring the live scene objects.
The live archive adapter allocates scene payloads and publishes raw header
replacements through the shared chunk editor. Direct transition edits use
the same SCEN write path. The shared editable archive binding owns its input
and output blocks in C89. It also stages actor event replacements and
handles raw and packed payload patches for the hosts.
C89 also preserves background and material subtrees that would lose their
final parent when Studio removes, replaces, or rearranges scenes. Older
background and costume tags can then resolve the same chunk numbers through
edits and undo. Named saves still prune unused standalone materials.
Scene-sorter imports use C89 to bind cloned costume events to destination
material numbers before publishing any imported scene. The archive adapter
serves Studio and Marcus.
The scene-sorter import also uses C89 to link embedded actor and frame sounds
from the cloned movie's sound bank at their authored scene slots.
C89 loads document-owned backgrounds against the scene's resource cache.
Sorter imports and scene saves adopt the background tree and write its
destination chunk number into the start event. Imports also handle older
movies that store the background as a standalone chunk.
Marcus also builds and caches waveform peaks and clip lengths in C89.
Timeline, asset and recorder previews use the same peak reduction. Timeline
columns sample the C89 presentation clock with one cache lookup per request.
Archive lookup and SDL decoding supply the media.
The C89 engine now selects Marcus's frame cues and authored waits, including
prior scene music, restart sample positions and motion-cue precedence. C89
also owns Studio's pending movie sound queue, including the signed low-word
lane key, replacement order, requests held for playback, and timeout policy.
Studio embeds the queue and its owned clock reference in the native command
handler. C89 prepares and drains synchronized batches, releases references,
and orders short preview alarms and sound-class stops. The host supplies
allocation, the clock, and device operations. Sound playback and pending
replacement both compute Studio's queue key in C89.
Marcus caption compositing also uses C89. The renderer shares text-box
visibility rules with Studio and keeps visible captions in presentation
order across seeks. C owns mask sampling, workspace clipping, blending
and depth-tested camera-plane projection. The host supplies font faces,
glyph masks and palette colours.
Marcus caption layout now decodes authored character and paragraph properties
in C89. It uses the same C rich-text line and run formatter as Studio and
publishes positioned glyph runs and the total height used by scrolling.
The shared C89 transition engine dispatches Studio's indexed cut, black,
fade and dissolve effects. It also prepares Marcus's outgoing dissolve
snapshot and owns its palette indices. Both players apply authored palette
ranges through the same C89 overlay routine.
Marcus movie polling now coordinates audio restore, frame entry,
loop rewinds, outgoing capture, scene selection and failure cleanup in
C89. The poller uses Studio's C89 frame and scene advancement rule.
The C89 movie scene module also coordinates Marcus's first scene, scene
switches and explicit seeks. It applies edited frame bounds, camera and prop
placements before publishing a switched scene, and resets playback after a
successful seek.
Marcus frame-edge editing now stages undo, bounds and sound timing before
evaluating the new frame. C89 publishes those changes together and restores
the prior range and frame after failed evaluation. Preview and native save
share range validation, while the saved opening camera uses Studio's C89
relocation rule.
Marcus camera edits now stage the journal and undo entry until C89
accepts the replacement track. Live preview and native save share
opening-frame resolution and camera-event replacement. Failed live
evaluation restores the previous track and replays the old frame when
the preview can recover. C89 also reads and updates the archive's
frame-event groups and scene header for saved camera and range edits.
The archive writer preserves the group byte order and unrelated
payloads, replaces the frame-group link, and removes thumbnails for
opening edits.
Marcus now retains added-scene definitions and camera cuts in C89-owned
records. Scene definitions preserve insertion order. The camera journal
replaces repeated keys, preserves the opening-frame sentinel, and supplies
the same ordered cuts to preview, thumbnail checks, native save, and the
32-bit renderer worker. Undo retains the previous records.
Ordinary scene insertion and storyboard scene edits publish through the C89
snapshot transaction. It records undo only after the live scene is ready and
restores the prior definitions, timing and position on failure. If recovery
also fails, it clears the live selection.
C89 tracks declared and generated live resource source IDs and retains the
archive references for generated storyboards, text, costumes, and sound
previews. Generated IDs skip occupied sources and the retail alias. Internal
reopens carry the allocation cursor into the replacement session.
Marcus sound archive edits now use C89 for scene packet changes,
actor binding selection, and imported sound links. Scene changes share
the camera and range editor's frame-group reader and writer. Actor
changes preserve route and resource links and replace only the
selected scene's actor instance. Native audio preparation and archive
file access remain host services.
Marcus undo now uses a C89-owned bounded snapshot stack. C89 chooses
whether to reopen the base movie or restore the scene in place, rebuilds
derived state in order, and releases the undo entry after a successful
seek. It can replay the prior snapshot after a failed undo. Native
archive and edit payloads remain typed host resources.
Structural edits now use that C89 snapshot path to install the replacement
archive and restore the prior scene and playhead. Paste verifies the new
actor before publishing its selection and recording undo. Sound edits and
work-view transforms use the same installation and recovery sequence.
Marcus object copy and paste use C89 to retime the saved track, assign
the actor ID, remap embedded templates, fonts, costumes and sounds, and
reuse a scene sound link for repeated cues. C89 also assigns the scene
child slot, appends the start event, updates the chunk links and writes
the movie roll call during insertion and paste. 3D text renames use
the same roll-call writer and keep existing reference counts. Clipboard
copy and paste select actors by authored ID, decode and rewrite event
tracks with the shared C89 actor codecs, and register external source
IDs on first use through a C89 map. The native adapter still opens the
writable archive and clones or copies chunk graphs. Movie
content-source registration also uses C89 for VFS alias matching,
fallback and new IDs, and the source table's native encoding and link.
C89 also creates movie roots for scene-only archives, links the existing
scenes in order, and writes the header and event groups for a new scene.
The Studio and Marcus paths share the encoders and use the live C89 chunk
editor to link movie roots, new scenes, roll calls and storyboard metadata.
Scene saves stage both event groups before releasing either previous link.
They also use the live index to link sounds, actors, captions and thumbnails
and to discard a failed candidate scene. New scenes and inserted actors
move to their requested chunk numbers through the same index, which supplies
runtime ID access for chunk moves and copies.
Actor placement, animation, sound changes and reframing use the same index
for graph edits. Caption edits, rich-text body saves and storyboard resource
creation also use it directly. The movie archive host supplies payload I/O
and one live index for lookups and mutations. C89 takes a fresh read view
after edits that change row positions, including when preserving assets
before dropping a scene.
Studio's scene ordering and composition, autosave, and close-time asset
and sound collection read that same index. Sound attachment, copying,
and importing use the shared editor to publish links and remove failed
copies. During import, the destination captured before the temporary-file
step receives the sound, while its slot comes from the current working file.
Studio's live document startup and teardown now run in C89 over the movie,
save, and scene owners. The runtime owns the client, world, sound queue,
and thumbnail palette references. C loads the working archive, binds the
saved file, and orders source merge, dependency caching, and title
publication. An unsuccessful open destroys the partial document before
ending the wait cursor. The native host supplies container paths, typed
files and objects, and display and audio effects.
Studio dispatches the C89 startup and render commands directly. C89 also
owns its alarm scheduling, deferred render after a user pause, and stop
cleanup, including the distinct behavior after an alarm retry fails. The
frame pacer and scene advance policy are shared with Marcus's polling host.
Portable source tables and background event groups publish a replacement
before releasing the old link. Native source registration retains its
existing unlink-before-adopt order.
Actor-free Marcus movie materialization writes the empty roll call
required by the retail and SDL Studio loaders.
C89 now writes newly inserted actor, prop and 3D text instances,
including their native chunk numbers, template and font references,
stationary tracks and roll-call entries. Live preview and saved
insertion share the stationary track constructor. Saved 3D text
replacement also uses C89 to keep actor identity, install the copied
template and material, and update animation events and the roll call.
Host services open archives and copy the selected asset graphs.
Saved Marcus object placement uses C89 to copy the actor header,
retain shared child chunks, install a scene-local actor link and
invalidate the thumbnail. C89 also rewrites the rotation, stretch and
3D text material event tracks using the actor codecs and fixed-point
math shared with Studio.
Marcus action and path edits now run their draft sequence in C89 using
Studio's actor action, transform and recording primitives. C89 validates
targets, samples the path by stride, and computes the edited lifetime.
It also installs the saved route and events on the selected scene's
actor, reapplies appearance and shape, and extends the scene as needed.
The C89 scene archive editor now lists Marcus text boxes in start-event
order and publishes their insertion, replacement and removal. It reuses
vacant text slots, keeps unrelated event records and retains the start
group's byte order. The native rich-text host prepares each box payload.
C89 also translates caption bounds and frame ranges, normalizes paragraph
breaks, and applies character and paragraph property edits to the changed
UTF-16 span. Preview and save share these decisions and retain the first
character's formatting and argument references when text is inserted there.
The C89 caption renderer also maps UTF-16 layout offsets to the editor's
UTF-8 byte positions, publishes caret stops for wrapped lines and tabs,
and clamps draft scrolling to keep the active caret visible.
The same presentation stack now drives caption hit-testing, draft
substitution, and back-to-front drawing in C89. C89 also selects the
credits that can scroll on each frame and tracks their scroll offsets.
Work-view reframing now uses C89 for camera projection, route splitting,
event rebasing, and replacement of the selected scene's actor tracks.
The native adapter supplies evaluated poses and calls BRender's Euler
conversions.
Marcus provides archive access and the host's character conversion.
Movie roll-call queries/naming,
actor-ID/tag/reference control, actor selection/placement/edit dispatch, sound
defaults, camera/pause/transition dispatch, background-driven scene creation and
common undo registration/notification now also use C89. Movie material insertion,
3-D text creation/template replacement and caption insertion/removal/hiding/
visible-index selection transactions now also use C89. Movie composition,
descriptor storage, reorder, removal, and rollback now also use C89. The
sorter accesses the same rows that composition reads. C89 links imported scene
sounds, loads and transfers roll-call rows through the working cache, and
traverses the live archive to rewrite shared and nested actor IDs. It also
reads and writes stored scene transitions and rebinds embedded backgrounds
and materials through the C archive editor. It resolves thumbnail payloads,
adopts imported scene sounds and checks final scene-child order in C89.
Composition binds both archives to the C chunk copier for movie clones and
embedded references, including stored payloads and translated names.
Kauai live-file copies and Archive copies from immutable resource indexes
now also bind their payloads and names in C89. The copy facade supplies the
physical file, text conversion and native failure reporting.
Native bitmap allocation and final resource and object integration remain.
Background resource loading, exact child lookup, cache and light policy use
C89. Studio selects cameras through the same cache and reads colour and depth
through typed block readers. Preview background image, mesh and frame rendering
control, picking, camera interaction and guide metadata caching use C89 too.
Live definition/actor ownership, reconciliation and timeline editing/seeking
control are now C89 as well. Dependency budgets, asset/scene selection, template
action/rest-pose control, pose validation and geometry counts now use C89 too.
PreviewActor state/resource ownership and load/create/frame/seek/placement control
now use C89 as well. Preview source registration/resolution, session replacement/
setup and runtime-lease/source/cache ownership now also use C89. Typed resource/
track/archive/path providers remain. Actor timeline replay/span/event control,
scene cue traversal, actor-cue mapping and stable ordering now also use C89.
Caption text/property traversal, stable ordering, audio envelope/payload/MIDI
loading and public sound remapping now also use C89. Stored rich-text/text-box
persistence now also uses C89. Live rich-text chunk save/redirection and text-box
adoption/rollback now also use C89. Text document file saves, initialization/loading,
factory cleanup and resource release now also use C89. Stored-to-live rich-text
loading, property/argument normalization and default font argument opening now
also use C89. Rich-text property lookup, masked range application, edit-boundary
adjustment and argument interning/reference counts now also use C89. Character/
paragraph value conversion, cached fetches, paragraph bounds and formatting apply
transactions now also use C89. Cache/search/navigation and plain/rich raw/file/
stream replacement transactions, including formatting and translated-file ownership,
now also use C89. Rich-document/property copying and embedded-object queries,
dispatch, insertion and property edits now also use C89. Text undo counters,
pending transactions and rich snapshot records/combination/replay now use C89 and
plain C control/record data. Generic document undo-list control/storage now also
use C89, shared by Movie and text documents. Document registry/links, tree traversal,
internal/clipboard guards, close/save decisions and document/view release sequencing
now also use C89 and plain C registry/node/iterator data. Borrowed view storage,
first-view selection/activation, title traversal and name selection/counters now
also use C89 and plain C data. Help topic metadata, wire codecs/read/save, string
expansion, object/group argument normalization and picture/button/group edits now
also use C89 and plain C topic data. Help embedded-object bounds, resource selection
and drawing sequence now also use C89 and plain C button-visibility state.
Help topic mouse/key/hit/cursor and script-dispatch policy now also use C89 and
plain C tracking/base-handler state. Physical bitmap/font/graphics providers,
display/output
representations, native string/display/UI representations and final host integration
still need conversion. Balloon creation/replacement/layout and button construction/
hit dispatch now also use C89 and plain C topic-view/button state. Help topic
group scanning, handler reservation, embedded-control decoding/geometry/creation
and factory cleanup now also use C89. Native Help bridge methods retain typed
provider binding, marshalling and validation; this is not final C-only integration.
Shared Studio/Marcus rich-text character runs and line layout now also use C89
and plain C state. Typed property/font adapters remain in C++; final removal of
native representations and facades remains required.
Text-box metadata and scene/selection storage now also use plain C state, with
C89 frame/view, lifetime, show/hide core, bounds/type and dispatch policy. Marcus
uses the same visibility and first-frame scrolling predicates. Text-box factory,
duplication, edit transactions, all five undo variants, property selection and
clipboard ownership/paste now also use C89 and plain C saved-value/reference state.
MovieUndo base context and actor/composite undo storage now also use persistent C
state, including counted actor names and snapshot/table references. Typed child,
name-table and movie/scene services remain. Physical views and final host
integration remain work.
The SDL release `pixel-canvas` scrolling reference failure is resolved by using
signed 64-bit reference-loop coordinates and checking guard samples explicitly;
the renderer is unchanged. Evidence is in `docs/tools/playback-regression.md`.
Model, texture, material, custom material, template, action, font and sound
readers now take the cache-owned C block directly. Scripts, string registries,
colour tables, Kidspace descriptors and cursors also use typed C block
readers. C89 owns the descriptor records and performs the SDL cursor image
selection. Script literals resolve in the same C archive graph as the root.
Streamed MIDI builds device events directly from the cached track in C89.
Background and MSND headers use the already-bound cache block. C89 prepares
the retained file, memory or temporary-file backing for classic AudioMan.
C89 also owns its stream cursor, queue scheduling, synchronization groups,
shared mixer lifecycle, format fallback and system volume handling. Windows
supplies COM and wave-device services.
The typed callback is the cache key throughout preload, lookup, tag resolution,
manager dispatch and nested fetches. Individual cache entries can carry C89
object ownership services alongside native entries. The tag and source manager
preserve those services through archive-local and content-pack lookups. Studio
and Preview share the C89 BRender reader for TDF chunks, glyph lookup, and
model references used in font authoring. Kauai supplies memory, decoding, and
diagnostics. C89 also owns models from BMDL chunks, authored geometry and DAT
imports. Actor
bodies, costume overrides, font glyphs and Preview poses retain those model
owners directly. The C89 BRender body adapter supplies body allocation,
body-part materials and models, pose and geometry services for Studio and
Preview. Marcus, its Preview helper and its regression runner link the C89
body adapter directly. Kauai supplies allocation and diagnostic callbacks.
Actor-track editing in both hosts uses the shared C89 local tag operations.
Loaded 3D text carries its host's font provider. Studio binds its catalogue
and Preview binds the archive set for its session. Materials and shade
tables retain C89 texture owners. The
BRender adapter supplies a writable pixelmap view in the same allocation,
and imported pixelmaps retain their original renderer allocation. Studio and
Preview share the C89 BRender cache readers for BMDL, MTRL and CMTL, including
nested dependency references, material appearance edits and the shade table.
Both hosts use these owners for body costume snapshots, actor effects, 3D text
material edits and archive writes. Kauai provides native memory, codec, file
and diagnostic callbacks. The x64 Marcus, Preview helper and regression runner
link this shared adapter directly. Studio and Preview also share the C89
template and action cache readers, body creation, costume application, cel
evaluation, and 3D text initialization and generation. Archive-backed and
generated actions use the same C89 owner and playback readers for cels,
matrices, distance, flags and motion sound tags. The C89 text generator owns
the cached action reference and replaces it after text edits. Kauai supplies
chunk names, allocation, decoding and diagnostics. Marcus, its Preview
helper and regression runner build and link without engine-core. Studio
still uses native engine and archive interfaces around the shared engine.
The C89 MSND owner holds the counted sound name, payload metadata and
cache binding. Studio's cache reader, invalidation and frame-sound dispatch
use the bound C89 operations. Preview reads the same sound state from its
archive graph. Studio and Marcus use the same C89 WAVE import through the
archive write, with SDL supplying decode and sample conversion. The native
sound manager and clock provide output and timing services. Actor template
and 3D text objects use the same C89 owner in Studio and Preview. Background
resources use a C89 owner for metadata, lights, palettes and placement
points. Studio's C89 BRender binding owns the cache reader, light attachment
and teardown, and camera selection and mouse orientation. The World adapter
publishes images, camera transforms and redraw requests. Preview uses the
same background decoders with its archive-graph renderer. Studio scene
save, transition editing and undo snapshots bind their live or private
archive in C89, including embedded asset copy and editor cleanup. Movie,
scene and actor loads borrow the live graph through the C89 cache. Studio's
in-place saves, same-name replacement and Save As use the same C89 archive
owner as Marcus working-file saves. Native services provide title conversion,
object rendering and filesystem operations.
Movie title I/O binds the live resource cache in C89. Direct archive titles
use the same chunk-owner callbacks as private archives. Tag cache clearing
walks the C source catalogue and reopens live resource caches directly.
The immutable archive source now owns its native file and validated index
in C89. Studio's source catalogue and the Archive/Preview reader use this
binding. Resource caches retain the index and file independently.
Working archive lookup, cache retention and reload now use the C89
registry and archive owner. Studio and Preview supply filename and
physical access operations for the shared selection.
Live and immutable resource caches now bind their copy endpoints in C89.
Studio's local tag saves and embedded asset copies and Preview's font
drafts share that binding and use the cache's name codec and backing state.
Local tag opening also accepts a typed source archive and binds the destination
cache in C89. Copies into private working archives preserve names, packed
payloads and child references. Opening a tag within the same archive retains
the cache without looking up or copying the chunk.
Studio's Kidspace descriptors now give their C89 cache directly to script
and artwork loaders. The C cache owns their references and streaming
priorities. Studio movie thumbnails also load from a C-owned working cache.
The C resource services no longer carry a back-pointer to a C++ cache wrapper.
Studio and Marcus share the C89 rich-text binding for stored TEXT extents,
file references and bounded reads. Preview uses the same decoder for the
authored TEXT encoding marker. Studio's live property and argument tables
use the same C89 collection owners and wire codec as Marcus's captions.
Studio supplies its document/view hooks, font mapping and allocator.
Property edits in both hosts bind those C89 owners directly. Argument
allocation, lookup, insertion and reference updates use shared collection
operations that preserve the payload byte order.
Studio's document loader now transfers and normalizes these collections in
C89. Font argument lookup uses the live collection before and after the
native font mapping call, so buffer relocation keeps the mapped ID current.
The stored rich-text path also owns its property and argument tables
directly in C89. The archive binding uses the same collection codec for
Studio, Preview and Marcus, including authored byte order.
The resource, object and HQ facades remain part of final host integration.
Validation and coverage limits are recorded in `docs/tools/playback-regression.md`.
After the first cutover, extend the same session through continuous playback,
captions, transitions, sound, editing, undo and saving. Track the remaining
body, template, resource and object dependencies by the operation that still
needs them. Retire each temporary backend path when both hosts use its C89
replacement and the relevant regression and format checks pass.

The migration is complete when Studio and Marcus use the same C89 engine for
their engine operations, including Studio editing/saving, the backend builds
without project-owned C++ engine dependencies, and the regression and format
gates above pass within explicitly recorded coverage. A C API or C89 frontend
alone does not meet this criterion.

---

## Follow-on: native 7gen / v3dmm compatibility

The November 2026 request adds existing `.vxp` expansion and `.vmm` movie
compatibility to the port's planned scope. See the
[Ghidra findings and implementation sequence](docs/tools/v3dmm-research.md).
Support the existing containers and resource identities natively, using the
current chunk loaders and expansion store. Preserve the hard constraints above:
ordinary `.3MM` output remains retail-compatible, and mod-dependent bundled
documents use a separate `.vmm` save path. Initial native SDL support now covers
Portable VXP installation, source-6 browsers and resources, and VMM open/save.
See [native V3DMM support](docs/tools/v3dmm.md) for usage, tests and remaining
compatibility validation.

## Project 0: rename modernization

**Status:** in flight on branch `c`. ~24 commits already; queue tracked in `plan-renames.md`.

**Why:** Improves readability and grep-ability without changing behavior. Cheap to do incrementally; warms muscle memory for the codebase.

**Done when:** No bare 4-or-5-letter Hungarian-style type names remain in `inc/` and `kauai/src/` *except* the ones explicitly deferred (PT/RC/RAT — defer until Project 3 surfaces platform-header collisions; SITOBREN classes — disabled build).

**Abandonment criteria:** None — this can be paused and resumed indefinitely. Every commit is independently revertible.

### §0a — Test infrastructure (now-doable)

Three test buckets that need no Project 1/2/3 dependency. Set these up early; they pay off immediately by giving every subsequent rename and refactor a regression net.

1. **Chomp output determinism.** Hash every `.chk` produced by `build/3dmovie/`, store the hashes in-tree (e.g. `tests/fixtures/chunk-hashes.txt`), fail CI if they change without an explicit baseline update. Turns the "byte-identical chomped chunks" criterion in `plan-renames.md` into automation. Catches accidental regressions in `chomp.exe`, `kpack`, `mkmbmp`, `chcm`, plus any rename that touches `.cht`/`.chh` files.
2. **Geometry math tests** for `PT`, `RC`, `Region` (`kauai/src/utilint.h` and `kauai/src/region.cpp`). Pure unit tests, no I/O. Tests: `RC::Union`/`Intersect`/`OffsetToOrigin`, `PT::Map`/`Transform`, `Region::Union`/`Intersect`/`Diff` against fixed input rects. **Becomes load-bearing for Project 1's sized-types audit** — these are the types most likely to be silently miscompiled under LP64.
3. **Codec round-trip** (`kauai/src/codec.cpp`, `codkauai.cpp`, `kcdc_386.c`, `kcd2_386.c`). For each codec, compress a known buffer, decompress, byte-diff. Catches silent codec regressions that are otherwise undetectable until a chomped chunk happens to round-trip wrong.

**Test framework:** small home-rolled C++ harness (or pull in Catch2 / doctest header-only) plus a thin Python pytest wrapper that invokes the test binaries. Don't over-engineer — these are pure-function tests; they don't need fixtures or mocking infrastructure.

---

## Project 1: 64-bit build

**Why first:** Today `CMakeLists.txt` `FATAL_ERROR`s on 64-bit pointer width, and `kcdc-386`/`kcd2-386` host codegen tools cannot cross-compile. No Mac/Linux work is real until this is fixed (Apple Silicon doesn't run x86 binaries usefully).

**Tasks:**

1. **Audit `IN_80386` define usage.** Find every conditional and decide: portable C replacement, or `<cstdint>`-typed equivalent.
2. **Replace `kcdc_386.c`/`kcd2_386.c`** with portable codegen, or pre-generate the headers and check them in.
3. **Audit byte-order plumbing** (`kbom*` constants, `SwapBytesBom`) — confirm 32-vs-64 is irrelevant. These are byte-stream FOURCCs; should be word-size-independent but verify.
4. **Replace bare `long` in on-disk structs with sized types.** This is the single most likely place to silently break `.3MM` compat. MSVC x64 keeps `long` at 32 bits (Windows LLP64), so a Windows-only 64-bit build hides the bug — but on Linux/macOS `long` is 64 bits (LP64) and the layout silently changes. Files to audit: `MovieFilePrefix`, `SceneOnFile`, `ActorChunkOnFile`, `MACTR`, `SceneEvent`, `SceneSoundEvent`, `TagChildPair`, `MaterialOnFile`, `BackgroundOnFile`, etc. Replace bare `long` with `int32_t`, `short` with `int16_t`. The `blck.Cb() != size(...)` checks will catch any miss at runtime.
5. **Strip `set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded...")` static-CRT lock-in** if it's only there for x86 reasons.
6. **Remove the `FATAL_ERROR` and fix the resulting cascade.**

**Risk:** medium. Likely to surface places where `long` was used as a pointer-width int. Not architecturally hard; lots of mechanical fixes.

**Abandonment criteria:** If task 4 surfaces a struct that genuinely cannot be made layout-compatible without breaking compat (e.g., uses pointers stored on disk), document it and defer to 3DMMPlus. Don't try to invent compat shims — those are bug farms.

**Done when:** `cmake --preset x64:msvc:debug` builds, `3dmovie.exe` opens a sample `.3MM` from `cd3/SAMPLES/` cleanly, and `git diff` of chomped chunks against the x86 build is byte-identical.

---

## Project 2: library extraction + Python bindings

**Updated direction:** Python bindings are an optional consumer of the shared
C89 engine. The older extraction tasks below describe transitional work only;
they do not replace the engine conversion or require completing Python bindings
before Studio and Marcus adopt the C89 backend.

**Why:** Enables `.3MM` inspection, batch conversion, and headless tooling without spinning up the studio app. Validates that engine is separable from UI — prerequisite for the SDL port.

**The friction is not C++.** It's that `engine` depends on Kauai for non-data reasons: error stack (`PushErc`), memory primitives (`HQ`, `PvAddBv`), `String`/`StringTable_GST`, `DataVersion`, the chunky-file API, and `TagManager` (which scans drives for content). You don't get a clean "engine without UI" by shaving off `studio` — you get "engine + a chunk of Kauai's runtime."

**Tasks:**

1. **Pre-flight survey.** Before designing the shim, grep `src/engine/` for includes of `appb.h`, `gob.h`, `dlg.h`, `cmd.h`, `cursor.h`, `menu.h`. Catalogue every UI-leaning header that engine pulls. The result calibrates the headless-Kauai effort and may surface engine code that needs lifting into `studio` first.
2. **Headless Kauai.** Add CMake option `KAUAI_HEADLESS`. When set, swap `appbwin.cpp`, `gobwin.cpp`, `gfxwin.cpp`, `dlgwin.cpp`, `menuwin.cpp`, `picwin.cpp` for stubs returning `fFalse`/no-op. Keep file/fni/mem/region as-is (they're mostly std-C already).
3. **Lazy/skip TagManager.** `vptagm`'s drive-scan triggers UI prompts when content sources are missing. Add a "no-prompt, fail-fast" mode used by headless callers. Tags that fail to resolve return `pvNil` instead of triggering a CD-search dialog.
4. **Define the shared engine C API** — open/close, scene and actor enumeration, roll-call and tag access, metadata, evaluation, editing and save. Grow the interface with converted slices rather than mirroring every `Movie` method at once. Temporary C++ adapters copy legacy types into plain C records; replace their engine implementation with C89 under the migration above. Python sees `bytes`/`str`/`int`/list-of-dict, never opaque C++ objects.
5. **`libsoc` shared lib target.** Links `engine + brender + kauai (headless) + audioman (stub)`, exports only the shim. `__declspec(dllexport)` on Windows, visibility attrs elsewhere.
6. **Python binding via `cffi`.** `cffi` against a flat C ABI sidesteps the static-CRT/MSVC-ABI mess. `pybind11` would force Python and engine to share the C++ runtime — annoying with `MultiThreaded` static CRT.
7. **Reuse `movie-chomp`.** `src/tools/movie_chomp.cpp` already opens `.3MM`, walks chunks, dumps the prefix and per-scene structures. Treat it as the prototype for the read side of the shim — extract its calls.

### §2a — Test-driver methods and round-trip test suite

Extend the C ABI (task 4) with a small test-driver surface that the existing engine-without-UI code path supports for free:

- `soc_movie_save(handle, path)` — pairs with `soc_movie_open` to enable round-trip tests
- `soc_movie_get_scene_count(handle)` / `soc_scene_get_actor_count(handle, iscen)` / `soc_actor_get_tag(handle, iscen, iactr, *tag_out)` — scene/actor introspection beyond just enumeration (returns full state for assertion)
- `soc_movie_advance_frame(handle)` — drive scene playback in headless mode without a render target
- `soc_set_tag_resolver(callback)` — let test code stub tag resolution rather than hitting the real `TagManager`

**Round-trip test suite as a Project 2 done-criterion:** for every sample `.3MM` in `cd3/SAMPLES/`, open, save to a temp file, reopen and compare movie semantics, stored versions and chunk-reference integrity. Physical layout and chunk numbering can legitimately change; byte-identical containers are not a general save criterion. Keep strict format checks separate from normalized semantic comparisons and verify saved movies in retail before claiming compatibility. The headless C API supports this without UI driving.

Two further test categories that fall out for free once the C ABI exists:

- **DataVersion handshake tests.** Build synthetic `MovieFilePrefix` blobs with `(swCur, swBack)` combinations spanning `<kcvnMin`, `=kcvnMin`, `=kcvnCur`, `>kcvnCur`, and verify the loader accepts/rejects correctly. Directly exercises the `.3MM` compat enforcement code (`Movie::FVerifyVersion` in `src/engine/movie.cpp:1783`, `DataVersion::FReadable`).
- **Tag manager with mocked content sources.** Use the `soc_set_tag_resolver` callback (task 4 above) to stub out `vptagm`. Tests: missing-CD path, multiple sources with shadow names, `ksidUseCrf` tag resolution, version-skewed source GST. The headless tag-manager mode is one of the riskiest pieces of Project 2; tests are the only way to keep it honest.

**Why this matters:** the high-value tests for this codebase aren't pixel-perfect rendering tests (too flaky for hobbyist pace). They're tests that catch silent breakage of the on-disk format invariants — exactly the regressions Project 1's sized-types audit and any future Kauai changes are most likely to introduce.

### §2b — MCP wrapper (optional add-on)

Once §2a lands, a thin MCP server exposing the same C ABI lets Claude drive 3DMMForever for exploratory regression hunting and bug-repro from issue reports ("open BONGO.3MM, jump to scene 3, list actors"). **Effort: 1-2 days.** This is a quality-of-life add-on, not load-bearing — pytest against the C ABI directly is the actual test infrastructure; MCP is just a different frontend on top.

UI-level driving (clicks, drags, easel interactions) is **explicitly out of scope** for §2a/§2b. That's a separate test-harness effort that depends on Project 3 being on Windows-x64 first, and is only worth doing if you commit to running it in CI.

**Phasing inside Project 2:** ship **read-only** first (open + enumerate + dump). Write/save (which §2a depends on) is the second milestone — write paths in `engine` are the most globally-stateful and most likely to need still more Kauai surface. MCP wrapper is the third, optional milestone.

**Risk:** medium. Header reorganisation may surface latent UI-layer leakage from engine. Tag manager headless mode is a subtle bug source. Most insidious: `engine` and Kauai both rely on global init order (`vpappb`, `vptagm`, `vpsndm` etc.) — calling shim functions before/around those globals will crash in non-obvious ways. Plan for an explicit `soc_init()`/`soc_shutdown()` pair in the shim.

**Abandonment criteria:** If task 2 (headless Kauai) reveals that >30% of `kauai/src/*.cpp` files transitively depend on `appbwin.cpp` globals, the shim is more invasive than the SDL backend itself — pivot directly to Project 3 and skip Python bindings until SDL is in.

**Done when (read-only milestone):** from Python, `Movie.open("cd3/SAMPLES/BONGO.3MM")` returns scenes/actors/sounds/tags as native Python objects, no UI launched, no CD-search dialog.

---

## Project 3: SDL3 backend (cross-platform UI)

**Why:** Mac/Linux port. The porting unit is **Kauai**, not engine. Engine is mostly platform-clean already; Kauai is what owns Win32.

**SDL3, not SDL2 or raylib.** Decision drivers:

- **8-bit indexed-palette blitting with palette animation** (kidspace fade transitions, Mczee anims). Both SDL versions have first-class `SDL_Surface` palette support; raylib's GL/texture pipeline makes palette work awkward.
- **Region-based dirty-rect drawing.** SDL has texture-update with dirty rects; raylib assumes per-frame redraw.
- **Win32 `MM_TIME`/MIDI/AVI** is the largest port surface. SDL lives closer to "Win32 minus Microsoft" — natural fit. raylib pulls in opinions about how draw should work that fight Kauai.
- **SDL3 over SDL2:** built-in `SDL_ShowOpenFileDialog`/`SDL_ShowSaveFileDialog` removes the `nativefiledialog-extended` dependency entirely. Redesigned audio subsystem shrinks the AudioMan port. Better high-DPI on Retina (matters for 640×480 kidspace scaling). SDL2 is now in maintenance; picking it today bakes in a future migration. The tradeoff is that SDL3 (stable since Jan 2025) has less battle-tested ecosystem than SDL2, but by the time Project 3 actually ships, that gap will be much smaller.

**Depends on:** Project 1 (64-bit). Strongly benefits from Project 2 (the headless Kauai work directly bridges into a clean platform-backend abstraction).

**Tasks:**

1. **Kauai SDL backend files** mirroring the existing Win32 implementations: `appbsdl.cpp`, `gfxsdl.cpp`, `gobsdl.cpp`, `filesdl.cpp`, `fnisdl.cpp`, `dlgsdl.cpp`, `menusdl.cpp`, `picsdl.cpp`, `memsdl.cpp`. CMake picks backend by `CMAKE_SYSTEM_NAME` / explicit option. File dialogs (`dlgsdl.cpp`) wrap SDL3's built-in `SDL_ShowOpenFileDialog`/`SDL_ShowSaveFileDialog` — no extra dependency needed.
2. **Replace `Vfw32`** (AVI) — *highest-risk component, see §3a*.
3. **Replace `Winmm`** (MIDI) with fluidsynth + a free GM-compatible SoundFont (or libADLMIDI for an OPL3 vibe closer to original era). Splash/ambient music.
4. **Replace `Msacm32`/AudioMan** using SDL3's audio subsystem (`SDL_AudioStream` + `SDL_OpenAudioDevice`). `audioman/audioman.cpp` is already a thin wrapper — easy swap, and SDL3's audio API is closer to AudioMan's queue/mix model than SDL2's was.
5. **Replace `MM_TIME`** with `SDL_GetTicks` / `SDL_AddTimer`. **All clocks must agree** — palette animation, MIDI scheduling, AVI playback, and game tick all need to share one timeline. Pick one early; thread it through everything.
6. **BRender source build** — see §3b. This is its own sub-project with its own risk, not a single bullet here.

**Audio/MIDI codec tests** to add alongside tasks 3-4 above: pure parsers (`MIDI` stream events, WAV via `RiffHeader` in `inc/srec.h`), no platform dependency. Tests: parse fixed `.mid`/`.wav` blobs, verify decoded byte streams match a stored reference. Critical when Project 3 swaps `Winmm`/`Msacm32`/AudioMan — these tests verify the new audio backends produce the same decoded output as the old ones, otherwise audio drift / pitch bugs go undetected until a user hears them.

### §3a — AVI replacement is the highest-risk piece

Kidspace transitions sync to AVI playback. VfW delivers frames synchronously when the next frame is ready; ffmpeg/libav's frame delivery is asynchronous and threaded. Naively swapping in `libavformat`+`libswscale` will cause:

- Palette flashes during transitions (palette swap timing now off by frame queue depth)
- Audio drift on long Mczee animations
- Frame-rate mismatches if the source AVI is variable-rate

Plan: prototype this in isolation (single-AVI test harness with audio + palette) **before** wiring it into kidspace. Worth 2-3 weeks alone.

### §3b — BRender source build is its own sub-project

> "MIT-relicensed via foone's repos" is a realistic *direction*, not a realistic *path*.

The BRender 1.3.2 / 1997 source predates `<stdint.h>`, contains inline x86 assembly fast paths, and was tested against late-90s C compilers. Building for ARM macOS is unbounded. Realistic estimate: **2-6 weeks dedicated work to get a software-rendering-only build on macOS x86_64 + ARM and Linux x86_64**. Do not block Project 3's Windows 64-bit milestone on this.

**Sub-project sequencing:** ship Project 3 first as Windows-x64-only (using existing `bren.lib` after Project 1 sized-types pass); then take on BRender source build as a separate effort; then integrate into the SDL backend for non-Windows targets.

**Regression baseline before touching BRender:** before §3b's source-build effort, build a render-to-buffer hash test suite — load a fixed `TMPL` chunk, set known camera/lighting, render one frame to a software buffer, hash. **Without this, you have no way to know if the BRender source build is correct** — the binary you replace it with will produce different pixels and you won't be able to tell "different but right" from "different and wrong." Build this suite while still running on the original `bren.lib`, store the hashes, then verify against them after every BRender source-build change.

**Risk:** high. Multi-month even excluding BRender sub-project. Many subsystems, lots of platform-specific edge cases, palette-animation and AVI-blit timing especially fiddly.

**Abandonment criteria:** if the BRender source-build sub-project fails to produce a clean macOS/Linux build after **6 weeks**, drop back to a software-3D replacement (e.g., a tiny custom SW rasterizer or porting to a modern free renderer). Document the time spent and the specific blockers.

**Done when:** `3dmovie` opens on macOS / Linux, plays through the kidspace intro, opens a sample movie, plays it back at the right framerate with sound. Vertical-slice milestone first: splash + main menu rendering on macOS, no kidspace, no movie playback.

---

## Project 4: WebAssembly / browser deployment

**Why:** Once SDL3 is in (Project 3) and BRender source compiles cleanly (§3b or its software-3D fallback), the same codebase compiles to WASM with Emscripten and runs in any modern browser. SDL3 has Emscripten as a first-class target — `appbsdl.cpp` / `gfxsdl.cpp` / etc. cross-compile without source changes. This unlocks a "click-to-watch" movie sharing path that no other approach delivers.

**Compat:** unchanged. Same `.3MM` format, same playback semantics, just different runtime. The compat constraint applies to the file format, not to where the binary executes.

**Depends on:**
- Project 1 (64-bit). The sized-types audit must be done first; without it the engine and Kauai are full of `long`-width assumptions that emcc compiles incorrectly.
- Project 3 SDL3 backend, including §3b (BRender source build) **or** its 6-week-abandonment fallback (software-3D replacement). `bren.lib` is x86 Windows only — there is no shortcut.

### Milestone 4a: Read-only web player

**Why:** A simple "click here to watch this movie" sharing path. Massively smaller scope than porting the full studio. The most useful and most realistic first milestone.

**Implementation:**

- Drop `studio` entirely. Ship a build that links `engine + brender + kauai (SDL3) + audioman` plus a minimal "movie viewer" GOK tree — viewport, play/pause, scene-forward/back, scene name, volume slider. No toolbox, no easels, no portfolio, no help machine, no kidspace navigation flow.
- **Asset bundling.** Pre-transcode the AVIs (kidspace intros / Mczee animations) to WebM and use HTML5 `<video>` instead of bringing libav into WASM — saves 5-10 MB of binary size. Stock content `.3cn` files (`bkgds.3cn`, `tmpls.3cn`, `mtrls.3cn`, `snds.3cn`, `tdfs.3cn`) are required runtime assets but lazy-loaded on first reference. Tag manager already does drive-scan; adapt it to async fetch from a CDN.
- **`.3MM` input.** User picks via `<input type="file">` or File System Access API. Movie loads, autoplays. URL-based loading (e.g., `?movie=https://...`) for embeddable links.
- **No save support.** Read-only: opens, plays, stops. Eliminates the IDBFS / download-blob complexity for this milestone.
- **Audio.** SDL3's audio backend over Web Audio API. AudioMan replacement (Project 3 task 4) compiles to WASM the same way it does on macOS/Linux.
- **MIDI.** Either ship fluidsynth-WASM (~3 MB) plus a SoundFont, or pre-render the MIDI cues to PCM and ship those. PCM is smaller per-asset and avoids the runtime synth dependency; pick this for 4a.

**Scope: 6-8 weeks after Project 3 + §3b land** (or after the software-3D fallback is in). Most of the work is the simplified viewer GOK and the asset bundle / lazy-load pipeline. The build configuration (CMake + emscripten toolchain file) is bounded — a few days.

**Risk:** medium. Biggest unknowns are BRender-WASM (already covered by §3b's risk profile) and Web Audio latency vs. palette-animation timing (kidspace transitions are tight; verify on real browsers).

**Done when:** a static-hosted page loads `BONGO.3MM` (or another sample) and plays it through to the end with sound, in current Chrome/Firefox/Safari, with the kidspace intro skipped.

### Milestone 4b: Full web editor

**Why:** All of the studio in the browser — author and save movies entirely in browser. Useful but niche; editing 1995-format movies in a 2026+ browser is a narrow use case.

**Implementation:**

- Same WASM build as 4a but linking `studio` instead of the simplified viewer GOK. Includes the full toolbox, easels, browsers, portfolio.
- **Save/load.** File System Access API where supported (Chrome family); fall back to IDBFS for in-browser persistence and download-blob for export. Engine code calling `FileObject::PfilFromFni` synchronously needs adapting — Emscripten's IDBFS makes it look synchronous after a preload, but new file picks have to round-trip through async UI.
- **Threading.** Optional; single-threaded build first. Adding pthreads requires SharedArrayBuffer which requires COOP/COEP headers, which restricts embedding contexts. Skip unless profiling shows a real bottleneck.

**Scope: another 4-6 weeks** on top of 4a. Mostly UX adaptation (async file dialogs, save UX) and audio polishing.

**Risk:** medium-low given 4a is in. Most of the architectural risk (BRender, asset pipeline, audio) was paid down in 4a.

**Done when:** in a browser, the user can open a sample movie, edit it (add an actor, record a path, change a camera, save a scene), and export the result back to a downloadable `.3MM` file that round-trips correctly through desktop 3DMMForever.

**Honest framing:** 4b is more 3DMMPlus material than 3DMMForever. The compat constraint says "the file format works," but a full editor in the browser is enough scope to justify a fresh evaluation of whether to keep targeting the 1995 format. Defer the 4b decision until 4a has shipped and use is observed.

---

## Studio UI features (incremental backlog)

Discrete UX additions that don't fit Projects 1-3 but are individually shippable. UI-1 through UI-6 are pure UI changes that don't touch the `.3MM` file format. UI-7 produces video output (not `.3MM`), so it's also compat-safe. UI-8 produces standard one-scene `.3MM` files using the existing format. UI-9 *does* persist new content into `.3MM` files (custom CAM chunks embedded under user-customized BKGD copies) — but does so additively, using the same `ksidUseCrf` embedding pattern the studio already uses for custom MTRLs and TMPLs, so 1995 3DMM continues to load and play these movies correctly.

Listed roughly in increasing scope.

### UI-1: Exact-input dialogs for rotate / move / scale (low effort, low risk)

**Why:** Today every transform tool is mouse-drag-driven only. Authors who want "rotate exactly 90°" or "move actor to (X, Y, Z) = (50, 0, 100)" have no way to do it precisely.

**Implementation:** new `cidRotateActorTo` / `cidMoveActorTo` / `cidScaleActorTo` cids. Each has a small dialog with X/Y/Z text fields and an absolute/relative radio. Studio handler reads the dialog and calls `Movie::FRotateActr` / a new `Movie::FMoveActrTo` wrapper / `Movie::FScaleActr`. Add toolbox glyphs in `sectools.cht` or as menu items in `popups.cht`. Engine API is already there (`FRotateActr` accepts BRA radians, body has `SetPosition`); only the studio UI needs work.

**Scope: 2-3 days per tool.** Touches: 1 dialog, 1 cid handler, 1 wrapper method, 1 glyph each.

### UI-2: Numeric pose readout (low effort, low risk)

**Why:** Authors editing a path can't see exact actor coordinates. A status-line readout showing "X=12.3 Y=0.0 Z=-45.7 RotY=90°" updated on every tick eliminates a class of "did I move it where I meant to?" bugs.

**Implementation:** small `gob` rendered in a corner of the viewport, polls `Pscen()->PactrSelected()->Pbody()->GetPosition()` and orientation each frame. No engine changes required.

**Scope: half a day.**

### UI-3: Constrain-to-axis modifier (low effort, low risk)

**Why:** When you want to drag an actor along just one axis, holding Shift should lock the others. Standard 3D-tool behavior; absent today.

**Implementation:** in `MovieView::_FActorMouseDrag`, when `_grfcust & fcustShift`, snap the dominant axis component and zero the others before passing to `FMoveActr`. Same applies to rotation tools.

**Scope: 1-2 days.**

### UI-4: Free-fly preview camera (medium effort, medium risk)

**Why:** Today cameras are pre-authored presets per background; authors pick from N pre-baked angles via the camera browser, recorded as `sevtChngCamera`. There is no way to look at a shot from any other angle while editing. A free-fly preview camera lets authors orbit/pan/dolly during editing to inspect the scene; recorded playback still uses the preset.

**Importantly:** this is editor-only. The recorded `.3MM` still references preset cameras via `sevtChngCamera` — no on-disk format change. **Persisting custom poses is a separate feature (UI-9)** that uses BKGD embedding to stay 1995-compat-safe at a file-size cost; see UI-9 for details.

**Implementation:**

- New `_fFreeCam` flag on `MovieView` (or `Scene`).
- New tools `toolCamOrbit`, `toolCamPan`, `toolCamDolly`, `toolCamZoom`. Mouse-drag deltas modify a held `BMAT34` + FOV in place. Each tick calls `World::SetCamera(pbmat34, ...)` directly (`bren/bwld.cpp:429`), **bypassing `Background::FSetCamera`** so the preset state isn't disturbed.
- "Reset to preset" button — `_pbkgd->FSetCamera(pbwld, _pbkgd->Icam())`.
- Force-reset to preset when entering playback (`Movie::FPlaying()` true) or when starting to record actor motion.
- Actor placement (click-to-place ground-zero projection, `Background::_bmat34Mouse`) **always uses the preset matrix**, never the preview camera. Otherwise users would place actors based on a temporary view and be confused when the recorded shot has them in the "wrong" place.
- Toolbox UI: new "Camera" cover under the Settings primary tools — glyphs in `popups.cht`, scripts firing the new cids, command-map handlers in `studio.cpp`.

**Scope: 2-3 weeks.** Risk: medium. No file format changes, no compat exposure, no LP64 concerns — can ship without waiting on Project 1.

### UI-5: Multi-actor selection — Phase 1 (move-tool MVP) DONE

**Why:** Today `Scene::_pactrSelected` is exactly one pointer (`inc/scene.h:147`). Operating on N actors requires N independent edits. Standard editor multi-select would let users rotate, move, copy, or delete a group at once.

**Phase 1 (move-tool MVP), shipped on branch `c`:**

- Spec: `docs/superpowers/specs/2026-04-29-multi-select-move-mvp-design.md`
- Plan: `docs/superpowers/plans/2026-04-29-multi-select-move-mvp.md`
- Commits: `b27ac34..bf74f13` (Tasks 1-5 with their fix-ups).

What landed:

- `Scene::_pglpactrSelExtra` (lazy `DynamicArray` of additional selected actors, `pvNil` when no extras) plus `FIsActrSelected` / `CactrSelected` / `PactrSelectedAt` / `FToggleActrSelected` / `ClearSelection` accessors. `SelectActr` clears extras as well as primary; `RemActrCore` scrubs extras when an actor is removed (no dangling pointers).
- `ActorMoveGroupUndo` composite undo: owns N child `ActorUndo` snapshots and replays them as a unit. The undo loop stops on the first child failure to avoid post-`ClearUndo` perturbation.
- Shift-click an actor with the move/default/select tool to toggle its membership in the selection set (3 px drag tolerance disarms the toggle and falls through to the existing single-actor drag).
- With N≥2 selected, a `toolCompose` drag iterates the selection and applies the same world-space delta to each via `FMoveRoute` / `FTweakRoute`. Cmd-tweak and Shift-entire-subroute modifiers carry through per-actor. One drag = one undo step (incidentally fixed a latent Tweak-undo leak; see commit `c1022fd`).
- Esc (`kvkEscape`, fixed Mac value at the same time) clears the entire selection.
- Ctrl+A (Cmd+A on Mac) selects every actor in the current scene via new `Scene::FSelectAllActrs`; first actor becomes primary, rest are added as extras. Falls through to the base in text mode so TBOX edit can still interpret it as select-all-text.
- Non-move tools collapse to single-select on mousedown via the existing `SelectActr` path — no extra code needed, since Task 1's `SelectActr` clears extras.
- Compat: pure runtime UI state, never persisted. Original 1995 3DMM playback unaffected.

**Acceptance status (9 criteria from the spec):**

Automated-verified by build + spec-compliance + code-quality reviews:

- (1) Single-actor behavior bit-for-bit identical: build passes; reviewers confirmed single-select code paths unchanged.
- (5) One-undo-per-drag: code review traced per-actor `ActorUndo` refcount/ownership through `ActorMoveGroupUndo`.
- (8) Deleting a selected actor leaves no dangling pointer: `RemActrCore` scrubs extras (Task 1 fix-up `3775fcf`).
- (9) Original 1995 3DMM still loads / plays movies produced: theoretical — nothing was added to disk format.

Deferred to user manual smoke (interactive click sequences, cannot run GUI from a subagent):

- (2) Shift-click on unselected actor adds it; hilite turns on; no drag.
- (3) Shift-click on selected actor removes it; hilite turns off.
- (4) N≥2 + `toolCompose` drag translates all selected by the same delta. Cmd / Shift modifiers apply per-actor.
- (6) Esc clears all selection.
- (7) Non-move-tool click on actor collapses selection.

**Phase 2a (rotate / resize / squash-stretch around group centroid) shipped on branch `c`:**

- Spec: `docs/superpowers/specs/2026-04-30-multi-select-rotate-scale-design.md`
- Plan: `docs/superpowers/plans/2026-04-30-multi-select-rotate-scale.md`

What landed:

- `Scene::FXyzSelectionCentroid` averages currently-visible (`FIsInView`) selected actors' world positions.
- `MovieView::_xrPivot/_yrPivot/_zrPivot` capture the centroid at mousedown when the tool is rotate / resize / squash and N≥2 are selected; same `ActorMoveGroupUndo` infrastructure as Phase 1 captures per-actor snapshots.
- `_MouseDrag` rotate / resize / squash branches on `_paundGroup != pvNil`: per actor per tick, compute world-space `delta_i` to orbit/scale the actor around the frozen pivot, then `FMoveRoute(delta_i)` and `FRotate / FScale / FPull` for the local body change. Cmd modifier (`fFromHereFwd`) carries through. Single-select path unchanged.
- One drag = one undo step (reuses `ActorMoveGroupUndo`).

**Phase 3 (named selection groups via hashtag tags) shipped on branch `c`:**

- Spec: `docs/superpowers/specs/2026-04-30-actor-tag-groups-design.md`
- Plan: `docs/superpowers/plans/2026-04-30-actor-tag-groups.md`

What landed:

- `Scene::FSelectActrsByTag` parses ASCII `#tag` substrings out of actor names (case-insensitive) and replaces the current selection with every matching actor in the scene.
- `ActorRenameGroupUndo` stores counted names and actor IDs in a C89 table and swaps all names in one undo or redo.
- `Ctrl+G` appends the scene's next `#N` to every selected name. `Ctrl+Shift+G` strips a trailing tag. `Ctrl+1`..`Ctrl+9` reselects the matching numeric group.
- Tags live in the movie's roll-call names. Retail 3D Movie Maker displays and saves the suffix as part of the name.

**Phase 2+ remaining work (still in scope for "UI-5"):**

- Marquee / box-select / lasso selection.
- Multi-target costume change, sooner/later, action change.
- `BuildActionMenu` becomes the intersection of available actions across the selection.
- Distinct hilite color for primary vs. secondary selected actors.
- Multi-select for text boxes, and mixed actor + tbox selection.
- Phase B of named groups: alphanumeric tag picker (`Ctrl+/`), dimmed `#tag` rendering in roll-call, movie-wide `#tag` operations.
- Optional `fMultiSelect` feature flag if a regression risk emerges.

**Scope: phase 1 shipped; phase 2+ is 2-3 weeks of additional work, medium risk** — touches every tool path. Compat-safe (selection state isn't persisted to `.3MM`).

### UI-6: Integer UI scaling for modern displays (medium effort, medium risk)

**Why:** Studio runs at fixed 640×480, illegible on modern Retina/4K displays. Authors want 2x/3x/4x scale.

**Implementation: defer to Project 3.** SDL3 provides this almost for free via `SDL_SetRenderLogicalPresentation` with `SDL_LOGICAL_PRESENTATION_INTEGER_SCALE`. Doing it on Win32 first would require introducing a logical-canvas concept into Kauai's `gob.cpp` + `gfxwin.cpp` plus mouse-coord unscaling on the input path — 2-3 weeks of work that becomes nearly free under SDL3.

**Scope: 1-2 days as part of Project 3 SDL3 backend; 2-3 weeks if attempted on Win32 first.** Don't do the Win32 version.

### UI-7: Render-to-video with free-fly camera (medium effort, medium risk)

**Why:** Authors who want to share a movie as video (rather than as a `.3MM` requiring 3DMM to play) have no path today. Combined with UI-4, this also enables machinima — render an existing movie's timeline through a user-chosen camera path, write to MP4/WebM/AVI.

**Depends on:** UI-4 (free-fly camera) and Project 3 §3a (which already pulls libavformat/libavcodec into the build for AVI playback replacement).

**Implementation:**

- Capture each rendered frame from BRender's RGB output buffer (via the existing palette-to-RGB conversion path) and feed to `libavcodec`'s encoder.
- New `cidRenderToFile` command. Dialog asks for output path, codec choice, resolution (1×/2×/4× the logical 640×480), frame range.
- During render, **suppress the scene's `sevtChngCamera` events** when a free-fly path is active — otherwise BRender camera would jump back to the preset every time the recorded event fires.
- Audio: muxed from the existing scene playback (sound events) into the output file. Same audio in render mode as in playback.
- Optional: a small camera-path **sidecar file** (e.g., `.3SH` "shot list") storing `(scene_index, frame, camera_matrix, fov, hither, yon)` keyframes. Render interpolates between keyframes. Sidecar is a separate file from the `.3MM` — original movie unchanged, multiple sidecars give multiple takes.

**Scope: 4-6 weeks** including audio mux, palette-state-per-frame handling, codec parameter UX. The encoder integration is the bulk; the camera-suppression flag and sidecar format are small additions on top.

**Compat:** doesn't touch `.3MM`. Output is video, not movie. Sidecar is a new file type the new reader knows about and the old reader ignores (sidecars never get loaded by 1995 3DMM).

### UI-8: Reusable scene library (medium effort, low risk)

**Why:** Today scenes only exist inside `.3MM` movies. A scene authored once with a particular set of actors, paths, sounds, and camera cuts can't be reused in another movie without manual recreation. A scene library lets authors save individual scenes and import them into other movies.

**Key insight: no new file format needed.** `SCEN` chunks already write to any `PChunkyResourceFile` via `Scene::FWrite` (`src/engine/scene.cpp:4187`) and read from any via `Scene::PscenRead` (line 3764). A scene library is just a directory of one-scene `.3MM` files — each is a tiny standard movie with a single SCEN under MVIE. Original 1995 3DMM can open these as single-scene movies; the new reader can extract the scene and inject it into another movie.

**Implementation:**

- **"Save scene as..."**: pick scene N from the open movie. Create a new chunky file with file-type `'MVIE'`, write a fresh MVIE root chunk (`MovieFilePrefix` with `bo`, `osk`, `dver` set to current values), call `Scene::FWrite(pcrfDest, &cnoScen)`, adopt the SCEN under MVIE at chid 0, write the standard companion children (roll-call GST, source GST). Save with `.3MM` extension.
- **"Insert scene from..."**: file picker for `.3MM` files. Open as `PChunkyResourceFile`, find the first SCEN child, call `Scene::PscenRead`, then use the existing scene-paste path to insert into the current movie at the chosen position. Tag references resolve through the destination's tag manager — the destination must have access to the same content sources (templates, materials, sounds, **backgrounds**) the source referenced.
- **Optional library browser**: kidspace browser (modeled after the camera/actor browsers) listing `.3MM` files in a known directory, showing each scene's thumbnail (the SCEN already has a `kctgThumbMbmp` child), drag-drop into the current movie.

**Camera handling:**
- Preset cameras (`sevtChngCamera icam=K` referencing a built-in BKGD camera) ride along automatically — they're just integers in the SCEN's frame-event GST.
- If the source scene used a custom camera (UI-9), the saved scene must also embed the BKGD copy with the custom CAMs (or the import has nothing to resolve the high icam against). UI-9 import logic handles this.

**Scope: 2-3 weeks.** The hard part isn't the file format — it's tag resolution across movies (importing requires the destination's content stack to provide the same template/material/sound chunks the source referenced). For backgrounds with custom cameras, see UI-9.

### UI-9: Custom camera authoring with BKGD embedding (high effort, medium risk)

**Why:** UI-4 lets authors free-fly to a custom angle but only for preview. UI-9 makes that pose **persist** in the movie file — the recorded `.3MM` plays back from the custom angle in both 3DMMForever and original 1995 3DMM.

**Compat strategy:** the chunk-graph design already supports this without new chunk types or new event types:

- Cameras are `CAM` chunks owned as children of the BKGD chunk (`src/engine/bkgd.cpp:258`).
- `sevtChngCamera icam=N` looks up child #N of the scene's BKGD via generic chid enumeration (no fixed range check).
- The studio already embeds custom MTRL/TMPL chunks in movies via `ksidUseCrf` tags — the same mechanism applies to BKGDs. When the tag resolves, the engine reads the BKGD from the movie file instead of `bkgds.3cn`.
- 1995 3DMM uses the same tag-resolution code path. It will load the embedded BKGD, count the extra CAM children correctly, and resolve `sevtChngCamera icam=N+K` to the new camera's matrix without any code changes. **Compat-safe.**

**Implementation:**

- When the user records a free-fly pose (UI-4 active + "save this angle" command):
  1. Check if the scene's BKGD is already embedded (`tagBkgd.sid == ksidUseCrf`). If not:
     - Copy the standard BKGD chunk from `bkgds.3cn` into the movie file.
     - Adopt all of the standard BKGD's children (existing CAMs, BDS, GLLT, palette, etc.) as children of the embedded copy. Use the chunky-file's multi-parent child support if possible to share data; otherwise deep-copy.
     - Update `tagBkgd` to reference the embedded BKGD (`sid = ksidUseCrf`, new cno).
  2. Write a new `CAM` chunk into the movie with the custom pose's data (`CameraPosition` struct: matrix, FOV, hither/yon, APOS).
  3. Adopt the new CAM as a child of the embedded BKGD at chid `_ccam` (next index past the existing presets).
  4. Write a `sevtChngCamera icam=_ccam` event into the SCEN's frame events GST at the current frame.
  5. Increment `_ccam` for future custom cameras in this movie.

- **Garbage collection at save:** add BKGD GC to `_FDoMtrlTmplGC` (or sibling routine) — if no scene's `sevtChngCamera` references an embedded BKGD's custom CAMs anymore, drop the embedded BKGD copy and revert tags to the standard `bkgds.3cn` reference.

- **UI:** "Save current preview camera as new angle" button on the camera toolbar (next to UI-4's free-fly tools). Once saved, the new angle appears in the camera browser alongside built-in presets.

**File-size caveat:** an embedded BKGD is 200 KB-2 MB depending on the source. Each distinct background that gets a custom camera adds that overhead to the movie file. **Multiple scenes using the same custom-camera background share the embedded BKGD** (multi-parent child support means the embedded BKGD is referenced by all scenes that use it; only embedded once per movie).

**Scope: 4-6 weeks.** The BKGD-embedding logic is the load-bearing piece; needs careful testing to confirm 1995 3DMM correctly resolves the embedded BKGD, plays the custom angles, and doesn't lose data on re-save. Add a regression test (per §0a / §2a) that round-trips a custom-camera movie through 1995 3DMM (or, since we don't have a 1995 binary, through a strict-mode 3DMMForever reader that simulates the 1995 behavior).

**Risk:** medium. Edge case to watch: BKGDs reference content (palette chunks, lights, default sound) that may itself be tagged. Embedding a BKGD requires resolving and either copying or sharing those references. The garbage-collection logic at save time has to be careful not to drop chunks that the embedded BKGD's children still reference.

### UI-10: Flash-style timeline widget (high effort, low risk)

**Why:** Today the studio's only timeline UI is a single frame slider plus a scene scrollbar. Authors can't see at a glance when an actor enters or leaves, when a sound starts, when a camera cut happens. Multi-actor scenes are hard to visualize. A Flash-style timeline — horizontal time axis, vertical layer stack, bars showing object lifetimes, keyframe icons for scene events, scrubbable playhead — is a substantial UX upgrade.

**Why this codebase is well-suited:**

- The data is already there in the right shape. `Scene::_pggsevFrm` is literally a list of `(nfrm, sevt, payload)` tuples — exactly what a timeline visualizes. Actor paths are per-frame continuous keyframed data (`PATH` chunks). The playhead is `Scene::_nfrmCur`.
- No file format changes required — purely a new view onto existing data.
- No engine API changes for read-only visualization. For drag-to-retime editing, the engine already has scene-chop / scene-paste primitives that shift events; the timeline just exposes them via a new UI path.
- Compat-safe by construction (UI-only).

**Phased implementation:**

- **Phase 1 — Read-only visualization (~3-4 weeks).** New `gob` rendered below the 3D viewport (or pop-out). Horizontal axis: frames. Vertical layers: one per actor (lifetime bar from sevtAddActr to scene end), one per text box, one per sound channel, one per camera (cuts shown as keyframe markers), one per scene-level event. Playhead drawn at `_nfrmCur`. Click-to-scrub: clicking on the timeline jumps `_nfrmCur` to that frame. No editing yet, just a viewer that always stays in sync with the live scene state.
- **Phase 2 — Drag-to-scrub + multi-select (~2-3 weeks, depends on UI-5).** Drag the playhead. Multi-select keyframes. The undo-grouping work in UI-5 unblocks bulk timeline operations.
- **Phase 3 — Drag-to-retime events (~3-4 weeks).** Drag a keyframe to a different frame, scene event's `nfrm` updates. Drag an actor's lifetime bar to shift their entire path data — uses existing scene-chop / scene-paste primitives under the hood. Bulk drag of N keyframes coalesces to one undo group (UI-5 prerequisite).
- **Phase 4 — Bonus features (variable scope).** Onion skinning (overlay neighboring frames), tweening visualization, keyframe-type icons (different glyph per `sevt`), zoom in/out on the timeline, horizontal scroll for long scenes. These are individually scoped sub-features that can be added or skipped per priority.

**Layout challenge:**

640×480 is small. The timeline needs vertical space the studio doesn't easily give up. Options:

- Pop-out window (Kauai supports MDI gob trees). Simple but requires multi-window coordination.
- Replace one of the secondary tool panels when timeline mode is active (toggle on/off via toolbar button).
- Wait for UI-6 (integer UI scaling) — at 2x scale the studio has 1280×960 logical pixels and the timeline can sit below the viewport without crowding.

The "wait for UI-6" path is probably the cleanest, which also implies waiting for Project 3 SDL3 backend to land integer scaling cheaply. Or just ship Phase 1 in pop-out form on the existing 640×480 layout and let UI-6 unblock the integrated layout later.

**Scope: 8-12 weeks** for Phases 1-3. Risk: low — no engine changes, no file format, no compat exposure. The risk is purely scope creep; Flash-style timelines are notoriously fractal (per-property keyframes, custom interpolation curves, motion paths overlaid on viewport, layer locking/visibility, frame labels, looping markers). Stay disciplined with the phased rollout.

**Dependencies:** UI-5 (multi-actor selection) is a hard prerequisite for Phase 2-3 because the undo-grouping infrastructure for bulk operations is the same problem solved twice if done independently. UI-6 (integer scaling) is a soft prerequisite for the integrated layout (Phase 1 can ship as pop-out without it).

**Compat:** zero. This is purely a new view + new UI input paths over existing data. The on-disk `.3MM` format doesn't even know the timeline exists.

### UI-11: Custom prop composer (medium effort, low risk)

**Why:** 3DMM ships with a fixed set of props (chairs, vehicles, signs, etc.) authored at Microsoft. Users can't author new ones. A primitive-composer easel (similar in shape to the existing 3D Words easel) lets authors build static props from cubes, cylinders, spheres, etc., resize/position them, and save the result as a custom prop usable like any built-in.

**Why this codebase fits well:**

A "prop" in 3DMM terminology is just a `TMPL` chunk with one flag bit set: `ftmplProp` (`inc/tmpl.h:134`). Props use the same data structures as character actors — same body hierarchy, same action system, same path system. The bit only controls which browser the prop appears in (props vs. actors). A static prop is a *very* simple TMPL: one body part, one ACTN ("rest"), one CEL pointing at a single MODL chunk. Nothing exotic.

`EngineModel::New(cbrv, prgbrv, cbrf, prgbrf)` in `inc/modl.h` is the native authoring binding — takes raw vertex/face arrays and builds a Model. The 3D Words easel already uses this pattern to generate 3D text geometry on-the-fly. Same pattern applies for prop geometry.

The studio already embeds custom TMPL chunks in movies via `ksidUseCrf` tags (the mechanism behind custom-character costumes). `_FDoMtrlTmplGC` (`src/engine/movie.cpp:2774`) garbage-collects unreferenced embedded TMPLs at save time — works for custom props with no changes since the GC doesn't distinguish prop from character TMPLs.

**Compat:** original 1995 3DMM resolves embedded TMPLs via the same tag-resolution code path used for custom characters today. It reads the embedded MODL, hands the geometry to BRender, renders. **Custom user-built props work in 1995 3DMM unchanged**, no new chunk types or event types required.

**Phased implementation:**

- **Phase 1 — Primitive composer (5-7 weeks).**
  - Pure-C++ primitive geometry generator: cube, cylinder, sphere, cone, plane. Returns `(BRV*, count, BRF*, count)` arrays. ~1 week.
  - New easel — kidspace `.cht` + new gob. Shape similar to the 3D Words easel: small preview viewport, primitive picker, per-primitive transform widgets (position X/Y/Z, rotation X/Y/Z, scale), part list, name field, "save as prop" button. ~3 weeks.
  - MODL generator: takes the user's primitive composition, applies transforms, optionally welds duplicate vertices, emits a single MODL chunk via `EngineModel::New` then writes via `EngineModel::Write`. ~1 week.
  - TMPL writer: wraps the MODL in a TMPL chunk with `ftmplProp` set, single body part, single "rest" ACTN with one CEL referencing the MODL by chid. ~1 week.
  - Embed + roll-call wiring: add the new TMPL to the movie via `ksidUseCrf`, register in the props browser (the browser already filters roll-call by `ftmplProp`, no browser changes needed). ~1 week.
- **Phase 2 — Materials + color (2-3 weeks).** Assign existing MTRLs (or solid colors via a CMTL) to faces. Per-primitive material picker. UV generation: simple planar/box projection per primitive type.
- **Phase 3 — Multi-part / articulation (3-4 weeks).** Build a TMPL with N body parts (one per primitive), so user can later author a prop with movable parts. Reuses the body hierarchy infrastructure that character actors already use.
- **Phase 4 — Mesh import (open-ended).** Import `.obj` / `.gltf` / similar, convert to BRender vertices/faces, emit MODL. Substantial scope and intersects with the v3dmm content pipeline. Defer.

**Caveats:**

- **BRender geometry validation.** Stock MODLs were authored via SoftImage and tend to be clean closed manifolds. User-built geometry might have non-manifold edges, flipped face winding, near-coincident vertices. BRender's renderer can glitch on these (z-fighting, missing faces). Add a validator pass that runs on Save with warnings/auto-fix for common cases.
- **Pivot point.** Each MODL has a `bvec3Pivot` controlling rotation behavior. Default to bounding-box center; expose an override in the easel.
- **No texture coords in Phase 1.** Untextured geometry only. Phase 2 adds materials and the UV-generation problem.
- **File size.** Embedded MODLs are small (KB range); embedded TMPLs are slightly bigger but still small. Multiple custom props per movie are cheap. Garbage-collected at save when unused.

**Scope: 5-7 weeks for Phase 1; 10-14 weeks for Phases 1-3.** Phase 4 (mesh import) is open-ended.

**Risk:** low — engine API is fully there, embedding pattern is existing, browser integration is automatic. Main risk is BRender quirks with user-built geometry; mitigated by the validator pass.

**Dependencies:** none. Could ship today on Win32 without Projects 1-4. UI-1 (exact-input dialogs) is a nice-to-have for the per-primitive transform widgets but not required.

### Recommended order

Bundles into roughly five phases:

1. **Quick wins (1-2 weeks total):** UI-1, UI-2, UI-3. Each ships visible value, each proves the "add a tool" pattern works without disturbing anything load-bearing. Any order, parallelizable.
2. **Free-fly camera (2-3 weeks):** UI-4. Higher value, more design surface; requires the camera-cover toolbar and careful interaction with actor-placement projection. Editor-only — doesn't touch the file format.
3. **Custom-content composers (10-16 weeks total):** UI-11 prop composer (5-7 weeks Phase 1; +5-7 weeks Phases 2-3) and UI-9 custom camera authoring (4-6 weeks, depends on UI-4). Both add user-authored content via `ksidUseCrf` embedding — natural to design and ship together since they share the embedded-content patterns.
4. **Scene library (~2-3 weeks):** UI-8. Independent of UI-9/UI-11 but synergizes — once they land, library scenes can carry custom cameras and custom props via their embedded TMPLs/BKGDs.
5. **Project 3-era (UI-5, UI-6, UI-7, UI-10):** UI-5 multi-select needs careful undo/menu design (3-5 weeks); UI-6 integer UI scaling falls out of SDL3's renderer almost for free; UI-7 render-to-video sits naturally on top of Project 3 §3a's libav integration; UI-10 timeline is the most ambitious (8-12 weeks, hard prereq on UI-5). All gated on Project 3 reaching at least Windows-x64.

Each item is independently shippable and independently revertible.

## Sequencing

**11 November 2026 update:** the shared C89 engine migration above is the active
backend roadmap. Its slices integrate with the existing Studio and Marcus
builds and use the playback regression corpus throughout. The Project 0-4
sequence below is historical modernization context, not a requirement to defer
engine conversion until Python bindings, another UI port or WASM is finished.

1. **Project 0 (renames)** — already in flight. Background task; can interleave with anything else.
2. **Project 1 (64-bit build)** — first real project. **Hard prerequisite for 3 and 4.** Don't start Project 2 until at least task 4 (sized-types audit) is done; otherwise the shim ABI bakes in `long`-width assumptions you'll have to redo.
3. **Project 2 (libsoc + Python)** — second. Validates engine/UI separation. Read-only milestone unlocks tooling immediately. Write milestone is a follow-up, not part of the same shippable.
4. **Project 3 (SDL3)** — third. Windows-x64 milestone first, then BRender source sub-project, then macOS/Linux.
5. **Project 4 (WASM / browser)** — fourth, gated on Project 3 reaching macOS/Linux + §3b's BRender-source-build (or its software-3D fallback). Ship 4a (read-only player) before 4b (full editor); defer the 4b decision until 4a has shipped.

Projects 0, 1, and 2 are independently shippable. Projects 3 and 4 have hard dependencies on Project 1, and Project 4 has a hard dependency on Project 3.
