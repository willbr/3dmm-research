// Round-trip test for KauaiCodec (kcdc + kcd2).
// Generates deterministic pseudo-random source buffers, encodes via KauaiCodec,
// decodes back, and reports any mismatch with byte/value detail.
//
// On x86 the decoder is the inline-asm fast path generated from kcd2_386.c.
// On x64 the decoder falls back to the portable C path inside _FDecode/_FDecode2.
// Same source, different decoder bodies -- the test exposes any drift between
// them without requiring 3dmm to launch and grind through chunky-file decoding.

#include "frame.h"
#include "codec.h"

ASSERTNAME

#include <cstdio>
#include <cstdlib>

namespace
{
// Linear-congruential PRNG so tests are deterministic across builds.
struct LCG
{
    uint32_t state;
    uint32_t Next()
    {
        state = state * 1103515245u + 12345u;
        return state;
    }
};

void FillRandom(byte *pb, long cb, uint32_t seed, int kind)
{
    LCG g{seed};
    switch (kind)
    {
    case 0: // pure noise -- no compressible runs
        for (long i = 0; i < cb; i++)
            pb[i] = (byte)g.Next();
        break;
    case 1: // mostly zeros -- highly compressible
        for (long i = 0; i < cb; i++)
            pb[i] = (g.Next() & 0xF) ? 0 : (byte)g.Next();
        break;
    case 2: // small alphabet (0..15) -- moderately compressible
        for (long i = 0; i < cb; i++)
            pb[i] = (byte)(g.Next() & 0x0F);
        break;
    case 3: // long literal runs interleaved with repeats
        for (long i = 0; i < cb; i++)
        {
            if ((i / 32) & 1)
                pb[i] = (byte)g.Next(); // literal-ish
            else
                pb[i] = (byte)(i & 0xFF); // pattern -- repeats every 256
        }
        break;
    case 4: // long-distance back-references (>= 4096 apart)
        for (long i = 0; i < cb; i++)
        {
            if (i < 5000)
                pb[i] = (byte)g.Next();
            else
                pb[i] = pb[i - 4500]; // forces deep-offset (kcbitKcd2_3) branch
        }
        break;
    }
}

bool RoundTrip(const char *pszName, long cbSrc, uint32_t seed, int kind, long cfmt)
{
    byte *pbSrc = (byte *)malloc(cbSrc);
    byte *pbEnc = (byte *)malloc(cbSrc * 2 + 1024);
    byte *pbDec = (byte *)malloc(cbSrc + 1024);
    long cbEnc = 0;
    long cbDec = 0;

    FillRandom(pbSrc, cbSrc, seed, kind);

    KauaiCodec codec;
    if (!codec.FConvert(fTrue, cfmt, pbSrc, cbSrc, pbEnc, cbSrc * 2 + 1024, &cbEnc))
    {
        printf("[FAIL] %s cfmt=0x%lX cbSrc=%ld: encode failed\n", pszName, cfmt, cbSrc);
        free(pbSrc); free(pbEnc); free(pbDec);
        return false;
    }

    if (!codec.FConvert(fFalse, cfmt, pbEnc, cbEnc, pbDec, cbSrc + 1024, &cbDec))
    {
        printf("[FAIL] %s cfmt=0x%lX cbSrc=%ld cbEnc=%ld: decode returned false\n",
               pszName, cfmt, cbSrc, cbEnc);
        free(pbSrc); free(pbEnc); free(pbDec);
        return false;
    }

    if (cbDec != cbSrc)
    {
        printf("[FAIL] %s cfmt=0x%lX cbSrc=%ld cbDec=%ld: size mismatch (encoded to %ld bytes)\n",
               pszName, cfmt, cbSrc, cbDec, cbEnc);
        free(pbSrc); free(pbEnc); free(pbDec);
        return false;
    }

    for (long i = 0; i < cbSrc; i++)
    {
        if (pbSrc[i] != pbDec[i])
        {
            printf("[FAIL] %s cfmt=0x%lX cbSrc=%ld: byte %ld src=0x%02X dec=0x%02X (encoded %ld bytes)\n",
                   pszName, cfmt, cbSrc, i, pbSrc[i], pbDec[i], cbEnc);
            // Show a few bytes around the mismatch.
            long start = i > 16 ? i - 16 : 0;
            long stop = i + 16 < cbSrc ? i + 16 : cbSrc;
            printf("       src[%ld..%ld]:", start, stop - 1);
            for (long k = start; k < stop; k++)
                printf(" %02X", pbSrc[k]);
            printf("\n       dec[%ld..%ld]:", start, stop - 1);
            for (long k = start; k < stop; k++)
                printf(" %02X", pbDec[k]);
            printf("\n");
            free(pbSrc); free(pbEnc); free(pbDec);
            return false;
        }
    }

    printf("[ OK ] %s cfmt=0x%lX cbSrc=%ld -> cbEnc=%ld (ratio %.2f)\n",
           pszName, cfmt, cbSrc, cbEnc, (double)cbEnc / (double)cbSrc);
    free(pbSrc); free(pbEnc); free(pbDec);
    return true;
}
}

