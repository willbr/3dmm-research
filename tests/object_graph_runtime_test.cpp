#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <future>
#include <new>
#include <thread>
#include "kauai_core.h"
ASSERTNAME
static std::FILE *trace;
static uint32_t hash = 2166136261u;
static bool details;
static unsigned marks, summaries, reports;
static void Check(bool value) { if (!value) std::exit(1); }
static void Word(uint32_t value)
{
    for (unsigned i = 0; i < 4; ++i) {
        const auto b = static_cast<unsigned char>(value >> (i * 8));
        hash = (hash ^ b) * 16777619u;
        if (trace) Check(std::fputc(b, trace) != EOF);
    }
}
void WarnProc(schar *, LegacyLong, schar *message) { std::fprintf(stderr, "%s\n", message); std::exit(1); }
bool FAssertProc(schar *file, LegacyLong line, schar *message, void *, LegacyLong bytes)
{
    if (message && !std::strncmp(message, "Total lost objects: ", 20)) {
        ++summaries; Word(1); for (auto p = message; *p; ++p) Word(static_cast<byte>(*p)); Word(0); return details;
    }
    if (message && !std::strncmp(message, "\nLost object: ", 14)) {
        ++reports; Word(2); Word(line); Word(bytes);
        for (auto p = file; *p; ++p) Word(static_cast<byte>(*p)); Word(0);
        for (auto p = message; *p; ++p) Word(static_cast<byte>(*p)); Word(0); return false;
    }
    if (message && !std::strcmp(message, "Raw list is not empty!")) { Word(3); return false; }
    std::fprintf(stderr, "%s:%" LEGACY_PRId ": %s\n", file, line, message); std::exit(1);
}
class Probe : public BASE
{
public:
    unsigned id;
    Probe *child = nullptr;
    explicit Probe(unsigned value) : id(value) { Word(10); Word(id); Word(CactRef()); }
    ~Probe() { Word(11); Word(id); }
    LegacyLong Cls() override { return 0x50303030 + id; }
#ifdef DEBUG
    void MarkMem() override { BASE::MarkMem(); ++marks; Word(12); Word(id); MarkMemObj(child); }
#endif
};
class Thrower : public BASE { public: Thrower() { Word(13); throw 17; } };
static void *Raw(unsigned id)
{
#ifdef DEBUG
    static char sources[][8] = {"probe-0", "probe-1", "probe-2", "probe-3"};
    return BASE::operator new(sizeof(Probe), sources[id], 100 + id);
#else
    (void)id; return BASE::operator new(sizeof(Probe));
#endif
}
static void Cases()
{
    Word(sizeof(BASE)); Word(sizeof(Probe));
    void *one = Raw(1), *two = Raw(2), *three = Raw(3); Check(one && two && three);
    auto *b = ::new(two) Probe(2);
    UnmarkAllObjs(); details = true; AssertUnmarkedObjs();
    BASE::operator delete(three);
    auto *a = ::new(one) Probe(1); a->child = b;
    auto *c = NewObj Probe(3); Check(c);
    UnmarkAllObjs(); AssertUnmarkedObjs();
    Word(marks); Word(summaries); Word(reports);
    const auto savedMarks = marks;
    AssertUnmarkedObjs(); Check(marks == savedMarks);
    UnmarkAllObjs(); MarkMemObj(a); MarkMemObj(a); AssertUnmarkedObjs();
    details = false; UnmarkAllObjs(); AssertUnmarkedObjs();
    Probe local(4); MarkMemObj(&local); MarkMemObj(&local);
    b->child = a; UnmarkAllObjs(); MarkMemObj(a); AssertUnmarkedObjs(); b->child = nullptr;
    b->Release(); a->child = nullptr; a->Release(); c->Release();
    try { auto *unexpected = NewObj Thrower; (void)unexpected; Check(false); }
    catch (int value) { Check(value == 17); Word(value); }
    UnmarkAllObjs(); AssertUnmarkedObjs();
    std::promise<Probe *> ready;
    std::promise<void> resume;
    auto continueWorker = resume.get_future();
    std::thread worker([&] {
        auto *object = NewObj Probe(5); Check(object); MarkMemObj(object); ready.set_value(object);
        continueWorker.wait();
        auto count = marks; MarkMemObj(object); Check(marks == count);
        UnmarkAllObjs(); MarkMemObj(object); object->Release();
    });
    auto *foreign = ready.get_future().get();
    auto count = marks; UnmarkAllObjs(); MarkMemObj(foreign); AssertUnmarkedObjs(); Check(marks == count);
    resume.set_value(); worker.join();
    Word(marks); Word(summaries); Word(reports);
}
int main()
{
    const auto path = std::getenv("MM_OBJECT_GRAPH_SNAPSHOT"); trace = path ? std::fopen(path, "wb") : nullptr; Check(!path || trace);
    Cases(); if (trace) Check(!std::fclose(trace));
    std::printf("Object graph fingerprint: %08X\n", hash); return 0;
}
