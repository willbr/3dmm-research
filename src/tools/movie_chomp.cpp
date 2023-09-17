#include "movie_chomp.h"
#include "actor.h"

// #include "studio.h"
// #include "socres.h"
// #include "mminstal.h"

// using namespace Chunky;
using namespace ActorEvent;

ASSERTNAME

bool FInsertCD(PSTN pstnTitle);

// // 2MB cache per source for TagManager
// const ulong kcbCacheTagm = 2048 * 1024;


PTagManager vptagm = pvNil;
// Filename _fniMsKidsDir;


// int __cdecl main(int cpszs, char *prgpszs[])
// {
//     Filename fni;
//     STN stn;
//     STN path;
//     PMovie pmvie = pvNil;
//     PChunkyFile pcfl = pvNil;
//     PScene pscene = pvNil;

// // Debugger();
//     puts("hi1");
//     Debugger();
//     // stn = PszLit("./");
//     // AssertDo(_fniMsKidsDir.FBuildFromPath(&stn), 0);

//     // vptagm = TagManager::PtagmNew(&_fniMsKidsDir, FInsertCD, kcbCacheTagm);

//     stn = PszLit("C:/Users/wjbr/src/3DMMForever/build/input.3mm");
//     AssertDo(fni.FBuildFromPath(&stn), 0);

//     fni.GetStnPath(&path);
//     printf("%s\n", path.Psz());

//     pcfl = ChunkyFile::PcflOpen(&fni, fcflNil);
//     printf("cf %p\n", pcfl);

//     pmvie = Movie::PNewMovieFromFilename(&fni, pvNil);
//     printf("%p\n", pmvie);
//     if (pvNil == pmvie) {
//         fprintf(stderr, "failed to load movie");
//         return 1;
//     }

//     puts(".");
//     pmvie->FSwitchScen(0);
//     puts(".");
//     pscene = pmvie->Pscen();

//     printf("%p\n", pscene);

//     puts("bye2");

//     return 0;
// }

void __cdecl FrameMain(void) {
    return;
}


/* Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */

/***************************************************************************

    CHOMP.CPP
    Main routine for Chomp, the chunky compiler

***************************************************************************/
// #include "chomp.h"
ASSERTNAME