void main(void)
{
    printf("=== KauaiCodec round-trip tests ===\n");
    printf("Pointer width: %d bits\n", (int)(sizeof(void *) * 8));
    printf("\n");

    int passed = 0, failed = 0;
    auto run = [&](const char *name, long cb, uint32_t seed, int kind, long cfmt) {
        if (RoundTrip(name, cb, seed, kind, cfmt))
            passed++;
        else
            failed++;
    };

    // KCDC (older codec)
    run("noise-small",       256,    0x1234, 0, kcfmtKauai);
    run("noise-medium",      4096,   0x1234, 0, kcfmtKauai);
    run("zeros",             4096,   0x5678, 1, kcfmtKauai);
    run("alphabet",          4096,   0x9abc, 2, kcfmtKauai);
    run("pattern",           4096,   0xdef0, 3, kcfmtKauai);
    run("long-back-ref",     8192,   0x4444, 4, kcfmtKauai);

    // KCD2 (newer codec -- this is the one failing in 3dmm startup)
    run("noise-small",       256,    0x1234, 0, kcfmtKauai2);
    run("noise-medium",      4096,   0x1234, 0, kcfmtKauai2);
    run("zeros",             4096,   0x5678, 1, kcfmtKauai2);
    run("alphabet",          4096,   0x9abc, 2, kcfmtKauai2);
    run("pattern",           4096,   0xdef0, 3, kcfmtKauai2);
    run("long-back-ref",     8192,   0x4444, 4, kcfmtKauai2);
    // Specifically exercise the deep-offset (kcbitKcd2_3) branch: needs >= 0x1241 byte
    // distance for the back-reference, and several iterations to hit the 20-bit branch.
    run("deep-offset",       16384,  0xbeef, 4, kcfmtKauai2);

    printf("\n=== %d passed, %d failed ===\n", passed, failed);
    exit(failed == 0 ? 0 : 1);
}

// Stub: kauai's appbwin.cpp WinMain references FrameMain. We never reach
// WinMain (subsystem:console enters at main), but the symbol must resolve.
void FrameMain(void)
{
}

// Override kauai's FAssertProc so the test prints assert failures to stderr
// and CONTINUES (returns false = "don't break to debugger"). Without this,
// every codec-internal Bug() call STATUS_BREAKPOINTs the test process and
// no output is visible. We track that an assertion fired so the test fails.
bool g_fAssertFired = false;
char g_szLastAssert[512] = "";
bool FAssertProc(schar *pszsFile, long lwLine, schar *pszsMsg, void *pv, long cb)
{
    g_fAssertFired = true;
    if (pszsMsg != nullptr)
        snprintf(g_szLastAssert, sizeof(g_szLastAssert), "%s:%ld: %s", pszsFile ? (char *)pszsFile : "?", lwLine, (char *)pszsMsg);
    else
        snprintf(g_szLastAssert, sizeof(g_szLastAssert), "%s:%ld", pszsFile ? (char *)pszsFile : "?", lwLine);
    fprintf(stderr, "[ASSERT] %s\n", g_szLastAssert);
    fflush(stderr);
    return false; // don't break to debugger -- let the test continue and report
}

// Same for WarnProc -- redirect to stderr instead of the (nonexistent) app.
void WarnProc(schar *pszsFile, long lwLine, schar *pszsMsg)
{
    fprintf(stderr, "[WARN] %s:%ld: %s\n",
            pszsFile ? (char *)pszsFile : "?", lwLine, pszsMsg ? (char *)pszsMsg : "");
    fflush(stderr);
}
