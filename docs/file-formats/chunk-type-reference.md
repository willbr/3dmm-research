# Chunk type reference

Every chunk in a `.CHK` or `.3MM` is identified by a 4-byte ASCII type
tag. The full list of tags 3DMM uses lives in
[`inc/soc.h`](../../inc/soc.h); kauai itself defines a handful more in
[`kauai/src/chunk.h`](../../kauai/src/chunk.h) and
[`kauai/inc/`](../../kauai/inc/).

Tags are quoted-character constants — e.g. `'MVIE'` is the literal four
bytes `M`, `V`, `I`, `E`. Mnemonics shorter than four characters are
padded with a trailing space (`'SND '`, `'BDS '`, …).

## Movie / scene structure

| Tag    | Constant       | Where it lives                                     | Notes |
|--------|----------------|----------------------------------------------------|-------|
| `SOC ` | `kctgSoc`      | Root creator tag passed to `ChunkyFile::FSave` for `.3MM` files. | Identifies the file's "creator" — analogous to a Mac creator code. |
| `MVIE` | `kctgMvie`     | `Movie::FAutoSave` / `Movie::FLoadAutoSave` (`src/engine/movie.cpp`). | Top-level movie chunk. Payload is `MovieFilePrefix` (8 B). Owns scenes, sounds, source GST. |
| `SCEN` | `kctgScen`     | `Scene::FWrite` / `Scene::FRead` (`src/engine/scene.cpp`).         | One per scene in a movie. Owns actors, sound events, background. |
| `INFO` | `kctgInfo`     | Movie metadata (title, palette, view config).      |       |

## Actors and animation

| Tag    | Constant   | Where it lives                              | Notes |
|--------|------------|---------------------------------------------|-------|
| `ACTR` | `kctgActr` | `Actor::FRead` / `Actor::FWrite` (`src/engine/actor.cpp`, `actrsave.cpp`). | An actor instance: roll-call entry plus event-stream GG. |
| `ACTN` | `kctgActn` | `ActionDefinition` chunk inside `TMPL`.     | A named action (walk, dance, …). |
| `TMPL` | `kctgTmpl` | `Template::PtmplRead` (`src/engine/tmpl.cpp`). | Actor template (model + actions + costumes). |
| `BDS ` | `kctgBds`  | Body-part-set inside `TMPL`.                | Names the costume slots a template exposes. |
| `BPMP` | `kctgBpmp` | Body-part map (per-template).               |       |
| `PATH` | `kctgPath` | Camera or actor motion path.                |       |
| `CAM ` | `kctgCam`  | Camera chunk inside `SCEN` / `BKGD`.        |       |

## 3D rendering

| Tag    | Constant   | Where it lives                              | Notes |
|--------|------------|---------------------------------------------|-------|
| `BMDL` | `kctgBmdl` | `Model::FReadModl` (`src/engine/modl.cpp`). | Vertex + face geometry. See [`bmdl-models.md`](bmdl-models.md). |
| `MTRL` | `kctgMtrl` | `Material::*` (`src/engine/mtrl.cpp`).      | Standard material (palette + shade table). |
| `CMTL` | `kctgCmtl` | Custom material (textured / per-actor).     |       |
| `BKGD` | `kctgBkgd` | `Background::*` (`src/engine/bkgd.cpp`).    | 3D scene background (cameras, lights, sky). |
| `TBOX` | `kctgTbox` | `TextBox::*` (`src/engine/tbox.cpp`).       | 3D text box object placed in a scene. |
| `TDF ` | `kctgTdf`  | `ThreeDTextFont::*` (`src/engine/tdf.cpp`). | 3D-text font definition. |
| `TDT ` | `kctgTdt`  | `ThreeDTextTemplate::*` (`src/engine/tdt.cpp`). | 3D-text template (geometry generated from a font). |
| `PICT` | `kctgPict` | Bitmap / picture (kauai picture chunk).     |       |

## Audio

| Tag    | Constant   | Where it lives                                    | Notes |
|--------|------------|---------------------------------------------------|-------|
| `SND ` | `kctgSnd`  | kauai SoundManager / AudioMan device.             | Sampled-audio chunk (WAV-shaped payload). |
| `MSND` | `kctgMsnd` | `MovieSoundMSND` (`src/engine/msnd.cpp`).         | Movie sound: timeline-aware wrapper around a sampled or MIDI chunk. |