/***************************************************************************
    Main routine for the stand-alone chunky compiler.  Returns non-zero
    iff there's an error.
***************************************************************************/
int __cdecl main(int cpszs, char *prgpszs[])
{
    Filename fniSrc, fniDst;
    PChunkyFile pcfl;
    STN stn;
    char *pszs;
    MSSIO mssioError(stderr);
    bool fCompile = fTrue;
    bool fCompileMovie = fFalse;

#ifdef UNICODE
    fprintf(stderr, "\nMicrosoft (R) Chunky File Compiler (Unicode; " Debug("Debug; ") __DATE__ "; " __TIME__ ")\n");
#else  //! UNICODE
    fprintf(stderr, "\nMicrosoft (R) Chunky File Compiler (Ansi; " Debug("Debug; ") __DATE__ "; " __TIME__ ")\n");
#endif //! UNICODE
    // fprintf(stderr, "Copyright (C) Microsoft Corp 1995. All rights reserved.\n\n");

    for (prgpszs++; --cpszs > 0; prgpszs++)
    {
        pszs = *prgpszs;
        if (pszs[0] == '-' || pszs[0] == '/')
        {
            // option
            switch (pszs[1])
            {
            case 'c':
            case 'C':
                fCompile = fTrue;
                break;

            case 'd':
            case 'D':
                fCompile = fFalse;
                break;

            case 'm':
            case 'M':
                fCompileMovie = fTrue;
                break;

            default:
                fprintf(stderr, "Bad command line option\n\n");
                goto LUsage;
            }

            if (pszs[2] != 0)
            {
                fprintf(stderr, "Bad command line option\n\n");
                goto LUsage;
            }
            continue;
        }

        if (fniDst.Ftg() != ftgNil)
        {
            fprintf(stderr, "Too many files specified\n\n");
            goto LUsage;
        }
        stn.SetSzs(pszs);
        if (!fniDst.FBuildFromPath(&stn))
        {
            fprintf(stderr, "Bad file name\n\n");
            goto LUsage;
        }
        if (fniSrc.Ftg() == ftgNil)
        {
            fniSrc = fniDst;
            fniDst.SetNil();
        }
    }

    fCompile = fFalse;
    stn = PszLit("C:/Users/wjbr/src/3DMMForever/build/input.3mm");
    AssertDo(fniSrc.FBuildFromPath(&stn), 0);

    if (fniSrc.Ftg() == ftgNil)
    {
        fprintf(stderr, "Missing source file name\n\n");
        goto LUsage;
    }

    if (fCompile)
    {
        Compiler chcm;

        if (fniDst.Ftg() == ftgNil)
        {
            fprintf(stderr, "Missing destination file name\n\n");
            goto LUsage;
        }
        pcfl = chcm.PcflCompile(&fniSrc, &fniDst, &mssioError);
        FIL::ShutDown();
        return pvNil == pcfl;
    }
    else
    {
        bool fRet;
        MSSIO mssioDump(stdout);
        MSFIL msfilDump;
        MovieDecompiler chdc;

        if (pvNil == (pcfl = ChunkyFile::PcflOpen(&fniSrc, fcflNil)))
        {
            fprintf(stderr, "Couldn't open source file as a chunky file\n\n");
            goto LUsage;
        }

        if (fniDst.Ftg() != ftgNil)
        {
            PFIL pfil;

            if (pvNil == (pfil = FIL::PfilCreate(&fniDst)))
            {
                fprintf(stderr, "Couldn't create destination file\n\n");
                FIL::ShutDown();
                return 1;
            }
            msfilDump.SetFile(pfil);
        }

        fRet = chdc.FDecompile(pcfl, fniDst.Ftg() == ftgNil ? (PMSNK)&mssioDump : (PMSNK)&msfilDump, &mssioError);
        ReleasePpo(&pcfl);
        FIL::ShutDown();
        return !fRet;
    }

    // print usage
LUsage:
    fprintf(stderr, "%s",
            "Usage:\n"
            "   chomp [/c] <srcTextFile> <dstChunkFile>  - compile chunky file\n"
            "   chomp /d <srcChunkFile> [<dstTextFile>]  - decompile chunky file\n\n");

    FIL::ShutDown();
    return 1;
}

#ifdef DEBUG
bool _fEnableWarnings = fTrue;

/***************************************************************************
    Warning proc called by Warn() macro
***************************************************************************/
void WarnProc(PSZS pszsFile, long lwLine, PSZS pszsMessage)
{
    if (_fEnableWarnings)
    {
        fprintf(stderr, "%s(%ld) : warning", pszsFile, lwLine);
        if (pszsMessage != pvNil)
        {
            fprintf(stderr, ": %s", pszsMessage);
        }
        fprintf(stderr, "\n");
    }
}

/***************************************************************************
    Returning true breaks into the debugger.
***************************************************************************/
bool FAssertProc(PSZS pszsFile, long lwLine, PSZS pszsMessage, void *pv, long cb)
{
    fprintf(stderr, "An assert occurred: \n");
    if (pszsMessage != pvNil)
        fprintf(stderr, "   Message: %s\n", pszsMessage);
    if (pv != pvNil)
    {
        fprintf(stderr, "   Address %p\n", pv);
        if (cb != 0)
        {
            fprintf(stderr, "   Value: ");
            switch (cb)
            {
            default: {
                byte *pb;
                byte *pbLim;

                for (pb = (byte *)pv, pbLim = pb + cb; pb < pbLim; pb++)
                    fprintf(stderr, "%02x", (int)*pb);
            }
            break;

            case 2:
                fprintf(stderr, "%04x", (int)*(short *)pv);
                break;

            case 4:
                fprintf(stderr, "%08lx", *(long *)pv);
                break;
            }
            printf("\n");
        }
    }
    fprintf(stderr, "   File: %s\n", pszsFile);
    fprintf(stderr, "   Line: %ld\n", lwLine);

    return fFalse;
}
#endif // DEBUG

struct SCENH
{
    short bo;
    short osk;
    long nfrmLast;
    long nfrmFirst;
    TRANS trans;
};

const auto kbomScenh = 0x5FC00000;



