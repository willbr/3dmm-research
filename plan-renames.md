# Rename modernization plan

## Status

243 rename commits already on branch `c`. Two prior bursts (Aug 2023: 126; Apr-May 2024: 104). The remaining queue is mostly clustered around the parts of Kauai and the studio that the earlier passes didn't reach.

## Constraints

### RTCLASS magic — DO NOT change `kclsXXX` literal values

Every class in this codebase has a 4-byte RTCLASS magic constant `kclsFOO` used at runtime for `FIs(kclsFoo)` checks (see `kauai/src/base.h:22-29`). The convention from base.h itself:

> `kclsFOO` should be `'FOO'` if FOO is at most 4 characters long and should be a 4 character constant which is intended to be unique if the class name is greater than 4 characters. E.g., `kclsGraphicsObjectInterpreter` is `'GraphicsObjectInterpreter'`, but `kclsSTUDIO` is `'stdo'`.

Because the literal is what gets baked into runtime type checks (and may bleed into chunk type tags and on-disk identifiers in some cases), **renaming the C++ class must NOT change the `kcls...` magic value**. Pattern:

```cpp
// Before
#define kclsBRWT 'BRWT'
class BRWT : public BRWT_PAR { RTCLASS_DEC ... };

// After — class renamed; magic literal preserved; macro renamed too
#define kclsBrowserText 'BRWT'
class BrowserText : public BrowserText_PAR { RTCLASS_DEC ... };
```

Always grep for `kcls<OLD>` after renaming the class and update its definition site without changing the literal.

### `.3MM` compatibility

Renames are pure source-level. They don't touch the on-disk format, so they're compat-safe by construction. **But:** chunk type tags (`kctgFOO 'FOO '`) and BOM constants (`kbomFOO`) ARE on-disk values. They look like they fit the rename pattern; they don't. Leave them alone.

## Recipe per rename

Every rename is one commit, message `rename OLD -> New` (matching existing log style). Steps:

1. `grep -rn '\bOLD\b' --include='*.cpp' --include='*.h' --include='*.cht' --include='*.chh'` — get the call sites. Don't forget `.cht`/`.chh` — chunky-source files reference engine types.
2. Rename in declaration site first (`inc/foo.h` or `kauai/src/foo.h`). Update:
   - `class OLD` / `typedef class OLD *POLD;`
   - `OLD_PAR` macro
   - `kclsOLD` → `kclsNew` (keep the `'OLD '` literal value intact)
3. Search-and-replace `OLD` → `New`, `POLD` → `PNew` across the tree (use clang-format-aware tool or vetted sed; double-check word boundaries, especially for short names like `PT`, `RC`).
4. Build (`cmake --build build`) — must compile with zero diff in chomped chunk output.
5. Spot-check that no `kctgOLD`/`kbomOLD`/`ercSocOLD` got swept up.
6. Commit.

## Naming conventions (from prior renames)

Match what the existing pass established:

- **Verbose, no Hungarian**: `Application`, `BrowserList`, `SceneEventType`, `WaitReason`, `MovieSoundMSND`.
- **Pointer typedefs prefixed `P`**: `PMovie`, `PScene` — the `P` survives, only the suffix changes.
- **Discriminated suffixes** when the parent of a hierarchy gets a generic name and children need disambiguation: `SceneUndoChop`, `SceneUndoBackground`, `SceneUndoText`, etc.
- **Ambiguous short names get descriptive expansions**: `WIT` → `WaitReason`, `SEV` → `SceneEvent`, `THD` → `ThumbnailDescriptor`. When the original mnemonic is opaque, look at the class body to figure out what it actually represents before naming.
- **`OnFile` suffix for on-disk struct mirrors**: `SCENH` → `SceneOnFile` (matches `ActorChunkOnFile`, `CursorOnFile`).

## Queue, by cluster

Each cluster is one batch of related commits. Recommend doing one cluster per session — names within a cluster cross-reference each other and refactoring them together avoids intermediate broken builds.

### A. Browser hierarchy (studio) — `inc/browser.h`, `src/studio/browser.cpp`, `src/studio/stdiobrw.cpp`

`BRWT`, `BRWN`, `BRWA`, `BRWP`, `BRWB`, `BRWC`, `BRWM`, `BRWI`, `BRWR`, `BRCNL`, `BCLS`, `FNET`. Already partially modernized — `BRWL → BrowserList`, `BCL → BrowserContentList`, `BRWD → BrowserDisplay`, `BWS → BrowserSelectionFlags`. Finish the family. **Touch count is high (184 occurrences for the BRW* core)** — biggest single cluster, do it on a clean afternoon.

### B. Easel / sound recorder — `inc/esl.h`

`ESL`, `ESLT`, `ESLA`, `ESLL`, `ESLR`, `LSND`, `SNE`. Suggested: `Easel`, `EaselTextureMap`/`EaselTitle`, `EaselActorContent`, `EaselListenSound`, `EaselRecordSound`. Read class bodies to decide T/A/L/R expansions.

### C. Studio popups, splot, scene sorter — `inc/popup.h`, `inc/splot.h`, `inc/scnsort.h`

`MP`, `MPFNT`, `SPLOT`, `GOMP`. Small batch.

### D. Engine actor/body/template — `inc/actor.h`, `inc/body.h`, `inc/tmpl.h`, `inc/tdf.h`, `inc/tdt.h`, `inc/tbox.h`

