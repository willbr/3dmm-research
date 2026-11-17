#include "kauai_core.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
ASSERTNAME
static std::FILE *trace;
static unsigned hash=2166136261u,assertions;
static bool selfCopy;
static void CheckAt(bool ok,int line) {if(!ok){std::fprintf(stderr,"Filename owned: %d\n",line);std::exit(1);}}
#define Check(x) CheckAt(!!(x),__LINE__)
static void Word(unsigned n)
{for(unsigned i=0;i<4;++i){auto b=static_cast<unsigned char>(n>>(8*i));hash=(hash^b)*16777619u;if(trace)Check(std::fputc(b,trace)!=EOF);}}
void WarnProc(schar *,LegacyLong,schar *s) {while(*s)Word(static_cast<byte>(*s++));Word(0);}
bool FAssertProc(schar *file,LegacyLong line,schar *message,void *,LegacyLong)
{
    if(selfCopy&&message&&!std::strcmp(message,"blocks overlap")){Word(91);++assertions;return false;}
    std::fprintf(stderr,"%s:%" LEGACY_PRId ": %s\n",file,line,message?message:"(null)");std::exit(1);
}
static void State(Filename &name,LegacyLong prefix)
{
    String path,leaf;name.GetStnPath(&path);if(path.Cch())name.GetLeaf(&leaf);Word(name.Ftg());Word(name.FDir());Word(name.CactRef());
    if(path.Cch()>=prefix)path.Delete(0,prefix);
    for(auto *s:{&path,&leaf}){Word(s->Cch());for(LegacyLong i=0;i<s->Cch();++i)Word(static_cast<uchar>(s->Prgch()[i]));}
}
int main()
{
    auto snapshot=std::getenv("MM_FILENAME_OWNED_SNAPSHOT");trace=snapshot?std::fopen(snapshot,"wb"):nullptr;Check(!snapshot||trace);
    Word(sizeof(Filename));Word(sizeof(String));Word(sizeof(BASE));
    ZString systemRoot;unsigned index=0;auto mask=KauaiFiles::RootMask();Check(mask);while(!(mask&(1u<<index)))++index;
    Check(KauaiFiles::RootPath(index,systemRoot,kcchMaxSz));String root(systemRoot);Check(root.FAppendSz(PszLit("filename-owned-fixture/")));
    Filename directory;Check(directory.FBuildFromPath(&root,kftgDir));directory.GetStnPath(&root);auto prefix=root.Cch();
    Filename source=directory;String text(PszLit("long leaf name.txt"));Check(source.FSetLeaf(&text));source.AddRef();source.AddRef();
    State(source,prefix);Filename copied=source;State(copied,prefix);Filename assigned;assigned=source;State(assigned,prefix);
    text=PszLit("x.bin");Check(source.FSetLeaf(&text));State(source,prefix);State(copied,prefix);State(assigned,prefix);
    assigned=source;State(assigned,prefix);selfCopy=true;assigned=assigned;selfCopy=false;State(assigned,prefix);
    copied.SetNil();State(copied,prefix);copied=source;State(copied,prefix);
    String longest;for(LegacyLong i=prefix;i<254;++i)Check(longest.FAppendCh(ChLit('q')));
    source=directory;Check(source.FSetLeaf(&longest));assigned=source;copied=assigned;State(source,prefix);State(copied,prefix);
    source.SetNil();assigned=source;copied=assigned;State(copied,prefix);State(assigned,prefix);
    Word(assertions);Word(vpers->Cerc());for(LegacyLong i=0;i<vpers->Cerc();++i)Word(vpers->ErcGet(i));
    if(trace)Check(!std::fclose(trace));std::printf("Filename owned runtime fingerprint: %08X\n",hash);return 0;
}
