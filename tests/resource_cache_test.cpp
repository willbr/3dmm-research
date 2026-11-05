/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include <array>
#include <cstdio>
#include <cstdlib>
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

int main()
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
}
