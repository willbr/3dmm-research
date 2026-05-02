/*
 * inspect-chunks <chk-file> <command> [args...]
 *
 * Reflection / debug walker for 3DMM chunky files (.chk / .3MM / .3CN /
 * .3TH etc.). Useful for mapping an unfamiliar chunky file's graph
 * before writing a loader -- the alternative is the GUI ched, which
 * isn't scriptable.
 *
 * Commands:
 *   list                              List every chunk: ctg cno size name.
 *   list-ctg <CTG>                    List chunks of one type (e.g. "TMPL").
 *   children <CTG> <CNO>              List children of one chunk by chid.
 *   dump <CTG> <CNO> <out>            Dump unpacked bytes of one chunk.
 *
 * <CTG> is a 4-character chunk type tag (e.g. TMPL, BMDL, GLPI). It is
 * passed as a 4-char string and packed into a ChunkTagOrType ('TMPL' is
 * 0x544D504C). <CNO> is a hex chunk number (0xNN or NN).
 *
 * Self-contained tool that links engine.lib (and transitively kauai +
 * brender + audioman). Same harness as extract-bmdl (FrameMain stub +
 * Mcp_FEnabledFromCmdLine stub) so the kauai WinMain bootstrap is
 * satisfied without dragging in studio/MCP. EXCLUDE_FROM_ALL.
 */

#include "kauai_core.h"
#include <ctype.h>

ASSERTNAME

/* CLI-only stubs of kauai's assertion callbacks. See extract_bmdl.cpp
 * for rationale -- print to stderr instead of popping a dialog. */
void WarnProc(schar *pszsFile, long lwLine, schar *pszsMsg)
{
    fprintf(stderr, "WARN %s:%ld %s\n", pszsFile ? pszsFile : "?", lwLine, pszsMsg ? pszsMsg : "");
}
bool FAssertProc(schar *pszsFile, long lwLine, schar *pszsMsg, void *, long)
{
    fprintf(stderr, "ASSERT %s:%ld %s\n", pszsFile ? pszsFile : "?", lwLine, pszsMsg ? pszsMsg : "");
    return false;
}

/* Pack a 4-char tag string ("TMPL") into a ChunkTagOrType (0x544D504C).
 * The original 3DMM source defines kctg* via 4-char-literal multibyte
 * char constants; that gives the same packing as MakeLong(big-endian),
 * so we replicate that here byte-for-byte. */
static ChunkTagOrType PackCtg(const char *s)
{
    ChunkTagOrType ctg = 0;
    for (int i = 0; i < 4; i++)
    {
        char c = s[i] ? s[i] : ' ';
        ctg = (ctg << 8) | (unsigned char)c;
    }
    return ctg;
}

static void UnpackCtg(ChunkTagOrType ctg, char out[5])
{
    out[0] = (char)((ctg >> 24) & 0xFF);
    out[1] = (char)((ctg >> 16) & 0xFF);
    out[2] = (char)((ctg >> 8) & 0xFF);
    out[3] = (char)(ctg & 0xFF);
    out[4] = 0;
    /* Replace non-printables with '?' so list output stays readable. */
    for (int i = 0; i < 4; i++)
        if (out[i] < 32 || out[i] > 126)
            out[i] = '?';
}

static ChunkNumber ParseCno(const char *s)
{
    return (ChunkNumber)strtoul(s, NULL, 0);
}

static int CmdListAll(PChunkyFile pcfl)
{
    long ccki = pcfl->Ccki();
    fprintf(stderr, "inspect-chunks: %ld total chunks\n", ccki);
    for (long i = 0; i < ccki; i++)
    {
        ChunkIdentification cki;
        long ckid = 0;
        DataBlock blck;
        if (!pcfl->FGetCki(i, &cki, &ckid, &blck))
            continue;
        char tag[5];
        UnpackCtg(cki.ctg, tag);
        printf("%4ld  %s  cno=0x%08lx  kids=%ld  size=%ld\n", i, tag, (unsigned long)cki.cno, ckid, blck.Cb());
    }
    return 0;
}