//
// Scene event types
//
enum SEVT
{                   // StartEv	FrmEv	Param
    sevtAddActr,    //    X		  		pactr/chid
    sevtPlaySnd,    // 	  		  X		SSE (Scene Sound Event)
    sevtAddTbox,    // 	  X				ptbox/chid
    sevtChngCamera, // 			  X		icam
    sevtSetBkgd,    // 	  X				Background Tag
    sevtPause       // 			  X		type, duration
};

//
// Struct for saving event pause information
//
struct SEVP
{
    WIT wit;
    long dts;
};


//
// Scene event
//
struct SEV
{
    long nfrm; // frame number of the event.
    SEVT sevt; // event type
};

typedef struct SEV *PSEV;

const auto kbomSev = 0xF0000000;
const auto kbomLong = 0xC0000000;

//
// Movie file prefix
//
struct MFP
{
    short bo;  // byte order
    short osk; // which system wrote this
    DVER dver; // chunky file version
};
const ByteOrderMask kbomMfp = 0x55000000;



struct ACTF // Actor chunk on file
{
    short bo;        // Byte order
    short osk;       // OS kind
    RoutePoint dxyzFullRte; // Translation of the route
    long arid;       // Unique id assigned to this actor.
    long nfrmFirst;  // First frame in this actor's stage life
    long nfrmLast;   // Last frame in this actor's stage life
    TAG tagTmpl;     // Tag to actor's template
};
const ByteOrderMask kbomActf = 0x5ffc0000 | kbomTag;



/***************************************************************************
    Read the ACTF. This handles converting an ACTF that doesn't have an
    nfrmLast.
***************************************************************************/
bool _FReadActf(PDataBlock pblck, ACTF *pactf)
{
    AssertPo(pblck, 0);
    AssertVarMem(pactf);
    bool fOldActf = fFalse;

    if (!pblck->FUnpackData())
        return fFalse;

    if (pblck->Cb() != size(ACTF))
    {
        if (pblck->Cb() != size(ACTF) - size(long))
            return fFalse;
        fOldActf = fTrue;
    }

    if (!pblck->FReadRgb(pactf, pblck->Cb(), 0))
        return fFalse;

    if (fOldActf)
    {
        BltPb(&pactf->nfrmLast, &pactf->nfrmLast + 1, size(ACTF) - offset(ACTF, nfrmLast) - size(long));
    }

    if (kboOther == pactf->bo)
        SwapBytesBom(pactf, kbomActf);
    if (kboCur != pactf->bo)
    {
        Bug("Corrupt ACTF");
        return fFalse;
    }

    if (fOldActf)
        pactf->nfrmLast = knfrmInvalid;
    return fTrue;
}








RTCLASS(MovieDecompiler)

/***************************************************************************
    Constructor for the Decompiler class. This is the chunky decompiler.
***************************************************************************/
MovieDecompiler::MovieDecompiler(void)
{
    _ert = ertNil;
    _pcfl = pvNil;
    AssertThis(0);
}

/***************************************************************************
    Destructor for the Decompiler class.
***************************************************************************/
MovieDecompiler::~MovieDecompiler(void)
{
    ReleasePpo(&_pcfl);
}

