/* Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */
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
static void Check(bool condition)
{
    if (!condition)
        std::exit(1);
}
int main()
{
    HQ handle;
    Check(FAllocHq(&handle, 32, fmemClear, mprNormal));
    Check(reinterpret_cast<uintptr_t>(QvFromHq(handle)) % alignof(std::max_align_t) == 0);
    Check(FResizePhq(&handle, 128, fmemClear, mprNormal));
    Check(reinterpret_cast<uintptr_t>(QvFromHq(handle)) % alignof(std::max_align_t) == 0);
    FreePhq(&handle);
    void *memory;
    Check(FAllocPv(&memory, 32, fmemClear, mprNormal));
    Check(reinterpret_cast<uintptr_t>(memory) % alignof(std::max_align_t) == 0);
    FreePpv(&memory);
    LegacyUlong high, low;
    MulLu(0xffffffffu, 0xffffffffu, &high, &low);
    Check(high == 0xfffffffeu && low == 1);
    LegacyLong signedHigh;
    MulLw(0x7fffffff, 0x7fffffff, &signedHigh, &low);
    Check(signedHigh == 0x3fffffff && low == 1);
    MulLw(-0x7fffffff, 0x7fffffff, &signedHigh, &low);
    Check(signedHigh == -1073741824 && low == 0xffffffffu);
    LegacyLong remainder;
    Check(LwMulDivMod(0x7fffffff, 0x7fffffff, 0x7fffffff, &remainder) == 0x7fffffff && remainder == 0);
    Check(LwMulDivMod(-11, 7, 3, &remainder) == -25 && remainder == -2);
    Check(LwMulDiv(-11, 7, 3) == -25);
    Check(LuMulDiv(0xffffffffu, 0xffffffffu, 0xffffffffu) == 0xffffffffu);
    return 0;
}
