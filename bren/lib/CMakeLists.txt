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
#
# Explicit source list matches the original FW/MAKEFILE OBJS_C list -- not a
# glob, because the source dir contains additional files (ofileops.c,
# brexcept.c, loader.c, defdiag/deffile/defmem.c) that the 1995 build
# intentionally excluded. Including them breaks compilation (missing argstd.h)
# or duplicates the bren/ wrapper's STD handlers.

set(BRENDER_FW_C
  fw/actsupt.c
  fw/angles.c
  fw/brlists.c
  fw/brqsort.c
  fw/bswap.c
  fw/custsupt.c
  fw/datafile.c
  fw/def_mdl.c
  fw/def_mat.c
  fw/diag.c
  fw/envmap.c
  fw/error.c
  fw/ffhooks.c
  fw/file.c
  fw/fileops.c
  fw/fixed.c
  fw/fontptrs.c
  fw/fwsetup.c
  fw/light8.c
  fw/light8o.c
  fw/light24.c
  fw/light24o.c
  fw/logprint.c
  fw/matrix23.c
  fw/matrix34.c
  fw/matrix4.c
  fw/mem.c
  fw/onscreen.c
  fw/pick.c
  fw/pixelmap.c
  fw/pmdsptch.c
  fw/pmgenops.c
  fw/pmmemops.c
  fw/pool.c
  fw/prelight.c
  fw/prepmatl.c
  fw/prepmesh.c
  fw/prepmap.c
  fw/preptab.c
  fw/quat.c
  fw/quantize.c
  fw/register.c
  fw/regsupt.c
  fw/resource.c
  fw/scalar.c
  fw/scale.c
  fw/scratch.c
  fw/scrstr.c
  fw/surface.c
  fw/transfrm.c
  fw/vector.c
)

add_library(brender_fw STATIC ${BRENDER_FW_C})
target_include_directories(brender_fw
  PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/inc"
  PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/fw"
)

if (BREN_X86)
  # ASM list also pulled from FW/MAKEFILE OBJS_ASM (excludes tmp.asm scratch).
  target_sources(brender_fw PRIVATE
    fw/fixed386.asm
    fw/fxadc386.asm
    fw/blockops.asm
    fw/magicsym.asm
    fw/fnt3x5.asm
    fw/fntp4x6.asm
    fw/fntp7x9.asm
    fw/memloops.asm
  )
  # Original BASE_ASFLAGS from MAKEDEFS.MSC: -Cp preserves identifier case
  # (the font tables use both glyph_a and glyph_A as distinct labels).
  # -DDOS386 and -D__VISUALC__=1 match the historical assembler defines.
  set_source_files_properties(
    fw/fixed386.asm fw/fxadc386.asm fw/blockops.asm fw/magicsym.asm
    fw/fnt3x5.asm fw/fntp4x6.asm fw/fntp7x9.asm fw/memloops.asm
    PROPERTIES COMPILE_OPTIONS "/Cp;/DDOS386;/D__VISUALC__=1;/DBASED_FIXED=1"
  )
else()
  # C fallbacks for the .ASM functions, used on non-x86.
  target_sources(brender_fw PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/fallback/blockops.c"
  )
endif()

# Defines that the original Argonaut MAKEDEFS.MSC passed:
#   STATIC          -- empty in DEBUG, the C `static` keyword otherwise; many
#                      file-scope helpers in the source use STATIC as a marker
#                      that toggles linkage based on build config.
#   BASED_FIXED=1   -- selects the fixed-point math variant. 3DMM links
#                      against the FIXED-base BRender libs (brf_mxr).
#   DEBUG=0/1       -- internal debug guard inside the BRender source.
#   PARANOID=0/1    -- extra runtime checks.
#   -Zp4 (/Zp4)     -- pack structs at 4-byte alignment to match the 1995
#                      build's on-disk and in-memory layouts.
target_compile_definitions(brender_fw PRIVATE
  $<IF:$<CONFIG:Debug>,STATIC=,STATIC=static>
  BASED_FIXED=1
  $<IF:$<CONFIG:Debug>,DEBUG=1,DEBUG=0>
  $<IF:$<CONFIG:Debug>,PARANOID=1,PARANOID=0>
)

if (MSVC)
  # Gate C/CXX-only options to $<COMPILE_LANGUAGE:C> so MASM doesn't try to
  # parse /Zp4, /wd4047 etc as assembler flags.
  target_compile_options(brender_fw PRIVATE
    "$<$<COMPILE_LANGUAGE:C>:/Zp4>"
    "$<$<COMPILE_LANGUAGE:C>:/wd4047>"
    "$<$<COMPILE_LANGUAGE:C>:/wd4133>"
    "$<$<COMPILE_LANGUAGE:C>:/wd4244>"
    "$<$<COMPILE_LANGUAGE:C>:/wd4267>"
    "$<$<COMPILE_LANGUAGE:C>:/wd4311>"
    "$<$<COMPILE_LANGUAGE:C>:/wd4312>"
    "$<$<COMPILE_LANGUAGE:C>:/wd4229>"
  )
  target_compile_definitions(brender_fw PRIVATE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
    _CRT_SECURE_NO_WARNINGS
  )
endif()
