# Library split

Every kauai/engine/BRender-wrapper library is split into a `*-core`
(CLI / headless / x64-clean) and a gui (`*`) variant. This page
documents what's in each, and why each file lives where it does.

> **Source of truth:** [`CMakeLists.txt`](../../CMakeLists.txt).
> The lists below are mirrored from there — if they drift, CMake wins.

## Why the split exists

CLI tools (`extract-bmdl`, `inspect-chunks`, `geometry-test`,
`codec-test`, `actor-render-test`) want to call the real loaders without
dragging in the entire UI runtime. The `*-core` libs let them link a
focused subset:

- They build cleanly on x64 (no Win32 GUI / GDI deps).
- Their public headers don't pull `frame.h` or any `gob.h`/`gfx.h`
  surface, so callers can use lightweight headers like `soc_core.h`
  in place of `soc.h`.
- They're free of `CommandHandler` / `GraphicsObject` / `Application`
  references — verified per-file by grep when each file is promoted.

The gui (`*`) lib re-exports its `*-core` counterpart `PUBLIC`-ly, so
existing consumers (`studio`, `ched`, `ft`, `ut`, …) link against
`kauai` / `engine` / `brender` exactly as before and see every symbol
they used to.

## kauai-core

```
appb       chcm      chse      chunk     clip      clok      cmd_core
codec      codkauai  crf       ctl       cursor    dlg       docb
file       gfx       gob       groups    groups2   kidhelp   kidspace
kidworld   lex       mbmp      mbmpgui   midi      mssio     pic
region     rtxt      rtxt2     scrcom    scrcomg   screxe    screxeg
sndam      sndm      spell     stream    text      textdoc   util
utilcopy   utilerro  utilglob  utilint   utilmem   utilrnd   utilstr
video
```

Files in `kauai-core` (per `CMakeLists.txt:317`):

`base.cpp`, `chse.cpp`, `chunk.cpp`, `codec.cpp`, `codkauai.cpp`,
`cmd_core.cpp`, `clok.cpp`, `crf.cpp`, `dlg.cpp`, `file.cpp`,
`groups.cpp`, `groups2.cpp`, `lex.cpp`, `mssio.cpp`, `mbmp.cpp`,
`midi.cpp`, `pic.cpp`, `region.cpp`, `scrcom.cpp`, `screxe.cpp`,
`sndm.cpp`, `spell.cpp`, `stream.cpp`, `util.cpp`, `utilcopy.cpp`,
`utilerro.cpp`, `utilglob.cpp`, `utilint.cpp`, `utilmem.cpp`,
`utilrnd.cpp`, `utilstr.cpp`, `stub.cpp`.

Plus Win32 implementations: `filewin.cpp`, `fniwin.cpp`, `memwin.cpp`,
`picwin.cpp`, `regionwin.cpp`.

Plus codegen: `kcdc_386.h`, `kcd2_386.h` on x86 only (kauai codec asm).

Linked deps: `mpr`, `Winmm` (timer only).

## kauai (gui)

Files in `kauai` (per `CMakeLists.txt:423`):

`appb.cpp`, `chcm.cpp`, `clip.cpp`, `cmd_gui.cpp`, `ctl.cpp`,
`cursor.cpp`, `dlg.cpp` *(also in core — see note below)*, `docb.cpp`,
`gfx.cpp`, `gob.cpp`, `kidhelp.cpp`, `kidspace.cpp`, `kidworld.cpp`,
`mbmpgui.cpp`, `mididev.cpp`, `mididev2.cpp`, `rtxt.cpp`, `rtxt2.cpp`,
`scrcomg.cpp`, `screxeg.cpp`, `sndam.cpp`, `text.cpp`, `textdoc.cpp`,
`video.cpp`.

Plus Win32 implementations: `appbwin.cpp`, `dlgwin.cpp`, `gfxwin.cpp`,
`menuwin.cpp`, `gobwin.cpp`. Plus `frame.rc` resources.

Linked deps: `kauai-core`, `3DMMForever::AudioMan`, `Msacm32`,
`Vfw32`, `Winmm`, `mpr`.

> **Note:** `dlg.cpp` appears in both lists in `CMakeLists.txt`. After
> the dlg promotion in commit `467a1c1`, the gui-side entry should be
> removed; until then it harmlessly compiles into both libs (the
> kauai-core copy wins at link time for `kauai`-linked targets through
> the `PUBLIC` re-export). Cleaning this up is a one-line change to
> `CMakeLists.txt` line 431.

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

## engine-core

```
actor      actredit   actrsave   actrsnd    bkgd       body
modl       msnd       mtrl       srec       tagl       tagman
tdf        tdt        tmpl
```

(`CMakeLists.txt:520`) Pulls `kauai-core` + `brender-core`. Public
headers via `inc/`; the headless `actor-render-test` uses `soc_core.h`
in place of `soc.h` to skip the playback-side headers.

> **`msnd.cpp` in engine-core.** `MovieSoundQueue` is a
> `CommandHandler` subclass — only safe in `engine-core` because the
> `cmd_core` split (commits `c3416d2`…`e9b0096`) put `CommandHandler` /
> `CEM` in `kauai-core`. `msnd.cpp`'s only kauai-gui dep is `vpsndm`
> (the `SoundManager` singleton), which gui-linked consumers
> (`studio`, `movie-save-load-test`) supply transitively. The lone
> headless consumer `actor-render-test` never pulls `msnd.o` because
> its call graph (`Template`/`Body`/`Material`) doesn't reference any
> `msnd` symbol.

## engine (playback runtime)

| File          | engine-core | engine |
|---------------|-------------|--------|
| `movie.cpp`   |             | ✓      |
| `scene.cpp`   |             | ✓      |
| `tbox.cpp`    |             | ✓      |

Pulls `engine-core` + `kauai` + `brender`.

## Why playback files are still gui-coupled

`movie`, `scene`, `tbox` need:

- `DocumentBase` (kauai gui) for the document model + close-with-save.
- `GraphicsObject` for view invalidation on edits.
- The kauai modal-pump for stop-rendering / interruption.

A separate plan (`docs/superpowers/plans/`) would split `DocumentBase`
itself into a `kauai-core` data class plus a gui-side DDG/DMD layer —
but that's multi-day. Today the natural floor for one-shot promotions
is reached. See `project_cmd_split_followup.md` for the ongoing audit.

## Headless test targets and what they link

| Target                  | Link line                                      |
|-------------------------|------------------------------------------------|
| `geometry-test`         | `kauai-core`                                   |
| `codec-test`            | `kauai-core`                                   |
| `extract-bmdl`          | `kauai-core`                                   |
| `inspect-chunks`        | `kauai-core`                                   |
| `actor-render-test`     | `engine-core` (which pulls `kauai-core` + `brender-core`) |
| `bren-rasterizer-test`  | `BRender::Libraries`                           |
| `movie-save-load-test`  | `engine` (full gui — currently)                |

## See also

- [`kauai-framework.md`](kauai-framework.md) — what each kauai file does.
- [`engine.md`](engine.md) — what each engine file does.
- [`brender-wrapper.md`](brender-wrapper.md) — BRender wrapper layout.
- `project_cmd_split_followup.md` (project memory) — running notes on
  remaining split candidates.
