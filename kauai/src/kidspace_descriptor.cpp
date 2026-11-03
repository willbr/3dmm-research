/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "kidspace_descriptor.h"
ASSERTNAME
RTCLASS(KidspaceGraphicObjectDescriptor)
RTCLASS(KidspaceGraphicObjectDescriptorLocation)

/***************************************************************************
    Static method to read a KidspaceGraphicObjectDescriptorLocation from the ChunkyResourceFile. This is a
ChunkyResourceFile object reader.
***************************************************************************/
bool KidspaceGraphicObjectDescriptorLocation::FReadGkds(PChunkyResourceFile pcrf, ChunkTagOrType ctg, ChunkNumber cno,
                                                        PDataBlock pblck, PBaseCacheableObject *ppbaco, LegacyLong *pcb)
{
    PKidspaceGraphicObjectDescriptorLocation pgkds;
    GOKDF gokdf;
    HQ hq;
    LOP *qlop;
    long cb;

    *pcb = pblck->Cb(fTrue);
    if (pvNil == ppbaco)
        return fTrue;

    if (!pblck->FUnpackData())
        return fFalse;
    *pcb = pblck->Cb();

    if (*pcb < size(GOKDF) + size(LOP) || CbRoundToLong(*pcb) != *pcb)
    {
        Bug("Bad KidspaceGraphicObjectDescriptor");
        return fFalse;
    }

    if (!pblck->FReadRgb(&gokdf, size(GOKDF), 0))
        return fFalse;
    if (gokdf.bo == kboOther)
        SwapBytesBom(&gokdf, kbomGokdf);
    else if (gokdf.bo != kboCur)
    {
        Bug("Bad KidspaceGraphicObjectDescriptor 2");
        return fFalse;
    }

    if (!pblck->FMoveMin(size(gokdf)))
        return fFalse;
    hq = pblck->HqFree();
    if (hqNil == hq)
        return fFalse;
    cb = CbOfHq(hq);

    if (pvNil == (pgkds = NewObj KidspaceGraphicObjectDescriptorLocation))
    {
        FreePhq(&hq);
        return fFalse;
    }
    pgkds->_hqData = hq;
    pgkds->_gokk = gokdf.gokk;
    if (gokdf.bo == kboOther)
        SwapBytesRglw(QvFromHq(hq), cb / size(int32_t));

    qlop = (LOP *)QvFromHq(hq);
    for (pgkds->_clop = 0;; qlop++)
    {
        if (cb < size(LOP))
        {
            Bug("Bad LOP list in KidspaceGraphicObjectDescriptor");
            ReleasePpo(&pgkds);
            return fFalse;
        }
        pgkds->_clop++;
        cb -= size(LOP);
        if (hidNil == qlop->hidPar)
            break;
    }
    if ((cb % size(CursorMapEntry)) != 0)
    {
        Bug("Bad CursorMapEntry list in KidspaceGraphicObjectDescriptor");
        ReleasePpo(&pgkds);
        return fFalse;
    }
    pgkds->_ccume = cb / size(CursorMapEntry);
    *ppbaco = pgkds;
    return fTrue;
}

/***************************************************************************
    Destructor for a KidspaceGraphicObjectDescriptorLocation object.
***************************************************************************/
KidspaceGraphicObjectDescriptorLocation::~KidspaceGraphicObjectDescriptorLocation(void)
{
    FreePhq(&_hqData);
}

#ifdef DEBUG
/***************************************************************************
    Assert the validity of a KidspaceGraphicObjectDescriptorLocation.
***************************************************************************/
void KidspaceGraphicObjectDescriptorLocation::AssertValid(LegacyUlong grf)
{
    LOP *qrglop;

    KidspaceGraphicObjectDescriptorLocation_PAR::AssertValid(0);
    AssertHq(_hqData);
    AssertIn(_clop, 0, kcbMax);
    AssertIn(_ccume, 0, kcbMax);
    Assert(LwMul(_clop, size(LOP)) + LwMul(_ccume, size(CursorMapEntry)) == CbOfHq(_hqData),
           "KidspaceGraphicObjectDescriptorLocation _hqData wrong size");
    qrglop = (LOP *)QvFromHq(_hqData);
    Assert(qrglop[_clop - 1].hidPar == hidNil, "bad rglop in KidspaceGraphicObjectDescriptorLocation");
}

/***************************************************************************
    Mark memory for the KidspaceGraphicObjectDescriptorLocation.
***************************************************************************/
void KidspaceGraphicObjectDescriptorLocation::MarkMem(void)
{
    AssertValid(0);
    KidspaceGraphicObjectDescriptorLocation_PAR::MarkMem();
    MarkHq(_hqData);
}
#endif // DEBUG

/***************************************************************************
    Return the KidspaceGraphicObject kind id.
***************************************************************************/
long KidspaceGraphicObjectDescriptorLocation::Gokk(void)
{
    AssertThis(0);
    return _gokk;
}

/***************************************************************************
    Look for a cursor map entry in this KidspaceGraphicObjectDescriptorLocation.
***************************************************************************/
bool KidspaceGraphicObjectDescriptorLocation::FGetCume(ulong grfcust, long sno, CursorMapEntry *pcume)
{
    AssertThis(0);
    AssertVarMem(pcume);
    CursorMapEntry *qcume;
    long ccume;
    LegacyUlong fbitSno = (LegacyUlong{1} << (sno & 0x1F));

    if (0 == _ccume)
        return fFalse;

    qcume = (CursorMapEntry *)PvAddBv(QvFromHq(_hqData), LwMul(_clop, size(LOP)));
    for (ccume = _ccume; ccume > 0; ccume--, qcume++)
    {
        if ((qcume->grfbitSno & fbitSno) && (qcume->grfcustMask & grfcust) == qcume->grfcust)
        {
            *pcume = *qcume;
            return fTrue;
        }
    }
    TrashVar(pcume);
    return fFalse;
}

/***************************************************************************
    Get the location map entry from the parent id.
***************************************************************************/
void KidspaceGraphicObjectDescriptorLocation::GetLop(long hidPar, LOP *plop)
{
    AssertThis(0);
    AssertVarMem(plop);
    LOP *qlop;

    for (qlop = (LOP *)QvFromHq(_hqData);; qlop++)
    {
        if (hidNil == qlop->hidPar || hidPar == qlop->hidPar)
            break;
    }
    *plop = *qlop;
}
