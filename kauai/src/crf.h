/* Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */

/* Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */

/***************************************************************************
    Author: ShonK
    Project: Kauai
    Reviewed:
    Copyright (c) Microsoft Corporation

    Chunky resource file management. A ChunkyResourceFile is a cache wrapped around a
    chunky file. A ChunkyResourceManager is a list of CRFs. A BaseCacheableObject is an object that
    can be cached in a ChunkyResourceFile. ResourceCache is the interface implemented
    by both ChunkyResourceFile and ChunkyResourceManager.

***************************************************************************/
#ifndef CHRES_H
#define CHRES_H

#include "three_mm/resource_cache.h"

namespace Chunky
{

typedef class ChunkyResourceFile *PChunkyResourceFile;
typedef ChunkNumber RSC;
const RSC rscNil = 0L;

// chunky resource entry priority
enum
{
    crepToss,
    crepTossFirst,
    crepNormal,
};

/***************************************************************************
    Base cacheable object.  All cacheable objects must be based on BaseCacheableObject.
***************************************************************************/
typedef class BaseCacheableObject *PBaseCacheableObject;
#define BaseCacheableObject_PAR BASE
#define kclsBaseCacheableObject 'BACO'
class BaseCacheableObject : public BaseCacheableObject_PAR
{
    RTCLASS_DEC
    ASSERT
    MARKMEM

  private:
    ThreeMMResourceObject _resource;
    static const ThreeMMResourceObjectServices *_ObjectServices();

    friend class ChunkyResourceFile;

  protected:
    BaseCacheableObject(void);
    ~BaseCacheableObject(void);

  public:
    virtual void Release(void);

    virtual void SetCrep(LegacyLong crep);
    virtual void Detach(void);

    ChunkTagOrType Ctg(void)
    {
        return _resource.type;
    }
    ChunkNumber Cno(void)
    {
        return _resource.number;
    }
    PChunkyResourceFile Pcrf(void)
    {
        return static_cast<PChunkyResourceFile>(_resource.owner);
    }
    LegacyLong Crep(void)
    {
        return _resource.priority;
    }

    // Many objects know how big they are, and how to write themselves to a
    // chunky file.  Here are some useful prototypes so that the users of those
    // objects don't need to know what the actual class is.
    virtual bool FWrite(PDataBlock pblck);
    virtual bool FWriteFlo(PFileLocation pflo);
    virtual LegacyLong CbOnFile(void);
};

/***************************************************************************
    Chunky resource cache - this is a pure virtual class that supports
    the crf and crm classes.
***************************************************************************/
// Object reader function - must handle ppo == pvNil, in which case, the
// *pcb should be set to an estimate of the size when read.
typedef bool FNRPO(PChunkyResourceFile pcrf, ChunkTagOrType ctg, ChunkNumber cno, PDataBlock pblck,
                   PBaseCacheableObject *ppbaco, LegacyLong *pcb);
typedef FNRPO *PFNRPO;

typedef class ResourceCache *PResourceCache;
#define ResourceCache_PAR BASE
#define kclsResourceCache 'RCA'
class ResourceCache : public ResourceCache_PAR
{
    RTCLASS_DEC

  public:
    virtual tribool TLoad(ChunkTagOrType ctg, ChunkNumber cno, PFNRPO pfnrpo, RSC rsc = rscNil,
                          LegacyLong crep = crepNormal) = 0;
    virtual PBaseCacheableObject PbacoFetch(ChunkTagOrType ctg, ChunkNumber cno, PFNRPO pfnrpo, bool *pfError = pvNil,
                                            RSC rsc = rscNil) = 0;
    virtual PBaseCacheableObject PbacoFind(ChunkTagOrType ctg, ChunkNumber cno, PFNRPO pfnrpo, RSC rsc = rscNil) = 0;
    virtual bool FSetCrep(LegacyLong crep, ChunkTagOrType ctg, ChunkNumber cno, PFNRPO pfnrpo, RSC rsc = rscNil) = 0;
    virtual PChunkyResourceFile PcrfFindChunk(ChunkTagOrType ctg, ChunkNumber cno, RSC rsc = rscNil) = 0;
};

/***************************************************************************
    Chunky resource file.
***************************************************************************/
#define ChunkyResourceFile_PAR ResourceCache
#define kclsChunkyResourceFile 'CRF'
class ChunkyResourceFile : public ChunkyResourceFile_PAR
{
    RTCLASS_DEC
    ASSERT
    MARKMEM

  protected:
    friend class ChunkyResourceManager;
    ThreeMMResourceCache _cache;
    ThreeMMResourceServices _Services();
    ChunkyResourceFile(PChunkyFile pcfl, LegacyLong cbMax);

  public:
    ~ChunkyResourceFile(void);
    static PChunkyResourceFile PcrfNew(PChunkyFile pcfl, LegacyLong cbMax);

