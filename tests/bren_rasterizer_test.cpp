/*
 * BRender rasterizer harness.
 *
 * Drives the asm-only z-buffer scan-converters directly via the global zb
 * state, dumps the colour buffer to a PGM file. Same source compiles and
 * runs on x86 (asm path) and x64 (C-fallback path); the resulting PGMs
 * should match byte-for-byte once the C ports are correct.
 *
 *   bren-rasterizer-test <scene> <output.pgm>
 *
 * Scenes:
 *   tri-piz2i    -- TriangleRenderPIZ2I (flat z-buffered, intensity per
 *                   vertex, no texture). Whole function is asm; on x64
 *                   the stub leaves the buffer untouched.
 *   tri-piz2tia  -- TriangleRenderPIZ2TIA (textured z-buffered, transparent,
 *                   lit). The C wrapper from awtmz.c sets up zb.* state
 *                   and calls TrapezoidRenderPIZ2TIA, which is the asm/stub.
 *
 * Build target is EXCLUDE_FROM_ALL: cmake --build build --target
 * bren-rasterizer-test.
 */

extern "C" {
#include "brender.h"
#include "zb.h"
}

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

/* These are declared in BRender's internal zbiproto.h with BR_ASM_CALL.
 * Rather than dragging that private header in we redeclare here -- the
 * linker resolves to the asm symbol on x86 and the stub/fallback on x64. */
extern "C" void BR_ASM_CALL TriangleRenderPIZ2I(struct temp_vertex_fixed *, struct temp_vertex_fixed *,
                                                struct temp_vertex_fixed *);
extern "C" void BR_ASM_CALL TriangleRenderPIZ2TIA(struct temp_vertex_fixed *, struct temp_vertex_fixed *,
                                                  struct temp_vertex_fixed *);
/* BrFixedFMac3 is declared in fwproto.h (pulled in by brender.h above). */

/* fwsetup.c references these globals through extern declarations and the
 * studio build pulls them from bren/std{err,file,mem}.c which depend on
 * kauai. Inline minimal nullimpls here to keep the test self-contained.
 * These are also in bren/lib/fw/def{diag,file,mem}.c but those TUs are
 * excluded from BRENDER_FW_C to avoid duplicate definitions in the studio
 * build. They're harmless here. */
extern "C" {
static void BR_CALLBACK NullWarning(char *) {}
static void BR_CALLBACK NullFailure(char *msg)
{
    fprintf(stderr, "BRender FATAL: %s\n", msg ? msg : "(null)");
    exit(11);
}
static br_diaghandler g_NullDiag = { (char *)"test-null-diag", NullWarning, NullFailure };
br_diaghandler *_BrDefaultDiagHandler = &g_NullDiag;

/* malloc-backed allocator; the rasterizer scenes don't allocate but
 * BrBegin (used by the actor-render mode) expects a working one. */
static void *MallocAlloc(br_size_t size, br_uint_8) { return malloc(size); }
static void MallocFree(void *m) { free(m); }
static br_size_t MallocInquire(br_uint_8) { return 0; }
static br_allocator g_MallocAllocator = { (char *)"test-malloc", MallocAlloc, MallocFree, MallocInquire };
br_allocator *_BrDefaultAllocator = &g_MallocAllocator;

static br_uint_32 NullAttrs(void) { return 0; }
static br_filesystem g_NullFs = { (char *)"test-null-fs" };
br_filesystem *_BrDefaultFilesystem = &g_NullFs;
}

/* Patch the filesystem function pointers at startup. C++ refuses the
 * void*-to-fnptr coercion that the original C file used, so we punch them
 * in via a constructor instead. The test never opens files, so even though
 * the slot signatures vary the no-op cast through `void*` is safe; nothing
 * dispatches through them. */
struct InitNullFs { InitNullFs() {
    g_NullFs.attributes = NullAttrs;
} } g_InitNullFs;

#define WIDTH 256
#define HEIGHT 256

static unsigned char g_colour[WIDTH * HEIGHT];
static unsigned short g_depth[WIDTH * HEIGHT];

#define TEX_W 32
#define TEX_H 32
static unsigned char g_tex[TEX_W * TEX_H];

/* 256x256 identity shade table: shade[intensity][texel] = texel.
 * The asm computes shade_table[(intensity_high_byte << 8) + texel] when
 * BR_MATF_LIGHT is on, so an identity table makes the lit path equivalent
 * to "write the texel as-is" -- handy for first-pass image comparison. */
static unsigned char g_shade_id[256 * 256];

static br_pixelmap g_tex_pm;
static br_material g_mat;

static void init_buffers(void)
{
    int i, j;

    memset(g_colour, 0, sizeof(g_colour));
    memset(g_depth, 0xFF, sizeof(g_depth)); /* far Z = 0xFFFF */

    for (i = 0; i < 256; i++)
        for (j = 0; j < 256; j++)
            g_shade_id[i * 256 + j] = (unsigned char)j;

    /* Procedural 8x8-cell checker, palette indices 60 (dark) and 200 (light). */
    for (i = 0; i < TEX_H; i++)
        for (j = 0; j < TEX_W; j++)
        {
            int cell = ((i >> 2) ^ (j >> 2)) & 1;
            g_tex[i * TEX_W + j] = (unsigned char)(cell ? 200 : 60);
        }
}

static void init_material(void)
{
    memset(&g_tex_pm, 0, sizeof(g_tex_pm));
    g_tex_pm.identifier = "test-tex";
    g_tex_pm.pixels = g_tex;
    g_tex_pm.row_bytes = TEX_W;
    g_tex_pm.type = BR_PMT_INDEX_8;
    g_tex_pm.width = TEX_W;
    g_tex_pm.height = TEX_H;

    memset(&g_mat, 0, sizeof(g_mat));
    g_mat.identifier = "test-mat";
    g_mat.colour_map = &g_tex_pm;
    g_mat.flags = BR_MATF_LIGHT;
    g_mat.index_base = 0;
    g_mat.index_range = 255;
}

static void install_zb(void)
{
    zb.colour_buffer = g_colour;
    zb.depth_buffer = (br_fixed_ls *)g_depth; /* asm treats as uint16[] */
    zb.row_width = WIDTH;                     /* bytes per row; 8bpp == width */
    zb.depth_row_width = WIDTH * 2;
    zb.material = &g_mat;
    zb.texture_buffer = g_tex;
    zb.shade_table = g_shade_id;
}

static void mkv(struct temp_vertex_fixed *v, int x, int y, br_fixed_ls z, int intensity)
{
    memset(v, 0, sizeof(*v));
    v->v[0] = (br_fixed_ls)x << 16;
    v->v[1] = (br_fixed_ls)y << 16;
    v->v[2] = z;
    v->comp[C_I] = (br_fixed_ls)intensity << 16;
}

static void mkv_uv(struct temp_vertex_fixed *v, int x, int y, br_fixed_ls z, int intensity, br_fixed_ls u,
                   br_fixed_ls vc)
{
    mkv(v, x, y, z, intensity);
    v->comp[C_U] = u;
    v->comp[C_V] = vc;
}

static void scene_tri_piz2i(void)
{
    struct temp_vertex_fixed a, b, c;
    mkv(&a, 128, 32, 0x4000, 64);   /* top centre, dim   */
    mkv(&b, 32, 224, 0x4000, 192);  /* bottom-left, bright */
    mkv(&c, 224, 224, 0x4000, 128); /* bottom-right, mid  */
    TriangleRenderPIZ2I(&a, &b, &c);
}

static void scene_tri_piz2tia(void)
{
    struct temp_vertex_fixed a, b, c;
    /* Texture coords as 16.16 fixed, range 0..tex_dim. */
    mkv_uv(&a, 128, 32, 0x4000, 200, 0, 0);
    mkv_uv(&b, 32, 224, 0x4000, 200, 0, (br_fixed_ls)TEX_H << 16);
    mkv_uv(&c, 224, 224, 0x4000, 200, (br_fixed_ls)TEX_W << 16, (br_fixed_ls)TEX_H << 16);
    TriangleRenderPIZ2TIA(&a, &b, &c);
}