static int CmdListCtg(PChunkyFile pcfl, ChunkTagOrType ctg)
{
    long ccki = pcfl->CckiCtg(ctg);
    char tag[5];
    UnpackCtg(ctg, tag);
    fprintf(stderr, "inspect-chunks: %ld %s chunks\n", ccki, tag);
    for (long i = 0; i < ccki; i++)
    {
        ChunkIdentification cki;
        long ckid = 0;
        DataBlock blck;
        if (!pcfl->FGetCkiCtg(ctg, i, &cki, &ckid, &blck))
            continue;
        printf("%4ld  %s  cno=0x%08lx  kids=%ld  size=%ld\n", i, tag, (unsigned long)cki.cno, ckid, blck.Cb());
    }
    return 0;
}

static int CmdChildren(PChunkyFile pcfl, ChunkTagOrType ctg, ChunkNumber cno)
{
    long ckid = pcfl->Ckid(ctg, cno);
    char tag[5];
    UnpackCtg(ctg, tag);
    fprintf(stderr, "inspect-chunks: %s 0x%lx has %ld children\n", tag, (unsigned long)cno, ckid);
    for (long i = 0; i < ckid; i++)
    {
        ChildChunkIdentification kid;
        if (!pcfl->FGetKid(ctg, cno, i, &kid))
            continue;
        char ktag[5];
        UnpackCtg(kid.cki.ctg, ktag);
        printf("%4ld  %s  cno=0x%08lx  chid=0x%08lx\n", i, ktag, (unsigned long)kid.cki.cno, (unsigned long)kid.chid);
    }
    return 0;
}

static int CmdDump(PChunkyFile pcfl, ChunkTagOrType ctg, ChunkNumber cno, const char *out_path)
{
    DataBlock blck;
    ChunkIdentification cki;
    void *pv = NULL;
    FILE *out = NULL;
    int rc = 1;
    long cb = 0;

    if (!pcfl->FFind(ctg, cno, &blck))
    {
        char tag[5];
        UnpackCtg(ctg, tag);
        fprintf(stderr, "inspect-chunks: chunk %s 0x%lx not found\n", tag, (unsigned long)cno);
        goto LDone;
    }
    if (!blck.FUnpackData())
    {
        fprintf(stderr, "inspect-chunks: FUnpackData failed\n");
        goto LDone;
    }
    cb = blck.Cb();
    fprintf(stderr, "inspect-chunks: unpacked size=%ld bytes\n", cb);

    if (!FAllocPv(&pv, cb, fmemNil, mprNormal))
    {
        fprintf(stderr, "inspect-chunks: out of memory\n");
        goto LDone;
    }
    if (!blck.FReadRgb(pv, cb, 0))
    {
        fprintf(stderr, "inspect-chunks: FReadRgb failed\n");
        goto LDone;
    }
    out = fopen(out_path, "wb");
    if (!out)
    {
        perror(out_path);
        goto LDone;
    }
    fwrite(pv, 1, (size_t)cb, out);
    fclose(out);
    out = NULL;
    fprintf(stderr, "inspect-chunks: wrote %s\n", out_path);
    rc = 0;

LDone:
    if (out) fclose(out);
    if (pv) FreePpv(&pv);
    (void)cki; /* silence unused-var if compiler whines */
    return rc;
}

/* For an actor template, emit one line per body part:
 *   <chid> BMDL=<cno> TMAP=<cno>
 * resolved by walking TMPL -> CMTL chid=i -> MTRL chid=0 -> TMAP chid=0
 * (the default-costume material for body part i). BMDL is the TMPL's
 * child at chid=i with ctg=BMDL. The result is stable enough to use
 * as a manifest for the bren-rasterizer-test actor-pose scene. */
