#include "movie_chomp.h"
#include "actor.h"

ASSERTNAME

using namespace ActorEvent;

/*

SCEN
|-- THUM
|-- GGST SceneEvents
|-- GGFT FrameEvents
|-- ACTR
    |-- PATH
    |-- GGAE ActorEvents

*/

const ChildChunkID kchidGstSource = 1;
const ChunkNumber movie_chunk_number = 0;

void __cdecl FrameMain(void) {
    return;
}

#ifdef DEBUG

/***************************************************************************
    Assert the validity of the World.
***************************************************************************/
void MovieDecompiler::AssertValid(ulong grf)
{
    // MovieDecompiler_PAR::AssertValid(fobjAllocated);
}


void MovieDecompiler::MarkMem(void)
{
    // AssertThis(0);
    // MovieDecompiler_PAR::MarkMem();
}

#endif // DEBUG


/***************************************************************************
    Main routine for the stand-alone chunky compiler.  Returns non-zero
    iff there's an error.
***************************************************************************/
int __cdecl main(int cpszs, char *prgpszs[])
{
    Filename fniSrc, fniDst;
    PChunkyFile pcfl;
    String stn;
    char *pszs;
    MessageSinkIO mssioError(stderr);
    bool fCompile = fTrue;
    bool fCompileMovie = fFalse;

// #ifdef UNICODE
//     fprintf(stderr, "\nMicrosoft (R) Chunky File Compiler (Unicode; " Debug("Debug; ") __DATE__ "; " __TIME__ ")\n");
// #else  //! UNICODE
//     fprintf(stderr, "\nMicrosoft (R) Chunky File Compiler (Ansi; " Debug("Debug; ") __DATE__ "; " __TIME__ ")\n");
// #endif //! UNICODE
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
    stn = PszLit("C:/Users/wjbr/src/lib3dmm/walk.3mm");
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
        FileObject::ShutDown();
        return pvNil == pcfl;
    }
    else
    {
        bool fRet;
        MessageSinkIO mssioDump(stdout);
        MessageSinkFile msfilDump;
        MovieDecompiler chdc;

        if (pvNil == (pcfl = ChunkyFile::PcflOpen(&fniSrc, fcflNil)))
        {
            fprintf(stderr, "Couldn't open source file as a chunky file\n\n");
            goto LUsage;
        }

        if (fniDst.Ftg() != ftgNil)
        {
            PFileObject pfil;

            if (pvNil == (pfil = FileObject::PfilCreate(&fniDst)))
            {
                fprintf(stderr, "Couldn't create destination file\n\n");
                FileObject::ShutDown();
                return 1;
            }
            msfilDump.SetFile(pfil);
        }

        fRet = chdc.FDecompileMovie(pcfl, fniDst.Ftg() == ftgNil ? (PMSNK)&mssioDump : (PMSNK)&msfilDump, &mssioError);
        ReleasePpo(&pcfl);
        FileObject::ShutDown();
        return !fRet;
    }

    // print usage
LUsage:
    fprintf(stderr, "%s",
            "Usage:\n"
            "   chomp [/c] <srcTextFile> <dstChunkFile>  - compile chunky file\n"
            "   chomp /d <srcChunkFile> [<dstTextFile>]  - decompile chunky file\n\n");

    FileObject::ShutDown();
    return 1;
}

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
enum SceneEventType
{                   // StartEv	FrmEv	Param
    sevtAddActr,    //    X		  		pactr/chid
    sevtPlaySnd,    // 	  		  X		SceneSoundEvent (Scene Sound Event)
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
    WaitReason wit;
    long dts;
};


//
// Scene event
//
struct SEV
{
    long nfrm; // frame number of the event.
    SceneEventType sevt; // event type
};

typedef struct SEV *PSEV;

const auto kbomSev = 0xF0000000;
const auto kbomLong = 0xC0000000;

//
// Movie file prefix
//
struct MovieFilePrefix
{
    short bo;  // byte order
    short osk; // which system wrote this
    DataVersion dver; // chunky file version
};
const ByteOrderMask kbomMfp = 0x55000000;



struct ActorChunkOnFile // Actor chunk on file
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

//
// Used to keep track of the roll call list of the movie
//
struct MACTR
{
    long arid;
    long cactRef;
    ulong grfbrws; // browser properties
    TAG tagTmpl;
};

typedef MACTR *PMACTR;

const ByteOrderMask kbomMactr = (0xFC000000 | (kbomTag >> 4));

