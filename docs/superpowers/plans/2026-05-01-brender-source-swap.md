# BRender source swap (x86 parity, foundation for x64) — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the binary `elib/win[d|s]/br*.lib` blobs (Argonaut BRender ~1995, x86-only) with an in-tree build of the open-source BRender 1.1.2 sources from `vendor/BRender-v1.1.2-main/`. Land x86 first with byte-identical render output, then unblock the x64 port.

**Architecture:** Add CMake targets `brender_fw`, `brender_fmm`, `brender_zb` that compile the vendored sources directly. Rewrite `cmake/FindBRender.cmake` to point `BRender::Libraries` at these in-tree targets instead of the `elib/` static libs. Existing `bren/` wrapper (the wraparound C++ that sits between kauai and BRender) is unchanged. The `.ASM` fast-paths are gated behind `IN_80386` and replaced by C fallbacks on non-x86 — same pattern kauai already uses.

**Tech Stack:** CMake (Ninja generator), MSVC x86 (current) and MSVC x64 (target), MASM (`ml.exe`) for the asm fast-paths on x86, plain C99 for the fallbacks.

**Scope of *this* plan:** x86-only source build with parity test. Step 4 of the parent roadmap (`vendor/BRender-v1.1.2-main` → linkable `BRender::Libraries`). x64 enablement is the follow-on plan; SDL3 frontend swap is the plan after that.

**Plan tracker file:** `docs/superpowers/specs/2026-05-01-sized-types-audit.md` covers the sized-types side of the 64-bit work — not this plan's concern. This plan is the BRender swap-in.

---

## File Structure

**New (vendor sources, copy or move from `vendor/BRender-v1.1.2-main/`):**
- `bren/lib/inc/` — public BRender headers (currently `vendor/.../INC/*.H`, all 41 files), lowercased filenames to match `bren/inc/` style
- `bren/lib/fw/` — framework C sources (currently `vendor/.../FW/*.C` + `*.ASM`), ~50 files
- `bren/lib/fmm/` — fixed-math C sources. In 1.1.2 these aren't a separate dir — they're part of FW (`FIXED.C`, `FIXED386.ASM`, `MATRIX*.C`, `SCALAR.C`, `VECTOR.C`, `QUAT.C`). Either pull them out into `fmm/` per the lib split or keep all in `fw/` and link as one combined lib. **Decision:** keep as one `brender_fw` library; the historical 3-way split (BRFMMXR/BRFWMXR/BRZBMXR) is a packaging artifact, not a clean architectural boundary.
- `bren/lib/zb/` — Z-buffered renderer sources (currently `vendor/.../ZB/*.C` + `*.ASM`), ~30 files
- `bren/lib/fallback/blockops.c` — new file, C fallbacks for `BrBlockFill`/`BrBlockCopy`/`BrFarBlockCopy` so the framework builds without `BLOCKOPS.ASM` on non-x86

**Modified:**
- `CMakeLists.txt` — replace `find_package(BRender REQUIRED)` consumer logic so it adds the new targets; add `add_subdirectory(bren/lib)` (or inline definitions); the existing `add_library(brender)` block at line 303 stays exactly as-is — it builds the wrapper and links against `BRender::Libraries`, which now resolves to the in-tree targets.
- `cmake/FindBRender.cmake` — replace with an exporter that aliases the in-tree CMake targets as `BRender::BRFMMXR`, `BRender::BRFWMXR`, `BRender::BRZBMXR`, `BRender::Libraries` so no consumer code needs to change.
- `.gitignore` — keep `vendor/BRender-v1.1.2-main/` (or the chosen vendor location) tracked, OR adopt a "vendored sources are gitignored, hydrated from upstream" pattern. **Decision: track in-tree** — these are MIT-licensed and the project benefits from build determinism.
- `CLAUDE.md` — add a paragraph about the vendored BRender location, replace the "BRender static libs in elib/" note.
- `elib/wind/`, `elib/wins/` — **deleted at the end of the plan** once parity is proven (separate cleanup task, last commit).

**New (build glue):**
- `bren/lib/CMakeLists.txt` — defines `brender_fw`, `brender_zb` static libs, links them, plumbs include dirs.

