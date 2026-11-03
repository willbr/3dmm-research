/* Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */
#include "soc_core.h"
#include "movie_data.h"
ASSERTNAME

namespace
{
const short kcvnCur = 2;
const short kcvnBack = 2;
const short kcvnMin = 1;
} // namespace

MovieData::~MovieData()
{
    ClearActors();
    ReleasePpo(&resources);
}
void MovieData::ClearActors()
{
    if (actors == pvNil)
        return;
    for (LegacyLong i = 0; i < actors->IvMac(); ++i)
    {
        RollCallActorEntry actor;
        actors->GetExtra(i, &actor);
        TagManager::CloseTag(&actor.tagTmpl);
    }
    ReleasePpo(&actors);
}

bool MovieData::FInitEmpty()
{
    MovieData fresh;
    PChunkyFile file = ChunkyFile::PcflCreateTemp();
    if (file == pvNil)
        return fFalse;
    fresh.resources = ChunkyResourceFile::PcrfNew(file, 0);
    ReleasePpo(&file);
    fresh.actors = StringTable_GST::PgstNew(size(RollCallActorEntry));
    if (fresh.resources == pvNil || fresh.actors == pvNil)
        return fFalse;
    if (!FCreateRoot(fresh.resources->Pcfl(), &fresh.movieNumber))
        return fFalse;
    ClearActors();
    ReleasePpo(&resources);
    resources = fresh.resources;
    fresh.resources = pvNil;
    actors = fresh.actors;
    fresh.actors = pvNil;
    movieNumber = fresh.movieNumber;
    actorIdLimit = sceneCount = 0;
    title = fresh.title;
    return fTrue;
}

bool MovieData::FLoad(PFilename filename, ChunkNumber number)
{
    MovieData loaded;
    PChunkyFile file = ChunkyFile::PcflOpen(filename, fcflNil);
    if (file == pvNil)
        return fFalse;
    bool valid = FVerifyVersion(file, &number);
    if (valid)
        loaded.resources = ChunkyResourceFile::PcrfNew(file, 0);
    ReleasePpo(&file);
    if (!valid || loaded.resources == pvNil)
        return fFalse;
    loaded.movieNumber = number;
    if (!FReadRollCall(loaded.resources, number, &loaded.actors, &loaded.actorIdLimit))
        return fFalse;
    file = loaded.resources->Pcfl(); // borrowed until loaded relinquishes ownership
    file->FGetName(kctgMvie, number, &loaded.title);
    ChildChunkIdentification child;
    while (file->FGetKidChidCtg(kctgMvie, number, loaded.sceneCount, kctgScen, &child))
        ++loaded.sceneCount;
    ClearActors();
    ReleasePpo(&resources);
    resources = loaded.resources;
    loaded.resources = pvNil;
    actors = loaded.actors;
    loaded.actors = pvNil;
    movieNumber = loaded.movieNumber;
    actorIdLimit = loaded.actorIdLimit;
    sceneCount = loaded.sceneCount;
    title = loaded.title;
    return fTrue;
}

bool MovieData::FSave(PFilename filename)
{
    if (resources == pvNil || actors == pvNil)
        return fFalse;
    PChunkyFile file = resources->Pcfl();
    ChildChunkIdentification previous;
    bool hadPrevious = file->FGetKidChidCtg(kctgMvie, movieNumber, 0, kctgGst, &previous);
    ChunkNumber written;
    if (!FWriteRollCall(file, movieNumber, actors, &written))
        return fFalse;
    if (hadPrevious)
        file->DeleteChild(kctgMvie, movieNumber, kctgGst, previous.cki.cno, 0);
    if (!file->FSetName(kctgMvie, movieNumber, &title) || !file->FSave(kctgSoc, filename))
        return fFalse;
    // A successful explicit save makes the backing file persistent. Without
    // this, exporting a temporary archive would delete it on destruction.
    file->SetTemp(fFalse);
    return fTrue;
}

