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
static void BR_CALLBACK NullFailure(char *) {}
static br_diaghandler g_NullDiag = { (char *)"test-null-diag", NullWarning, NullFailure };
br_diaghandler *_BrDefaultDiagHandler = &g_NullDiag;

static void *NullAlloc(br_size_t, br_uint_8) { return 0; }
static void NullFree(void *) {}
static br_size_t NullInquire(br_uint_8) { return 0; }
static br_allocator g_NullAllocator = { (char *)"test-null-alloc", NullAlloc, NullFree, NullInquire };
br_allocator *_BrDefaultAllocator = &g_NullAllocator;

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

/* Unit test for BrFixedFMac3 -- the FMac variants are 1.15-fraction *
 * 16.16-fixed accumulators (`movsx + imul + shrd 15` in the asm; the C
 * fallback shifts by 15 too -- a previous off-by-one shift of 16 was
 * the cause of x64 face-normal dot products coming out at half magnitude
 * and back-face culling rendering models inside-out).
 *
 * Writes one line per case to the supplied file in a deterministic
 * format. x86 (asm path) and x64 (C path) outputs must be byte-identical. */
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
    fclose(f);
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
                        "        fmac (writes .txt unit-test output -- not a PGM)\n");
        return 2;
    }

    /* fmac mode is a math unit test, not a render -- short-circuit before
     * setting up zb state and writing PGM. */
    if (strcmp(argv[1], "fmac") == 0)
        return run_fmac_unit(argv[2]);

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
