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

### A. Browser hierarchy (studio) — DONE

`BRWT → BrowserText`, `BRWN → BrowserNamedList`, `BRWA → BrowserAction`, `BRWP → BrowserPropActor`, `BRWB → BrowserBackground`, `BRWC → BrowserCamera`, `BRWM → BrowserMusic`, `BRWI → BrowserImportSound`, `BRWR → BrowserRollCall`, `BRCNL → BrowserListContext`, `BCLS → BrowserContentListWithStrings`, `FNET → ThumbnailFileEnumerator`.

### B. Easel / sound recorder — DONE

`ESL → Easel`, `ESLT → EaselText`, `ESLA → EaselActor`, `ESLL → EaselListen`, `ESLR → EaselRecord`, `LSND → ListenerSound`, `SNE → SpletterNameEditor`.

### C. Studio popups, splot, scene sorter — DONE

`MP → MenuPopup`, `MPFNT → MenuPopupFont`, `SPLOT → SplotMachine`, `GOMP → GraphicsObjectMenuPopup` (from earlier passes).

### D. Engine actor/body/template — DONE

`ACLP → ActorClipboard`, `BODY → Body`, `COST → BodyCostume` (couldn't use `Costume` — collides with existing `struct Costume` event payload in actor.h), `ACTN → ActionDefinition` (couldn't use `Action` — collides with existing `struct Action` event payload in actor.h), `TMPL → Template`, `TDF → ThreeDFont`, `TDT → ThreeDText`, `TBXB → TextBoxBase`, `TBXG → TextBoxGobject`, `TCLP → TextBoxClipboard`.

### E. Sound record — DONE

`SREC → SoundRecorder`, `RIFF → RiffWriter` (from earlier passes).

### F. Kidspace gor* — DONE

`GORF → GraphicalObjectRepresentationFill`, `GORB → GraphicalObjectRepresentationBitmap`, `GORT → GraphicalObjectRepresentationTile`, `GORV → GraphicalObjectRepresentationVideo` (from earlier passes).

### G. Text editor stack — DONE

`TRUL → TextRuler`, `EDPAR → EditParameter`, `EDMW → EditControlMultiLineWrap`, `EDSL → EditControlSingleLine`, `EDML → EditControlMultiLine`, `EDPL → EditControlPlain`, `EDCB → EditControlBase`, `TXDD → TextDocumentByteStreamDisplay`, `TXDC → TextDocumentByteStream`.

### H. MIDI subsystem — DONE

`MDWS → MidiStreamCached`, `MSTP → MidiStreamParser`, `MIDS → MidiStream`, `MIDP → MidiPlayer`, `MIDO → MidiOut`, `MPQUE → MidiPlayerQueue`, `MDPS → MidiStreamPlayer`, `MSMIX → MidiStreamMixer`, `MISI → MidiStreamInterface`, `MSQUE → MidiStreamQueue`, `WMS → WindowsMidiStream`, `OMS → OurMidiStream`.

### I. AudioMan — DONE

`STBL → DataBlockStream`, `AMNOT → AudioManNotifySink`, `CAMS → CachedAudioManSound`, `AMQUE → AudioManQueue`, `SDAM → AudioManSoundDevice`, `SNDMQ → SoundManagerQueue`.

### J. Codec — DONE

`BITA → BitArray`, `KCDC → KauaiCodec`, `CODM → CodecManager`, `CODC → Codec`.

### K. Video — DONE

`GVID → GenericVideo`, `GVDS → GenericVideoStream`, `GVDW → GenericVideoWindow` (from earlier passes).

### L. Geometry — `kauai/src/utilint.h` (PT, RC, RAT) — DEFERRED

**HIGHEST RISK.** `PT` and `RC` are everywhere — point and rectangle types used pervasively. Suggested: `Point`, `Rectangle`, `Rational` (for `RAT`). Conflicts with platform headers possible (`Point` clashes with macOS `Point` typedef, `Rectangle` with GDI `Rectangle()` function). Either:
- Defer this one until after Project 1 (64-bit) and during Project 3 (SDL/Mac port) where you'll see the conflicts immediately, OR
- Pick non-clashing names: `KauaiPoint`, `KauaiRectangle`. Less elegant but safer.

