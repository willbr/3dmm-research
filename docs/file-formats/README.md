# File formats

Almost everything 3DMM reads or writes is a **chunky file** — a binary
container of typed, optionally-compressed chunks identified by a 4-byte
type tag and a number. The chunky model is defined by kauai and described
in [`chunky-files.md`](chunky-files.md); the user-facing `.3MM` format,
the shipped data files, and most internal asset formats all sit on top
of it.

## At a glance

| Format       | Extension | Loader                                           | Notes                                                              |
|--------------|-----------|--------------------------------------------------|--------------------------------------------------------------------|
| Movie        | `.3MM`    | `Movie::FLoadAutoSave` / `Movie::FAutoSave` (`src/engine/movie.cpp`) | The user document. A chunky file rooted at a `MVIE` chunk.         |
| Chunky data  | `.CHK`    | `ChunkyFile::PcflOpen` (`kauai/src/chunk.cpp`)   | Generic chunky file. The shipped asset bundles are `.CHK`.         |
| Chunky source | `.CHT` / `.CHH` | `chomp.exe` (kauai tool), via `target_chomp_sources()` | Human-readable resource definitions, preprocessed and chomped at build time. |
| 3D model     | `BMDL` chunk inside `.CHK` | `Model::FReadModl` (`inc/modl.h`, `src/engine/modl.cpp`) | Vertices + faces; stored as `ModelOnFile` + arrays.       |
| Material     | `MTRL` / `CMTL` chunks      | `Material::*` (`src/engine/mtrl.cpp`)            | Standard and custom materials.                                     |
| Sound        | `SND ` chunk + `WAV` payload | kauai `SoundManager` / `AudioManSoundDevice`     | Sampled audio. Embedded in `.CHK` or referenced from a `.3MM`.     |
| Movie sound  | `MSND` chunk                | `MovieSoundMSND` (`src/engine/msnd.cpp`)         | Movie-timeline-aware wrapper around a `SND ` payload.              |
| MIDI stream  | (chunk payload)             | `MidiStream` parser (`kauai/src/midi.cpp`)       | Standard MIDI bytes, parsed into a runtime queue.                  |
| Thumbnail manifest | `.3TH`              | `kftgThumbDesc` consumers in `src/studio/`       | Browser thumbnails (per-asset preview chunk types like `BKTH`, `PRTH`, …). |
| Content payload | `.3CN`                   | `kftgContent` consumers                          | Per-asset payload referenced by thumbnail entries.                 |

The `kctg*` constant names live in [`inc/soc.h`](../../inc/soc.h);
[`chunk-type-reference.md`](chunk-type-reference.md) groups them by
subsystem.

## Cross-architecture wire formats

The 1995 `.3MM` format is **load-bearing**: files saved by 3DMMForever
must still load and play in original 1995 3DMM. Most on-disk structs
embed kauai's `TAG`, which holds a runtime `PChunkyResourceFile` pointer —
4 bytes on x86, 8 bytes on x64. Rather than break the wire format, the
codebase uses paired structs:

- `Foo` (runtime, embeds full `TAG`)
- `FooOnFile` (wire format, embeds the fixed-16-byte `TAGOnFile`)

with a marshal step at the I/O boundary. See `inc/tagman.h` for
`TAGOnFile` / `TagFromOnFile`, and the `static_assert(sizeof(...OnFile) == N)`
guards that pin every on-disk size. Examples:

| Runtime type           | Wire type                | Wire size | Defined in                |
|------------------------|--------------------------|-----------|---------------------------|
| `RollCallActorEntry`   | `RollCallActorEntryOnFile` | 28 B    | `src/engine/movie.cpp`    |
| `ActorEvent::Costume`  | `CostumeOnFile`          | 28 B      | `inc/actor.h`             |
| `ActorEvent::Sound`    | `SoundOnFile`            | 44 B      | `inc/actor.h`             |
| `TagChildPair`         | `TagChildPairOnFile`     | 20 B      | `src/engine/scene.cpp`    |
| `Model` (top-of-chunk) | `ModelOnFile`            | 48 B      | `inc/modl.h`              |
| `BrFace` (per-face)    | `BrFaceOnFile`           | 32 B      | `inc/modl.h`              |

## See also

- [`chunky-files.md`](chunky-files.md) — the underlying chunky file model.
- [`chunky-source.md`](chunky-source.md) — `.CHT` / `.CHH` and the chomp build step.
- [`chunk-type-reference.md`](chunk-type-reference.md) — every `kctg*` tag, grouped.
- [`3mm-movie.md`](3mm-movie.md) — the `.3MM` movie document.
- [`bmdl-models.md`](bmdl-models.md) — the BMDL model chunk layout.
- [`audio-and-midi.md`](audio-and-midi.md) — sound and MIDI chunks.
- [`../kauai-reference/chunky-files-api.md`](../kauai-reference/chunky-files-api.md) — the full kauai chunky API reference.
