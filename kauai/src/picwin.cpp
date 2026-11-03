/* Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */

/***************************************************************************
    Author: ShonK
    Project: Kauai
    Reviewed:
    Copyright (c) Microsoft Corporation

    Windows specific picture (metafile) routines.

***************************************************************************/
#include "frame.h"
ASSERTNAME

/***************************************************************************
    Constructor for a picture.
***************************************************************************/
Picture::Picture(void)
{
    _hpic = hNil;
    _rc.Zero();
}

/***************************************************************************
    Destructor for a picture.
***************************************************************************/
Picture::~Picture(void)
{
    AssertBaseThis(0);
    if (hNil != _hpic)
        DeleteEnhMetaFile(_hpic);
}

/***************************************************************************
    Read a picture from a chunky file.  This routine only reads or converts
    OS specific representations with the given chid value.
***************************************************************************/
PPicture Picture::PpicFetch(PChunkyFile pcfl, ChunkTagOrType ctg, ChunkNumber cno, ChildChunkID chid)
{
    AssertPo(pcfl, 0);
    DataBlock blck;
    ChildChunkIdentification kid;

    if (!pcfl->FFind(ctg, cno))
        return pvNil;
    if (pcfl->FGetKidChidCtg(ctg, cno, chid, kctgMeta, &kid) && pcfl->FFind(kid.cki.ctg, kid.cki.cno, &blck))
    {
        return PpicRead(&blck);
    }

    // REVIEW shonk: convert another type to a MetaFile...
    return pvNil;
}

/***************************************************************************
    Read a picture from a chunky file.  This routine only reads a system
    specific pict (Mac PICT or Windows MetaFile) and its header.
***************************************************************************/
PPicture Picture::PpicRead(PDataBlock pblck)
{
    AssertPo(pblck, fblckReadable);
    PictureData data;
    if (!data.FRead(pblck)) return pvNil;
    HPIC handle = SetEnhMetaFileBits(data.PayloadBytes(), const_cast<byte *>(data.Payload()));
    if (!handle) return pvNil;
    PPicture picture = NewObj Picture;
    if (!picture) { DeleteEnhMetaFile(handle); return pvNil; }
    picture->_hpic = handle;
    picture->_rc = data.Bounds();
    picture->_stored.Swap(data);
    return picture;
}

/***************************************************************************
    Return the total size on file.
***************************************************************************/
LegacyLong Picture::CbOnFile(void)
{
    AssertThis(0);
    return _stored.CbOnFile() ? _stored.CbOnFile() : GetEnhMetaFileBits(_hpic, 0, pvNil) + PictureData::HeaderBytes;
}

/***************************************************************************
    Write the meta file (and its header) to the given DataBlock.
***************************************************************************/
bool Picture::FWrite(PDataBlock pblck)
{
    AssertThis(0);
    if (_stored.CbOnFile()) return _stored.FWrite(pblck);
    LegacyLong count = GetEnhMetaFileBits(_hpic, 0, pvNil);
    if (count <= 0) return fFalse;
    HQ bytes;
    if (!FAllocHq(&bytes, count, fmemNil, mprNormal)) return fFalse;
    auto *payload = static_cast<byte *>(QvFromHq(bytes));
    bool success = GetEnhMetaFileBits(_hpic, count, payload) == static_cast<ulong>(count) &&
                   _stored.FInit(_rc, payload, count);
    FreePhq(&bytes);
    return success && _stored.FWrite(pblck);
}

/***************************************************************************
    Static method to read the file as a native picture (EMF or WMF file).
***************************************************************************/
PPicture Picture::PpicReadNative(Filename *pfni)
{
    AssertPo(pfni, ffniFile);
    HPIC hpic;
    PPicture ppic;
    RC rc;
    ENHMETAHEADER emh;
    String stn;

    switch (pfni->Ftg())
    {
    default:
        return pvNil;

    case kftgMeta:
        hpic = _HpicReadWmf(pfni);
        break;

    case kftgEnhMeta:
        pfni->GetStnPath(&stn);
        hpic = GetEnhMetaFile(stn.Psz());
        break;
    }

    if (hNil == hpic)
        return pvNil;

    if (pvNil == (ppic = NewObj Picture))
    {
        DeleteEnhMetaFile(hpic);
        return pvNil;
    }

    GetEnhMetaFileHeader(hpic, size(emh), &emh);
    rc.Set(LwMulDiv(emh.rclFrame.left, 72, 2540), LwMulDiv(emh.rclFrame.top, 72, 2540),
           LwMulDiv(emh.rclFrame.right, 72, 2540), LwMulDiv(emh.rclFrame.bottom, 72, 2540));

    ppic->_hpic = hpic;
    ppic->_rc = rc;
    AssertPo(ppic, 0);
    return ppic;
}

/* placeable metafile data definitions */
#define lwMEFH 0x9AC6CDD7

/* placeable metafile header */
typedef struct _MEFH
{
    DWORD lwKey;
    short w1;
    short xpLeft;
    short ypTop;
    short xpRight;
    short ypBottom;
    WORD w2;
    DWORD dw1;
    WORD w3;
} MEFH;

/***************************************************************************
    Static method to read an old style WMF file.
***************************************************************************/
HPIC Picture::_HpicReadWmf(Filename *pfni)
{
    MEFH mefh;
    METAHEADER mh;
    HPIC hpic;
    LegacyLong lw;
    LegacyLong cb;
    PFileObject pfil;
    void *pv;
    bool fT;
    FilePosition fp;

    const LegacyLong kcbMefh = 22;
    const LegacyLong kcbMetaHeader = 18;

    if (pvNil == (pfil = FileObject::PfilOpen(pfni)))
        return hNil;

    // check for type of meta file
    if (!pfil->FReadRgb(&lw, size(LegacyLong), 0))
        goto LFail;

    // read placeable meta file header - NOTE: we can't just use size(MEFH) for
    // the cb because the MEFH is padded to a LegacyLong boundary by the compiler
    fp = 0;
    if (lw == lwMEFH && !pfil->FReadRgbSeq(&mefh, kcbMefh, &fp))
        goto LFail;

    // read METAHEADER structure - NOTE: we can't just use size(METAHEADER) for
    // the cb because the METAHEADER is padded to a LegacyLong boundary by the
    // compiler
    if (!pfil->FReadRgbSeq(&mh, kcbMetaHeader, &fp) || mh.mtVersion < 0x0300 || 2 * mh.mtHeaderSize != kcbMetaHeader ||
        !FIn(cb = 2 * mh.mtSize, kcbMetaHeader + 1, kcbMax))
    {
        goto LFail;
    }

    if (!FAllocPv(&pv, cb, fmemNil, mprNormal))
    {
    LFail:
        ReleasePpo(&pfil);
        return hNil;
    }

    *(METAHEADER *)pv = mh;
    fT = pfil->FReadRgbSeq(PvAddBv(pv, kcbMetaHeader), cb - kcbMetaHeader, &fp);
    ReleasePpo(&pfil);

    if (!fT)
    {
        FreePpv(&pv);
        return hNil;
    }

    // convert the old style metafile to an enhanced metafile
    hpic = SetWinMetaFileBits(cb, (byte *)pv, hNil, pvNil);
    FreePpv(&pv);

    return hpic;
}
