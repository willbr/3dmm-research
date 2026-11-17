#include "kauai_core.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
ASSERTNAME
static std::FILE *trace;
static unsigned hash=2166136261u;
static String prefix;
static void CheckAt(bool ok,int line) {if(!ok){std::fprintf(stderr,"Filename frame: %d\n",line);std::exit(1);}}
#define Check(x) CheckAt(!!(x),__LINE__)
static void Word(unsigned n)
{for(unsigned i=0;i<4;++i){auto b=static_cast<unsigned char>(n>>(8*i));hash=(hash^b)*16777619u;if(trace)Check(std::fputc(b,trace)!=EOF);}}
void WarnProc(schar *,LegacyLong,schar *s) {Word(90);while(*s)Word(static_cast<byte>(*s++));Word(0);}
bool FAssertProc(schar *file,LegacyLong line,schar *message,void *,LegacyLong)
{std::fprintf(stderr,"%s:%" LEGACY_PRId ": %s\n",file,line,message?message:"(null)");std::exit(1);}
static void State(Filename &name)
{
    String path;name.GetStnPath(&path);Word(name.CactRef());Word(name.Ftg());Word(name.FDir());
    const bool relative=path.Cch()>=prefix.Cch()&&!std::memcmp(path.Prgch(),prefix.Prgch(),prefix.Cch()*sizeof(achar));
    Word(relative);if(relative)path.Delete(0,prefix.Cch());Word(path.Cch());for(LegacyLong i=0;i<path.Cch();++i)Word(static_cast<uchar>(path.Prgch()[i]));
}
static void File(Filename name,const char *leaf,bool remove=false)
{
    String text; text.SetSzs(const_cast<char *>(leaf));Check(name.FSetLeaf(&text));name.GetStnPath(&text);
    if(remove){Check(KauaiFiles::Delete(text.Psz()));return;}
    KauaiFiles::OpenOptions o;o.write=true;o.creation=KauaiFiles::Creation::CreateExclusive;
    auto h=KauaiFiles::Open(text.Psz(),o);Check(h);KauaiFiles::Close(h);
}
static void Walk(FileNameEnumerator &e,Filename &out,bool skip=false,unsigned limit=100)
{
    LegacyUlong flags=0xcafebabe,next=0;
    for(unsigned i=0;i<limit;++i){const bool ok=e.FNextFni(&out,&flags,next);Word(ok);Word(flags);State(out);if(!ok)return;
        out.AddRef();Word(out.CactRef());next=skip&&out.FDir()&&flags==ffnePre?ffneSkipDir:0;}
    Check(limit==1);
}
int main()
{
    auto snapshot=std::getenv("MM_FILENAME_FRAME_SNAPSHOT");trace=snapshot?std::fopen(snapshot,"wb"):nullptr;Check(!snapshot||trace);
    Filename root;Check(root.FGetTemp());String path;root.GetStnPath(&path);Check(root.FBuildFromPath(&path,kftgDir));
    root.GetStnPath(&prefix);Check(KauaiFiles::MakeDirectory(prefix.Psz()));root.AddRef();root.AddRef();
    Filename child=root;String leaf(PszLit("sub"));Check(child.FDownDir(&leaf,ffniCreateDir|ffniMoveToDir));child.AddRef();
    Filename deep[7];deep[0]=child;
    for(unsigned i=1;i<7;++i){deep[i]=deep[i-1];leaf=PszLit("level");Check(deep[i].FDownDir(&leaf,ffniCreateDir|ffniMoveToDir));}
    File(root,"a.bin");File(child,"b.txt");File(deep[6],"end.bin");
    {
        FileNameEnumerator e;Filename out=root;out.AddRef();out.AddRef();
        Check(e.FInit(&root,nullptr,0,ffneRecurse));Walk(e,out);
        Check(e.FInit(&root,nullptr,0,ffneRecurse));Walk(e,out,true);
        Check(e.FInit(&root,nullptr,0,ffneRecurse));Walk(e,out,false,1);
        Check(e.FInit(&child,nullptr,0,ffneRecurse));Walk(e,out);
        FileType filters[21];for(auto &type:filters)type='BIN';filters[0]=kftgDir;
        Check(e.FInit(&root,filters,21,ffneRecurse));Walk(e,out);
        out=root;out.AddRef();out.AddRef();out.AddRef();
        Check(e.FInit(nullptr,nullptr,0));LegacyUlong flags=0;Word(e.FNextFni(&out,&flags));Word(flags);Word(out.CactRef());Word(out.FDir());
        Check(e.FInit(&root,nullptr,0));Walk(e,out);
    }
    {FileNameEnumerator e;Filename out=child;Check(e.FInit(&child,nullptr,0,ffneRecurse));Walk(e,out,false,1);}
    File(deep[6],"end.bin",true);File(child,"b.txt",true);File(root,"a.bin",true);
    for(unsigned i=7;i>1;--i){deep[i-1].GetStnPath(&path);Check(KauaiFiles::DeleteDirectory(path.Psz()));}
    child.GetStnPath(&path);Check(KauaiFiles::DeleteDirectory(path.Psz()));Check(KauaiFiles::DeleteDirectory(prefix.Psz()));
    Word(vpers->Cerc());for(LegacyLong i=0;i<vpers->Cerc();++i)Word(vpers->ErcGet(i));
    if(trace)Check(!std::fclose(trace));std::printf("Filename frame runtime fingerprint: %08X\n",hash);return 0;
}
