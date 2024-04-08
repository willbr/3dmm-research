/* Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */

/***************************************************************************

    mtrl.cpp: Material (Material_MTRL) and custom material (CustomMaterial_CMTL) classes

    Primary Author: ******
    Review Status: REVIEWED - any changes to this file must be reviewed!

***************************************************************************/
#include "soc.h"
ASSERTNAME

RTCLASS(Material_MTRL)
RTCLASS(CustomMaterial_CMTL)

// REVIEW *****: kiclrBaseDefault and kcclrDefault are palette-specific
const byte kiclrBaseDefault = 15; // base index of default color
const byte kcclrDefault = 15;     // count of shades in default color

const br_ufraction kbrufKaDefault = BR_UFRACTION(0.10);
const br_ufraction kbrufKdDefault = BR_UFRACTION(0.60);
const br_ufraction kbrufKsDefault = BR_UFRACTION(0.60);
const BRS krPowerDefault = BR_SCALAR(50);
const byte kbOpaque = 0xff;

PTextureMap Material_MTRL::_ptmapShadeTable = pvNil; // shade table for all MTRLs

/***************************************************************************
    Call this function to assign the global shade table.  It is read from
    the given chunk.
***************************************************************************/
bool Material_MTRL::FSetShadeTable(PChunkyFile pcfl, ChunkTag ctg, ChunkNumber cno)
{
    AssertPo(pcfl, 0);

    ReleasePpo(&_ptmapShadeTable);
    _ptmapShadeTable = TextureMap::PtmapRead(pcfl, ctg, cno);
    return (pvNil != _ptmapShadeTable);
}

/***************************************************************************
    Create a new solid-color material
***************************************************************************/
PMaterial_MTRL Material_MTRL::PmtrlNew(long iclrBase, long cclr)
{
    if (ivNil != iclrBase)
        AssertIn(iclrBase, 0, kbMax);
    if (ivNil != cclr)
        AssertIn(cclr, 0, kbMax - iclrBase);

    PMaterial_MTRL pmtrl;

    pmtrl = NewObj Material_MTRL;
    if (pvNil == pmtrl)
        return pvNil;

    // An arbitrary 4-character string is passed to BrMaterialAllocate (to
    // be stored in a string pointed to by _pbmtl->identifier).  The
    // contents of the string are then replaced by the "this" pointer.
    pmtrl->_pbmtl = BrMaterialAllocate("1234");
    if (pvNil == pmtrl->_pbmtl)
    {
        ReleasePpo(&pmtrl);
        return pvNil;
    }
    CopyPb(&pmtrl, pmtrl->_pbmtl->identifier, size(long));

    pmtrl->_pbmtl->ka = kbrufKaDefault;
    pmtrl->_pbmtl->kd = kbrufKdDefault;
    pmtrl->_pbmtl->ks = kbrufKsDefault;
    pmtrl->_pbmtl->power = krPowerDefault;
    if (ivNil == iclrBase)
        pmtrl->_pbmtl->index_base = kiclrBaseDefault;
    else
        pmtrl->_pbmtl->index_base = (byte)iclrBase;
    if (ivNil == cclr)
        pmtrl->_pbmtl->index_range = kcclrDefault;
    else
        pmtrl->_pbmtl->index_range = (byte)cclr;
    pmtrl->_pbmtl->opacity = kbOpaque; // all socrates objects are opaque
    pmtrl->_pbmtl->flags = BR_MATF_LIGHT | BR_MATF_GOURAUD;
    BrMaterialAdd(pmtrl->_pbmtl);
    AssertPo(pmtrl, 0);
    return pmtrl;
}

/***************************************************************************
    A PFNRPO to read Material_MTRL objects.
***************************************************************************/
bool Material_MTRL::FReadMtrl(PChunkyResourceFile pcrf, ChunkTag ctg, ChunkNumber cno, PDataBlock pblck, PBaseCacheableObject *ppbaco, long *pcb)
{
    AssertPo(pcrf, 0);
    AssertPo(pblck, 0);
    AssertNilOrVarMem(ppbaco);
    AssertVarMem(pcb);

    PMaterial_MTRL pmtrl;

    *pcb = size(Material_MTRL);
    if (pvNil == ppbaco)
        return fTrue;
    pmtrl = NewObj Material_MTRL;
    if (pvNil == pmtrl || !pmtrl->_FInit(pcrf, ctg, cno))
    {
        TrashVar(ppbaco);
        TrashVar(pcb);
        ReleasePpo(&pmtrl);
        return fFalse;
    }
    AssertPo(pmtrl, 0);
    *ppbaco = pmtrl;
    return fTrue;
}

