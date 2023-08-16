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
ASSERTNAME

namespace Chunky {

using ScriptCompiler::PCompilerBase;

RTCLASS(SourceEmitter)

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
    if (_bsf.IbMac() > 0)
        _bsf.FReplace(pvNil, 0, 0, _bsf.IbMac());
}

#ifdef DEBUG
/***************************************************************************
    Assert the validity of a SourceEmitter.
***************************************************************************/
void SourceEmitter::AssertValid(ulong grfchse)
{
    SourceEmitter_PAR::AssertValid(0);
    AssertPo(&_bsf, 0);
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
    MarkMemObj(&_bsf);
}
#endif // DEBUG

/***************************************************************************
    Dumps chunk header.
***************************************************************************/
void SourceEmitter::DumpHeader(ChunkTag ctg, ChunkNumber cno, PSTN pstnName, bool fPack)
{
    AssertThis(fchseDump);
    AssertNilOrPo(pstnName, 0);

    STN stnT;

    if (pstnName != pvNil)
    {
        STN stnName;

        stnName = *pstnName;
        stnName.FExpandControls();
        stnT.FFormatSz(PszLit("CHUNK('%f', %d, \"%s\")"), ctg, cno, &stnName);
    }
    else
        stnT.FFormatSz(PszLit("CHUNK('%f', %d)"), ctg, cno);

    DumpSz(stnT.Psz());
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

    FLO flo;

    if (pblck->FPacked())
        DumpSz(PszLit("PREPACKED"));
    if (!pblck->FGetFlo(&flo, fTrue))
    {
        Error(PszLit("Dumping chunk failed"));
        return;
    }
    _bsf.FReplaceFlo(&flo, fFalse, 0, _bsf.IbMac());
    ReleasePpo(&flo.pfil);
    _DumpBsf(1);
}

/***************************************************************************
    Dump raw data from memory.
***************************************************************************/
void SourceEmitter::DumpRgb(void *prgb, long cb, long cactTab)
{
    AssertThis(fchseDump);
    AssertIn(cb, 0, kcbMax);
    AssertPvCb(prgb, cb);

    if (cb > 0)
    {
        _bsf.FReplace(prgb, cb, 0, _bsf.IbMac());
        _DumpBsf(cactTab);
    }
}

/***************************************************************************
    Dump a parent directive
***************************************************************************/
void SourceEmitter::DumpParentCmd(ChunkTag ctgPar, ChunkNumber cnoPar, ChildChunkID chid)
{
    AssertThis(fchseDump);

    STN stn;

    stn.FFormatSz(PszLit("PARENT('%f', %d, %d)"), ctgPar, cnoPar, chid);
    DumpSz(stn.Psz());
}

/***************************************************************************
    Dump a bitmap directive
***************************************************************************/
void SourceEmitter::DumpBitmapCmd(byte bTransparent, long dxp, long dyp, PSTN pstnFile)
{
    AssertThis(fchseDump);
    AssertPo(pstnFile, 0);

    STN stn;

    stn.FFormatSz(PszLit("BITMAP(%d, %d, %d) \"%s\""), (long)bTransparent, dxp, dyp, pstnFile);
    DumpSz(stn.Psz());
}

/***************************************************************************
    Dump a file directive
***************************************************************************/
void SourceEmitter::DumpFileCmd(PSTN pstnFile, bool fPacked)
{
    AssertThis(fchseDump);
    AssertPo(pstnFile, 0);

    STN stn;

    if (fPacked)
        stn.FFormatSz(PszLit("PACKEDFILE \"%s\""), pstnFile);
    else
        stn.FFormatSz(PszLit("FILE \"%s\""), pstnFile);
    DumpSz(stn.Psz());
}

/***************************************************************************
    Dump an adopt directive
***************************************************************************/
void SourceEmitter::DumpAdoptCmd(ChunkIdentification *pcki, ChildChunkIdentification *pkid)
{
    AssertThis(fchseDump);
    AssertVarMem(pcki);
    AssertVarMem(pkid);

    STN stn;

    stn.FFormatSz(PszLit("ADOPT('%f', %d, '%f', %d, %d)"), pcki->ctg, pcki->cno, pkid->cki.ctg, pkid->cki.cno,
                  pkid->chid);
    DumpSz(stn.Psz());
}

