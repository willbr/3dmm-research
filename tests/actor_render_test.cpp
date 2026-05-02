/*
 * actor-render-test <chk-file> <tmpl-cno> <out.pgm>
 *
 * Loads a real 3DMM actor template via the engine's loaders
 * (Template::FReadTmpl, Template::PbodyCreate, Template::FSetActnCel)
 * and renders the rest pose. Unlike bren-rasterizer-test's actor-pose
 * scene (which hand-rolls the CelPartSpec / GLBS / GGCM / CMTL
 * binding logic and gets it wrong for accessories like hair), this
 * test calls the SAME engine code the studio uses. If the rendered
 * pose is wrong, the bug is in the engine, not in the test.
 *
 * Links engine-core + kauai-core + brender. Does NOT need the full
 * engine playback runtime (movie/scene/actor/tbox) -- those are in
 * the gui-side `engine` library and pull cmd/gob.
 *
 * Self-contained: provides its own WarnProc / FAssertProc CLI stubs.
 */

#include "soc.h"
ASSERTNAME

#include <stdio.h>

void WarnProc(schar *f, long l, schar *m)
{
    fprintf(stderr, "WARN %s:%ld %s\n", f ? f : "?", l, m ? m : "");
}
bool FAssertProc(schar *f, long l, schar *m, void *, long)
{
    fprintf(stderr, "ASSERT %s:%ld %s\n", f ? f : "?", l, m ? m : "");
    return false;
}

int __cdecl main(int argc, char **argv)
{
    if (argc != 4)
    {
        fprintf(stderr, "usage: actor-render-test <chk-file> <tmpl-cno> <out.pgm>\n");
        fprintf(stderr, "  Loads the TMPL chunk via the real engine loaders\n");
        fprintf(stderr, "  (Template::FReadTmpl + PbodyCreate + FSetActnCel) and\n");
        fprintf(stderr, "  renders the rest pose. Stub for now -- prints the\n");
        fprintf(stderr, "  Template's name and exits.\n");
        return 2;
    }

    String stnPath;
    Filename fni;
    PChunkyFile pcfl = pvNil;

    stnPath.SetSzs(argv[1]);
    if (!fni.FBuildFromPath(&stnPath))
    {
        fprintf(stderr, "actor-render-test: bad path '%s'\n", argv[1]);
        return 1;
    }

    pcfl = ChunkyFile::PcflOpen(&fni, fcflNil);
    if (pvNil == pcfl)
    {
        fprintf(stderr, "actor-render-test: cannot open '%s' as chunky file\n", argv[1]);
        return 1;
    }

    ChunkNumber cno = (ChunkNumber)strtoul(argv[2], NULL, 0);
    String stnName;
    if (pcfl->FGetName(kctgTmpl, cno, &stnName))
    {
        char buf[256] = {0};
        stnName.GetSzs(buf);
        printf("TMPL 0x%lx name: %s\n", (unsigned long)cno, buf);
    }
    else
    {
        fprintf(stderr, "actor-render-test: TMPL 0x%lx not found in %s\n", (unsigned long)cno, argv[1]);
        ReleasePpo(&pcfl);
        return 1;
    }

    /* TODO: wrap in ChunkyResourceFile, instantiate TagManager,
     * Template::FReadTmpl + PbodyCreate + FSetActnCel, walk Body's
     * BACT tree, render via BrZbSceneRender, write PGM/PPM. */
    fprintf(stderr, "actor-render-test: stub -- engine-core link verified, full render TODO\n");
    (void)argv[3];

    ReleasePpo(&pcfl);
    return 0;
}
