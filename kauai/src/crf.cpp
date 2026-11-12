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

const ThreeMMResourceObjectServices *BaseCacheableObject::_ObjectServices()
{
    static const ThreeMMResourceObjectServices services = {
        [](void *o) -> ThreeMMResourceObject * { return &static_cast<PBaseCacheableObject>(o)->_resource; },
        [](void *o) -> ThreeMMI32 { return static_cast<PBaseCacheableObject>(o)->_cactRef; },
        [](void *o, ThreeMMI32 n) { static_cast<PBaseCacheableObject>(o)->_cactRef = n; },
        [](void *o) { static_cast<PBaseCacheableObject>(o)->AddRef(); },
        [](void *o) { static_cast<PBaseCacheableObject>(o)->Release(); },
        [](void *o) { static_cast<PBaseCacheableObject>(o)->Detach(); },
        [](void *o, ThreeMMI32 n) { static_cast<PBaseCacheableObject>(o)->SetCrep(n); },
        [](void *o) { delete static_cast<PBaseCacheableObject>(o); },
        [](void *c) { static_cast<PChunkyResourceFile>(c)->AddRef(); },
        [](void *c) { static_cast<PChunkyResourceFile>(c)->Release(); },
        [](void *c, void *o) {
            static_cast<PChunkyResourceFile>(c)->BacoReleased(static_cast<PBaseCacheableObject>(o));
        },
        [](void *c, void *o) {
            static_cast<PChunkyResourceFile>(c)->BacoDetached(static_cast<PBaseCacheableObject>(o));
        },
        [](void *, int kind) {
            if (kind == THREE_MM_RESOURCE_BAD_REFS)
                Bug("calling Release without an AddRef");
            else
                Bug("invalid cacheable object ownership");
        }};
    return &services;
}
BaseCacheableObject::BaseCacheableObject(void)
{
    AssertBaseThis(fobjAllocated);
    three_mm_resource_object_init(&_resource);
}
BaseCacheableObject::~BaseCacheableObject(void)
{
    AssertBaseThis(fobjAllocated);
    Assert(!_resource.attached, "still attached");
    three_mm_resource_object_dispose(this, _ObjectServices());
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
    three_mm_resource_object_release(this, _ObjectServices());
}
void BaseCacheableObject::Detach(void)
{
    AssertThis(0);
    three_mm_resource_object_detach(this, _ObjectServices());
}
void BaseCacheableObject::SetCrep(LegacyLong priority)
{
    AssertThis(0);
    three_mm_resource_object_priority(this, priority, _ObjectServices());
}
#ifdef DEBUG
void BaseCacheableObject::AssertValid(LegacyUlong flags)
{
    BaseCacheableObject_PAR::AssertValid(fobjAllocated);
    Assert(!_resource.attached || _resource.owner != nullptr, "attached baco has no crf");
    AssertNilOrVarMem(static_cast<PChunkyResourceFile>(_resource.owner));
}
void BaseCacheableObject::MarkMem(void)
{
    AssertValid(0);
    BaseCacheableObject_PAR::MarkMem();
    if (!_resource.attached)
        MarkMemObj(static_cast<PChunkyResourceFile>(_resource.owner));
}
#endif
ThreeMMResourceServices ChunkyResourceFile::_Services()
{
    ThreeMMResourceServices s = {};
    s.context = this;
    s.objects = *BaseCacheableObject::_ObjectServices();
    s.compare_reader = [](ThreeMMResourceReader a, ThreeMMResourceReader b) -> int {
        const auto x = reinterpret_cast<PFNRPO>(a), y = reinterpret_cast<PFNRPO>(b);
        return x == y ? 0 : x < y ? -1 : 1;
    };
    s.contains = [](void *c, ThreeMMU32 type, ThreeMMU32 number) -> int {
        return static_cast<PChunkyResourceFile>(c)->Pcfl()->FFind(type, number);
    };
    s.find_block = [](void *c, ThreeMMU32 type, ThreeMMU32 number, void *block) -> int {
        return static_cast<PChunkyResourceFile>(c)->Pcfl()->FFind(type, number, static_cast<PDataBlock>(block));
    };
    s.read = [](void *c, ThreeMMResourceReader reader, ThreeMMU32 type, ThreeMMU32 number, void *block, void **object,
                ThreeMMI32 *bytes) -> int {
        PBaseCacheableObject value = nullptr;
        LegacyLong count = *bytes;
        const bool ok =
            reinterpret_cast<PFNRPO>(reader)(static_cast<PChunkyResourceFile>(c), type, number,
                                             static_cast<PDataBlock>(block), object ? &value : nullptr, &count);
        if (object)
            *object = value;
        *bytes = count;
        return ok;
    };
    s.retain_file = [](void *file) { static_cast<PChunkyFile>(file)->AddRef(); };
    s.release_file = [](void *file) { static_cast<PChunkyFile>(file)->Release(); };
    s.owner_refs = [](void *c) -> ThreeMMI32 { return static_cast<PChunkyResourceFile>(c)->_cactRef; };
    s.set_owner_refs = [](void *c, ThreeMMI32 refs) { static_cast<PChunkyResourceFile>(c)->_cactRef = refs; };
    s.issue = [](void *c, int kind, ThreeMMU32 type, ThreeMMU32 number) {
        if (kind == THREE_MM_RESOURCE_LOAD_FAILED)
        {
#if defined(DEBUG) || defined(STUDIO_TEST_TOOLS)
            const auto batch = std::getenv("THREEDMM_BATCH_TEST");
            if (batch && std::strcmp(batch, "1") == 0)
            {
                Filename file;
                String path;
                char name[kcchMaxStz];
                static_cast<PChunkyResourceFile>(c)->Pcfl()->GetFni(&file);
                file.GetStnPath(&path);
                path.GetSzs(name);
                std::fprintf(stderr, "BATCH_RESOURCE_LOAD_FAILED ctg=%08lx cno=%08lx file=%s\n",
                             static_cast<unsigned long>(type), static_cast<unsigned long>(number), name);
            }
#endif
            PushErc(ercCrfCantLoad);
        }
        else if (kind == THREE_MM_RESOURCE_DUPLICATE)
            Bug("how did this happen?");
        else if (kind == THREE_MM_RESOURCE_BAD_CHARGE)
            Bug("negative resource size");
        else if (kind == THREE_MM_RESOURCE_BAD_OWNER)
            Bug("someone still refers to this ChunkyResourceFile");
        else
            Bug("invalid resource cache ownership");
    };
    return s;
}
ChunkyResourceFile::ChunkyResourceFile(PChunkyFile file, LegacyLong budget)
{
    AssertBaseThis(fobjAllocated);
    AssertPo(file, 0);
    AssertIn(budget, 0, kcbMax);
    static_assert(kctgRsc == 0x52534320 && tNo == 0 && tYes == 1 && tMaybe == 2, "resource C contract");
    const auto services = _Services();
    int diagnostic = 0;
    Debug(diagnostic = 1;)
        AssertDo(three_mm_resource_cache_init(&_cache, this, file, budget, &services, KauaiCopyAllocator(),
                                              diagnostic) == THREE_MM_STORAGE_OK,
                 "invalid resource cache");
}
ChunkyResourceFile::~ChunkyResourceFile(void)
{
    AssertBaseThis(fobjAllocated);
    AssertDo(three_mm_resource_cache_dispose(&_cache) == THREE_MM_STORAGE_OK, "invalid resource cache disposal");
}
PChunkyResourceFile ChunkyResourceFile::PcrfNew(PChunkyFile file, LegacyLong budget)
{
    AssertPo(file, 0);
    AssertIn(budget, 0, kcbMax);
    auto cache = NewObj ChunkyResourceFile(file, budget);
    if (cache && three_mm_resource_cache_reserve(&cache->_cache, 5) != THREE_MM_STORAGE_OK)
        ReleasePpo(&cache);
    AssertNilOrPo(cache, 0);
    return cache;
}
void ChunkyResourceFile::SetCbMax(LegacyLong budget)
{
    AssertThis(0);
    AssertIn(budget, 0, kcbMax);
    three_mm_resource_cache_budget(&_cache, budget);
}
tribool ChunkyResourceFile::TLoad(ChunkTagOrType type, ChunkNumber number, PFNRPO reader, RSC scope,
                                  LegacyLong priority)
{
    AssertThis(0);
    Assert(reader != nullptr, "bad object reader");
    Assert(priority > crepToss, "crep too small");
    DataBlock block;
    return static_cast<tribool>(three_mm_resource_cache_load(
        &_cache, type, number, reinterpret_cast<ThreeMMResourceReader>(reader), scope, priority, &block));
}
PBaseCacheableObject ChunkyResourceFile::PbacoFetch(ChunkTagOrType type, ChunkNumber number, PFNRPO reader, bool *error,
                                                    RSC scope)
{
    AssertThis(0);
    Assert(reader != nullptr, "bad object reader");
    AssertNilOrVarMem(error);
    DataBlock block;
    int failed = 0;
    auto object = static_cast<PBaseCacheableObject>(three_mm_resource_cache_fetch(
        &_cache, type, number, reinterpret_cast<ThreeMMResourceReader>(reader), scope, &block, &failed));
    if (error)
        *error = failed != 0;
    return object;
}
PBaseCacheableObject ChunkyResourceFile::PbacoFind(ChunkTagOrType type, ChunkNumber number, PFNRPO reader, RSC scope)
{
    AssertThis(0);
    Assert(reader != nullptr, "bad object reader");
    return static_cast<PBaseCacheableObject>(
        three_mm_resource_cache_find(&_cache, type, number, reinterpret_cast<ThreeMMResourceReader>(reader), scope));
}
bool ChunkyResourceFile::FSetCrep(LegacyLong priority, ChunkTagOrType type, ChunkNumber number, PFNRPO reader,
                                  RSC scope)
{
    AssertThis(0);
    Assert(reader != nullptr, "bad object reader");
    return three_mm_resource_cache_priority(&_cache, priority, type, number,
                                            reinterpret_cast<ThreeMMResourceReader>(reader), scope) != 0;
}
PChunkyResourceFile ChunkyResourceFile::PcrfFindChunk(ChunkTagOrType type, ChunkNumber number, RSC scope)
{
    AssertThis(0);
    return three_mm_resource_cache_contains(&_cache, type, number, scope) ? this : nullptr;
}
void ChunkyResourceFile::BacoDetached(PBaseCacheableObject object)
{
    AssertThis(0);
    AssertPo(object, 0);
    three_mm_resource_cache_detached(&_cache, object);
}
void ChunkyResourceFile::BacoReleased(PBaseCacheableObject object)
{
    AssertThis(0);
    AssertPo(object, 0);
    three_mm_resource_cache_released(&_cache, object);
}
#ifdef DEBUG
void ChunkyResourceFile::AssertValid(LegacyUlong flags)
{
    ChunkyResourceFile_PAR::AssertValid(fobjAllocated);
    AssertPo(Pcfl(), 0);
    AssertIn(_cache.budget, 0, kcbMax);
    Assert(_cache.resident.high >= 0, "negative cache size");
    AssertIn(_cache.releases, 0, kcbMax);
    Assert(_cache.count <= _cache.storage.buffers[0].capacity / sizeof(ThreeMMResourceEntry), "bad cache count");
}
void ChunkyResourceFile::MarkMem(void)
{
    AssertThis(0);
    ChunkyResourceFile_PAR::MarkMem();
    MarkPv(_cache.storage.buffers[0].data);
    MarkMemObj(Pcfl());
    ThreeMMResourceEntry row;
    for (ThreeMMU32 i = _cache.count; i-- > 0;)
    {
        AssertDo(three_mm_resource_cache_at(&_cache, i, &row) == THREE_MM_STORAGE_OK, "bad cache row");
        auto object = static_cast<PBaseCacheableObject>(row.object);
        AssertPo(object, 0);
        Assert(object->_resource.attached, "baco not attached");
        object->_resource.attached = 1;
        MarkMemObj(object);
    }
}
#endif
namespace
{
ThreeMMResourceRequest ResourceRequest(ChunkTagOrType type, ChunkNumber number, PFNRPO reader, RSC scope,
                                       LegacyLong priority = crepNormal)
{
    return {type, number, scope, reinterpret_cast<ThreeMMResourceReader>(reader), priority};
}
} // namespace
ThreeMMResourceManagerServices ChunkyResourceManager::_Services()
{
    return {nullptr,
            [](void *, void *file, ThreeMMI32 budget) -> void * {
                return ChunkyResourceFile::PcrfNew(static_cast<PChunkyFile>(file), budget);
            },
            [](void *c) { static_cast<PChunkyResourceFile>(c)->Release(); },
            [](void *c, const ThreeMMResourceRequest *q) -> int {
                return static_cast<PChunkyResourceFile>(c)->TLoad(
                    q->type, q->number, reinterpret_cast<PFNRPO>(q->reader), q->scope, q->priority);
            },
            [](void *c, const ThreeMMResourceRequest *q, int *error) -> void * {
                bool failed = false;
                auto object = static_cast<PChunkyResourceFile>(c)->PbacoFetch(
                    q->type, q->number, reinterpret_cast<PFNRPO>(q->reader), &failed, q->scope);
                *error = failed;
                return object;
            },
            [](void *c, const ThreeMMResourceRequest *q) -> void * {
                return static_cast<PChunkyResourceFile>(c)->PbacoFind(q->type, q->number,
                                                                      reinterpret_cast<PFNRPO>(q->reader), q->scope);
            },
            [](void *c, const ThreeMMResourceRequest *q) -> int {
                return static_cast<PChunkyResourceFile>(c)->FSetCrep(q->priority, q->type, q->number,
                                                                     reinterpret_cast<PFNRPO>(q->reader), q->scope);
            },
            [](void *c, ThreeMMU32 type, ThreeMMU32 number, ThreeMMU32 scope) -> int {
                return three_mm_resource_cache_contains(&static_cast<PChunkyResourceFile>(c)->_cache, type, number,
                                                        scope);
            }};
}
ChunkyResourceManager::ChunkyResourceManager(void)
{
    const auto services = _Services();
    AssertDo(three_mm_resource_manager_init(&_manager, &services, KauaiCopyAllocator()) == THREE_MM_STORAGE_OK,
             "invalid resource manager");
}
ChunkyResourceManager::~ChunkyResourceManager(void)
{
    AssertBaseThis(fobjAllocated);
    AssertDo(three_mm_resource_manager_dispose(&_manager) == THREE_MM_STORAGE_OK, "invalid manager disposal");
}
PChunkyResourceManager ChunkyResourceManager::PcrmNew(LegacyLong reserve)
{
    AssertIn(reserve, 0, kcbMax);
    auto manager = NewObj ChunkyResourceManager();
    if (manager && three_mm_resource_manager_reserve(&manager->_manager, reserve) != THREE_MM_STORAGE_OK)
        ReleasePpo(&manager);
    AssertNilOrPo(manager, 0);
    return manager;
}
tribool ChunkyResourceManager::TLoad(ChunkTagOrType type, ChunkNumber number, PFNRPO reader, RSC scope,
                                     LegacyLong priority)
{
    AssertThis(0);
    Assert(reader != nullptr, "nil object reader");
    const auto q = ResourceRequest(type, number, reader, scope, priority);
    return static_cast<tribool>(three_mm_resource_manager_load(&_manager, &q));
}
PBaseCacheableObject ChunkyResourceManager::PbacoFetch(ChunkTagOrType type, ChunkNumber number, PFNRPO reader,
                                                       bool *error, RSC scope)
{
    AssertThis(0);
    Assert(reader != nullptr, "nil object reader");
    AssertNilOrVarMem(error);
    const auto q = ResourceRequest(type, number, reader, scope);
    int failed = 0;
    auto object = static_cast<PBaseCacheableObject>(three_mm_resource_manager_fetch(&_manager, &q, &failed));
    if (error)
        *error = failed != 0;
    return object;
}
PBaseCacheableObject ChunkyResourceManager::PbacoFind(ChunkTagOrType type, ChunkNumber number, PFNRPO reader, RSC scope)
{
    AssertThis(0);
    Assert(reader != nullptr, "nil object reader");
    const auto q = ResourceRequest(type, number, reader, scope);
    return static_cast<PBaseCacheableObject>(three_mm_resource_manager_find(&_manager, &q));
}
bool ChunkyResourceManager::FSetCrep(LegacyLong priority, ChunkTagOrType type, ChunkNumber number, PFNRPO reader,
                                     RSC scope)
{
    AssertThis(0);
    Assert(reader != nullptr, "nil object reader");
    const auto q = ResourceRequest(type, number, reader, scope, priority);
    return three_mm_resource_manager_priority(&_manager, &q) != 0;
}
PChunkyResourceFile ChunkyResourceManager::PcrfFindChunk(ChunkTagOrType type, ChunkNumber number, RSC scope)
{
    AssertThis(0);
    return static_cast<PChunkyResourceFile>(three_mm_resource_manager_find_file(&_manager, type, number, scope));
}
bool ChunkyResourceManager::FAddCfl(PChunkyFile file, LegacyLong budget, LegacyLong *position)
{
    AssertThis(0);
    AssertPo(file, 0);
    AssertIn(budget, 0, kcbMax);
    AssertNilOrVarMem(position);
    ThreeMMU32 at;
    const int result = three_mm_resource_manager_add(&_manager, file, budget, &at);
    if (result == THREE_MM_STORAGE_OK)
    {
        if (position)
            *position = at;
        return true;
    }
    if (result == THREE_MM_STORAGE_IO)
        TrashVar(position);
    return false;
}
PChunkyResourceFile ChunkyResourceManager::PcrfGet(LegacyLong position)
{
    AssertThis(0);
    AssertIn(position, 0, kcbMax);
    return static_cast<PChunkyResourceFile>(three_mm_resource_manager_at(&_manager, position));
}
#ifdef DEBUG
void ChunkyResourceManager::AssertValid(LegacyUlong flags)
{
    ChunkyResourceManager_PAR::AssertValid(flags | fobjAllocated);
    Assert(_manager.count <= _manager.storage.buffers[0].capacity / sizeof(void *), "bad manager count");
}
void ChunkyResourceManager::MarkMem(void)
{
    AssertThis(0);
    ChunkyResourceManager_PAR::MarkMem();
    MarkPv(_manager.storage.buffers[0].data);
    for (ThreeMMU32 i = 0; i < _manager.count; ++i)
        MarkMemObj(static_cast<PChunkyResourceFile>(three_mm_resource_manager_at(&_manager, i)));
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
