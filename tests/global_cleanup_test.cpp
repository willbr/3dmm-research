/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "kauai_core.h"
#include <cstdio>
#include <cstdlib>
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

namespace
{
// Like Studio's global font table, this consumer is constructed before the
// utility archive's globals on macOS and releases tracked memory after main.
struct GlobalAllocation
{
    PStringTable_GST strings = nullptr;
    void *bytes = nullptr;

    ~GlobalAllocation()
    {
        ReleasePpo(&strings);
        FreePpv(&bytes);
    }
} allocation;
} // namespace

int main()
{
    allocation.strings = StringTable_GST::PgstNew(0);
    if (!allocation.strings || !FAllocPv(&allocation.bytes, 64, fmemClear, mprNormal))
        return 1;
    // The test succeeds only if the process also finishes static destruction.
    return 0;
}
