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
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "handle_services.h"
#include "mm/chunk_ops.h"
#include "util.h"
ASSERTNAME

namespace Chunky
{

RTCLASS(BaseCacheableObject)
RTCLASS(GenericHQ)
RTCLASS(ResourceCache)
RTCLASS(ChunkyResourceFile)
RTCLASS(ChunkyResourceManager)
RTCLASS(GenericCacheableObject)

namespace
{
MMResourceRequest ResourceRequest(ChunkTagOrType type, ChunkNumber number, ResourceReader reader, RSC scope,
                                  LegacyLong priority = crepNormal)
{
    return {type, number, scope, reader.Core(), priority};
}
} // namespace

const MMResourceObjectServices *BaseCacheableObject::_ObjectServices()
{
    static const MMResourceObjectServices services = {
        [](void *o) -> MMResourceObject * { return &static_cast<PBaseCacheableObject>(o)->_resource; },
        [](void *o) -> MMReference * { return &static_cast<PBaseCacheableObject>(o)->_references; },
        [](void *o) { delete static_cast<PBaseCacheableObject>(o); },
        [](void *, int kind) {
            if (kind == MM_RESOURCE_BAD_REFS)
                Bug("calling Release without an AddRef");
            else if (kind == MM_RESOURCE_BAD_RETAIN)
                Bug("reference count not positive");
            else
                Bug("invalid cacheable object ownership");
        }};
    return &services;
}
BaseCacheableObject::BaseCacheableObject(void)
{
    AssertBaseThis(fobjAllocated);
    mm_resource_object_init(&_resource);
}
BaseCacheableObject::~BaseCacheableObject(void)
{
    AssertBaseThis(fobjAllocated);
    Assert(!_resource.attached, "still attached");
    mm_resource_object_dispose(this, _ObjectServices());
}
bool BaseCacheableObject::FWriteFlo(PFileLocation location)
{
    AssertThis(0);
    AssertPo(location, 0);
    DataBlock block(location);
    return FWrite(&block);
}
bool BaseCacheableObject::FWrite(PDataBlock block)
{
    AssertThis(0);
    RawRtn();
    return false;
}
LegacyLong BaseCacheableObject::CbOnFile(void)
{
    AssertThis(0);
    RawRtn();
    return 0;
}
void BaseCacheableObject::Release(void)
{
    AssertThis(0);
    mm_resource_object_release(this, _ObjectServices());
}
void BaseCacheableObject::Detach(void)
{
    AssertThis(0);
    mm_resource_object_detach(this, _ObjectServices());
}
void BaseCacheableObject::SetCrep(LegacyLong priority)
{
    AssertThis(0);
    mm_resource_object_priority(this, priority, _ObjectServices());
}
#ifdef DEBUG
void BaseCacheableObject::AssertValid(LegacyUlong flags)
{
    BaseCacheableObject_PAR::AssertValid(fobjAllocated);
    Assert(!_resource.attached || _resource.cache != nullptr, "attached baco has no crf");
    AssertNilOrVarMem(_resource.cache);
    AssertNilOrVarMem(Pcrf());
}
void BaseCacheableObject::MarkMem(void)
{
    AssertValid(0);
    BaseCacheableObject_PAR::MarkMem();
    if (!_resource.attached)
        MarkMemObj(Pcrf());
}
#endif
MMResourceServices ChunkyResourceFile::_Services()
{
    MMResourceServices s = {};
    s.context = this;
    s.objects = *BaseCacheableObject::_ObjectServices();
    s.compare_reader = [](MMResourceReader a, MMResourceReader b) -> int {
        return a == b ? 0 : a < b ? -1 : 1;
    };
    s.contains = [](void *file, MMU32 type, MMU32 number) -> int {
        return static_cast<PChunkyFile>(file)->FFind(type, number);
    };
    s.live_chunks = [](void *file) { return static_cast<PChunkyFile>(file)->ReadChunkAccess(); };
    const auto blocks = KauaiChunkPayloadServices();
    s.handles = blocks->handles;
    s.block_files = blocks->files;
    s.locate = blocks->locate;
    s.read_legacy = [](void *c, MMResourceReader reader, MMU32 type, MMU32 number, MMOwnedBlock *block, void **object,
                MMI32 *bytes) -> int {
        DataBlock view(*block);
        PBaseCacheableObject value = nullptr;
        LegacyLong count = *bytes;
        const bool ok =
            reinterpret_cast<PFNRPO>(reader)(static_cast<PChunkyResourceFile>(c), type, number,
                                             &view, object ? &value : nullptr, &count);
        if (object)
            *object = value;
        *bytes = count;
        return ok;
    };
    s.retain_file = [](void *file) { static_cast<PChunkyFile>(file)->AddRef(); };
    s.release_file = [](void *file) { static_cast<PChunkyFile>(file)->Release(); };
    s.destroy_owner = [](void *c) {
        auto cache = static_cast<PChunkyResourceFile>(c);
        AssertPo(cache, fobjAllocated);
        delete cache;
    };
    s.issue = [](void *c, int kind, MMU32 type, MMU32 number) {
        if (kind == MM_RESOURCE_LOAD_FAILED)
        {
#if defined(DEBUG) || defined(STUDIO_TEST_TOOLS)
            const auto batch = std::getenv("THREEDMM_BATCH_TEST");
            if (batch && std::strcmp(batch, "1") == 0)
            {
                Filename file;
                String path;
                char name[kcchMaxStz];
                auto cache = static_cast<PChunkyResourceFile>(c);
                if (cache->_cache.wire_file)
                    static_cast<PFileObject>(cache->_cache.wire_file)->GetFni(&file);
                else
                    cache->Pcfl()->GetFni(&file);
                file.GetStnPath(&path);
                path.GetSzs(name);
                std::fprintf(stderr, "BATCH_RESOURCE_LOAD_FAILED ctg=%08lx cno=%08lx file=%s\n",
                             static_cast<unsigned long>(type), static_cast<unsigned long>(number), name);
            }
#endif
            PushErc(ercCrfCantLoad);
        }
        else if (kind == MM_RESOURCE_DUPLICATE)
            Bug("how did this happen?");
        else if (kind == MM_RESOURCE_BAD_CHARGE)
            Bug("negative resource size");
        else if (kind == MM_RESOURCE_BAD_OWNER)
            Bug("someone still refers to this ChunkyResourceFile");
        else if (kind == MM_RESOURCE_BAD_RETAIN)
            Bug("reference count not positive");
        else if (kind == MM_RESOURCE_BAD_BLOCK)
            Bug("invalid resource block");
        else
            Bug("invalid resource cache ownership");
    };
    return s;
}
ChunkyResourceFile::~ChunkyResourceFile(void)
{
    AssertBaseThis(fobjAllocated);
    if (_cache.owner)
        AssertDo(mm_resource_cache_dispose(&_cache) == MM_STORAGE_OK, "invalid resource cache disposal");
}
PChunkyResourceFile ChunkyResourceFile::PcrfNew(PChunkyFile file, LegacyLong budget)
{
    AssertPo(file, 0);
    return _PcrfNew({file, nullptr, nullptr}, budget);
}
PChunkyResourceFile ChunkyResourceFile::PcrfNewIndex(const MMChunkIndex *index, PFileObject backing, LegacyLong budget)
{
    AssertPo(backing, 0);
    return _PcrfNew({nullptr, index, backing}, budget);
}
PChunkyResourceFile ChunkyResourceFile::_PcrfNew(const MMResourceSource &source, LegacyLong budget)
{
    AssertIn(budget, 0, kcbMax);
    static_assert(kctgRsc == 0x52534320 && tNo == 0 && tYes == 1 && tMaybe == 2 &&
                      int(crepNormal) == MM_RESOURCE_NORMAL_PRIORITY, "resource C contract");
    auto cache = NewObj ChunkyResourceFile;
    if (!cache)
        return nullptr;
    const auto services = cache->_Services();
    int diagnostic = 0;
    Debug(diagnostic = 1;)
    if (mm_resource_cache_init(&cache->_cache, cache, &cache->_references, &source, budget, &services,
                               KauaiCopyAllocator(), diagnostic) != MM_STORAGE_OK)
    {
        delete cache;
        return nullptr;
    }
    if (mm_resource_cache_reserve(&cache->_cache, 5) != MM_STORAGE_OK)
        ReleasePpo(&cache);
    AssertNilOrPo(cache, 0);
    return cache;
}
void ChunkyResourceFile::Release(void)
{
    AssertThis(0);
    mm_resource_cache_release(&_cache);
}
void ChunkyResourceFile::SetCbMax(LegacyLong budget)
{
    AssertThis(0);
    AssertIn(budget, 0, kcbMax);
    mm_resource_cache_budget(&_cache, budget);
}
tribool ChunkyResourceFile::TLoad(ChunkTagOrType type, ChunkNumber number, ResourceReader reader, RSC scope,
                                  LegacyLong priority)
{
    AssertThis(0);
    Assert(reader.Core().key != nullptr, "bad object reader");
    Assert(priority > crepToss, "crep too small");
    const auto q = ResourceRequest(type, number, reader, scope, priority);
    return static_cast<tribool>(mm_resource_cache_load(&_cache, &q));
}
PBaseCacheableObject ChunkyResourceFile::PbacoFetch(ChunkTagOrType type, ChunkNumber number, ResourceReader reader, bool *error,
                                                    RSC scope)
{
    AssertThis(0);
    Assert(reader.Core().key != nullptr, "bad object reader");
    AssertNilOrVarMem(error);
    const auto q = ResourceRequest(type, number, reader, scope);
    int failed = 0;
    auto object = static_cast<PBaseCacheableObject>(mm_resource_cache_fetch(&_cache, &q, &failed));
    if (error)
        *error = failed != 0;
    return object;
}
PBaseCacheableObject ChunkyResourceFile::PbacoFind(ChunkTagOrType type, ChunkNumber number, ResourceReader reader, RSC scope)
{
    AssertThis(0);
    Assert(reader.Core().key != nullptr, "bad object reader");
    return static_cast<PBaseCacheableObject>(
        mm_resource_cache_find(&_cache, type, number, reader.Core().key, scope));
}
bool ChunkyResourceFile::FSetCrep(LegacyLong priority, ChunkTagOrType type, ChunkNumber number, ResourceReader reader,
                                  RSC scope)
{
    AssertThis(0);
    Assert(reader.Core().key != nullptr, "bad object reader");
    return mm_resource_cache_priority(&_cache, priority, type, number,
                                            reader.Core().key, scope) != 0;
}
PChunkyResourceFile ChunkyResourceFile::PcrfFindChunk(ChunkTagOrType type, ChunkNumber number, RSC scope)
{
    AssertThis(0);
    return mm_resource_cache_contains(&_cache, type, number, scope) ? this : nullptr;
}
MMChunkAccess ChunkyResourceFile::ReadChunkAccess()
{
    AssertThis(0);
    return mm_resource_cache_chunks(&_cache);
}
bool ChunkyResourceFile::FReadBlock(ChunkTagOrType type, ChunkNumber number, DataBlock &block)
{
    AssertThis(0);
    return mm_resource_cache_read_block(&_cache, type, number, &block._state) == MM_CHUNK_OK;
}
static bool ReadStoredName(const MMChunkIndex *index, MMU32 position, String &name)
{
    unsigned char scratch[MM_CHUNK_NAME_WIRE_BYTES];
    const unsigned char *bytes;
    MMU32 count;
    if (mm_chunk_serialized_name(index, position, KauaiEncodeLegacyChunkName, nullptr, scratch, &bytes,
                                 &count) != MM_CHUNK_OK ||
        !count || !name.FSetData(const_cast<unsigned char *>(bytes), count))
        name.SetNil();
    return name.Cch() > 0;
}
bool ChunkyResourceFile::FGetName(ChunkTagOrType type, ChunkNumber number, PString name)
{
    AssertThis(0);
    AssertPo(name, 0);
    if (Pcfl())
        return Pcfl()->FGetName(type, number, name);
    const auto graph = ReadChunkAccess();
    MMU32 position;
    if (mm_chunk_find(&graph, type, number, &position) == MM_CHUNK_OK)
        return ReadStoredName(_cache.wire_index, position, *name);
    name->SetNil();
    return false;
}
bool ChunkyResourceFile::FCopyChunk(ChunkTagOrType type, ChunkNumber number, PChunkyFile destination, ChunkNumber *copied)
{
    AssertThis(0);
    AssertPo(destination, fcflFull);
    if (Pcfl())
        return Pcfl()->FCopy(type, number, destination, copied);
    if (static_cast<PFileObject>(_cache.wire_file)->GrffilCur() & ffilWriteEnable)
        return false;
    struct Source
    {
        ChunkyResourceFile &cache;
        const MMChunkIndex *index;
        DataBlock block;
        String name;
    } context{*this, _cache.wire_index, {}, {}};
    const auto chunks = ReadChunkAccess();
    MMChunkEndpoint source = {};
    source.identity = this;
    source.context = &context;
    source.stored = &chunks;
    source.allocator = KauaiCopyAllocator();
    source.read_payload = [](void *p, MMU32, const MMChunkEntry *entry) -> void * {
        auto &s = *static_cast<Source *>(p);
        return s.cache.FReadBlock(entry->type, entry->number, s.block) ? &s.block : nullptr;
    };
    source.read_name = [](void *p, MMU32 position) -> const void * {
        auto &s = *static_cast<Source *>(p);
        return ReadStoredName(s.index, position, s.name) ? &s.name : nullptr;
    };
    return destination->_FCopyFrom(source, type, number, copied, false, nullptr);
}
PChunkyResourceFile ChunkyResourceFile::PcrfNewSibling(LegacyLong budget)
{
    AssertThis(0);
    return _PcrfNew({Pcfl(), _cache.wire_index, _cache.wire_file}, budget);
}
#ifdef DEBUG
void ChunkyResourceFile::AssertValid(LegacyUlong flags)
{
    ChunkyResourceFile_PAR::AssertValid(fobjAllocated);
    if (_cache.wire_index)
    {
        Assert(!Pcfl(), "wire cache with a live graph");
        AssertPo(static_cast<PFileObject>(_cache.wire_file), 0);
    }
    else
        AssertPo(Pcfl(), 0);
    AssertIn(_cache.budget, 0, kcbMax);
    Assert(_cache.resident.high >= 0, "negative cache size");
    AssertIn(_cache.releases, 0, kcbMax);
    Assert(_cache.count <= _cache.storage.buffers[0].capacity / sizeof(MMResourceEntry), "bad cache count");
}
void ChunkyResourceFile::MarkMem(void)
{
    AssertThis(0);
    ChunkyResourceFile_PAR::MarkMem();
    MarkPv(_cache.storage.buffers[0].data);
    MarkMemObj(Pcfl());
    if (_cache.wire_file)
        MarkMemObj(static_cast<PFileObject>(_cache.wire_file));
    MMResourceEntry row;
    for (MMU32 i = _cache.count; i-- > 0;)
    {
        AssertDo(mm_resource_cache_at(&_cache, i, &row) == MM_STORAGE_OK, "bad cache row");
        auto object = static_cast<PBaseCacheableObject>(row.object);
        AssertPo(object, 0);
        Assert(object->_resource.cache == &_cache && object->_resource.attached, "baco not attached to this cache");
        MarkMemObj(object);
    }
}
#endif
MMResourceManagerServices ChunkyResourceManager::_Services()
{
    return {nullptr,
            [](void *, void *file, MMI32 budget) -> MMResourceCache * {
                auto cache = ChunkyResourceFile::PcrfNew(static_cast<PChunkyFile>(file), budget);
                return cache ? &cache->_cache : nullptr;
            }};
}
ChunkyResourceManager::ChunkyResourceManager(void)
{
    const auto services = _Services();
    AssertDo(mm_resource_manager_init(&_manager, &services, KauaiCopyAllocator()) == MM_STORAGE_OK,
             "invalid resource manager");
}
ChunkyResourceManager::~ChunkyResourceManager(void)
{
    AssertBaseThis(fobjAllocated);
    AssertDo(mm_resource_manager_dispose(&_manager) == MM_STORAGE_OK, "invalid manager disposal");
}
PChunkyResourceManager ChunkyResourceManager::PcrmNew(LegacyLong reserve)
{
    AssertIn(reserve, 0, kcbMax);
    auto manager = NewObj ChunkyResourceManager();
    if (manager && mm_resource_manager_reserve(&manager->_manager, reserve) != MM_STORAGE_OK)
        ReleasePpo(&manager);
    AssertNilOrPo(manager, 0);
    return manager;
}
tribool ChunkyResourceManager::TLoad(ChunkTagOrType type, ChunkNumber number, ResourceReader reader, RSC scope,
                                     LegacyLong priority)
{
    AssertThis(0);
    Assert(reader.Core().key != nullptr, "nil object reader");
    Assert(_manager.count == 0 || priority > crepToss, "crep too small");
    const auto q = ResourceRequest(type, number, reader, scope, priority);
    return static_cast<tribool>(mm_resource_manager_load(&_manager, &q));
}
PBaseCacheableObject ChunkyResourceManager::PbacoFetch(ChunkTagOrType type, ChunkNumber number, ResourceReader reader,
                                                       bool *error, RSC scope)
{
    AssertThis(0);
    Assert(reader.Core().key != nullptr, "nil object reader");
    AssertNilOrVarMem(error);
    const auto q = ResourceRequest(type, number, reader, scope);
    int failed = 0;
    auto object = static_cast<PBaseCacheableObject>(mm_resource_manager_fetch(&_manager, &q, &failed));
    if (error)
        *error = failed != 0;
    return object;
}
PBaseCacheableObject ChunkyResourceManager::PbacoFind(ChunkTagOrType type, ChunkNumber number, ResourceReader reader, RSC scope)
{
    AssertThis(0);
    Assert(reader.Core().key != nullptr, "nil object reader");
    const auto q = ResourceRequest(type, number, reader, scope);
    return static_cast<PBaseCacheableObject>(mm_resource_manager_find(&_manager, &q));
}
bool ChunkyResourceManager::FSetCrep(LegacyLong priority, ChunkTagOrType type, ChunkNumber number, ResourceReader reader,
                                     RSC scope)
{
    AssertThis(0);
    Assert(reader.Core().key != nullptr, "nil object reader");
    const auto q = ResourceRequest(type, number, reader, scope, priority);
    return mm_resource_manager_priority(&_manager, &q) != 0;
}
PChunkyResourceFile ChunkyResourceManager::PcrfFindChunk(ChunkTagOrType type, ChunkNumber number, RSC scope)
{
    AssertThis(0);
    auto cache = mm_resource_manager_find_file(&_manager, type, number, scope);
    return cache ? static_cast<PChunkyResourceFile>(cache->owner) : nullptr;
}
bool ChunkyResourceManager::FAddCfl(PChunkyFile file, LegacyLong budget, LegacyLong *position)
{
    AssertThis(0);
    AssertPo(file, 0);
    AssertIn(budget, 0, kcbMax);
    AssertNilOrVarMem(position);
    MMU32 at;
    const int result = mm_resource_manager_add(&_manager, file, budget, &at);
    if (result == MM_STORAGE_OK)
    {
        if (position)
            *position = at;
        return true;
    }
    if (result == MM_STORAGE_IO)
        TrashVar(position);
    return false;
}
PChunkyResourceFile ChunkyResourceManager::PcrfGet(LegacyLong position)
{
    AssertThis(0);
    AssertIn(position, 0, kcbMax);
    auto cache = mm_resource_manager_at(&_manager, position);
    return cache ? static_cast<PChunkyResourceFile>(cache->owner) : nullptr;
}
#ifdef DEBUG
void ChunkyResourceManager::AssertValid(LegacyUlong flags)
{
    ChunkyResourceManager_PAR::AssertValid(flags | fobjAllocated);
    Assert(_manager.count <= _manager.storage.buffers[0].capacity / sizeof(MMResourceCache *), "bad manager count");
}
void ChunkyResourceManager::MarkMem(void)
{
    AssertThis(0);
    ChunkyResourceManager_PAR::MarkMem();
    MarkPv(_manager.storage.buffers[0].data);
    for (MMU32 i = 0; i < _manager.count; ++i)
        MarkMemObj(PcrfGet(i));
}
#endif
/***************************************************************************
    A PFNRPO to read GenericHQ objects.
***************************************************************************/
bool GenericHQ::FReadGhq(PChunkyResourceFile pcrf, ChunkTagOrType ctg, ChunkNumber cno, PDataBlock pblck,
                         PBaseCacheableObject *ppbaco, LegacyLong *pcb)
{
    AssertPo(pcrf, 0);
    AssertPo(pblck, 0);
    AssertNilOrVarMem(ppbaco);
    AssertVarMem(pcb);
    GenericHQ *pghq;
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

    if (pvNil == (pghq = NewObj GenericHQ(hq)))
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
    Assert the validity of a GenericHQ.
***************************************************************************/
void GenericHQ::AssertValid(LegacyUlong grf)
{
    GenericHQ_PAR::AssertValid(grf);
    if (hqNil != hq)
        AssertHq(hq);
}

/***************************************************************************
    Mark memory used by the GenericHQ.
***************************************************************************/
void GenericHQ::MarkMem(void)
{
    GenericHQ_PAR::MarkMem();
    MarkHq(hq);
}
#endif // DEBUG

#ifdef DEBUG
/***************************************************************************
    Assert the validity of a GenericCacheableObject.
***************************************************************************/
void GenericCacheableObject::AssertValid(LegacyUlong grf)
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