`ACLP`, `BODY`, `COST`, `ACTN`, `TMPL`, `TDF`, `TDT`, `TBXB`, `TBXG`, `TCLP`. Suggested: `ActorClipboard`, `Body`, `Costume`, `Action`, `Template`, `ThreeDFont`, `ThreeDText`, `TextBoxBase`, `TextBoxGobject`, `TextBoxClipboard`. **Heavy cross-references with engine internals** — budget time.

### E. Sound record — `inc/srec.h`

`SREC`, `RIFF`. Small.

### F. Kidspace gor* — `kauai/src/kidspace.h`

`GORF`, `GORB`, `GORT`, `GORV` (frame, button?, transition?, video?). Read the class bodies — these are subclasses of `GORP` (which has already been renamed to `GraphicalObjectRepresentation`), so suffix the same way: `GraphicalObjectRepresentationFrame`, etc. Verbose but matches the established pattern.

### G. Text editor stack — `kauai/src/text.h`, `kauai/src/textdoc.h`, `kauai/src/rtxt.h`

`EDPAR`, `EDCB`, `EDPL`, `EDSL`, `EDML`, `EDMW`, `TXDC`, `TXDD`, `TRUL`, `CHR`, `CHRD`. Editor parameter, edit control base, plain-text line, single-line, multi-line, multi-line-window, text-doc class, text-doc display, text-rule, character-run-data, etc. Read carefully before naming.

### H. MIDI subsystem — `kauai/src/midi.h`, `mididev.h`, `mididev2.h`, `mdev2pri.h`

`MIDS`, `MSTP`, `MIDP`, `MIDO`, `MPQUE`, `MDPS`, `MSMIX`, `MISI`, `MSQUE`, `WMS`, `OMS`, `MDWS`. Big cluster but tightly scoped — all live within the MIDI files. Likely candidates: `MidiStream`, `MidiStopper`, `MidiPlayer`, `MidiOut`, `MidiPlayerQueue`, `MidiDevicePerformanceState`, etc.

### I. AudioMan — `kauai/src/sndam.h`, `sndampri.h`, `sndm.h`

`SDAM`, `STBL`, `CAMS`, `AMNOT`, `AMQUE`, `SNDMQ`. Soundtrack from AudioMan, sample table base, channel/mixer thing, AudioMan notification, AudioMan queue, sound-manager queue.

### J. Codec — `kauai/src/codec.h`, `codkauai.cpp`

`CODC`, `CODM`, `KCDC`, `BITA`. Codec base, codec manager?, Kauai codec, bit accumulator?

### K. Video — `kauai/src/video.h`

`GVID`, `GVDS`, `GVDW`. Generic video, video stream, video window.

### L. Geometry — `kauai/src/utilint.h` (PT, RC, RAT)

**HIGHEST RISK.** `PT` and `RC` are everywhere — point and rectangle types used pervasively. Suggested: `Point`, `Rectangle`, `Rational` (for `RAT`). Conflicts with platform headers possible (`Point` clashes with macOS `Point` typedef, `Rectangle` with GDI `Rectangle()` function). Either:
- Defer this one until after Project 1 (64-bit) and during Project 3 (SDL/Mac port) where you'll see the conflicts immediately, OR
- Pick non-clashing names: `KauaiPoint`, `KauaiRectangle`. Less elegant but safer.

My recommendation: **defer until Mac port**. Don't fight platform header conflicts on speculation.

### M. Stream / spell / random / region

`BSM` (`stream.h`), `SPLC` (`spell.h`), `RND`, `SFL` (`utilrnd.h`), `REGBL` (`region.cpp`). Small one-offs. Easy fillers between bigger clusters.

### N. Controls — `kauai/src/ctl.h`

`CTL`, `SCB`, `WSB`. Control, scrollbar, window-scroll-bar. Used across the studio UI — touch count similar to browser cluster. Do it as its own batch.

## Skip / defer

- **`ft.cpp` test classes** (`GPRC`, `GFRC`, `TDC`, `DWN`, `TTW`, `RTW`, `DOC`, `DOCP`, `DDP`, `DOCPIC`, `DDPIC`, `DOCGPT`, `DDGPT`, `TAN`, `TED`). These are inside `ft.cpp`/`ut.cpp` test apps — `EXCLUDE_FROM_ALL`, low traffic, low value. Skip until everything else is done.
- **`sitobren.h`** (`S2B`, `S2BLX`). Build is disabled (no SoftImage SDK). Skip.
- **`*.cht` chunk-source files** if a rename target is *only* referenced from `.cht` files, double-check whether it actually IS a C++ type vs. a chunky-file token — chompy syntax sometimes looks similar.

## Sequencing

Do clusters in roughly this order — early ones are isolated, late ones are pervasive:

1. C (popups/splot/scnsort) — small warm-up
2. E (srec) — small
3. M (stream/spell/random/region) — small one-offs
4. F (kidspace gor*) — moderate, contained
5. K (video) — moderate, contained
6. J (codec) — moderate
7. H (MIDI) — bigger but file-local
8. I (AudioMan) — bigger but file-local
9. B (easels) — moderate, more cross-references
10. G (text editor) — bigger, cross-references
11. N (controls) — pervasive across studio
12. A (browsers) — biggest single cluster
13. D (actor/body/template) — pervasive across engine
14. **Defer L (PT/RC/RAT) until Project 3 / Mac port** to surface platform-header conflicts naturally

## Done criteria

- Every cluster's classes renamed; `kcls...` literals preserved
- `cmake --build build` clean across Debug + Release
- `git diff <chomped chunks>` is byte-identical before/after each batch (renames must not affect emitted `.chk` files)
- No new entries in the queue — leftover items have been triaged into "skip" or moved to a later phase
