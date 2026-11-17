#include <string>
#include <initializer_list>
#include "kauai_core.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
ASSERTNAME
static std::FILE *trace;
static unsigned hash = 2166136261u, tickValue, tickCalls;
static const char *stage = "initial";
static String prefix;
static Filename *observed;
static unsigned assertions;
static void CheckAt(bool value, int line) { if (!value) { std::fprintf(stderr, "Filename naming: %d\n", line); std::exit(1); } }
#define Check(x) CheckAt((x), __LINE__)
static void Word(unsigned value)
{
    for (unsigned i = 0; i < 4; ++i) { const auto b = static_cast<unsigned char>(value >> (i * 8));
        hash = (hash ^ b) * 16777619u; if (trace) Check(std::fputc(b, trace) != EOF); }
}
static void Text(const char *text) { while (*text) Word(static_cast<byte>(*text++)); Word(0); }
static void State(Filename &name)
{
    String path; name.GetStnPath(&path); Word(name.Ftg()); Word(name.FDir());
    bool relative = path.Cch() >= prefix.Cch() && !std::memcmp(path.Prgch(), prefix.Prgch(), prefix.Cch() * sizeof(achar));
    Word(relative); if (relative) path.Delete(0, prefix.Cch());
    Word(path.Cch()); for (LegacyLong i = 0; i < path.Cch(); ++i) Word(static_cast<uchar>(path.Prgch()[i]));
}
void WarnProc(schar *, LegacyLong, schar *message) { Word(90); Text(message); }
bool FAssertProc(schar *file, LegacyLong line, schar *message, void *, LegacyLong)
{
    for (const char *known : {"bad fni", "unexpected empty", "named empty?", "unexpected filename", "expected filename"})
        if (message && !std::strcmp(message, known)) { ++assertions; Word(91); Text(stage); Text(message); return false; }
    std::fprintf(stderr, "%s:%" LEGACY_PRId " (%s): %s\n", file, line, stage, message ? message : "(null)"); std::exit(1);
}
class Errors : public ErrorStack {
public:
#ifdef DEBUG
    void Push(LegacyLong code, schar *file, LegacyLong line) override
#else
    void Push(LegacyLong code) override
#endif
    {
        Word(92); Word(code); if (observed) State(*observed);
#ifdef DEBUG
        ErrorStack::Push(code, file, line);
#else
        ErrorStack::Push(code);
#endif
    }
};
static std::uint32_t Ticks() { ++tickCalls; return tickValue; }
static void Env(const char *key, const std::string &value)
{
#ifdef _WIN32
    Check(_putenv_s(key, value.c_str()) == 0);
#else
    Check(setenv(key, value.c_str(), 1) == 0);
#endif
}
static void Unique(Filename &value, FileType type, unsigned ticks)
{
    tickValue = ticks; tickCalls = 0; observed = &value;
    Word(value.FGetUnique(type)); observed = nullptr; Word(tickCalls); State(value);
}
int main()
{
    const auto snapshot = std::getenv("MM_FILENAME_NAMING_SNAPSHOT");
    trace = snapshot ? std::fopen(snapshot, "wb") : nullptr; Check(!snapshot || trace);
    const auto oldClock = KauaiPlatform::SetTickSource(Ticks);
    const auto oldType = vftgTemp; Errors errors; auto *oldErrors = vpers; vpers = &errors;
    ZString temp; Check(KauaiFiles::TempPath(temp, kcchMaxSz)); prefix = temp;
    Filename root; String path = prefix; Check(path.FAppendSz(PszLit("naming-root")));
    Check(root.FBuildFromPath(&path, kftgDir)); root.GetStnPath(&path); Check(KauaiFiles::MakeDirectory(path.Psz()));
    Filename value = root;
    stage = "first"; Unique(value, 'TXT', 0);
    stage = "signed-clock"; value = root; Unique(value, 'TXT', 0x7ffe);
    stage = "old-leaf"; value = root; String leaf(PszLit("Temp0003")); Check(value.FSetLeaf(&leaf)); Unique(value, ftgNil, 0);
    const unsigned candidates[] = {5,11,18,26,35,45,56,68,81,95,110,126,143,161,180,200,221,243,266,290};
    for (auto bits : candidates) {
        Filename collision = root; leaf.FFormatSz(PszLit("Temp%04x"), static_cast<LegacyLong>(bits)); Check(collision.FSetLeaf(&leaf, 'TXT'));
        collision.GetStnPath(&path); KauaiFiles::OpenOptions options; options.write = true; options.creation = KauaiFiles::Creation::CreateExclusive;
        auto file = KauaiFiles::Open(path.Psz(), options); Check(file); KauaiFiles::Close(file);
    }
    stage = "exhaustion"; value = root; Unique(value, 'TXT', 0); Check(value.Ftg() == ftgNil);
    stage = "after-exhaustion"; value = root; Unique(value, 'TXT', 0); value.GetLeaf(&leaf); Check(leaf.FEqualSz(PszLit("Temp0035.TXT")));
    stage = "capacity-recovery"; path.SetRgch(prefix.Prgch(), 3);
    while (path.Cch() < 249) Check(path.FAppendCh('a')); Check(path.FAppendCh(KauaiFiles::Separator()));
    Check(value.FBuildFromPath(&path, kftgDir)); Unique(value, 'TXT', 0);
    const std::string savedTmp = std::getenv("TMP") ? std::getenv("TMP") : "";
    const std::string savedTemp = std::getenv("TEMP") ? std::getenv("TEMP") : "";
    const std::string savedTmpDir = std::getenv("TMPDIR") ? std::getenv("TMPDIR") : "";
    stage = "temp-provider-failure"; const std::string tooLong(300, 'a');
    Env("TMP", tooLong); Env("TEMP", tooLong); Env("TMPDIR", tooLong); value = root;
    observed = &value; tickCalls = 0; Word(value.FGetTemp()); observed = nullptr; Word(tickCalls); State(value);
    Env("TMP", savedTmp); Env("TEMP", savedTemp); Env("TMPDIR", savedTmpDir);
    stage = "temp-first"; tickValue = 0xffff; tickCalls = 0; Check(value.FGetTemp()); Word(tickCalls); State(value);
    stage = "temp-type"; vftgTemp = 'aB'; tickValue = 0x80000000; tickCalls = 0; Check(value.FGetTemp()); Word(tickCalls); State(value);
    stage = "temp-cache"; Env("TMP", tooLong); Env("TEMP", tooLong); Env("TMPDIR", tooLong);
    vftgTemp = ftgNil; tickValue = 0xffffffff; tickCalls = 0; Check(value.FGetTemp()); Word(tickCalls); State(value);
    Env("TMP", savedTmp); Env("TEMP", savedTemp); Env("TMPDIR", savedTmpDir);
    for (auto bits : candidates) {
        Filename collision = root; leaf.FFormatSz(PszLit("Temp%04x"), static_cast<LegacyLong>(bits));
        Check(collision.FSetLeaf(&leaf, 'TXT') && collision.FDelete());
    }
    root.GetStnPath(&path); Check(KauaiFiles::DeleteDirectory(path.Psz()));
    Word(assertions); Word(errors.Cerc()); for (LegacyLong i = 0; i < errors.Cerc(); ++i) Word(errors.ErcGet(i));
    vpers = oldErrors; vftgTemp = oldType; KauaiPlatform::SetTickSource(oldClock);
    if (trace) Check(!std::fclose(trace)); std::printf("Filename naming runtime fingerprint: %08X\n", hash);
}
