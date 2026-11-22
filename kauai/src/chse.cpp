/* Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */

/***************************************************************************
    Author: ******
    Project: Kauai
    Reviewed:
    Copyright (c) Microsoft Corporation

    Chunky source emitter class implementation.

***************************************************************************/
#include "util.h"
#include "mm/chunky_decompile.h"
#include <cstring>
ASSERTNAME

namespace Chunky {

using ScriptCompiler::PCompilerBase;

RTCLASS(SourceEmitter)

namespace {

static_assert(kcchMaxStn == MM_CHUNKY_LISTING_LINE_LIMIT, "Chunky listing line must fit a native string");

void WriteListingLine(void *context, const void *text, size_t length, unsigned unit)
{
    ZString line;
    if (unit == sizeof(achar))
        std::memcpy(line, text, length * unit);
    else
        for (size_t index = 0; index < length; ++index)
            line[index] = static_cast<const unsigned char *>(text)[index];
    line[length] = 0;
    static_cast<SourceEmitter *>(context)->DumpSz(line);
}

int ReadByteLocation(void *context, MMU32 offset, void *data, size_t length)
{
    return static_cast<FileLocation *>(context)->FReadRgb(data, static_cast<LegacyLong>(length),
                                                         static_cast<FilePosition>(offset));
}

size_t TranslateListingString(void *, const void *input, size_t bytes, int system, void *output, size_t capacity)
{
    return CchTranslateRgb(const_cast<void *>(input), bytes, system, static_cast<achar *>(output), capacity);
}

void ReportListingIssue(void *context, MMChunkyDecompileIssue issue)
{
    if (issue == MM_CHUNKY_DECOMPILE_COLLECTION)
        Warn("file corrupt or not the requested collection");
    else
        static_cast<SourceEmitter *>(context)->Error(PszLit("Dumping chunk failed"));
}

} // namespace

/***************************************************************************
    Constructor for a chunky source emitter.
***************************************************************************/
SourceEmitter::SourceEmitter(void)
{
    AssertBaseThis(0);
    _pmsnkError = pvNil;
    _pmsnkDump = pvNil;
    AssertThis(0);
}

/***************************************************************************
    Destructor for a chunky source emitter.
***************************************************************************/
SourceEmitter::~SourceEmitter(void)
{
    Uninit();
}

/***************************************************************************
    Initialize the chunky source emitter.
***************************************************************************/
void SourceEmitter::Init(PMSNK pmsnkDump, PMSNK pmsnkError)
{
    AssertThis(0);
    AssertPo(pmsnkDump, 0);
    AssertNilOrPo(pmsnkError, 0);

    Uninit();

    _pmsnkDump = pmsnkDump;
    _pmsnkDump->AddRef();
    _pmsnkError = pmsnkError;
    if (_pmsnkError != pvNil)
        _pmsnkError->AddRef();

    AssertThis(fchseDump);
}

/***************************************************************************
    Clean up and return the chse to an inactive state.
***************************************************************************/
void SourceEmitter::Uninit(void)
{
    AssertThis(0);
    ReleasePpo(&_pmsnkDump);
    ReleasePpo(&_pmsnkError);
}

#ifdef DEBUG
/***************************************************************************
    Assert the validity of a SourceEmitter.
***************************************************************************/
void SourceEmitter::AssertValid(LegacyUlong grfchse)
{
    SourceEmitter_PAR::AssertValid(0);
    if (grfchse & fchseDump)
        AssertPo(_pmsnkDump, 0);
    AssertNilOrPo(_pmsnkError, 0);
}

/***************************************************************************
    Mark memory for the SourceEmitter.
***************************************************************************/
void SourceEmitter::MarkMem(void)
{
    AssertValid(0);
    SourceEmitter_PAR::MarkMem();
    MarkMemObj(_pmsnkError);
    MarkMemObj(_pmsnkDump);
}
#endif // DEBUG

/***************************************************************************
    Dumps chunk header.
***************************************************************************/
void SourceEmitter::DumpHeader(ChunkTagOrType ctg, ChunkNumber cno, PString pstnName, bool fPack)
{
    AssertThis(fchseDump);
    AssertNilOrPo(pstnName, 0);

    const MMChunkEntry entry = {ctg, cno};
    mm_chunky_list_header(&entry, pstnName ? pstnName->Prgch() : nullptr, pstnName ? pstnName->Cch() : 0,
                          sizeof(achar), WriteListingLine, this);
    if (fPack)
        DumpSz(PszLit("\tPACK"));
}

/***************************************************************************
    Dump a raw data chunk
***************************************************************************/
void SourceEmitter::DumpBlck(PDataBlock pblck)
{
    AssertThis(fchseDump);
    AssertPo(pblck, 0);

    FileLocation flo;

    if (pblck->FPacked())
        DumpSz(PszLit("PREPACKED"));
    if (!pblck->FGetFlo(&flo, fTrue))
    {
        Error(PszLit("Dumping chunk failed"));
        return;
    }
    const MMReadStream stream = {&flo, static_cast<MMU32>(flo.cb), ReadByteLocation};
    const bool dumped = mm_chunky_list_bytes(&stream, 1, WriteListingLine, this) != 0;
    ReleasePpo(&flo.pfil);
    if (!dumped)
        Error(PszLit("Dumping chunk failed"));
}

/***************************************************************************
    Dump raw data from memory.
***************************************************************************/
void SourceEmitter::DumpRgb(void *prgb, LegacyLong cb, LegacyLong cactTab)
{
    AssertThis(fchseDump);
    AssertIn(cb, 0, kcbMax);
    AssertIn(cactTab, 0, kcchMaxStn + 1);
    AssertPvCb(prgb, cb);

    if (cb > 0)
    {
        MMMemoryStream memory;
        if (!mm_memory_stream_init(&memory, prgb, cb) ||
            !mm_chunky_list_bytes(&memory.stream, cactTab, WriteListingLine, this))
            Error(PszLit("Dumping bytes failed"));
    }
}

bool SourceEmitter::FDumpArchive(PChunkyFile file, const MMChunkyDecompileHooks *hooks)
{
    AssertThis(fchseDump);
    const auto names = KauaiChunkOwnerTitleIO();
    auto reader = file->ReadOwner();
    const MMChunkyCollectionListing listing = {
        this, WriteListingLine, TranslateListingString, sizeof(achar), koskCur, koskWin, MacWin(0, 1)
    };
    const MMChunkyDecompile source = {
        file->ReadChunkAccess(), reader, &reader->payloads, &names, &listing, hooks, ReportListingIssue
    };
    return mm_chunky_decompile(&source) != 0;
}

void SourceEmitter::DumpPack(LegacyLong format)
{
    static_assert(cfmtNil == 0, "Chunky default codec marker");
    mm_chunky_list_pack(format, WriteListingLine, this);
}

/***************************************************************************
    Dump a parent directive
***************************************************************************/
void SourceEmitter::DumpParentCmd(ChunkTagOrType ctgPar, ChunkNumber cnoPar, ChildChunkID chid)
{
    AssertThis(fchseDump);

    String stn;

    stn.FFormatSz(PszLit("PARENT('%f', %d, %d)"), ctgPar, cnoPar, chid);
    DumpSz(stn.Psz());
}

/***************************************************************************
    Dump a bitmap directive
***************************************************************************/
void SourceEmitter::DumpBitmapCmd(byte bTransparent, LegacyLong dxp, LegacyLong dyp, PString pstnFile)
{
    AssertThis(fchseDump);
    AssertPo(pstnFile, 0);

    String stn;

    stn.FFormatSz(PszLit("BITMAP(%d, %d, %d) \"%s\""), (LegacyLong)bTransparent, dxp, dyp, pstnFile);
    DumpSz(stn.Psz());
}

/***************************************************************************
    Dump a file directive
***************************************************************************/
void SourceEmitter::DumpFileCmd(PString pstnFile, bool fPacked)
{
    AssertThis(fchseDump);
    AssertPo(pstnFile, 0);

    String stn;

    if (fPacked)
        stn.FFormatSz(PszLit("PACKEDFILE \"%s\""), pstnFile);
    else
        stn.FFormatSz(PszLit("FILE \"%s\""), pstnFile);
    DumpSz(stn.Psz());
}

/******************************************************************************
    Disassembles a script (pscpt) using the given script compiler (psccb)
    and dumps the result (including a "SCRIPTPF" directive).
******************************************************************************/
bool SourceEmitter::FDumpScript(PScript pscpt, PCompilerBase psccb)
{
    AssertThis(fchseDump);
    Assert(pscpt, "nil script");
    AssertPo(psccb, 0);

    DumpSz(PszLit("SCRIPTPF"));
    if (!psccb->FDisassemble(pscpt, _pmsnkDump, _pmsnkError))
    {
        Error(PszLit("Dumping script failed"));
        return fFalse;
    }
    return fTrue;
}

/******************************************************************************
    Dumps a DynamicArray or AllocatedArray, including the DynamicArray or AllocatedArray directive. pglb is the DynamicArray or AllocatedArray
    to dump.
******************************************************************************/
void SourceEmitter::DumpList(PVirtualArray pglb)
{
    AssertThis(fchseDump);
    AssertPo(pglb, 0);

    LegacyLong cbEntry;
    LegacyLong iv, ivMac;
    String stn;
    bool fAl = pglb->FIs(kclsAllocatedArray);

    Assert(fAl || pglb->FIs(kclsDynamicArray), "neither a DynamicArray or AllocatedArray!");

    // have a valid DynamicArray or AllocatedArray -- print it out in readable format
    cbEntry = pglb->CbEntry();
    AssertIn(cbEntry, 0, kcbMax);
    ivMac = pglb->IvMac();

    stn.FFormatSz(fAl ? PszLit("\tAL(%d)") : PszLit("\tGL(%d)"), cbEntry);
    DumpSz(stn.Psz());

    // print out the entries
    for (iv = 0; iv < ivMac; iv++)
    {
        if (pglb->FFree(iv))
            DumpSz(PszLit("\tFREE"));
        else
        {
            DumpSz(PszLit("\tITEM"));
            DumpRgb(pglb->PvLock(iv), cbEntry, 2);
            pglb->Unlock();
        }
    }
}

/******************************************************************************
    Dumps a GeneralGroup or AllocatedGroup, including the GeneralGroup or AllocatedGroup directive. pggb is the GeneralGroup or AllocatedGroup
    to dump.
******************************************************************************/
void SourceEmitter::DumpGroup(PVirtualGroup pggb)
{
    AssertThis(fchseDump);
    AssertPo(pggb, 0);

    LegacyLong cbFixed, cb;
    LegacyLong iv, ivMac;
    String stnT;
    bool fAg = pggb->FIs(kclsAllocatedGroup);

    Assert(fAg || pggb->FIs(kclsGeneralGroup), "neither a GeneralGroup or AllocatedGroup!");

    // have a valid GeneralGroup or AllocatedGroup -- print it out in readable format
    cbFixed = pggb->CbFixed();
    AssertIn(cbFixed, 0, kcbMax);
    ivMac = pggb->IvMac();

    stnT.FFormatSz(fAg ? PszLit("\tAG(%d)") : PszLit("\tGG(%d)"), cbFixed);
    DumpSz(stnT.Psz());

    // print out the entries
    for (iv = 0; iv < ivMac; iv++)
    {
        if (pggb->FFree(iv))
        {
            DumpSz(PszLit("\tFREE"));
            continue;
        }
        DumpSz(PszLit("\tITEM"));
        if (cbFixed > 0)
        {
            DumpRgb(pggb->PvFixedLock(iv), cbFixed, 2);
            pggb->Unlock();
        }
        if (0 < (cb = pggb->Cb(iv)))
        {
            DumpSz(PszLit("\tVAR"));
            DumpRgb(pggb->PvLock(iv), cb, 2);
            pggb->Unlock();
        }
    }
}

/******************************************************************************
    Dumps a StringTable_GST or AllocatedStringTable, including the StringTable_GST or AllocatedStringTable directive. pggb is the StringTable_GST or
    AllocatedStringTable to dump.
******************************************************************************/
bool SourceEmitter::FDumpStringTable(PVirtualStringTable pgstb)
{
    AssertThis(fchseDump);
    AssertPo(pgstb, 0);

    LegacyLong cbExtra;
    LegacyLong iv, ivMac;
    String stn1;
    String stn2;
    void *pvExtra = pvNil;
    bool fAst = pgstb->FIs(kclsAllocatedStringTable);

    Assert(fAst || pgstb->FIs(kclsStringTable_GST), "neither a StringTable_GST or AllocatedStringTable!");

    // have a valid StringTable_GST or AllocatedStringTable -- print it out in readable format
    cbExtra = pgstb->CbExtra();
    AssertIn(cbExtra, 0, kcbMax);
    ivMac = pgstb->IvMac();

    if (cbExtra > 0 && !FAllocPv(&pvExtra, cbExtra, fmemNil, mprNormal))
        return fFalse;

    stn1.FFormatSz(fAst ? PszLit("\tAST(%d)") : PszLit("\tGST(%d)"), cbExtra);
    DumpSz(stn1.Psz());

    // print out the entries
    for (iv = 0; iv < ivMac; iv++)
    {
        if (pgstb->FFree(iv))
        {
            DumpSz(PszLit("\tFREE"));
            continue;
        }
        DumpSz(PszLit("\tITEM"));

        pgstb->GetStn(iv, &stn2);
        stn2.FExpandControls();
        stn1.FFormatSz(PszLit("\t\t\"%s\""), &stn2);
        DumpSz(stn1.Psz());

        if (cbExtra > 0)
        {
            pgstb->GetExtra(iv, pvExtra);
            DumpRgb(pvExtra, cbExtra, 2);
        }
    }

    FreePpv(&pvExtra);
    return fTrue;
}

} // end of namespace Chunky
