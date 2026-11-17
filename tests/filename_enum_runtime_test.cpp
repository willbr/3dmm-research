#include <initializer_list>
#include "kauai_core.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#ifdef _WIN32
#include <windows.h>
#endif
ASSERTNAME
static std::FILE *trace;
static unsigned hash = 2166136261u;
static const char *stage = "initial";
static String prefix;
static void CheckAt(bool ok, int line) { if (!ok) { std::fprintf(stderr,"Filename enumeration (%s): %d\n",stage,line); std::exit(1); } }
#define Check(x) CheckAt((x),__LINE__)
static void Word(unsigned v)
{ for (unsigned i = 0; i < 4; ++i) { const auto b = static_cast<unsigned char>(v >> (8*i)); hash = (hash ^ b) * 16777619u;
    if (trace) Check(std::fputc(b,trace) != EOF); } }
static void Text(const char *s) { while (*s) Word(static_cast<byte>(*s++)); Word(0); }
static void State(Filename &f)
{
    String s; f.GetStnPath(&s); Word(f.Ftg()); Word(f.FDir());
    const bool relative = s.Cch() >= prefix.Cch() && !std::memcmp(s.Prgch(),prefix.Prgch(),prefix.Cch()*sizeof(achar));
    Word(relative); if (relative) s.Delete(0,prefix.Cch()); Word(s.Cch());
    for (LegacyLong i = 0; i < s.Cch(); ++i) Word(static_cast<uchar>(s.Prgch()[i]));
}
void WarnProc(schar *, LegacyLong, schar *message) { Word(90); Text(message); }
bool FAssertProc(schar *file, LegacyLong line, schar *message, void *, LegacyLong)
{
    for (const char *known : {"must initialize the FileNameEnumerator before using it!","bad fni","unexpected filename","expected filename"})
        if (message && !std::strcmp(message,known)) { Word(91); Text(stage); Text(message); return false; }
    std::fprintf(stderr,"%s:%" LEGACY_PRId " (%s): %s\n",file,line,stage,message ? message : "(null)"); std::exit(1);
}
static void Child(Filename &output, Filename &root, const char *leaf)
{ output = root; String s(const_cast<char *>(leaf)); Check(output.FDownDir(&s,ffniCreateDir|ffniMoveToDir)); }
static void File(Filename root, const char *leaf, bool remove = false)
{
    String s(const_cast<char *>(leaf)), path; Check(root.FSetLeaf(&s)); root.GetStnPath(&path);
    if (remove) { Check(KauaiFiles::Delete(path.Psz())); return; }
    KauaiFiles::OpenOptions options; options.write = true; options.creation = KauaiFiles::Creation::CreateExclusive;
    auto handle = KauaiFiles::Open(path.Psz(),options); Check(handle); KauaiFiles::Close(handle);
#ifdef _WIN32
    if (leaf[0] == '.') Check(SetFileAttributesA(path.Psz(),FILE_ATTRIBUTE_HIDDEN) != 0);
#endif
}
static void Walk(FileNameEnumerator &e, Filename root, bool skip = false, bool outputFlags = true, unsigned limit = 100)
{
    Filename found = root; LegacyUlong flags = 0xdeadbeefUL, next = 0;
    for (unsigned n = 0; n < limit; ++n) {
        const bool ok = e.FNextFni(&found,outputFlags ? &flags : nullptr,next); Word(ok); Word(flags); State(found);
        if (!ok) return;
        next = skip && found.FDir() && flags == ffnePre ? ffneSkipDir : 0;
    }
    Check(limit == 1);
}
int main()
{
    const auto snapshot = std::getenv("MM_FILENAME_ENUM_SNAPSHOT"); trace = snapshot ? std::fopen(snapshot,"wb") : nullptr; Check(!snapshot || trace);
    Filename root; Check(root.FGetTemp()); String s; root.GetStnPath(&s); Check(root.FBuildFromPath(&s,kftgDir));
    root.GetStnPath(&prefix); Check(KauaiFiles::MakeDirectory(prefix.Psz()));
    Filename empty, sub, deep; Child(empty,root,"empty"); Child(sub,root,"sub"); Child(deep,sub,"deep");
    File(root,"a.txt"); File(root,"b.bin"); File(root,".hidden.bin"); File(sub,"c.txt"); File(deep,"d.bin");
    {
        FileNameEnumerator e;
        stage = "uninitialized"; Walk(e,root);
        stage = "flat"; Check(e.FInit(&root,nullptr,0)); Walk(e,root);
        stage = "after-done"; Walk(e,root);
        stage = "recursive"; Check(e.FInit(&root,nullptr,0,ffneRecurse)); Walk(e,root);
        stage = "filter-files"; FileType text = 'txt'; Check(e.FInit(&root,&text,1,ffneRecurse)); Walk(e,root);
        stage = "filter-tree"; FileType types[] = {kftgDir,'txt'}; Check(e.FInit(&root,types,2,ffneRecurse)); Walk(e,root);
        stage = "filter-none"; FileType absent = 'zzz'; Check(e.FInit(&root,&absent,1,ffneRecurse)); Walk(e,root);
        stage = "heap-filters"; FileType many[21]; for (auto &type : many) type = 'zzz'; many[20] = 'txt';
        Check(e.FInit(&root,many,21,ffneRecurse)); Walk(e,root);
        stage = "skip"; Check(e.FInit(&root,nullptr,0,ffneRecurse)); Walk(e,root,true);
        stage = "null-flags"; Check(e.FInit(&root,nullptr,0,ffneRecurse)); Walk(e,root,false,false);
        stage = "reinit-active"; Check(e.FInit(&root,nullptr,0,ffneRecurse)); Walk(e,root,false,true,1);
        Check(e.FInit(&sub,nullptr,0,ffneRecurse)); Walk(e,root);
        stage = "empty"; Check(e.FInit(&empty,nullptr,0)); Walk(e,root);
        stage = "skip-base"; Check(e.FInit(&root,nullptr,0)); Filename found = root; LegacyUlong flags = 0xface;
        Word(e.FNextFni(&found,&flags,ffneSkipDir)); Word(flags); State(found);
        stage = "missing"; Filename missing = root; String leaf(PszLit("missing")); Check(missing.FSetLeaf(&leaf));
        missing.GetStnPath(&s); Check(missing.FBuildFromPath(&s,kftgDir)); Check(e.FInit(&missing,nullptr,0)); Walk(e,root);
        stage = "root-filter"; Check(e.FInit(nullptr,&absent,1)); found = root;
        Word(e.FNextFni(&found,&flags)); Word(flags); Word(found.FDir()); Word(found.Ftg());
    }
    stage = "destruction-active"; { FileNameEnumerator e; Check(e.FInit(&sub,nullptr,0,ffneRecurse)); Walk(e,root,false,true,1); }
    File(deep,"d.bin",true); File(sub,"c.txt",true); File(root,"a.txt",true); File(root,"b.bin",true); File(root,".hidden.bin",true);
    for (auto *directory : {&deep,&sub,&empty,&root}) { directory->GetStnPath(&s); Check(KauaiFiles::DeleteDirectory(s.Psz())); }
    Word(vpers->Cerc()); for (LegacyLong i = 0; i < vpers->Cerc(); ++i) Word(vpers->ErcGet(i));
    if (trace) Check(!std::fclose(trace)); std::printf("Filename enumeration runtime fingerprint: %08X\n",hash);
}
