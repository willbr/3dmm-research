/*
 * extract-bmdl <chk-file> <bmdl-index> <out-file>
 *
 * Pulls the Nth 'BMDL' chunk out of a 3DMM chunky file (.chk / .3MM /
 * .3CN / .3TH / etc.), unpacks it via DataBlock::FUnpackData, and writes
 * the raw bytes to out-file. The dumped bytes match exactly what
 * Model::_FInit reads from the DataBlock at runtime: a ModelOnFile
 * header, an array of br_vertex (32 bytes each, layout identical x86 and
 * x64), then an array of BrFaceOnFile (36 bytes each, wire format).
 *
 * Used by the bren-rasterizer-test prop-render scene to load a real 3DMM
 * model and compare x86 (asm) vs x64 (C fallback) renders byte-for-byte.
 *
 * Self-contained tool that links engine.lib (and transitively kauai +
 * brender + audioman). Exits non-zero on any failure.
 */

#include "frame.h"
#include "soc.h" /* kctgBmdl */

ASSERTNAME

void __cdecl FrameMain(void)
{
}

/* kauai's appbwin.cpp WinMain references this; the studio provides it
 * via mcpserv.cpp but a CLI tool has no MCP server. Stub it out. */
extern "C" int Mcp_FEnabledFromCmdLine(const char *)
{
    return 0;
}

int __cdecl main(int argc, char **argv)
{
    if (argc != 4)
    {
        fprintf(stderr, "usage: extract-bmdl <chk-file> <bmdl-index> <out-file>\n");
        fprintf(stderr, "  Dumps the Nth 'BMDL' chunk's unpacked bytes to out-file.\n");
        return 2;
    }

    String stnPath;
    Filename fni;
    PChunkyFile pcfl = pvNil;
    DataBlock blck;
    ChunkIdentification cki;
    long ibmdl = atoi(argv[2]);
    long ccki = 0;
    long cb = 0;
    void *pv = NULL;
    FILE *out = NULL;
    int rc = 1;

    stnPath.SetSzs(argv[1]);
    if (!fni.FBuildFromPath(&stnPath))
    {
        fprintf(stderr, "extract-bmdl: bad path '%s'\n", argv[1]);
        return 1;
    }

    pcfl = ChunkyFile::PcflOpen(&fni, fcflNil);
    if (pvNil == pcfl)
    {
        fprintf(stderr, "extract-bmdl: cannot open '%s' as chunky file\n", argv[1]);
        return 1;
    }

    ccki = pcfl->CckiCtg(kctgBmdl);
    fprintf(stderr, "extract-bmdl: %s contains %ld BMDL chunks\n", argv[1], ccki);
    if (ibmdl < 0 || ibmdl >= ccki)
    {
        fprintf(stderr, "extract-bmdl: index %ld out of range [0, %ld)\n", ibmdl, ccki);
        goto LDone;
    }

    if (!pcfl->FGetCkiCtg(kctgBmdl, ibmdl, &cki, pvNil, &blck))
    {
        fprintf(stderr, "extract-bmdl: FGetCkiCtg failed for index %ld\n", ibmdl);
        goto LDone;
    }

    if (!blck.FUnpackData())
    {
        fprintf(stderr, "extract-bmdl: FUnpackData failed for chunk cno=0x%lx\n", (long)cki.cno);
        goto LDone;
    }

    cb = blck.Cb();
    fprintf(stderr, "extract-bmdl: chunk %ld cno=0x%lx unpacked size=%ld bytes\n", ibmdl, (long)cki.cno, cb);

    if (!FAllocPv(&pv, cb, fmemNil, mprNormal))
    {
        fprintf(stderr, "extract-bmdl: out of memory\n");
        goto LDone;
    }
    if (!blck.FReadRgb(pv, cb, 0))
    {
        fprintf(stderr, "extract-bmdl: FReadRgb failed\n");
        goto LDone;
    }
    out = fopen(argv[3], "wb");
    if (!out)
    {
        perror(argv[3]);
        goto LDone;
    }
    fwrite(pv, 1, (size_t)cb, out);
    fclose(out);
    out = NULL;

    fprintf(stderr, "extract-bmdl: wrote %s\n", argv[3]);
    rc = 0;

LDone:
    if (out) fclose(out);
    if (pv) FreePpv(&pv);
    ReleasePpo(&pcfl);
    return rc;
}
