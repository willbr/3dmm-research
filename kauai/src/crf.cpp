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
#include "mm/chunk_copy.h"
#include "mm/native_archive_file.h"
#include "mm/private_archive_cache.h"
#include "mm/string_value.h"
#include "resource_archive_host.h"
#include "util.h"
ASSERTNAME

namespace Chunky
{

RTCLASS(BaseCacheableObject)
RTCLASS(GenericHQ)
RTCLASS(ResourceCache)
RTCLASS(ChunkyResourceFile)
RTCLASS(ChunkyResourceManager)

namespace
{
MMResourceRequest ResourceRequest(ChunkTagOrType type, ChunkNumber number, ResourceReader reader, RSC scope,
                                  LegacyLong priority = crepNormal)
{
    return {type, number, scope, reader, priority};
}
} // namespace

const MMResourceObjectServices *BaseCacheableObject::ObjectServices()
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
        },
#ifdef DEBUG
        [](void *o) { MarkMemObj(static_cast<PBaseCacheableObject>(o)); }
#else
        nullptr
#endif
    };
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
    mm_resource_object_dispose(this, ObjectServices());
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
    mm_resource_object_release(this, ObjectServices());
}
void BaseCacheableObject::Detach(void)
{
    AssertThis(0);
    mm_resource_object_detach(this, ObjectServices());
}
void BaseCacheableObject::SetCrep(LegacyLong priority)
{
    AssertThis(0);
    mm_resource_object_priority(this, priority, ObjectServices());
}
#ifdef DEBUG
void BaseCacheableObject::AssertValid(LegacyUlong flags)
{
    BaseCacheableObject_PAR::AssertValid(fobjAllocated);
    Assert(!_resource.attached || _resource.cache != nullptr, "attached baco has no crf");
    AssertNilOrVarMem(_resource.cache);
}
void BaseCacheableObject::MarkMem(void)
{
    AssertValid(0);
    BaseCacheableObject_PAR::MarkMem();
    if (!_resource.attached)
        mm_resource_cache_mark(_resource.cache);
}
#endif
namespace
{
const MMResourceCopyServices *ResourceCopyProfile(bool kauaiBacking)
{
    static const MMResourceCopyServices ordinary = {
        KauaiCopyAllocator(), KauaiChunkOwnerTitleIO(), KauaiEncodeLegacyChunkName, sizeof(achar), nullptr};
    static const auto kauai = [] {
        auto profile = ordinary;
        profile.stored_writable = [](void *file) -> int {
            return (static_cast<PFileObject>(file)->GrffilCur() & ffilWriteEnable) != 0;
        };
        return profile;
    }();
    return kauaiBacking ? &kauai : &ordinary;
}

MMResourceServices TypedCacheServices(MMResourceServices services)
{
    services.context = nullptr;
    services.objects = {};
    services.compare_reader = nullptr;
    services.issue = [](void *, int kind, MMU32, MMU32) {
        if (kind == MM_RESOURCE_LOAD_FAILED)
            PushErc(ercCrfCantLoad);
        else
            Bug("invalid typed resource cache operation");
    };
#ifdef DEBUG
    services.mark_owner = [](MMResourceCache *cache) {
        MarkPv(cache->owner);
        MarkPv(cache->storage.buffers[0].data);
        if (cache->wire_index)
        {
            if (cache->services.block_files == KauaiBlockFileServices())
                MarkMemObj(static_cast<PFileObject>(cache->wire_file));
        }
        else if (cache->services.mark_source)
            cache->services.mark_source(cache);
        for (MMU32 i = cache->count; i-- > 0;)
        {
            MMResourceEntry row;
            AssertDo(mm_resource_cache_at(cache, i, &row) == MM_STORAGE_OK, "bad typed cache row");
            if (row.objects && row.objects->mark_memory)
                row.objects->mark_memory(row.object);
        }
    };
#endif
    return services;
}

MMResourceCache *NewTypedCache(const MMResourceSource &source, const MMResourceServices &profile, MMI32 budget)
{
    const auto services = TypedCacheServices(profile);
    return mm_resource_cache_create(&source, budget, &services, KauaiResourceObjectAllocator(), KauaiCopyAllocator());
}
}

