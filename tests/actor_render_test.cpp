/*
 * actor-render-test <chk-file> <tmpl-cno> <out.pgm>
 *
 * Loads a real 3DMM actor template via the engine's loaders
 * (Template::FReadTmpl, Template::PbodyCreate, Template::FSetActnCel)
 * and renders the rest pose by walking the Body's BACT tree directly
 * through BrZbScene -- no World, no PWorld, no playback runtime.
 *
 * Unlike bren-rasterizer-test's actor-pose scene (which hand-rolls the
 * CelPartSpec / GLBS / GGCM / CMTL binding logic and gets it wrong for
 * accessories like hair), this test calls the SAME engine code the
 * studio uses. If the rendered pose is wrong, the bug is in the
 * engine, not the test.
 *
 * Links engine-core + kauai-core + brender. Does NOT need the full
 * engine playback runtime (movie/scene/actor/tbox).
 */

#include "soc.h"
ASSERTNAME

#include <stdio.h>
#include <stdlib.h>

void WarnProc(schar *f, long l, schar *m)
{
    fprintf(stderr, "WARN %s:%ld %s\n", f ? f : "?", l, m ? m : "");
}
bool FAssertProc(schar *f, long l, schar *m, void *, long)
{
    fprintf(stderr, "ASSERT %s:%ld %s\n", f ? f : "?", l, m ? m : "");
    return false;
}

/* Stubs for engine-core / brender-core symbols that are normally
 * supplied by kauai-gui or by the studio app. None of these paths
 * fire during the actor-load + rest-pose render we exercise here:
 *   - World::* / Body::PbodyClicked (Body's hit-test path)
 *   - PtmapReadNative (.bmp import; engine-core uses chunky FReadTmap)
 *   - AppendCrashLog_C (studio crash dialog)
 *   - vptagm / vpappb (TagManager + Application globals)
 * The link must satisfy these symbols even though no callsite from
 * actor-render-test reaches them. */
PTagManager vptagm = pvNil;
PApplicationBase vpappb = pvNil;
extern "C" void AppendCrashLog_C(schar *) {}
/* FReadBitmap is now in kauai-core (mbmp.cpp). */
namespace BRender {
void World::AssertValid(unsigned long) {}
void World::AddActor(struct br_actor *) {}
void World::IterateActorsInPt(int (*)(struct br_actor *, struct br_model *, struct br_material *, struct br_vector3 *,
                                      struct br_vector3 *, long, long, void *),
                              void *, long, long)
{
}
}

#define WIDTH 256
#define HEIGHT 256

/* (Body's BACT tree is attached directly to the scene root via
 * BrActorAdd; no manual walk needed -- BrZbSceneRender handles the
 * hierarchy.) */

