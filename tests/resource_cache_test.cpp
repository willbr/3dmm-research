/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "kauai_core.h"
#include "handle_services.h"
#include "resource_archive_host.h"
#include "mm/chunk_copy.h"
#include "mm/native_archive_file.h"
#include "mm/tag.h"
#undef size
ASSERTNAME

void WarnProc(schar *file, LegacyLong line, schar *message)
{
    std::fprintf(stderr, "%s:%" LEGACY_PRId ": %s\n", file, line, message);
}
bool FAssertProc(schar *file, LegacyLong line, schar *message, void *, LegacyLong)
{
    WarnProc(file, line, message);
    std::exit(1);
}

#define CHECK(value)                                                                                                   \
    do                                                                                                                 \
    {                                                                                                                  \
        if (!(value))                                                                                                  \
        {                                                                                                              \
            std::fprintf(stderr, "Cache check: %d\n", __LINE__);                                                       \
            return 1;                                                                                                  \
        }                                                                                                              \
    } while (0)

namespace
{
int resident = 0;
// Report realistic decoded-resource sizes without allocating gigabytes in a test.
class SizedResource : public BaseCacheableObject
{
  public:
    SizedResource()
    {
        ++resident;
    }
    ~SizedResource()
    {
        --resident;
    }
    static int Read(MMResourceCache *, MMU32, MMU32, MMOwnedBlock *, void **result, MMI32 *bytes)
    {
        *bytes = 192 * 1024 * 1024;
        if (result)
            *result = NewObj SizedResource;
        return !result || *result;
    }
};
} // namespace

namespace
{
FILE *resourceSnapshot = nullptr;
int probeResident = 0;
PBaseCacheableObject cascade = nullptr;
void Observe(uint32_t kind, uint32_t id, uint32_t type, uint32_t number, uint32_t value, uint32_t refs, uint32_t owner)
{
    const uint32_t row[] = {kind, id, type, number, value, refs, owner};
    if (resourceSnapshot && std::fwrite(row, sizeof(row), 1, resourceSnapshot) != 1)
        std::exit(1);
}
class ProbeResource : public BaseCacheableObject
{
    int _id;