static int CmdActorManifest(PChunkyFile pcfl, ChunkTagOrType tmpl_ctg, ChunkNumber tmpl_cno)
{
    long n_kids = pcfl->Ckid(tmpl_ctg, tmpl_cno);
    int max_chid = -1;
    for (long i = 0; i < n_kids; i++)
    {
        ChildChunkIdentification kid;
        if (pcfl->FGetKid(tmpl_ctg, tmpl_cno, i, &kid) && (long)kid.chid > max_chid)
            max_chid = (long)kid.chid;
    }
    int n_parts = 0;
    for (int chid = 0; chid <= max_chid; chid++)
    {
        ChildChunkIdentification kid_bmdl, kid_cmtl;
        ChunkTagOrType ctg_bmdl = 'BMDL';
        ChunkTagOrType ctg_cmtl = 'CMTL';
        ChunkTagOrType ctg_mtrl = 'MTRL';
        ChunkTagOrType ctg_tmap = 'TMAP';
        if (!pcfl->FGetKidChidCtg(tmpl_ctg, tmpl_cno, (ChildChunkID)chid, ctg_bmdl, &kid_bmdl))
            continue;
        if (!pcfl->FGetKidChidCtg(tmpl_ctg, tmpl_cno, (ChildChunkID)chid, ctg_cmtl, &kid_cmtl))
        {
            printf("%d BMDL=0x%08lx TMAP=NONE\n", chid, (unsigned long)kid_bmdl.cki.cno);
            n_parts++;
            continue;
        }
        ChildChunkIdentification kid_mtrl;
        if (!pcfl->FGetKidChidCtg(ctg_cmtl, kid_cmtl.cki.cno, 0, ctg_mtrl, &kid_mtrl))
        {
            printf("%d BMDL=0x%08lx TMAP=NONE\n", chid, (unsigned long)kid_bmdl.cki.cno);
            n_parts++;
            continue;
        }
        ChildChunkIdentification kid_tmap;
        if (!pcfl->FGetKidChidCtg(ctg_mtrl, kid_mtrl.cki.cno, 0, ctg_tmap, &kid_tmap))
        {
            printf("%d BMDL=0x%08lx TMAP=NONE\n", chid, (unsigned long)kid_bmdl.cki.cno);
            n_parts++;
            continue;
        }
        printf("%d BMDL=0x%08lx TMAP=0x%08lx\n", chid, (unsigned long)kid_bmdl.cki.cno,
               (unsigned long)kid_tmap.cki.cno);
        n_parts++;
    }
    fprintf(stderr, "inspect-chunks: emitted manifest for %d body parts\n", n_parts);
    return 0;
}

/* Print the per-chunk name (kauai's chunky-file format stores an
 * optional UTF8/SBCS name string alongside each chunk). 3DMM uses
 * these as actor template names, palette names, etc. */
static int CmdName(PChunkyFile pcfl, ChunkTagOrType ctg, ChunkNumber cno)
{
    String stn;
    char tag[5];
    UnpackCtg(ctg, tag);
    if (!pcfl->FGetName(ctg, cno, &stn))
    {
        fprintf(stderr, "inspect-chunks: %s 0x%lx has no name\n", tag, (unsigned long)cno);
        return 1;
    }
    char buf[256] = {0};
    stn.GetSzs(buf);
    printf("%s\n", buf);
    return 0;
}

/* Recursively walk one chunk's parent->child DAG and dump every reached
 * chunk to <out_dir>/<CTG>_<cno>.bin. Flat directory layout (no nested
 * subdirs): the chunky graph is a DAG, not a tree, so a chunk reached
 * via two paths only gets dumped once. Tracks visited (ctg, cno) pairs
 * in a sorted array probed linearly -- TMPL subgraphs are small. */
