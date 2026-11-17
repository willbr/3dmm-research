#include <string>
#include <vector>
#include <initializer_list>
#include "kauai_core.h"
#include "mm/filename_case.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#undef size
ASSERTNAME
static std::FILE *trace;
static unsigned hash = 2166136261u;
static const char *stage = "initial";
static void CheckAt(bool ok, int line) { if (!ok) { std::fprintf(stderr, "Filename case (%s): %d\n", stage, line); std::exit(1); } }
#define Check(x) CheckAt((x), __LINE__)
static void Word(unsigned value)
{ for (unsigned i = 0; i < 4; ++i) { auto b = static_cast<unsigned char>(value >> (i * 8)); hash = (hash ^ b) * 16777619u;
    if (trace) Check(std::fputc(b, trace) != EOF); } }
static void Text(const char *s) { while (*s) Word(static_cast<byte>(*s++)); Word(0); }
static void Text(String &s) { Word(s.Cch()); for (LegacyLong i = 0; i < s.Cch(); ++i) Word(static_cast<uchar>(s.Prgch()[i])); }
void WarnProc(schar *, LegacyLong, schar *message) { Word(90); Text(message); }
bool FAssertProc(schar *file, LegacyLong line, schar *message, void *, LegacyLong)
{
    if (message && !std::strcmp(message,"sz too long")) { Word(91); Text(stage); Text(message); return false; }
    std::fprintf(stderr, "%s:%" LEGACY_PRId " (%s): %s\n", file, line, stage, message ? message : "(null)"); std::exit(1);
}
struct Query { std::string path; int status; };
struct Directory { std::string pattern; std::vector<std::string> names; int end = MM_FILENAME_CASE_END; bool handle = true; };
struct Fixture {
    MMFilenameCaseHost host{};
    MMFilenameState state{0x414243}; String path;
    MMFilenameValue value;
    std::vector<Query> queries;
    std::vector<Directory> directories;
    unsigned query = 0, directory = 0, entry = 0, closes = 0;
    Directory *active = nullptr;
    explicit Fixture(const char *text, int sensitive = 1) : path(const_cast<achar *>(text))
    {
        value = {&state, {path.Pst(), sizeof(achar), nullptr, nullptr}, this};
        host.sensitive = sensitive; host.path.context = host.path.value.context = this;
        host.name_length = [](void *, const void *raw) -> MMI32 { return CchSz(const_cast<achar *>(static_cast<const achar *>(raw))); };
        host.path.value.upper = [](void *, unsigned ch) -> unsigned { return ch >= 'a' && ch <= 'z' ? ch - 'a' + 'A' : ch; };
        host.path.value.separator = [](void *, unsigned ch) -> int { return ch == '/'; };
        host.path.value.validate = [](void *, const MMFilenameValue *, int flags) { Word(5); Word(flags); };
        host.path.issue = [](void *, const MMFilenameValue *, int) { Check(false); };
        host.path.info = [](void *context, const void *raw, MMFilenameInfo *out) -> int {
            auto &f = *static_cast<Fixture *>(context); Check(f.query < f.queries.size()); const auto &q = f.queries[f.query++];
            Check(q.path == static_cast<const char *>(raw)); Word(1); Text(q.path.c_str()); Word(q.status);
            out->directory = out->hidden = out->system = 1; return q.status;
        };
        host.first = [](void *context, const void *raw, void **handle, void *name, MMI32 capacity) -> int {
            auto &f = *static_cast<Fixture *>(context); Check(f.directory < f.directories.size());
            auto &d = f.directories[f.directory++]; Check(d.pattern == static_cast<const char *>(raw));
            f.active = &d; f.entry = 0; *handle = d.handle ? &d : nullptr; Word(2); Text(d.pattern.c_str());
            return f.Emit(name, capacity);
        };
        host.next = [](void *context, void *handle, void *name, MMI32 capacity) -> int {
            auto &f = *static_cast<Fixture *>(context); Check(handle && handle == f.active); ++f.entry; Word(3); return f.Emit(name, capacity);
        };
        host.close = [](void *context, void *handle) { auto &f = *static_cast<Fixture *>(context);
            Check(handle == (f.active->handle ? f.active : nullptr)); ++f.closes; Word(4); Word(handle != nullptr); f.active = nullptr; };
    }
    int Emit(void *raw, MMI32 capacity)
    {
        Check(capacity == 1024); const auto &d = *active;
        const int result = entry < d.names.size() ? MM_FILENAME_CASE_ENTRY : d.end; Word(result);
        if (result == MM_FILENAME_CASE_ENTRY) { const auto &s = d.names[entry]; Check(s.size() < static_cast<unsigned>(capacity));
            std::memcpy(raw, s.c_str(), s.size() + 1); Text(s.c_str()); }
        return result;
    }
    void Run(bool expected, const char *output)
    {
#ifdef MM_FILENAME_CASE_ORIGINAL
        extern int FilenameCaseOriginal(MMFilenameValue *, const MMFilenameCaseHost *, String &);
        const bool result = FilenameCaseOriginal(&value, &host, path) != 0;
#else
        const bool result = mm_filename_resolve_case(&value, &host) != 0;
#endif
        Word(result); Word(state.type); Text(path); Word(closes);
        Check(result == expected && path.FEqualSz(const_cast<achar *>(output)));
        Check(query == queries.size() && directory == directories.size() && !active);
    }
};
static void ModelCases()
{
    stage = "exact"; { Fixture f("/FILE.BIN"); f.queries = {{"/FILE.BIN",0}}; f.Run(true,"/FILE.BIN"); Check(f.state.type == 0x414243); }
    stage = "info-error"; { Fixture f("/FILE.BIN"); f.queries = {{"/FILE.BIN",2}}; f.Run(false,"/FILE.BIN"); }
    stage = "insensitive-missing"; { Fixture f("/FILE.BIN",0); f.queries = {{"/FILE.BIN",1}}; f.host.first = nullptr; f.Run(false,"/FILE.BIN"); }
    stage = "relative"; { Fixture f("FILE.BIN"); f.queries = {{"FILE.BIN",1}}; f.Run(false,"FILE.BIN"); }
    stage = "root"; { Fixture f("/"); f.queries = {{"/",1}}; f.Run(true,"/"); Check(f.state.type == MM_FILENAME_DIRECTORY); }
    stage = "nested"; { Fixture f("/SETS/FILE.BIN"); f.queries = {{"/SETS/FILE.BIN",1},{"/SETS",1},{"/Sets/FILE.BIN",1}};
        f.directories = {{"/*",{"other","Sets"}},{"/Sets/*",{"file.bin","unused"}}}; f.Run(true,"/Sets/file.bin"); Check(f.state.type == 'BIN'); }
    stage = "exact-component"; { Fixture f("/Sets/FILE.BIN"); f.queries = {{"/Sets/FILE.BIN",1},{"/Sets",0},{"/Sets/FILE.BIN",1}};
        f.directories = {{"/Sets/*",{"file.bin"}}}; f.Run(true,"/Sets/file.bin"); }
    stage = "ambiguous"; { Fixture f("/SETS"); f.queries = {{"/SETS",1},{"/SETS",1}};
        f.directories = {{"/*",{"Sets","SETS","never-read"},2}}; f.Run(false,"/SETS"); }
    stage = "no-match"; { Fixture f("/SETS"); f.queries = {{"/SETS",1},{"/SETS",1}};
        f.directories = {{"/*",{"set","wrong"}}}; f.Run(false,"/SETS"); }
    for (bool handle : {false,true}) { stage = handle ? "first-error-handle" : "first-error-null";
        Fixture f("/SETS"); f.queries = {{"/SETS",1},{"/SETS",1}}; f.directories = {{"/*",{},2,handle}}; f.Run(false,"/SETS"); }
    stage = "next-error"; { Fixture f("/SETS"); f.queries = {{"/SETS",1},{"/SETS",1}};
        f.directories = {{"/*",{"Sets"},2}}; f.Run(false,"/SETS"); }
    stage = "component-error"; { Fixture f("/SETS/FILE.BIN"); f.queries = {{"/SETS/FILE.BIN",1},{"/SETS",2}}; f.Run(false,"/SETS/FILE.BIN"); }
    stage = "partial-failure"; { Fixture f("/SETS/FILE.BIN"); f.queries = {{"/SETS/FILE.BIN",1},{"/SETS",1},{"/Sets/FILE.BIN",1}};
        f.directories = {{"/*",{"Sets"}},{"/Sets/*",{}}}; f.Run(false,"/SETS/FILE.BIN"); }
    stage = "directory"; { Fixture f("/SETS/"); f.queries = {{"/SETS/",1},{"/SETS",1}};
        f.directories = {{"/*",{"Sets"}}}; f.Run(true,"/Sets/"); Check(f.state.type == MM_FILENAME_DIRECTORY); }
    stage = "literal-backslash"; { Fixture f("/SETS\\FILE.BIN"); f.queries = {{"/SETS\\FILE.BIN",1},{"/SETS\\FILE.BIN",1}};
        f.directories = {{"/*",{"Sets\\file.bin"}}}; f.Run(true,"/Sets\\file.bin"); }
    stage = "non-ascii-different"; { Fixture f("/\xC0.BIN"); f.queries = {{"/\xC0.BIN",1},{"/\xC0.BIN",1}};
        f.directories = {{"/*",{"\xE0.bin"}}}; f.Run(false,"/\xC0.BIN"); }
    stage = "non-ascii-equal"; { Fixture f("/\xC0.BIN"); f.queries = {{"/\xC0.BIN",1},{"/\xC0.BIN",1}};
        f.directories = {{"/*",{"\xC0.bin"}}}; f.Run(true,"/\xC0.bin"); }
    stage = "long-entry"; { const std::string input = "/" + std::string(254,'a'), output = "/" + std::string(254,'A');
        Fixture f(input.c_str()); f.queries = {{input,1},{input,1}};
        f.directories = {{"/*",{std::string(1023,'A'), std::string(255,'A'), std::string(254,'A')}}}; f.Run(true,output.c_str()); }
}
static void NativeCases()
{
    stage = "native"; Filename root; Check(root.FGetTemp()); String path; root.GetStnPath(&path);
    Check(root.FBuildFromPath(&path,kftgDir)); root.GetStnPath(&path); Check(KauaiFiles::MakeDirectory(path.Psz())); const auto prefix = path.Cch();
    Filename file = root; String leaf(PszLit("Case.bin")); Check(file.FSetLeaf(&leaf));
    file.GetStnPath(&path); KauaiFiles::OpenOptions options; options.write = true; options.creation = KauaiFiles::Creation::CreateExclusive;
    auto handle = KauaiFiles::Open(path.Psz(),options); Check(handle); KauaiFiles::Close(handle);
    for (const char *text : {"Case.bin","CASE.BIN","missing.bin"}) {
        Filename name = root; leaf = const_cast<char *>(text); Check(name.FSetLeaf(&leaf)); String before; name.GetStnPath(&before);
        Word(name.FResolveCase()); name.GetStnPath(&path); Check(path.FEqual(&before)); path.Delete(0,prefix); Text(path); Word(name.Ftg());
    }
    Filename mismatch = file; file.GetStnPath(&path); Check(mismatch.FBuildFromPath(&path,kftgDir)); Word(mismatch.FResolveCase());
    Check(file.FDelete()); root.GetStnPath(&path); Check(KauaiFiles::DeleteDirectory(path.Psz()));
}
int main()
{
    const auto snapshot = std::getenv("MM_FILENAME_CASE_SNAPSHOT"); trace = snapshot ? std::fopen(snapshot,"wb") : nullptr; Check(!snapshot || trace);
    ModelCases(); NativeCases(); if (trace) Check(!std::fclose(trace)); std::printf("Filename case runtime fingerprint: %08X\n",hash);
}
