/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "kauai_core.h"
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
    static bool Read(PChunkyResourceFile, ChunkTagOrType, ChunkNumber, PDataBlock, PBaseCacheableObject *result,
                     LegacyLong *bytes)
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
        Observe(3, _id, Ctg(), Cno(), static_cast<uint32_t>(Crep()), CactRef(), Pcrf() != nullptr);
        --probeResident;
        if (_id == 301 && cascade)
        {
            auto other = cascade;
            cascade = nullptr;
            ReleasePpo(&other);
        }
    }
    static bool Read(PChunkyResourceFile, ChunkTagOrType type, ChunkNumber number, PDataBlock block,
                     PBaseCacheableObject *result, LegacyLong *bytes, int reader)
    {
        int32_t record[5];
        if (!block->FReadRgb(record, sizeof(record), 0))
            return false;
        *bytes = record[result ? 2 : 1];
        Observe(result ? 1 : 0, record[0] * 10 + reader, type, number, *bytes, 0, 0);
        if (record[result ? 4 : 3])
            return false;
        if (result)
            *result = NewObj ProbeResource(record[0] * 10 + reader);
        return !result || *result != nullptr;
    }
    static bool A(PChunkyResourceFile c, ChunkTagOrType t, ChunkNumber n, PDataBlock b, PBaseCacheableObject *o,
                  LegacyLong *s)
    {
        return Read(c, t, n, b, o, s, 1);
    }
    static bool B(PChunkyResourceFile c, ChunkTagOrType t, ChunkNumber n, PDataBlock b, PBaseCacheableObject *o,
                  LegacyLong *s)
    {
        return Read(c, t, n, b, o, s, 2);
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
    CHECK(held->Pcrf() == nullptr && held->CactRef() == 1 && probeResident == 1);
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
    CHECK(manager->PcrfFindChunk('RCHE', 1, 11) == manager->PcrfGet(0) &&
          manager->PcrfFindChunk('RCHE', 1, 22) == manager->PcrfGet(1));
    bool error = true;
    auto first = manager->PbacoFetch('RCHE', 1, ProbeResource::A, &error);
    CHECK(first && !error && first->Pcrf() == manager->PcrfGet(0));
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
    CHECK(first->Pcrf() == nullptr && second->Pcrf() == nullptr && probeResident == 2);
    ReleasePpo(&first);
    ReleasePpo(&second);
    CHECK(!probeResident && a->CactRef() == 1 && b->CactRef() == 1);
    ReleasePpo(&a);
    ReleasePpo(&b);
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

int main()
{
    if (const char *path = std::getenv("THREE_MM_RESOURCE_CACHE_SNAPSHOT"))
    {
        resourceSnapshot = std::fopen(path, "wb");
        CHECK(resourceSnapshot);
    }
    CHECK(CacheCases() == 0 && ManagerCases() == 0 && LargeResidentCase() == 0);
    if (resourceSnapshot)
        CHECK(std::fclose(resourceSnapshot) == 0);
    return 0;
}