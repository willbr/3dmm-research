/* Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */

/***************************************************************************

    tdf.h: Three-D Font class

    Primary Author: ******
    Review Status: REVIEWED - any changes to this file must be reviewed!

    BASE ---> BaseCacheableObject ---> TDF  (Three-D Font)

***************************************************************************/
#ifndef TDF_H
#define TDF_H

using namespace BRender;

/****************************************
    3-D Font class
****************************************/
typedef class TDF *PTDF;
#define TDF_PAR BaseCacheableObject
#define kclsTDF 'TDF'
class TDF : public TDF_PAR
{
    RTCLASS_DEC
    ASSERT
    MARKMEM

  protected:
    long _cch;    // count of chars
    BRS _dyrMax;  // max character height
    BRS *_prgdxr; // character widths
    BRS *_prgdyr; // character heights

  protected:
    TDF(void)
    {
    }
    bool _FInit(PDataBlock pblck);

  public:
    static bool FReadTdf(PChunkyResourceFile pcrf, ChunkTag ctg, ChunkNumber cno, PDataBlock pblck, PBaseCacheableObject *ppbaco, long *pcb);
    ~TDF(void);

    // This authoring-only API creates a new TDF based on a set of models
    static bool FCreate(PChunkyResourceFile pcrf, PDynamicArray pglkid, STN *pstn, ChunkIdentification *pckiTdf = pvNil);

    PMODL PmodlFetch(ChildChunkID chid);
    BRS DxrChar(long ich);
    BRS DyrChar(long ich);
    BRS DyrMax(void)
    {
        return _dyrMax;
    }
};

#endif // TDF_H