bool MovieData::FCreateRoot(PChunkyFile file, ChunkNumber *number)
{
    DataBlock block;
    if (!file->FAdd(size(MovieFilePrefix), kctgMvie, number, &block))
        return fFalse;
    MovieFilePrefix prefix;
    prefix.bo = kboCur;
    prefix.osk = koskCur;
    prefix.dver.Set(kcvnCur, kcvnBack);
    if (block.FWrite(&prefix))
        return fTrue;
    file->Delete(kctgMvie, *number);
    *number = cnoNil;
    return fFalse;
}

bool MovieData::FVerifyVersion(PChunkyFile pcfl, ChunkNumber *pcno)
{
    AssertPo(pcfl, 0);

    ChildChunkIdentification kid;
    ChunkNumber cnoMvie = pcno != pvNil ? *pcno : cnoNil;
    MovieFilePrefix mfp;
    DataBlock blck;

    // Get the cno of the first kid of the movie
    if (pvNil == pcno || cnoNil == *pcno)
    {
        if (!pcfl->FGetCkiCtg(kctgMvie, 0, &(kid.cki)))
        {
            PushErc(ercSocBadFile);
            return fFalse;
        }
        cnoMvie = kid.cki.cno;
        if (pvNil != pcno)
            *pcno = cnoMvie;
    }

    // Get version number of the file
    if (!pcfl->FFind(kctgMvie, cnoMvie, &blck) || !blck.FUnpackData() || (blck.Cb() != size(MovieFilePrefix)) ||
        !blck.FReadRgb(&mfp, size(MovieFilePrefix), 0))
    {
        PushErc(ercSocBadFile);
        return fFalse;
    }

    if (mfp.bo == kboOther)
    {
        SwapBytesBom(&mfp, kbomMfp);
    }

    if (mfp.bo != kboCur)
    {
        PushErc(ercSocBadFile);
        return fFalse;
    }

    // Check the version numbers
    if (!mfp.dver.FReadable(kcvnCur, kcvnMin))
    {
        PushErc(ercSocBadVersion);
        return fFalse;
    }
    return fTrue;
}

bool MovieData::FReadRollCall(PChunkyResourceFile pcrf, ChunkNumber cno, PStringTable_GST *ppgst, LegacyLong *paridLim)
{
    AssertPo(pcrf, 0);
    AssertVarMem(ppgst);
    Assert(*ppgst == pvNil, "Overwriting existing StringTable_GST");
    AssertNilOrVarMem(paridLim);

    short bo;
    LegacyLong imactr, imactrMac;
    LegacyLong aridLimit = paridLim != pvNil ? *paridLim : 0;
    PChunkyFile pcfl = pcrf->Pcfl();
    ChildChunkIdentification kid;
    DataBlock blck;
    PStringTable_GST pgstOnFile = pvNil;
    PStringTable_GST pgstRuntime = pvNil;
    String stn;
    RollCallActorEntryOnFile mactrOnFile;
    RollCallActorEntry mactr;

    if (!pcfl->FGetKidChidCtg(kctgMvie, cno, 0, kctgGst, &kid) || !pcfl->FFind(kid.cki.ctg, kid.cki.cno, &blck))
    {
        PushErc(ercSocBadFile);
        goto LFail;
    }

    // Read the wire-format GST (extras are sized RollCallActorEntryOnFile).
    pgstOnFile = StringTable_GST::PgstRead(&blck, &bo);
    if (pgstOnFile == pvNil || pgstOnFile->CbExtra() != size(RollCallActorEntryOnFile))
        goto LFail;

    // Marshal into a fresh runtime GST whose extras are sized RollCallActorEntry
    // (which on x64 is wider than the wire form because TAG embeds a pointer).
    pgstRuntime = StringTable_GST::PgstNew(size(RollCallActorEntry));
    if (pgstRuntime == pvNil)
        goto LFail;

    imactrMac = pgstOnFile->IvMac();
    for (imactr = 0; imactr < imactrMac; imactr++)
    {
        pgstOnFile->GetStn(imactr, &stn);
        pgstOnFile->GetExtra(imactr, &mactrOnFile);
        if (bo == kboOther)
            SwapBytesBom(&mactrOnFile, kbomMactr);

        mactr.arid = mactrOnFile.arid;
        mactr.cactRef = mactrOnFile.cactRef;
        mactr.grfbrws = mactrOnFile.grfbrws;
        TagFromOnFile(&mactr.tagTmpl, mactrOnFile.tagTmpl);

        if (mactr.arid < 0 || mactr.arid == INT32_MAX || mactr.tagTmpl.sid < 0)
            goto LFail;
        if (mactr.arid >= aridLimit)
            aridLimit = mactr.arid + 1;

        // Open the tags, since they might be ThreeDTexts. FOpenTag may
        // populate mactr.tagTmpl.pcrf for ksidUseCrf tags; that pcrf is the
        // owning ref balanced by CloseTag, so it must land in the runtime GST.
        if (!TagManager::FOpenTag(&mactr.tagTmpl, pcrf))
            goto LFail;

        if (!pgstRuntime->FAddStn(&stn, &mactr))
        {
            TagManager::CloseTag(&mactr.tagTmpl);
            goto LFail;
        }
    }

    ReleasePpo(&pgstOnFile);
    *ppgst = pgstRuntime;
    if (paridLim != pvNil)
        *paridLim = aridLimit;
    return fTrue;
LFail:
    ReleasePpo(&pgstOnFile);
    if (pgstRuntime != pvNil)
    {
        for (LegacyLong i = 0; i < pgstRuntime->IvMac(); ++i)
        {
            pgstRuntime->GetExtra(i, &mactr);
            TagManager::CloseTag(&mactr.tagTmpl);
        }
    }
    ReleasePpo(&pgstRuntime);
    *ppgst = pvNil;
    return fFalse;
}