/* Same triangle as tri-piz2tia but vertices in reversed (CW vs CCW) order.
 * Exercises the "direction = false" branch of awtmi.h's setup, which
 * negates u_grad/v_grad and makes the trapezoid renderer walk pixels
 * right-to-left. If our backward branch has a bug this scene will diverge
 * from the asm version. */
static void scene_tri_piz2tia_reversed(void)
{
    struct temp_vertex_fixed a, b, c;
    mkv_uv(&a, 128, 32, 0x4000, 200, 0, 0);
    mkv_uv(&b, 224, 224, 0x4000, 200, (br_fixed_ls)TEX_W << 16, (br_fixed_ls)TEX_H << 16);
    mkv_uv(&c, 32, 224, 0x4000, 200, 0, (br_fixed_ls)TEX_H << 16);
    TriangleRenderPIZ2TIA(&a, &b, &c);
}

/* Two triangles forming a textured quad with consistent CCW winding. */
static void scene_quad_ccw(void)
{
    struct temp_vertex_fixed tl, tr, bl, br;
    mkv_uv(&tl, 32,  32, 0x4000, 200, 0,                          0);
    mkv_uv(&tr, 224, 32, 0x4000, 200, (br_fixed_ls)TEX_W << 16,   0);
    mkv_uv(&bl, 32, 224, 0x4000, 200, 0,                          (br_fixed_ls)TEX_H << 16);
    mkv_uv(&br, 224,224, 0x4000, 200, (br_fixed_ls)TEX_W << 16,   (br_fixed_ls)TEX_H << 16);
    TriangleRenderPIZ2TIA(&tl, &bl, &tr); /* upper-left tri */
    TriangleRenderPIZ2TIA(&tr, &bl, &br); /* lower-right tri */
}

/* Same quad with CW winding (vertex order swapped on each triangle). */
static void scene_quad_cw(void)
{
    struct temp_vertex_fixed tl, tr, bl, br;
    mkv_uv(&tl, 32,  32, 0x4000, 200, 0,                          0);
    mkv_uv(&tr, 224, 32, 0x4000, 200, (br_fixed_ls)TEX_W << 16,   0);
    mkv_uv(&bl, 32, 224, 0x4000, 200, 0,                          (br_fixed_ls)TEX_H << 16);
    mkv_uv(&br, 224,224, 0x4000, 200, (br_fixed_ls)TEX_W << 16,   (br_fixed_ls)TEX_H << 16);
    TriangleRenderPIZ2TIA(&tl, &tr, &bl);
    TriangleRenderPIZ2TIA(&tr, &br, &bl);
}

/* Tall, thin triangle with a steep long edge. Stresses the X-fraction
 * carry detection (each scanline advances X by a small fractional amount,
 * occasionally crossing a pixel boundary). */
static void scene_tri_thin_vertical(void)
{
    struct temp_vertex_fixed a, b, c;
    mkv_uv(&a, 128, 32, 0x4000, 200, 0, 0);
    mkv_uv(&b, 124, 224, 0x4000, 200, 0, (br_fixed_ls)TEX_H << 16);
    mkv_uv(&c, 132, 224, 0x4000, 200, (br_fixed_ls)TEX_W << 16, (br_fixed_ls)TEX_H << 16);
    TriangleRenderPIZ2TIA(&a, &b, &c);
}

/* Wide, short triangle with a shallow long edge -- stresses the per-
 * scanline X delta (large d_i + carry per row). */
static void scene_tri_thin_horizontal(void)
{
    struct temp_vertex_fixed a, b, c;
    mkv_uv(&a, 128, 124, 0x4000, 200, 0, 0);
    mkv_uv(&b, 32, 132, 0x4000, 200, 0, (br_fixed_ls)TEX_H << 16);
    mkv_uv(&c, 224, 132, 0x4000, 200, (br_fixed_ls)TEX_W << 16, (br_fixed_ls)TEX_H << 16);
    TriangleRenderPIZ2TIA(&a, &b, &c);
}

/* "Cube faces" -- five quads laid out as the projected faces of a cube
 * viewed from a 3/4 angle. Vertex positions and UVs hand-computed.
 * Mimics what the engine produces after model_to_screen transform. */
static void scene_cube_faces(void)
{
    struct temp_vertex_fixed v[8];
    /* Two "depths" of quads: the front (closer, smaller z) and the back
     * (further, larger z). z values within 16-bit range. Coordinates are
     * already in screen space. */
    /* Front quad (closer): z=0x2000, smaller squares around the centre. */
    mkv_uv(&v[0], 64,  64, 0x2000, 220, 0,                          0);
    mkv_uv(&v[1], 192, 64, 0x2000, 220, (br_fixed_ls)TEX_W << 16,   0);
    mkv_uv(&v[2], 64, 192, 0x2000, 220, 0,                          (br_fixed_ls)TEX_H << 16);
    mkv_uv(&v[3], 192,192, 0x2000, 220, (br_fixed_ls)TEX_W << 16,   (br_fixed_ls)TEX_H << 16);
    /* Back quad (further): z=0x6000, larger; offset right and down so it
     * peeks out from behind the front one. */
    mkv_uv(&v[4], 96,  96, 0x6000, 120, 0,                          0);
    mkv_uv(&v[5], 240, 96, 0x6000, 120, (br_fixed_ls)TEX_W << 16,   0);
    mkv_uv(&v[6], 96, 240, 0x6000, 120, 0,                          (br_fixed_ls)TEX_H << 16);
    mkv_uv(&v[7], 240,240, 0x6000, 120, (br_fixed_ls)TEX_W << 16,   (br_fixed_ls)TEX_H << 16);

    TriangleRenderPIZ2TIA(&v[4], &v[6], &v[5]); /* back, two tris */
    TriangleRenderPIZ2TIA(&v[5], &v[6], &v[7]);
    TriangleRenderPIZ2TIA(&v[0], &v[2], &v[1]); /* front (overlaps back; z-buffer should hide back where they overlap) */
    TriangleRenderPIZ2TIA(&v[1], &v[2], &v[3]);
}

/* Single triangle whose vertices have THREE different z values, so the
 * per-pixel z interpolation actually does something. Previous trapezoid
 * tests all had constant z per face, leaving per-pixel z interp
 * unverified. The asm uses pz.grad_x added per pixel and pz.d_carry/
 * d_nocarry per scanline; if either is mishandled the pgm diverges. */
static void scene_tri_tilted_z(void)
{
    struct temp_vertex_fixed a, b, c;
    /* Wide z range: near (0x1000) at apex, far (0xE000) at bottom-left,
     * mid (0x7000) at bottom-right. UVs vary too so the texture sampling
     * also exercises a slope. */
    mkv_uv(&a, 128, 32, 0x1000, 220, 0, 0);
    mkv_uv(&b, 32, 224, 0xE000, 200, 0, (br_fixed_ls)TEX_H << 16);
    mkv_uv(&c, 224, 224, 0x7000, 120, (br_fixed_ls)TEX_W << 16, (br_fixed_ls)TEX_H << 16);
    TriangleRenderPIZ2TIA(&a, &b, &c);
}

/* Two overlapping triangles where the SECOND triangle has a per-pixel
 * z gradient that crosses the first triangle's constant z at the
 * overlap region. The z-buffer should split the overlap so pixels
 * closer than the first show the second, pixels farther stay first.
 * If z interp is off by even a small amount the split line moves and
 * the pgm diverges. Mirrors the actor case where hair (varying z) and
 * face (varying z) overlap. */
