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
    chunky file. A ChunkyResourceManager is a list of caches. A BaseCacheableObject
    can be cached in a ChunkyResourceFile. ResourceCache provides the common
    resource view to legacy callers.

***************************************************************************/
#ifndef CHRES_H
#define CHRES_H

#include "mm/resource_cache.h"

struct MMChunkOwnerRead;
struct MMArchiveWriter;
struct MMPrivateArchive;
struct MMStringValue;

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
    MMResourceObject _resource;
    friend class ChunkyResourceFile;

  protected:
    BaseCacheableObject(void);
    ~BaseCacheableObject(void);
    const MMResourceObject *ResourceBinding() const { return &_resource; }

  public:
    void Release(void) override;

    static const MMResourceObjectServices *ObjectServices();
    MMResourceCache *CoreCache() const { return _resource.cache; }
    void SetCrep(LegacyLong crep);
    void Detach(void);

    ChunkTagOrType Ctg(void)
    {
        return _resource.type;
    }
    ChunkNumber Cno(void)
    {
        return _resource.number;
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
    Legacy owner of a borrowed resource view. The C access functions query
    either the file cache or ordered manager bound during construction.
***************************************************************************/
// Object reader function - must handle ppo == pvNil, in which case, the
// *pcb should be set to an estimate of the size when read.
using ResourceReader = MMResourceBlockReader;

typedef class ResourceCache *PResourceCache;
#define ResourceCache_PAR BASE
#define kclsResourceCache 'RCA'
class ResourceCache : public ResourceCache_PAR
{
    RTCLASS_DEC

  protected:
    MMResourceAccess _access = {};
    ResourceCache() = default;

  public:
    const MMResourceAccess &Resources() const { return _access; }
    tribool TLoad(ChunkTagOrType ctg, ChunkNumber cno, ResourceReader reader, RSC rsc = rscNil,
                  LegacyLong crep = crepNormal);
    PBaseCacheableObject PbacoFetch(ChunkTagOrType ctg, ChunkNumber cno, ResourceReader reader, bool *pfError = pvNil,
                                   RSC rsc = rscNil);
    PBaseCacheableObject PbacoFind(ChunkTagOrType ctg, ChunkNumber cno, ResourceReader reader, RSC rsc = rscNil);
    bool FSetCrep(LegacyLong crep, ChunkTagOrType ctg, ChunkNumber cno, ResourceReader reader, RSC rsc = rscNil);
    MMResourceCache *FindChunk(ChunkTagOrType ctg, ChunkNumber cno, RSC rsc = rscNil);
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
    MMResourceCache _cache = {};
    MMPrivateArchive *_privateArchive = nullptr; // The live cache owns this reference.
    PFileObject _legacyWireFile = nullptr; // Borrowed from _cache when using Kauai file services.
    String _wirePath;
    MMResourceServices _Services();
    static void _Destroy(void *);
    ChunkyResourceFile() { _access.cache = &_cache; }
    static PChunkyResourceFile _PcrfNew(const MMResourceSource &, LegacyLong budget,
                                       const MMBlockFileServices *files = nullptr, String *path = nullptr,
                                       MMPrivateArchive *archive = nullptr);

  public:
    ~ChunkyResourceFile(void);
    static PChunkyResourceFile PcrfNew(PChunkyFile pcfl, LegacyLong cbMax);
    static PChunkyResourceFile PcrfNewPrivate(MMPrivateArchive *, LegacyLong cbMax);
    static PChunkyResourceFile PcrfOpenWorking(PFilename, bool create, LegacyLong budget = 0);
    // Use a live owner when one exists or when the file needs a shared handle.
    // A selected live path can still fail to open its resource cache.
    static PChunkyResourceFile PcrfNewIndex(const MMChunkIndex *, PFileObject, LegacyLong cbMax);
    static PChunkyResourceFile PcrfNewIndex(const MMChunkIndex *, void *backing, const MMBlockFileServices &,
                                           String &path, LegacyLong cbMax);
    void Release(void) override;

    MMResourceCache *CoreCache() { return &_cache; }
    MMChunkAccess ReadChunkAccess();
    MMChunkOwnerRead *ReadOwner();
    MMArchiveWriter ArchiveWriter();
    bool FGetName(ChunkTagOrType, ChunkNumber, PString);
    PChunkyResourceFile PcrfNewSibling(LegacyLong budget);

    LegacyLong CbMax(void)
    {
        return _cache.budget;
    }
    void SetCbMax(LegacyLong cbMax);

    // The mutable file backing a live cache.
    PChunkyFile Pcfl(void)
    {
        return _privateArchive ? nullptr : static_cast<PChunkyFile>(_cache.file);
    }
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
    MMResourceManager _manager;
    static MMResourceManagerServices _Services();
    ChunkyResourceManager(void);

  public:
    ~ChunkyResourceManager(void);
    static PChunkyResourceManager PcrmNew(LegacyLong ccrfInit);

    bool FAddCfl(PChunkyFile pcfl, LegacyLong cbMax, LegacyLong *piv = pvNil);
    LegacyLong Ccrf(void)
    {
        AssertThis(0);
        return _manager.count;
    }
    // The Help editor uses this accessor for files it adds through FAddCfl.
    PChunkyResourceFile PcrfGet(LegacyLong icrf);
    MMResourceManager *CoreManager() { return &_manager; }
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
    static int FReadGhq(MMResourceCache *, MMU32, MMU32, MMOwnedBlock *, void **, MMI32 *);
};

MMResourceCache *NewTypedPrivateCache(MMPrivateArchive *, MMI32 budget);
MMResourceCache *NewTypedSiblingCache(const MMResourceCache *, MMI32 budget);
bool ReadResourceName(const MMResourceCache *, ChunkTagOrType, ChunkNumber, PString);
bool ReadResourceName(const MMResourceCache *, ChunkTagOrType, ChunkNumber, const MMStringValue &);

} // end of namespace Chunky

#endif //! CHRES_H
