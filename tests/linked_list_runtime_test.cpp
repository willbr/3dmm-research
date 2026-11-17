#include "kauai_core.h"
#include <cstdio>
#include <cstdlib>
#include <initializer_list>
ASSERTNAME
static std::FILE *trace;
static unsigned hash = 2166136261u;
static void Check(bool value) { if (!value) std::exit(1); }
static void Word(unsigned value)
{
    for (unsigned i = 0; i < 4; ++i) {
        unsigned char b = static_cast<unsigned char>(value >> (i * 8));
        hash = (hash ^ b) * 16777619u;
        if (trace) Check(std::fputc(b, trace) != EOF);
    }
}
void WarnProc(schar *, LegacyLong, schar *message) { std::fprintf(stderr, "%s\n", message); std::exit(1); }
bool FAssertProc(schar *file, LegacyLong line, schar *message, void *, LegacyLong)
{
    std::fprintf(stderr, "%s:%" LEGACY_PRId ": %s\n", file, line, message); std::exit(1);
}
class Probe : public BaseLinkedList
{
public:
    unsigned id;
    explicit Probe(unsigned value) : id(value) { Word(1); Word(id); Check(!PbllNext()); }
    ~Probe() { Word(2); Word(id); Word(PbllNext() ? static_cast<Probe *>(PbllNext())->id : 0); }
    void Attach(MMLinkedListNode **head) { _Attach(head); }
};
static void List(MMLinkedListNode *node, std::initializer_list<unsigned> expected)
{
    auto head = node ? static_cast<PBaseLinkedList>(node->owner) : nullptr;
    Word(3); Word(static_cast<unsigned>((expected.size)()));
    for (auto id : expected) {
        Check(head && static_cast<Probe *>(head)->id == id);
        Debug(head->AssertValid(0););
        Word(id); head = head->PbllNext();
    }
    Check(!head);
}
int main()
{
    const auto path = std::getenv("MM_LINKED_LIST_SNAPSHOT");
    trace = path ? std::fopen(path, "wb") : nullptr; Check(!path || trace);
    MMLinkedListNode *a = nullptr, *b = nullptr;
    {
        Probe one(1), two(2), three(3), four(4);
        one.Attach(&a); two.Attach(&a); three.Attach(&a); List(a, {3, 2, 1});
        two.Attach(&b); List(a, {3, 1}); List(b, {2});
        one.Attach(&b); List(a, {3}); List(b, {1, 2});
        three.Attach(&b); List(a, {}); List(b, {3, 1, 2});
        one.Attach(&b); List(b, {1, 3, 2});
        one.Attach(&b); List(b, {1, 3, 2});
        two.Attach(nullptr); List(b, {1, 3}); Check(!two.PbllNext());
        two.Attach(nullptr); two.Attach(&a); four.Attach(&b);
        List(a, {2}); List(b, {4, 1, 3});
    }
    List(a, {}); List(b, {});
    auto *one = NewObj Probe(5); auto *two = NewObj Probe(6); auto *three = NewObj Probe(7);
    Check(one && two && three);
    one->Attach(&a); two->Attach(&a); three->Attach(&a); List(a, {7, 6, 5});
    two->AddRef(); two->Release(); List(a, {7, 6, 5});
    two->Release(); List(a, {7, 5}); one->Release(); List(a, {7}); three->Release(); List(a, {});
    // Exercise the real registry's C head through typed FileObject iteration.
    auto *original = FileObject::PfilFirst();
    auto *first = FileObject::PfilCreateTemp();
    auto *second = FileObject::PfilCreateTemp();
    auto *third = FileObject::PfilCreateTemp();
    Check(first && second && third);
    Check(FileObject::PfilFirst() == third && third->PfilNext() == second);
    Check(second->PfilNext() == first && first->PfilNext() == original);
    second->Release(); Check(third->PfilNext() == first);
    first->Release(); Check(third->PfilNext() == original);
    third->Release(); Check(FileObject::PfilFirst() == original);
    if (trace) Check(!std::fclose(trace));
    std::printf("Linked list runtime fingerprint: %08X\n", hash);
}