static void scene_tilted_overlap(void)
{
    struct temp_vertex_fixed a1, b1, c1;
    struct temp_vertex_fixed a2, b2, c2;
    /* First triangle: constant z = 0x6000 (mid-far), full screen. */
    mkv_uv(&a1, 32, 32, 0x6000, 200, 0, 0);
    mkv_uv(&b1, 224, 32, 0x6000, 200, (br_fixed_ls)TEX_W << 16, 0);
    mkv_uv(&c1, 128, 224, 0x6000, 200, (br_fixed_ls)TEX_W / 2 << 16, (br_fixed_ls)TEX_H << 16);
    TriangleRenderPIZ2TIA(&a1, &b1, &c1);
    /* Second triangle: tilted z from 0x2000 (closer than #1) at apex to
     * 0xA000 (farther than #1) at bottom. Crosses #1's z partway down. */
    mkv_uv(&a2, 128, 64, 0x2000, 100, 0, 0);
    mkv_uv(&b2, 64, 200, 0xA000, 100, 0, (br_fixed_ls)TEX_H << 16);
    mkv_uv(&c2, 192, 200, 0xA000, 100, (br_fixed_ls)TEX_W << 16, (br_fixed_ls)TEX_H << 16);
    TriangleRenderPIZ2TIA(&a2, &b2, &c2);
}

/* Comprehensive unit test for the BrFixed* arithmetic functions.
 * Diff-compares asm (x86) vs C fallback (x64) output for every
 * function with a non-trivial implementation. A previous shrd-by-15
 * vs shrd-by-16 off-by-one in the FMac variants was the cause of
 * x64 face-normal dot products coming out at half magnitude and
 * back-face culling rendering models inside-out -- this sweep catches
 * any similar drift in the other ports. */
static int run_fmac_unit(const char *path)
{
    /* Each row: a (1.15 fraction), b (16.16 fixed), c, d, e, f */
    struct
    {
        br_fixed_ls a, b, c, d, e, f;
    } cases[] = {
        {0x4000, 0x10000, 0, 0, 0, 0},                         /* 0.5 * 1.0 = 0.5 */
        {0x7FFF, 0x10000, 0, 0, 0, 0},                         /* ~1.0 * 1.0 ~ 1.0 */
        {0xC000, 0x10000, 0, 0, 0, 0},                         /* -0.5 * 1.0 = -0.5 (sign-extended) */
        {0x4000, 0x20000, 0x4000, 0x20000, 0x4000, 0x20000},   /* 0.5*2.0 thrice = 3.0 */
        {0x2000, 0x40000, 0x2000, 0x40000, 0x2000, 0x40000},   /* 0.25*4.0 thrice = 3.0 */
        {0x7FFF, 0x80000, 0x7FFF, 0x80000, 0x7FFF, 0x80000},   /* near 1.0 * 8.0 thrice = ~24.0 */
        {0x1000, 0x100000, 0x2000, 0x80000, 0x4000, 0x40000},  /* mixed */
        {(br_fixed_ls)(int32_t)0xFFFF8001, 0x10000, 0, 0, 0, 0}, /* low16 = 0x8001 = -32767 */
    };
    FILE *f = fopen(path, "w");
    if (!f)
    {
        perror(path);
        return -1;
    }
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++)
    {
        br_fixed_ls r = BrFixedFMac3(cases[i].a, cases[i].b, cases[i].c, cases[i].d, cases[i].e, cases[i].f);
        fprintf(f, "fmac3 a=%08lx b=%08lx c=%08lx d=%08lx e=%08lx f=%08lx -> %08lx\n", (long)(unsigned long)cases[i].a,
                (long)(unsigned long)cases[i].b, (long)(unsigned long)cases[i].c, (long)(unsigned long)cases[i].d,
                (long)(unsigned long)cases[i].e, (long)(unsigned long)cases[i].f, (long)(unsigned long)r);
    }

    /* Sweep over the rest of the BrFixed surface area. Pick representative
     * 16.16 fixed-point inputs that include sign changes, fractional values,
     * and magnitudes that exercise the high half of the 64-bit accumulator. */
    /* Magnitudes kept moderate so a/b never trips divide-overflow in
     * idiv (which the asm signals via SIGFPE rather than returning a
     * sentinel). +eps and most-negative are skipped for the same reason. */
    static const br_fixed_ls samples[] = {
        0x00010000,                /* 1.0 */
        0x00018000,                /* 1.5 */
        (br_fixed_ls)(int32_t)0xFFFF0000, /* -1.0 */
        0x00008000,                /* 0.5 */
        0x00040000,                /* 4.0 */
        0x00100000,                /* 16.0 */
        (br_fixed_ls)(int32_t)0xFFFC0000, /* -4.0 */
    };
    const int nsam = (int)(sizeof(samples) / sizeof(samples[0]));

    for (int i = 0; i < nsam; i++)
    {
        for (int j = 0; j < nsam; j++)
        {
            br_fixed_ls a = samples[i], b = samples[j];
            fprintf(f, "mul   a=%08lx b=%08lx       -> %08lx\n", (long)(unsigned long)a, (long)(unsigned long)b,
                    (long)(unsigned long)BrFixedMul(a, b));
            if (b != 0)
                fprintf(f, "div   a=%08lx b=%08lx       -> %08lx\n", (long)(unsigned long)a, (long)(unsigned long)b,
                        (long)(unsigned long)BrFixedDiv(a, b));
            fprintf(f, "mac2  a=%08lx b=%08lx c=a d=b -> %08lx\n", (long)(unsigned long)a, (long)(unsigned long)b,
                    (long)(unsigned long)BrFixedMac2(a, b, a, b));
            fprintf(f, "sqr   a=%08lx                 -> %08lx\n", (long)(unsigned long)a,
                    (long)(unsigned long)BrFixedSqr(a));
            fprintf(f, "sqr2  a=%08lx b=%08lx       -> %08lx\n", (long)(unsigned long)a, (long)(unsigned long)b,
                    (long)(unsigned long)BrFixedSqr2(a, b));
            fprintf(f, "len2  a=%08lx b=%08lx       -> %08lx\n", (long)(unsigned long)a, (long)(unsigned long)b,
                    (long)(unsigned long)BrFixedLength2(a, b));
            if (a != 0 || b != 0)
                fprintf(f, "rlen2 a=%08lx b=%08lx       -> %08lx\n", (long)(unsigned long)a, (long)(unsigned long)b,
                        (long)(unsigned long)BrFixedRLength2(a, b));
            if (b != 0)
                fprintf(f, "muldv a=%08lx b=%08lx c=One -> %08lx\n", (long)(unsigned long)a, (long)(unsigned long)b,
                        (long)(unsigned long)BrFixedMulDiv(a, b, 0x10000));
            if (a != 0)
                fprintf(f, "rcp   a=%08lx                 -> %08lx\n", (long)(unsigned long)a,
                        (long)(unsigned long)BrFixedRcp(a));
        }
    }

    /* Trig: BAM angles. */
    for (int deg = 0; deg <= 360; deg += 15)
    {
        br_angle ang = (br_angle)(deg * 65536L / 360);
        fprintf(f, "sin   ang=%04x                    -> %08lx\n", ang, (long)(unsigned long)BrFixedSin(ang));
        fprintf(f, "cos   ang=%04x                    -> %08lx\n", ang, (long)(unsigned long)BrFixedCos(ang));
    }

    /* atan2: critical for actor facing-direction calculation in 3DMM.
     * The BRender signature is BrFixedATan2(x, y) and semantically
     * returns the angle of point (x, y) from the +x axis (= libm
     * atan2(y, x)). Sweep all 8 octants plus axis cases. */
    {
        struct
        {
            br_fixed_ls x, y;
        } a2cases[] = {
            {0x10000, 0x00000},  /* +x axis */
            {0x10000, 0x10000},  /* +x +y, |x|=|y| */
            {0x08000, 0x10000},  /* +x +y, |x|<|y| */
            {0x10000, 0x08000},  /* +x +y, |x|>|y| */
            {0x00000, 0x10000},  /* +y axis */
            {-0x08000, 0x10000}, /* -x +y, |x|<|y| */
            {-0x10000, 0x08000}, /* -x +y, |x|>|y| */
            {-0x10000, 0x00000}, /* -x axis */
            {-0x10000, -0x08000},/* -x -y */
            {-0x08000, -0x10000},/* -x -y */
            {0x00000, -0x10000}, /* -y axis */
            {0x08000, -0x10000}, /* +x -y, |x|<|y| */
            {0x10000, -0x08000}, /* +x -y, |x|>|y| */
        };
        for (size_t i = 0; i < sizeof(a2cases) / sizeof(a2cases[0]); i++)
        {
            br_angle r = BrFixedATan2(a2cases[i].x, a2cases[i].y);
            fprintf(f, "atan2 x=%08lx y=%08lx -> %04x\n", (long)(unsigned long)a2cases[i].x,
                    (long)(unsigned long)a2cases[i].y, r);
        }
    }

    /* Mac3 (all _F) -- routes through the most-used 3-arg accumulator. */
    for (int i = 0; i < nsam; i++)
    {
        for (int j = 0; j < nsam; j++)
        {
            br_fixed_ls a = samples[i], b = samples[j];
            fprintf(f, "mac3  a=%08lx b=%08lx       -> %08lx\n", (long)(unsigned long)a, (long)(unsigned long)b,
                    (long)(unsigned long)BrFixedMac3(a, b, a, b, a, b));
        }
    }

    /* DivF -- the 1.31-fraction divide used by PERSP_DIV_Z. Asm pre-shifts
     * dividend by 31 (sar/rcr pair) then unsigned div, so result for
     * |a|<|b| is a/b * 2^31 (a 1.31 fraction). The previous fallback
     * shifted by 16 and produced depth values 2^15 too small (root of the
     * 68-pixel z-fight in props-overlap). PERSP_DIV_Z passes -a as the
     * dividend so the asm's unsigned div sees a positive value; sweep
     * positive a < positive b only -- otherwise the unsigned div either
     * overflows (a >= b yields a quotient that doesn't fit in 32 bits and
     * SIGFPEs in the asm) or wraps a negative dividend into a huge
     * unsigned. */
    for (int i = 0; i < nsam; i++)
    {
        for (int j = 0; j < nsam; j++)
        {
            br_fixed_ls a = samples[i], b = samples[j];
            if (b == 0 || a < 0 || b < 0 || a >= b)
                continue;
            fprintf(f, "divF  a=%08lx b=%08lx       -> %08lx\n", (long)(unsigned long)a, (long)(unsigned long)b,
                    (long)(unsigned long)BrFixedDivF(a, b));
        }
    }

    fflush(f);

    fclose(f);
    return 0;
}