  public:
    explicit ProbeResource(int id) : _id(id)
    {
        ++probeResident;
        Observe(2, id, 0, 0, 0, 0, 0);
    }
    ~ProbeResource()
    {
        Observe(3, _id, Ctg(), Cno(), static_cast<uint32_t>(Crep()), CactRef(), CoreCache() != nullptr);
        --probeResident;
        if (_id == 301 && cascade)
        {
            auto other = cascade;
            cascade = nullptr;
            ReleasePpo(&other);
        }
    }
    static int Read(MMU32 type, MMU32 number, MMOwnedBlock *block, void **result, MMI32 *bytes, int reader)
    {
        MMBlockIO io;
        int32_t record[5];
        if (mm_owned_block_io(block, &io) != MM_DATA_OK ||
            mm_block_read(&io, 0, record, sizeof(record), 0) != MM_DATA_OK)
            return false;
        *bytes = record[result ? 2 : 1];
        Observe(result ? 1 : 0, record[0] * 10 + reader, type, number, *bytes, 0, 0);
        if (record[result ? 4 : 3])
            return false;
        if (result)
            *result = NewObj ProbeResource(record[0] * 10 + reader);
        return !result || *result != nullptr;
    }
    static int A(MMResourceCache *, MMU32 t, MMU32 n, MMOwnedBlock *b, void **o, MMI32 *s)
    {
        return Read(t, n, b, o, s, 1);
    }
    static int B(MMResourceCache *, MMU32 t, MMU32 n, MMOwnedBlock *b, void **o, MMI32 *s)
    {
        return Read(t, n, b, o, s, 2);
    }
};
bool Put(PChunkyFile file, ChunkNumber number, int id, int estimate, int actual, int failEstimate = 0, int failRead = 0)
{
    int32_t data[] = {id, estimate, actual, failEstimate, failRead};
    return file->FPutPv(data, sizeof(data), 'RCHE', number);
}
int CacheCases()
{
    auto file = ChunkyFile::PcflCreateTemp();
    CHECK(file);
    CHECK(Put(file, 11, 10, 4, 8) && Put(file, 12, 20, 4, 8) && Put(file, 13, 30, 4, 8) && Put(file, 14, 40, 1, 12) &&
          Put(file, 15, 50, 1, 20));
    auto cache = ChunkyResourceFile::PcrfNew(file, 16);
    CHECK(cache);
    CHECK(cache->TLoad('RCHE', 11, ProbeResource::A, rscNil, 2) == tYes &&
          cache->TLoad('RCHE', 12, ProbeResource::A, rscNil, 2) == tYes);
    auto held = cache->PbacoFetch('RCHE', 11, ProbeResource::A);
    CHECK(held);
    ReleasePpo(&held);
    CHECK(cache->TLoad('RCHE', 13, ProbeResource::A, rscNil, 3) == tYes &&
          !cache->PbacoFind('RCHE', 12, ProbeResource::A));
    held = cache->PbacoFind('RCHE', 11, ProbeResource::A);
    CHECK(held);
    ReleasePpo(&held);
    cache->SetCbMax(0);
    CHECK(!probeResident);
    ReleasePpo(&cache);
    cache = ChunkyResourceFile::PcrfNew(file, 16);
    CHECK(cache && cache->TLoad('RCHE', 11, ProbeResource::A, rscNil, 1) == tYes);
    CHECK(cache->TLoad('RCHE', 14, ProbeResource::A, rscNil, 3) == tYes &&
          !cache->PbacoFind('RCHE', 11, ProbeResource::A));
    cache->SetCbMax(0);
    CHECK(!probeResident);
    ReleasePpo(&cache);
    cache = ChunkyResourceFile::PcrfNew(file, 16);
    CHECK(cache && cache->TLoad('RCHE', 15, ProbeResource::A, rscNil, 3) == tMaybe && !probeResident);
    ReleasePpo(&cache);
    cache = ChunkyResourceFile::PcrfNew(file, 100);
    CHECK(cache && cache->TLoad('RCHE', 13, ProbeResource::A) == tYes);
    held = cache->PbacoFetch('RCHE', 14, ProbeResource::A);
    CHECK(held);
    cascade = held;
    held = nullptr;
    cache->SetCbMax(0);
    CHECK(!cascade && !probeResident);
    ReleasePpo(&cache);
    cache = ChunkyResourceFile::PcrfNew(file, 100);
    CHECK(cache);
    held = cache->PbacoFetch('RCHE', 11, ProbeResource::A);
    CHECK(held && cache->TLoad('RCHE', 12, ProbeResource::A) == tYes);
    ReleasePpo(&cache);
    CHECK(held->CoreCache() == nullptr && held->CactRef() == 1 && probeResident == 1);
    ReleasePpo(&held);
    CHECK(!probeResident);
    cache = ChunkyResourceFile::PcrfNew(file, 100);
    CHECK(cache);
    CHECK(cache->TLoad('RCHE', 11, ProbeResource::A) == tYes && cache->TLoad('RCHE', 11, ProbeResource::B) == tYes);
    held = cache->PbacoFind('RCHE', 11, ProbeResource::A);
    auto alternate = cache->PbacoFind('RCHE', 11, ProbeResource::B);
    CHECK(held && alternate && held != alternate);
    held->Detach();
    CHECK(!cache->PbacoFind('RCHE', 11, ProbeResource::A));
    ReleasePpo(&held);
    alternate->SetCrep(crepToss);
    ReleasePpo(&alternate);
    CHECK(!probeResident);
    ReleasePpo(&cache);
    ReleasePpo(&file);
    return 0;
}
int ManagerCases()
{
    const MMResourceAccess empty = {};
    const MMResourceRequest request = {'RCHE', 1, 0, ProbeResource::A, MM_RESOURCE_NORMAL_PRIORITY};
    int missing = 1;
    CHECK(!mm_resource_access_fetch(&empty, &request, nullptr, &missing) && !missing);
    CHECK(!mm_resource_access_load(&empty, &request, nullptr) && !mm_resource_access_find(&empty, &request) &&
          !mm_resource_access_priority(&empty, &request) && !mm_resource_access_find_cache(&empty, 'RCHE', 1, 0));
    auto a = ChunkyFile::PcflCreateTemp(), b = ChunkyFile::PcflCreateTemp();
    CHECK(a && b);
    CHECK(Put(a, 1, 1, 4, 8) && Put(b, 1, 2, 4, 8) && Put(a, 2, 5, 4, 8, 0, 1) && Put(b, 2, 6, 4, 8) &&
          Put(b, 3, 7, 4, 8));
    CHECK(Put(a, 4, 8, 4, 8, 1, 0) && Put(b, 4, 9, 4, 8) && a->FPutPv(nullptr, 0, kctgRsc, 11) &&
          b->FPutPv(nullptr, 0, kctgRsc, 22));
    auto manager = ChunkyResourceManager::PcrmNew(0);
    CHECK(manager);
    LegacyLong slot = -1;
    CHECK(manager->FAddCfl(a, 100, &slot) && slot == 0 && manager->FAddCfl(b, 100, &slot) && slot == 1 &&
          manager->Ccrf() == 2 && !manager->PcrfGet(2));
    CHECK(manager->FindChunk('RCHE', 1, 11) == manager->PcrfGet(0)->CoreCache() &&
          manager->FindChunk('RCHE', 1, 22) == manager->PcrfGet(1)->CoreCache());
    bool error = true;
    auto first = manager->PbacoFetch('RCHE', 1, ProbeResource::A, &error);
    CHECK(first && !error && first->CoreCache() == manager->PcrfGet(0)->CoreCache());
    auto second = manager->PcrfGet(1)->PbacoFetch('RCHE', 1, ProbeResource::A);
    CHECK(second);
    auto found = manager->PbacoFind('RCHE', 1, ProbeResource::A, 22);
    CHECK(found == second);
    ReleasePpo(&found);
    CHECK(manager->FSetCrep(1, 'RCHE', 1, ProbeResource::A, 22) && second->Crep() == 1);
    CHECK(!manager->PbacoFetch('RCHE', 2, ProbeResource::A, &error) && error);
    CHECK(manager->TLoad('RCHE', 4, ProbeResource::A) == tMaybe &&
          manager->TLoad('RCHE', 3, ProbeResource::A, 22) == tYes);
    ReleasePpo(&manager);
    CHECK(first->CoreCache() == nullptr && second->CoreCache() == nullptr && probeResident == 2);
    ReleasePpo(&first);
    ReleasePpo(&second);
    CHECK(!probeResident && a->CactRef() == 1 && b->CactRef() == 1);
    ReleasePpo(&a);
    ReleasePpo(&b);
    return 0;
}

int TypedManagerCases()
{
    auto file = ChunkyFile::PcflCreateTemp();
    CHECK(file && Put(file, 1, 7, 4, 8));
    auto wrapped = ChunkyResourceFile::PcrfNew(file, 0);
    CHECK(wrapped);
    auto typed = mm_resource_cache_typed_sibling(wrapped->CoreCache(), 100);
    ReleasePpo(&wrapped);
    ReleasePpo(&file);
    auto manager = ChunkyResourceManager::PcrmNew(1);
    CHECK(manager && typed && !typed->services.objects.state);
    CHECK(mm_resource_manager_add_cache(manager->CoreManager(), typed, nullptr) == MM_STORAGE_OK);
    mm_resource_cache_release(typed);
    CHECK(manager->FindChunk('RCHE', 1) == typed && !manager->PcrfGet(0));
    CHECK(manager->TLoad('RCHE', 1, GenericHQ::FReadGhq) == tYes);
    auto preloaded = manager->PbacoFind('RCHE', 1, GenericHQ::FReadGhq);
    auto fetched = manager->PbacoFetch('RCHE', 1, GenericHQ::FReadGhq);
    CHECK(preloaded && fetched == preloaded && fetched->CoreCache() == typed);
    CHECK(manager->TLoad('RCHE', 2, GenericHQ::FReadGhq) == tNo);
    ReleasePpo(&preloaded);
    ReleasePpo(&manager);
    CHECK(!fetched->CoreCache());
    ReleasePpo(&fetched);
    return 0;
}
} // namespace
static int LargeResidentCase()
{
    auto file = ChunkyFile::PcflCreateTemp();
    CHECK(file);
    auto cache = ChunkyResourceFile::PcrfNew(file, 0);
    CHECK(cache);
    std::array<PBaseCacheableObject, 24> held{};
    std::array<ChunkNumber, 24> numbers{};
    for (std::size_t i = 0; i < held.size(); ++i)
    {
        CHECK(file->FAdd(0, 'TEST', &numbers[i]));
        held[i] = cache->PbacoFetch('TEST', numbers[i], SizedResource::Read);
        CHECK(held[i] && resident == int(i + 1));
    }
    // Decoded objects can individually exceed the legacy 128 MiB allocation
    // limit, and their combined resident size can exceed signed 32-bit size.
    for (std::size_t i = 0; i < held.size(); ++i)
    {
        auto found = cache->PbacoFind('TEST', numbers[i], SizedResource::Read);
        CHECK(found == held[i]);
        ReleasePpo(&found);
    }
    for (auto &object : held)
        ReleasePpo(&object);
    CHECK(resident == 0);
    for (auto number : numbers)
        CHECK(!cache->PbacoFind('TEST', number, SizedResource::Read));
    ReleasePpo(&cache);
    ReleasePpo(&file);
    std::puts("Resource cache: large aggregate sizes retain in-use objects and evict released objects.");
    return 0;
}