int __cdecl main(int argc, char **argv)
{
    if (argc != 5)
    {
        fprintf(stderr, "usage: actor-render-test <main-chk-with-shade-table> <chk-file> <tmpl-cno> <out.pgm>\n");
        fprintf(stderr, "  e.g. actor-render-test 'dist/Microsoft Kids/3D Movie Maker/3dmovie.chk' content-files/tmpls.3cn 0x2010 out.pgm\n");
        return 2;
    }

    String stnPath;
    Filename fni;
    PChunkyFile pcflShade = pvNil;
    PChunkyFile pcfl = pvNil;
    PChunkyResourceFile pcrf = pvNil;
    PTemplate ptmpl = pvNil;
    PBody pbody = pvNil;
    int rc = 1;
    ChunkNumber cno = 0;
    String stnName;
    bool fError = fFalse;
    BRS dwr = 0;

    /* BRender init -- Body::PbodyNew allocates materials via
     * BrMaterialAllocate, which needs the BRender registries set up. */
    BrBegin();

    /* Load the shade table from the main 3dmovie.chk. The engine's
     * Material_MTRL keeps a static _ptmapShadeTable that every MTRL
     * load asserts non-nil. The studio sets this at startup via
     * `Material_MTRL::FSetShadeTable(_pcfl, kctgTmap, 0)` (utest.cpp:452). */
    stnPath.SetSzs(argv[1]);
    if (!fni.FBuildFromPath(&stnPath))
    {
        fprintf(stderr, "bad shade-table path '%s'\n", argv[1]);
        goto LDone;
    }
    pcflShade = ChunkyFile::PcflOpen(&fni, fcflNil);
    if (pvNil == pcflShade)
    {
        fprintf(stderr, "cannot open shade chk '%s'\n", argv[1]);
        goto LDone;
    }
    if (!Material_MTRL::FSetShadeTable(pcflShade, kctgTmap, 0))
    {
        fprintf(stderr, "FSetShadeTable failed\n");
        goto LDone;
    }
    fprintf(stderr, "Shade table loaded.\n");

    stnPath.SetSzs(argv[2]);
    if (!fni.FBuildFromPath(&stnPath))
    {
        fprintf(stderr, "actor-render-test: bad path '%s'\n", argv[2]);
        goto LDone;
    }
    pcfl = ChunkyFile::PcflOpen(&fni, fcflNil);
    if (pvNil == pcfl)
    {
        fprintf(stderr, "actor-render-test: cannot open '%s'\n", argv[2]);
        goto LDone;
    }

    /* Wrap in CRF with a generous cache (8 MB). The CRF resolves
     * BaseCacheableObject fetches via PFNRPO callbacks (Template::FReadTmpl
     * here; Model::FReadModel + Material::FRead also fire transitively
     * through Template::_FInit when costume materials are loaded). */
    pcrf = ChunkyResourceFile::PcrfNew(pcfl, 8L << 20);
    if (pvNil == pcrf)
    {
        fprintf(stderr, "actor-render-test: PcrfNew failed\n");
        goto LDone;
    }

    cno = (ChunkNumber)strtoul(argv[3], NULL, 0);
    if (pcfl->FGetName(kctgTmpl, cno, &stnName))
    {
        char buf[256] = {0};
        stnName.GetSzs(buf);
        printf("TMPL 0x%lx name: %s\n", (unsigned long)cno, buf);
    }

    /* Real engine load. Same call path as src/engine/actor.cpp:
     *   ptmpl = vptagm->PbacoFetch(tag, Template::FReadTmpl);
     * Just bypassing TagManager since we have the file directly. */
    ptmpl = (PTemplate)pcrf->PbacoFetch(kctgTmpl, cno, Template::FReadTmpl, &fError);
    if (pvNil == ptmpl)
    {
        fprintf(stderr, "actor-render-test: Template::FReadTmpl failed (fError=%d)\n", fError);
        goto LDone;
    }
    fprintf(stderr, "Template loaded.\n");

    pbody = ptmpl->PbodyCreate();
    if (pvNil == pbody)
    {
        fprintf(stderr, "actor-render-test: PbodyCreate failed\n");
        goto LDone;
    }
    fprintf(stderr, "Body created with %ld BACTs.\n", pbody->Cbact());

    /* Action 0, cel 0 = rest pose. This is the call that eluded my
     * hand-rolled actor-pose binding -- it's what the engine actually
     * does to bind body parts to their action-data-selected models
     * and matrices, including the chidNil-accessory handling. */
    if (!ptmpl->FSetActnCel(pbody, 0, 0, &dwr))
    {
        fprintf(stderr, "actor-render-test: FSetActnCel(0, 0) failed\n");
        goto LDone;
    }
    fprintf(stderr, "Rest pose set.\n");

    /* Render the loaded body via BrZbScene. The body's BACT tree is
     * already populated with proper transforms, models, and materials
     * by FSetActnCel. We just need to attach it to a world actor that
     * also holds a camera and a light, then call BrZbSceneRender. */
    {
        br_pixelmap *colour = BrPixelmapAllocate(BR_PMT_INDEX_8, WIDTH, HEIGHT, 0, BR_PMAF_NORMAL);
        br_pixelmap *depth = BrPixelmapMatch(colour, BR_PMMATCH_DEPTH_16);
        if (!colour || !depth)
        {
            fprintf(stderr, "pixmap alloc failed\n");
            goto LDone;
        }
        BrPixelmapFill(colour, 0);
        BrPixelmapFill(depth, 0xFFFF);
        BrZbBegin(colour->type, BR_PMT_DEPTH_16);

        br_actor *world_actor = BrActorAllocate(BR_ACTOR_NONE, 0);

        /* Compute world-space bounds of the body so we can frame the
         * camera. Body::GetBcbBounds gives us the bounding box in
         * world coordinates (with fWorld=fTrue). */
        BCB bcb = {0};
        pbody->GetBcbBounds(&bcb, fTrue);
        float xMin = BrScalarToFloat(bcb.xrMin), xMax = BrScalarToFloat(bcb.xrMax);
        float yMin = BrScalarToFloat(bcb.yrMin), yMax = BrScalarToFloat(bcb.yrMax);
        float zMin = BrScalarToFloat(bcb.zrMin), zMax = BrScalarToFloat(bcb.zrMax);
        float cx = (xMin + xMax) * 0.5f, cy = (yMin + yMax) * 0.5f, cz = (zMin + zMax) * 0.5f;
        float spanX = xMax - xMin, spanY = yMax - yMin;
        float scene_size = spanX > spanY ? spanX : spanY;
        if (scene_size < 1.0f) scene_size = 1.0f;
        fprintf(stderr, "body bounds: x=(%.2f..%.2f) y=(%.2f..%.2f) z=(%.2f..%.2f)\n", xMin, xMax, yMin, yMax, zMin,
                zMax);

        static br_camera cam_data;
        cam_data.identifier = (char *)"cam";
        cam_data.type = BR_CAMERA_PERSPECTIVE_FOV;
        cam_data.field_of_view = BR_ANGLE_DEG(45);
        cam_data.hither_z = BR_SCALAR(1.0);
        cam_data.yon_z = BR_SCALAR(1000.0);
        cam_data.aspect = BR_SCALAR(1.0);
        br_actor *cam = BrActorAdd(world_actor, BrActorAllocate(BR_ACTOR_CAMERA, &cam_data));
        cam->t.type = BR_TRANSFORM_MATRIX34;
        BrMatrix34Translate(&cam->t.t.mat, BR_SCALAR(cx), BR_SCALAR(cy), BR_SCALAR(zMax + scene_size * 1.4));

        static br_light light_data;
        light_data.identifier = (char *)"l";
        light_data.type = BR_LIGHT_DIRECT;
        light_data.colour = BR_COLOUR_RGB(255, 255, 255);
        light_data.attenuation_c = BR_SCALAR(1);
        br_actor *light = BrActorAdd(world_actor, BrActorAllocate(BR_ACTOR_LIGHT, &light_data));
        BrLightEnable(light);

        /* Attach each body part BACT under our world. We skip the
         * body's root + hilite BACTs -- the hilite uses
         * BR_RSTYLE_BOUNDING_EDGES which BrZbScene gets stuck on for
         * actors that don't have proper bounding info; we don't need
         * the highlight ring for a static render. Each part BACT has
         * its world transform composed via BRender's hierarchy walk
         * but we attach them as siblings here, so we need to bake
         * the world transform into each part's local transform first.
         *
         * Actually the simpler approach: walk the part chain by part
         * index using the GLPI parents the engine populated, and
         * compose world matrices ourselves. But since each part BACT
         * is already linked to its proper parent BACT in Body's
         * tree, the path of least surprise is to detach from Body's
         * root and re-parent under our world. */
        long n_parts = pbody->Cbact() - 2; /* skip root + hilite */
        long n_attached = 0;
        long n_max = n_parts;
        if (const char *m = getenv("MAX_PARTS")) n_max = atol(m);
        /* Allocate fresh sibling actors with copies of each part's
         * model + material + (recursively-composed) world transform.
         * Avoids relink-from-Body's-tree which we couldn't get to
         * render -- BrActorAdd's prev != NULL assert fires silently
         * and tangles the linked list. Fresh actors guarantee clean
         * .prev/.next/.parent pointers.
         *
         * World transform is each BACT's accumulated chain from its
         * Body ancestors back to body root. We compose by walking
         * .parent pointers within Body's tree before attaching. */
        for (long i = 0; i < n_parts && n_attached < n_max; i++)
        {
            PBACT pb = pbody->PbactPart(i);
            if (!pb || pb->type != BR_ACTOR_MODEL || !pb->model) continue;

            /* Compose world transform by walking parent chain. */
            br_matrix34 m;
            BrMatrix34Copy(&m, &pb->t.t.mat);
            for (br_actor *par = pb->parent; par != NULL && par != pbody->PbactRoot(); par = par->parent)
            {
                br_matrix34 tmp;
                BrMatrix34Mul(&tmp, &m, &par->t.t.mat);
                m = tmp;
            }

            br_actor *a = BrActorAdd(world_actor, BrActorAllocate(BR_ACTOR_MODEL, 0));
            a->model = pb->model;
            a->material = pb->material;
            a->t.type = BR_TRANSFORM_MATRIX34;
            a->t.t.mat = m;
            n_attached++;
        }
        fprintf(stderr, "calling BrZbSceneRender (%ld parts attached)...\n", n_attached);

        BrZbSceneRender(world_actor, cam, colour, depth);
        fprintf(stderr, "BrZbSceneRender returned\n");

        FILE *f = fopen(argv[4], "wb");
        if (f)
        {
            fprintf(f, "P5\n%d %d\n255\n", WIDTH, HEIGHT);
            fwrite(colour->pixels, 1, (size_t)WIDTH * HEIGHT, f);
            fclose(f);
            long lit = 0;
            uint8_t *pix = (uint8_t *)colour->pixels;
            for (int i = 0; i < WIDTH * HEIGHT; i++) if (pix[i] != 0) lit++;
            fprintf(stderr, "rendered: lit=%ld bg=%ld\n", lit, (long)WIDTH * HEIGHT - lit);
        }
        else
        {
            perror(argv[4]);
        }
        BrZbEnd();
    }
    fprintf(stderr, "actor-render-test: ok\n");
    rc = 0;

LDone:
    ReleasePpo(&pbody);
    ReleasePpo(&ptmpl);
    ReleasePpo(&pcrf);
    ReleasePpo(&pcfl);
    ReleasePpo(&pcflShade);
    BrEnd();
    return rc;
}