int OpenNativeArchiveSource(Filename *filename, MMNativeArchiveSource *out)
{
    String path;
    filename->GetStnPath(&path);
#ifdef CHUNK_BIG_INDEX
    const int expanded = 1;
#else
    const int expanded = 0;
#endif
    return mm_native_archive_source_open(out, reinterpret_cast<const MMNativePathChar *>(path.Psz()), expanded,
                                          kcbMaxDataStn);
}

MMResourceCache *NewTypedSourceCache(const MMNativeArchiveSource *source, int budget)
{
    auto services = TypedCacheServices({});
    services.handles = KauaiHandleServices();
    services.copy = ResourceCopyProfile(false);
    return mm_native_archive_source_cache(source, budget, &services,
                                           KauaiResourceObjectAllocator(), KauaiCopyAllocator());
}

MMResourceCache *NewTypedSiblingCache(const MMResourceCache *cache, MMI32 budget)
{
    return NewTypedCache(mm_resource_cache_source(cache), cache->services, budget);
}

bool ReadResourceName(const MMResourceCache *cache, ChunkTagOrType type, ChunkNumber number, PString name)
{
    byte wire[kcbMaxDataStn];
    MMU32 count;
    if (!mm_resource_cache_read_name(cache, type, number, KauaiEncodeLegacyChunkName, nullptr, wire, sizeof(wire),
                                     &count) || !name->FSetData(wire, count))
        name->SetNil();
    return name->Cch() > 0;
}

bool ReadResourceName(const MMResourceCache *cache, ChunkTagOrType type, ChunkNumber number,
                      const MMStringValue &out)
{
    String name;
    const bool found = ReadResourceName(cache, type, number, &name);
    mm_string_value_copy(&out, name.Pst());
    return found;
}

namespace
{
MMResourceServices LegacyCacheServices()
{
    MMResourceServices s = {};
    s.contains = [](void *file, MMU32 type, MMU32 number) -> int {
        return static_cast<PChunkyFile>(file)->FFind(type, number);
    };
    s.live_chunks = [](void *file) { return static_cast<PChunkyFile>(file)->ReadChunkAccess(); };
    s.live_reader = [](void *file) { return static_cast<PChunkyFile>(file)->ReadOwner(); };
    s.live_writer = [](void *file) { return static_cast<PChunkyFile>(file)->WriteOwnerIndex(); };
    s.live_reopen = [](void *file) { static_cast<PChunkyFile>(file)->FReopen(); };
    const auto blocks = KauaiChunkPayloadServices();
    s.handles = blocks->handles;
    s.block_files = blocks->files;
    s.locate = blocks->locate;
    s.retain_file = [](void *file) { static_cast<PChunkyFile>(file)->AddRef(); };
    s.release_file = [](void *file) { static_cast<PChunkyFile>(file)->Release(); };
    s.live_name = [](void *file, MMU32 type, MMU32 number, void *out, MMU32 capacity, MMU32 *bytes) {
        const auto reader = static_cast<PChunkyFile>(file)->ReadOwner();
        return mm_chunk_owner_title_read(reader, type, number, out, capacity, bytes);
    };
    s.copy = ResourceCopyProfile(false);
#ifdef DEBUG
    s.mark_source = [](MMResourceCache *cache) { MarkMemObj(static_cast<PChunkyFile>(cache->file)); };
#endif
    return s;
}

MMResourceServices PrivateCacheProfile()
{
    MMResourceServices s = {};
    s.handles = KauaiHandleServices();
#ifdef DEBUG
    s.mark_source = [](MMResourceCache *cache) {
        const auto reader = mm_private_archive_reader(static_cast<MMPrivateArchive *>(cache->file));
        const auto &files = reader->storage->owner->files;
        MarkMemObj(static_cast<PFileObject>(files.main_file));
        MarkMemObj(static_cast<PFileObject>(files.extra_file));
    };
#endif
    return s;
}

}

