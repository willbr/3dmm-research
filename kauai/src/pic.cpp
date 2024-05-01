/* Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */

/***************************************************************************
    Author: ShonK
    Project: Kauai
    Reviewed:
    Copyright (c) Microsoft Corporation

    Common OS-independent picture routines.

***************************************************************************/
#include "frame.h"
ASSERTNAME

RTCLASS(Picture)

/***************************************************************************
    Static method to create a new Picture based on the given HPIC with
    bounding rectangle *prc. If this fails, it is the callers responsibility
    to free the hpic. If it succeeds, the ppic returned owns the hpic.
***************************************************************************/
PPicture Picture::PpicNew(HPIC hpic, RC *prc)
{
    Assert(hpic != hNil, "nil hpic");
    AssertVarMem(prc);
    PPicture ppic;

    if (pvNil == (ppic = NewObj Picture))
        return pvNil;

    ppic->_hpic = hpic;
    ppic->_rc = *prc;
    AssertPo(ppic, 0);
    return ppic;
}

/***************************************************************************
    Get the natural rectangle for the picture.
***************************************************************************/
void Picture::GetRc(RC *prc)
{
    AssertThis(0);
    *prc = _rc;
}

/***************************************************************************
    Add the picture to the chunky file. The OS specific representation
    will be a child of the chunk and have the given chid value.
***************************************************************************/
bool Picture::FAddToCfl(PChunkyFile pcfl, ChunkTag ctg, ChunkNumber *pcno, ChildChunkID chid)
{
    AssertThis(0);
    AssertPo(pcfl, 0);
    AssertVarMem(pcno);
    DataBlock blck;
    ChunkNumber cnoKid;
    long cb;

    if (!pcfl->FAdd(0, ctg, pcno))
        return fFalse;
    cb = CbOnFile();
    if (!pcfl->FAddChild(ctg, *pcno, chid, cb, kctgPictNative, &cnoKid, &blck) || !FWrite(&blck))
    {
        pcfl->Delete(ctg, *pcno);
        TrashVar(pcno);
        return fFalse;
    }
    return fTrue;
}

/***************************************************************************
    Put the picture as the given ctg and cno in the chunky file. The
    OS specific representation will be a child of the chunk and have the
    given chid value.
***************************************************************************/
bool Picture::FPutInCfl(PChunkyFile pcfl, ChunkTag ctg, ChunkNumber cno, ChildChunkID chid)
{
    AssertThis(0);
    AssertPo(pcfl, 0);
    bool fDelOnFail;
    long ikid;
    ChildChunkIdentification kid;
    DataBlock blck;
    ChunkNumber cnoKid;
    long cb;

    fDelOnFail = !pcfl->FFind(ctg, cno);
    if (!pcfl->FPut(0, ctg, cno))
        goto LFail;
    cb = CbOnFile();
    if (!pcfl->FAddChild(ctg, cno, chid, cb, kctgPictNative, &cnoKid, &blck))
        goto LFail;
    if (!FWrite(&blck))
    {
        if (!fDelOnFail)
            pcfl->DeleteChild(ctg, cno, kctgPictNative, cnoKid, chid);
    LFail:
        if (fDelOnFail)
            pcfl->Delete(ctg, cno);
        return fFalse;
    }

    // delete all other reps with the same ctg and chid
    for (ikid = pcfl->Ckid(ctg, cno); ikid-- > 0;)
    {
        AssertDo(pcfl->FGetKid(ctg, cno, ikid, &kid), 0);
        if (kid.cki.ctg == kctgPictNative && kid.chid == chid && kid.cki.cno != cnoKid)
            pcfl->DeleteChild(ctg, cno, kid.cki.ctg, kid.cki.cno, kid.chid);
    }
    return fTrue;
}

#ifdef DEBUG
/***************************************************************************
    Assert the validity of a Picture.
***************************************************************************/
void Picture::AssertValid(ulong grf)
{
    Picture_PAR::AssertValid(0);
    Assert(_hpic != hNil, "bad hpic");
}
#endif // DEBUG

/***************************************************************************
    A PFNRPO to read Picture 0 from a GRAF chunk.
***************************************************************************/
bool FReadMainPic(PChunkyFile pcfl, ChunkTag ctg, ChunkNumber cno, PDataBlock pblck, PBaseCacheableObject *ppbaco, long *pcb)
{
    PPicture ppic;

    if (pvNil == ppbaco)
    {
        // REVIEW shonk: get a better estimate of the real size -
        *pcb = 1;
        return fTrue;
    }

    if (pvNil == (ppic = Picture::PpicFetch(pcfl, ctg, cno)))
    {
        TrashVar(pcb);
        TrashVar(ppbaco);
        return fFalse;
    }

    *pcb = ppic->CbOnFile();
    *ppbaco = ppic;
    return fTrue;
}
