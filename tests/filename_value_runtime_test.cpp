#include "kauai_core.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
ASSERTNAME
static std::FILE *trace;
static unsigned hash = 2166136261u;
static unsigned assertions;
static const char *expected;
static const char *stage = "initial";
static void CheckAt(bool value, int line) { if (!value) { std::fprintf(stderr, "Filename value fixture: %d\n", line); std::exit(1); } }
#define Check(value) CheckAt((value), __LINE__)
static void Word(unsigned value)
{
    for (unsigned i = 0; i < 4; ++i) {
        unsigned char b = static_cast<unsigned char>(value >> (i * 8));
        hash = (hash ^ b) * 16777619u;
        if (trace) Check(std::fputc(b, trace) != EOF);
    }
}
static void Text(String &s) { Word(s.Cch()); for (LegacyLong i = 0; i < s.Cch(); ++i) Word(static_cast<uchar>(s.Prgch()[i])); }
void WarnProc(schar *, LegacyLong, schar *message) { Word(90); while (*message) Word(static_cast<byte>(*message++)); Word(0); }
bool FAssertProc(schar *file, LegacyLong line, schar *message, void *, LegacyLong)
{
    bool known = false;
    for (const char *text : {"bad fni", "expected filename", "unexpected filename", "expected trailing slash",
                             "unexpected file", "unexpected dir", "unexpected empty", "named empty?"})
        if (message && !std::strcmp(text, message)) known = true;
    if (expected && known) {
        ++assertions; Word(91);
        for (const char *p : {stage, static_cast<const char *>(message)}) {
            while (*p) Word(static_cast<byte>(*p++)); Word(0);
        }
        return false;
    }
    std::fprintf(stderr, "%s:%" LEGACY_PRId " (%s): %s\n", file, line, stage, message ? message : "(null)"); std::exit(1);
}
static void State(Filename &f, LegacyLong base)
{
    String path, leaf; f.GetStnPath(&path);
    Word(f.Ftg()); Word(f.FDir());
    if (path.Cch()) {
        Check(path.Cch() >= base); path.Delete(0, base); Text(path);
        f.GetLeaf(&leaf); Text(leaf);
    } else Text(path);
}
int main()
{
    const auto snapshot = std::getenv("MM_FILENAME_VALUE_SNAPSHOT");
    trace = snapshot ? std::fopen(snapshot, "wb") : nullptr; Check(!snapshot || trace);
    ZString nativeRoot;
    unsigned rootIndex = 0; const auto roots = KauaiFiles::RootMask();
    Check(roots != 0); while (!(roots & (1u << rootIndex))) ++rootIndex;
    Check(KauaiFiles::RootPath(rootIndex, nativeRoot, kcchMaxSz));
    String rootPath(nativeRoot); Check(rootPath.FAppendSz(PszLit("filename-value-fixture")));
    Filename root; Check(root.FBuildFromPath(&rootPath, kftgDir)); root.GetStnPath(&rootPath);
    const auto base = rootPath.Cch();
#ifdef KAUAI_EXPENSIVE_CHECKS
    expected = "bad fni";
#endif
    Filename value; State(value, base);
    for (const char *tail : {"plain", "a.txt", "a.abcd", "a.abcde", "a.", ".ext", "a.b.c", "a.a b", "a.1!@#"}) {
        stage = tail;
        String leaf; leaf.SetSzs(const_cast<char *>(tail));
        value = root; Check(value.FSetLeaf(&leaf)); State(value, base);
        for (LegacyLong type : {LegacyLong(0), LegacyLong('TXT'), LegacyLong('abcd'), LegacyLong(0x41004243), LegacyLong(0x41424300)}) {
            Filename changed = value;
            Word(changed.FChangeFtg(type)); State(changed, base);
        }
    }
    stage = "extended bytes";
    for (unsigned byte : {0x7fu, 0x80u, 0x9au, 0xe9u, 0xffu}) {
        achar chars[] = {ChLit('x'), ChLit('.'), static_cast<achar>(byte), 0};
        String leaf(chars); value = root; Check(value.FSetLeaf(&leaf)); State(value, base);
        Word(value.FChangeFtg(static_cast<LegacyLong>((byte << 8) | 'z'))); State(value, base);
    }
    // Change-type rejects a 255-unit result even when its extension is empty.
    stage = "capacity";
    String longLeaf; for (LegacyLong i = base; i < 255; ++i) Check(longLeaf.FAppendCh(ChLit('q')));
    value = root; Check(value.FSetLeaf(&longLeaf)); State(value, base);
    Word(value.FChangeFtg(0)); State(value, base);
    Check(longLeaf.FAppendCh(ChLit('q'))); value = root;
    Check(!value.FSetLeaf(&longLeaf)); State(value, base);
    // Set-leaf failure clears both path/type; change-type failure preserves them.
    longLeaf.Delete(longLeaf.Cch() - 4); value = root; Check(value.FSetLeaf(&longLeaf)); State(value, base);
    Word(value.FChangeFtg('ABCD')); State(value, base);
    String empty; value = root; Check(value.FSetLeaf(&empty, kftgDir)); State(value, base);
    Check(value.FSetLeaf(nullptr, kftgDir)); State(value, base);
    value.SetNil(); State(value, base);
    Word(assertions); Word(vpers->Cerc()); for (LegacyLong i = 0; i < vpers->Cerc(); ++i) Word(vpers->ErcGet(i));
    vpers->Clear(); if (trace) Check(!std::fclose(trace));
    std::printf("Filename value runtime fingerprint: %08X\n", hash);
}
