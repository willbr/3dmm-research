# Audio and MIDI

3DMM has two audio paths: **sampled audio** (WAV-shaped, played through
AudioMan) and **MIDI** (parsed by kauai's `MidiStreamParser`, played
through a Win32 MIDI device). Both flow through the kauai
`SoundManager` so a movie's timeline can mix them.

## Chunk types

| Tag    | Constant   | Payload                                                | Owner                     |
|--------|------------|--------------------------------------------------------|---------------------------|
| `SND ` | `kctgSnd`  | Raw sampled audio (WAV-shaped) or MIDI byte stream.    | kauai SoundManager.       |
| `MSND` | `kctgMsnd` | `MovieSoundMSND` header + child `SND ` references.     | `src/engine/msnd.cpp`.    |

`SND ` chunks come in via the build (chomped from `.cht` `WAVE_CHUNK`
macros, packaged into the shipped `.CHK` files) or via user recording
(written into the autosave document, referenced with a
`ksidUseCrf` `TAG`).

## `MSND` — movie sound

A `MovieSoundMSND` ([`src/engine/msnd.cpp`](../../src/engine/msnd.cpp))
is a movie-timeline-aware wrapper around a `SND ` chunk. It carries:

- The sound's volume, loop flag, queue flag.
- A sound type (`sty`): voice / FX / MIDI / midi-from-instrument.
- A reference (`TAG`) to the underlying `SND ` chunk (which may live in
  the same movie, in a shipped `.CHK`, or in a user CD source).
- For looped queue sounds, a `MovieSoundQueue` runtime helper that walks
  the queue and arms the SoundManager.

`MSND` chunks are referenced from:

- **Scene sound events** (`SceneSoundEvent` in `src/engine/scene.cpp`):
  one or more `(chid, TAGOnFile)` pairs (`TagChildPairOnFile`, 20 B
  each) following the SSE header. Triggered when the playhead reaches
  the event's frame.
- **Actor sound events** (`ActorEvent::Sound` in `inc/actor.h`):
  per-actor sound triggered by an action or position change. The wire
  form is `SoundOnFile` (44 B fixed).
- **Templates** (`TMPL`'s `GLMS` motion-match-sounds child): action-
  dependent sounds that fire when an actor performs a particular action.

## `SND ` chunk payload

For sampled audio the chunk body is essentially a WAV file's data block
(no RIFF header — the metadata that would be in a WAV's `fmt ` chunk is
stored elsewhere in the chunky file or implied by the source). Loaded
into an AudioMan buffer by `AudioManSoundDevice`
([`kauai/src/sndam.cpp`](../../kauai/src/sndam.cpp)).

For MIDI the chunk body is a Standard MIDI File (SMF) byte stream. The
parser is [`MidiStream` / `MidiStreamParser`](../../kauai/src/midi.cpp);
it emits a runtime queue of `(delta_time, status, data…)` events that
the device layer (`mididev.cpp` / `mididev2.cpp`) feeds to the Win32
HMIDISTRM API.

## Runtime path

```
            chunk file
                |
                v
        +-------+--------+
        | SND  / MSND    |
        +----------------+
                |
                v
+-----------------------------+
|  TagManager::PbacoFetch     |   <- resolves (sid, ctg, cno) -> in-memory object
+-----------------------------+
                |
                v
+-----------------------------+
|  MovieSoundMSND             |   <- if MSND
|  (or raw SoundFromChunk)    |
+-----------------------------+
                |
                v
+-----------------------------+
|  SoundManager (kauai-core)  |   <- adds to a SoundQueue
+-----------------------------+
                |
                v
+-----------------------------+
|  SoundDevice                |   <- AudioManSoundDevice (sampled),
|  (gui-side)                 |      mididev / mididev2 (MIDI)
+-----------------------------+
                |
                v
              audio
```

`SoundManager` lives in `kauai-core`
([`kauai/src/sndm.cpp`](../../kauai/src/sndm.cpp)) — it's pure queueing
+ scheduling, no Win32. The actual device implementations
(`AudioManSoundDevice`, `MidiDeviceMM`, `MidiDevice2`) live in the gui-
side `kauai` lib because they pull in Win32 multimedia APIs.

## Cross-architecture wire shape

`SND ` payload is plain bytes — no struct embeds, no architecture
sensitivity. `MSND` chunks carry `TAGOnFile`s (16 B fixed) for
references to child `SND ` chunks. `SoundOnFile` (the actor-event wire
form) is 44 bytes fixed. See the table in
[`README.md`](README.md#cross-architecture-wire-formats).

## See also

- [`chunky-files.md`](chunky-files.md) — chunky model.
- [`3mm-movie.md`](3mm-movie.md) — where `MSND` chunks attach in a `.3MM`.
- [`chunk-type-reference.md`](chunk-type-reference.md) — `SND `, `MSND`,
  thumbnail tags `SVTH` / `SFTH` / `SMTH`.
- [`../code-map/kauai-framework.md`](../code-map/kauai-framework.md) —
  where `SoundManager`, `MidiStream`, and the device implementations live.
