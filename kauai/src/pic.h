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
#include "picture_data.h"

const FileType kftgPict = 'PICT';
const FileType kftgMeta = 'WMF';
const FileType kftgEnhMeta = 'EMF';

/***************************************************************************
    Picture class.  This is a wrapper around a system picture (Mac Pict or
    Win MetaFile).
***************************************************************************/
typedef class Picture *PPicture;
#define Picture_PAR BaseCacheableObject
#define kclsPicture 'PIC'
class Picture : public Picture_PAR
{
    RTCLASS_DEC
    ASSERT

  protected:
    PictureData _stored; // preserves original native bytes independently of GDI

    CoreNative::PictureHandle _hpic;
    RC _rc;

    Picture(void);
#ifdef WIN
    static CoreNative::PictureHandle _HpicReadWmf(Filename *pfni);
#endif // WIN

  public:
    ~Picture(void);

    static PPicture PpicFetch(PChunkyFile pcfl, ChunkTagOrType ctg, ChunkNumber cno, ChildChunkID chid = 0);
    static PPicture PpicRead(PDataBlock pblck);
    static PPicture PpicReadNative(Filename *pfni);
    static PPicture PpicNew(CoreNative::PictureHandle hpic, RC *prc);

    void GetRc(RC *prc);
    CoreNative::PictureHandle Hpic(void)
    {
        return _hpic;
    }
    bool FAddToCfl(PChunkyFile pcfl, ChunkTagOrType ctg, ChunkNumber *pcno, ChildChunkID chid = 0);
    bool FPutInCfl(PChunkyFile pcfl, ChunkTagOrType ctg, ChunkNumber cno, ChildChunkID chid = 0);
    virtual LegacyLong CbOnFile(void);
    virtual bool FWrite(PDataBlock pblck);
};

// a chunky resource reader to read picture 0 from a GRAF chunk
bool FReadMainPic(PChunkyFile pcfl, ChunkTagOrType ctg, ChunkNumber cno, PDataBlock pblck, PBaseCacheableObject *ppbaco, LegacyLong *pcb);

#endif //! PIC_H