static int WireSourceCase(bool native)
{
    Filename path;
    CHECK(path.FGetTemp());
    const ChunkTagOrType type = 'TEST';
    const ChunkNumber root = 17, glyph = 21;
    std::array<unsigned char, 512> stored;
    stored.fill(42);
    unsigned char changed = 99;
    String authored, name;
    authored.SetSzs(PszLit("Stored name"));
    auto live = ChunkyFile::PcflCreate(&path, fcflWriteEnable);
    CHECK(live && live->FPutPv(nullptr, 0, type, root) && live->FPutPv(stored.data(), stored.size(), type, glyph));
    CHECK(live->FPackData(type, glyph));
    CHECK(live->FSetName(type, root, &authored) && live->FAdoptChild(type, root, type, glyph, 0) && live->FSave(type));
    ReleasePpo(&live);

    String nativePath;
    path.GetStnPath(&nativePath);
    PChunkyResourceFile cache;
    if (native)
    {
        MMNativeArchiveSource source = {};
        CHECK(OpenNativeArchiveSource(&path, &source) == MM_ARCHIVE_READ_OK);
        Filename invalid;
        CHECK(invalid.FGetTemp());
        auto empty = FileObject::PfilCreate(&invalid);
        CHECK(empty);
        ReleasePpo(&empty);
        CHECK(OpenNativeArchiveSource(&invalid, &source) == MM_ARCHIVE_INVALID_PAYLOAD && invalid.FDelete());

        MMArchiveReaderOptions options = {};
        options.max_packed_bytes = stored.size();
        MMArchiveReader *reader = nullptr;
        CHECK(mm_native_archive_source_reader(&source, &options, &reader) == MM_ARCHIVE_READ_OK);
        CHECK(mm_archive_reader_wire_index(reader) == source.index);
        const auto chunks = mm_archive_reader_access(reader);
        MMU32 position;
        CHECK(mm_chunk_find(&chunks, type, glyph, &position) == MM_CHUNK_OK);
        MMReadStream payload;
        std::array<unsigned char, 512> decoded;
        CHECK(mm_archive_reader_payload(reader, position, &payload) == MM_ARCHIVE_READ_OK &&
              payload.length == decoded.size() && mm_read_exact(&payload, 0, decoded.data(), decoded.size()) &&
              decoded == stored);
        mm_archive_reader_close(reader);

        auto typed = NewTypedSourceCache(&source, 1024);
        CHECK(typed && typed->wire_index == source.index && typed->wire_file == source.file);
        cache = ChunkyResourceFile::PcrfNewIndex(source.index, source.file, *mm_native_archive_file_blocks(),
                                                nativePath, 1024);
        mm_native_archive_source_clear(&source);
        CHECK(ReadResourceName(typed, type, root, &name) && name.FEqual(&authored));
        mm_resource_cache_release(typed);
    }
    else
    {
        auto backing = FileObject::PfilOpen(&path);
        CHECK(backing);
        const auto stream = backing->ReadStream();
        const unsigned short order = 1;
        MMChunkPolicy policy = {};
#ifdef CHUNK_BIG_INDEX
        policy.expanded_runtime = 1;
#endif
        policy.native_big_endian = !*reinterpret_cast<const unsigned char *>(&order);
        policy.max_name_bytes = kcbMaxDataStn;
        MMChunkIndex *index;
        CHECK(mm_chunk_index_open(&stream, &policy, &index) == MM_CHUNK_OK);
        cache = ChunkyResourceFile::PcrfNewIndex(index, backing, 1024);
        mm_chunk_index_close(index);
        ReleasePpo(&backing);
    }
    CHECK(cache && !cache->Pcfl() && !ChunkyFile::PcflFromFni(&path));
    auto sibling = cache->PcrfNewSibling(1024);
    CHECK(sibling && !sibling->Pcfl());
    ReleasePpo(&cache);
    cache = sibling;
    CHECK(cache->FGetName(type, root, &name) && name.FEqual(&authored));

    live = ChunkyFile::PcflOpen(&path, fcflAddToExtra);
    CHECK(live && live->FPutPv(&changed, 1, type, glyph));
    live->SetPacked(type, glyph, false);
    auto writable = ChunkyFile::PcflCreateTemp();
    CHECK(writable);
    auto target = ChunkyResourceFile::PcrfNew(writable, 1024);
    CHECK(target);
    MMTag tag = {0, nullptr, type, root};
    const auto tags = mm_tag_local_cache_services();
    CHECK(mm_tag_open(&tag, cache->CoreCache(), nullptr, tags));
    CHECK(mm_tag_save(&tag, target->CoreCache(), 1, tags) && tag.cache == target->CoreCache());
    const ChunkNumber copied = tag.number;
    mm_tag_close(&tag, tags);
    ReleasePpo(&target);
    CHECK(ChunkyFile::PcflFromFni(&path) == live);
    ChildChunkIdentification child;
    DataBlock block;
    MMOwnedBlock original;
    MMBlockIO originalIO;
    unsigned char value;
    std::array<unsigned char, 512> decoded, packed;
    CHECK(copied == root && writable->FGetKidChid(type, copied, 0, &child) && child.cki.ctg == type);
    CHECK(writable->FFind(type, child.cki.cno, &block) && block.FPacked());
    const auto core = cache->CoreCache();
    CHECK(mm_owned_block_init(&original, core->services.handles, KauaiBlockFileServices()) == MM_DATA_OK);
    CHECK(mm_resource_cache_read_block(core, type, glyph, &original) == MM_CHUNK_OK && original.view.packed);
    CHECK(original.files == core->services.block_files);
    CHECK(mm_owned_block_io(&original, &originalIO) == MM_DATA_OK);
    const auto packedBytes = original.view.length;
    CHECK(packedBytes > 0 && packedBytes <= packed.size() && block.Cb(true) == packedBytes);
    CHECK(mm_block_read(&originalIO, 0, packed.data(), packedBytes, 1) == MM_DATA_OK && block.FRead(decoded.data(), true));
    CHECK(!std::memcmp(packed.data(), decoded.data(), packedBytes) && block.FUnpackData());
    CHECK(block.Cb() == decoded.size() && block.FRead(decoded.data()) && decoded == stored);
    auto generic = static_cast<PGenericHQ>(cache->PbacoFetch(type, glyph, GenericHQ::FReadGhq));
    CHECK(generic && CbOfHq(generic->hq) == stored.size());
    const auto payload = PvLockHq(generic->hq);
    CHECK(!std::memcmp(payload, stored.data(), stored.size()));
    UnlockHq(generic->hq);
    MMResourceEntry entry;
    CHECK(mm_resource_cache_at(cache->CoreCache(), 0, &entry) == MM_STORAGE_OK && entry.bytes == stored.size());
    ReleasePpo(&generic);
    CHECK(live->FFind(type, glyph, &block) && block.FReadRgb(&value, 1, 0) && value == changed);
    CHECK(writable->FGetName(type, copied, &name) && name.FEqual(&authored));
    block.Free();
    ReleasePpo(&writable);
    ReleasePpo(&live);

    Filename privatePath;
    CHECK(privatePath.FGetTemp());
    // The new working archive owns cleanup of its temporary file.
    const auto privateTarget = OpenWorkingResourceCache(&privatePath, true, 1024);
    CHECK(privateTarget && !privateTarget->services.objects.state);
    MMResourceChunkCopy sourceBinding;
    const auto source = mm_resource_cache_copy_source(core, &sourceBinding);
    tag = {0, nullptr, type, root};
    CHECK(mm_tag_open(&tag, privateTarget, &source, tags) && tag.cache == privateTarget);
    CHECK(ReadResourceName(privateTarget, type, tag.number, &name) && name.FEqual(&authored));
    MMTag copiedChild = {};
    CHECK(mm_tag_child(&tag, 0, type, &copiedChild, tags));
    MMOwnedBlock privateBlock;
    MMBlockIO privateIO;
    CHECK(mm_owned_block_init(&privateBlock, privateTarget->services.handles,
                              privateTarget->services.block_files) == MM_DATA_OK);
    CHECK(mm_resource_cache_read_block(privateTarget, type, copiedChild.number, &privateBlock) == MM_CHUNK_OK &&
          privateBlock.view.packed && privateBlock.view.length == packedBytes);
    CHECK(mm_owned_block_io(&privateBlock, &privateIO) == MM_DATA_OK &&
          mm_block_read(&privateIO, 0, decoded.data(), packedBytes, 1) == MM_DATA_OK &&
          !std::memcmp(packed.data(), decoded.data(), packedBytes));
    CHECK(mm_owned_block_clear(&privateBlock) == MM_DATA_OK);
    mm_tag_close(&copiedChild, tags);
    mm_tag_close(&tag, tags);
    mm_resource_cache_release(privateTarget);

    if (!native)
    {
        auto backing = static_cast<PFileObject>(core->wire_file);
        CHECK(backing->FSetGrffil(ffilWriteEnable, ffilWriteEnable));
        CHECK(!mm_resource_cache_copy_source(cache->CoreCache(), &sourceBinding).identity);
    }
    ReleasePpo(&cache);
    CHECK(mm_block_read(&originalIO, 0, decoded.data(), packedBytes, 1) == MM_DATA_OK &&
          !std::memcmp(packed.data(), decoded.data(), packedBytes));
    CHECK(mm_owned_block_clear(&original) == MM_DATA_OK);
    CHECK(path.FDelete());
    return 0;
}