/* ============================================================
 * Real-pipeline actor-render harness.
 *
 * Goes through the full BRender machinery (BrBegin / BrZbBegin /
 * BrZbSceneRender) to render a hand-built cube model from N camera
 * angles into one composited bitmap. Mirrors the visible-rendering
 * path that 3DMM uses for actors -- transform, light, cull, rasterize.
 *
 * Used to bisect between "rasterizer is the bug" (already disproven by
 * the byte-perfect rasterizer scenes) and "transform / cull / lighting
 * is the bug" (where the user's reported z-order issue is most likely
 * to live).
 * ============================================================ */

/* 8 vertices of a unit cube centred at origin. Coordinates are 16.16
 * fixed via BR_SCALAR. UVs are 0..1 in 16.16 fixed via BR_SCALAR. */
#define CUBE_HALF BR_SCALAR(0.5)

/* Triangle face winding for a cube viewed from outside (CCW in BRender's
 * left-handed coords). Each face = 2 triangles = 6 vertex indices. */
struct CubeFace
{
    int v[6];
};
static const CubeFace g_cube_faces[6] = {
    /* +Z (front) */ {{0, 1, 2, 1, 3, 2}},
    /* -Z (back)  */ {{4, 6, 5, 5, 6, 7}},
    /* +X (right) */ {{1, 5, 3, 5, 7, 3}},
    /* -X (left)  */ {{4, 0, 6, 0, 2, 6}},
    /* +Y (top)   */ {{2, 3, 6, 3, 7, 6}},
    /* -Y (bot)   */ {{4, 5, 0, 0, 5, 1}},
};

static br_model *make_cube_model(void)
{
    br_model *m = BrModelAllocate((char *)"unit-cube", 8, 12);
    if (!m)
        return 0;

    /* Vertex coords: (±0.5, ±0.5, ±0.5). Bit layout (x, y, z) -> index. */
    for (int i = 0; i < 8; i++)
    {
        m->vertices[i].p.v[0] = (i & 1) ? CUBE_HALF : -CUBE_HALF;
        m->vertices[i].p.v[1] = (i & 2) ? CUBE_HALF : -CUBE_HALF;
        m->vertices[i].p.v[2] = (i & 4) ? CUBE_HALF : -CUBE_HALF;
        /* UV: project x,y to 0..1 -- not great per-face mapping but
         * sufficient for a visual diff between asm and C. */
        m->vertices[i].map.v[0] = (i & 1) ? BR_SCALAR(1.0) : BR_SCALAR(0.0);
        m->vertices[i].map.v[1] = (i & 2) ? BR_SCALAR(1.0) : BR_SCALAR(0.0);
        m->vertices[i].index = 0;
    }

    /* 12 faces from 6 cube faces × 2 triangles. */
    for (int f = 0; f < 6; f++)
    {
        for (int t = 0; t < 2; t++)
        {
            br_face *fp = &m->faces[f * 2 + t];
            fp->vertices[0] = (br_uint_16)g_cube_faces[f].v[t * 3 + 0];
            fp->vertices[1] = (br_uint_16)g_cube_faces[f].v[t * 3 + 1];
            fp->vertices[2] = (br_uint_16)g_cube_faces[f].v[t * 3 + 2];
            fp->material = 0;
            fp->smoothing = 0;
            fp->flags = 0;
        }
    }

    m->flags = 0;
    return m;
}

/* Lit + textured material with BR_MATF_LIGHT and our checker texture. */
static br_material *make_test_material(br_pixelmap *colour_map)
{
    br_material *mat = BrMaterialAllocate((char *)"cube-mat");
    if (!mat)
        return 0;
    mat->colour_map = colour_map;
    mat->flags = BR_MATF_LIGHT | BR_MATF_SMOOTH;
    mat->index_base = 0;
    mat->index_range = 255;
    mat->ka = BR_UFRACTION(0.20);
    mat->kd = BR_UFRACTION(0.80);
    mat->ks = BR_UFRACTION(0.00);
    mat->opacity = 0xFF;
    return mat;
}

/* On-disk wire format for a 3DMM BMDL chunk's ModelOnFile header.
 * Mirrors inc/modl.h's struct exactly; we redeclare here so the test
 * doesn't need to pull in the engine include path. */
struct ModelOnFile_Wire
{
    int16_t bo;
    int16_t osk;
    int16_t cver;
    int16_t cfac;
    int32_t rRadius;
    int32_t bounds_min[3]; /* br_bounds.min */
    int32_t bounds_max[3]; /* br_bounds.max */
    int32_t pivot[3];
};
/* Wire-format face entry, 32 bytes -- see inc/modl.h's BrFaceOnFile. */
struct BrFaceOnFile_Wire
{
    uint16_t vertices[3];
    uint16_t edges[3];
    uint32_t material_slot; /* placeholder */
    uint16_t smoothing;
    uint8_t flags;
    uint8_t _pad0;
    int16_t n[3]; /* face plane normal as 3 fractions */
    int16_t _pad1;
    int32_t d; /* face plane d */
};

/* Load a real BMDL chunk dumped via the extract-bmdl tool, parse the
 * ModelOnFile + br_vertex[] + BrFaceOnFile[] bytes (as Model::_FInit
 * does in src/engine/modl.cpp), build a br_model, and render it from
 * N camera angles. This is the user-requested "real prop from 3DMM"
 * test -- exercises the same vertex / face data the studio renders. */
