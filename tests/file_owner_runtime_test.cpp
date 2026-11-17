#include "kauai_core.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
ASSERTNAME
static std::FILE *trace;
static unsigned hash = 2166136261u;
static const char *expected;
static void Check(bool value) { if (!value) std::exit(1); }
static void Word(unsigned value)
{
    for (unsigned i = 0; i < 4; ++i) {
        unsigned char b = static_cast<unsigned char>(value >> (i * 8));
        hash = (hash ^ b) * 16777619u;
        if (trace) Check(std::fputc(b, trace) != EOF);
    }
}
static void Text(const char *p) { while (*p) Word(static_cast<byte>(*p++)); Word(0); }
void WarnProc(schar *, LegacyLong, schar *message) { Word(90); Text(message); }
bool FAssertProc(schar *file, LegacyLong line, schar *message, void *, LegacyLong)
{
    if (expected && message && !std::strcmp(expected, message)) { Word(91); Text(message); return false; }
    std::fprintf(stderr, "%s:%" LEGACY_PRId ": %s\n", file, line, message); std::exit(1);
}
class Probe : public FileObject
{
public:
    Probe(Filename *name, LegacyUlong flags) : FileObject(name, flags) {}
    bool Open(bool create, LegacyUlong flags) { return _FOpen(create, flags); }
    void Close() { _Close(); }
    void State() { Word(_file.flags); Word(_file.error); Word(_file.open); Word(_file.ever_open); Word(_file.wrote); Word(_file.handle != nullptr); }
    void Unmark() { _file.flags &= ~ffilMark; }
};
static Probe *observed;
class Errors : public ErrorStack
{
public:
#ifdef DEBUG
    void Push(LegacyLong code, schar *file, LegacyLong line) override
#else
    void Push(LegacyLong code) override
#endif
    {
        Word(92); Word(code); if (observed) observed->State();
#ifdef DEBUG
        ErrorStack::Push(code, file, line);
#else
        ErrorStack::Push(code);
#endif
    }
};
int main()
{
    const auto snapshot = std::getenv("MM_FILE_OWNER_SNAPSHOT");
    trace = snapshot ? std::fopen(snapshot, "wb") : nullptr; Check(!snapshot || trace);
    Errors errors; auto *savedErrors = vpers; vpers = &errors;
    Filename name; Check(name.FGetTemp());
    auto *file = NewObj Probe(&name, ffilTemp | ffilWriteEnable | ffilDenyWrite); Check(file); observed = file;
    file->State(); Check(file->Open(true, file->GrffilCur())); file->State();
    Check(file->FWriteRgb((void *)"abcdef", 6, 0)); file->State(); Word(file->FpMac());
    char bytes[8] = {}; Check(file->FReadRgb(bytes, 6, 0) && !std::memcmp(bytes, "abcdef", 6));
    Text(bytes); Check(file->FSetFpMac(4)); Word(file->FpMac()); Check(file->FSetFpMac(8)); Word(file->FpMac());
    file->SetEl(kelWrite); Check(!file->FWriteRgb(bytes, 1, 0)); file->State();
    Check(file->FReadRgb(bytes, 4, 0) && !std::memcmp(bytes, "abcd", 4)); file->State();
    file->SetEl(kelRead); Check(!file->FReadRgb(bytes, 1, 0)); Word(file->FpMac()); file->State();
    file->SetEl(kelSeek); Check(!file->FWriteRgb(bytes, 1, 0)); Word(file->FpMac()); file->State();
    file->SetEl(kelCritical); Check(file->FReadRgb(bytes, 0, 0) && file->FWriteRgb(bytes, 0, 0));
    file->State(); file->SetEl();
    expected = "read past EOF"; Check(!file->FReadRgb(bytes, 1, 8)); expected = nullptr; file->State();
    file->SetEl(); file->Flush(); file->Close(); file->State();
    Check(file->FReadRgb(bytes, 4, 0)); file->State();
    Check(file->FSetGrffil(ffilMark)); Check(file->FSetGrffil(0, ffilMark)); file->State();
    Check(file->GrffilCur() & ffilMark); file->Unmark();
    file->SetTemp(false); Check(!file->FTemp()); file->State(); file->SetTemp(); file->State();
    observed = nullptr; file->Release(); Check(name.TExists() == tNo); Word(93);
    // Real sharing rejection must restore the old open handle and permissions.
    Check(name.FGetTemp()); file = NewObj Probe(&name, ffilWriteEnable | ffilDenyWrite); Check(file);
    observed = file; Check(file->Open(true, file->GrffilCur()));
    Check(file->FWriteRgb((void *)"1234", 4, 0)); file->State(); observed = nullptr; file->Release();
    file = NewObj Probe(&name, ffilDenyWrite); Check(file); observed = file;
    Check(file->Open(false, file->GrffilCur())); String path; name.GetStnPath(&path);
    auto blocker = KauaiFiles::Open(path.Psz(), KauaiFiles::OpenOptions{}); Check(blocker);
    Check(!file->FSetGrffil(ffilWriteEnable)); file->State(); Check(file->ElError() == elNil);
    Check(file->FReadRgb(bytes, 4, 0) && !std::memcmp(bytes, "1234", 4));
    KauaiFiles::Close(blocker); Check(file->FSetGrffil(ffilWriteEnable)); file->State();
    Check(file->FWriteRgb((void *)"XY", 2, 1)); file->State();
    file->SetTemp(); observed = nullptr; file->Release(); Check(name.TExists() == tNo);
    // Never-opened temp owners do not delete anything during final close.
    file = NewObj Probe(&name, ffilTemp | ffilDenyWrite); Check(file); observed = file;
    Check(!file->Open(false, file->GrffilCur())); file->State();
    Check(!file->Open(true, ffilWriteEnable)); file->State(); Check(name.TExists() == tNo);
    observed = nullptr; file->Release();
    Word(errors.Cerc()); for (LegacyLong i = 0; i < errors.Cerc(); ++i) Word(errors.ErcGet(i));
    vpers = savedErrors; if (trace) Check(!std::fclose(trace));
    std::printf("File owner runtime fingerprint: %08X\n", hash);
}
