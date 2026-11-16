#include "kauai_core.h"
#include "handle_services.h"
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
ASSERTNAME
static std::FILE *trace;
static uint32_t hash = 2166136261u;
static const char *expected;
static unsigned assertions;
static ErrorStack *observed;
static void **owner;
static void *saved;
static void Check(bool value) { if (!value) std::exit(1); }
static void Word(uint32_t value)
{
    for (unsigned i = 0; i < 4; ++i) {
        const auto b = static_cast<unsigned char>(value >> (8 * i)); hash = (hash ^ b) * 16777619u;
        if (trace) Check(std::fputc(b, trace) != EOF);
    }
}
static void Text(const char *p) { while (*p) Word(static_cast<byte>(*p++)); Word(0); }
static void Owner() { if (owner) Word(!*owner ? 0 : *owner == saved ? 1 : 2); else Word(3); }
static void Errors() { Word(observed->Cerc()); for (LegacyLong i = 0; i < observed->Cerc(); ++i) Word(observed->ErcGet(i)); }
void WarnProc(schar *, LegacyLong, schar *message) { Word(1); Text(message); Owner(); Errors(); }
bool FAssertProc(schar *file, LegacyLong line, schar *message, void *, LegacyLong bytes)
{
    if (expected && message && !std::strcmp(expected, message)) {
        ++assertions; Word(2); Text(message); Word(bytes); Owner(); return false;
    }
    std::fprintf(stderr, "%s:%" LEGACY_PRId ": %s\n", file, line, message); std::exit(1);
}
class ObservedErrors : public ErrorStack {
public:
#ifdef DEBUG
    void Push(LegacyLong code, schar *file, LegacyLong line) override
#else
    void Push(LegacyLong code) override
#endif
    {
        Word(3); Word(code); Owner(); Errors();
#ifdef DEBUG
        ErrorStack::Push(code, file, line);
#else
        ErrorStack::Push(code);
#endif
    }
};
struct alignas(std::max_align_t) HeaderProfile { LegacyLong bytes, locks; Debug(LegacyLong magic;) };
static byte *Raw(HQ h) { return static_cast<byte *>(h) - sizeof(HeaderProfile); }
static void Describe(HQ h)
{
    HeaderProfile header; std::memcpy(&header, Raw(h), sizeof(header));
    Check(header.bytes == CbOfHq(h)); Word(header.bytes); Word(header.locks); Debug(Word(header.magic););
    auto *p = static_cast<byte *>(QvFromHq(h)); Check(p == h);
    for (LegacyLong i = 0; i < header.bytes; ++i) Word(p[i]);
    AssertPvAlloced(Raw(h), header.bytes + sizeof(HeaderProfile));
}
#ifdef DEBUG
static DebugMemoryAllocatorGlobals before;
static void Stats()
{
    const auto &s = vdmglob.dmaglHq;
    Word(s.cv - before.cv); Word(s.cvTot - before.cvTot); Word(s.cb - before.cb);
    Word(s.cvRun); Word(s.cbRun); Word(s.cactDo); Word(s.cactFail);
}
#endif
static void Cases()
{
#if defined(DEBUG) && defined(KAUAI_EXPENSIVE_CHECKS)
    Word(1);
#else
    Word(0);
#endif
    Word(sizeof(HeaderProfile));
    Debug(before = vdmglob.dmaglHq;);
    for (auto bytes : {LegacyLong(0), LegacyLong(1), LegacyLong(7), LegacyLong(31), LegacyLong(128)}) {
        HQ h = nullptr; Check(FAllocHq(&h, bytes, fmemClear, mprCritical)); Describe(h); Debug(Stats(););
        auto *p = static_cast<byte *>(PvLockHq(h)); Check(p == h); Check(PvLockHq(h) == p);
        for (LegacyLong i = 0; i < bytes; ++i) { Check(!p[i]); p[i] = static_cast<byte>(i + 1); }
        saved = h; owner = &h; expected = "Resizing locked HQ";
        Check(!FResizePhq(&h, bytes + 1, fmemClear, mprForSpeed) && h == saved); expected = nullptr; Errors(); observed->Clear();
        UnlockHq(h); Describe(h); UnlockHq(h);
        expected = "hq not locked"; UnlockHq(h); expected = nullptr;
        HQ copy = nullptr; Check(FCopyHq(h, &copy, mprNormal)); Describe(copy); FreePhq(&copy);
        Check(FResizePhq(&h, bytes + 17, fmemClear, mprNormal)); Describe(h);
        Check(FResizePhq(&h, 3, fmemNil, mprNormal)); Describe(h);
        Check(FResizePhq(&h, 0, fmemNil, mprNormal) && h); Describe(h); FreePhq(&h); Check(!h); FreePhq(&h); Debug(Stats();); owner = nullptr;
    }
#ifdef DEBUG
    HQ h = reinterpret_cast<void *>(1); owner = &h; saved = h; vdmglob.dmaglHq.cactFail = 1;
    Check(!FAllocHq(&h, 5, fmemNil, mprNormal) && !h); Errors(); observed->Clear(); Stats();
    Check(FAllocHq(&h, 5, fmemClear, mprNormal)); saved = h; vdmglob.dmaglHq.cactFail = 1;
    Check(!FResizePhq(&h, 12, fmemClear, mprNormal) && h == saved); Errors(); observed->Clear(); Describe(h);
    vdmglob.dmaglHq.cactFail = 1; Check(FResizePhq(&h, 2, fmemNil, mprNormal)); Check(vdmglob.dmaglHq.cactFail == 1);
    saved = h; Check(!FResizePhq(&h, 12, fmemNil, mprNormal) && h == saved); observed->Clear();
    HQ copy = reinterpret_cast<void *>(1); owner = &copy; saved = copy; vdmglob.dmaglHq.cactFail = 1;
    Check(!FCopyHq(h, &copy, mprNormal) && !copy); Errors(); observed->Clear(); owner = &h; saved = h;
    LegacyLong magic; auto *field = Raw(h) + offsetof(HeaderProfile, magic);
    std::memcpy(&magic, field, sizeof(magic)); LegacyLong bad = 0; std::memcpy(field, &bad, sizeof(bad));
    expected = "beginning of hq block is trashed"; AssertHq(h); SuspendCheckPointers(); AssertHq(h); ResumeCheckPointers(); expected = nullptr;
    std::memcpy(field, &magic, sizeof(magic)); Describe(h);
    expected = "hq is nil"; AssertHq(nullptr); expected = nullptr;
    MarkHq(h); MarkHq(nullptr); PvLockHq(h); expected = "Freeing locked HQ"; FreePhq(&h); expected = nullptr; Check(!h); owner = nullptr;
    Stats(); Check(vdmglob.dmaglHq.cv == before.cv && vdmglob.dmaglHq.cb == before.cb);
#endif
    const auto *services = KauaiHandleServices(); void *handle = nullptr;
    Check(services->allocate(services->context, 6, &handle)); Check((services->size)(services->context, handle) == 6);
    Check(services->lock(services->context, handle) == handle); services->unlock(services->context, handle);
    Check(services->resize(services->context, &handle, 20)); Check((services->size)(services->context, handle) == 20);
    services->release(services->context, handle); Word(assertions); Errors();
}
int main()
{
    const auto path = std::getenv("MM_HANDLE_RUNTIME_SNAPSHOT"); trace = path ? std::fopen(path, "wb") : nullptr; Check(!path || trace);
    ObservedErrors errors; observed = &errors; auto *previous = vpers; vpers = &errors;
    Cases(); vpers = previous; observed = nullptr; if (trace) Check(!std::fclose(trace));
    std::printf("Handle runtime fingerprint: %08X\n", hash); return 0;
}