    virtual tribool TLoad(ChunkTagOrType ctg, ChunkNumber cno, PFNRPO pfnrpo, RSC rsc = rscNil,
                          LegacyLong crep = crepNormal);
    virtual PBaseCacheableObject PbacoFetch(ChunkTagOrType ctg, ChunkNumber cno, PFNRPO pfnrpo, bool *pfError = pvNil,
                                            RSC rsc = rscNil);
    virtual PBaseCacheableObject PbacoFind(ChunkTagOrType ctg, ChunkNumber cno, PFNRPO pfnrpo, RSC rsc = rscNil);
    virtual bool FSetCrep(LegacyLong crep, ChunkTagOrType ctg, ChunkNumber cno, PFNRPO pfnrpo, RSC rsc = rscNil);
    virtual PChunkyResourceFile PcrfFindChunk(ChunkTagOrType ctg, ChunkNumber cno, RSC rsc = rscNil);

    LegacyLong CbMax(void)
    {
        return _cache.budget;
    }
    void SetCbMax(LegacyLong cbMax);

    PChunkyFile Pcfl(void)
    {
        return static_cast<PChunkyFile>(_cache.file);
    }

    // These APIs are intended for BaseCacheableObject use only
    void BacoDetached(PBaseCacheableObject pbaco);
    void BacoReleased(PBaseCacheableObject pbaco);
};

/***************************************************************************
    Chunky resource manager - a list of CRFs.
***************************************************************************/
typedef class ChunkyResourceManager *PChunkyResourceManager;
#define ChunkyResourceManager_PAR ResourceCache
#define kclsChunkyResourceManager 'CRM'
class ChunkyResourceManager : public ChunkyResourceManager_PAR
{
    RTCLASS_DEC
    ASSERT
    MARKMEM

  protected:
    ThreeMMResourceManager _manager;
    static ThreeMMResourceManagerServices _Services();
    ChunkyResourceManager(void);

  public:
    ~ChunkyResourceManager(void);
    static PChunkyResourceManager PcrmNew(LegacyLong ccrfInit);

    virtual tribool TLoad(ChunkTagOrType ctg, ChunkNumber cno, PFNRPO pfnrpo, RSC rsc = rscNil,
                          LegacyLong crep = crepNormal);
    virtual PBaseCacheableObject PbacoFetch(ChunkTagOrType ctg, ChunkNumber cno, PFNRPO pfnrpo, bool *pfError = pvNil,
                                            RSC rsc = rscNil);
    virtual PBaseCacheableObject PbacoFind(ChunkTagOrType ctg, ChunkNumber cno, PFNRPO pfnrpo, RSC rsc = rscNil);
    virtual bool FSetCrep(LegacyLong crep, ChunkTagOrType ctg, ChunkNumber cno, PFNRPO pfnrpo, RSC rsc = rscNil);
    virtual PChunkyResourceFile PcrfFindChunk(ChunkTagOrType ctg, ChunkNumber cno, RSC rsc = rscNil);

    bool FAddCfl(PChunkyFile pcfl, LegacyLong cbMax, LegacyLong *piv = pvNil);
    LegacyLong Ccrf(void)
    {
        AssertThis(0);
        return _manager.count;
    }
    PChunkyResourceFile PcrfGet(LegacyLong icrf);
};

/***************************************************************************
    An object (BaseCacheableObject) wrapper around a generic HQ.
***************************************************************************/
#define GenericHQ_PAR BaseCacheableObject
typedef class GenericHQ *PGenericHQ;
#define kclsGenericHQ 'GHQ'
class GenericHQ : public GenericHQ_PAR
{
    RTCLASS_DEC
    ASSERT
    MARKMEM

  public:
    HQ hq;

    GenericHQ(HQ hqT)
    {
        hq = hqT;
    }
    ~GenericHQ(void)
    {
        FreePhq(&hq);
    }

    // An object reader for a GenericHQ.
    static bool FReadGhq(PChunkyResourceFile pcrf, ChunkTagOrType ctg, ChunkNumber cno, PDataBlock pblck,
                         PBaseCacheableObject *ppbaco, LegacyLong *pcb);
};

/***************************************************************************
    A BaseCacheableObject wrapper around a generic object.
***************************************************************************/
#define GenericCacheableObject_PAR BaseCacheableObject
typedef class GenericCacheableObject *PGenericCacheableObject;
#define kclsGenericCacheableObject 'CABO'
class GenericCacheableObject : public GenericCacheableObject_PAR
{
    RTCLASS_DEC
    ASSERT
    MARKMEM

  public:
    BASE *po;

    GenericCacheableObject(BASE *poT)
    {
        po = poT;
    }
    ~GenericCacheableObject(void)
    {
        ReleasePpo(&po);
    }
};

} // end of namespace Chunky

#endif //! CHRES_H