/***************************************************************************
    Decompile a chunky file.
***************************************************************************/
bool MovieDecompiler::FDecompile(PChunkyFile pcflSrc, PMSNK pmsnk, PMSNK pmsnkError)
{
    AssertThis(0);
    AssertPo(pcflSrc, 0);
    long icki, ikid, ckid;
    ChunkTag ctg;
    ChunkIdentification cki;
    ChildChunkIdentification kid;
    DataBlock blck;

    _pcfl = pcflSrc;
    _ert = ertNil;
    _chse.Init(pmsnk, pmsnkError);

    _bo = kboCur;
    _osk = koskCur;

    _chse.DumpSz(PszLit("BYTE"));
    _chse.DumpSz(PszLit(""));
    for (icki = 0; _pcfl->FGetCki(icki, &cki, pvNil, &blck); icki++)
    {
        STN stnName;
        STN stnTag;
        SCENH scenh;
        short bo;

        PDynamicArray pglrpt;
        long irdp;
        RouteDistancePoint rdp;

        PGeneralGroup pggsevFrm;   // List of events that occur in frames.
        long isevFrm;
        SEV sev;

        PGeneralGroup pggsevStart;     // List of frame independent events.
        long isevStart;

        // don't dump these, because they're embedded in the script
        if (cki.ctg == kctgScriptStrs)
            continue;

        _pcfl->FGetName(cki.ctg, cki.cno, &stnName);
        _chse.DumpHeader(cki.ctg, cki.cno, &stnName);

        // look for special CTGs
        ctg = cki.ctg;

        // stnTag.FFormatSz(PszLit("%f"), ctg);
        // printf("TAG '%s'\n", stnTag.Psz());

        // handle 4 character ctg's
        switch (ctg)
        {
        case kctgScript:
            if (_FDumpScript(&cki))
                goto LEndChunk;
            _pcfl->FGetCki(icki, &cki, pvNil, &blck);
            break;

        case kctgThumbMbmp:
            printf("PACK BITMAP( 0, 0, 0 ) \"thumbnail.mbmp\"\n");
            goto LEndChunk;
        
        case kctgScen:
            if ( !blck.FReadRgb(&scenh, size(SCENH), 0)) {
                goto LFail;
            }

            //
            // Check header for byte swapping
            //
            if (scenh.bo == kboOther)
            {
                SwapBytesBom(&scenh, kbomScenh);
            }
            else
            {
                Assert(scenh.bo == kboCur, "Bad Chunky file");
            }

            // printf("block size: %ld\n", blck.Cb());
            // printf("block size: %ld\n", size(SCENH));

            printf("\tScene(\n");
            printf("\t\tbo=%d,\n", scenh.bo);
            printf("\t\tosk=%d,\n", scenh.osk);
            printf("\t\tnfrmFirst=%d,\n", scenh.nfrmFirst);
            printf("\t\tnfrmLast=%d,\n", scenh.nfrmLast);
            printf("\t\ttrans=%d\n", scenh.trans);
            printf("\t)\n");

            goto LEndChunk;

        case kctgPath:
            pglrpt = DynamicArray::PglRead(&blck, &bo);
            if (pvNil == pglrpt) {
                return fFalse;
            }
            AssertBomRglw(kbomRpt, size(RouteDistancePoint));
            if (kboOther == bo)
            {
                SwapBytesRglw(pglrpt->QvGet(0), LwMul(pglrpt->IvMac(), size(RouteDistancePoint) / size(long)));
            }
            // printf("length of path: %d\n", pglrpt->IvMac());

            for (irdp = 0; irdp < pglrpt->IvMac(); irdp++)
            {
                pglrpt->Get(irdp, &rdp);
                printf("\t// %d\n", irdp);
                printf("\tRouteDistancePoint(\n");
                printf("\t\t{x=%d, y=%d, z=%d},\n", rdp.xyz.dxr, rdp.xyz.dyr, rdp.xyz.dzr);
                printf("\t\tdwr=%d\n", rdp.dwr);
                printf("\t)\n");
            }
            goto LEndChunk;

        case kctgGgae: {
            PGeneralGroup pggaev = pvNil;
            pggaev = GeneralGroup::PggRead(&blck, &bo);

            // printf("GGAE\n");
            // printf("%d\n", pggaev->IvMac());

            long iaev;
            Base aev;
            COST cost;
            FramePosition xfrm;

            printf("// length: %d\n", pggaev->IvMac());

            for (iaev = 0; iaev < pggaev->IvMac(); iaev++)
            {
                pggaev->GetFixed(iaev, &aev);
                printf("\tActorEvent(\n");
                printf("\t\tnfrm=%ld,\n", aev.nfrm);
                printf("\t\trtel.irpt=%d,\n", aev.rtel.irpt);
                printf("\t\trtel.dwrOffset=%ld,\n", aev.rtel.dwrOffset);
                printf("\t\trtel.dnfrm=%ld,\n", aev.rtel.dnfrm);

                switch (aev.aet)
                {
                case aetActn:
                    Action aevactn;
                    ulong grfactn;
                    pggaev->Get(iaev, &aevactn);
                    printf("\t\tAction(\n");
                    printf("\t\t\tanid=%d,\n", aevactn.anid);
                    printf("\t\t\tceln=%d\n", aevactn.celn);
                    printf("\t\t)\n");
                    break;

                case aetAdd:
                    Add aevadd;
                    RouteDistancePoint rpt;

                    // Set the translation for the subroute
                    pggaev->Get(iaev, &aevadd);
                    printf("\t\tAdd(\n");
                    printf("\t\t\taevadd.dxr=%ld,\n", aevadd.dxr);
                    printf("\t\t\taevadd.dxr=%ld,\n", aevadd.dyr);
                    printf("\t\t\taevadd.dxr=%ld\n", aevadd.dzr);
                    printf("\t\t)\n");
                    break;

                case aetRem:
                    printf("\t\tRemove()\n");
                    break;

                case aetCost:
                    Costume aevcost;
                    pggaev->Get(iaev, &aevcost);
                    printf("\t\tCostume()\n");
                    break;

                case aetRotF: {
                    BMAT34 bmat34fwd;
                    // Actors are xformed in _FDoFrm, Rotate or Scale
                    pggaev->Get(iaev, &bmat34fwd);
                    printf("\t\tRotF(bmat34fwd)\n");
                    break;
                }

                case aetRotH: {
                    BMAT34 bmat34fwd;
                    // Actors are xformed in _FDoFrm, Rotate or Scale
                    pggaev->Get(iaev, &bmat34fwd);
                    printf("\t\tRotH(bmat34fwd)\n");
                    break;
                }

                case aetPull:
                    // Actors are xformed in _FDoFrm, Rotate or Scale
                    pggaev->Get(iaev, &xfrm.aevpull);
                    printf("\t\tPull()\n");
                    break;

                case aetSize:
                    // Actors are xformed in _FDoFrm, Rotate or Scale
                    pggaev->Get(iaev, &xfrm.rScaleStep);
                    printf("\t\tSize()\n");
                    break;

                case aetStep: { // Exists for timing control (eg walk in place)
                    BRS dwrStep;
                    pggaev->Get(iaev, &dwrStep);
                    // Force the location to the step event
                    // Avoids incorrect event ordering on static segments
                    if (rZero == dwrStep)
                    {
                        // _rtelCur = aev.rtel;
                        // _GetXyzFromRtel(&_rtelCur, &_xyzCur);
                    }
                    printf("\t\tStep()\n");
                    break;
                }

                case aetFreeze: {
                    long fFrozen; //_fFrozen is a bit
                    pggaev->Get(iaev, &fFrozen);
                    // _fFrozen = FPure(fFrozen);
                    printf("\t\tFreeze()\n");
                    break;
                }

                case aetTweak: {
                    RoutePoint xyzCur;
                    pggaev->Get(iaev, &xyzCur);
                    // The actual locating of the actor is done in _FDoFrm or FTweakRoute
                    printf("\t\tTweak()\n");
                    break;
                }

                case aetSnd:
                    printf("\t\tSound()\n");
                    break;

                

                case aetMove: {
                    RoutePoint dxyz;
                    pggaev->Get(iaev, &dxyz);
                    printf("\t\tMove(\n");
                    printf("\t\t\tdxyz.dxr=%ld\n", dxyz.dxr);
                    printf("\t\t\tdxyz.dxr=%ld\n", dxyz.dyr);
                    printf("\t\t\tdxyz.dxr=%ld\n", dxyz.dzr);
                    printf("\t\t)\n");
                    break;
                }


                default:
                    printf("\t\tUnknown AET\n");
                }

                printf("\t)\n");
            }


            goto LEndChunk;
        }

        case kctgActr: {
            ACTF actf;
            // printf("ACTR\n");
            _FReadActf(&blck, &actf);

            printf("\tActor(\n");
            printf("\t\tbo=%d,\n", actf.bo);
            printf("\t\tosk=%d,\n", actf.osk);

            printf("\t\tRoutePoint(\n");
            printf("\t\t\tdxyzFullRte.dxr=%ld,\n", actf.dxyzFullRte.dxr);
            printf("\t\t\tdxyzFullRte.dyr=%ld,\n", actf.dxyzFullRte.dyr);
            printf("\t\t\tdxyzFullRte.dzr=%ld\n", actf.dxyzFullRte.dzr);
            printf("\t\t)\n");

            printf("\t\tarid=%d,\n", actf.arid);
            printf("\t\tnfrmFirst=%d,\n", actf.nfrmFirst);
            printf("\t\tnfrmLast=%d,\n", actf.nfrmLast);

            printf("\t\tTAG(\n");
            printf("\t\t\ttagTmpl.cno=%d,\n", actf.tagTmpl.cno);

            STN template_tag;
            template_tag.FFormatSz(PszLit("%f"), actf.tagTmpl.ctg);
            printf("\t\t\ttagTmpl='%s',\n", template_tag.Psz());

            printf("\t\t\ttagTmpl.pcrf=%p,\n", actf.tagTmpl.pcrf);
            printf("\t\t\ttagTmpl.sid=%d\n", actf.tagTmpl.sid);
            printf("\t\t)\n");

            printf("\t)\n");
            goto LEndChunk;
        }

        case kctgMvie: {
            MFP mfp;
            blck.FReadRgb(&mfp, size(MFP), 0);
            printf("\tMovie(\n");
            printf("\t\tbo=%d,\n", mfp.bo);
            printf("\t\tosk=%d,\n", mfp.osk);
            printf("\t\tdver={swCur=%d, swBack=%d}\n", mfp.dver._swCur, mfp.dver._swBack);
            printf("\t)\n");
            goto LEndChunk;
        }

        case kctgFrmGg:
            // printf("GGFR\n");
            pggsevFrm = GeneralGroup::PggRead(&blck, &bo);
            if (pggsevFrm == pvNil)
            {
                printf("fail\n");
                goto LFail;
            }

            printf("// length: %d\n", pggsevFrm->IvMac());
            for (isevFrm = 0; isevFrm < pggsevFrm->IvMac(); isevFrm++)
            {
                // pggsevFrm->Get(isevFrm, &sev);
                sev = *(PSEV)pggsevFrm->QvFixedGet(isevFrm);

                printf("\tFrameEvent(\n");
                printf("\t\tnfrm=%d,\n", sev.nfrm);
                printf("\t\tsevt=%d,\n", sev.sevt);

                switch (sev.sevt)
                {
                case sevtPlaySnd: {
                    // PSSE psse;

                    // printf("sevtPlaySnd\n");

                    // psse = SSE::PsseDupFromGg(pggsevFrm, isevFrm);
                    // if (pvNil == psse)
                    // {
                    //     goto LFail;
                    // }
                    printf("sevtPlaySnd\n");
                    break;
                }

                case sevtChngCamera: {
                    // long iangle;
                    // iangle = *(*long)pggsevFrm->QvGet(isevFrm);
                    printf("sevtChngCamera\n");
                    break;
                }

                case sevtPause: {
                    // SEVP sevp;
                    // sevp = *(*SEVP)pggsevFrm->QvGet(isevFrm);
                    printf("sevtPause\n");
                    break;
                }

                case sevtAddActr:
                case sevtSetBkgd:
                case sevtAddTbox:
                default:
                    Assert(0, "Bad event in frame event list");
                    break;
                }

                printf("\t)\n");
                // printf("\t// %d\n", irdp);
                // printf("\tRouteDistancePoint(\n");
                // printf("\t\t{x=%d, y=%d, z=%d}\n", rdp.xyz.dxr, rdp.xyz.dyr, rdp.xyz.dzr);
                // printf("\t\tdwr=%d;\n", rdp.dwr);
                // printf("\t)\n");
            }

            goto LEndChunk;

        case kctgStartGg:
            // printf("GGST\n");
            pggsevStart = GeneralGroup::PggRead(&blck, &bo);

            if (pggsevStart == pvNil)
            {
                printf("fail\n");
                goto LFail;
            }

            printf("// length: %d\n", pggsevStart->IvMac());
            for (isevStart = 0; isevStart < pggsevStart->IvMac(); isevStart++)
            {
                // pggsevFrm->Get(isevStart, &sev);
                sev = *(PSEV)pggsevStart->QvFixedGet(isevStart);
                printf("\tSceneEvent(\n");
                printf("\t\tnfrm=%lu,\n", sev.nfrm);

                switch (sev.sevt) {
                    case sevtAddActr:
                        printf("\t\tsevt=sevtAddActr,\n");
                        break;

                    case sevtSetBkgd: {
                        printf("\t\tsevt=sevtSetBkgd,\n");
                        TAG tag;
                        tag = *(PTAG)pggsevStart->QvGet(isevStart);
                        printf("\t\tTAG(\n");
                        printf("\t\t\tcno=%lu,\n", tag.cno);

                        STN background_tag;
                        background_tag.FFormatSz(PszLit("%f"), tag.ctg);
                        printf("\t\t\tctg='%s',\n", background_tag.Psz());

                        printf("\t\t\tpcrf=%p,\n", tag.pcrf);
                        printf("\t\t\tsig=%lu\n", tag.sid);
                        printf("\t\t)\n");

                        break;
                    }

                    case sevtChngCamera: {
                        long iangle;
                        iangle = *(long*)pggsevStart->QvGet(isevStart);
                        // long
                        printf("\t\tsevt=sevtChngCamera,\n");
                        printf("\t\tiangle=%lu\n", iangle);
                        break;
                    }

                    case sevtAddTbox:
                        printf("\t\tsevt=sevtAddTbox\n");
                        break;

                    case sevtPause:
                    case sevtPlaySnd:
                    default:
                        Assert(0, "Bad event in frame event list");
                        break;
                }

                printf("\t)\n");
            //     // printf("\t// %d\n", irdp);
            //     // printf("\tRouteDistancePoint(\n");
            //     // printf("\t\t{x=%d, y=%d, z=%d}\n", rdp.xyz.dxr, rdp.xyz.dyr, rdp.xyz.dzr);
            //     // printf("\t\tdwr=%d;\n", rdp.dwr);
            //     // printf("\t)\n");
            }

            goto LEndChunk;

        case kctgTmpl:
            printf("4 char tag %d\n", ctg);
            _chse.DumpBlck(&blck);
            goto LEndChunk;
            break;
        }

        // handle 3 character ctg's
        ctg = ctg & 0xFFFFFF00L | 0x00000020L;
        switch (ctg)
        {
        case kctgGst:
        case kctgAst:
            if (_FDumpStringTable(&blck, kctgAst == ctg))
                goto LEndChunk;
            _pcfl->FGetCki(icki, &cki, pvNil, &blck);
            break;
        }

        // handle 2 character ctg's
        ctg = ctg & 0xFFFF0000L | 0x00002020L;
        switch (ctg)
        {
        case kctgGl:
        case kctgAl:
            if (_FDumpList(&blck, kctgAl == ctg))
                goto LEndChunk;
            _pcfl->FGetCki(icki, &cki, pvNil, &blck);
            break;
        case kctgGg:
        case kctgAg:
            if (_FDumpGroup(&blck, kctgAg == ctg))
                goto LEndChunk;
            _pcfl->FGetCki(icki, &cki, pvNil, &blck);
            break;
        }

        _chse.DumpBlck(&blck);

    LEndChunk:
        _chse.DumpSz(PszLit("ENDCHUNK"));
        _chse.DumpSz(PszLit(""));
    }

    // now output parent-child relationships
    for (icki = 0; _pcfl->FGetCki(icki++, &cki, &ckid);)
    {
        for (ikid = 0; ikid < ckid;)
        {
            AssertDo(_pcfl->FGetKid(cki.ctg, cki.cno, ikid++, &kid), 0);
            if (kid.cki.ctg == kctgScriptStrs && cki.ctg == kctgScript)
                continue;
            _chse.DumpAdoptCmd(&cki, &kid);
        }
    }
    _pcfl = pvNil;
    _chse.Uninit();

    LFail:
    return !FError();
}



