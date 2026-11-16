#include "kauai_core.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
ASSERTNAME
static std::FILE *trace;
static uint32_t hash = 2166136261u;
static ErrorStack *observed;
static void **ownerSlot;
static void *savedOwner;
static bool badSizeExpected;
static unsigned assertionCount;
static void Check(bool value) { if (!value) std::exit(1); }
static void Word(uint32_t value)
{
    for (unsigned i = 0; i < 4; ++i) {
        const auto byte = static_cast<unsigned char>(value >> (i * 8));
        hash = (hash ^ byte) * 16777619u;
        if (trace) Check(std::fputc(byte, trace) != EOF);
    }
}
static void State()
{
    Word(observed->Cerc());
    for (LegacyLong i = 0; i < observed->Cerc(); ++i) Word(observed->ErcGet(i));
}
void WarnProc(schar *, LegacyLong, schar *message)
{
    Word(!std::strncmp(message, "Error ", 6) ? 1 : 2);
    if (!std::strncmp(message, "Error ", 6)) Word(static_cast<uint32_t>(std::strtol(message + 6, nullptr, 10)));
    if (observed) State();
}
bool FAssertProc(schar *file, LegacyLong line, schar *message, void *, LegacyLong)
{
    if (badSizeExpected) { ++assertionCount; return false; }
    std::fprintf(stderr, "%s:%" LEGACY_PRId ": %s\n", file, line, message);
    std::exit(1);
}
class ObservedErrors : public ErrorStack
{
public:
#ifdef DEBUG
    void Push(LegacyLong code, schar *file, LegacyLong line) override
#else
    void Push(LegacyLong code) override
#endif
    {
        Word(3); Word(code);
        if (ownerSlot) Word(!*ownerSlot ? 0 : *ownerSlot == savedOwner ? 1 : 2);
        State();
#ifdef DEBUG
        ErrorStack::Push(code, file, line);
#else
        ErrorStack::Push(code);
#endif
    }
};
static void Push(ObservedErrors &errors, LegacyLong code, LegacyLong line = 10)
{
#ifdef DEBUG
    errors.Push(code, const_cast<char *>("fixture-source"), line);
#else
    (void)line; errors.Push(code);
#endif
    State();
}
static void ErrorCases(ObservedErrors &errors)
{
    Word(sizeof(ErrorStack));
    LegacyLong output = 7654321;
    Check(!errors.FPop(&output));
#ifdef DEBUG
    Check(static_cast<LegacyUlong>(output) == 0xa3a3a3a3u);
#else
    Check(output == 7654321);
#endif
    Word(static_cast<uint32_t>(output));
    Check(!errors.FPop());
    for (LegacyLong i = 0; i < 27; ++i) {
        Push(errors, i % 7 - 3, i + 10);
        Push(errors, i % 7 - 3, i + 100);
        for (LegacyLong j = -1; j <= errors.Cerc(); ++j) Word(errors.ErcGet(j));
        for (LegacyLong j = -4; j <= 4; ++j) Word(errors.FIn(j));
    }
    for (auto code : {LegacyLong(-2), LegacyLong(200), LegacyLong(0), LegacyLong(3)}) {
        errors.Flush(code); State();
    }
    while (errors.Cerc()) { Check(errors.FPop(&output)); Word(output); State(); }
    Push(errors, 123); Check(errors.FPop()); Check(errors.Cerc() == 0);
    Push(errors, -2147483647L - 1); Push(errors, 2147483647L); errors.Clear(); State();
}
#ifdef DEBUG
using Stats = DebugMemoryAllocatorGlobals;
static void StatsState(const Stats &s)
{
    Word(s.cv); Word(s.cvTot); Word(s.cvRun); Word(s.cb); Word(s.cbRun); Word(s.cactDo); Word(s.cactFail);
}
static void StatsCases()
{
    Stats s = {}; Word(sizeof(s));
    for (LegacyLong skip = -1; skip < 4; ++skip) {
        s = {}; s.cactDo = skip; s.cactFail = 3;
        for (int i = 0; i < 8; ++i) { Word(s.FFail()); StatsState(s); }
    }
    s = {};
    s.Allocate(12); StatsState(s); s.Allocate(0); StatsState(s);
    s.Resize(20); StatsState(s); s.Resize(-15); StatsState(s);
    s.Free(17); StatsState(s); s.Free(0); StatsState(s);
}
#else
struct Stats {};
#endif
static bool Allocate(void **out, LegacyLong bytes, LegacyUlong flags, Stats *stats)
{
#ifdef DEBUG
    return FAllocPvDebug(out, bytes, flags, mprNormal, const_cast<char *>("fixture-source"), 55, stats);
#else
    (void)stats; return FAllocPv(out, bytes, flags, mprNormal);
#endif
}
static bool Resize(void **out, LegacyLong next, LegacyLong before, LegacyUlong flags, Stats *stats)
{
#ifdef DEBUG
    return _FResizePpvDebug(out, next, before, flags, mprForSpeed, stats);
#else
    (void)stats; return _FResizePpv(out, next, before, flags, mprForSpeed);
#endif
}
static void Free(void **out, Stats *stats)
{
#ifdef DEBUG
    FreePpvDebug(out, stats);
#else
    (void)stats; FreePpv(out);
#endif
}
static void Bytes(void *memory, LegacyLong count)
{
    Word(count);
    for (LegacyLong i = 0; i < count; ++i) Word(static_cast<byte *>(memory)[i]);
}
static void MemoryCases(ObservedErrors &errors)
{
    Stats stats = {};
    for (auto count : {LegacyLong(0), LegacyLong(1), LegacyLong(7), LegacyLong(32), LegacyLong(255)}) {
        void *memory = nullptr;
        Check(Allocate(&memory, count, fmemClear, &stats) && memory);
        Bytes(memory, count);
        for (LegacyLong i = 0; i < count; ++i) static_cast<byte *>(memory)[i] = static_cast<byte>(i + 1);
        Check(Resize(&memory, count + 19, count, fmemClear, &stats)); Bytes(memory, count + 19);
        Check(Resize(&memory, 3, count + 19, fmemNil, &stats)); Bytes(memory, 3);
        Check(Resize(&memory, 0, 3, fmemNil, &stats) && memory); Free(&memory, &stats); Check(!memory);
        Free(&memory, &stats);
#ifdef DEBUG
        StatsState(stats);
#endif
    }
    void *memory = nullptr;
    Check(Allocate(&memory, 8, fmemClear, &stats));
    std::memset(memory, 0x37, 8); savedOwner = memory; ownerSlot = &memory;
#ifdef DEBUG
    stats.cactDo = 0; stats.cactFail = 1;
    Check(!Resize(&memory, 16, 8, fmemClear, &stats) && memory == savedOwner);
    Bytes(memory, 8); StatsState(stats);
    stats.cactFail = 1;
    Check(Resize(&memory, 4, 8, fmemClear, &stats) && stats.cactFail == 1);
    savedOwner = memory;
    Check(Resize(&memory, 4, 4, fmemNil, &stats) && stats.cactFail == 1);
    savedOwner = memory;
    stats.cactFail = 0;
    const auto bytes = LegacyLong(4);
#else
    const auto bytes = LegacyLong(8);
#endif
    badSizeExpected = true;
    Check(!Resize(&memory, kcbMax + 1, bytes, fmemClear, &stats) && memory == savedOwner);
    badSizeExpected = false;
    Bytes(memory, bytes); Free(&memory, &stats);
    memory = reinterpret_cast<void *>(1); savedOwner = memory;
    badSizeExpected = true;
    Check(!Allocate(&memory, kcbMax + 1, fmemClear, &stats) && !memory);
    badSizeExpected = false;
    Word(assertionCount); errors.Clear(); ownerSlot = nullptr;
#ifdef DEBUG
    stats.cactFail = 1; memory = reinterpret_cast<void *>(1);
    Check(!Allocate(&memory, 5, fmemNil, &stats) && !memory); StatsState(stats); errors.Clear();
    Check(Allocate(&memory, 9, fmemNil, &stats)); Bytes(memory, 9);
    Check(Resize(&memory, 15, 9, fmemNil, &stats)); Bytes(memory, 15); Free(&memory, &stats); StatsState(stats);
#endif
}
int main()
{
    const auto path = std::getenv("MM_MEMORY_RUNTIME_SNAPSHOT");
    trace = path ? std::fopen(path, "wb") : nullptr; Check(!path || trace);
    ObservedErrors errors; observed = &errors;
    auto *old = vpers; vpers = &errors;
    ErrorCases(errors);
#ifdef DEBUG
    StatsCases();
#endif
    MemoryCases(errors); vpers = old; observed = nullptr;
    if (trace) Check(std::fclose(trace) == 0);
    std::printf("Memory/error runtime fingerprint: %08X\n", hash);
    return 0;
}