static br_model *load_bmdl_from_file(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f)
    {
        perror(path);
        return 0;
    }
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (size < (long)sizeof(ModelOnFile_Wire))
    {
        fprintf(stderr, "%s: too small (%ld bytes)\n", path, size);
        fclose(f);
        return 0;
    }
    uint8_t *buf = (uint8_t *)malloc(size);
    fread(buf, 1, size, f);
    fclose(f);

    ModelOnFile_Wire *hdr = (ModelOnFile_Wire *)buf;
    int cver = hdr->cver;
    int cfac = hdr->cfac;
    int radius_zero = (hdr->rRadius == 0);

    fprintf(stderr, "loaded %s: cver=%d cfac=%d rRadius=0x%08lx %s\n", path, cver, cfac, (long)hdr->rRadius,
            radius_zero ? "(unprepared)" : "(prepared)");

    long expected = sizeof(ModelOnFile_Wire) + (long)cver * 32 + (long)cfac * 32;
    if (expected != size)
    {
        fprintf(stderr, "%s: size mismatch (header expects %ld, file has %ld)\n", path, expected, size);
        free(buf);
        return 0;
    }

    br_model *m = BrModelAllocate((char *)"prop", (br_uint_16)cver, (br_uint_16)cfac);
    if (!m)
    {
        free(buf);
        return 0;
    }

    /* br_vertex layout is identical x86/x64 (32 bytes, no embedded pointers). */
    memcpy(m->vertices, buf + sizeof(ModelOnFile_Wire), (size_t)cver * 32);

    /* Marshal each on-disk BrFaceOnFile_Wire into runtime br_face. Same as
     * src/engine/modl.cpp's _BrFaceFromOnFile. */
    BrFaceOnFile_Wire *faces_wire = (BrFaceOnFile_Wire *)(buf + sizeof(ModelOnFile_Wire) + (long)cver * 32);
    for (int i = 0; i < cfac; i++)
    {
        br_face *fp = &m->faces[i];
        fp->vertices[0] = faces_wire[i].vertices[0];
        fp->vertices[1] = faces_wire[i].vertices[1];
        fp->vertices[2] = faces_wire[i].vertices[2];
        fp->edges[0] = faces_wire[i].edges[0];
        fp->edges[1] = faces_wire[i].edges[1];
        fp->edges[2] = faces_wire[i].edges[2];
        fp->material = 0;
        fp->smoothing = faces_wire[i].smoothing;
        fp->flags = faces_wire[i].flags;
        fp->_pad0 = faces_wire[i]._pad0;
        fp->n.v[0] = (br_fraction)faces_wire[i].n[0];
        fp->n.v[1] = (br_fraction)faces_wire[i].n[1];
        fp->n.v[2] = (br_fraction)faces_wire[i].n[2];
        fp->d = (br_scalar)faces_wire[i].d;
    }

    if (!radius_zero)
    {
        /* The asm engine handles "preprepared" models specially. For the
         * test scene we let BrModelUpdate recompute everything from
         * scratch, so this matters less. Clear flags so Update runs. */
        m->flags = 0;
    }

    free(buf);
    return m;
}

/* Render N camera angles around a real 3DMM prop loaded from a BMDL bin. */
static int run_prop_render(const char *out_path, const char *bmdl_path, int n_angles)
{
    BrBegin();

    br_pixelmap *colour = BrPixelmapAllocate(BR_PMT_INDEX_8, WIDTH, HEIGHT, 0, BR_PMAF_NORMAL);
    br_pixelmap *depth = BrPixelmapMatch(colour, BR_PMMATCH_DEPTH_16);
    if (!colour || !depth)
    {
        fprintf(stderr, "pixmap alloc failed\n");
        return 1;
    }
    BrPixelmapFill(colour, 0);
    BrPixelmapFill(depth, 0xFFFF);

    BrZbBegin(colour->type, BR_PMT_DEPTH_16);

    br_pixelmap tex = {};
    tex.identifier = (char *)"checker";
    tex.pixels = g_tex;
    tex.row_bytes = TEX_W;
    tex.type = BR_PMT_INDEX_8;
    tex.width = TEX_W;
    tex.height = TEX_H;
    BrMapAdd(&tex);

    br_pixelmap shade = {};
    shade.identifier = (char *)"identity-shade";
    shade.pixels = g_shade_id;
    shade.row_bytes = 256;
    shade.type = BR_PMT_INDEX_8;
    shade.width = 256;
    shade.height = 256;
    BrTableAdd(&shade);

    br_material *mat = make_test_material(&tex);
    mat->index_shade = &shade;
    BrMaterialAdd(mat);

    br_model *prop = load_bmdl_from_file(bmdl_path);
    if (!prop)
    {
        fprintf(stderr, "could not load prop from %s\n", bmdl_path);
        return 1;
    }
    BrModelAdd(prop);

    br_actor *world = BrActorAllocate(BR_ACTOR_NONE, 0);

    static br_camera cam_data;
    cam_data.identifier = (char *)"cam";
    cam_data.type = BR_CAMERA_PERSPECTIVE_FOV;
    cam_data.field_of_view = BR_ANGLE_DEG(45);
    /* yon_z capped to fit 16.16 fixed (max ~32767). Hither correspondingly
     * small but positive. */
    cam_data.hither_z = BR_SCALAR(0.1);
    cam_data.yon_z = BR_SCALAR(1000.0);
    cam_data.aspect = BR_SCALAR(1.0);
    br_actor *cam = BrActorAdd(world, BrActorAllocate(BR_ACTOR_CAMERA, &cam_data));
    cam->t.type = BR_TRANSFORM_MATRIX34;
    /* Camera distance derived from model bounding radius -- look at
     * something far enough that the prop fits in view. */
    BrModelUpdate(prop, BR_MODU_ALL);
    float radius = BrScalarToFloat(prop->radius);
    if (radius < 0.5f) radius = 1.0f;
    if (radius > 100.0f) radius = 100.0f; /* keep cam translate in fixed range */
    BrMatrix34Translate(&cam->t.t.mat, BR_SCALAR(0), BR_SCALAR(0), BR_SCALAR(radius * 3.0));

    static br_light light_data;
    light_data.identifier = (char *)"l";
    light_data.type = BR_LIGHT_DIRECT;
    light_data.colour = BR_COLOUR_RGB(255, 255, 255);
    light_data.attenuation_c = BR_SCALAR(1);
    br_actor *light = BrActorAdd(world, BrActorAllocate(BR_ACTOR_LIGHT, &light_data));
    BrLightEnable(light);

    /* N copies of the prop arranged horizontally, each at a different yaw. */
    int cols = n_angles;
    float spacing = radius * 2.5f;
    for (int i = 0; i < n_angles; i++)
    {
        br_actor *a = BrActorAdd(world, BrActorAllocate(BR_ACTOR_MODEL, 0));
        a->model = prop;
        a->material = mat;
        a->t.type = BR_TRANSFORM_MATRIX34;
        BrMatrix34Identity(&a->t.t.mat);
        BrMatrix34RotateY(&a->t.t.mat, (br_angle)((long)i * 65536L / n_angles));
        float xoff = (i - (cols - 1) * 0.5f) * spacing;
        a->t.t.mat.m[3][0] = BR_SCALAR(xoff);
    }

    BrZbSceneRender(world, cam, colour, depth);

    FILE *f = fopen(out_path, "wb");
    if (!f) { perror(out_path); return 1; }
    fprintf(f, "P5\n%d %d\n255\n", WIDTH, HEIGHT);
    fwrite(colour->pixels, 1, (size_t)WIDTH * HEIGHT, f);
    fclose(f);

    BrZbEnd();
    BrEnd();
    printf("ok: prop-render -> %s (radius=%.2f)\n", out_path, radius);
    return 0;
}

/* Multi-prop overlap test. Loads up to two real BMDL chunks and renders
 * them as several intersecting copies, each in a distinct solid colour
 * via per-actor material with a 1x1 colour pixmap. Mirrors how 3DMM body
 * parts overlap in screen space -- pixels in the overlap region must be
 * resolved by the z-buffer, NOT by render order, so a wrong z value or
 * a missed face cull shows up as the wrong colour winning. */