**Files NOT touched in this plan:**
- `bren/*.cpp`, `bren/*.c` (wrapper), `bren/inc/*.h` (wrapper headers) — the wrapper consumes the public BRender API, which is unchanged between binary and source builds.
- `inc/`, `src/engine/`, `src/studio/` — engine and app code. They consume BRender via `bren/`, never directly.

---

## Task 1: Vendor source layout and gitignore

**Files:**
- Create: `bren/lib/` directory tree by *copying* `vendor/BRender-v1.1.2-main/{INC,FW,STD,ZB,FMT}` into `bren/lib/{inc,fw,std,zb,fmt}` with lowercased filenames.
- Modify: `.gitignore` — confirm vendor/ is not gitignored. If it is, remove that entry.
- Modify: `CLAUDE.md` (the "Things that are intentionally broken / missing" section needs an update).

- [ ] **Step 1: Inspect current .gitignore for vendor/ or BRender entries**

```bash
grep -i -E "vendor|brender|elib" .gitignore || echo "no entries"
```

Expected: either no entries, or only entries unrelated to vendor/. Note any matches.

- [ ] **Step 2: Copy and lowercase the vendor sources into bren/lib/**

```bash
mkdir -p bren/lib/inc bren/lib/fw bren/lib/std bren/lib/zb bren/lib/fmt bren/lib/fallback
for src in vendor/BRender-v1.1.2-main/INC/*.H; do
  base=$(basename "$src" .H | tr '[:upper:]' '[:lower:]')
  cp "$src" "bren/lib/inc/${base}.h"
done
for dir in FW STD ZB FMT; do
  lc=$(echo "$dir" | tr '[:upper:]' '[:lower:]')
  for src in vendor/BRender-v1.1.2-main/${dir}/*.C; do
    base=$(basename "$src" .C | tr '[:upper:]' '[:lower:]')
    cp "$src" "bren/lib/${lc}/${base}.c"
  done
  for src in vendor/BRender-v1.1.2-main/${dir}/*.ASM; do
    [ -e "$src" ] || continue
    base=$(basename "$src" .ASM | tr '[:upper:]' '[:lower:]')
    cp "$src" "bren/lib/${lc}/${base}.asm"
  done
  # Also copy any .h, .inc, .fns, .lst that the .c files include
  for ext in h H inc INC fns FNS lst LST; do
    for src in vendor/BRender-v1.1.2-main/${dir}/*.${ext}; do
      [ -e "$src" ] || continue
      base=$(basename "$src" | tr '[:upper:]' '[:lower:]')
      cp "$src" "bren/lib/${lc}/${base}"
    done
  done
done
```

- [ ] **Step 3: Verify file counts match expectations**

```bash
echo "inc:"; ls bren/lib/inc/*.h | wc -l
echo "fw:"; ls bren/lib/fw/*.c | wc -l ; ls bren/lib/fw/*.asm | wc -l
echo "zb:"; ls bren/lib/zb/*.c | wc -l ; ls bren/lib/zb/*.asm | wc -l
echo "fmt:"; ls bren/lib/fmt/*.c | wc -l
echo "std:"; ls bren/lib/std/*.c | wc -l
```

Expected: inc 41, fw ~50 .c + ~9 .asm, zb ~16 .c + ~14 .asm, fmt 8 .c, std 4 .c. Exact numbers may vary — flag any zero count as a wildcard glob bug.

- [ ] **Step 4: Update CLAUDE.md to reflect the source-built BRender**

Edit the "Three static libraries feed the studio executable" section at `CLAUDE.md` to mention `bren/lib/` as the BRender source location instead of `elib/wind|wins/bren.lib`. Replace the bullet:

```
- **`brender`** (`bren/`) — wrapper over the BRender 3D engine (the BRender static libs themselves live in `elib/wind|wins/bren.lib` and are pulled via `find_package(BRender)`).
```

with:

```
- **`brender`** (`bren/`) — wrapper over the BRender 3D engine. The BRender source itself lives vendored under `bren/lib/` (Argonaut BRender 1.1.2, MIT-licensed via the foone/Hanselman 2022 release). It builds as the `brender_fw` and `brender_zb` static libraries, exposed as `BRender::Libraries` for backward compatibility with the original `elib/`-based build.
```

- [ ] **Step 5: Commit the vendored sources**

```bash
git add bren/lib/ CLAUDE.md
git commit -m "$(cat <<'EOF'
brender: vendor BRender 1.1.2 source under bren/lib/

Foone/Hanselman 2022 MIT-released Argonaut BRender 1.1.2,
copied from vendor/BRender-v1.1.2-main/ with paths and
filenames lowercased. No code changes -- this is the input
for the source-build swap that replaces the x86-only
elib/win[d|s]/br*.lib binaries.

EOF
)"
```

---

## Task 2: Define brender_fw CMake target and build it standalone

The framework lib (FW directory) is the bulk of BRender — math, file IO, pixmap, registry, etc. Get it compiling first so we have a foundation. The renderer (ZB) and the wrapper (`bren/`) depend on it.

**Files:**
- Create: `bren/lib/CMakeLists.txt`
- Modify: `CMakeLists.txt` (top-level — add `add_subdirectory(bren/lib)` near the existing brender target)

- [ ] **Step 1: Create bren/lib/CMakeLists.txt with brender_fw target**

`bren/lib/CMakeLists.txt`:

```cmake
# Vendored BRender 1.1.2 sources (Argonaut Software, MIT-released 2022).
# These replace the binary elib/win[d|s]/br*.lib blobs. Keeping the historical
# names BRender::BRFMMXR / BRender::BRFWMXR / BRender::BRZBMXR for consumer
# compat, but they are aliases over a single combined target.

# IN_80386 already gates inline x86 fast paths in kauai. BRender uses the same
# convention here: enable .ASM fast paths only on x86, fall back to C otherwise.
set(BREN_X86 FALSE)
if (CMAKE_SIZEOF_VOID_P EQUAL 4)
  set(BREN_X86 TRUE)
  enable_language(ASM_MASM)
endif()

# --- brender_fw: framework (math, file IO, registry, pixmap support) ---

file(GLOB BRENDER_FW_C
  "${CMAKE_CURRENT_SOURCE_DIR}/fw/*.c"
)

add_library(brender_fw STATIC ${BRENDER_FW_C})
target_include_directories(brender_fw
  PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/inc"
  PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/fw"
)

if (BREN_X86)
  file(GLOB BRENDER_FW_ASM "${CMAKE_CURRENT_SOURCE_DIR}/fw/*.asm")
  target_sources(brender_fw PRIVATE ${BRENDER_FW_ASM})
else()
  # C fallbacks for the .ASM functions, used on non-x86.
  target_sources(brender_fw PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/fallback/blockops.c"
  )
endif()

# BRender uses long-style typedefs internally and was written assuming
# /Zc:strictStrings- semantics. The top-level CMakeLists.txt already enables
# /permissive- + /Zc:strictStrings-; nothing extra needed here.

# Quiet down legacy-warning noise that would otherwise drown out real errors.
if (MSVC)
  target_compile_options(brender_fw PRIVATE
    /wd4047  # different levels of indirection
    /wd4133  # incompatible types
    /wd4244  # conversion, possible loss of data
    /wd4267  # size_t -> smaller, possible loss of data
    /wd4311  # pointer truncation
    /wd4312  # conversion to greater size
  )
endif()
```

Top-level `CMakeLists.txt`: insert *after* the existing `find_package(BRender REQUIRED)` line (line 24):

```cmake
add_subdirectory(bren/lib)
```

(`find_package(BRender REQUIRED)` will be replaced in Task 5; for now it still resolves to the elib/ binaries, and the new `brender_fw` target builds in parallel without affecting the link.)

- [ ] **Step 2: Create the blockops.c C fallback**

`bren/lib/fallback/blockops.c`:

```c
/*
 * C fallbacks for the BLOCKOPS.ASM fast-path routines.
 * Used on non-x86 builds where the MASM file cannot be assembled.
 *
 * The .ASM versions use `rep stosd`/`rep movsd` -- DWORD-stride.
 * These C versions are equivalent for DWORD-aligned destinations,
 * which is what BRender's pixmap and mesh code always provides.
 */