MMResourceCache *NewTypedLiveCache(PChunkyFile file, MMI32 budget)
{
    const MMResourceSource source = {file, nullptr, nullptr};
    return NewTypedCache(source, LegacyCacheServices(), budget);
}

MMResourceCache *NewTypedPrivateCache(MMPrivateArchive *archive, MMI32 budget)
{
    const auto services = TypedCacheServices(PrivateCacheProfile());
    return mm_private_archive_cache_create(archive, budget, &services, KauaiResourceObjectAllocator(),
                                           KauaiCopyAllocator());
}

MMResourceCache *OpenWorkingResourceCache(Filename *name, bool create, MMI32 budget)
{
    const auto archive = KauaiWorkingArchive(name, create);
    const auto cache = NewTypedPrivateCache(archive, budget);
    mm_private_archive_close(archive);
    return cache;
}

MMResourceCache *OpenForestResourceCache(MMResourceCache *source, MMU32 type, MMU32 number)
{
    const auto files = KauaiNamedArchiveServices();
    const auto archive = mm_private_archive_open_resource_forest(source, type, number, KauaiDecodeBlockHandle,
                                                                 nullptr, files->io, files->blocks, files->profile);
    const auto cache = NewTypedPrivateCache(archive, 0);
    mm_private_archive_close(archive);
    return cache;
}

MMResourceCache *OpenSharedTypedCache(Filename *name, bool preferWorking, bool &selected, MMChunkOwnerRead **reader,
                                      MMI32 budget)
{
    const auto working = TypedCacheServices(PrivateCacheProfile());
    const auto legacy = TypedCacheServices(LegacyCacheServices());
    const MMSharedArchiveCacheServices services = {
        KauaiNamedArchiveServices(), &working, &legacy,
        [](void *name) -> void * { return ChunkyFile::PcflFromFni(static_cast<PFilename>(name)); },
        [](void *archive) -> int { return static_cast<PChunkyFile>(archive)->FSetGrfcfl(fcflNil); },
        KauaiResourceObjectAllocator(), KauaiCopyAllocator()};
    int live;
    const auto cache = mm_shared_archive_cache_open(&services, name, preferWorking, budget, &live, reader);
    selected = live != 0;
    return cache;
}