static br_pixelmap make_solid_pixmap(uint8_t *backing, int idx)
{
    backing[0] = (uint8_t)idx;
    br_pixelmap pm = {};
    pm.identifier = (char *)"solid";
    pm.pixels = backing;
    pm.row_bytes = 1;
    pm.type = BR_PMT_INDEX_8;
    pm.width = 1;
    pm.height = 1;
    return pm;
}
static int run_props_overlap(const char *out_path, const char *bmdl_a, const char *bmdl_b)
{
    BrBegin();

    br_pixelmap *colour = BrPixelmapAllocate(BR_PMT_INDEX_8, WIDTH, HEIGHT, 0, BR_PMAF_NORMAL);
    br_pixelmap *depth = BrPixelmapMatch(colour, BR_PMMATCH_DEPTH_16);
    if (!colour || !depth) { fprintf(stderr, "pixmap alloc failed\n"); return 1; }
    BrPixelmapFill(colour, 0);
    BrPixelmapFill(depth, 0xFFFF);

    BrZbBegin(colour->type, BR_PMT_DEPTH_16);

    /* One identity shade table, used by every material. */
    static br_pixelmap shade;
    shade.identifier = (char *)"id-shade";
    shade.pixels = g_shade_id;
    shade.row_bytes = 256;
    shade.type = BR_PMT_INDEX_8;
    shade.width = 256;
    shade.height = 256;
    BrTableAdd(&shade);

    /* Distinct flat colour per prop instance. Palette indices spread
     * across the range so the differences pop in the bitmap dump. */
    enum { N_INSTANCES = 5 };
    static const uint8_t prop_colours[N_INSTANCES] = { 60, 100, 140, 180, 220 };

    static uint8_t solid_pixels[N_INSTANCES];
    static br_pixelmap solid_pms[N_INSTANCES];
    static br_material *mats[N_INSTANCES];
    for (int i = 0; i < N_INSTANCES; i++)
    {
        solid_pms[i] = make_solid_pixmap(&solid_pixels[i], prop_colours[i]);
        BrMapAdd(&solid_pms[i]);
        mats[i] = BrMaterialAllocate((char *)"solid-mat");
        mats[i]->colour_map = &solid_pms[i];
        mats[i]->index_shade = &shade;
        mats[i]->flags = BR_MATF_LIGHT | BR_MATF_SMOOTH;
        mats[i]->ka = BR_UFRACTION(0.20);
        mats[i]->kd = BR_UFRACTION(0.80);
        mats[i]->ks = BR_UFRACTION(0.00);
        mats[i]->opacity = 0xFF;
        mats[i]->index_base = 0;
        mats[i]->index_range = 255;
        BrMaterialAdd(mats[i]);
    }

    /* Two distinct prop models, alternated across instances so we get
     * inter-model and intra-model overlap. */
    br_model *propA = load_bmdl_from_file(bmdl_a);
    br_model *propB = bmdl_b ? load_bmdl_from_file(bmdl_b) : propA;
    if (!propA || !propB) { fprintf(stderr, "could not load props\n"); return 1; }
    fprintf(stderr, "  step: BrModelAdd(propA)\n");
    BrModelAdd(propA);
    if (propB != propA) {
        fprintf(stderr, "  step: BrModelAdd(propB)\n");
        BrModelAdd(propB);
    }
    fprintf(stderr, "  step: BrModelUpdate(propA)\n");
    BrModelUpdate(propA, BR_MODU_ALL);
    if (propB != propA) {
        fprintf(stderr, "  step: BrModelUpdate(propB)\n");
        BrModelUpdate(propB, BR_MODU_ALL);
    }
    fprintf(stderr, "  step: model setup done\n");

    float radiusA = BrScalarToFloat(propA->radius);
    if (radiusA < 0.5f) radiusA = 1.0f;
    if (radiusA > 100.0f) radiusA = 100.0f;

    br_actor *world = BrActorAllocate(BR_ACTOR_NONE, 0);

    static br_camera cam_data;
    cam_data.identifier = (char *)"cam";
    cam_data.type = BR_CAMERA_PERSPECTIVE_FOV;
    cam_data.field_of_view = BR_ANGLE_DEG(45);
    cam_data.hither_z = BR_SCALAR(0.1);
    cam_data.yon_z = BR_SCALAR(1000.0);
    cam_data.aspect = BR_SCALAR(1.0);
    br_actor *cam = BrActorAdd(world, BrActorAllocate(BR_ACTOR_CAMERA, &cam_data));
    cam->t.type = BR_TRANSFORM_MATRIX34;
    /* Pulled back further than single-prop scene so all 5 fit in view. */
    BrMatrix34Translate(&cam->t.t.mat, BR_SCALAR(0), BR_SCALAR(0), BR_SCALAR(radiusA * 4.0));

    static br_light light_data;
    light_data.identifier = (char *)"l";
    light_data.type = BR_LIGHT_DIRECT;
    light_data.colour = BR_COLOUR_RGB(255, 255, 255);
    light_data.attenuation_c = BR_SCALAR(1);
    br_actor *light = BrActorAdd(world, BrActorAllocate(BR_ACTOR_LIGHT, &light_data));
    BrLightEnable(light);

    /* Five copies of the props in a loose cluster so they overlap and
     * intersect each other. Each at a different yaw and depth so the
     * z-buffer has to sort them per pixel. The instances and props
     * alternate so the overlap region mixes geometry and colour. */
    static struct
    {
        float x, y, z;
        long yaw_idx;
        int prop_idx;
    } layout[N_INSTANCES] = {
        { -0.6f,  0.0f,  0.0f, 0, 0 },
        {  0.6f,  0.0f,  0.0f, 1, 1 },
        {  0.0f, -0.4f,  0.7f, 2, 0 }, /* in front, between */
        {  0.0f,  0.4f, -0.5f, 3, 1 }, /* behind, between */
        {  0.0f,  0.0f,  0.0f, 4, 0 }, /* dead centre, intersects everything */
    };
    /* PROPS_OVERLAP_MASK env var: bitmask of which instances to render.
     * Default 0x1f = all 5. Use to bisect divergence. */
    int mask = 0x1f;
    if (const char *m = getenv("PROPS_OVERLAP_MASK"))
        mask = (int)strtol(m, NULL, 0);
    for (int i = 0; i < N_INSTANCES; i++)
    {
        if (!(mask & (1 << i))) continue;
        br_actor *a = BrActorAdd(world, BrActorAllocate(BR_ACTOR_MODEL, 0));
        a->model = (layout[i].prop_idx == 0) ? propA : propB;
        a->material = mats[i];
        a->t.type = BR_TRANSFORM_MATRIX34;
        BrMatrix34Identity(&a->t.t.mat);
        BrMatrix34RotateY(&a->t.t.mat, (br_angle)((long)layout[i].yaw_idx * 65536L / N_INSTANCES));
        a->t.t.mat.m[3][0] = BR_SCALAR(layout[i].x * radiusA);
        a->t.t.mat.m[3][1] = BR_SCALAR(layout[i].y * radiusA);
        a->t.t.mat.m[3][2] = BR_SCALAR(layout[i].z * radiusA);
    }

    fprintf(stderr, "  step: BrZbSceneRender\n");
    BrZbSceneRender(world, cam, colour, depth);
    fprintf(stderr, "  step: render done\n");

    FILE *f = fopen(out_path, "wb");
    if (!f) { perror(out_path); return 1; }
    fprintf(f, "P5\n%d %d\n255\n", WIDTH, HEIGHT);
    fwrite(colour->pixels, 1, (size_t)WIDTH * HEIGHT, f);
    fclose(f);

    /* Histogram per colour band so the diff is human-readable. Before
     * BrZbEnd / BrEnd because those free `colour`. */
    long hist[N_INSTANCES + 1] = { 0 };
    uint8_t *p = (uint8_t *)colour->pixels;
    for (int i = 0; i < WIDTH * HEIGHT; i++)
    {
        if (p[i] == 0) { hist[N_INSTANCES]++; continue; }
        for (int c = 0; c < N_INSTANCES; c++) if (p[i] == prop_colours[c]) { hist[c]++; break; }
    }
    fprintf(stderr, "props-overlap pixel histogram (lit pixels per colour):\n");
    for (int c = 0; c < N_INSTANCES; c++)
        fprintf(stderr, "  colour %3u : %ld\n", prop_colours[c], hist[c]);
    fprintf(stderr, "  background : %ld\n", hist[N_INSTANCES]);

    BrZbEnd();
    BrEnd();
    printf("ok: props-overlap -> %s\n", out_path);
    return 0;
}