static int SharedArchiveSourceCase()
{
    Filename path;
    CHECK(path.FGetTemp());
    const ChunkTagOrType type = 'TEST';
    const ChunkNumber number = 3;
    String saved, pending, name;
    saved.SetSzs(PszLit("Saved name"));
    pending.SetSzs(PszLit("Pending name"));

    auto legacy = ChunkyFile::PcflCreate(&path, fcflWriteEnable);
    CHECK(legacy && legacy->FPutPv(nullptr, 0, type, number));
    CHECK(legacy->FSetName(type, number, &saved) && legacy->FSave(type));

    bool selected = false;
    MMChunkOwnerRead *reader = nullptr;
    auto cache = OpenSharedTypedCache(&path, false, selected, &reader);
    CHECK(selected && cache && reader == legacy->ReadOwner());
    CHECK(legacy->FSetName(type, number, &pending));
    ReleasePpo(&legacy);
    CHECK(ReadResourceName(cache, type, number, &name) && name.FEqual(&pending));
    mm_resource_cache_release(cache);

    reader = nullptr;
    CHECK(!OpenSharedTypedCache(&path, false, selected, &reader) && !selected && !reader);

    cache = OpenSourceResourceCache(&path, 4096, false);
    CHECK(cache && cache->wire_index && !mm_resource_cache_reader(cache) && cache->budget == 4096);
    CHECK(ReadResourceName(cache, type, number, &name) && name.FEqual(&saved));
    mm_resource_cache_release(cache);

    auto working = OpenWorkingResourceCache(&path, false, 128);
    CHECK(working && working->budget == 128);
    cache = OpenSharedTypedCache(&path, false, selected, &reader);
    CHECK(selected && cache && cache != working && reader == mm_resource_cache_reader(working));
    CHECK(ReadResourceName(cache, type, number, &name) && name.FEqual(&saved));
    byte wire[kcbMaxDataStn];
    const auto encoded = ChunkyFile::EncodeName(&pending, wire);
    CHECK(mm_chunk_owner_write_name(reader->storage, type, number, reader->max_name_bytes,
                                   encoded.wire, encoded.bytes) == MM_CHUNK_OK);
    mm_resource_cache_release(working);
    CHECK(ReadResourceName(cache, type, number, &name) && name.FEqual(&pending));
    mm_resource_cache_release(cache);

    cache = OpenSourceResourceCache(&path, 0, true);
    CHECK(cache && mm_resource_cache_reader(cache) && !cache->wire_index);
    CHECK(ReadResourceName(cache, type, number, &name) && name.FEqual(&saved));
    mm_resource_cache_release(cache);

    reader = nullptr;
    cache = OpenSharedTypedCache(&path, true, selected, &reader);
    CHECK(selected && cache && reader);
    CHECK(ReadResourceName(cache, type, number, &name) && name.FEqual(&saved));
    mm_resource_cache_release(cache);
    CHECK(path.FDelete());
    return 0;
}

