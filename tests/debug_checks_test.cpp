/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "kauai_core.h"
#include <cstdio>
#include <cstdlib>
ASSERTNAME

static int assertions;
static bool expectingAssertion;

void WarnProc(schar *file, LegacyLong line, schar *message)
{
    std::fprintf(stderr, "%s:%" LEGACY_PRId ": %s\n", file, line, message);
}

bool FAssertProc(schar *file, LegacyLong line, schar *message, void *, LegacyLong)
{
    if (!expectingAssertion)
    {
        WarnProc(file, line, message);
        std::exit(1);
    }
    ++assertions;
    return false;
}

static void Check(bool condition)
{
    if (!condition)
        std::exit(1);
}

#ifdef DEBUG
class ValidationProbe : public BASE
{
  public:
    int validations = 0;

    void AssertValid(LegacyUlong)
    {
        ++validations;
    }

    void Validate()
    {
        AssertThis(fobjAssertFull);
    }
};
#endif

int main()
{
#ifdef DEBUG
    // Basic assertions and range/null preconditions remain enabled in both modes.
    expectingAssertion = true;
    Assert(false, "expected ordinary assertion");
    AssertIn(-1, 0, 2);
    ValidationProbe *missing = nullptr;
    AssertPo(missing, 0);
    AssertBasePo(missing, 0);
    Check(assertions == 4);
    expectingAssertion = false;

    ValidationProbe probe;
    const auto savedDepth = vcactAV;
    vcactAV = 2;
    AssertPo(&probe, fobjAssertFull);
    AssertNilOrPo(&probe, 0);
    probe.Validate();
#ifdef KAUAI_EXPENSIVE_CHECKS
    Check(probe.validations == 3);
#else
    Check(probe.validations == 0);
#endif
    Check(vcactAV == 2);
    vcactAV = savedDepth;
#endif

    void *memory = nullptr;
    Check(FAllocPv(&memory, 32, fmemClear, mprNormal));
#ifdef DEBUG
    const auto before = assertions;
    expectingAssertion = true;
    // A valid allocation with a deliberately wrong claimed size is safe to inspect.
    AssertPvAlloced(memory, 31);
    expectingAssertion = false;
#ifdef KAUAI_EXPENSIVE_CHECKS
    Check(assertions == before + 1);
#else
    Check(assertions == before);
#endif
#endif
    FreePpv(&memory);
    Check(memory == nullptr);

    HQ handle = hqNil;
    Check(FAllocHq(&handle, 32, fmemClear, mprNormal));
    Check(FResizePhq(&handle, 128, fmemClear, mprNormal));
    Check(CbOfHq(handle) == 128);
    Check(PvLockHq(handle) != nullptr);
    UnlockHq(handle);
    FreePhq(&handle);
    Check(handle == hqNil);
    return 0;
}