#include <stdint.h>
#include <string.h>

void BrBlockFill(void *dest_ptr, int value, int dwords)
{
    uint32_t *dest = (uint32_t *)dest_ptr;
    int i;
    for (i = 0; i < dwords; i++) {
        dest[i] = (uint32_t)value;
    }
}

void BrBlockCopy(void *dest_ptr, void *src_ptr, int dwords)
{
    memcpy(dest_ptr, src_ptr, (size_t)dwords * 4);
}

void BrFarBlockCopy(void *dest_ptr, void *src_ptr, int dwords)
{
    /* No segmented memory on flat 32/64-bit targets -- same as BrBlockCopy. */
    memcpy(dest_ptr, src_ptr, (size_t)dwords * 4);
}
```

- [ ] **Step 3: Configure and try to build brender_fw alone**

```bash
cmake --preset x86:msvc:debug
cmake --build build --target brender_fw 2>&1 | tail -60
```

Expected: many compile errors on first try. The 1995 BRender sources predate `<windows.h>` interactions, predate `_CRT_SECURE_NO_WARNINGS`, use K&R-style declarations, etc. Capture the *first* error and proceed to Step 4.

- [ ] **Step 4: Triage and fix first compile failure**

Common categories and how to handle each:
- **Missing header:** check whether the header is in `bren/lib/inc/` (top-level public) or in another vendor subdir we didn't copy. Add to glob or copy missing files.
- **Conflicting Windows symbol:** define `WIN32_LEAN_AND_MEAN` and/or `NOMINMAX` before any windows.h include. Add to `target_compile_definitions(brender_fw PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)`.
- **`int` vs `long`:** the binary `elib/` build was compiled with the same source — error means a header isn't being seen the same way. Add include path or a `#define` that the original Watcom/MSVC makefile passed.
- **Missing fw_p.h-style internal protos:** check `vendor/BRender-v1.1.2-main/FW/MAKEFILE` for `-D` flags and `INCLUDE` paths. The original makefile is the source of truth for what the build expected.