**Decision: deferred until Mac port** — don't fight platform header conflicts on speculation.

### M. Stream / spell / random / region — DONE

`BSM`, `SPLC`, `RND`, `SFL`, `REGBL` (from earlier passes).

### N. Controls — DONE

`WSB → WindowSizeBox`, `SCB → ScrollBar`, `CTL → Control`.

### O. Engine residual Hungarian — IN FLIGHT

Types surfaced during the 2026-05-01 sized-types audit that survived earlier passes. Most are in `src/engine/` at file scope; a few are in public headers. Order by blast radius (smallest first):

- `CC → ChidCtgPair` — DONE (commit `c2ef0d1`).
- `TAGF → CachedTag` — DONE (commit `8ad6292`).
- `MACTR → RollCallActorEntry` — DONE (commit `5c3c699`). Note: `kbomMactr` left as-is per the convention that `kbom*` symbols reference the wire format.
- `TBOXH → TextBoxOnFile` — DONE (commit `e46b5ca`).
- `MUNS → MovieSceneUndo` — DONE (commit `1b9be5a`).
- `TUNT/TUNS/TUNH/TUND/TUNC → TextBoxUndo{Type,Size,Hide,Edit,Color}` — DONE (commit `a321645`, bundled — five tightly-related undo subclasses in one file).
- `TFC → BrowserThumbEntry` — DONE (commit `9aed0d1`; struct definition was bundled into `caddc56`).
- `CMG → GokdCnoMap` — DONE (commit `caddc56`).
- `CPS → CelPartSpec` — DONE (commit `b48c24b`).
- `CEL → AnimationCel` — DONE.
- `KEYTT → LexerKeywordEntry` — DONE (commit `9cc8fe0`; sitobren straggler in `b48c24b`).
- `WIG → WindowsAppGlobals` — DONE. Variable `vwig` retained per Hungarian-prefix-in-variable-names convention.

Excluded from this cluster:
- `BCB` (`bren/inc/bren.h:84`) — inside BRender wrapper; treat alongside any future BRender modernization.
- `SMPTE` (`kauai/src/audioman.h`) — industry standard time format; leave alone.
- `BASE` — class name baked into `_PAR` macro convention; leave alone.
- `MBH`, `MBF`, `ADST` (utilmem) — internal allocator types; defer until any utilmem rewrite.

## Skip / defer

- **`ft.cpp` test classes** (`GPRC`, `GFRC`, `TDC`, `DWN`, `TTW`, `RTW`, `DOC`, `DOCP`, `DDP`, `DOCPIC`, `DDPIC`, `DOCGPT`, `DDGPT`, `TAN`, `TED`). These are inside `ft.cpp`/`ut.cpp` test apps — `EXCLUDE_FROM_ALL`, low traffic, low value. Skip until everything else is done.
- **`sitobren.h`** (`S2B`, `S2BLX`). Build is disabled (no SoftImage SDK). Skip.
- **`*.cht` chunk-source files** if a rename target is *only* referenced from `.cht` files, double-check whether it actually IS a C++ type vs. a chunky-file token — chompy syntax sometimes looks similar.

## Sequencing — completed

Order actually used (early small/contained, late pervasive):

1. C (popups/splot/scnsort) — done
2. E (srec) — done
3. M (stream/spell/random/region) — done
4. F (kidspace gor*) — done
5. K (video) — done
6. J (codec) — done
7. H (MIDI) — done
8. I (AudioMan) — done
9. B (easels) — done
10. G (text editor) — done
11. N (controls) — done
12. A (browsers) — done
13. D (actor/body/template) — done
14. **L (PT/RC/RAT) — deferred until Project 3 / Mac port** to surface platform-header conflicts naturally

## Done criteria

- Every cluster's classes renamed; `kcls...` literals preserved
- `cmake --build build` clean across Debug + Release
- `git diff <chomped chunks>` is byte-identical before/after each batch (renames must not affect emitted `.chk` files)
- No new entries in the queue — leftover items have been triaged into "skip" or moved to a later phase

All clusters except L are complete; L is intentionally deferred.