## Group containers (kauai)

These are the on-disk forms of kauai collection types — see
[`../kauai-reference/groups-api.md`](../kauai-reference/groups-api.md).
3DMM uses them to embed lists / tables inside chunks.

| Tag    | Constant     | Container kind                  | Used for |
|--------|--------------|---------------------------------|----------|
| `GLPI` | `kctgGlpi`   | `DynamicArray` (GL)             | Palettes (per-bitmap). |
| `GLBS` | `kctgGlbs`   | `DynamicArray`                  | Body-part sets. |
| `GLTM` | `kctgGltm`   | `DynamicArray`                  | Template metadata. |
| `GLBK` | `kctgGlbk`   | `DynamicArray`                  | Background metadata. |
| `GLCG` | `kctgGlcg`   | `DynamicArray`                  | Cels per group. |
| `GLLT` | `kctgGllt`   | `DynamicArray`                  | Lights. |
| `GLMS` | `kctgGlms`   | `DynamicArray`                  | Motion-match sounds (under `ActionDefinition`). |
| `GLXF` | `kctgGlxf`   | `DynamicArray`                  | Cross-file references. |
| `GGAE` | `kctgGgae`   | `GeneralGroup` (GG)             | Actor-event stream (`Actor::_pggaev`). |
| `GGCM` | `kctgGgcm`   | `GeneralGroup`                  | Custom-material slot table. |
| `GGCL` | `kctgGgcl`   | `GeneralGroup`                  | Cel list. |
| `GGFR` | `kctgFrmGg`  | `GeneralGroup`                  | Frame metadata. |
| `GGST` | `kctgStartGg`| `GeneralGroup`                  | Start-state metadata. |
| `GLDC` | `kctgGldc`   | (obsolete — kept for round-trip)|          |

## Thumbnails (browser)

Each browseable asset class has a paired thumbnail chunk type. Thumbnail
chunks live in `.3TH` files (`kftgThumbDesc`) and reference content
chunks in `.3CN` files (`kftgContent`).

| Tag    | Constant     | What it previews             |
|--------|--------------|------------------------------|
| `THUM` | `kctgThumbMbmp` | Generic thumbnail bitmap (`MBMP`). |
| `BKTH` | `kctgBkth`   | Background.                  |
| `CATH` | `kctgCath`   | Camera.                      |
| `TMTH` | `kctgTmth`   | Template (non-prop).         |
| `PRTH` | `kctgPrth`   | Prop.                        |
| `ANTH` | `kctgAnth`   | Action.                      |
| `SVTH` | `kctgSvth`   | Sound (voice).               |
| `SFTH` | `kctgSfth`   | Sound (FX).                  |
| `SMTH` | `kctgSmth`   | Sound (MIDI).                |
| `MTTH` | `kctgMtth`   | Material.                    |
| `CMTH` | `kctgCmth`   | Custom material.             |
| `TSTH` | `kctgTsth`   | 3D shape.                    |
| `TFTH` | `kctgTfth`   | 3D font.                     |
| `TCTH` | `kctgTcth`   | Text colour.                 |
| `TBTH` | `kctgTbth`   | Text background.             |
| `TZTH` | `kctgTzth`   | Text size.                   |
| `TYTH` | `kctgTyth`   | Text style.                  |

## File-type tags

These are not chunk tags — they're file-type tags used by the file
manager (`Filename::Ftg`) to choose extensions on Windows / type+creator
codes on Mac:

| Constant         | Mac type | Windows extension |
|------------------|----------|--------------------|
| `kftgChunky`     | `chnk`   | `.CHK`             |
| `kftg3mm`        | `3mm `   | `.3MM`             |
| `kftgContent`    | `3con`   | `.3CN`             |
| `kftgThumbDesc`  | `3thd`   | `.3TH`             |
| `kftgSocTemp`    | `3tmp`   | `.3TP`             |

## See also

- [`chunky-files.md`](chunky-files.md) — what a chunk actually is.
- [`3mm-movie.md`](3mm-movie.md) — how the movie tags compose into a `.3MM`.
- [`bmdl-models.md`](bmdl-models.md) — `BMDL` payload layout.
- [`audio-and-midi.md`](audio-and-midi.md) — `SND ` / `MSND` / MIDI.