bool MovieData::FWriteRollCall(PChunkyFile pcfl, ChunkNumber number, PStringTable_GST actors, ChunkNumber *written)
{
    if (actors == pvNil || actors->CbExtra() != size(RollCallActorEntry))
        return fFalse;
    DataBlock blck;
    // Marshal the runtime GST into a transient wire-format GST whose
    // extras are sized RollCallActorEntryOnFile (28 bytes on every arch).
    // The runtime GST holds RollCallActorEntry which on x64 is wider, so
    // we cannot FWrite it directly without breaking the .3MM wire format.
    PStringTable_GST pgstOnFile = StringTable_GST::PgstNew(size(RollCallActorEntryOnFile));
    if (pvNil == pgstOnFile)
        return fFalse;
    LegacyLong imactrSave, imactrMacSave = actors->IvMac();
    String stnSave;
    RollCallActorEntry mactrSave;
    RollCallActorEntryOnFile mactrSaveOnFile;
    bool fOk = fTrue;
    for (imactrSave = 0; imactrSave < imactrMacSave && fOk; imactrSave++)
    {
        actors->GetStn(imactrSave, &stnSave);
        actors->GetExtra(imactrSave, &mactrSave);
        mactrSaveOnFile.arid = mactrSave.arid;
        mactrSaveOnFile.cactRef = mactrSave.cactRef;
        mactrSaveOnFile.grfbrws = mactrSave.grfbrws;
        mactrSaveOnFile.tagTmpl.From(mactrSave.tagTmpl);
        if (!pgstOnFile->FAddStn(&stnSave, &mactrSaveOnFile))
            fOk = fFalse;
    }
    if (!fOk || !pcfl->FAdd(pgstOnFile->CbOnFile(), kctgGst, written, &blck))
    {
        ReleasePpo(&pgstOnFile);
        return fFalse;
    }
    if (!pgstOnFile->FWrite(&blck) || !pcfl->FAdoptChild(kctgMvie, number, kctgGst, *written, 0))
    {
        ReleasePpo(&pgstOnFile);
        pcfl->Delete(kctgGst, *written);
        return fFalse;
    }
    ReleasePpo(&pgstOnFile);
    return fTrue;
}
