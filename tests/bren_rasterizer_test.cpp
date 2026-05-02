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
#include <string.h>

/* These are declared in BRender's internal zbiproto.h with BR_ASM_CALL.
 * Rather than dragging that private header in we redeclare here -- the
 * linker resolves to the asm symbol on x86 and the stub/fallback on x64. */
extern "C" void BR_ASM_CALL TriangleRenderPIZ2I(struct temp_vertex_fixed *, struct temp_vertex_fixed *,
                                                struct temp_vertex_fixed *);
extern "C" void BR_ASM_CALL TriangleRenderPIZ2TIA(struct temp_vertex_fixed *, struct temp_vertex_fixed *,
                                                  struct temp_vertex_fixed *);

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
        fprintf(stderr, "usage: %s <scene> <output.pgm>\n", argv[0]);
        fprintf(stderr, "scenes: tri-piz2i, tri-piz2tia\n");
        return 2;
    }

    init_buffers();
    init_material();
    install_zb();

    scene = argv[1];
    if (strcmp(scene, "tri-piz2i") == 0)
        scene_tri_piz2i();
    else if (strcmp(scene, "tri-piz2tia") == 0)
        scene_tri_piz2tia();
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
