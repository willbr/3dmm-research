#include "movie_chomp.h"
#include "mm/chunky_decompile.h"
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

// Movie/scene wire definitions come from the shared core headers.
typedef SceneEvent *PSEV;
const auto kbomLong = 0xC0000000;

RTCLASS(MovieDecompiler)

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
                RollCallActorEntryOnFile mactr;
                ppgst->GetExtra(iv, &mactr);
                // printf("%zu\n", cbExtra);
                // printf("%zu\n", sizeof(long));

                // extra = *(long*)pvExtra;
                // printf(", %ld", extra);

                // _bsf.FReplace(pvExtra, cbExtra, 0, _bsf.IbMac());
                // _DumpBsf(2);

                printf(", {\n");
                printf("\t\t\tarid=%d,\n", mactr.arid);
                printf("\t\t\tcactRef=%d,\n", mactr.cactRef);
                printf("\t\t\tgrfbrws=%u,\n", mactr.grfbrws);

                {
                    printf("\t\t\tTAG(\n");
                    printf("\t\t\t\ttagTmpl.cno=%d,\n", mactr.tagTmpl.cno);

                    String template_tag;
                    template_tag.FFormatSz(PszLit("%f"), mactr.tagTmpl.ctg);
                    printf("\t\t\t\ttagTmpl='%s',\n", template_tag.Psz());

                    printf("\t\t\t\ttagTmpl._pcrfPad=%u,\n", mactr.tagTmpl._pcrfPad);
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
        // SceneOnFile scenh;
        // DataBlock data_block; 
        // pcfl->FFind(kctgScen, kid.cki.cno, &data_block);

        // if ( !data_block.FReadRgb(&scenh, size(SceneOnFile), 0)) {
        //     goto LFail;
        // }
    }
    printf("\t\t\t]\n");
}

