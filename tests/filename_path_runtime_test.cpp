#include "kauai_core.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
ASSERTNAME
static std::FILE *trace;
static unsigned hash = 2166136261u;
static const char *stage = "initial";
static Filename *observed;
static LegacyLong prefix;
static unsigned assertions;
static void CheckAt(bool value, int line) { if (!value) { std::fprintf(stderr, "Filename path: %d\n", line); std::exit(1); } }
#define Check(x) CheckAt((x), __LINE__)
static void Word(unsigned value)
{
    for (unsigned i = 0; i < 4; ++i) { unsigned char b = static_cast<unsigned char>(value >> (i * 8));
        hash = (hash ^ b) * 16777619u; if (trace) Check(std::fputc(b, trace) != EOF); }
}
static void Text(const char *p) { while (*p) Word(static_cast<byte>(*p++)); Word(0); }
static void StringValue(String &s) { Word(s.Cch()); for (LegacyLong i = 0; i < s.Cch(); ++i) Word(static_cast<uchar>(s.Prgch()[i])); }
static void State(Filename &name)
{
    String path; name.GetStnPath(&path); Word(name.Ftg()); Word(name.FDir());
    if (path.Cch()) { Check(path.Cch() >= prefix); path.Delete(0, prefix); }
    StringValue(path);
}
void WarnProc(schar *, LegacyLong, schar *message) { Word(90); Text(message); }
bool FAssertProc(schar *file, LegacyLong line, schar *message, void *, LegacyLong)
{
    if (message && (!std::strcmp(message, "bad fni") || !std::strcmp(message, "expected filename"))) {
        ++assertions; Word(91); Text(stage); Text(message); return false;
    }
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
int main()
{
    const auto snapshot = std::getenv("MM_FILENAME_PATH_SNAPSHOT");
    trace = snapshot ? std::fopen(snapshot, "wb") : nullptr; Check(!snapshot || trace);
    Errors errors; auto *previous = vpers; vpers = &errors;
    // A real private directory for filesystem cases; its spelling is excluded.
    Filename root; Check(root.FGetTemp()); String rootPath; root.GetStnPath(&rootPath);
    Check(root.FBuildFromPath(&rootPath, kftgDir)); root.GetStnPath(&rootPath);
    Check(KauaiFiles::MakeDirectory(rootPath.Psz())); prefix = rootPath.Cch();
    Filename value;
    for (const char *tail : {"plain", "a.txt", "a.", ".ext", ".", "..", "child/../plain", "sub/", "sub/..."}) {
        stage = tail; String input = rootPath; input.FAppendSz(const_cast<char *>(tail));
        for (LegacyLong type : {LegacyLong(ftgNil), LegacyLong('BIN'), LegacyLong(kftgDir)}) {
            value = root; observed = &value; Word(value.FBuildFromPath(&input, type)); observed = nullptr;
            // Dot-parent paths can point above the private directory.
            if (!std::strcmp(tail, "..")) { Word(value.Ftg()); Word(value.FDir()); }
            else State(value);
        }
    }
    stage = "navigation"; String child(PszLit("child")), leaf;
    value = root; observed = &value; Word(value.FDownDir(&child, 0)); observed = nullptr; State(value);
    Check(value.FDownDir(&child, ffniCreateDir)); Check(value.FEqual(&root));
    Check(value.FDownDir(&child, ffniMoveToDir)); State(value); Check(value.TExists() == tYes);
    Filename sibling = value; Check(sibling.FUpDir(&leaf, 0)); Check(leaf.FEqual(&child)); StringValue(leaf); State(sibling);
    Check(value.FUpDir(&leaf, ffniMoveToDir) && value.FEqual(&root)); State(value);
    Filename first = root, second = root;
    leaf = PszLit("data.bin"); Check(first.FSetLeaf(&leaf)); leaf = PszLit("other.bin"); Check(second.FSetLeaf(&leaf));
    Word(first.FSameDir(&second)); Word(first.FEqual(&second)); Word(first.TExists());
    auto *file = FileObject::PfilCreate(&first); Check(file && file->FWriteRgb((void *)"DATA", 4, 0)); file->Release();
    Word(first.TExists()); Filename directory = first; String path; first.GetStnPath(&path);
    Check(directory.FBuildFromPath(&path, kftgDir)); observed = &directory; Word(directory.TExists()); observed = nullptr;
    stage = "search"; String search(rootPath), input(PszLit("data.bin"));
    Check(value.FSearchInPath(&input, &search)); State(value);
    input = PszLit("missing.bin"); observed = &value; Word(value.FSearchInPath(&input, &search)); observed = nullptr; State(value);
    stage = "rename-delete"; Check(first.FRename(&second)); State(first); State(second);
    Word(first.TExists()); Word(second.TExists()); Check(second.FDelete());
    observed = &second; Word(second.FDelete()); Word(second.FRename(&first)); observed = nullptr;
    // Missing descent and upward movement preserve state/output on failure.
    stage = "up-root"; ZString systemRoot; unsigned index = 0; const auto mask = KauaiFiles::RootMask();
    while (!(mask & (1u << index))) ++index; Check(KauaiFiles::RootPath(index, systemRoot, kcchMaxSz));
    String rootText(systemRoot); Check(value.FBuildFromPath(&rootText, kftgDir)); leaf = PszLit("unchanged");
    Word(value.FUpDir(&leaf, ffniMoveToDir)); StringValue(leaf); Word(value.FDir());
    child = PszLit("child"); value = root; Check(value.FDownDir(&child, ffniMoveToDir)); value.GetStnPath(&path);
    Check(KauaiFiles::DeleteDirectory(path.Psz()) && KauaiFiles::DeleteDirectory(rootPath.Psz()));
    Word(assertions); Word(errors.Cerc()); for (LegacyLong i = 0; i < errors.Cerc(); ++i) Word(errors.ErcGet(i));
    vpers = previous; if (trace) Check(!std::fclose(trace));
    std::printf("Filename path runtime fingerprint: %08X\n", hash);
}