/***************************************************************************
    Read the given Material_MTRL chunk from file
***************************************************************************/
bool Material_MTRL::_FInit(PChunkyResourceFile pcrf, ChunkTag ctg, ChunkNumber cno)
{
    AssertBaseThis(0);
    AssertPo(pcrf, 0);

    PChunkyFile pcfl = pcrf->Pcfl();
    DataBlock blck;
    MTRLF mtrlf;
    ChildChunkIdentification kid;
    Material_MTRL *pmtrlThis = this; // to get Material_MTRL from BMTL
    PTextureMap ptmap = pvNil;

    if (!pcfl->FFind(ctg, cno, &blck) || !blck.FUnpackData())
        return fFalse;

    if (blck.Cb() < size(MTRLF))
        return fFalse;
    if (!blck.FReadRgb(&mtrlf, size(MTRLF), 0))
        return fFalse;
    if (kboOther == mtrlf.bo)
        SwapBytesBom(&mtrlf, kbomMtrlf);
    Assert(kboCur == mtrlf.bo, "bad MTRLF");

    // An arbitrary 4-character string is passed to BrMaterialAllocate (to
    // be stored in a string pointed to by _pbmtl->identifier).  The
    // contents of the string are then replaced by the "this" pointer.
    _pbmtl = BrMaterialAllocate("1234");
    if (pvNil == _pbmtl)
        return fFalse;
    CopyPb(&pmtrlThis, _pbmtl->identifier, size(long));
    _pbmtl->colour = mtrlf.brc;
    _pbmtl->ka = mtrlf.brufKa;
    _pbmtl->kd = mtrlf.brufKd;
    // Note: for socrates, mtrlf.brufKs should be zero
    _pbmtl->ks = mtrlf.brufKs;

    _pbmtl->power = mtrlf.rPower;
    _pbmtl->index_base = mtrlf.bIndexBase;
    _pbmtl->index_range = mtrlf.cIndexRange;
    _pbmtl->opacity = kbOpaque; // all socrates objects are opaque

    // REVIEW *****: also set the BR_MATF_PRELIT flag to use prelit models
    _pbmtl->flags = BR_MATF_LIGHT | BR_MATF_SMOOTH;

    // now read texture map, if any
    if (pcfl->FGetKidChidCtg(ctg, cno, 0, kctgTmap, &kid))
    {
        ptmap = (PTextureMap)pcrf->PbacoFetch(kid.cki.ctg, kid.cki.cno, TextureMap::FReadTmap);
        if (pvNil == ptmap)
            return fFalse;
        _pbmtl->colour_map = ptmap->Pbpmp();
        Assert((PTextureMap)_pbmtl->colour_map->identifier == ptmap, "lost tmap!");
        AssertPo(_ptmapShadeTable, 0);
        _pbmtl->index_shade = _ptmapShadeTable->Pbpmp();
        _pbmtl->flags |= BR_MATF_MAP_COLOUR;
        _pbmtl->index_base = 0;
        _pbmtl->index_range = _ptmapShadeTable->Pbpmp()->height - 1;

        /* Look for a texture transform for the Material_MTRL */
        if (pcfl->FGetKidChidCtg(ctg, cno, 0, kctgTxxf, &kid))
        {
            TXXFF txxff;

            if (!pcfl->FFind(kid.cki.ctg, kid.cki.cno, &blck) || !blck.FUnpackData())
                goto LFail;
            if (blck.Cb() < size(TXXFF))
                goto LFail;
            if (!blck.FReadRgb(&txxff, size(TXXFF), 0))
                goto LFail;
            if (kboCur != txxff.bo)
                SwapBytesBom(&txxff, kbomTxxff);
            Assert(kboCur == txxff.bo, "bad TXXFF");
            _pbmtl->map_transform = txxff.bmat23;
        }
    }
    BrMaterialAdd(_pbmtl);
    AssertThis(0);
    return fTrue;
LFail:
    /* REVIEW ***** (peted): Only the code that I added uses this LFail
        case.  It's my opinion that any API which can fail should clean up
        after itself.  It happens that in the case of this Material_MTRL class, when
        the caller releases this instance, the TextureMap and BMTL are freed anyway,
        but I don't think that it's good to count on that */
    ReleasePpo(&ptmap);
    _pbmtl->colour_map = pvNil;
    BrMaterialFree(_pbmtl);
    _pbmtl = pvNil;
    return fFalse;
}