void DumpFrameEvent(PChunkyFile pcfl, PMSNK pmsnk, PMSNK pmsnkError, PGeneralGroup pggsevFrm, long isevFrm)
{
    long indent = 32;
    SceneEvent sev;

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
        // SceneEventPause sevp;
        // sevp = *(*SceneEventPause)pggsevFrm->QvGet(isevFrm);
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
    Base aev;
    long indent = 48;

    pggaev->GetFixed(iaev, &aev);

    printf("%*.sActorEvent(\n", indent, "");
    printf("%*.s\tnfrm=%d,\n", indent, "", aev.nfrm);
    printf("%*.s\trtel.irpt=%d,\n", indent, "", aev.rtel.irpt);
    printf("%*.s\trtel.dwrOffset=%ld,\n", indent, "", aev.rtel.dwrOffset);
    printf("%*.s\trtel.dnfrm=%d,\n", indent, "", aev.rtel.dnfrm);

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

    case aetPull: {
        // Actors are xformed in _FDoFrm, Rotate or Scale
        Stretch stretch;
        pggaev->Get(iaev, &stretch);
        printf("%*.sPull()\n", indent, "");
        break;
    }

    case aetSize: {
        // Actors are xformed in _FDoFrm, Rotate or Scale
        BRS scale;
        pggaev->Get(iaev, &scale);
        printf("%*.sSize()\n", indent, "");
        break;
    }

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
        int32_t fFrozen; //_fFrozen is a bit
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
        printf("%*.sUnknown ActorEventType\n", indent, "");
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
        SwapBytesRglw(pglrpt->QvGet(0), LwMul(pglrpt->IvMac(), size(RouteDistancePoint) / size(int32_t)));
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
        SceneOnFile scenh;
        DataBlock data_block; 
        pcfl->FFind(kctgScen, kid.cki.cno, &data_block);

        if ( !data_block.FReadRgb(&scenh, size(SceneOnFile), 0)) {
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

namespace {

MMChunkyBodyResult DumpMovieBody(void *context, const MMChunkEntry *entry)
{
    auto archive = static_cast<PChunkyFile>(context);
    DataBlock blck;
    const auto ctg = entry->type;
    const auto load = [&] { return archive->FFind(ctg, entry->number, &blck); };

    SceneOnFile scenh;
    short bo;
    PDynamicArray pglrpt;
    long irdp;
    RouteDistancePoint rdp;
    PGeneralGroup pggsevFrm;
    long isevFrm;
    SceneEvent sev;
    PGeneralGroup pggsevStart;
    long isevStart;

    switch (ctg)
    {
    case kctgThumbMbmp:
        printf("PACK BITMAP( 0, 0, 0 ) \"thumbnail.mbmp\"\n");
        return MM_CHUNKY_BODY_WRITTEN;

    case kctgScen:
        if (!load() || !blck.FReadRgb(&scenh, size(SceneOnFile), 0))
            return MM_CHUNKY_BODY_STOP;

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
        // printf("block size: %ld\n", size(SceneOnFile));

        printf("\tScene(\n");
        printf("\t\tbo=%d,\n", scenh.bo);
        printf("\t\tosk=%d,\n", scenh.osk);
        printf("\t\tnfrmFirst=%d,\n", scenh.nfrmFirst);
        printf("\t\tnfrmLast=%d,\n", scenh.nfrmLast);
        printf("\t\ttrans=%d\n", scenh.trans);
        printf("\t)\n");

        return MM_CHUNKY_BODY_WRITTEN;

    case kctgPath:
        if (!load() || pvNil == (pglrpt = DynamicArray::PglRead(&blck, &bo)))
            return MM_CHUNKY_BODY_ERROR;
        AssertBomRglw(kbomRpt, size(RouteDistancePoint));
        if (kboOther == bo)
        {
            SwapBytesRglw(pglrpt->QvGet(0), LwMul(pglrpt->IvMac(), size(RouteDistancePoint) / size(int32_t)));
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
        ReleasePpo(&pglrpt);
        return MM_CHUNKY_BODY_WRITTEN;

    case kctgGgae:
        Bug("TODO");
        return MM_CHUNKY_BODY_WRITTEN;

    case kctgActr: {
        ActorChunkOnFile actf;
        if (!load())
            return MM_CHUNKY_BODY_ERROR;
        ActorData::FReadHeader(&blck, &actf);

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

        printf("\t\t\ttagTmpl._pcrfPad=%u,\n", actf.tagTmpl._pcrfPad);
        printf("\t\t\ttagTmpl.sid=%d\n", actf.tagTmpl.sid);
        printf("\t\t)\n");

        printf("\t)\n");
        return MM_CHUNKY_BODY_WRITTEN;
    }

    case kctgMvie: {
        MovieFilePrefix mfp;
        if (!load())
            return MM_CHUNKY_BODY_ERROR;
        blck.FReadRgb(&mfp, size(MovieFilePrefix), 0);
        printf("\tMovie(\n");
        printf("\t\tbo=%d,\n", mfp.bo);
        printf("\t\tosk=%d,\n", mfp.osk);
        printf("\t\tdver={swCur=%d, swBack=%d}\n", mfp.dver._swCur, mfp.dver._swBack);
        printf("\t)\n");
        return MM_CHUNKY_BODY_WRITTEN;
    }

    case kctgFrmGg:
        if (!load() || pvNil == (pggsevFrm = GeneralGroup::PggRead(&blck, &bo)))
        {
            printf("fail\n");
            return MM_CHUNKY_BODY_STOP;
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
                //     return MM_CHUNKY_BODY_STOP;
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
                // SceneEventPause sevp;
                // sevp = *(*SceneEventPause)pggsevFrm->QvGet(isevFrm);
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

        ReleasePpo(&pggsevFrm);
        return MM_CHUNKY_BODY_WRITTEN;

    case kctgStartGg:
        if (!load() || pvNil == (pggsevStart = GeneralGroup::PggRead(&blck, &bo)))
        {
            printf("fail\n");
            return MM_CHUNKY_BODY_STOP;
        }

        printf("// length: %d\n", pggsevStart->IvMac());
        for (isevStart = 0; isevStart < pggsevStart->IvMac(); isevStart++)
        {
            // pggsevFrm->Get(isevStart, &sev);
            sev = *(PSEV)pggsevStart->QvFixedGet(isevStart);
            printf("\tSceneEvent(\n");
            printf("\t\tnfrm=%d,\n", sev.nfrm);

            switch (sev.sevt) {
                case sevtAddActr:
                    printf("\t\tsevt=sevtAddActr,\n");
                    break;

                case sevtSetBkgd: {
                    printf("\t\tsevt=sevtSetBkgd,\n");
                    TAG tag;
                    tag = *(PTAG)pggsevStart->QvGet(isevStart);
                    printf("\t\tTAG(\n");
                    printf("\t\t\tcno=%u,\n", tag.cno);

                    String background_tag;
                    background_tag.FFormatSz(PszLit("%f"), tag.ctg);
                    printf("\t\t\tctg='%s',\n", background_tag.Psz());

                    printf("\t\t\tpcrf=%p,\n", static_cast<void *>(tag.cache));
                    printf("\t\t\tsig=%lu\n", tag.sid);
                    printf("\t\t)\n");

                    break;
                }

                case sevtChngCamera: {
                    int32_t iangle;
                    iangle = *(int32_t*)pggsevStart->QvGet(isevStart);
                    printf("\t\tsevt=sevtChngCamera,\n");
                    printf("\t\tiangle=%d\n", iangle);
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

        ReleasePpo(&pggsevStart);
        return MM_CHUNKY_BODY_WRITTEN;

    case kctgTmpl:
        printf("4 char tag %d\n", ctg);
        return MM_CHUNKY_BODY_FALLBACK;
    }
    return MM_CHUNKY_BODY_FALLBACK;
}

} // namespace

bool MovieDecompiler::FDecompile(PChunkyFile pcflSrc, PMSNK pmsnk, PMSNK pmsnkError)
{
    AssertThis(0);
    AssertPo(pcflSrc, 0);
    const MMChunkyDecompileHooks hooks = {pcflSrc, DumpMovieBody, nullptr};
    _chse.Init(pmsnk, pmsnkError);
    _fError = !_chse.FDumpArchive(pcflSrc, &hooks);
    _chse.Uninit();
    return !_fError;
}
