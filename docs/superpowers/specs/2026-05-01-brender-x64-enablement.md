# BRender x64 enablement spec

**Goal:** With BRender source now in-tree (`bren/lib/`), enable the x64
configure path so the studio builds for Win64 and eventually for LP64
(Linux/macOS x64).

**Prerequisites done (commit `e19fbab`, plan `2026-05-01-brender-source-swap`):**
- BRender 1.1.2 source builds in-tree as `brender_fw` and `brender_zb`
  static libraries (replacing the binary `elib/win[d|s]/br*.lib`).
- Engine on-disk struct widths pinned to explicit-width types
  (sized-types audit, complete).
- AudioMan is a 59-line stub; not an x64 blocker.
- BREN_X86 gating already in `bren/lib/CMakeLists.txt`: when
  `CMAKE_SIZEOF_VOID_P EQUAL 4` is false, the `.asm` files are
  excluded from the source list and only the C fallbacks are compiled.
  Today the brender_zb non-x86 branch is `message(FATAL_ERROR ...)`;
  x64 enablement is exactly the work that lifts that fatal.

## Remaining work for x64

1. **Pin BRender fixed-point types from `long` to `int32_t`.**
   `bren/lib/inc/fixed.h` lines 21-31 typedef `br_fixed_ls`/`br_fixed_lu`
   etc. as `long`/`unsigned long`. On LP64, `long` is 8 bytes -- corrupts
   every fixed-point op since the BR_ONE_LS shift constants assume 32-bit.
   One-line per typedef. All 15.16/16.16 math is intrinsically 32-bit by
   spec. Add `static_assert(sizeof(br_fixed_ls) == 4)` next to the typedef
   block. Also update the parallel typedef in `bren/inc/fixed.h` (the
   wrapper-side header that 3DMM consumes).

2. **Replace MASM .ASM fast paths with C fallbacks on x64.**
   `bren/lib/CMakeLists.txt` already gates `*.asm` behind `BREN_X86`.
   The fallback for `BlockOps` already exists in
   `bren/lib/fallback/blockops.c`. New fallbacks needed for:

   **FW asm (already excluded from non-x86 build):**
   - `fw/fixed386.asm` -- fixed-point arithmetic (mul, div, normalize).
     The C versions exist in `fw/fixed.c` but are guarded by
     `#if !defined(USE_ASM_FIXED)` or similar. Need to verify the C
     fallbacks are actually taken when the asm is dropped.
   - `fw/fxadc386.asm` -- additional fixed-point routines.
   - `fw/memloops.asm` -- segmented-memory pixel get/set. On flat 64-bit
     the segmented variants are no-ops; the C fallback in
     `bren/lib/fallback/blockops.c` style would be ~20 lines.
   - `fw/magicsym.asm` -- exports the build-stamp symbol; not actually
     code, just data. Could be replaced by a C const string.
   - `fw/fnt3x5.asm`, `fw/fntp4x6.asm`, `fw/fntp7x9.asm` -- bitmap font
     glyph tables. Pure data; could be hand-converted to C arrays or
     loaded from external files.

   **ZB asm (today FATAL_ERROR on non-x86):**
   - `zb/mesh386.asm` -- mesh transform inner loop. C fallback in
     `zb/zbmesh.c` may exist; investigate.
   - `zb/safediv.asm`, `zb/sar16.asm` -- math primitives. Trivial
     C ports (a few lines each).
   - `zb/t_piza.asm`, `zb/t_piza2.asm`, `zb/ti8_piz.asm`, `zb/ti8_pizp.asm`,
     `zb/tt15_piz.asm`, `zb/tt24_piz.asm` -- triangle scan-converter
     inner loops. These are the hot path. Either:
       (a) Write C fallbacks. The `awtmz.c`/`l_piz.c`/`p_piz.c` modules
           define `TrapezoidRender*` callbacks that the asm fills in;
           in principle, the C versions in 1.1.2 source are the
           reference. Verify they exist and aren't guarded out.
       (b) Salvage equivalents from BRender 1.3.2's
           `vendor/BRender-v1.3.2-main/drivers/softrend/` (alpha.c,
           depth.c, lighting.c, mapping.c are pure C).
       (c) Rewrite in ml64 -- larger effort.

3. **Audit `__asm` blocks in `bren/lib/inc/g386ifix.h`.**
   The only inline `__asm` in 1.1.2. Already `#ifdef`'d for compilers
   that need it; verify the non-asm path is correct.

4. **The `zb.inc` problem.** The committed `zb.inc` is a snapshot of
   the assembler-friendly version of `zb.h`, regenerated from `zb.h`
   via `h2inc` (Microsoft's header-to-INC tool). It is not portable
   to non-x86 because `h2inc` is x86-only and emits assembler. On
   x64 we don't include `zb.inc` from the .ASM files because the
   .ASM files don't compile -- but if any C file relies on the
   structure layouts in `zb.inc` matching the C structs in `zb.h`,
   that constraint vanishes. Verify zb.inc is consumed only by .ASM.

5. **Engine-side LP64 work outside BRender** -- scope-tracked in
   `docs/superpowers/specs/2026-05-01-sized-types-audit.md`. Kauai
   `sizeof(long)==4` assumption is the long pole.

## Risk

The .ASM scan-converter inner loops are perf-critical. Naive C
fallbacks may make the renderer too slow to use in the studio's
playback view. If so, options are:
- ml64 rewrite (large effort, but matches the original logic)
- SSE2 intrinsics in C (medium effort, still platform-specific)
- Accept slower software render and rely on a future hardware-accelerated
  path (SDL3_GPU / OpenGL / DirectX 12). Modern CPUs are 100-1000x
  faster than 1995-vintage x86, so even an unoptimised C path may
  hit acceptable framerates for 640x480.

## Out of scope for this spec

- SDL3 frontend (separate spec). The renderer side terminates at BRender
  pixmaps; SDL3 enters the picture in `kauai/src/gfxwin.cpp` (becomes
  `gfxsdl.cpp`), translating the pixmap to a `SDL_Texture` via
  `SDL_UpdateTexture`. Independent of BRender.
- LP64 (Linux/macOS x64) -- Win64 first, since the kauai assumption
  work is shared but smaller for LLP64.
- Floating-point variant of BRender. The bren/ wrapper consumes the
  fixed-point variant; switching to BASED_FLOAT would change the wire
  format of brender pixmaps and break parity with original 3DMM movies.

## Suggested ordering

1. (Cheap) Pin `br_fixed_ls` and friends to `int32_t`. Audit fallout
   on x86 build. Target: x86 build still green, LP64 builds get one
   step further.
2. (Cheap) Trivial C ports for `safediv.asm`, `sar16.asm`,
   `magicsym.asm`. Confirm `bren/lib/fallback/` populates.
3. (Medium) Convert `fnt3x5/fntp4x6/fntp7x9.asm` data tables to C arrays.
4. (Medium) Investigate fixed-point C fallbacks in `fixed.c` /
   `fxadc.c` -- determine if usable as-is when asm is dropped.
5. (Hard) Triangle scan-converter C fallbacks. Consider salvaging from
   BRender 1.3.2's `softrend/`. This is the bulk of the work.
6. (Validation) Attempt x64 configure (`-A x64` in cmake), iterate.
7. (Final) Smoke test: BONGO.3MM renders identically (or
   acceptably-similar) on x64 build.