/***************************************************************************
    Read a PIX and build a PMaterial_MTRL from it
***************************************************************************/
PMaterial_MTRL Material_MTRL::PmtrlNewFromPix(PFilename pfni)
{
    AssertPo(pfni, ffniFile);

    String stn;
    PMaterial_MTRL pmtrl;
    PBMTL pbmtl;
    PTextureMap ptmap;

    pmtrl = NewObj Material_MTRL;
    if (pvNil == pmtrl)
        goto LFail;

    // An arbitrary 4-character string is passed to BrMaterialAllocate (to
    // be stored in a string pointed to by _pbmtl->identifier).  The
    // contents of the string are then replaced by the "this" pointer.
    pmtrl->_pbmtl = BrMaterialAllocate("1234");
    if (pvNil == pmtrl->_pbmtl)
        goto LFail;
    pbmtl = pmtrl->_pbmtl;
    CopyPb(&pmtrl, pbmtl->identifier, size(long));
    pbmtl->colour = 0; // this field is ignored
    pbmtl->ka = kbrufKaDefault;
    pbmtl->kd = kbrufKdDefault;
    pbmtl->ks = kbrufKsDefault;
    pbmtl->power = krPowerDefault;
    pbmtl->opacity = kbOpaque; // all socrates objects are opaque
    pbmtl->flags = BR_MATF_LIGHT | BR_MATF_GOURAUD;
    pfni->GetStnPath(&stn);
    pbmtl->colour_map = BrPixelmapLoad(stn.Psz());
    if (pvNil == pbmtl->colour_map)
        goto LFail;

    // Create a TextureMap for this BPMP.  We don't directly save
    // the ptmap...it's automagically attached to the
    // BPMP's identifier.
    ptmap = TextureMap::PtmapNewFromBpmp(pbmtl->colour_map);
    if (pvNil == ptmap)
    {
        BrPixelmapFree(pbmtl->colour_map);
        goto LFail;
    }
    Assert((PTextureMap)pbmtl->colour_map->identifier == ptmap, "lost our TextureMap!");
    AssertPo(_ptmapShadeTable, 0);
    pbmtl->index_shade = _ptmapShadeTable->Pbpmp();
    pbmtl->flags |= BR_MATF_MAP_COLOUR;
    pbmtl->index_base = 0;
    pbmtl->index_range = _ptmapShadeTable->Pbpmp()->height - 1;
    AssertPo(pmtrl, 0);
    return pmtrl;
LFail:
    ReleasePpo(&pmtrl);
    return pvNil;
}

/***************************************************************************
    Read a BMP and build a PMaterial_MTRL from it
***************************************************************************/
PMaterial_MTRL Material_MTRL::PmtrlNewFromBmp(PFilename pfni, PDynamicArray pglclr)
{
    AssertPo(pfni, ffniFile);
    AssertPo(_ptmapShadeTable, 0);

    PMaterial_MTRL pmtrl;
    PTextureMap ptmap;

    pmtrl = PmtrlNew();
    if (pvNil == pmtrl)
        return pvNil;

    ptmap = TextureMap::PtmapReadNative(pfni, pglclr);
    if (pvNil == ptmap)
    {
        ReleasePpo(&pmtrl);
        return pvNil;
    }
    pmtrl->_pbmtl->index_base = 0;
    pmtrl->_pbmtl->index_range = _ptmapShadeTable->Pbpmp()->height - 1;
    pmtrl->_pbmtl->index_shade = _ptmapShadeTable->Pbpmp();
    pmtrl->_pbmtl->flags |= BR_MATF_MAP_COLOUR;
    pmtrl->_pbmtl->colour_map = ptmap->Pbpmp();
    // The reference for ptmap has been transfered to pmtrl by the previous
    // line, so I don't need to ReleasePpo(&ptmap) in this function.

    return pmtrl;
}

/***************************************************************************
    Return a pointer to the Material_MTRL that owns this BMTL
***************************************************************************/
PMaterial_MTRL Material_MTRL::PmtrlFromBmtl(PBMTL pbmtl)
{
    AssertVarMem(pbmtl);

    PMaterial_MTRL pmtrl = (PMaterial_MTRL) * (long *)pbmtl->identifier;
    AssertPo(pmtrl, 0);
    return pmtrl;
}