/* Render TMPL 0x1020's two body parts as a composed actor. The
 * fixture was captured by `inspect-chunks dump-all TMPL 0x1020 ...`
 * (see tests/data/tmpl_1020/). This is the smallest 2-body-part
 * actor template in tmpls.3cn and exercises the multi-mesh-per-
 * actor render path that real actors use, without needing engine.lib
 * to actually parse the GG-format cel data.
 *
 * For the MVP comparison we render both BMDLs at identity transforms
 * (with part 1 offset along +X for visibility), distinct flat colours.
 * If the GLXF transforms turn out to matter for matching the real
 * studio render, we can extend the loader to parse them; for the
 * x86-vs-x64 cross-arch byte diff that's not necessary -- both arches
 * see the same hardcoded transforms. */
static int run_actor_tmpl1020(const char *out_path, const char *bmdl0_path, const char *bmdl1_path)
{
    BrBegin();

    br_pixelmap *colour = BrPixelmapAllocate(BR_PMT_INDEX_8, WIDTH, HEIGHT, 0, BR_PMAF_NORMAL);
    br_pixelmap *depth = BrPixelmapMatch(colour, BR_PMMATCH_DEPTH_16);
    if (!colour || !depth) { fprintf(stderr, "pixmap alloc failed\n"); return 1; }
    BrPixelmapFill(colour, 0);
    BrPixelmapFill(depth, 0xFFFF);

    BrZbBegin(colour->type, BR_PMT_DEPTH_16);

    static br_pixelmap shade;
    shade.identifier = (char *)"id-shade";
    shade.pixels = g_shade_id;
    shade.row_bytes = 256;
    shade.type = BR_PMT_INDEX_8;
    shade.width = 256;
    shade.height = 256;
    BrTableAdd(&shade);

    /* Use the 32x32 checker texture (g_tex / g_tex_pm, set up by
     * init_material) so the BMDL's UV-mapped sampling actually shows.
     * Without a real texture, every texel was the (0,0) pixel of a
     * 1x1 solid pixmap, painting the model a uniform colour blob. */
    init_material();
    BrMapAdd(&g_tex_pm);
    static br_material *mats[2];
    for (int i = 0; i < 2; i++)
    {
        mats[i] = BrMaterialAllocate((char *)"part-mat");
        mats[i]->colour_map = &g_tex_pm;
        mats[i]->index_shade = &shade;
        mats[i]->flags = BR_MATF_LIGHT | BR_MATF_SMOOTH;
        mats[i]->ka = BR_UFRACTION(0.20);
        mats[i]->kd = BR_UFRACTION(0.80);
        mats[i]->ks = BR_UFRACTION(0.00);
        mats[i]->opacity = 0xFF;
        mats[i]->index_base = 0;
        mats[i]->index_range = 255;
        BrMaterialAdd(mats[i]);
    }

    br_model *parts[2];
    parts[0] = load_bmdl_from_file(bmdl0_path);
    parts[1] = load_bmdl_from_file(bmdl1_path);
    if (!parts[0] || !parts[1]) { fprintf(stderr, "could not load body parts\n"); return 1; }
    BrModelAdd(parts[0]);
    BrModelAdd(parts[1]);
    BrModelUpdate(parts[0], BR_MODU_ALL);
    BrModelUpdate(parts[1], BR_MODU_ALL);

    /* Use the larger radius for camera framing. */
    float r0 = BrScalarToFloat(parts[0]->radius);
    float r1 = BrScalarToFloat(parts[1]->radius);
    float radius = (r0 > r1) ? r0 : r1;
    if (radius < 0.5f) radius = 1.0f;
    if (radius > 100.0f) radius = 100.0f;

    br_actor *world = BrActorAllocate(BR_ACTOR_NONE, 0);

    static br_camera cam_data;
    cam_data.identifier = (char *)"cam";
    cam_data.type = BR_CAMERA_PERSPECTIVE_FOV;
    cam_data.field_of_view = BR_ANGLE_DEG(45);
    cam_data.hither_z = BR_SCALAR(0.1);
    cam_data.yon_z = BR_SCALAR(1000.0);
    cam_data.aspect = BR_SCALAR(1.0);
    br_actor *cam = BrActorAdd(world, BrActorAllocate(BR_ACTOR_CAMERA, &cam_data));
    cam->t.type = BR_TRANSFORM_MATRIX34;
    BrMatrix34Translate(&cam->t.t.mat, BR_SCALAR(0), BR_SCALAR(0), BR_SCALAR(radius * 4.0));

    static br_light light_data;
    light_data.identifier = (char *)"l";
    light_data.type = BR_LIGHT_DIRECT;
    light_data.colour = BR_COLOUR_RGB(255, 255, 255);
    light_data.attenuation_c = BR_SCALAR(1);
    br_actor *light = BrActorAdd(world, BrActorAllocate(BR_ACTOR_LIGHT, &light_data));
    BrLightEnable(light);

    /* Part 0 at origin, part 1 offset along +X by ~radius so they are
     * visibly distinct in the dump. Identity rotations both. */
    for (int i = 0; i < 2; i++)
    {
        br_actor *a = BrActorAdd(world, BrActorAllocate(BR_ACTOR_MODEL, 0));
        a->model = parts[i];
        a->material = mats[i];
        a->t.type = BR_TRANSFORM_MATRIX34;
        BrMatrix34Identity(&a->t.t.mat);
        a->t.t.mat.m[3][0] = BR_SCALAR((i == 0) ? 0.0 : (radius * 0.6));
    }

    BrZbSceneRender(world, cam, colour, depth);

    FILE *f = fopen(out_path, "wb");
    if (!f) { perror(out_path); return 1; }
    fprintf(f, "P5\n%d %d\n255\n", WIDTH, HEIGHT);
    fwrite(colour->pixels, 1, (size_t)WIDTH * HEIGHT, f);
    fclose(f);

    /* The render is now textured, so a per-colour histogram isn't
     * meaningful. Instead just split background vs lit. */
    long lit = 0, bg = 0;
    uint8_t *p = (uint8_t *)colour->pixels;
    for (int i = 0; i < WIDTH * HEIGHT; i++)
        if (p[i] == 0) bg++; else lit++;
    fprintf(stderr, "actor-tmpl1020 lit=%ld bg=%ld\n", lit, bg);

    BrZbEnd();
    BrEnd();
    printf("ok: actor-tmpl1020 -> %s\n", out_path);
    return 0;
}

/* Render the cube N times into a single image, each instance translated
 * to its tile and rotated to a different yaw angle. Output bitmap is
 * the colour buffer. */
