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
 * Self-contained CLI tool that links kauai-core only (no UI bootstrap).
 * Provides its own int main() rather than going through kauai's
 * appbwin.cpp WinMain. Exits non-zero on any failure.
 */

#include "kauai_core.h"

ASSERTNAME

/* Chunk type tag for body-models. Defined in inc/soc.h, but soc.h
 * pulls in frame.h + brender.h + the entire engine-side header tree;
 * inline the one constant we need so the tool can link kauai-core
 * only. Same packing as soc.h's `'BMDL'` 4-char literal. */
#ifndef kctgBmdl
#define kctgBmdl 'BMDL'
#endif

/* Kauai's debug.h declares WarnProc as the assertion-failure callback
 * and FAssertProc as the hard-fail one; the gui-side appb.cpp defines
 * them as "show a dialog". A CLI tool has no UI, so just print and
 * continue -- assertion fires are still surfaced to stderr. */
void WarnProc(schar *pszsFile, LegacyLong lwLine, schar *pszsMsg)
{
    fprintf(stderr, "WARN %s:%" LEGACY_PRId " %s\n", pszsFile ? pszsFile : "?", lwLine, pszsMsg ? pszsMsg : "");
}
bool FAssertProc(schar *pszsFile, LegacyLong lwLine, schar *pszsMsg, void *, LegacyLong)
{
    fprintf(stderr, "ASSERT %s:%" LEGACY_PRId " %s\n", pszsFile ? pszsFile : "?", lwLine, pszsMsg ? pszsMsg : "");
    return false; /* false = continue; true = enter debugger */
}

int main(int argc, char **argv)
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
    LegacyLong ibmdl = atoi(argv[2]);
    LegacyLong ccki = 0;
    LegacyLong cb = 0;
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
    fprintf(stderr, "extract-bmdl: %s contains %" LEGACY_PRId " BMDL chunks\n", argv[1], ccki);
    if (ibmdl < 0 || ibmdl >= ccki)
    {
        fprintf(stderr, "extract-bmdl: index %" LEGACY_PRId " out of range [0, %" LEGACY_PRId ")\n", ibmdl, ccki);
        goto LDone;
    }

    if (!pcfl->FGetCkiCtg(kctgBmdl, ibmdl, &cki, pvNil, &blck))
    {
        fprintf(stderr, "extract-bmdl: FGetCkiCtg failed for index %" LEGACY_PRId "\n", ibmdl);
        goto LDone;
    }

    if (!blck.FUnpackData())
    {
        fprintf(stderr, "extract-bmdl: FUnpackData failed for chunk cno=0x%" LEGACY_PRIx "\n", (LegacyLong)cki.cno);
        goto LDone;
    }

    cb = blck.Cb();
    fprintf(stderr, "extract-bmdl: chunk %" LEGACY_PRId " cno=0x%" LEGACY_PRIx " unpacked size=%" LEGACY_PRId " bytes\n", ibmdl, (LegacyLong)cki.cno, cb);

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
