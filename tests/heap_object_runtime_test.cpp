#include "kauai_core.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
#include <limits>
ASSERTNAME
static std::FILE *trace;
static uint32_t hash = 2166136261u;
static ErrorStack *observed;
static void Check(bool value) { if (!value) std::exit(1); }
static void Word(uint32_t value)
{
    for (unsigned i = 0; i < 4; ++i) {
        const auto byte = static_cast<unsigned char>(value >> (8 * i));
        hash = (hash ^ byte) * 16777619u;
        if (trace) Check(std::fputc(byte, trace) != EOF);
    }
}
void WarnProc(schar *, LegacyLong, schar *message)
{
    Word(!std::strncmp(message, "Error ", 6) ? 1 : 2);
    if (observed) { Word(observed->Cerc()); for (LegacyLong i = 0; i < observed->Cerc(); ++i) Word(observed->ErcGet(i)); }
}
bool FAssertProc(schar *file, LegacyLong line, schar *message, void *, LegacyLong)
{
    std::fprintf(stderr, "%s:%" LEGACY_PRId ": %s\n", file, line, message); std::exit(1);
}
static void RawHeapCases()
{
    using namespace KauaiPlatform;
    Word(sizeof(size_t)); Word(alignof(std::max_align_t));
    for (auto count : {size_t(0),size_t(1),size_t(7),size_t(31),size_t(128),size_t(511)}) {
        auto *p = static_cast<byte *>(Allocate(count, true)); Check(p != nullptr);
        Word(AllocationSize(p)); Word(reinterpret_cast<std::uintptr_t>(p) % alignof(std::max_align_t));
        for (size_t i = 0; i < count; ++i) { Check(!p[i]); Word(p[i]); p[i] = static_cast<byte>(i + 1); }
        Check(!Reallocate(p, (std::numeric_limits<size_t>::max)())); Check(AllocationSize(p) == count);
        for (size_t i = 0; i < count; ++i) Word(p[i]);
        p = static_cast<byte *>(Reallocate(p, count + 31)); Check(p && AllocationSize(p) == count + 31);
        for (size_t i = 0; i < count; ++i) Word(p[i]);
        p = static_cast<byte *>(Reallocate(p, 1)); Check(p && AllocationSize(p) == 1);
        if (count) { Check(p[0] == 1); Word(p[0]); }
        p = static_cast<byte *>(Reallocate(p, 0)); Check(p && AllocationSize(p) == 0); Free(p);
    }
    void *p = Reallocate(nullptr, 5); Check(p && AllocationSize(p) == 5); Free(p);
    Free(nullptr); Check(!AllocationSize(nullptr)); Check(!Allocate((std::numeric_limits<size_t>::max)()));
}
#ifdef DEBUG
static DebugMemoryAllocatorGlobals before;
static void Stats()
{
    const auto &s = vdmglob.dmaglBase;
    Word(s.cv-before.cv); Word(s.cvTot-before.cvTot); Word(s.cb-before.cb);
    Word(s.cvRun); Word(s.cbRun); Word(s.cactDo); Word(s.cactFail);
}
#endif
static unsigned destroyed;
class Object : public BASE
{
public:
    LegacyUlong values[4];
    Object() { for (unsigned i = 0; i < 4; ++i) values[i] = 0x12340000u + i; }
    ~Object() { ++destroyed; Word(4); Word(CactRef()); Debug(Stats();); }
};
static void ObjectCases(ErrorStack &errors)
{
    Word(sizeof(BASE)); Word(sizeof(Object));
#ifdef DEBUG
    before = vdmglob.dmaglBase; Check(!before.cactFail);
#endif
    for (auto extra : {size_t(0),size_t(7),size_t(33)}) {
        const size_t bytes = sizeof(BASE) + extra;
#ifdef DEBUG
        auto *raw = static_cast<byte *>(BASE::operator new(bytes, const_cast<char *>("fixture-source"), 60));
#else
        auto *raw = static_cast<byte *>(BASE::operator new(bytes));
#endif
        Check(raw);
        for (size_t i = 0; i < bytes; ++i) { Check(!raw[i]); Word(raw[i]); }
        Debug(Stats(););
        BASE::operator delete(raw); Debug(Stats(););
    }
    auto *object = NewObj Object;
    Check(object && object->CactRef() == 1); Debug(Stats(););
    for (auto value : object->values) Word(value);
    object->AddRef(); Word(object->CactRef()); object->Release(); Word(object->CactRef());
    object->Release(); Check(destroyed == 1); Debug(Stats(););
#ifdef DEBUG
    vdmglob.dmaglBase.cactDo = 0; vdmglob.dmaglBase.cactFail = 1;
    Check(!NewObj Object); Check(!vdmglob.dmaglBase.cactFail && destroyed == 1); Stats();
    Word(errors.Cerc()); Word(errors.ErcGet(0)); errors.Clear();
    vdmglob.dmaglBase.cactDo = 1; vdmglob.dmaglBase.cactFail = 1;
    object = NewObj Object; Check(object); Check(!NewObj Object); object->Release();
    Check(destroyed == 2 && !vdmglob.dmaglBase.cactFail); Stats(); errors.Clear();
    Check(vdmglob.dmaglBase.cv == before.cv && vdmglob.dmaglBase.cb == before.cb);
#else
    Check(!BASE::operator new((std::numeric_limits<size_t>::max)()));
    Word(errors.Cerc()); Word(errors.ErcGet(0)); errors.Clear();
#endif
}
int main()
{
    const auto path = std::getenv("MM_HEAP_OBJECT_SNAPSHOT");
    trace = path ? std::fopen(path, "wb") : nullptr; Check(!path || trace);
    ErrorStack errors; auto *previous = vpers; vpers = &errors; observed = &errors;
    RawHeapCases(); ObjectCases(errors);
    vpers = previous; observed = nullptr;
    if (trace) Check(std::fclose(trace) == 0);
    std::printf("Raw heap/object fingerprint: %08X\n", hash);
    return 0;
}
