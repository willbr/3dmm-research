#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <future>
#include <initializer_list>
#include <thread>
#include "kauai_core.h"
ASSERTNAME
static std::FILE *trace;
static uint32_t hash = 2166136261u;
static const char *expected;
static unsigned errors, lost;
static bool collect, markOnReport;
static void *markTarget;
struct Report { char source[32], message[256]; LegacyLong line, bytes; };
static Report reports[8];
static unsigned reportCount;
static void Check(bool value) { if (!value) std::exit(1); }
static void Word(uint32_t value)
{
    for (unsigned i = 0; i < 4; ++i) {
        const auto b = static_cast<unsigned char>(value >> (8 * i));
        hash = (hash ^ b) * 16777619u;
        if (trace) Check(std::fputc(b, trace) != EOF);
    }
}
static void Text(const char *p) { while (*p) Word(static_cast<byte>(*p++)); Word(0); }
static void Emit(const Report &r) { Word(1); Text(r.source); Text(r.message); Word(r.line); Word(r.bytes); }
void WarnProc(schar *, LegacyLong, schar *message) { std::fprintf(stderr, "%s\n", message); std::exit(1); }
bool FAssertProc(schar *file, LegacyLong line, schar *message, void *, LegacyLong bytes)
{
    if (expected && message && !std::strcmp(expected, message)) { ++errors; Word(2); Text(message); Word(bytes); return false; }
    if (message && !std::strncmp(message, "\nLost block: ", 13)) {
        ++lost; Report r{};
        Check(std::strlen(file) < sizeof(r.source) && std::strlen(message) < sizeof(r.message));
        std::strcpy(r.source, file); std::strcpy(r.message, message); r.line = line; r.bytes = bytes;
        if (collect) { Check(reportCount < 8); reports[reportCount++] = r; } else Emit(r);
        if (markOnReport) { markOnReport = false; MarkPv(markTarget); }
        return false;
    }
    std::fprintf(stderr, "%s:%" LEGACY_PRId ": %s\n", file, line, message); std::exit(1);
}
#ifdef DEBUG
struct alignas(std::max_align_t) HeaderProfile {
    LegacyLong total; char *source; LegacyLong line, thread;
    void *previous, *next; LegacyLong stack[5]; short marks, magic;
};
static byte *Raw(void *p) { return static_cast<byte *>(p) - sizeof(HeaderProfile); }
static short Marks(void *p) { short v; std::memcpy(&v, Raw(p) + offsetof(HeaderProfile, marks), sizeof(v)); return v; }
static void Describe(void *p, LegacyLong bytes)
{
    LegacyLong total; std::memcpy(&total, Raw(p), sizeof(total));
    Check(total == bytes + sizeof(HeaderProfile) + sizeof(short));
    Check(KauaiPlatform::AllocationSize(Raw(p)) == static_cast<size_t>(total));
    Word(total); Word(static_cast<unsigned short>(Marks(p)));
}
#endif
static void *Allocate(LegacyLong bytes, LegacyUlong flags, char *source, LegacyLong line)
{
    void *p = nullptr;
#ifdef DEBUG
    Check(FAllocPvDebug(&p, bytes, flags, mprNormal, source, line, &vdmglob.dmaglPv));
    Describe(p, bytes);
#else
    (void)source; (void)line; Check(FAllocPv(&p, bytes, flags, mprNormal));
#endif
    return p;
}
static void Cases()
{
#if defined(DEBUG) && defined(KAUAI_EXPENSIVE_CHECKS)
    Word(1);
#else
    Word(0);
#endif
    static char aSource[] = "block-a", bSource[] = "block-b", cSource[] = "block-c", workerSource[] = "worker";
    void *a = Allocate(8, fmemClear, aSource, 101), *b = Allocate(15, fmemNil, bSource, 102), *c = Allocate(0, fmemClear, cSource, 103);
    AssertUnmarkedMem(); UnmarkAllMem(); MarkPv(b); MarkPv(b); AssertUnmarkedMem();
    UnmarkAllMem(); markTarget = a; markOnReport = true; AssertUnmarkedMem(); markOnReport = false;
    UnmarkAllMem();
    const char *order[] = {cSource, bSource, aSource};
    for (auto size : {LegacyLong(1048576), LegacyLong(3), LegacyLong(0)}) {
        static LegacyLong before = 8;
        unsigned char oldAddress[sizeof(void *)]; std::memcpy(oldAddress, &a, sizeof(a));
        Check(_FResizePpvDebug(&a, size, before, fmemClear, mprNormal, &vdmglob.dmaglPv)); before = size;
        void *freshAddress = a; const bool moved = std::memcmp(oldAddress, &freshAddress, sizeof(a)) != 0;
#ifdef DEBUG
        if (moved) for (unsigned i = 0; i < 3; ++i) if (order[i] == aSource) {
            for (unsigned j = i; j; --j) order[j] = order[j - 1]; order[0] = aSource; break;
        }
        Describe(a, size); collect = true; reportCount = 0; AssertUnmarkedMem(); collect = false;
        Check(reportCount == 3);
        for (unsigned i = 0; i < 3; ++i) Check(!std::strcmp(reports[i].source, order[i]));
        // Physical realloc movement is allocator-dependent; compare normalized
        // reports after asserting the actual corresponding traversal order.
        for (auto source : {aSource, bSource, cSource}) for (unsigned i = 0; i < reportCount; ++i)
            if (!std::strcmp(source, reports[i].source)) Emit(reports[i]);
#else
        (void)moved;
#endif
    }
    expected = "wrong cb"; AssertPvAlloced(a, 1); expected = nullptr;
#ifdef DEBUG
    short marker; auto *magic = Raw(a) + offsetof(HeaderProfile, magic);
    std::memcpy(&marker, magic, sizeof(marker)); short wrong = 0; std::memcpy(magic, &wrong, sizeof(wrong));
    expected = "bad magic number"; AssertPvAlloced(a, 0); expected = nullptr; std::memcpy(magic, &marker, sizeof(marker));
    auto *tail = static_cast<byte *>(a); std::memcpy(&marker, tail, sizeof(marker)); std::memcpy(tail, &wrong, sizeof(wrong));
    expected = "bad tail magic number"; AssertPvAlloced(a, 0); expected = nullptr; std::memcpy(tail, &marker, sizeof(marker));
    const auto count = errors; SuspendCheckPointers();
    AssertPvAlloced(a, 1);
#ifdef KAUAI_EXPENSIVE_CHECKS
    AssertPvAlloced(nullptr, cvNil);
#endif
    MarkPv(a); ResumeCheckPointers(); Check(errors == count); Describe(a, 0);
#endif
    FreePpv(&b); FreePpv(&c); FreePpv(&a); Check(!a && !b && !c);
    std::promise<void *> ready; std::promise<void> resume; auto signal = resume.get_future();
    std::thread worker([&] {
        void *p = Allocate(4, fmemClear, workerSource, 104); MarkPv(p); ready.set_value(p); signal.wait();
#ifdef DEBUG
        Check(Marks(p) == 2);
#endif
        UnmarkAllMem(); AssertUnmarkedMem(); FreePpv(&p);
    });
    void *foreign = ready.get_future().get(); UnmarkAllMem(); MarkPv(foreign); AssertUnmarkedMem();
    resume.set_value(); worker.join(); Word(errors); Word(lost);
}
int main()
{
    const auto path = std::getenv("MM_MEMORY_GRAPH_SNAPSHOT"); trace = path ? std::fopen(path, "wb") : nullptr; Check(!path || trace);
    Cases(); if (trace) Check(!std::fclose(trace));
    std::printf("Memory graph fingerprint: %08X\n", hash); return 0;
}
