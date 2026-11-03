# Engine (`src/engine/`)

The engine is the movie runtime: actors, scenes, movies, 3D bodies and
materials, templates, sounds, and tag-managed asset references. Headers
all live in [`inc/`](../../inc/).

The library is split: stored data, assets/Body state, actor evaluation,
playback decisions and pending-sound ownership live in `engine-core`;
editing, World integration, resource effects, native clocks/devices and
document/view code live in `engine`. Core consumers include
`soc_core.h`; full runtime consumers include `soc.h`. See
[`library-split.md`](library-split.md) for ownership and verification.

## Movie and scene (core data/control; runtime views and effects)

| File          | Header              | Purpose                                                        |
|---------------|---------------------|----------------------------------------------------------------|
| `movie_playback.cpp` | `inc/movie_playback.h` | Core playback state, startup/toggle and render/pause/clock decisions with ordered host commands. |
| `movie.cpp`   | `inc/movie.h`       | `Movie` — top-level movie document. Owns scenes, roll call, sounds. Entry points: `FLoadAutoSave`, `FAutoSave` (`movie.cpp:2480`). |
| `scene.cpp`   | `inc/scene.h`       | `Scene` — single scene playback. Owns actors, sound events, background. `SceneSoundEvent` + `TagChildPair` for sound triggers. |
| `tbox.cpp`    | `inc/tbox.h`        | `TextBox` — 3D text-box scene object. Embeds rich text + 3D rendering. |

These three are gui-coupled because they hook into the kauai `docb` /
`gob` machinery (undo, command dispatch, view invalidation). See
`project_cmd_split_followup.md` for the rationale.

## Actor system (data/track in `engine-core`; editing/playback in `engine`)

| File            | Header           | Purpose                                                       |
|-----------------|------------------|---------------------------------------------------------------|
| `actor_data.cpp` | `inc/actor_data.h` | Core actor-header, route and event wire codecs. |
| `actor_seek.cpp` | `inc/actor_seek.h` | Core checkpoint/quick/replay seek control with explicit runtime commands. |
| `actor_timeline.cpp` | `inc/actor_timeline.h` | Core lifetime, stall and backward/quick-seek calculations with borrowed step metadata. |
| `actor_frame.cpp` | `inc/actor_frame.h` | Core per-frame sequencing with ordered host commands and final effect directives. |
| `actor_state.cpp` | `inc/actor_state.h` | Core evaluated state snapshots and pure value-event application. |
| `actor_pose.cpp` | `inc/actor_pose.h` | Core rest/stretch/rotation and route-tangent pose calculation. |
| `actor_route.cpp` | `inc/actor_route.h` | Core route advancement, event clipping and position interpolation. |
| `actor_track.cpp` | `inc/actor_track.h` | Core route/event ownership, transactional cloning and TAG lifetimes. |
| `actor.cpp`     | `inc/actor.h`    | `Actor` — runtime actor object. Pose, current action, position. |
| `actredit.cpp`  | `inc/actor.h`    | Actor editing operations (insert / delete / modify events).   |
| `actrsave.cpp` | `inc/actor.h` | Actor I/O delegates wire conversion to ActorData and resolves resources for live actors. |
| `actrsnd.cpp`   | `inc/actor.h`    | Actor sound-event handling (`aetSnd`).                        |

The actor event stream uses paired runtime / wire structs for events
that embed `TAG`: `Costume`/`CostumeOnFile`, `Sound`/`SoundOnFile`. See
[`../file-formats/3mm-movie.md`](../file-formats/3mm-movie.md#actr-actor-children).

## 3D rendering data (`engine-core`)

| File          | Header           | Purpose                                                       |
|---------------|------------------|---------------------------------------------------------------|
| `body.cpp`    | `inc/body.h`     | `Body` — bound BRender actor: hierarchy of body parts with materials. |
| `modl.cpp`    | `inc/modl.h`     | `Model` — geometry (vertices + faces). Loads `BMDL` chunks. See [`../file-formats/bmdl-models.md`](../file-formats/bmdl-models.md). |
| `mtrl.cpp`    | `inc/mtrl.h`     | `Material` (`MTRL`) and `CustomMaterial` (`CMTL`).            |

## Templates & 3D text (`engine-core`)

| File          | Header           | Purpose                                                       |
|---------------|------------------|---------------------------------------------------------------|
| `tmpl.cpp`    | `inc/tmpl.h`     | `Template` — actor template (the underlying definition for an actor: model + actions + costumes). Loads `TMPL` chunks. |
| `tdf.cpp`     | `inc/tdf.h`      | `ThreeDTextFont` — 3D-text font definition (`TDF `).          |
| `tdt.cpp`     | `inc/tdt.h`      | `ThreeDTextTemplate` — 3D-text template (`TDT `, geometry generated from a font + string). |

## Sound (pending queue policy in `engine-core`; devices/import in `engine`)

| File          | Header           | Purpose                                                       |
|---------------|------------------|---------------------------------------------------------------|
| `sound_queue.cpp` | `inc/sound_queue.h` | Core pending-request ownership, ordering and preview-duration policy. |
| `msnd.cpp`    | `inc/msnd.h`     | `MovieSoundMSND` (the `MSND` chunk wrapper) + `MovieSoundQueue` (a `CommandHandler` subclass driving timeline-aware playback). |
| `srec.cpp`    | `inc/srec.h`     | Sound record/import — capture from microphone, import WAV.    |

## Asset / tag management (`engine-core`)

| File          | Header           | Purpose                                                       |
|---------------|------------------|---------------------------------------------------------------|
| `tagman.cpp`  | `inc/tagman.h`   | `TagManager` — resolves `(sid, ctg, cno)` tags to in-memory objects, handles CD prompts, manages HD cache. |
| `tagl.cpp`    | `inc/tagl.h`     | `TagList` — collected list of tags for batch operations.      |

`TAG` carries a runtime `PChunkyResourceFile pcrf`; the wire form
`TAGOnFile` is fixed at 16 bytes. Marshal at every I/O boundary.

## Background (`engine`)

| File          | Header           | Purpose                                                       |
|---------------|------------------|---------------------------------------------------------------|
| `bkgd.cpp`    | `inc/bkgd.h`     | `Background` — 3D scene background: cameras, lights, sky.     |

## Where to add things

- **New actor event type** → `inc/actor_data.h` (struct + `aet*` enum + kbom mask) + `src/engine/actor_data.cpp` wire validation/conversion + `src/engine/actor.cpp` event-handling switch. If the event embeds `TAG`, add a `*OnFile` mirror struct with a `static_assert` on its size.
- **New chunk type** → declare `kctgFoo` in `inc/soc_core.h`, then add a loader in the appropriate `*.cpp` (engine-core for static data, engine for playback-time state). Update [`../file-formats/chunk-type-reference.md`](../file-formats/chunk-type-reference.md).
- **New scene primitive** → `src/engine/scene.cpp` (and `inc/scene.h`); follow the `SceneSoundEvent` pattern if it embeds variable-length data.
- **New movie save field** → `MovieFilePrefix` is fixed-size 8 B; new fields go into a child chunk under `MVIE`, not into `MovieFilePrefix`. See [`../file-formats/3mm-movie.md`](../file-formats/3mm-movie.md).

## See also

- [`studio.md`](studio.md) — the UI that drives the engine.
- [`kauai-framework.md`](kauai-framework.md) — kauai dependencies.
- [`brender-wrapper.md`](brender-wrapper.md) — the `Body`/`Model`/`Material` 3D layer.
- [`../file-formats/3mm-movie.md`](../file-formats/3mm-movie.md) — what these classes serialise to disk.
