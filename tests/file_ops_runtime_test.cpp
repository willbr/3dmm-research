#include "kauai_core.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
ASSERTNAME
static std::FILE *trace;
static unsigned hash = 2166136261u;
static const char *expected;
static Filename *names;
static bool observe;
static void CheckAt(bool value, int line)
{ if (!value) { std::fprintf(stderr, "File operations check failed at line %d\n", line); std::exit(1); } }
#define Check(value) CheckAt(!!(value), __LINE__)
static void Word(unsigned value)
{
    for (unsigned i = 0; i < 4; ++i) {
        unsigned char b = static_cast<unsigned char>(value >> (i * 8));
        hash = (hash ^ b) * 16777619u;
        if (trace) Check(std::fputc(b, trace) != EOF);
    }
}
static void Text(const char *p) { while (*p) Word(static_cast<byte>(*p++)); Word(0); }
static unsigned NameId(Filename *name)
{
    for (unsigned i = 0; i < 4; ++i) if (name->FEqual(&names[i])) return i + 1;
    return 0;
}
static void Registry()
{
    unsigned count = 0;
    for (auto *p = FileObject::PfilFirst(); p; p = p->PfilNext()) {
        Filename name; p->GetFni(&name);
        Word(1); Word(NameId(&name)); Word(p->GrffilCur()); Word(p->ElError()); Word(p->CactRef()); ++count;
    }
    Word(0); Word(count);
}
void WarnProc(schar *, LegacyLong, schar *message) { Word(90); Text(message); }
bool FAssertProc(schar *file, LegacyLong line, schar *message, void *, LegacyLong)
{
    if (expected && message && !std::strcmp(expected, message)) { Word(91); Text(message); return false; }
    std::fprintf(stderr, "%s:%" LEGACY_PRId ": %s\n", file, line, message); std::exit(1);
}
class Errors : public ErrorStack
{
public:
#ifdef DEBUG
    void Push(LegacyLong code, schar *file, LegacyLong line) override
#else
    void Push(LegacyLong code) override
#endif
    {
        Word(92); Word(code); if (observe) Registry();
#ifdef DEBUG
        ErrorStack::Push(code, file, line);
#else
        ErrorStack::Push(code);
#endif
    }
};
static void Content(PFileObject file, const char *text)
{
    char bytes[5] = {}; Check(file->FReadRgb(bytes, 4, 0) && !std::memcmp(bytes, text, 4));
    Text(bytes); Word(file->FpMac());
}
static void Name(PFileObject file, unsigned id)
{
    Filename name; file->GetFni(&name); Check(NameId(&name) == id); Word(id);
}
int main()
{
    const auto snapshot = std::getenv("MM_FILE_OPS_SNAPSHOT");
    trace = snapshot ? std::fopen(snapshot, "wb") : nullptr; Check(!snapshot || trace);
    Errors errors; auto *previous = vpers; vpers = &errors;
    Filename n[4]; names = n; Check(!FileObject::PfilFirst());
    Check(n[0].FGetTemp());
    for (int i = 1; i < 4; ++i) {
        n[i] = n[0]; bool distinct = false;
        // FGetUnique checks disk, but earlier fixture names are not reserved.
        // Collision retries and advancing ticks can revisit one of those names.
        for (unsigned attempt = 0; attempt < 20 && !distinct; ++attempt) {
            Check(n[i].FGetUnique(n[0].Ftg())); distinct = true;
            for (int j = 0; j < i; ++j) if (n[i].FEqual(&n[j])) { distinct = false; break; }
        }
        Check(distinct);
    }
    observe = true;
    Check(!FileObject::PfilOpen(&n[3])); Registry();
    auto *a = FileObject::PfilCreate(&n[0]), *b = FileObject::PfilCreate(&n[1]); Check(a && b);
    Check(a->FWriteRgb((void *)"AAAA", 4, 0) && b->FWriteRgb((void *)"BBBB", 4, 0)); Registry();
    auto *again = FileObject::PfilOpen(&n[0]); Check(again == a && a->CactRef() == 2); Registry(); again->Release();
    expected = "trying to create an open file"; Check(!FileObject::PfilCreate(&n[0])); expected = nullptr;
    expected = "Why are you calling FSwapNames on the same file?"; Check(a->FSwapNames(a)); expected = nullptr;
    Check(!a->FRename(&n[1])); Name(a, 1); Content(a, "AAAA"); Registry();
    Check(a->FSwapNames(b)); Name(a, 2); Name(b, 1); Content(a, "AAAA"); Content(b, "BBBB"); Registry();
    Check(a->FSetFni(&n[1])); Check(a->FSetFni(&n[0]));
    Name(a, 1); Name(b, 2); Check(b->FTemp()); Content(a, "AAAA"); Content(b, "BBBB"); Registry();
    Check(a->FRename(&n[2])); Name(a, 3);
    auto *closed = FileObject::PfilCreate(&n[0]); Check(closed && closed->FWriteRgb((void *)"CCCC", 4, 0)); closed->Release();
    Check(a->FSetFni(&n[0])); Name(a, 1); Content(a, "AAAA"); Registry();
    b->Release(); Check(n[1].TExists() == tNo); a->SetTemp(); a->Release(); Check(n[0].TExists() == tNo); Registry();
    for (auto *basis : {&n[0], static_cast<Filename *>(nullptr)}) {
        auto *temp = FileObject::PfilCreateTemp(basis); Check(temp && temp->FTemp());
        Filename name; temp->GetFni(&name); Check(name.TExists() == tYes);
        Check(!basis || (name.FSameDir(basis) && name.Ftg() == basis->Ftg()));
        Check(FileObject::PfilFromFni(&name) == temp); Registry(); temp->Release(); Check(name.TExists() == tNo);
    }
    a = FileObject::PfilCreate(&n[0]); Check(a && a->FWriteRgb((void *)"DDDD", 4, 0));
    FileObject::ClearMarks(); FileObject::CloseUnmarked(); Registry();
    FileObject::ShutDown(); Check(n[0].TExists() == tYes && FileObject::PfilFromFni(&n[0]) == a);
    Content(a, "DDDD"); a->Release(); Check(n[0].FDelete()); Registry();
#ifdef DEBUG
    for (int create = 0; create < 2; ++create) {
        vdmglob.dmaglBase.cactDo = 0; vdmglob.dmaglBase.cactFail = 1;
        Check(!(create ? FileObject::PfilCreate(&n[3]) : FileObject::PfilOpen(&n[3])));
        Check(!vdmglob.dmaglBase.cactFail); Registry();
    }
#endif
    observe = false; Word(errors.Cerc()); for (LegacyLong i = 0; i < errors.Cerc(); ++i) Word(errors.ErcGet(i));
    vpers = previous; if (trace) Check(!std::fclose(trace));
    std::printf("File operations runtime fingerprint: %08X\n", hash);
}
