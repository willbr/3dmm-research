# Kauai framework (`kauai/src/`)

Kauai is Microsoft's in-house C++ application framework, originally
designed to be cross-platform between Windows and Mac. Its sources are
organised into platform-agnostic `.cpp` files plus per-platform
`*win.cpp` / `*mac.cpp` companions. The full framework still builds on Windows. Portable platform services now have
Windows and POSIX file backends; the old Mac files are historical references.

The library is split into [`kauai-core`](library-split.md#kauai-core)
(CLI/headless) and the full `kauai` (gui). The clusters below note
which side each file lives on.

## Chunky files / streams / I/O

| File              | Side       | Purpose                                                       |
|-------------------|------------|---------------------------------------------------------------|
| `chunk.cpp`       | core       | `ChunkyFile` — chunky DAG container. See [`../file-formats/chunky-files.md`](../file-formats/chunky-files.md). |
| `chse.cpp`        | core       | `ChunkySource` — streaming / paged-in chunk reader.           |
| `crf.cpp`         | core       | `ChunkyResourceFile` / `ChunkyResourceManager` — refcounted, cached chunky access. |
| `codec.cpp`       | core       | `Codec` interface + KauaiCodec encoder.                       |
| `codkauai.cpp`    | core       | KauaiCodec decoder; pulls in the asm fast path on x86 (`kcdc_386.h`, `kcd2_386.h` codegen). |
| `file.cpp`        | core       | `FileObject` cross-platform abstraction. See [`../kauai-reference/file-api.md`](../kauai-reference/file-api.md). |
| `file_native.cpp` | core | Shared file lifecycle, permission restoration and error policy. |
| `filename.cpp` | core | Shared filename navigation, filtering and enumeration. |
| `platform_files_win.cpp` / `platform_files_posix.cpp` | core backend | Native [file/path services](platform-files.md), with no OS types in the interface. |
| `mssio.cpp`       | core       | Microsoft IStream adapter for OLE / GDI+ interop.             |
| `stream.cpp`      | core       | In-memory stream abstraction.                                 |

## Containers

| File              | Side       | Purpose                                                       |
|-------------------|------------|---------------------------------------------------------------|
| `groups.cpp`      | core       | `DynamicArray` (GL), `AllocatedArray` (AL), `GeneralGroup` (GG), `AllocatedGroup` (AG), `StringTable_GST`, `AllocatedStringTable`. See [`../kauai-reference/groups-api.md`](../kauai-reference/groups-api.md). |
| `groups2.cpp`     | core       | Hash tables and sorted variants.                              |

## Memory & utilities

| File              | Side       | Purpose                                                       |
|-------------------|------------|---------------------------------------------------------------|
| `memwin.cpp`      | core (Win) | `HQ` handle alloc / lock; debug heap. See [`../kauai-reference/memory-api.md`](../kauai-reference/memory-api.md). |
| `util.cpp`        | core       | Misc helpers (range / clamp / power-of-two).                  |
| `utilcopy.cpp`    | core       | `CopyPb` / `CopyPv` — bulk byte copies (asm fast path on x86). |
| `utilstr.cpp`     | core       | `String` (Pascal-style 2-byte-length, kauai's string class).  |
| `utilint.cpp`     | core       | Fixed-point arithmetic, integer math.                         |
| `utilrnd.cpp`     | core       | RNG.                                                          |
| `utilmem.cpp`     | core       | Bit-array / nibble helpers.                                   |
| `utilglob.cpp`    | core       | Global symbol / class registry, RTCLASS.                      |
| `utilerro.cpp`    | core       | `ErrorStack`, assert reporting.                               |
| `base.cpp`        | core       | `BASE` root class for refcounted heap objects.                |
| `spell.cpp`       | core       | Spell-check dictionary (no UI).                               |
| `mssio.cpp`       | core       | (listed above)                                                |
| `stub.cpp`        | core       | Stubs for legacy CRT calls absent in modern MSVC.             |

## Graphics & UI

`gfx.h` supplies the SDK-free high-level drawing API and portable value layouts.
Runtime/native adapters include `gfx_port.h` for the native port definition.
`gob.h` is also SDK-free: it exposes the object tree and opaque window interop
tokens; backend implementations own invalidation calls. SDL window services
handle root titles, logical bounds and scaled desktop coordinate conversion.

| File              | Side  | Purpose                                                       |
|-------------------|-------|---------------------------------------------------------------|
| `gob.cpp`         | gui   | `GraphicsObject` — kauai's UI tree node (windows / panes).    |
| `gobwin.cpp`      | gui   | Win32 binding: HWND ↔ `GraphicsObject`.                       |
| `gfx.cpp`         | gui   | `GraphicsPort`, `GraphicsEnvironment` — drawing state.        |
| `gfxwin.cpp`      | gui   | Win32 GDI binding for `GraphicsPort`.                         |
| `region.cpp`      | core  | Geometric `Region` / `RegionScanner` (clipping geometry).     |
| `regionwin.cpp`, `region_native_win.cpp` | gui | HRGN ownership and native region operations. |
| `cursor.cpp`      | gui   | Cursor management.                                            |
| `ctl.cpp`         | gui   | Generic controls (scrollbar, buttons).                        |
| `clip.cpp`        | gui   | Clipboard.                                                    |
| `picture_data.cpp` | core | Fixed-width picture envelope and opaque payload preservation. |
| `pic.cpp`         | gui   | `Picture` native import/rendering wrapper.                    |
| `picwin.cpp`      | gui   | Win32 ENHMETAFILE side of `Picture`.                          |
| `mbmp.cpp`        | core  | `MaskedBitmapMBMP` data side (alloc / RLE codec / chunk I/O / .BMP import). |
| `mbmpgui.cpp`     | gui   | `MaskedBitmapMBMP::Draw` / `DrawMask` (the actual blit).      |
| `video.cpp`       | gui   | Shared video scheduling through the SDK-free host interface.   |
| `avi_movie.cpp`   | core  | Bounded AVI/Video 1 decoding, palette/delta seeks, timing and PCM bytes. |
| `video_sdl.cpp`   | SDL host | Audio-clocked video playback, stream output, gain and lifetime. |
| `video_stream_sdl.cpp` | gui | Original video representation adapter: file loading, canvas drawing and redraw requests. |
| `video_stream_win.cpp` | gui | Private VfW stream decoding and presentation.                |
| `video_window.cpp` | gui  | Native window playback with opaque state.                      |

## MIDI playback

| File | Side | Purpose |
|------|------|---------|
| `midi.cpp` | core | MIDI stream parser and serialization. |
| `mididev2.cpp` | core | Stream cache, queues, priority mixer, seek/resume and expiry; injected device factory. |
| `midi_worker.cpp` | core | Timed worker with coalescing wakes and joined shutdown. |
| `midi_stream_host.h` | core API | Fixed event records and SDK-free borrowed-buffer/device contract. |
| `midi_stream_win.cpp` | gui backend | WinMM output, native headers, callbacks and device volume. |
| `midi_synth.cpp` / `midi_render.cpp` | portable synth | SoundFont instruments and frame-clock event rendering. |
| `midi_sdl.cpp` | SDL backend | PCM output and borrowed-buffer callbacks, without Windows MIDI APIs. |

## Documents & text

| File              | Side  | Purpose                                                       |
|-------------------|-------|---------------------------------------------------------------|
| `docb.cpp`        | gui   | `DocumentBase` + DDG/DMD layer — kauai's MVC document model.  |
| `text.cpp`        | gui   | `TextDocument` data side.                                     |
| `textdoc.cpp`     | gui   | Text-document UI integration.                                 |
| `rtxt.cpp`        | gui   | Rich-text data structures.                                    |
| `rtxt2.cpp`       | gui   | Rich-text rendering.                                          |

## Kidspace (interactive object UI)

| File              | Side  | Purpose                                                       |
|-------------------|-------|---------------------------------------------------------------|
| `kidspace.cpp`    | gui   | `Kidspace` runtime — interactive scriptable objects (the studio's "world inside a window"). |
| `kidworld.cpp`    | gui   | `KidWorld` — kidspace world / stage hierarchy.                |
| `kidhelp.cpp`     | gui   | Kidspace-rendered help system.                                |

## Sound & MIDI

| File              | Side  | Purpose                                                       |
|-------------------|-------|---------------------------------------------------------------|
| `sndm.cpp`        | core  | `SoundManager` + `SoundQueue` — pure scheduling / queueing.   |
| `sndam.cpp`       | gui   | `AudioManSoundDevice` — sampled-audio device using AudioMan.  |
| `midi.cpp`        | core  | `MidiStream` + `MidiStreamParser` — pure SMF byte-stream parser. |
| `mididev.cpp`     | gui   | Win32 `HMIDISTRM` device (primary).                           |
| `mididev2.cpp`    | gui   | Win32 `HMIDISTRM` device (alternate / fallback).              |

## Application bootstrap & command dispatch

`appb.h` exposes the application API without importing a window SDK. Native
Windows globals belong to private `appb_native_win.h`; callback aliases retain
native ABI and are checked against SDK signatures. Main-thread SDL assertions
use the SDL/ImGui modal host; worker/startup and frontend-failure paths retain
the native fallback. The application implementation still uses Windows bootstrap.

| File              | Side  | Purpose                                                       |
|-------------------|-------|---------------------------------------------------------------|
| `appb.cpp`        | gui   | `Application` base class + main loop.                         |
| `appbwin.cpp`     | gui   | Win32 `WinMain` / wndproc bootstrap (`vwig` global).          |
| `cmd_core.cpp`    | core  | `CommandHandler` + bare `CommandExecutionManager`. See `project_cmd_split_followup.md`. |
| `cmd_gui.cpp`     | gui   | `GuiCommandExecutionManager` subclass — mouse / keyboard / modal-gob walk. |
| `clok.cpp`        | core  | `Clock` — `CommandHandler` subclass for timed callbacks.      |
| `dlg.cpp`         | core  | `Dialog` data class (a `GeneralGroup` of dialog items).       |
| `dlgwin.cpp`      | gui   | Win32 modal-dialog spinning + control-message plumbing.       |
| `menuwin.cpp`     | gui   | Win32 menu bar.                                               |

## Embedded scripting (kauai script)

| File              | Side  | Purpose                                                       |
|-------------------|-------|---------------------------------------------------------------|
| `lex.cpp`         | core  | Lexer.                                                        |
| `scrcom.cpp`      | core  | Script compiler.                                              |
| `screxe.cpp`      | core  | Script executor (stack VM).                                   |
| `scrcomg.cpp`     | gui   | gui-aware compiler hooks.                                     |
| `screxeg.cpp`     | gui   | gui-aware executor hooks.                                     |
| `chcm.cpp`        | gui   | Chunky-source compiler — pulls in `GraphicsObjectCompiler` and the cursor parser; used by the `chomp` tool. |

## Mac files (present, not built)

`appbmac.cpp`, `dlgmac.cpp`, `filemac.cpp`, `fnimac.cpp`, `gfxmac.cpp`,
`gobmac.cpp`, `memmac.cpp`, `menumac.cpp`, `picmac.cpp` — original Mac
implementations from the 1995 source. Not in any current target's
sources list. Kept for the future Mac port.

## Test apps

| Target | Source         | Purpose                                          |
|--------|----------------|--------------------------------------------------|
| `ft`   | `ft.cpp`       | Kauai framework test app (gui).                  |
| `ut`   | `ut.cpp`       | Kauai utility test app.                          |
| `test.cpp` | (built into `ft`/`ut`) | Shared test helpers.                  |

## See also

- [`../file-formats/chunky-files.md`](../file-formats/chunky-files.md) — chunky model.
- [`../kauai-reference/`](../kauai-reference/) — full API references for chunk / file / groups / memory.
- [`library-split.md`](library-split.md) — exhaustive list of which file is in which library.