Read original makefile:

```bash
cat vendor/BRender-v1.1.2-main/FW/MAKEFILE | head -80
cat vendor/BRender-v1.1.2-main/MAKEDEFS.MSC
```

Apply the smallest fix that resolves the first error, repeat. Each round should remove a category of errors; if the error count *grows* after a fix, revert and reconsider.

- [ ] **Step 5: Repeat Step 4 until brender_fw compiles**

```bash
cmake --build build --target brender_fw 2>&1 | grep -E "error|Error" | head -20
```

Stop condition: zero `error` lines. Warnings are allowed at this stage.

- [ ] **Step 6: Commit each meaningful triage step separately as you make progress**

```bash
git add CMakeLists.txt bren/lib/CMakeLists.txt bren/lib/fallback/
git commit -m "brender: define brender_fw target, get it compiling"
```

If the triage took multiple commits, that's fine — each commit message should describe what category of issue it fixed (e.g. "brender_fw: define WIN32_LEAN_AND_MEAN to resolve windows.h symbol conflicts").

---

## Task 3: Define brender_zb CMake target

The Z-buffered renderer. Depends on brender_fw.

**Files:**
- Modify: `bren/lib/CMakeLists.txt` — add brender_zb target

- [ ] **Step 1: Append brender_zb to bren/lib/CMakeLists.txt**

```cmake
# --- brender_zb: Z-buffered software renderer ---

file(GLOB BRENDER_ZB_C "${CMAKE_CURRENT_SOURCE_DIR}/zb/*.c")

add_library(brender_zb STATIC ${BRENDER_ZB_C})
target_link_libraries(brender_zb PUBLIC brender_fw)
target_include_directories(brender_zb
  PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/inc"
  PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/zb"
    "${CMAKE_CURRENT_SOURCE_DIR}/fw"
)

if (BREN_X86)
  file(GLOB BRENDER_ZB_ASM "${CMAKE_CURRENT_SOURCE_DIR}/zb/*.asm")
  target_sources(brender_zb PRIVATE ${BRENDER_ZB_ASM})
else()
  # ZB asm fast paths cover the inner triangle scan-converters
  # (TI8_PIZ, TT15_PIZ, T_PIZA et al). Per-arch fallbacks belong in
  # the x64 enablement plan; for x86-only this branch is unreachable.
  message(FATAL_ERROR
    "brender_zb x64 path not yet implemented -- see follow-on x64 plan")
endif()

if (MSVC)
  target_compile_options(brender_zb PRIVATE
    /wd4047 /wd4133 /wd4244 /wd4267 /wd4311 /wd4312
  )
endif()
```

