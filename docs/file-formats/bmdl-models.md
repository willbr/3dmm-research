# `BMDL` model chunks

A `BMDL` chunk holds a single BRender-shaped 3D model: vertex array, face
array, bounds, pivot. Models are referenced from actor templates and
scenes; the loader is `Model::FReadModl`
([`inc/modl.h`](../../inc/modl.h),
[`src/engine/modl.cpp`](../../src/engine/modl.cpp)).

## On-disk layout

```
+--------------------------------+  offset 0
| ModelOnFile (48 bytes, fixed)  |
|   short bo                     |
|   short osk                    |
|   short cver  (vertex count)   |
|   short cfac  (face count)     |
|   BRS rRadius                  |
|   BRB brb     (bounds)         |
|   BVEC3 bvec3Pivot             |
+--------------------------------+  offset 48
| BRV vertices[cver]             |  per-vertex BRender vertex (size set by BRender build)
+--------------------------------+
| BrFaceOnFile faces[cfac]       |  32 bytes each, fixed across architectures
+--------------------------------+
```

Defined in [`inc/modl.h`](../../inc/modl.h):

```cpp
struct ModelOnFile {
    short bo;
    short osk;
    short cver;          // count of vertices
    short cfac;          // count of faces
    BRS rRadius;
    BRB brb;             // bounds
    BVEC3 bvec3Pivot;
    // br_vertex rgbrv[cver];   <- follows
    // BrFaceOnFile rgbrf[cfac];<- follows
};
static_assert(sizeof(ModelOnFile) == 48, "ModelOnFile on-disk layout drift");
const ByteOrderMask kbomModlf = 0x55fffff0;
```

## Why faces have their own wire struct

`br_face` (BRender's runtime face) embeds a `br_material*` slot, which is
4 bytes on x86 and 8 bytes on x64. The 1995 `.CHK` chunks store the
material slot as a 4-byte placeholder (loaded as nil at runtime; the
material is resolved separately through the chunk's `MTRL` / `CMTL`
children). To keep the on-disk size fixed at 32 bytes regardless of host
pointer width:

```cpp
#pragma pack(push, 4)
struct BrFaceOnFile {
    uint16_t vertices[3];     // br_uint_16 vertices[3]
    uint16_t edges[3];        // br_uint_16 edges[3]
    uint32_t material_slot;   // 32-bit placeholder for br_material*
    uint16_t smoothing;       // br_uint_16
    uint8_t  flags;           // br_uint_8
    uint8_t  _pad0;           // br_uint_8
    int16_t  n[3];            // br_fvector3 = 3 * br_fraction (short, 6 B)
    // 2 B implicit pad here (align d to 4)
    int32_t  d;               // br_scalar (long)
};
#pragma pack(pop)
static_assert(sizeof(BrFaceOnFile) == 32, "BrFaceOnFile on-disk layout drift");
```

`Model::_FInit` reads the face array from the chunk and expands each
`BrFaceOnFile` into a runtime `br_face` (32 B on x86, 36 B on x64),
filling `material` with `nil`.

> **Note on `br_fraction`.** In the BASED_FIXED BRender build (which
> 3DMM uses), `br_fraction` is a 16-bit short, so `n[3]` is 6 bytes —
> not 12 as it would be in a floating-point build. Don't trust the
> BRender SDK's nominal struct sizes; the wire format is pinned by
> `BrFaceOnFile`.

## The wider `/Zp4` story

The vendored BRender library (`bren/lib/`) is built with `/Zp4`
(4-byte struct packing) so its in-memory layouts match the 1995
expectations. The C++ wrapper in 3DMMForever is **not** built with
`/Zp4` — to stop those two from disagreeing, `bren/inc/brender.h` bakes
`#pragma pack(push, 4)` / `pop` around all BRender struct definitions.
That pragma is a load-bearing band-aid; the long-term plan is to drop
`/Zp4` entirely and add `Br*OnFile` mirrors at every BRender-data I/O
boundary, the same shape as the TAG marshal pattern. See
[`docs/superpowers/specs/2026-05-01-sized-types-audit.md`](../superpowers/specs/2026-05-01-sized-types-audit.md).

## See also

- [`chunky-files.md`](chunky-files.md) — chunky file model.
- [`chunk-type-reference.md`](chunk-type-reference.md) — `BMDL`, `MTRL`,
  `CMTL`, `BDS `.
- [`3mm-movie.md`](3mm-movie.md) — how `BMDL` chunks are reached from a
  `.3MM` (via `ACTR` → `TMPL` → `BMDL`).
- [`../code-map/brender-wrapper.md`](../code-map/brender-wrapper.md) —
  the BRender wrapper layer.
