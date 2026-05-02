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
bool FReadBitmap(Filename *, uint8_t **, PDynamicArray *, long *, long *, bool *, uint8_t)
{
    return fFalse;
}
namespace BRender {
void World::AssertValid(unsigned long) {}
void World::AddActor(struct br_actor *) {}
void World::IterateActorsInPt(int (*)(struct br_actor *, struct br_model *, struct br_material *, struct br_vector3 *,
                                      struct br_vector3 *, long, long, void *),
                              void *, long, long)
{
}
}

#define WIDTH 1024
#define HEIGHT 1024

/* Walk a BACT tree depth-first, attaching every model under the world
 * actor with its accumulated transform. BRender handles the
 * hierarchical compose itself; we just need to add each part. */
static void add_bacts_to_scene(PBACT pbact_root, br_actor *world_actor, br_material *fallback_mat)
{
    if (!pbact_root) return;

    /* Body's BACTs are laid out flat in _prgbact (root, hilite, parts).
     * Their .parent linkage encodes the hierarchy. We add each one as
     * an actor under world_actor, preserving the parent chain via
     * BrActorAdd's parent argument. */
    for (PBACT pb = pbact_root; pb != NULL; pb = pb->next)
    {
        /* Recurse into siblings via .next; children via .children. */
        if (pb->type == BR_ACTOR_MODEL && pb->model)
        {
            br_actor *a = BrActorAdd(world_actor, BrActorAllocate(BR_ACTOR_MODEL, 0));
            a->model = pb->model;
            a->material = pb->material ? pb->material : fallback_mat;
            a->t = pb->t;
            /* TODO walk pb->children for nested parts. */
        }
        if (pb->children)
            add_bacts_to_scene(pb->children, world_actor, fallback_mat);
    }
}

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

    /* TODO: render via BrZbScene. For this iteration we just verify
     * the engine load worked end-to-end. The output PGM is empty for
     * now -- next iteration walks the BACT tree. */
    (void)argv[4];
    (void)add_bacts_to_scene;
    fprintf(stderr, "actor-render-test: load OK; render TODO\n");
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