- [ ] **Step 2: Build brender_zb**

```bash
cmake --build build --target brender_zb 2>&1 | tail -40
```

- [ ] **Step 3: Triage compile errors as in Task 2 step 4-5**

Expected error categories: same as brender_fw, plus possibly missing `zbproto.h` private headers — check `vendor/BRender-v1.1.2-main/ZB/MAKEFILE` for include paths.

- [ ] **Step 4: Commit**

```bash
git add bren/lib/CMakeLists.txt
git commit -m "brender: add brender_zb target (Z-buffered renderer)"
```

---

## Task 4: Rewrite cmake/FindBRender.cmake to use in-tree targets

The wrapper (`bren/bwld.cpp` etc.) and the engine link against `BRender::Libraries`. We need `FindBRender.cmake` to resolve that to the new in-tree targets instead of `elib/`.

**Files:**
- Modify: `cmake/FindBRender.cmake`

- [ ] **Step 1: Replace cmake/FindBRender.cmake with in-tree target wiring**

```cmake
# Resolves BRender::Libraries to the in-tree brender_fw + brender_zb targets
# defined by bren/lib/CMakeLists.txt. The historical 3-way split (BRFMMXR,
# BRFWMXR, BRZBMXR) is preserved as aliases for any consumer that names them
# directly, but they all map to the combined target set.

include(FindPackageHandleStandardArgs)

if (NOT TARGET brender_fw OR NOT TARGET brender_zb)
  message(FATAL_ERROR
    "FindBRender.cmake expects the in-tree brender_fw / brender_zb targets. "
    "Make sure add_subdirectory(bren/lib) runs before find_package(BRender).")
endif()

if (NOT TARGET BRender::Libraries)
  add_library(BRender::Libraries INTERFACE IMPORTED)
  target_link_libraries(BRender::Libraries INTERFACE brender_fw brender_zb)

  # Compat aliases for any consumer that linked to the old per-lib names.
  add_library(BRender::BRFMMXR ALIAS brender_fw)  # fixed-math is part of fw in 1.1.2
  add_library(BRender::BRFWMXR ALIAS brender_fw)
  add_library(BRender::BRZBMXR ALIAS brender_zb)

  if (MSVC)
    # Original elib/ libs needed legacy_stdio_definitions; in-tree builds with
    # modern UCRT do not. The /SAFESEH:NO link option from the old find module
    # was a precompiled-blob limitation -- not needed for source builds.
  endif()
endif()

set(${CMAKE_FIND_PACKAGE_NAME}_FOUND TRUE)
find_package_handle_standard_args(${CMAKE_FIND_PACKAGE_NAME}
  REQUIRED_VARS ${CMAKE_FIND_PACKAGE_NAME}_FOUND)
```

- [ ] **Step 2: Reorder top-level CMakeLists.txt so add_subdirectory runs before find_package**

Edit `CMakeLists.txt` around the existing line 24. Move the `add_subdirectory(bren/lib)` from Task 2 to be *before* `find_package(BRender REQUIRED)`:

```cmake
find_package(AudioMan)
add_subdirectory(bren/lib)
find_package(BRender REQUIRED)
```

- [ ] **Step 3: Reconfigure to confirm BRender::Libraries now resolves to in-tree targets**

```bash
cmake --preset x86:msvc:debug 2>&1 | tail -30
```

Expected: configure succeeds with no `find_library` lines mentioning `elib/`. `BRender::Libraries` now points at `brender_fw` + `brender_zb`.

- [ ] **Step 4: Build the full studio target**

```bash
cmake --build build --target studio 2>&1 | tail -40
```

Expected: compiles and links. The `bren/` wrapper sources (`bwld.cpp` etc.) consume the same public headers from `bren/inc/` they always did; the underlying definitions now come from `bren/lib/` instead of `elib/`.

- [ ] **Step 5: Triage any link errors**