/***************************************************************************
    Dump the data in the _bsf
***************************************************************************/
void SourceEmitter::_DumpBsf(long cactTab)
{
    AssertThis(fchseDump);
    AssertIn(cactTab, 0, kcchMaxStn + 1);

    byte rgb[8], bT;
    STN stn1;
    STN stn2;
    long cact;
    long ib, ibMac;
    long cb, ibT;

    stn1.SetNil();
    for (cact = cactTab; cact-- > 0;)
        stn1.FAppendCh(kchTab);
    stn1.FAppendSz(PszLit("BYTE"));
    DumpSz(stn1.Psz());

    ibMac = _bsf.IbMac();
    for (ib = 0; ib < ibMac;)
    {
        stn1.SetNil();
        for (cact = cactTab; cact-- > 0;)
            stn1.FAppendCh(kchTab);

        cb = LwMin(ibMac - ib, size(rgb));
        _bsf.FetchRgb(ib, cb, rgb);

        // append the hex
        for (ibT = 0; ibT < size(rgb); ibT++)
        {
            if (ibT >= cb)
                stn1.FAppendSz(PszLit("     "));
            else
            {
                stn2.FFormatSz(PszLit("0x%02x "), rgb[ibT]);
                stn1.FAppendStn(&stn2);
            }
        }
        stn1.FAppendSz(PszLit("   // '"));

        // append the ascii
        for (ibT = 0; ibT < cb; ibT++)
        {
            bT = rgb[ibT];
            if (bT < 0x20 || bT == 0x7F)
                bT = '?';
            stn1.FAppendCh((achar)bT);
        }
        stn1.FAppendSz(PszLit("' "));

        DumpSz(stn1.Psz());
        ib += cb;
    }
}

/******************************************************************************
    Disassembles a script (pscpt) using the given script compiler (psccb)
    and dumps the result (including a "SCRIPTPF" directive).
******************************************************************************/
bool SourceEmitter::FDumpScript(PScript pscpt, PCompilerBase psccb)
{
    AssertThis(fchseDump);
    AssertPo(pscpt, 0);
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

    long cbEntry;
    long iv, ivMac;
    STN stn;
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
            _bsf.FReplace(pglb->PvLock(iv), cbEntry, 0, _bsf.IbMac());
            pglb->Unlock();
            _DumpBsf(2);
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

    long cbFixed, cb;
    long iv, ivMac;
    STN stnT;
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
            _bsf.FReplace(pggb->PvFixedLock(iv), cbFixed, 0, _bsf.IbMac());
            pggb->Unlock();
            _DumpBsf(2);
        }
        if (0 < (cb = pggb->Cb(iv)))
        {
            DumpSz(PszLit("\tVAR"));
            _bsf.FReplace(pggb->PvLock(iv), cb, 0, _bsf.IbMac());
            pggb->Unlock();
            _DumpBsf(2);
        }
    }
}

/******************************************************************************
    Dumps a StringTable or AllocatedStringTable, including the StringTable or AllocatedStringTable directive. pggb is the StringTable or
    AllocatedStringTable to dump.
******************************************************************************/
bool SourceEmitter::FDumpStringTable(PVirtualStringTable pgstb)
{
    AssertThis(fchseDump);
    AssertPo(pgstb, 0);

    long cbExtra;
    long iv, ivMac;
    STN stn1;
    STN stn2;
    void *pvExtra = pvNil;
    bool fAst = pgstb->FIs(kclsAllocatedStringTable);

    Assert(fAst || pgstb->FIs(kclsStringTable), "neither a StringTable or AllocatedStringTable!");

    // have a valid StringTable or AllocatedStringTable -- print it out in readable format
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
            _bsf.FReplace(pvExtra, cbExtra, 0, _bsf.IbMac());
            _DumpBsf(2);
        }
    }

    FreePpv(&pvExtra);
    return fTrue;
}

} // end of namespace Chunky