/***************************************************************************
    Read the ActorChunkOnFile. This handles converting an ActorChunkOnFile that doesn't have an
    nfrmLast.
***************************************************************************/
bool _FReadActf(PDataBlock pblck, ActorChunkOnFile *pactf)
{
    AssertPo(pblck, 0);
    AssertVarMem(pactf);
    bool fOldActf = fFalse;

    if (!pblck->FUnpackData())
        return fFalse;

    if (pblck->Cb() != size(ActorChunkOnFile))
    {
        if (pblck->Cb() != size(ActorChunkOnFile) - size(long))
            return fFalse;
        fOldActf = fTrue;
    }

    if (!pblck->FReadRgb(pactf, pblck->Cb(), 0))
        return fFalse;

    if (fOldActf)
    {
        BltPb(&pactf->nfrmLast, &pactf->nfrmLast + 1, size(ActorChunkOnFile) - offset(ActorChunkOnFile, nfrmLast) - size(long));
    }

    if (kboOther == pactf->bo)
        SwapBytesBom(pactf, kbomActf);
    if (kboCur != pactf->bo)
    {
        Bug("Corrupt ActorChunkOnFile");
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


void DumpSourceList(PChunkyFile pcflSrc, PMSNK pmsnk, PMSNK pmsnkError)
{
    short bo =  1;
    short osk=771;
    DataBlock data_block;

    ChildChunkIdentification kid;
    PStringTable_GST pgstSource;

    printf("\tsource_list = {\n");

    if (pcflSrc->FGetKidChidCtg(kctgMvie, movie_chunk_number, kchidGstSource, kctgGst, &kid) &&
        pcflSrc->FFind(kid.cki.ctg, kid.cki.cno, &data_block))
    {
        pgstSource = StringTable_GST::PgstRead(&data_block, &bo, &osk);
        if (pvNil != pgstSource)
        {
            String stn1;
            String stn2;  
            long iv, ivMac;
            long cbExtra;
            void *pvExtra = pvNil;

            cbExtra = pgstSource->CbExtra();
            AssertIn(cbExtra, 0, kcbMax);
            ivMac = pgstSource->IvMac();

            if (cbExtra > 0 && !FAllocPv(&pvExtra, cbExtra, fmemNil, mprNormal))
                return;

            for (iv = 0; iv < ivMac; iv++)
            {
                if (pgstSource->FFree(iv))
                {
                    puts(PszLit("\tFREE"));
                    continue;
                }
                printf(PszLit("\t\tITEM("));

                pgstSource->GetStn(iv, &stn2);
                stn2.FExpandControls();
                stn1.FFormatSz(PszLit("%s"), &stn2);
                printf("\"%s\"", stn1.Psz());

                if (cbExtra > 0)
                {
                    long extra;
                    pgstSource->GetExtra(iv, pvExtra);
                    // printf("%zu\n", cbExtra);
                    // printf("%zu\n", sizeof(long));

                    extra = *(long*)pvExtra;
                    printf(", %ld", extra);

                    // _bsf.FReplace(pvExtra, cbExtra, 0, _bsf.IbMac());
                    // _DumpBsf(2);
                }
                puts(")");
            }

            FreePpv(&pvExtra);
        }
    }

    printf("\t}\n");
}

void DumpRollcall(PChunkyFile pcfl, PMSNK pmsnk, PMSNK pmsnkError)
{
    DataBlock data_block;
    ChildChunkIdentification kid;
    short bo =  1;
    short osk=771;
    PStringTable_GST ppgst;

    puts("\trollcall = {");

    if (!pcfl->FGetKidChidCtg(kctgMvie, movie_chunk_number, 0, kctgGst, &kid) ||
        !pcfl->FFind(kid.cki.ctg, kid.cki.cno, &data_block))
    {
        Bug("failed to read rollcall");
        goto LFail;
    }

    ppgst = StringTable_GST::PgstRead(&data_block, &bo);
    if (pvNil != ppgst)
    {
        String stn1;
        String stn2;  
        long iv, ivMac;
        long cbExtra;
        void *pvExtra = pvNil;

        cbExtra = ppgst->CbExtra();
        AssertIn(cbExtra, 0, kcbMax);
        ivMac = ppgst->IvMac();

        if (cbExtra > 0 && !FAllocPv(&pvExtra, cbExtra, fmemNil, mprNormal))
            return;

        for (iv = 0; iv < ivMac; iv++)
        {
            if (ppgst->FFree(iv))
            {
                puts(PszLit("\tFREE"));
                continue;
            }
            printf(PszLit("\t\tITEM("));

            ppgst->GetStn(iv, &stn2);
            stn2.FExpandControls();
            stn1.FFormatSz(PszLit("%s"), &stn2);
            printf("\"%s\"", stn1.Psz());

            if (cbExtra > 0)
            {
                MACTR mactr;
                ppgst->GetExtra(iv, &mactr);
                // printf("%zu\n", cbExtra);
                // printf("%zu\n", sizeof(long));

                // extra = *(long*)pvExtra;
                // printf(", %ld", extra);

                // _bsf.FReplace(pvExtra, cbExtra, 0, _bsf.IbMac());
                // _DumpBsf(2);

                printf(", {\n");
                printf("\t\t\tarid=%ld,\n", mactr.arid);
                printf("\t\t\tcactRef=%ld,\n", mactr.cactRef);
                printf("\t\t\tgrfbrws=%lu,\n", mactr.grfbrws);

                {
                    printf("\t\t\tTAG(\n");
                    printf("\t\t\t\ttagTmpl.cno=%d,\n", mactr.tagTmpl.cno);

                    String template_tag;
                    template_tag.FFormatSz(PszLit("%f"), mactr.tagTmpl.ctg);
                    printf("\t\t\t\ttagTmpl='%s',\n", template_tag.Psz());

                    printf("\t\t\t\ttagTmpl.pcrf=%p,\n", mactr.tagTmpl.pcrf);
                    printf("\t\t\t\ttagTmpl.sid=%d\n", mactr.tagTmpl.sid);
                    printf("\t\t\t)");
                }


                printf("}\n");
            }
            puts("\t\t)");
        }

        FreePpv(&pvExtra);
    }


LFail:
    puts("\t}");
}


void DumpSceneEvents(PChunkyFile pcfl, PMSNK pmsnk, PMSNK pmsnkError, ChunkNumber cno)
{
    ChildChunkIdentification kid;
    printf("\t\t\tscene_events = [\n");

    //
    // Count the number of scenes in the movie
    //
    ChildChunkID chid;
    for (chid = 0; pcfl->FGetKidChidCtg(kctgScen, cno, chid, kctgStartGg, &kid); chid++)
    {
        printf("\t\t\t\tSceneEvent()\n");
        // SCENH scenh;
        // DataBlock data_block; 
        // pcfl->FFind(kctgScen, kid.cki.cno, &data_block);

        // if ( !data_block.FReadRgb(&scenh, size(SCENH), 0)) {
        //     goto LFail;
        // }
    }
    printf("\t\t\t]\n");
}

void DumpFrameEvent(PChunkyFile pcfl, PMSNK pmsnk, PMSNK pmsnkError, PGeneralGroup pggsevFrm, long isevFrm)
{
    long indent = 32;
    SEV sev;

    sev = *(PSEV)pggsevFrm->QvFixedGet(isevFrm);

    printf("%*.s" "FrameEvent(\n", indent, "");
    indent += 8;
    printf("%*.s" "nfrm=%d,\n", indent, "", sev.nfrm);
    printf("%*.s" "sevt=%d,\n", indent, "", sev.sevt);

    switch (sev.sevt)
    {
    case sevtPlaySnd: {
        // PSceneSoundEvent psse;

        // printf("sevtPlaySnd\n");

        // psse = SceneSoundEvent::PsseDupFromGg(pggsevFrm, isevFrm);
        // if (pvNil == psse)
        // {
        //     goto LFail;
        // }
        printf("%*.s" "sevtPlaySnd\n", indent, "");
        break;
    }

    case sevtChngCamera: {
        long iangle = 99;
        iangle = *(long*)pggsevFrm->QvGet(isevFrm);
        printf("%*.s" "sevtChngCamera angle=%d\n", indent, "", iangle);
        break;
    }

    case sevtPause: {
        // SEVP sevp;
        // sevp = *(*SEVP)pggsevFrm->QvGet(isevFrm);
        printf("%*.s" "sevtPause\n", indent, "");
        break;
    }

    case sevtAddActr:
    case sevtSetBkgd:
    case sevtAddTbox:
    default:
        Assert(0, "Bad event in frame event list");
        break;
    }

    indent -= 8;
    
    printf("%*.s" ")\n", indent, "");
}


void DumpFrameEvents(PChunkyFile pcfl, PMSNK pmsnk, PMSNK pmsnkError, ChunkNumber cno)
{
    ChildChunkIdentification kid;
    long indent = 24;

    printf("%*.s" "frame_events = [", indent, "");

    for (ChildChunkID chid = 0; pcfl->FGetKidChidCtg(kctgScen, cno, chid, kctgFrmGg, &kid); chid++)
    {
        PGeneralGroup pggsevFrm = pvNil;
        DataBlock data_block; 

        pcfl->FFind(kctgFrmGg, kid.cki.cno, &data_block);
        short bo;

        pggsevFrm = GeneralGroup::PggRead(&data_block, &bo);
        if (pggsevFrm == pvNil)
        {
            Bug("fail");
            return;
        }

        printf(" // length: %d\n", pggsevFrm->IvMac());

        for (long isevFrm = 0; isevFrm < pggsevFrm->IvMac(); isevFrm++)
        {
            DumpFrameEvent(pcfl, pmsnk, pmsnkError, pggsevFrm, isevFrm);
        }
        printf("%*.s" "", indent, "");
    }
    printf("]\n");
}

void DumpActorEvent(PChunkyFile pcfl, PMSNK pmsnk, PMSNK pmsnkError, PGeneralGroup pggaev, long iaev)
{
    // long iaev;
    // PGeneralGroup pggaev = pvNil;
    FramePosition xfrm;
    Base aev;
    long indent = 48;

    pggaev->GetFixed(iaev, &aev);

    printf("%*.sActorEvent(\n", indent, "");
    printf("%*.s\tnfrm=%ld,\n", indent, "", aev.nfrm);
    printf("%*.s\trtel.irpt=%d,\n", indent, "", aev.rtel.irpt);
    printf("%*.s\trtel.dwrOffset=%ld,\n", indent, "", aev.rtel.dwrOffset);
    printf("%*.s\trtel.dnfrm=%ld,\n", indent, "", aev.rtel.dnfrm);

    indent += 8;

    switch (aev.aet)
    {
    case aetActn:
        Action aevactn;
        ulong grfactn;
        pggaev->Get(iaev, &aevactn);
        printf("%*.sAction(\n", indent, "");
        printf("%*.s\tanid=%d,\n", indent, "", aevactn.anid);
        printf("%*.s\tceln=%d\n", indent, "", aevactn.celn);
        printf("%*.s)\n", indent, "");
        break;

    case aetAdd:
        Add aevadd;
        RouteDistancePoint rpt;

        // Set the translation for the subroute
        pggaev->Get(iaev, &aevadd);
        printf("%*.sAdd(\n", indent, "");
        printf("%*.s\tdxr=%ld,\n", indent, "", aevadd.dxr);
        printf("%*.s\tdyr=%ld,\n", indent, "", aevadd.dyr);
        printf("%*.s\tdzr=%ld\n", indent, "", aevadd.dzr);
        printf("%*.s)\n", indent, "");
        break;

    case aetRem:
        printf("%*.sRemove()\n", indent, "");
        break;

    case aetCost:
        Costume aevcost;
        pggaev->Get(iaev, &aevcost);
        printf("%*.sCostume()\n", indent, "");
        break;

    case aetRotF: {
        BMAT34 bmat34fwd;
        // Actors are xformed in _FDoFrm, Rotate or Scale
        pggaev->Get(iaev, &bmat34fwd);
        printf("%*.sRotF(bmat34fwd)\n", indent, "");
        break;
    }

    case aetRotH: {
        BMAT34 bmat34fwd;
        // Actors are xformed in _FDoFrm, Rotate or Scale
        pggaev->Get(iaev, &bmat34fwd);
        printf("%*.sRotH(bmat34fwd)\n", indent, "");
        break;
    }

    case aetPull:
        // Actors are xformed in _FDoFrm, Rotate or Scale
        pggaev->Get(iaev, &xfrm.aevpull);
        printf("%*.sPull()\n", indent, "");
        break;

    case aetSize:
        // Actors are xformed in _FDoFrm, Rotate or Scale
        pggaev->Get(iaev, &xfrm.rScaleStep);
        printf("%*.sSize()\n", indent, "");
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
        printf("%*.sStep()\n", indent, "");
        break;
    }

    case aetFreeze: {
        long fFrozen; //_fFrozen is a bit
        pggaev->Get(iaev, &fFrozen);
        // _fFrozen = FPure(fFrozen);
        printf("%*.sFreeze()\n", indent, "");
        break;
    }

    case aetTweak: {
        RoutePoint xyzCur;
        pggaev->Get(iaev, &xyzCur);
        // The actual locating of the actor is done in _FDoFrm or FTweakRoute
        printf("%*.sTweak()\n", indent, "");
        break;
    }

    case aetSnd:
        printf("%*.sSound()\n", indent, "");
        break;

    

    case aetMove: {
        RoutePoint dxyz;
        pggaev->Get(iaev, &dxyz);
        printf("%*.sMove(\n", indent, "");
        printf("%*.s\tdxyz.dxr=%ld\n", indent, "", dxyz.dxr);
        printf("%*.s\tdxyz.dxr=%ld\n", indent, "", dxyz.dyr);
        printf("%*.s\tdxyz.dxr=%ld\n", indent, "", dxyz.dzr);
        printf("%*.s)\n", indent, "");
        break;
    }


    default:
        printf("%*.sUnknown AET\n", indent, "");
    }

    indent -= 8;

    printf("%*.s)\n", indent, "");

}

void DumpActorEvents(PChunkyFile pcfl, PMSNK pmsnk, PMSNK pmsnkError, ChunkNumber cno)
{
    ChildChunkIdentification kid;
    printf("\t\t\t\t\tactor_events = [\n");

    //
    // Count the number of scenes in the movie
    //
    ChildChunkID chid;
    for (chid = 0; pcfl->FGetKidChidCtg(kctgActr, cno, chid, kctgGgae, &kid); chid++)
    {
        PGeneralGroup pggaev = pvNil;
        DataBlock data_block; 

        pcfl->FFind(kctgGgae, kid.cki.cno, &data_block);
        short bo;

        pggaev = GeneralGroup::PggRead(&data_block, &bo);

        long iaev;
        Base aev;
        COST cost;
        FramePosition xfrm;

        printf("%*.s// length: %d\n", 48, "", pggaev->IvMac());

        for (iaev = 0; iaev < pggaev->IvMac(); iaev++)
        {
            

            DumpActorEvent(pcfl, pmsnk, pmsnkError, pggaev, iaev);
        }
    }
    printf("\t\t\t\t\t]\n");
}

void DumpActorsPath(PChunkyFile pcfl, PMSNK pmsnk, PMSNK pmsnkError, PDataBlock pdata_block)
{
    short bo = 1;
    PDynamicArray pglrpt;
    int indent = 40;

    printf("%*.s" "path = [\n", indent, "");
    pglrpt = DynamicArray::PglRead(pdata_block, &bo);
    if (pvNil == pglrpt) {
        return;
    }
    AssertBomRglw(kbomRpt, size(RouteDistancePoint));
    if (kboOther == bo)
    {
        SwapBytesRglw(pglrpt->QvGet(0), LwMul(pglrpt->IvMac(), size(RouteDistancePoint) / size(long)));
    }
    printf("%*.s" "\t//length of path: %d\n", indent, "", pglrpt->IvMac());

    indent += 8;
    for (long irdp = 0; irdp < pglrpt->IvMac(); irdp++)
    {
        RouteDistancePoint rdp;
        pglrpt->Get(irdp, &rdp);
        // printf("%*.s" "\t// %d\n", indent, "", irdp);
        printf("%*.s" "RouteDistancePoint(\n", indent, "");
        printf("%*.s" "\t{x=%d, y=%d, z=%d},\n", indent, "", rdp.xyz.dxr, rdp.xyz.dyr, rdp.xyz.dzr);
        printf("%*.s" "\tdwr=%d\n", indent, "", rdp.dwr);
        printf("%*.s" ")\n", indent, "");
    }
    indent -= 8;
    
    printf("%*.s" "]\n", indent, "");
}

void DumpSceneActors(PChunkyFile pcfl, PMSNK pmsnk, PMSNK pmsnkError, ChunkNumber cno)
{
    ChildChunkIdentification kid;
    printf("\t\t\tactors = [\n");

    //
    // Count the number of scenes in the movie
    //
    ChildChunkID chid;
    for (chid = 0; pcfl->FGetKidChidCtg(kctgScen, cno, chid, kctgActr, &kid); chid++)
    {
        printf("\t\t\t\tActor(\n");
        DataBlock data_block; 
        pcfl->FFind(kctgPath, kid.cki.cno, &data_block);

        DumpActorsPath(pcfl, pmsnk, pmsnkError, &data_block);
        DumpActorEvents(pcfl, pmsnk, pmsnkError, kid.cki.cno);
        printf("\t\t\t\t)\n");
    }
    printf("\t\t\t]\n");
}


void DumpScenes(PChunkyFile pcfl, PMSNK pmsnk, PMSNK pmsnkError)
{
    
    ChildChunkIdentification kid;
    ChunkNumber cno = 0;
    printf("\tscenes = [\n");

    //
    // Count the number of scenes in the movie
    //
    ChildChunkID chid;
    for (chid = 0; pcfl->FGetKidChidCtg(kctgMvie, cno, chid, kctgScen, &kid); chid++)
    {
        SCENH scenh;
        DataBlock data_block; 
        pcfl->FFind(kctgScen, kid.cki.cno, &data_block);

        if ( !data_block.FReadRgb(&scenh, size(SCENH), 0)) {
            goto LFail;
        }

            printf("\t\tScene(\n");
            printf("\t\t\tbo=%d,\n", scenh.bo);
            printf("\t\t\tosk=%d,\n", scenh.osk);
            printf("\t\t\tnfrmFirst=%d,\n", scenh.nfrmFirst);
            printf("\t\t\tnfrmLast=%d,\n", scenh.nfrmLast);
            printf("\t\t\ttrans=%d\n", scenh.trans);
            
            DumpSceneEvents(pcfl, pmsnk, pmsnkError, kid.cki.cno);
            DumpFrameEvents(pcfl, pmsnk, pmsnkError, kid.cki.cno);
            DumpSceneActors(pcfl, pmsnk, pmsnkError, kid.cki.cno);

            printf("\t\t)\n");
    }

LFail:
    puts("\t]");
}

/***************************************************************************
    Decompile a chunky file.
***************************************************************************/
bool MovieDecompiler::FDecompileMovie(PChunkyFile pcflSrc, PMSNK pmsnk, PMSNK pmsnkError)
{
    String movie_name;
    pcflSrc->FGetName(kctgMvie, movie_chunk_number, &movie_name);
    
    DataBlock data_block; 
    pcflSrc->FFind(kctgMvie, movie_chunk_number, &data_block);

    printf("Movie(\n");

    MovieFilePrefix mfp;
    data_block.FReadRgb(&mfp, size(MovieFilePrefix), 0);

    printf("\tname=\"%s\",\n", movie_name.Psz());
    printf("\tbo=%d,\n", mfp.bo);
    printf("\tosk=%d,\n", mfp.osk);
    printf("\tdver={swCur=%d, swBack=%d}\n", mfp.dver._swCur, mfp.dver._swBack);

    DumpSourceList(pcflSrc, pmsnk, pmsnkError);
    DumpRollcall(pcflSrc, pmsnk, pmsnkError);

    DumpScenes(pcflSrc, pmsnk, pmsnkError);

    printf(")\n");

    return fTrue;
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
        String stnName;
        String stnTag;
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

        // look for special ChunkTags
        ctg = cki.ctg;

        // stnTag.FFormatSz(PszLit("%f"), ctg);
        // printf("TAG '%s'\n", stnTag.Psz());

        // handle 4 character ctg's
        switch (ctg)
        {

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

        case kctgGgae:
            Bug("TODO");
            // DumpActorEvent(_pcfl, pmsnk, pmsnkError, &blck);
            goto LEndChunk;

        case kctgActr: {
            ActorChunkOnFile actf;
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

            String template_tag;
            template_tag.FFormatSz(PszLit("%f"), actf.tagTmpl.ctg);
            printf("\t\t\ttagTmpl='%s',\n", template_tag.Psz());

            printf("\t\t\ttagTmpl.pcrf=%p,\n", actf.tagTmpl.pcrf);
            printf("\t\t\ttagTmpl.sid=%d\n", actf.tagTmpl.sid);
            printf("\t\t)\n");

            printf("\t)\n");
            goto LEndChunk;
        }

        case kctgMvie: {
            MovieFilePrefix mfp;
            blck.FReadRgb(&mfp, size(MovieFilePrefix), 0);
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
                    // PSceneSoundEvent psse;

                    // printf("sevtPlaySnd\n");

                    // psse = SceneSoundEvent::PsseDupFromGg(pggsevFrm, isevFrm);
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

                        String background_tag;
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

    pgstb = fAst ? (PVirtualStringTable)AllocatedStringTable::PastRead(pblck, &bo, &osk) : (PVirtualStringTable)StringTable_GST::PgstRead(pblck, &bo, &osk);
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
    String stn;

    if (cfmtNil == cfmt)
        _chse.DumpSz(PszLit("PACK"));
    else
    {
        stn.FFormatSz(PszLit("PACKFMT (0x%x) PACK"), cfmt);
        _chse.DumpSz(stn.Psz());
    }
}