/***************************************************************************
    Return this Material_MTRL's TextureMap, or pvNil if it's a solid-color Material_MTRL.
    Note: This function doesn't AssertThis because it gets called on
    objects which are not necessarily valid (e.g., from the destructor and
    from AssertThis())
***************************************************************************/
PTextureMap Material_MTRL::Ptmap(void)
{
    AssertBaseThis(0);

    if (pvNil == _pbmtl)
        return pvNil;
    else if (pvNil == _pbmtl->colour_map)
        return pvNil;
    else
        return (PTextureMap)_pbmtl->colour_map->identifier;
}

/***************************************************************************
    Write a Material_MTRL to a chunky file
***************************************************************************/
bool Material_MTRL::FWrite(PChunkyFile pcfl, ChunkTag ctg, ChunkNumber *pcno)
{
    AssertThis(0);
    AssertPo(pcfl, 0);
    AssertVarMem(pcno);

    MTRLF mtrlf;
    ChunkNumber cnoChild;
    PTextureMap ptmap;

    mtrlf.bo = kboCur;
    mtrlf.osk = koskCur;
    mtrlf.brc = _pbmtl->colour;
    mtrlf.brufKa = _pbmtl->ka;
    mtrlf.brufKd = _pbmtl->kd;
    mtrlf.brufKs = _pbmtl->ks;
    mtrlf.bIndexBase = _pbmtl->index_base;
    mtrlf.cIndexRange = _pbmtl->index_range;
    mtrlf.rPower = _pbmtl->power;

    if (!pcfl->FAddPv(&mtrlf, size(MTRLF), ctg, pcno))
        return fFalse;
    ptmap = Ptmap();
    if (pvNil != ptmap)
    {
        if (!ptmap->FWrite(pcfl, kctgTmap, &cnoChild))
        {
            pcfl->Delete(ctg, *pcno);
            return fFalse;
        }
        if (!pcfl->FAdoptChild(ctg, *pcno, kctgTmap, cnoChild, 0))
        {
            pcfl->Delete(kctgTmap, cnoChild);
            pcfl->Delete(ctg, *pcno);
            return fFalse;
        }
    }
    return fTrue;
}

/***************************************************************************
    Free the Material_MTRL
***************************************************************************/
Material_MTRL::~Material_MTRL(void)
{
    AssertBaseThis(0);

    PTextureMap ptmap;

    ptmap = Ptmap();

    if (pvNil != ptmap)
    {
        ReleasePpo(&ptmap);
        _pbmtl->colour_map = pvNil;
    }
    BrMaterialRemove(_pbmtl);
    BrMaterialFree(_pbmtl);
}

#ifdef DEBUG
/***************************************************************************
    Assert the validity of the Material_MTRL.
***************************************************************************/
void Material_MTRL::AssertValid(ulong grf)
{
    Material_MTRL_PAR::AssertValid(fobjAllocated);

    AssertNilOrPo(Ptmap(), 0);
    Assert(pvNil != _ptmapShadeTable, "Why do we have MTRLs but no shade table?");
}

/***************************************************************************
    Mark memory used by the Material_MTRL
***************************************************************************/
void Material_MTRL::MarkMem(void)
{
    AssertThis(0);

    PTextureMap ptmap;

    Material_MTRL_PAR::MarkMem();
    ptmap = Ptmap();
    if (pvNil != ptmap)
        MarkMemObj(ptmap);
}

/***************************************************************************
    Mark memory used by the shade table
***************************************************************************/
void Material_MTRL::MarkShadeTable(void)
{
    MarkMemObj(_ptmapShadeTable);
}

#endif // DEBUG

//
//
//
//  CustomMaterial_CMTL (custom material) stuff begins here
//
//
//

/***************************************************************************
    Static function to see if the given chunk has MODL children
***************************************************************************/
bool CustomMaterial_CMTL::FHasModels(PChunkyFile pcfl, ChunkTag ctg, ChunkNumber cno)
{
    AssertPo(pcfl, 0);

    ChildChunkIdentification kid;

    return pcfl->FGetKidChidCtg(ctg, cno, 0, kctgBmdl, &kid);
}

/***************************************************************************
    Static function to see if the two given CMTLs have the same child
    MODLs
***************************************************************************/
bool CustomMaterial_CMTL::FEqualModels(PChunkyFile pcfl, ChunkNumber cno1, ChunkNumber cno2)
{
    AssertPo(pcfl, 0);

    ChildChunkID chid = 0;
    ChildChunkIdentification kid1;
    ChildChunkIdentification kid2;

    while (pcfl->FGetKidChidCtg(kctgCmtl, cno1, chid, kctgBmdl, &kid1))
    {
        if (!pcfl->FGetKidChidCtg(kctgCmtl, cno2, chid, kctgBmdl, &kid2))
            return fFalse;
        if (kid1.cki.cno != kid2.cki.cno)
            return fFalse;
        chid++;
    }
    // End of cno1's BMDLs...make sure cno2 doesn't have any more
    if (pcfl->FGetKidChidCtg(kctgCmtl, cno2, chid, kctgBmdl, &kid2))
        return fFalse;
    return fTrue;
}

