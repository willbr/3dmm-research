/* Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */

/* Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */

/***************************************************************************
    Author: ShonK
    Project: Kauai
    Reviewed:
    Copyright (c) Microsoft Corporation

    Picture management code declarations.

***************************************************************************/
#ifndef PIC_H
#define PIC_H

const FileType kftgPict = 'PICT';
const FileType kftgMeta = 'WMF';
const FileType kftgEnhMeta = 'EMF';

/***************************************************************************
    Picture class.  This is a wrapper around a system picture (Mac Pict or
    Win MetaFile).
***************************************************************************/
typedef class PIC *PPIC;
#define PIC_PAR BaseCacheableObject
#define kclsPIC 'PIC'
class PIC : public PIC_PAR
{
    RTCLASS_DEC
    ASSERT

  protected:
    struct PICH
    {
        RC rc;
        long cb;
    };

    HPIC _hpic;
    RC _rc;

    PIC(void);
#ifdef WIN
    static HPIC _HpicReadWmf(Filename *pfni);
#endif // WIN

  public:
    ~PIC(void);

    static PPIC PpicFetch(PChunkyFile pcfl, ChunkTag ctg, ChunkNumber cno, ChildChunkID chid = 0);
    static PPIC PpicRead(PDataBlock pblck);
    static PPIC PpicReadNative(Filename *pfni);
    static PPIC PpicNew(HPIC hpic, RC *prc);

    void GetRc(RC *prc);
    HPIC Hpic(void)
    {
        return _hpic;
    }
    bool FAddToCfl(PChunkyFile pcfl, ChunkTag ctg, ChunkNumber *pcno, ChildChunkID chid = 0);
    bool FPutInCfl(PChunkyFile pcfl, ChunkTag ctg, ChunkNumber cno, ChildChunkID chid = 0);
    virtual long CbOnFile(void);
    virtual bool FWrite(PDataBlock pblck);
};

// a chunky resource reader to read picture 0 from a GRAF chunk
bool FReadMainPic(PChunkyFile pcfl, ChunkTag ctg, ChunkNumber cno, PDataBlock pblck, PBaseCacheableObject *ppbaco, long *pcb);

#endif //! PIC_H