MMResourceServices ChunkyResourceFile::_Services()
{
    auto s = _privateArchive ? PrivateCacheProfile() : LegacyCacheServices();
    if (_privateArchive)
        mm_private_archive_cache_services(_privateArchive, s.handles, &s);
    s.context = this;
    s.objects = *BaseCacheableObject::ObjectServices();
    s.compare_reader = [](MMResourceBlockReader a, MMResourceBlockReader b) -> int {
        return a == b ? 0 : a < b ? -1 : 1;
    };
#ifdef DEBUG
    s.mark_owner = [](MMResourceCache *cache) { MarkMemObj(static_cast<PChunkyResourceFile>(cache->owner)); };
#endif
    s.destroy_owner = _Destroy;
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
                if (cache->_legacyWireFile)
                    cache->_legacyWireFile->GetStnPath(&path);
                else if (cache->_cache.wire_index)
                    path = cache->_wirePath;
                else if (cache->_privateArchive)
                    static_cast<PFileObject>(cache->ReadOwner()->storage->owner->files.main_file)->GetStnPath(&path);
                else
                {
                    cache->Pcfl()->GetFni(&file);
                    file.GetStnPath(&path);
                }
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
void ChunkyResourceFile::_Destroy(void *owner)
{
    auto cache = static_cast<PChunkyResourceFile>(owner);
    AssertPo(cache, fobjAllocated);
    delete cache;
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
PChunkyResourceFile ChunkyResourceFile::PcrfNewPrivate(MMPrivateArchive *archive, LegacyLong budget)
{
    if (!archive || mm_private_archive_reader(archive)->payloads.files != KauaiBlockFileServices())
        return nullptr;
    return _PcrfNew({archive, nullptr, nullptr}, budget, nullptr, nullptr, archive);
}
PChunkyResourceFile ChunkyResourceFile::PcrfOpenWorking(PFilename name, bool create, LegacyLong budget)
{
    const auto archive = KauaiWorkingArchive(name, create);
    const auto resources = PcrfNewPrivate(archive, budget);
    mm_private_archive_close(archive);
    return resources;
}
PChunkyResourceFile ChunkyResourceFile::PcrfNewIndex(const MMChunkIndex *index, PFileObject backing, LegacyLong budget)
{
    AssertPo(backing, 0);
    return _PcrfNew({nullptr, index, backing}, budget);
}
PChunkyResourceFile ChunkyResourceFile::PcrfNewIndex(const MMChunkIndex *index, void *backing,
                                                    const MMBlockFileServices &files, String &path, LegacyLong budget)
{
    return _PcrfNew({nullptr, index, backing}, budget, &files, &path);
}

PChunkyResourceFile ChunkyResourceFile::_PcrfNew(const MMResourceSource &source, LegacyLong budget,
                                                const MMBlockFileServices *files, String *path,
                                                MMPrivateArchive *archive)
{
    AssertIn(budget, 0, kcbMax);
    static_assert(kctgRsc == 0x52534320 && tNo == 0 && tYes == 1 && tMaybe == 2 &&
                      int(crepTossFirst) == MM_RESOURCE_TOSS_FIRST_PRIORITY &&
                      int(crepNormal) == MM_RESOURCE_NORMAL_PRIORITY, "resource C contract");
    auto cache = NewObj ChunkyResourceFile;
    if (!cache)
        return nullptr;
    cache->_privateArchive = archive;
    auto services = cache->_Services();
    if (files)
    {
        services.block_files = files;
        cache->_wirePath = *path;
    }
    else if (source.index)
        cache->_legacyWireFile = static_cast<PFileObject>(source.backing);
    if (source.index)
        services.copy = ResourceCopyProfile(services.block_files == KauaiBlockFileServices());
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
tribool ResourceCache::TLoad(ChunkTagOrType type, ChunkNumber number, ResourceReader reader, RSC scope,
                             LegacyLong priority)
{
    AssertThis(0);
    Assert(reader != nullptr, "bad object reader");
    Assert((_access.manager && _access.manager->count == 0) || priority > crepToss, "crep too small");
    const auto q = ResourceRequest(type, number, reader, scope, priority);
    const auto objects = _access.manager ? BaseCacheableObject::ObjectServices() : nullptr;
    return static_cast<tribool>(mm_resource_access_load(&_access, &q, objects));
}
PBaseCacheableObject ResourceCache::PbacoFetch(ChunkTagOrType type, ChunkNumber number, ResourceReader reader,
                                               bool *error, RSC scope)
{
    AssertThis(0);
    Assert(reader != nullptr, "bad object reader");
    AssertNilOrVarMem(error);
    const auto q = ResourceRequest(type, number, reader, scope);
    int failed = 0;
    const auto objects = _access.manager ? BaseCacheableObject::ObjectServices() : nullptr;
    auto object = mm_resource_access_fetch(&_access, &q, objects, &failed);
    if (error)
        *error = failed != 0;
    return static_cast<PBaseCacheableObject>(object);
}
PBaseCacheableObject ResourceCache::PbacoFind(ChunkTagOrType type, ChunkNumber number, ResourceReader reader, RSC scope)
{
    AssertThis(0);
    Assert(reader != nullptr, "bad object reader");
    const auto q = ResourceRequest(type, number, reader, scope);
    return static_cast<PBaseCacheableObject>(mm_resource_access_find(&_access, &q));
}
bool ResourceCache::FSetCrep(LegacyLong priority, ChunkTagOrType type, ChunkNumber number, ResourceReader reader,
                             RSC scope)
{
    AssertThis(0);
    Assert(reader != nullptr, "bad object reader");
    const auto q = ResourceRequest(type, number, reader, scope, priority);
    return mm_resource_access_priority(&_access, &q) != 0;
}
MMResourceCache *ResourceCache::FindChunk(ChunkTagOrType type, ChunkNumber number, RSC scope)
{
    AssertThis(0);
    return mm_resource_access_find_cache(&_access, type, number, scope);
}
MMChunkAccess ChunkyResourceFile::ReadChunkAccess()
{
    AssertThis(0);
    return mm_resource_cache_chunks(&_cache);
}
MMChunkOwnerRead *ChunkyResourceFile::ReadOwner()
{
    return mm_resource_cache_reader(&_cache);
}
MMArchiveWriter ChunkyResourceFile::ArchiveWriter()
{
    return mm_resource_cache_writer(&_cache);
}
bool ChunkyResourceFile::FGetName(ChunkTagOrType type, ChunkNumber number, PString name)
{
    AssertThis(0);
    AssertPo(name, 0);
    if (Pcfl())
        return Pcfl()->FGetName(type, number, name);
    return ReadResourceName(&_cache, type, number, name);
}
PChunkyResourceFile ChunkyResourceFile::PcrfNewSibling(LegacyLong budget)
{
    AssertThis(0);
    if (_privateArchive)
        return PcrfNewPrivate(_privateArchive, budget);
    if (_cache.wire_index && !_legacyWireFile)
        return PcrfNewIndex(_cache.wire_index, _cache.wire_file, *_cache.services.block_files, _wirePath, budget);
    return _PcrfNew({Pcfl(), _cache.wire_index, _cache.wire_file}, budget);
}
#ifdef DEBUG
void ChunkyResourceFile::AssertValid(LegacyUlong flags)
{
    ChunkyResourceFile_PAR::AssertValid(fobjAllocated);
    if (_cache.wire_index)
    {
        Assert(!Pcfl(), "wire cache with a live graph");
        Assert(_cache.wire_file, "missing wire cache backing");
        AssertNilOrPo(_legacyWireFile, 0);
    }
    else if (_privateArchive)
        Assert(_cache.file == _privateArchive, "private cache backing differs");
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
    if (_privateArchive)
    {
        const auto &files = ReadOwner()->storage->owner->files;
        MarkMemObj(static_cast<PFileObject>(files.main_file));
        MarkMemObj(static_cast<PFileObject>(files.extra_file));
    }
    if (_legacyWireFile)
        MarkMemObj(_legacyWireFile);
    MMResourceEntry row;
    for (MMU32 i = _cache.count; i-- > 0;)
    {
        AssertDo(mm_resource_cache_at(&_cache, i, &row) == MM_STORAGE_OK, "bad cache row");
        if (row.objects)
        {
            const auto binding = row.objects->state(row.object);
            Assert(binding->cache == &_cache && binding->attached, "resource not attached to this cache");
            if (row.objects && row.objects->mark_memory)
                row.objects->mark_memory(row.object);
            continue;
        }
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
    _access.manager = &_manager;
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
    return cache && cache->services.destroy_owner == ChunkyResourceFile::_Destroy
        ? static_cast<PChunkyResourceFile>(cache->owner) : nullptr;
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
        mm_resource_cache_mark(mm_resource_manager_at(&_manager, i));
}
#endif
/***************************************************************************
    A cache reader for opaque handles.
***************************************************************************/
int GenericHQ::FReadGhq(MMResourceCache *, MMU32, MMU32, MMOwnedBlock *block, void **out, MMI32 *bytes)
{
    *bytes = static_cast<MMI32>(block->view.length);
    if (!out)
        return fTrue;

    HQ handle = nullptr;
    int shrinkFailed = 0;
    if (mm_owned_block_code(block, 0, nullptr, KauaiDecodeBlockHandle, nullptr) != MM_DATA_OK ||
        mm_owned_block_take_handle(block, 0, &handle, &shrinkFailed) != MM_DATA_OK || !handle)
    {
        TrashVar(bytes);
        TrashVar(out);
        return fFalse;
    }
    Assert(!shrinkFailed, "HQ shrink failed");
    *bytes = CbOfHq(handle);

    auto object = NewObj GenericHQ(handle);
    if (!object)
    {
        FreePhq(&handle);
        TrashVar(bytes);
        TrashVar(out);
        return fFalse;
    }
    *out = object;
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

} // end of namespace Chunky