/***************************************************************************
    Create a new custom material
***************************************************************************/
PCustomMaterial_CMTL CustomMaterial_CMTL::PcmtlNew(long ibset, long cbprt, PMaterial_MTRL *prgpmtrl)
{
    AssertPvCb(prgpmtrl, LwMul(cbprt, size(PMaterial_MTRL)));
    PCustomMaterial_CMTL pcmtl;
    long imtrl;

    pcmtl = NewObj CustomMaterial_CMTL;
    if (pvNil == pcmtl)
        return pvNil;

    pcmtl->_ibset = ibset;
    pcmtl->_cbprt = cbprt;
    if (!FAllocPv((void **)&pcmtl->_prgpmtrl, LwMul(pcmtl->_cbprt, size(PMaterial_MTRL)), fmemClear, mprNormal))
    {
        ReleasePpo(&pcmtl);
        return pvNil;
    }
    if (!FAllocPv((void **)&pcmtl->_prgpmodl, LwMul(pcmtl->_cbprt, size(PMODL)), fmemClear, mprNormal))
    {
        ReleasePpo(&pcmtl);
        return pvNil;
    }
    for (imtrl = 0; imtrl < cbprt; imtrl++)
    {
        AssertPo(prgpmtrl[imtrl], 0);
        pcmtl->_prgpmtrl[imtrl] = prgpmtrl[imtrl];
        pcmtl->_prgpmtrl[imtrl]->AddRef();
    }
    AssertPo(pcmtl, 0);
    return pcmtl;
}

/***************************************************************************
    A PFNRPO to read CustomMaterial_CMTL objects.
***************************************************************************/
bool CustomMaterial_CMTL::FReadCmtl(PChunkyResourceFile pcrf, ChunkTag ctg, ChunkNumber cno, PDataBlock pblck, PBaseCacheableObject *ppbaco, long *pcb)
{
    AssertPo(pcrf, 0);
    AssertPo(pblck, 0);
    AssertNilOrVarMem(ppbaco);
    AssertVarMem(pcb);

    PCustomMaterial_CMTL pcmtl;

    *pcb = size(CustomMaterial_CMTL);
    if (pvNil == ppbaco)
        return fTrue;
    pcmtl = NewObj CustomMaterial_CMTL;
    if (pvNil == pcmtl || !pcmtl->_FInit(pcrf, ctg, cno))
    {
        ReleasePpo(&pcmtl);
        TrashVar(ppbaco);
        TrashVar(pcb);
        return fFalse;
    }
    AssertPo(pcmtl, 0);
    *ppbaco = pcmtl;
    *pcb += LwMul(size(PMaterial_MTRL) + size(PMODL), pcmtl->_cbprt);
    return fTrue;
}

