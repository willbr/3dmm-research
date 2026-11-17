#include "platform_files.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <chrono>
#include <sys/stat.h>
#include <io.h>
using namespace KauaiFiles;
using Path = std::basic_string<PathChar>;
static std::FILE *trace;
static unsigned hash = 2166136261u;
static void CheckAt(bool ok, int line) { if (!ok) { std::fprintf(stderr,"Native files: %d\n",line); std::exit(1); } }
#define Check(x) CheckAt((x),__LINE__)
static void Word(unsigned value)
{ for (unsigned i=0;i<4;++i) { const auto b=static_cast<unsigned char>(value>>(i*8)); hash=(hash^b)*16777619u; if(trace) Check(std::fputc(b,trace)!=EOF); } }
static Path P(const char *s) { Path out; while(*s) out+=static_cast<PathChar>(static_cast<unsigned char>(*s++)); return out; }
static void Text(const PathChar *s) { while(*s) Word(*s++); Word(0); }
static void Result(bool result, bool expected) { static unsigned n=0; ++n; Word(result);
    if(result!=expected) std::fprintf(stderr,"Result %u: %d expected %d\n",n,result,expected); Check(result==expected); }
static void Metadata(const Path &path)
{
    PathInfo info; info.directory=info.hidden=info.system=info.readOnly=info.link=true;
    Word(static_cast<unsigned>(Info(path.c_str(),&info))); Word(info.directory); Word(info.hidden); Word(info.system); Word(info.readOnly); Word(info.link);
}
static Path Join(const Path &a,const char *b) {return a+(IsSeparator(a.back())?Path{}:Path(1,Separator()))+P(b);}
int main()
{
    const auto snapshot=std::getenv("MM_NATIVE_FILES_SNAPSHOT"); trace=snapshot?std::fopen(snapshot,"wb"):nullptr; Check(!snapshot||trace);
    PathChar buffer[2048], saved[2048]; Check(TempPath(buffer,2048)); const Path temp(buffer);
    Path root=Join(temp, ("native-files-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())).c_str()); Check(MakeDirectory(root.c_str()));
    const auto file=Join(root,"Space name.bin"), moved=Join(root,"moved.bin"), absent=Join(root,"missing"), nested=Join(root,"one/two");
    Result(IsSeparator('/'),true); Result(IsSeparator('\\'),true); Word(Separator()); Result(EqualPaths(P("C:\\a").c_str(),P("c:\\A").c_str()),true);
    Metadata(root); Metadata(absent); Metadata(Join(absent,"child"));
    OpenOptions create; create.write=true; create.creation=Creation::CreateExclusive;
    auto handle=Open(file.c_str(),create); Result(handle!=nullptr,true); Result(Open(file.c_str(),create)!=nullptr,false);
    Result(Write(handle,"abcdef",6),true); Result(Flush(handle),true); std::uint32_t bytes=99; Result(Size(handle,&bytes),true); Word(bytes);
    char data[8]; std::memset(data,'X',sizeof data); Result(Seek(handle,4),true); Result(Read(handle,data,4),false);
    for(char c:data) Word(static_cast<unsigned char>(c));
    Result(Seek(handle,0x80000000u),false); Result(Resize(handle,0x80000000u),false);
    Result(Resize(handle,3),true); Result(Size(handle,&bytes),true); Word(bytes); Close(handle); Close(nullptr);
    auto reader=Open(file.c_str(),OpenOptions{}); Check(reader); Result(Write(reader,"bad",3),false); Result(Resize(reader,1),false);
    std::memset(data,'X',sizeof data); Result(Read(reader,data,2),true); Word(data[0]); Word(data[1]);
    Result(Delete(file.c_str()),false); Result(Rename(file.c_str(),moved.c_str()),false);
    OpenOptions overwrite; overwrite.write=true; overwrite.creation=Creation::CreateAlways; Result(Open(file.c_str(),overwrite)!=nullptr,false); Close(reader);
    Metadata(file); Result(Rename(file.c_str(),moved.c_str()),true); Metadata(file); Metadata(moved);
    const Path collision=Join(root,"collision.bin"); handle=Open(collision.c_str(),create); Check(handle); Close(handle);
    Result(Rename(moved.c_str(),collision.c_str()),false); Result(Delete(collision.c_str()),true);
#ifdef UNICODE
    Check(_wchmod(reinterpret_cast<const wchar_t *>(moved.c_str()),_S_IREAD)==0);
#else
    Check(_chmod(moved.c_str(),_S_IREAD)==0);
#endif
    Metadata(moved); Result(Delete(moved.c_str()),false); Result(Rename(moved.c_str(),file.c_str()),false);
#ifdef UNICODE
    Check(_wchmod(reinterpret_cast<const wchar_t *>(moved.c_str()),_S_IREAD|_S_IWRITE)==0);
#else
    Check(_chmod(moved.c_str(),_S_IREAD|_S_IWRITE)==0);
#endif
    Result(MakeDirectories(nested.c_str()),true); Result(MakeDirectories(nested.c_str()),true); Result(MakeDirectories(moved.c_str()),false);
    Result(MakeDirectories(P("relative").c_str()),false); Result(MakeDirectories(nullptr),false);
    std::uint32_t error=99; Result(ChangeDirectory(nullptr,&error),false); Word(error); Result(ChangeDirectory(P("").c_str(),&error),false); Word(error);
    Check(CurrentDirectory(saved,2048)); Result(ChangeDirectory(root.c_str(),&error),true); Word(error);
    Result(CurrentDirectory(buffer,2048)!=0,true); Result(EqualPaths(buffer,root.c_str()),true); Result(ChangeDirectory(absent.c_str(),&error),false); Word(error);
    Result(ChangeDirectory(saved,&error),true); Word(error);
    for(auto query:{CurrentDirectory,ExecutablePath}) {
        const auto length=query(buffer,2048); Check(length); Path expected(buffer); Word(length>0);
        PathChar sentinel='X'; Word(query(&sentinel,0)); Word(sentinel); Word(query(nullptr,2048));
        Word(query(buffer,1)); Word(buffer[0]); Word(query(buffer,length)); Word(buffer[0]);
        Word(query(buffer,length+1)==length); Result(EqualPaths(buffer,expected.c_str()),true);
    }
    Path lexical=Join(root,"one/../moved.bin"); PathChar *leaf=nullptr;
    Check(FullPath(lexical.c_str(),buffer,2048,&leaf)); Result(EqualPaths(buffer,moved.c_str()),true); Check(leaf); Text(leaf);
    leaf=buffer+100; Word(FullPath(lexical.c_str(),buffer,1,&leaf)); Word(leaf==buffer+100);
    const auto leafCount=LookupLeafName(moved.c_str(),buffer,2048); Word(leafCount); Text(buffer);
    Word(LookupLeafName(moved.c_str(),buffer,leafCount)); Word(buffer[0]); Word(LookupLeafName(nullptr,buffer,2048)); Word(buffer[0]);
    Word(FindInPath(P("moved.bin").c_str(),root.c_str(),buffer,2048)>0); Result(EqualPaths(buffer,moved.c_str()),true);
    Word(FindInPath(P("missing.bin").c_str(),root.c_str(),buffer,2048));
    DirectoryHandle directory; DirectoryEntry entry; auto result=First(Join(root,"*").c_str(),&directory,&entry);
    while(result==Iteration::Entry) { Text(entry.name); Word(entry.info.directory); Word(entry.info.hidden); Word(entry.info.system); Word(entry.info.readOnly); Word(entry.info.link); result=Next(directory,&entry); }
    Word(static_cast<unsigned>(result)); CloseDirectory(directory); Result(First(Join(absent,"*").c_str(),&directory,&entry)==Iteration::Error,true); CloseDirectory(directory);
    for(unsigned i:{0u,25u,26u}) { buffer[0]='X'; Word(RootPath(i,buffer,4)); if(i<26) Text(buffer); else Word(buffer[0]); }
    buffer[0]='X'; Word(RootPath(0,buffer,3)); Word(buffer[0]); Word(VolumeKinds(P("").c_str())); Word(VolumeKinds(P("relative").c_str()));
    const auto mode=SuppressMediaErrors(); RestoreMediaErrors(mode);
    Path encoded=Join(root,"encoded-");
#ifdef UNICODE
    encoded+=static_cast<PathChar>(0x3a9);
#else
    encoded+=static_cast<PathChar>(0xe9);
#endif
    encoded+=P(".bin"); handle=Open(encoded.c_str(),create); Check(handle); Close(handle); Metadata(encoded);
    Word(LookupLeafName(encoded.c_str(),buffer,2048)); Text(buffer); Result(Delete(encoded.c_str()),true);
    Result(Delete(moved.c_str()),true); Result(DeleteDirectory(nested.c_str()),true); Result(DeleteDirectory(Join(root,"one").c_str()),true); Result(DeleteDirectory(root.c_str()),true);
    if(trace) Check(!std::fclose(trace)); std::printf("Native file runtime fingerprint: %08X\n",hash);
}
