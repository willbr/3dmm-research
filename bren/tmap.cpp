/***************************************************************************

    Texture map (br_pixmap wrapper) class

***************************************************************************/
#include "bren.h"

ASSERTNAME

namespace BRender {

RTCLASS(TextureMap)

/***************************************************************************
    A PFNRPO to read TextureMap objects.
***************************************************************************/
bool TextureMap::FReadTmap(PChunkyResourceFile pcrf, ChunkTag ctg, ChunkNumber cno, PDataBlock pblck, PBaseCacheableObject *ppbaco, long *pcb)
{
    AssertPo(pcrf, 0);
    AssertPo(pblck, 0);
    AssertNilOrVarMem(ppbaco);
    AssertVarMem(pcb);

    PTextureMap ptmap;

    *pcb = pblck->Cb(fTrue);
    if (pvNil == ppbaco)
        return fTrue;
    ptmap = PtmapRead(pcrf->Pcfl(), ctg, cno);
    if (pvNil == ptmap)
    {
        TrashVar(ppbaco);
        TrashVar(pcb);
        return fFalse;
    }
    AssertPo(ptmap, 0);
    *ppbaco = ptmap;
    return fTrue;
}

/***************************************************************************
    Read a TextureMap from a chunk
***************************************************************************/
PTextureMap TextureMap::PtmapRead(PChunkyFile pcfl, ChunkTag ctg, ChunkNumber cno)
{
    TextureMapFile tmapf;
    DataBlock blck;
    TextureMap *ptmap;

    ptmap = NewObj TextureMap;
    if (pvNil == ptmap)
        goto LFail;

    if (!pcfl->FFind(ctg, cno, &blck) || !blck.FUnpackData())
        goto LFail;
    if (!blck.FReadRgb(&tmapf, size(TextureMapFile), 0))
        goto LFail;

    if (kboCur != tmapf.bo)
        SwapBytesBom(&tmapf, kbomTmapf);
    Assert(kboCur == tmapf.bo, "bad TextureMapFile");

    ptmap->_bpmp.identifier = (char *)ptmap; // to get TextureMap from a (BPMP *)
    if (!FAllocPv((void **)&ptmap->_bpmp.pixels, LwMul(tmapf.cbRow, tmapf.dyp), fmemClear, mprNormal))
    {
        goto LFail;
    }
    ptmap->_bpmp.map = pvNil;
    ptmap->_bpmp.row_bytes = tmapf.cbRow;
    ptmap->_bpmp.type = tmapf.type;
    ptmap->_bpmp.flags = tmapf.grftmap;
    ptmap->_bpmp.base_x = tmapf.xpLeft;
    ptmap->_bpmp.base_y = tmapf.ypTop;
    ptmap->_bpmp.width = tmapf.dxp;
    ptmap->_bpmp.height = tmapf.dyp;
    ptmap->_bpmp.origin_x = tmapf.xpOrigin;
    ptmap->_bpmp.origin_y = tmapf.ypOrigin;

    if (!blck.FReadRgb(ptmap->_bpmp.pixels, LwMul(tmapf.cbRow, tmapf.dyp), size(TextureMapFile)))
    {
        goto LFail;
    }
    return ptmap;
LFail:
    ReleasePpo(&ptmap);
    return pvNil;
}

/***************************************************************************
    Create a TextureMap from a BRender BPMP...used only for importing PIX's
***************************************************************************/
PTextureMap TextureMap::PtmapNewFromBpmp(BPMP *pbpmp)
{
    PTextureMap ptmap;

    ptmap = NewObj TextureMap;
    if (pvNil == ptmap)
        return pvNil;
    ptmap->_bpmp = *pbpmp;
    ptmap->_bpmp.identifier = (char *)ptmap;
    pbpmp->identifier = (char *)ptmap;
    ptmap->_fImported = fTrue;
    return ptmap;
}

/***************************************************************************
    destructor
***************************************************************************/
TextureMap::~TextureMap(void)
{
    if (_fImported)
    {
        // REVIEW *****: this crashes BRender...why?
        //		BrMemFree(_bpmp.pixels);
    }
    else
        FreePpv((void **)&_bpmp.pixels);
}

/***************************************************************************
    Write a TextureMap to a chunk
***************************************************************************/
bool TextureMap::FWrite(PChunkyFile pcfl, ChunkTag ctg, ChunkNumber *pcno)
{
    AssertThis(0);
    DataBlock blck;

    if (!pcfl->FAdd(size(TextureMapFile) + LwMul(_bpmp.row_bytes, _bpmp.height), ctg, pcno, &blck))
    {
        return fFalse;
    }

    return FWrite(&blck);
}

/***************************************************************************
    Write a TextureMap to the given FLO
***************************************************************************/
bool TextureMap::FWrite(PDataBlock pblck)
{
    TextureMapFile tmapf;

    tmapf.bo = kboCur;
    tmapf.osk = koskCur;
    tmapf.cbRow = _bpmp.row_bytes;
    tmapf.type = _bpmp.type;
    tmapf.grftmap = _bpmp.flags;
    tmapf.xpLeft = _bpmp.base_x;
    tmapf.ypTop = _bpmp.base_y;
    tmapf.dxp = _bpmp.width;
    tmapf.dyp = _bpmp.height;
    tmapf.xpOrigin = _bpmp.origin_x;
    tmapf.ypOrigin = _bpmp.origin_y;
    if (!pblck->FWriteRgb(&tmapf, size(TextureMapFile), 0))
        return fFalse;
    if (!pblck->FWriteRgb(_bpmp.pixels, LwMul(tmapf.cbRow, tmapf.dyp), size(TextureMapFile)))
    {
        return fFalse;
    }
    return fTrue;
}

#ifdef WIN

#define CALCDIST(bRed1, bGreen1, bBlue1, bRed2, bGreen2, bBlue2)                                                       \
    (((bRed1) - (bRed2)) * ((bRed1) - (bRed2)) + ((bGreen1) - (bGreen2)) * ((bGreen1) - (bGreen2)) +                   \
     ((bBlue1) - (bBlue2)) * ((bBlue1) - (bBlue2)))

/*
 *	PtmapReadNative	--	Creates a TextureMap object, reading the data from a .BMP file
 *
 *	input:
 *			pfni	--	the Filename to read the data from
 *			pglclr	--	the colors to map to.
 *
 *	output:
 *			returns the pointer to the new TextureMap
 */
PTextureMap TextureMap::PtmapReadNative(Filename *pfni, PDynamicArray pglclr)
{
    byte *prgb = pvNil;
    PTextureMap ptmap = pvNil;
    long dxp, dyp;
    bool fUpsideDown;
    long iclrBest, igl;
    long iprgb;
    long dist, min;
    Color clr, clrSrc;
    PDynamicArray pglclrSrc;
    PDynamicArray pglCache;

    AssertPo(pfni, 0);

    if (FReadBitmap(pfni, &prgb, &pglclrSrc, &dxp, &dyp, &fUpsideDown))
    {
        Assert(!fUpsideDown, 0);
        AssertPo(pglclrSrc, 0);

        if (pglclr != pvNil)
        {
            AssertIn(pglclr->IvMac(), 0, 257);

            //
            // Do a closest color match
            //

            pglCache = DynamicArray::PglNew(size(long), pglclrSrc->IvMac());

            if (pglCache != pvNil)
            {

                iclrBest = ivNil;
                for (igl = 0; igl < pglclrSrc->IvMac(); igl++)
                {
                    AssertDo(pglCache->FAdd(&iclrBest), "Ensured by creation");
                }
            }

            for (iprgb = 0; iprgb < (dxp * dyp); iprgb++)
            {

                if (pglCache != pvNil)
                {
                    pglCache->Get(prgb[iprgb], &iclrBest);

                    if (iclrBest != ivNil)
                    {
                        prgb[iprgb] = (BYTE)iclrBest;
                        continue;
                    }
                }

                pglclrSrc->Get(prgb[iprgb], &clrSrc);

                iclrBest = ivNil;
                min = klwMax;

                for (igl = 0; igl < pglclr->IvMac(); igl++)
                {

                    pglclr->Get(igl, &clr);
                    dist = CALCDIST(clrSrc.bRed, clrSrc.bGreen, clrSrc.bBlue, clr.bRed, clr.bGreen, clr.bBlue);

                    if (dist <= min)
                    {
                        min = dist;
                        iclrBest = igl;
                    }
                }

                if (iclrBest != ivNil)
                {
                    AssertIn(iclrBest, 0, pglclr->IvMac());

                    if (pglCache != pvNil)
                    {
                        pglCache->Put(prgb[iprgb], &iclrBest);
                    }

                    prgb[iprgb] = (BYTE)iclrBest;
                }
            }

            ReleasePpo(&pglCache);
        }

        ReleasePpo(&pglclrSrc);

        ptmap = TextureMap::PtmapNew(prgb, dxp, dyp);
    }

    return ptmap;
}
#endif // WIN

#ifdef MAC
PTextureMap TextureMap::PtmapReadNative(Filename *pfni)
{
    RawRtn(); // REVIEW peted: NYI
    return pvNil;
}
#endif // MAC

/*
 *	PtmapNew	--	Given pixel data and attributes, creates a new TextureMap with
 *		the given information.
 *
 *	input:
 *			prgbPixels	--	the actual pixels for the TextureMap
 *			pbmh		--	The bitmap header from the .BMP file
 *
 *	output:
 *			returns the pointer to the new TextureMap
 */
PTextureMap TextureMap::PtmapNew(byte *prgbPixels, long dxp, long dyp)
{
    PTextureMap ptmap;

    Assert(dxp <= ksuMax, "bitmap too wide");
    Assert(dyp <= ksuMax, "bitmap too high");

    if ((ptmap = NewObj TextureMap) != pvNil)
    {
        ptmap->_fImported = fFalse;
        ptmap->_bpmp.identifier = (char *)ptmap;
        ptmap->_bpmp.pixels = prgbPixels;
        ptmap->_bpmp.map = pvNil;
        ptmap->_bpmp.row_bytes = (br_int_16)dxp;
        ptmap->_bpmp.type = BR_PMT_INDEX_8;
        ptmap->_bpmp.flags = BR_PMF_LINEAR;
        ptmap->_bpmp.base_x = ptmap->_bpmp.base_y = 0;
        ptmap->_bpmp.width = (br_uint_16)dxp;
        ptmap->_bpmp.height = (br_uint_16)dyp;
        ptmap->_bpmp.origin_x = ptmap->_bpmp.origin_y = 0;
    }
    AssertPo(ptmap, 0);
    return ptmap;
}

/******************************************************************************
    FWriteTmapChkFile
        Writes a stand-alone file with a TextureMap chunk in it.  The file can
        be later read in by the Compiler class with the FILE command.

    Arguments:
        PFilename pfniDst   -- Filename indicating the name of the output file
        bool fCompress -- fTrue if the chunk date is to be compressed
        PMSNK pmsnkErr -- optional message sink to direct errors to

    Returns: fTrue if the file was written successfully

************************************************************ PETED ***********/
bool TextureMap::FWriteTmapChkFile(PFilename pfniDst, bool fCompress, PMSNK pmsnkErr)
{
    AssertThis(0);
    AssertPo(pfniDst, ffniFile);
    AssertNilOrPo(pmsnkErr, 0);

    bool fRet = fFalse;
    long lwSig;
    PSZ pszErr = pvNil;
    FLO flo;

    if (pvNil == (flo.pfil = FIL::PfilCreate(pfniDst)))
    {
        pszErr = PszLit("Couldn't create destination file\n");
        goto LFail;
    }
    flo.fp = size(long);
    flo.cb = CbOnFile();

    if (fCompress)
    {
        DataBlock blck;

        if (!blck.FSetTemp(flo.cb) || !FWrite(&blck))
        {
            pszErr = PszLit("allocation failure\n");
            goto LFail;
        }
        if (!blck.FPackData())
            lwSig = klwSigUnpackedFile;
        else
        {
            lwSig = klwSigPackedFile;
            flo.cb = blck.Cb(fTrue);
        }
        if (!flo.pfil->FWriteRgb(&lwSig, size(long), 0) || !blck.FWriteToFlo(&flo, fTrue))
        {
            pszErr = PszLit("writing to destination file failed\n");
            goto LFail;
        }
    }
    else
    {
        lwSig = klwSigUnpackedFile;
        if (!flo.pfil->FWriteRgb(&lwSig, size(long), 0) || !FWriteFlo(&flo))
        {
            pszErr = PszLit("writing to destination file failed\n");
            goto LFail;
        }
    }

    fRet = fTrue;
LFail:
    if (pszErr != pvNil && pmsnkErr != pvNil)
        pmsnkErr->ReportLine(pszErr);
    if (!fRet && pvNil != flo.pfil)
        flo.pfil->SetTemp();
    ReleasePpo(&flo.pfil);
    return fRet;
}

#ifdef NOT_YET_REVIEWED
byte *TextureMap::PrgbBuildInverseTable(void)
{
    byte *prgb, *prgbT, iclr;
    long cbRgb;

    if (_pbpmp->type != BR_PMT_RGB_888)
        return pvNil;

    if (!FAllocPv((void **)&prgb, cbRgb = _pbpmp->height, fmemNil, mprNormal))
        return pvNil;

    for (prgbT = prgb, iclr = 0; iclr < cbRgb; prgbT++, iclr++)
        *prgbT = iclr;

    _SortInverseTable(prgb, cbRgb, BR_COLOUR_RGB(0, 0, 0), BR_COLOUR_RGB(0xFF, 0xFF, 0xFF));
    return prgb;
}

void TextureMap::_SortInverseTable(byte *prgb, long cbRgb, BRCLR brclrLo, BRCLR brclrHi)
{
    long cbRgb1 = 0, cbRgb2 = 0;
    byte *prgb2, *prgbRead, bT;
    BRCLR brclrPivot = brclrLo + (brclrHi - brclrLo) / 2;
    BRCLR *pbrclr;

    prgb2 = prgb + cbRgb;
    prgbRead = prgb;
    while (cbRgb--)
    {
        pbrclr = 0; // pbrclr from index *prgb;
        if (*pbrclr <= brclrPivot)
        {
            prgbRead++;
            cbRgb1++;
        }
        else
        {
            bT = *prgb2;
            *--prgb2 = *prgbRead;
            *prgbRead = bT;
            cbRgb2++;
        }
    }
    if (cbRgb1 > 1)
        _SortInverseTable(prgb, cbRgb1, brclrLo, brclrPivot);
    if (cbRgb2 > 1)
        _SortInverseTable(prgb2, cbRgb2, brclrPivot + 1, brclrHi);
}
#endif // NOT_YET_REVIEWED

#ifdef DEBUG
/***************************************************************************
    Assert the validity of the TextureMap.
***************************************************************************/
void TextureMap::AssertValid(ulong grf)
{
    TextureMap_PAR::AssertValid(fobjAllocated);
    if (!_fImported)
        AssertPvCb(_bpmp.pixels, LwMul(_bpmp.row_bytes, _bpmp.height));
}

/***************************************************************************
    Mark memory used by the TextureMap.
***************************************************************************/
void TextureMap::MarkMem(void)
{
    AssertThis(0);
    TextureMap_PAR::MarkMem();
    if (!_fImported)
        MarkPv(_bpmp.pixels);
}
#endif // DEBUG

} // end of namespace BRender
