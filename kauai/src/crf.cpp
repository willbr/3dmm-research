/* Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */

/***************************************************************************
    Author: ShonK
    Project: Kauai
    Reviewed:
    Copyright (c) Microsoft Corporation

    Chunky resource management.

    WARNING: BACOs should only be released or fetched from the main
    thread! CRFs are NOT thread safe! Alternatively, the BaseCacheableObject can be
    detached from the ChunkyResourceFile (in the main thread), then later released
    in a different thread.

***************************************************************************/
#include "util.h"
ASSERTNAME

namespace Chunky {

RTCLASS(BaseCacheableObject)
RTCLASS(GHQ)
RTCLASS(RCA)
RTCLASS(ChunkyResourceFile)
RTCLASS(ChunkyResourceManager)
RTCLASS(GenericCacheableObject)

/***************************************************************************
    Constructor for base cacheable object.
***************************************************************************/
BaseCacheableObject::BaseCacheableObject(void)
{
    AssertBaseThis(fobjAllocated);
    _pcrf = pvNil;
    _crep = crepToss;
    _fAttached = fFalse;
}

/***************************************************************************
    Destructor.
***************************************************************************/
BaseCacheableObject::~BaseCacheableObject(void)
{
    AssertBaseThis(fobjAllocated);
    Assert(!_fAttached, "still attached");
    ReleasePpo(&_pcrf);
}

/***************************************************************************
    Write the BaseCacheableObject to a FLO - just make the FLO a DataBlock and write to
    the block.
***************************************************************************/
bool BaseCacheableObject::FWriteFlo(PFLO pflo)
{
    AssertThis(0);
    AssertPo(pflo, 0);
    DataBlock blck(pflo);
    return FWrite(&blck);
}

/***************************************************************************
    Placeholder function for BaseCacheableObject generic writer.
***************************************************************************/
bool BaseCacheableObject::FWrite(PDataBlock pblck)
{
    AssertThis(0);
    RawRtn(); // Derived class should be defining this
    return fFalse;
}

/***************************************************************************
    Placeholder function for BaseCacheableObject generic cb-getter.
***************************************************************************/
long BaseCacheableObject::CbOnFile(void)
{
    AssertThis(0);
    RawRtn(); // Derived class should be defining this
    return 0;
}

#ifdef DEBUG
/***************************************************************************
    Assert the validity of a BaseCacheableObject.
***************************************************************************/
void BaseCacheableObject::AssertValid(ulong grf)
{
    BaseCacheableObject_PAR::AssertValid(fobjAllocated);
    Assert(!_fAttached || pvNil != _pcrf, "attached baco has no crf");
    AssertNilOrVarMem(_pcrf);
}

/***************************************************************************
    Mark memory for the BaseCacheableObject.
***************************************************************************/
void BaseCacheableObject::MarkMem(void)
{
    AssertValid(0);
    BaseCacheableObject_PAR::MarkMem();
    if (!_fAttached)
        MarkMemObj(_pcrf);
}
#endif // DEBUG

/***************************************************************************
    Release a reference to the BaseCacheableObject.  If the reference count goes to zero
    and the BaseCacheableObject is not attached, it is deleted.
***************************************************************************/
void BaseCacheableObject::Release(void)
{
    AssertThis(0);
    if (_cactRef-- <= 0)
    {
        Bug("calling Release without an AddRef");
        _cactRef = 0;
    }
    if (_cactRef == 0)
    {
        if (!_fAttached)
            delete this;
        else
        {
            AssertPo(_pcrf, 0);
            _pcrf->BacoReleased(this);
        }
    }
}

/***************************************************************************
    Detach a BaseCacheableObject from its ChunkyResourceFile.
***************************************************************************/
void BaseCacheableObject::Detach(void)
{
    AssertThis(0);
    if (_fAttached)
    {
        AssertPo(_pcrf, 0);
        _pcrf->AddRef();
        _fAttached = fFalse;
        _pcrf->BacoDetached(this);
    }
    if (_cactRef <= 0)
        delete this;
}

/***************************************************************************
    Set the crep for the BaseCacheableObject.
***************************************************************************/
void BaseCacheableObject::SetCrep(long crep)
{
    AssertThis(0);
    // An AddRef followed by Release is done so that BacoReleased() is
    // called if this BaseCacheableObject's _cactRef is 0...if crep is crepToss, this
    // detaches this BaseCacheableObject from the cache.
    AddRef();
    _crep = crep;
    Release();
}

/***************************************************************************
    Constructor for ChunkyResourceFile.  Increments the open count on the ChunkyFile.
***************************************************************************/
ChunkyResourceFile::ChunkyResourceFile(PChunkyFile pcfl, long cbMax)
{
    AssertBaseThis(fobjAllocated);
    AssertPo(pcfl, 0);
    AssertIn(cbMax, 0, kcbMax);

    pcfl->AddRef();
    _pcfl = pcfl;
    _cbMax = cbMax;
}

/***************************************************************************
    Destructor for the ChunkyResourceFile.  Decrements the open count on the ChunkyFile and frees
    all the cached data.
***************************************************************************/
ChunkyResourceFile::~ChunkyResourceFile(void)
{
    AssertBaseThis(fobjAllocated);
    CRE cre;

    _cactRef++; // so we don't get "deleted" while detaching the BACOs
    if (pvNil != _pglcre)
    {
        while (_pglcre->IvMac() > 0)
        {
            _pglcre->Get(0, &cre);
            cre.pbaco->AddRef(); // so it doesn't go away when being detached
            cre.pbaco->Detach();
            cre.pbaco->_pcrf = pvNil; // we're going away!
            Debug(_cactRef--;) cre.pbaco->Release();
        }
        ReleasePpo(&_pglcre);
    }
    Assert(_cactRef == 1, "someone still refers to this ChunkyResourceFile");
    ReleasePpo(&_pcfl);
}

/***************************************************************************
    Static method to create a new chunky resource file cache.
***************************************************************************/
PChunkyResourceFile ChunkyResourceFile::PcrfNew(PChunkyFile pcfl, long cbMax)
{
    AssertPo(pcfl, 0);
    AssertIn(cbMax, 0, kcbMax);
    PChunkyResourceFile pcrf;

    if (pvNil != (pcrf = NewObj ChunkyResourceFile(pcfl, cbMax)) && pvNil == (pcrf->_pglcre = DynamicArray::PglNew(size(CRE), 5)))
    {
        ReleasePpo(&pcrf);
    }
    AssertNilOrPo(pcrf, 0);
    return pcrf;
}

/***************************************************************************
    Set the size of the cache. This is most effecient when cbMax is 0
    (all non-required BACOs are flushed) or is bigger than the current
    cbMax.
***************************************************************************/
void ChunkyResourceFile::SetCbMax(long cbMax)
{
    AssertThis(0);
    AssertIn(cbMax, 0, kcbMax);

    if (0 == cbMax)
    {
        CRE cre;
        long icre;

        for (icre = _pglcre->IvMac(); icre-- > 0;)
        {
            _pglcre->Get(icre, &cre);
            AssertPo(cre.pbaco, 0);
            if (cre.pbaco->CactRef() == 0)
            {
                Assert(cre.pbaco->_fAttached, "BaseCacheableObject not attached!");
                cre.pbaco->Detach();

                // have to start over in case other BACOs got deleted or
                // reference counts went to zero
                icre = _pglcre->IvMac();
            }
        }
    }
    else if (_cbCur > cbMax)
        _FPurgeCb(_cbCur - cbMax, klwMax);

    _cbMax = cbMax;
}

/***************************************************************************
    Pre-fetch the object.  Returns tYes if the chunk is successfully cached,
    tNo if the chunk isn't in the ChunkyResourceFile and tMaybe if there wasn't room
    to cache the chunk.
***************************************************************************/
tribool ChunkyResourceFile::TLoad(ChunkTag ctg, ChunkNumber cno, PFNRPO pfnrpo, RSC rsc, long crep)
{
    AssertThis(0);
    Assert(pvNil != pfnrpo, "bad pfnrpo");
    Assert(crep > crepToss, "crep too small");
    CRE cre;
    long icre;
    DataBlock blck;

    // see if this ChunkyResourceFile contains this resource type
    if (rscNil != rsc && !_pcfl->FFind(kctgRsc, rsc))
        return tNo;

    // see if it's in the cache
    if (_FFindCre(ctg, cno, pfnrpo, &icre))
    {
        _pglcre->Get(icre, &cre);
        cre.pbaco->SetCrep(LwMax(cre.pbaco->_crep, crep));
        cre.cactRelease = _cactRelease++;
        _pglcre->Put(icre, &cre);
        return tYes;
    }

    // see if it's in the chunky file
    if (!_pcfl->FFind(ctg, cno, &blck))
        return tNo;

    // get the approximate size of the object
    if (!(*pfnrpo)(this, ctg, cno, &blck, pvNil, &cre.cb))
        return tMaybe;

    if (_cbCur + cre.cb > _cbMax)
    {
        if (!_FPurgeCb(_cbCur + cre.cb - _cbMax, crep - 1))
            return tMaybe;
    }

    if (!(*pfnrpo)(this, ctg, cno, &blck, &cre.pbaco, &cre.cb))
        return tMaybe;

    AssertPo(cre.pbaco, 0);
    AssertIn(cre.cb, 0, kcbMax);

    if (_cbCur + cre.cb > _cbMax && !_FPurgeCb(_cbCur + cre.cb - _cbMax, crep - 1))
    {
        ReleasePpo(&cre.pbaco);
        return tMaybe;
    }

    cre.pbaco->_pcrf = this;
    cre.pbaco->_ctg = ctg;
    cre.pbaco->_cno = cno;
    cre.pbaco->_crep = crep;

    AddRef(); // until the baco is attached it needs a reference count
    cre.pbaco->_fAttached = fFalse;
    cre.pfnrpo = pfnrpo;
    cre.cactRelease = _cactRelease++;

    // indexes may have changed, get the location to insert again
    AssertDo(!_FFindCre(ctg, cno, pfnrpo, &icre), "how did this happen?");

    if (!_pglcre->FInsert(icre, &cre))
    {
        // can't keep it loaded
        ReleasePpo(&cre.pbaco);
        return tMaybe;
    }

    _cbCur += cre.cb;
    cre.pbaco->_fAttached = fTrue;
    cre.pbaco->Release();
    Release(); // baco successfully attached, so release its reference count

    return tYes;
}

/***************************************************************************
    Make sure the object is loaded and increment its reference count.  If
    successful, must be balanced with a call to ReleasePpo.
***************************************************************************/
PBaseCacheableObject ChunkyResourceFile::PbacoFetch(ChunkTag ctg, ChunkNumber cno, PFNRPO pfnrpo, bool *pfError, RSC rsc)
{
    AssertThis(0);
    Assert(pvNil != pfnrpo, "bad pfnrpo");
    AssertNilOrVarMem(pfError);
    CRE cre;
    long icre;
    DataBlock blck;

    if (pvNil != pfError)
        *pfError = fFalse;

    // see if this ChunkyResourceFile contains this resource type
    if (rscNil != rsc && !_pcfl->FFind(kctgRsc, rsc))
        return pvNil;

    // see if it's in the cache
    if (_FFindCre(ctg, cno, pfnrpo, &icre))
    {
        _pglcre->Get(icre, &cre);
        AssertPo(cre.pbaco, 0);
        cre.pbaco->AddRef();
        return cre.pbaco;
    }

    // see if it's in the chunky file
    if (!_pcfl->FFind(ctg, cno, &blck))
        return pvNil;

    // get the object and its size
    if (!(*pfnrpo)(this, ctg, cno, &blck, &cre.pbaco, &cre.cb))
    {
        if (pvNil != pfError)
            *pfError = fTrue;
        PushErc(ercCrfCantLoad);
        return pvNil;
    }

    AssertPo(cre.pbaco, 0);
    AssertIn(cre.cb, 0, kcbMax);

    cre.pbaco->_pcrf = this;
    cre.pbaco->_ctg = ctg;
    cre.pbaco->_cno = cno;
    cre.pbaco->_crep = crepNormal;

    AddRef();
    cre.pbaco->_fAttached = fFalse;
    cre.pfnrpo = pfnrpo;

    // indexes may have changed, get the location to insert again
    AssertDo(!_FFindCre(ctg, cno, pfnrpo, &icre), "how did this happen?");

    if (!_pglcre->FInsert(icre, &cre))
    {
        // return the pbaco anyway.  when it's released it will go away
        if (pvNil != pfError)
            *pfError = fTrue;
        return cre.pbaco;
    }

    _cbCur += cre.cb;
    cre.pbaco->_fAttached = fTrue;
    Release();

    if (_cbCur > _cbMax)
    {
        // purge some stuff
        _FPurgeCb(_cbCur - _cbMax, klwMax);
    }

    return cre.pbaco;
}

/***************************************************************************
    If the object is loaded, increment its reference count and return it.
    If it's not already loaded, just return nil.
***************************************************************************/
PBaseCacheableObject ChunkyResourceFile::PbacoFind(ChunkTag ctg, ChunkNumber cno, PFNRPO pfnrpo, RSC rsc)
{
    AssertThis(0);
    Assert(pvNil != pfnrpo, "bad pfnrpo");

    CRE cre;
    long icre;

    // see if it's in the cache
    if (!_FFindCre(ctg, cno, pfnrpo, &icre) || rscNil != rsc && !_pcfl->FFind(kctgRsc, rsc))
    {
        return pvNil;
    }

    _pglcre->Get(icre, &cre);
    AssertPo(cre.pbaco, 0);
    cre.pbaco->AddRef();
    return cre.pbaco;
}

/***************************************************************************
    If the baco indicated chunk is cached, set its crep.  Returns true
    iff the baco was cached.
***************************************************************************/
bool ChunkyResourceFile::FSetCrep(long crep, ChunkTag ctg, ChunkNumber cno, PFNRPO pfnrpo, RSC rsc)
{
    AssertThis(0);
    Assert(pvNil != pfnrpo, "bad pfnrpo");

    CRE cre;
    long icre;

    // see if it's in the cache
    if (!_FFindCre(ctg, cno, pfnrpo, &icre) || rscNil != rsc && !_pcfl->FFind(kctgRsc, rsc))
    {
        return fFalse;
    }

    _pglcre->Get(icre, &cre);
    AssertPo(cre.pbaco, 0);
    cre.pbaco->SetCrep(crep);
    return fTrue;
}

/***************************************************************************
    Return this if the chunk is in this crf, otherwise return nil. The
    caller is not given a reference count.
***************************************************************************/
PChunkyResourceFile ChunkyResourceFile::PcrfFindChunk(ChunkTag ctg, ChunkNumber cno, RSC rsc)
{
    AssertThis(0);

    if (!_pcfl->FFind(ctg, cno) || rscNil != rsc && !_pcfl->FFind(kctgRsc, rsc))
    {
        return pvNil;
    }

    return this;
}

/***************************************************************************
    Check the _fAttached flag.  If it's false, make sure the BaseCacheableObject is not
    in the ChunkyResourceFile.
***************************************************************************/
void ChunkyResourceFile::BacoDetached(PBaseCacheableObject pbaco)
{
    AssertThis(0);
    AssertPo(pbaco, 0);
    Assert(pbaco->_pcrf == this, "BaseCacheableObject doesn't have right ChunkyResourceFile");
    long icre;
    CRE cre;

    if (pbaco->_fAttached)
    {
        Bug("who's calling BacoDetached?");
        return;
    }
    if (!_FFindBaco(pbaco, &icre))
    {
        Bug("why isn't the BaseCacheableObject in the ChunkyResourceFile?");
        return;
    }
    _pglcre->Get(icre, &cre);
    _cbCur -= cre.cb;
    AssertIn(_cbCur, 0, kcbMax);
    _pglcre->Delete(icre);
}

/***************************************************************************
    The BaseCacheableObject was released.  See if it should be flushed.
***************************************************************************/
void ChunkyResourceFile::BacoReleased(PBaseCacheableObject pbaco)
{
    AssertThis(0);
    AssertPo(pbaco, 0);
    Assert(pbaco->_pcrf == this, "BaseCacheableObject doesn't have right ChunkyResourceFile");
    long icre;
    CRE cre;

    if (!pbaco->_fAttached || pbaco->CactRef() != 0)
    {
        Bug("who's calling BacoReleased?");
        return;
    }

    if (!_FFindBaco(pbaco, &icre))
    {
        Bug("why isn't the BaseCacheableObject in the ChunkyResourceFile?");
        return;
    }
    _pglcre->Get(icre, &cre);
    cre.cactRelease = _cactRelease++;
    _pglcre->Put(icre, &cre);

    if (pbaco->_crep <= crepToss || _cbCur > _cbMax)
    {
        // toss it
        pbaco->Detach();
    }
}

/***************************************************************************
    Find the cre corresponding to the (ctg, cno, pfnrpo).  Set *picre to
    its location (or where it would be if it were in the list).
***************************************************************************/
bool ChunkyResourceFile::_FFindCre(ChunkTag ctg, ChunkNumber cno, PFNRPO pfnrpo, long *picre)
{
    AssertThis(0);
    AssertVarMem(picre);
    CRE *qrgcre, *qcre;
    long icreMin, icreLim, icre;

    // Do a binary search.  The CREs are sorted by (ctg, cno, pfnrpo).
    qrgcre = (CRE *)_pglcre->QvGet(0);
    for (icreMin = 0, icreLim = _pglcre->IvMac(); icreMin < icreLim;)
    {
        icre = (icreMin + icreLim) / 2;
        qcre = qrgcre + icre;
        AssertPo(qcre->pbaco, 0);
        if (ctg < qcre->pbaco->_ctg)
            icreLim = icre;
        else if (ctg > qcre->pbaco->_ctg)
            icreMin = icre + 1;
        else if (cno < qcre->pbaco->_cno)
            icreLim = icre;
        else if (cno > qcre->pbaco->_cno)
            icreMin = icre + 1;
        else if (pfnrpo == qcre->pfnrpo)
        {
            *picre = icre;
            return fTrue;
        }
        else if (pfnrpo < qcre->pfnrpo)
            icreLim = icre;
        else
            icreMin = icre + 1;
    }

    *picre = icreMin;
    return fFalse;
}

/***************************************************************************
    Find the cre corresponding to the BaseCacheableObject.  Set *picre to its location.
***************************************************************************/
bool ChunkyResourceFile::_FFindBaco(PBaseCacheableObject pbaco, long *picre)
{
    AssertThis(0);
    AssertPo(pbaco, 0);
    Assert(pbaco->_pcrf == this, "BaseCacheableObject doesn't have right ChunkyResourceFile");
    AssertVarMem(picre);
    ChunkTag ctg;
    ChunkNumber cno;
    CRE *qrgcre, *qcre;
    long icreMin, icreLim, icre;

    ctg = pbaco->_ctg;
    cno = pbaco->_cno;

    // Do a binary search.  The CREs are sorted by (ctg, cno, pfnrpo).
    qrgcre = (CRE *)_pglcre->QvGet(0);
    for (icreMin = 0, icreLim = _pglcre->IvMac(); icreMin < icreLim;)
    {
        icre = (icreMin + icreLim) / 2;
        qcre = qrgcre + icre;
        AssertPo(qcre->pbaco, 0);
        if (ctg < qcre->pbaco->_ctg)
            icreLim = icre;
        else if (ctg > qcre->pbaco->_ctg)
            icreMin = icre + 1;
        else if (cno < qcre->pbaco->_cno)
            icreLim = icre;
        else if (cno > qcre->pbaco->_cno)
            icreMin = icre + 1;
        else if (pbaco == qcre->pbaco)
        {
            *picre = icre;
            return fTrue;
        }
        else
        {
            // we've found the (ctg, cno), now look for the BaseCacheableObject
            for (icreMin = icre; icreMin-- > 0;)
            {
                qcre = qrgcre + icreMin;
                if (qcre->pbaco->_ctg != ctg || qcre->pbaco->_cno != cno)
                    break;
                if (qcre->pbaco == pbaco)
                {
                    *picre = icreMin;
                    return fTrue;
                }
            }
            for (icreLim = icre; ++icreLim < _pglcre->IvMac();)
            {
                qcre = qrgcre + icreLim;
                if (qcre->pbaco->_ctg != ctg || qcre->pbaco->_cno != cno)
                    break;
                if (qcre->pbaco == pbaco)
                {
                    *picre = icreLim;
                    return fTrue;
                }
            }
            TrashVar(picre);
            return fFalse;
        }
    }

    TrashVar(picre);
    return fFalse;
}

/***************************************************************************
    Try to purge at least cbPurge bytes of space.  Doesn't free anything
    with a crep > crepLast or that is locked.
***************************************************************************/
bool ChunkyResourceFile::_FPurgeCb(long cbPurge, long crepLast)
{
    AssertThis(0);
    AssertIn(cbPurge, 1, kcbMax);
    if (crepLast <= crepToss)
        return fFalse;

    CRE cre;
    long icreMac;

    while (0 < (icreMac = _pglcre->IvMac()))
    {
        // We want to find the "best" element to free.  This is determined by
        // keeping a "best so far" element, which we compare each element to.
        // If the cre has a larger crep, it is worse, so just continue.
        // If the cre has a smaller crep, it is better.  When the crep values
        // are the same, we score it based on when the cre was last released
        // (how many releases have happened since the cre was last used) and
        // how different the cb is from cbPurge.  Each release is worth
        // kcbRelease bytes.  Bytes short of cbPurge are considered worse
        // (by a factor of 3) than bytes beyond cbPurge, so we favor elements
        // that are larger than cbPurge.
        // REVIEW shonk: tune kcbRelease and the weighting factor...
        const long kcbRelease = 256;
        long icre, crep;
        long lw, dcb;
        long lwBest = klwMax;
        long icreBest = ivNil;
        long crepBest = crepLast;

        for (icre = 0; icre < icreMac; icre++)
        {
            _pglcre->Get(icre, &cre);
            AssertPo(cre.pbaco, 0);
            if (cre.pbaco->CactRef() > 0 || (crep = cre.pbaco->_crep) > crepBest)
            {
                continue;
            }
            Assert(crep <= crepBest, 0);
            AssertIn(cre.cactRelease, 0, _cactRelease);

            dcb = cre.cb - cbPurge;
            lw = -LwMul(kcbRelease, LwMin(kcbMax / kcbRelease, _cactRelease - cre.cactRelease)) + LwMul(2, LwAbs(dcb)) -
                 dcb;
            if (crep < crepBest || lw < lwBest)
            {
                icreBest = icre;
                crepBest = crep;
                lwBest = lw;
            }
        }

        if (ivNil == icreBest)
            return fFalse;

        _pglcre->Get(icreBest, &cre);
        Assert(cre.pbaco->_fAttached, "BaseCacheableObject not attached!");
        cre.pbaco->Detach();

        if (0 >= (cbPurge -= cre.cb))
            return fTrue;
    }

    return fFalse;
}

#ifdef DEBUG
/***************************************************************************
    Assert the validity of a ChunkyResourceFile (chunky resource file).
***************************************************************************/
void ChunkyResourceFile::AssertValid(ulong grf)
{
    ChunkyResourceFile_PAR::AssertValid(fobjAllocated);
    AssertPo(_pglcre, 0);
    AssertPo(_pcfl, 0);
    AssertIn(_cbMax, 0, kcbMax);
    AssertIn(_cbCur, 0, kcbMax);
    AssertIn(_cactRelease, 0, kcbMax);
}

/***************************************************************************
    Mark memory used by a ChunkyResourceFile.
***************************************************************************/
void ChunkyResourceFile::MarkMem(void)
{
    AssertThis(0);
    long icre;
    CRE cre;

    ChunkyResourceFile_PAR::MarkMem();
    MarkMemObj(_pglcre);
    MarkMemObj(_pcfl);

    for (icre = _pglcre->IvMac(); icre-- > 0;)
    {
        _pglcre->Get(icre, &cre);
        AssertPo(cre.pbaco, 0);
        Assert(cre.pbaco->_fAttached, "baco claims to not be attached!");
        cre.pbaco->_fAttached = fTrue; // safety to avoid infinite recursion
        MarkMemObj(cre.pbaco);
    }
}
#endif // DEBUG

/***************************************************************************
    Destructor for Chunky resource manager.
***************************************************************************/
ChunkyResourceManager::~ChunkyResourceManager(void)
{
    AssertBaseThis(fobjAllocated);
    long ipcrf;
    PChunkyResourceFile pcrf;

    if (pvNil != _pglpcrf)
    {
        for (ipcrf = _pglpcrf->IvMac(); ipcrf-- > 0;)
        {
            _pglpcrf->Get(ipcrf, &pcrf);
            AssertPo(pcrf, 0);
            ReleasePpo(&pcrf);
        }
        ReleasePpo(&_pglpcrf);
    }
}

/***************************************************************************
    Static method to create a new ChunkyResourceManager.
***************************************************************************/
PChunkyResourceManager ChunkyResourceManager::PcrmNew(long ccrfInit)
{
    AssertIn(ccrfInit, 0, kcbMax);
    PChunkyResourceManager pcrm;

    if (pvNil == (pcrm = NewObj ChunkyResourceManager()))
        return pvNil;
    if (pvNil == (pcrm->_pglpcrf = DynamicArray::PglNew(size(PChunkyResourceFile), ccrfInit)))
    {
        ReleasePpo(&pcrm);
        return pvNil;
    }
    AssertPo(pcrm, 0);
    return pcrm;
}

/***************************************************************************
    Prefetch the object if there is room in the cache.  Assigns the fetched
    object the given priority (crep).
***************************************************************************/
tribool ChunkyResourceManager::TLoad(ChunkTag ctg, ChunkNumber cno, PFNRPO pfnrpo, RSC rsc, long crep)
{
    AssertThis(0);
    Assert(pvNil != pfnrpo, "nil object reader");
    PChunkyResourceFile pcrf;
    tribool t;
    long ipcrf;
    long cpcrf = _pglpcrf->IvMac();

    for (ipcrf = 0; ipcrf < cpcrf; ipcrf++)
    {
        _pglpcrf->Get(ipcrf, &pcrf);
        AssertPo(pcrf, 0);
        t = pcrf->TLoad(ctg, cno, pfnrpo, rsc, crep);
        if (t != tNo)
            return t;
    }
    return tNo;
}

/***************************************************************************
    Make sure the object is loaded and increment its reference count.  If
    successful, must be balanced with a call to ReleasePpo.  If this fails,
    and pfError is not nil, *pfError is set iff the chunk exists but
    couldn't be loaded.
***************************************************************************/
PBaseCacheableObject ChunkyResourceManager::PbacoFetch(ChunkTag ctg, ChunkNumber cno, PFNRPO pfnrpo, bool *pfError, RSC rsc)
{
    AssertThis(0);
    Assert(pvNil != pfnrpo, "nil object reader");
    AssertNilOrVarMem(pfError);
    PChunkyResourceFile pcrf;
    long ipcrf;
    bool fError = fFalse;
    PBaseCacheableObject pbaco = pvNil;
    long cpcrf = _pglpcrf->IvMac();

    for (ipcrf = 0; ipcrf < cpcrf; ipcrf++)
    {
        _pglpcrf->Get(ipcrf, &pcrf);
        AssertPo(pcrf, 0);
        pbaco = pcrf->PbacoFetch(ctg, cno, pfnrpo, &fError, rsc);
        if (pvNil != pbaco || fError)
            break;
    }
    if (pvNil != pfError)
        *pfError = fError;
    AssertNilOrPo(pbaco, 0);
    return pbaco;
}

/***************************************************************************
    If the object is loaded, increment its reference count and return it.
    If it's not already loaded, just return nil.
***************************************************************************/
PBaseCacheableObject ChunkyResourceManager::PbacoFind(ChunkTag ctg, ChunkNumber cno, PFNRPO pfnrpo, RSC rsc)
{
    AssertThis(0);
    Assert(pvNil != pfnrpo, "nil object reader");

    PChunkyResourceFile pcrf;

    if (pvNil == (pcrf = PcrfFindChunk(ctg, cno, rsc)))
        return pvNil;

    return pcrf->PbacoFind(ctg, cno, pfnrpo);
}

/***************************************************************************
    If the chunk is cached, set its crep.  Returns true iff the chunk
    was cached.
***************************************************************************/
bool ChunkyResourceManager::FSetCrep(long crep, ChunkTag ctg, ChunkNumber cno, PFNRPO pfnrpo, RSC rsc)
{
    AssertThis(0);
    Assert(pvNil != pfnrpo, "nil object reader");
    PChunkyResourceFile pcrf;
    long ipcrf;
    long cpcrf = _pglpcrf->IvMac();

    for (ipcrf = 0; ipcrf < cpcrf; ipcrf++)
    {
        _pglpcrf->Get(ipcrf, &pcrf);
        AssertPo(pcrf, 0);
        if (pcrf->FSetCrep(crep, ctg, cno, pfnrpo, rsc))
            return fTrue;
    }
    return fFalse;
}

/***************************************************************************
    Return which ChunkyResourceFile the given chunk is in. The caller is not given a
    reference count.
***************************************************************************/
PChunkyResourceFile ChunkyResourceManager::PcrfFindChunk(ChunkTag ctg, ChunkNumber cno, RSC rsc)
{
    AssertThis(0);
    PChunkyResourceFile pcrf;
    long ipcrf;
    long cpcrf = _pglpcrf->IvMac();

    for (ipcrf = 0; ipcrf < cpcrf; ipcrf++)
    {
        _pglpcrf->Get(ipcrf, &pcrf);
        AssertPo(pcrf, 0);

        if (pcrf->Pcfl()->FFind(ctg, cno) && (rscNil == rsc || pcrf->Pcfl()->FFind(kctgRsc, rsc)))
        {
            return pcrf;
        }
    }

    return pvNil;
}

/***************************************************************************
    Add a chunky file to the list of chunky resource files, by
    creating the chunky resource file object and adding it to the DynamicArray
***************************************************************************/
bool ChunkyResourceManager::FAddCfl(PChunkyFile pcfl, long cbMax, long *piv)
{
    AssertThis(0);
    AssertPo(pcfl, 0);
    AssertIn(cbMax, 0, kcbMax);
    AssertNilOrVarMem(piv);

    PChunkyResourceFile pcrf;

    if (pvNil == (pcrf = ChunkyResourceFile::PcrfNew(pcfl, cbMax)))
    {
        TrashVar(piv);
        return fFalse;
    }
    if (!_pglpcrf->FAdd(&pcrf, piv))
    {
        ReleasePpo(&pcrf);
        return fFalse;
    }
    return fTrue;
}

/***************************************************************************
    Get the icrf'th ChunkyResourceFile.
***************************************************************************/
PChunkyResourceFile ChunkyResourceManager::PcrfGet(long icrf)
{
    AssertThis(0);
    AssertIn(icrf, 0, kcbMax);
    PChunkyResourceFile pcrf;

    if (!FIn(icrf, 0, _pglpcrf->IvMac()))
        return pvNil;

    _pglpcrf->Get(icrf, &pcrf);
    AssertPo(pcrf, 0);
    return pcrf;
}

#ifdef DEBUG
/***************************************************************************
    Check the sanity of the ChunkyResourceManager
***************************************************************************/
void ChunkyResourceManager::AssertValid(ulong grfobj)
{
    ChunkyResourceManager_PAR::AssertValid(grfobj | fobjAllocated);
    AssertPo(_pglpcrf, 0);
}

/***************************************************************************
    mark the memory associated with the ChunkyResourceManager
***************************************************************************/
void ChunkyResourceManager::MarkMem(void)
{
    AssertThis(0);
    long ipcrf;
    long cpcrf;
    PChunkyResourceFile pcrf;

    ChunkyResourceManager_PAR::MarkMem();
    MarkMemObj(_pglpcrf);

    for (ipcrf = 0, cpcrf = _pglpcrf->IvMac(); ipcrf < cpcrf; ipcrf++)
    {
        _pglpcrf->Get(ipcrf, &pcrf);
        AssertPo(pcrf, 0);
        MarkMemObj(pcrf);
    }
}
#endif // DEBUG

/***************************************************************************
    A PFNRPO to read GHQ objects.
***************************************************************************/
bool GHQ::FReadGhq(PChunkyResourceFile pcrf, ChunkTag ctg, ChunkNumber cno, PDataBlock pblck, PBaseCacheableObject *ppbaco, long *pcb)
{
    AssertPo(pcrf, 0);
    AssertPo(pblck, 0);
    AssertNilOrVarMem(ppbaco);
    AssertVarMem(pcb);
    GHQ *pghq;
    HQ hq;

    *pcb = pblck->Cb(fTrue);
    if (pvNil == ppbaco)
        return fTrue;

    if (!pblck->FUnpackData() || hqNil == (hq = pblck->HqFree()))
    {
        TrashVar(pcb);
        TrashVar(ppbaco);
        return fFalse;
    }
    *pcb = CbOfHq(hq);

    if (pvNil == (pghq = NewObj GHQ(hq)))
    {
        FreePhq(&hq);
        TrashVar(pcb);
        TrashVar(ppbaco);
        return fFalse;
    }
    *ppbaco = pghq;
    return fTrue;
}

#ifdef DEBUG
/***************************************************************************
    Assert the validity of a GHQ.
***************************************************************************/
void GHQ::AssertValid(ulong grf)
{
    GHQ_PAR::AssertValid(grf);
    if (hqNil != hq)
        AssertHq(hq);
}

/***************************************************************************
    Mark memory used by the GHQ.
***************************************************************************/
void GHQ::MarkMem(void)
{
    GHQ_PAR::MarkMem();
    MarkHq(hq);
}
#endif // DEBUG

#ifdef DEBUG
/***************************************************************************
    Assert the validity of a GenericCacheableObject.
***************************************************************************/
void GenericCacheableObject::AssertValid(ulong grf)
{
    GenericCacheableObject_PAR::AssertValid(grf);
    AssertNilOrPo(po, 0);
}

/***************************************************************************
    Mark memory used by the GenericCacheableObject.
***************************************************************************/
void GenericCacheableObject::MarkMem(void)
{
    GenericCacheableObject_PAR::MarkMem();
    MarkMemObj(po);
}
#endif // DEBUG

} // end of namespace Chunky