Most likely categories:
- **Undefined symbol:** a function the wrapper calls that wasn't compiled into `brender_fw` because it lived in a vendor file we didn't copy. Find which `vendor/BRender-v1.1.2-main/` file defines it; add to `bren/lib/`.
- **Multiple definition:** a function defined in both the C source AND its .ASM counterpart on the x86 path. Check the original FW makefile to see which file the historical build used. Drop the duplicate.
- **Wrong calling convention (`__cdecl` vs `BR_ASM_CALL`):** the .ASM files declare functions with a specific calling convention; the C consumers expect it. Verify the macro `BR_ASM_CALL` resolves to `__stdcall` or `__cdecl` correctly in the vendored `compiler.h`.

- [ ] **Step 6: Commit**

```bash
git add cmake/FindBRender.cmake CMakeLists.txt
git commit -m "$(cat <<'EOF'
brender: route BRender::Libraries to in-tree brender_fw/brender_zb

cmake/FindBRender.cmake now wires BRender::Libraries to the source-built
targets defined by bren/lib/CMakeLists.txt instead of looking for the
binary blobs in elib/win[d|s]/. The historical per-lib aliases
(BRender::BRFMMXR, BRender::BRFWMXR, BRender::BRZBMXR) are preserved
so the bren/ wrapper and engine targets need no edits.

elib/ binaries are still on disk -- they will be deleted in a separate
commit once the parity smoke test in Task 6 passes.

EOF
)"
```

---

## Task 5: Parity smoke test against the binary build

The 1.1.2 source we vendored is *close to* but not byte-identical to whatever specific Argonaut build produced the elib blobs. Confirm that engine output is render-equivalent before deleting the binaries.

**Files:**
- Create: `tests/brender_parity_test.md` — a short checklist for the human running the smoke test, since render output isn't trivially diff-able.

- [ ] **Step 1: Build the source-based studio.exe**

```bash
cmake --build build --target studio
```

Verify build succeeds. Note the output path (`build/3dmovie.exe` per CLAUDE.md).

- [ ] **Step 2: Run 3dmovie.exe and load BONGO.3MM (or any sample movie)**

Manual: Launch `build/3dmovie.exe`, open a known sample movie, advance through frames. The user (William) is the operator here — the test cannot be fully automated because there's no BRender output capture path yet.

Look for: actors render correctly, materials look right, lighting is right, camera transitions don't tear, no crashes on scene load/play/edit.

- [ ] **Step 3: If parity fails, snapshot the broken render**

Use the existing Ctrl+Shift+F10 hi-res viewport screenshot hotkey (added in commit `e96b1c8`) to capture both the new (source-built) render and a known-good x86 binary-built render of the same frame. Diff manually. Report any pixel differences as part of the next triage commit.

- [ ] **Step 4: If parity holds, delete elib/ binaries**

```bash
git rm -r elib/wind elib/wins
git commit -m "$(cat <<'EOF'
brender: drop elib/win[d|s]/br*.lib in favour of in-tree source build

The binary BRender 1.1.2-vintage static libs are no longer linked --
bren/lib/ provides the same code from MIT-licensed Argonaut sources.
Removing them shrinks the repo and removes the x86-only blocker
holding back the x64 port.

If parity ever needs to be re-checked against the original blobs,
they remain in the foone/Hanselman 2022 BRender release on GitHub.

EOF
)"
```

If `elib/` contains other libraries that aren't BRender (check first with `ls elib/`), only delete the BRender ones.

- [ ] **Step 5: Update CLAUDE.md "Things that are intentionally broken" section**

Remove the BRender-related notes that referenced `elib/`, since the build no longer depends on those binaries.

```bash
git add CLAUDE.md
git commit -m "docs: drop elib BRender note now that source build is the default"
```

---

## Task 6: Document the follow-on x64 enablement plan as a separate spec

**Files:**
- Create: `docs/superpowers/specs/2026-05-XX-brender-x64-enablement.md`

This is a *spec*, not a plan — it captures what's needed to take the source build x64. The plan itself comes later.

- [ ] **Step 1: Write the spec**

`docs/superpowers/specs/2026-05-01-brender-x64-enablement.md`:

```markdown
# BRender x64 enablement spec

**Goal:** With BRender source now in-tree (`bren/lib/`), enable the x64
configure path so the studio builds for Win64 and eventually for LP64
(Linux/macOS x64).

**Prerequisites done:**
- BRender source builds in-tree as `brender_fw` and `brender_zb` static
  libraries (this plan, 2026-05-01-brender-source-swap).
- Engine on-disk struct widths pinned to explicit-width types
  (sized-types audit, complete).
- AudioMan is a 59-line stub; not an x64 blocker.

**Remaining work for x64:**

1. **Pin BRender fixed-point types from `long` to `int32_t`.**
   `bren/lib/inc/fixed.h` lines 21-31 typedef `br_fixed_ls`/`br_fixed_lu`
   etc. as `long`/`unsigned long`. On LP64, `long` is 8 bytes -- corrupts
   every fixed-point op. One-line per typedef. All 15.16/16.16 math is
   intrinsically 32-bit by spec. Add `static_assert(sizeof(br_fixed_ls) == 4)`.

2. **Replace MASM .ASM fast paths with C fallbacks on x64.**
   `bren/lib/CMakeLists.txt` already gates `*.asm` behind `BREN_X86`
   (added in this plan). The fallback for `BlockOps` already exists
   (`bren/lib/fallback/blockops.c`). New fallbacks needed for:
   - `FW/FIXED386.ASM` — fixed-point arithmetic (mul, div, normalize).
     The C versions exist in `FW/FIXED.C` -- just stop excluding them
     when on x86 (currently the asm shadows them).
   - `ZB/MESH386.ASM` — mesh transform inner loop. C fallback in `ZB/PERSP.C`?
     Investigate.
   - `ZB/T*_*.ASM` — triangle scan-converter inner loops. These are the
     hot path. Either write C fallbacks or salvage the equivalents from
     1.3.2's `drivers/softrend/` (alpha.c, depth.c, lighting.c, mapping.c
     are pure C and cover similar functionality).
   - `ZB/RECTOPS.ASM`, `ZB/SAFEDIV.ASM`, `ZB/SAR16.ASM`, `ZB/PIZ2TIA.ASM`,
     `ZB/PSCAN.ASM` — small utilities. Trivial C ports.

3. **Audit `__asm` blocks in `INC/G386IFIX.H`.**
   The only inline `__asm` in 1.1.2. Already `#ifdef`'d for compilers
   that need it; verify the non-asm path is correct.

4. **Engine-side LP64 work outside BRender** — scope-tracked in
   `docs/superpowers/specs/2026-05-01-sized-types-audit.md`. Kauai
   `sizeof(long)==4` assumption is the long pole.

**Risk:** the .ASM scan-converter inner loops are perf-critical. Naive C
fallbacks may make the renderer too slow to use. If so, options are:
ml64 rewrite (large effort), SSE2 intrinsics in C (medium), or accept
slower software render and rely on a future hardware-accelerated path
(SDL3_GPU / OpenGL).

**Out of scope for this spec:**
- SDL3 frontend (separate spec).
- LP64 (Linux/macOS x64) — Win64 first, since the kauai assumption work
  is shared but smaller for LLP64.
```

- [ ] **Step 2: Commit**

```bash
git add docs/superpowers/specs/2026-05-01-brender-x64-enablement.md
git commit -m "docs: spec out BRender x64 enablement as follow-on work"
```

---

## Self-Review Notes

**Spec coverage:** This plan implements the user's request to "review them both, considering our plans for 64bit and SDL3" by taking the design decision (use 1.1.2, not 1.3.2) and reducing it to a build-able x86 source swap. x64 and SDL3 are explicitly deferred to follow-on specs/plans because they each warrant their own decomposition.

**Placeholder scan:** Task 2 step 4 is the one place the plan acknowledges open-ended triage rather than prescribing exact edits — this is unavoidable because the actual error list depends on what the vendor sources do that the elib build hid. The plan compensates by listing the *categories* of likely errors and the fix pattern for each.

**Type consistency:** `brender_fw` and `brender_zb` are consistent throughout. `BRender::Libraries` and `BRender::BR{FMMXR,FWMXR,ZBMXR}` aliases are explicit in Task 4.

**Worktree:** Run this plan in a worktree (`git worktree add ../3DMMForever-brender-swap brender-swap`) since it touches the build system in invasive ways and you may want to keep `c` branch buildable while iterating.