/***************************************************************************
    Read a CustomMaterial_CMTL from file
***************************************************************************/
bool CustomMaterial_CMTL::_FInit(PChunkyResourceFile pcrf, ChunkTag ctg, ChunkNumber cno)
{
    AssertBaseThis(0);
    AssertPo(pcrf, 0);

    long ikid;
    long imtrl;
    ChildChunkIdentification kid;
    DataBlock blck;
    PChunkyFile pcfl = pcrf->Pcfl();
    CMTLF cmtlf;

    if (!pcfl->FFind(ctg, cno, &blck) || !blck.FUnpackData())
        return fFalse;

    if (blck.Cb() != size(CMTLF))
    {
        Bug("bad CMTLF...you may need to update tmpls.chk");
        return fFalse;
    }
    if (!blck.FReadRgb(&cmtlf, size(CMTLF), 0))
        return fFalse;
    if (kboOther == cmtlf.bo)
        SwapBytesBom(&cmtlf, kbomCmtlf);
    Assert(kboCur == cmtlf.bo, "bad CMTLF");
    _ibset = cmtlf.ibset;

    // Highest chid is number of body part sets - 1
    _cbprt = 0;
    // note: there might be a faster way to compute _cbprt
    for (ikid = 0; pcfl->FGetKid(ctg, cno, ikid, &kid); ikid++)
    {
        if ((long)kid.chid > (_cbprt - 1))
            _cbprt = kid.chid + 1;
    }
    if (!FAllocPv((void **)&_prgpmtrl, LwMul(_cbprt, size(PMaterial_MTRL)), fmemClear, mprNormal))
    {
        return fFalse;
    }
    if (!FAllocPv((void **)&_prgpmodl, LwMul(_cbprt, size(PMODL)), fmemClear, mprNormal))
    {
        return fFalse;
    }
    for (imtrl = 0; imtrl < _cbprt; imtrl++)
    {
        if (pcfl->FGetKidChidCtg(ctg, cno, imtrl, kctgMtrl, &kid))
        {
            _prgpmtrl[imtrl] = (Material_MTRL *)pcrf->PbacoFetch(kid.cki.ctg, kid.cki.cno, Material_MTRL::FReadMtrl);
            if (pvNil == _prgpmtrl[imtrl])
                return fFalse;
        }
        if (pcfl->FGetKidChidCtg(ctg, cno, imtrl, kctgBmdl, &kid))
        {
            _prgpmodl[imtrl] = (MODL *)pcrf->PbacoFetch(kid.cki.ctg, kid.cki.cno, MODL::FReadModl);
            if (pvNil == _prgpmodl[imtrl])
                return fFalse;
        }
    }

    return fTrue;
}

/***************************************************************************
    Free the CustomMaterial_CMTL
***************************************************************************/
CustomMaterial_CMTL::~CustomMaterial_CMTL(void)
{
    AssertBaseThis(0);

    long imtrl;

    if (pvNil != _prgpmtrl)
    {
        for (imtrl = 0; imtrl < _cbprt; imtrl++)
            ReleasePpo(&_prgpmtrl[imtrl]);
        FreePpv((void **)&_prgpmtrl);
    }

    if (pvNil != _prgpmodl)
    {
        for (imtrl = 0; imtrl < _cbprt; imtrl++)
            ReleasePpo(&_prgpmodl[imtrl]);
        FreePpv((void **)&_prgpmodl);
    }
}

/***************************************************************************
    Return ibmtl'th BMTL
***************************************************************************/
BMTL *CustomMaterial_CMTL::Pbmtl(long ibmtl)
{
    AssertThis(0);
    AssertIn(ibmtl, 0, _cbprt);

    return _prgpmtrl[ibmtl]->Pbmtl();
}

/***************************************************************************
    Return imodl'th MODL
***************************************************************************/
PMODL CustomMaterial_CMTL::Pmodl(long imodl)
{
    AssertThis(0);
    AssertIn(imodl, 0, _cbprt);

    AssertNilOrPo(_prgpmodl[imodl], 0);
    return _prgpmodl[imodl];
}

/***************************************************************************
    Returns whether this CustomMaterial_CMTL has any models attached
***************************************************************************/
bool CustomMaterial_CMTL::FHasModels(void)
{
    AssertThis(0);

    long imodl;

    for (imodl = 0; imodl < _cbprt; imodl++)
    {
        if (pvNil != _prgpmodl[imodl])
            return fTrue;
    }
    return fFalse;
}

#ifdef DEBUG
/***************************************************************************
    Assert the validity of the CustomMaterial_CMTL
***************************************************************************/
void CustomMaterial_CMTL::AssertValid(ulong grf)
{
    long imtrl;

    Material_MTRL_PAR::AssertValid(fobjAllocated);
    AssertPvCb(_prgpmtrl, LwMul(_cbprt, size(Material_MTRL *)));
    AssertPvCb(_prgpmodl, LwMul(_cbprt, size(MODL *)));

    for (imtrl = 0; imtrl < _cbprt; imtrl++)
    {
        AssertPo(_prgpmtrl[imtrl], 0);
        AssertNilOrPo(_prgpmodl[imtrl], 0);
    }
}

/***************************************************************************
    Mark memory used by the Material_MTRL
***************************************************************************/
void CustomMaterial_CMTL::MarkMem(void)
{
    AssertThis(0);

    long imtrl;

    Material_MTRL_PAR::MarkMem();
    MarkPv(_prgpmtrl);
    MarkPv(_prgpmodl);
    for (imtrl = 0; imtrl < _cbprt; imtrl++)
    {
        MarkMemObj(_prgpmtrl[imtrl]);
        MarkMemObj(_prgpmodl[imtrl]);
    }
}
#endif // DEBUG