/***************************************************************************
    Try to read the chunk as a group and dump it out.  If the chunk isn't
    a group, return false so it can be dumped in hex.
***************************************************************************/
bool MovieDecompiler::_FDumpGroup(PDataBlock pblck, bool fAg)
{
    AssertThis(0);
    AssertPo(pblck, fblckReadable);

    PVirtualGroup pggb;
    short bo, osk;
    long cfmt;
    bool fPacked = pblck->FPacked(&cfmt);

    pggb = fAg ? (PVirtualGroup)AllocatedGroup::PagRead(pblck, &bo, &osk) : (PVirtualGroup)GeneralGroup::PggRead(pblck, &bo, &osk);
    if (pvNil == pggb)
        return fFalse;

    if (fPacked)
        _WritePack(cfmt);

    if (bo != _bo)
    {
        _chse.DumpSz(MacWin(bo != kboCur, bo == kboCur) ? PszLit("WINBO") : PszLit("MACBO"));
        _bo = bo;
    }
    if (osk != _osk)
    {
        _chse.DumpSz(osk == koskWin ? PszLit("WINOSK") : PszLit("MACOSK"));
        _osk = osk;
    }

    _chse.DumpGroup(pggb);
    ReleasePpo(&pggb);

    return fTrue;
}

/***************************************************************************
    Disassemble the script and dump it.
***************************************************************************/
bool MovieDecompiler::_FDumpScript(ChunkIdentification *pcki)
{
    AssertThis(0);
    AssertVarMem(pcki);
    PScript pscpt;
    bool fRet;
    GraphicsObjectCompiler sccg;
    long cfmt;
    bool fPacked;
    DataBlock blck;

    _pcfl->FFind(pcki->ctg, pcki->cno, &blck);
    fPacked = blck.FPacked(&cfmt);

    if (pvNil == (pscpt = Script::PscptRead(_pcfl, pcki->ctg, pcki->cno)))
        return fFalse;

    if (fPacked)
        _WritePack(cfmt);

    fRet = _chse.FDumpScript(pscpt, &sccg);

    ReleasePpo(&pscpt);

    return fRet;
}