static int run_actor_render(const char *path, int n_angles)
{
    BrBegin();

    /* 256x256 INDEX_8 colour buffer + matching depth. */
    br_pixelmap *colour = BrPixelmapAllocate(BR_PMT_INDEX_8, WIDTH, HEIGHT, 0, BR_PMAF_NORMAL);
    if (!colour)
    {
        fprintf(stderr, "BrPixelmapAllocate(colour) failed\n");
        return 1;
    }
    br_pixelmap *depth = BrPixelmapMatch(colour, BR_PMMATCH_DEPTH_16);
    if (!depth)
    {
        fprintf(stderr, "BrPixelmapMatch(depth) failed\n");
        return 1;
    }
    BrPixelmapFill(colour, 0);
    BrPixelmapFill(depth, 0xFFFF);

    /* Renderer must be initialised BEFORE any Material/Model Add so the
     * registered backend can populate rptr / per-material state during
     * BrMaterialUpdate. zbtest.c follows the same order. */
    BrZbBegin(colour->type, BR_PMT_DEPTH_16);

    /* Texture pixmap pointing at our static checker buffer. */
    br_pixelmap tex = {};
    tex.identifier = (char *)"checker";
    tex.pixels = g_tex;
    tex.row_bytes = TEX_W;
    tex.type = BR_PMT_INDEX_8;
    tex.width = TEX_W;
    tex.height = TEX_H;
    BrMapAdd(&tex);

    /* Identity shade table pointing at our static buffer. */
    br_pixelmap shade = {};
    shade.identifier = (char *)"identity-shade";
    shade.pixels = g_shade_id;
    shade.row_bytes = 256;
    shade.type = BR_PMT_INDEX_8;
    shade.width = 256;
    shade.height = 256;
    BrTableAdd(&shade);

    br_material *mat = make_test_material(&tex);
    mat->index_shade = &shade;
    BrMaterialAdd(mat);

    br_model *cube = make_cube_model();
    BrModelAdd(cube);

    /* Build world: camera + N cube actors. */
    br_actor *world = BrActorAllocate(BR_ACTOR_NONE, 0);

    static br_camera cam_data;
    cam_data.identifier = (char *)"cam";
    cam_data.type = BR_CAMERA_PERSPECTIVE_FOV;
    cam_data.field_of_view = BR_ANGLE_DEG(45);
    cam_data.hither_z = BR_SCALAR(0.5);
    cam_data.yon_z = BR_SCALAR(50.0);
    cam_data.aspect = BR_SCALAR(1.0);
    br_actor *cam = BrActorAdd(world, BrActorAllocate(BR_ACTOR_CAMERA, &cam_data));
    BrMatrix34Translate(&cam->t.t.mat, BR_SCALAR(0), BR_SCALAR(0), BR_SCALAR(8));
    cam->t.type = BR_TRANSFORM_MATRIX34;

    /* One directional light from +Z. */
    static br_light light_data;
    light_data.identifier = (char *)"l";
    light_data.type = BR_LIGHT_DIRECT;
    light_data.colour = BR_COLOUR_RGB(255, 255, 255);
    light_data.attenuation_c = BR_SCALAR(1);
    br_actor *light = BrActorAdd(world, BrActorAllocate(BR_ACTOR_LIGHT, &light_data));
    BrLightEnable(light);

    /* N cube actors arranged horizontally, each at a different yaw. */
    int cols = n_angles;
    float spacing = 2.5f;
    for (int i = 0; i < n_angles; i++)
    {
        br_actor *a = BrActorAdd(world, BrActorAllocate(BR_ACTOR_MODEL, 0));
        a->model = cube;
        a->material = mat;
        a->t.type = BR_TRANSFORM_MATRIX34;
        BrMatrix34Identity(&a->t.t.mat);
        BrMatrix34RotateY(&a->t.t.mat, (br_angle)((long)i * 65536L / n_angles));
        float xoff = (i - (cols - 1) * 0.5f) * spacing;
        a->t.t.mat.m[3][0] = BR_SCALAR(xoff);
        a->t.t.mat.m[3][2] = BR_SCALAR(0);
    }

    BrZbSceneRender(world, cam, colour, depth);

    /* Save colour buffer as PGM. */
    FILE *f = fopen(path, "wb");
    if (!f)
    {
        perror(path);
        return 1;
    }
    fprintf(f, "P5\n%d %d\n255\n", WIDTH, HEIGHT);
    fwrite(colour->pixels, 1, (size_t)WIDTH * HEIGHT, f);
    fclose(f);

    BrZbEnd();
    BrEnd();
    printf("ok: actor-render -> %s\n", path);
    return 0;
}

static int save_pgm(const char *path)
{
    FILE *f = fopen(path, "wb");
    if (!f)
    {
        perror(path);
        return -1;
    }
    fprintf(f, "P5\n%d %d\n255\n", WIDTH, HEIGHT);
    fwrite(g_colour, 1, sizeof(g_colour), f);
    fclose(f);
    return 0;
}

int main(int argc, char **argv)
{
    const char *scene;

    if (argc < 3)
    {
        fprintf(stderr, "usage: %s <scene> <output.pgm|.txt>\n", argv[0]);
        fprintf(stderr, "scenes: tri-piz2i, tri-piz2tia, tri-piz2tia-rev,\n"
                        "        quad-cw, quad-ccw, tri-thin-vert, tri-thin-horz, cube-faces,\n"
                        "        tri-tilted-z, tilted-overlap,\n"
                        "        actor-cube-1, actor-cube-4 (full BrZbScene)\n"
                        "        fmac (writes .txt unit-test output -- not a PGM)\n");
        return 2;
    }

    /* fmac mode is a math unit test, not a render -- short-circuit before
     * setting up zb state and writing PGM. */
    if (strcmp(argv[1], "fmac") == 0)
        return run_fmac_unit(argv[2]);

    /* actor-* modes go through the full BrZbScene pipeline. They need
     * their own buffers (allocated by BrPixelmapAllocate), not our
     * static rasterizer buffers, so short-circuit here too. The texture
     * + shade table are reused from the static globals via init_buffers. */
    if (strncmp(argv[1], "actor-cube-", 11) == 0)
    {
        init_buffers();
        return run_actor_render(argv[2], atoi(argv[1] + 11));
    }

    /* prop-N <out> <bin> -- render N angles of a real 3DMM BMDL chunk
     * dumped via the extract-bmdl tool. */
    if (strncmp(argv[1], "prop-", 5) == 0)
    {
        if (argc < 4)
        {
            fprintf(stderr, "prop-N requires <out.pgm> <bmdl.bin>\n");
            return 2;
        }
        init_buffers();
        return run_prop_render(argv[2], argv[3], atoi(argv[1] + 5));
    }

    /* props-overlap <out> <bmdl-a> [bmdl-b] -- five intersecting copies
     * of one or two BMDL chunks, each in a distinct flat colour. */
    if (strcmp(argv[1], "props-overlap") == 0)
    {
        if (argc < 4)
        {
            fprintf(stderr, "props-overlap requires <out.pgm> <bmdl-a.bin> [bmdl-b.bin]\n");
            return 2;
        }
        init_buffers();
        return run_props_overlap(argv[2], argv[3], argc >= 5 ? argv[4] : NULL);
    }

    /* actor-tmpl1020 <out> <bmdl0> <bmdl1> -- render TMPL 0x1020's two
     * body parts side-by-side. Validates multi-mesh-per-actor across
     * x86 vs x64 using real 3DMM body-part geometry. */
    if (strcmp(argv[1], "actor-tmpl1020") == 0)
    {
        if (argc < 5)
        {
            fprintf(stderr, "actor-tmpl1020 requires <out.pgm> <bmdl0.bin> <bmdl1.bin>\n");
            return 2;
        }
        init_buffers();
        return run_actor_tmpl1020(argv[2], argv[3], argv[4]);
    }

    init_buffers();
    init_material();
    install_zb();

    scene = argv[1];
    if (strcmp(scene, "tri-piz2i") == 0)
        scene_tri_piz2i();
    else if (strcmp(scene, "tri-piz2tia") == 0)
        scene_tri_piz2tia();
    else if (strcmp(scene, "tri-piz2tia-rev") == 0)
        scene_tri_piz2tia_reversed();
    else if (strcmp(scene, "quad-cw") == 0)
        scene_quad_cw();
    else if (strcmp(scene, "quad-ccw") == 0)
        scene_quad_ccw();
    else if (strcmp(scene, "tri-thin-vert") == 0)
        scene_tri_thin_vertical();
    else if (strcmp(scene, "tri-thin-horz") == 0)
        scene_tri_thin_horizontal();
    else if (strcmp(scene, "cube-faces") == 0)
        scene_cube_faces();
    else if (strcmp(scene, "tri-tilted-z") == 0)
        scene_tri_tilted_z();
    else if (strcmp(scene, "tilted-overlap") == 0)
        scene_tilted_overlap();
    else
    {
        fprintf(stderr, "unknown scene: %s\n", scene);
        return 2;
    }

    if (save_pgm(argv[2]) != 0)
        return 1;

    printf("ok: %s -> %s\n", scene, argv[2]);
    return 0;
}