static int EmbeddedForestCase(bool packed)
{
    constexpr MMU32 container = 0x464f5253UL, root = 0x524f4f54UL, leaf = 0x4c454146UL;
    std::array<byte, 48 + 512> wire = {
        1,0,3,0, 'T','O','O','R', 0,0,0,0, 0,0,0,0, 1,0,0,0, 0,0,0,0,
        1,0,3,0, 'F','A','E','L', 7,0,0,0, 0,2,0,0, 0,0,0,0, 0,0,0,0
    };
    std::memset(wire.data() + 48, 0x61, 512);
    Filename path;
    CHECK(path.FGetTemp());
    auto file = ChunkyFile::PcflCreate(&path, fcflWriteEnable);
    CHECK(file && file->FPutPv(wire.data(), wire.size(), container, 0));
    CHECK(file->FPutPv(wire.data(), wire.size() - 1, container, 1));
    file->SetForest(container, 0);
    file->SetForest(container, 1);
    if (packed)
        CHECK(file->FPackData(container, 0) && file->FPacked(container, 0));
    CHECK(file->FSave(container));
    ReleasePpo(&file);

    const auto source = OpenWorkingResourceCache(&path, false);
    CHECK(source && !OpenForestResourceCache(source, container, 1) && !OpenForestResourceCache(source, container, 2));
    const auto forest = OpenForestResourceCache(source, container, 0);
    mm_resource_cache_release(source);
    CHECK(forest && !forest->services.objects.state);
    MMChunkFileCopy binding;
    CHECK(!mm_resource_cache_live_copy_target(forest, &binding).identity);
    const auto graph = mm_resource_cache_chunks(forest);
    MMU32 number;
    CHECK(graph.count == 2 && mm_chunk_link_number(&graph, root, 0, 7, leaf, &number) == MM_CHUNK_OK);
    MMOwnedBlock block;
    MMBlockIO io;
    std::array<byte, 512> payload;
    CHECK(mm_owned_block_init(&block, forest->services.handles, forest->services.block_files) == MM_DATA_OK);
    CHECK(mm_resource_cache_read_block(forest, leaf, number, &block) == MM_CHUNK_OK && !block.view.packed);
    mm_resource_cache_release(forest);
    CHECK(mm_owned_block_io(&block, &io) == MM_DATA_OK && block.view.length == payload.size() &&
          mm_block_read(&io, 0, payload.data(), payload.size(), 0) == MM_DATA_OK &&
          !std::memcmp(payload.data(), wire.data() + 48, payload.size()));
    CHECK(mm_owned_block_clear(&block) == MM_DATA_OK && path.FDelete());
    return 0;
}

int main()
{
    if (const char *path = std::getenv("MM_RESOURCE_CACHE_SNAPSHOT"))
    {
        resourceSnapshot = std::fopen(path, "wb");
        CHECK(resourceSnapshot);
    }
    CHECK(CacheCases() == 0 && ManagerCases() == 0 && TypedManagerCases() == 0 && LargeResidentCase() == 0 &&
          WireSourceCase(false) == 0 && WireSourceCase(true) == 0 && SharedArchiveSourceCase() == 0 &&
          EmbeddedForestCase(false) == 0 && EmbeddedForestCase(true) == 0);
    if (resourceSnapshot)
        CHECK(std::fclose(resourceSnapshot) == 0);
    return 0;
}