/***************************************************************************
    Try to read the chunk as a list and dump it out.  If the chunk isn't
    a list, return false so it can be dumped in hex.
***************************************************************************/
bool MovieDecompiler::_FDumpList(PDataBlock pblck, bool fAl)
{
    AssertThis(0);
    AssertPo(pblck, fblckReadable);

    PVirtualArray pglb;
    short bo, osk;
    long cfmt;
    bool fPacked = pblck->FPacked(&cfmt);

    pglb = fAl ? (PVirtualArray)AllocatedArray::PalRead(pblck, &bo, &osk) : (PVirtualArray)DynamicArray::PglRead(pblck, &bo, &osk);
    if (pvNil == pglb)
        return fFalse;

    if (fPacked)
        _WritePack(cfmt);

    if (bo != _bo)
    {
        _chse.DumpSz(MacWin(bo != kboCur, bo == kboCur) ? PszLit("WINBO") : PszLit("MACBO"));
        _bo = bo;
    }
    if (osk != _osk)
    {
        _chse.DumpSz(osk == koskWin ? PszLit("WINOSK") : PszLit("MACOSK"));
        _osk = osk;
    }

    _chse.DumpList(pglb);
    ReleasePpo(&pglb);

    return fTrue;
}


/***************************************************************************
    Try to read the chunk as a string table and dump it out.  If the chunk
    isn't a string table, return false so it can be dumped in hex.
***************************************************************************/
bool MovieDecompiler::_FDumpStringTable(PDataBlock pblck, bool fAst)
{
    AssertThis(0);
    AssertPo(pblck, fblckReadable);

    PVirtualStringTable pgstb;
    short bo, osk;
    long cfmt;
    bool fPacked = pblck->FPacked(&cfmt);
    bool fRet;

    pgstb = fAst ? (PVirtualStringTable)AllocatedStringTable::PastRead(pblck, &bo, &osk) : (PVirtualStringTable)StringTable::PgstRead(pblck, &bo, &osk);
    if (pvNil == pgstb)
        return fFalse;

    if (fPacked)
        _WritePack(cfmt);

    if (bo != _bo)
    {
        _chse.DumpSz(MacWin(bo != kboCur, bo == kboCur) ? PszLit("WINBO") : PszLit("MACBO"));
        _bo = bo;
    }
    if (osk != _osk)
    {
        _chse.DumpSz(osk == koskWin ? PszLit("WINOSK") : PszLit("MACOSK"));
        _osk = osk;
    }

    fRet = _chse.FDumpStringTable(pgstb);
    ReleasePpo(&pgstb);

    return fRet;
}


/***************************************************************************
    Write out the PACKFMT and PACK commands
***************************************************************************/
void MovieDecompiler::_WritePack(long cfmt)
{
    AssertThis(0);
    STN stn;

    if (cfmtNil == cfmt)
        _chse.DumpSz(PszLit("PACK"));
    else
    {
        stn.FFormatSz(PszLit("PACKFMT (0x%x) PACK"), cfmt);
        _chse.DumpSz(stn.Psz());
    }
}