static int CmdDumpAll(PChunkyFile pcfl, ChunkTagOrType ctg, ChunkNumber cno, const char *out_dir)
{
    /* Visited set as a fixed-size array. 4096 entries is plenty for any
     * 3DMM TMPL graph (the largest TMPL has ~322 children). */
    enum { kMaxVisited = 4096 };
    static struct
    {
        ChunkTagOrType ctg;
        ChunkNumber cno;
    } visited[kMaxVisited];
    int n_visited = 0;

    /* Worklist (BFS), same fixed cap. */
    static struct
    {
        ChunkTagOrType ctg;
        ChunkNumber cno;
    } work[kMaxVisited];
    int work_head = 0, work_tail = 0;

    work[work_tail++] = { ctg, cno };

    long n_dumped = 0;
    while (work_head < work_tail)
    {
        ChunkTagOrType cctg = work[work_head].ctg;
        ChunkNumber ccno = work[work_head].cno;
        work_head++;

        /* Skip if already visited. */
        bool seen = false;
        for (int i = 0; i < n_visited; i++)
            if (visited[i].ctg == cctg && visited[i].cno == ccno) { seen = true; break; }
        if (seen) continue;
        if (n_visited >= kMaxVisited)
        {
            fprintf(stderr, "inspect-chunks: dump-all visited cap (%d) hit\n", kMaxVisited);
            return 1;
        }
        visited[n_visited].ctg = cctg;
        visited[n_visited].cno = ccno;
        n_visited++;

        /* Dump this chunk. */
        char tag[5];
        UnpackCtg(cctg, tag);
        char path[1024];
        snprintf(path, sizeof(path), "%s/%s_%08lx.bin", out_dir, tag, (unsigned long)ccno);
        if (CmdDump(pcfl, cctg, ccno, path) != 0)
            return 1;
        n_dumped++;

        /* Enqueue children. */
        long ckid = pcfl->Ckid(cctg, ccno);
        for (long i = 0; i < ckid; i++)
        {
            ChildChunkIdentification kid;
            if (!pcfl->FGetKid(cctg, ccno, i, &kid))
                continue;
            if (work_tail >= kMaxVisited)
            {
                fprintf(stderr, "inspect-chunks: dump-all worklist cap hit\n");
                return 1;
            }
            work[work_tail].ctg = kid.cki.ctg;
            work[work_tail].cno = kid.cki.cno;
            work_tail++;
        }
    }
    fprintf(stderr, "inspect-chunks: dump-all wrote %ld chunks under %s\n", n_dumped, out_dir);
    return 0;
}

static void Usage(void)
{
    fprintf(stderr,
            "usage: inspect-chunks <chk-file> <command> [args]\n"
            "  list                          List every chunk\n"
            "  list-ctg <CTG>                List chunks of type CTG (4 chars, e.g. TMPL)\n"
            "  children <CTG> <CNO>          List children of one chunk\n"
            "  dump <CTG> <CNO> <out>        Dump unpacked bytes to file\n"
            "  dump-all <CTG> <CNO> <dir>    Recursively dump CTG/CNO and every reachable\n"
            "                                  child to dir/<TAG>_<cno>.bin (flat, dedup'd)\n"
            "<CNO> is hex (0xNN or NN).\n");
}

int __cdecl main(int argc, char **argv)
{
    if (argc < 3)
    {
        Usage();
        return 2;
    }

    String stnPath;
    Filename fni;
    PChunkyFile pcfl = pvNil;
    int rc = 1;

    stnPath.SetSzs(argv[1]);
    if (!fni.FBuildFromPath(&stnPath))
    {
        fprintf(stderr, "inspect-chunks: bad path '%s'\n", argv[1]);
        return 1;
    }

    pcfl = ChunkyFile::PcflOpen(&fni, fcflNil);
    if (pvNil == pcfl)
    {
        fprintf(stderr, "inspect-chunks: cannot open '%s' as chunky file\n", argv[1]);
        return 1;
    }

    const char *cmd = argv[2];
    if (strcmp(cmd, "list") == 0 && argc == 3)
    {
        rc = CmdListAll(pcfl);
    }
    else if (strcmp(cmd, "list-ctg") == 0 && argc == 4)
    {
        rc = CmdListCtg(pcfl, PackCtg(argv[3]));
    }
    else if (strcmp(cmd, "children") == 0 && argc == 5)
    {
        rc = CmdChildren(pcfl, PackCtg(argv[3]), ParseCno(argv[4]));
    }
    else if (strcmp(cmd, "dump") == 0 && argc == 6)
    {
        rc = CmdDump(pcfl, PackCtg(argv[3]), ParseCno(argv[4]), argv[5]);
    }
    else if (strcmp(cmd, "dump-all") == 0 && argc == 6)
    {
        rc = CmdDumpAll(pcfl, PackCtg(argv[3]), ParseCno(argv[4]), argv[5]);
    }
    else if (strcmp(cmd, "name") == 0 && argc == 5)
    {
        rc = CmdName(pcfl, PackCtg(argv[3]), ParseCno(argv[4]));
    }
    else if (strcmp(cmd, "actor-manifest") == 0 && argc == 5)
    {
        rc = CmdActorManifest(pcfl, PackCtg(argv[3]), ParseCno(argv[4]));
    }
    else
    {
        Usage();
        rc = 2;
    }

    ReleasePpo(&pcfl);
    return rc;
}
