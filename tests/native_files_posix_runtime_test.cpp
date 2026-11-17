#include "platform_files.h"
#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>
using namespace KauaiFiles;
static std::FILE *trace;
static unsigned hash = 2166136261u;
static std::string root;
static int interruptedRead, interruptedWrite, interruptedSync, interruptedOpen, interruptedLock;
static bool shortIO;
extern "C" {
ssize_t __real_read(int,void *,size_t);
ssize_t __real_write(int,const void *,size_t);
int __real_fsync(int);
int __real_open(const char *,int,...);
int __real_flock(int,int);
ssize_t __wrap_read(int fd,void *p,size_t n)
{if(interruptedRead){--interruptedRead;errno=EINTR;return -1;}return __real_read(fd,p,shortIO&&n>2?2:n);}
ssize_t __wrap_write(int fd,const void *p,size_t n)
{if(interruptedWrite){--interruptedWrite;errno=EINTR;return -1;}return __real_write(fd,p,shortIO&&n>2?2:n);}
int __wrap_fsync(int fd)
{if(interruptedSync){--interruptedSync;errno=EINTR;return -1;}return __real_fsync(fd);}
int __wrap_open(const char *p,int flags,...)
{if(interruptedOpen){--interruptedOpen;errno=EINTR;return -1;}return __real_open(p,flags,0666);}
int __wrap_flock(int fd,int flags)
{if(interruptedLock){--interruptedLock;errno=EINTR;return -1;}return __real_flock(fd,flags);}
}
static void CheckAt(bool ok,int line)
{if(!ok){std::fprintf(stderr,"POSIX files: %d (errno %d)\n",line,errno);std::exit(1);}}
#define Check(x) CheckAt(!!(x),__LINE__)
static void Word(unsigned n)
{for(unsigned i=0;i<4;++i){auto b=static_cast<unsigned char>(n>>(8*i));hash=(hash^b)*16777619u;if(trace)Check(std::fputc(b,trace)!=EOF);}}
static void Text(std::string value)
{if(value==root.substr(root.find_last_of('/')+1))value="<root-leaf>";
 if(value.compare(0,root.size(),root)==0)value.replace(0,root.size(),"<root>");for(unsigned char c:value)Word(c);Word(0);}
static void Metadata(const PathInfo &p) {Word(p.directory);Word(p.hidden);Word(p.system);Word(p.readOnly);Word(p.link);}
static PathInfo Sentinel() {PathInfo p;p.directory=p.hidden=p.system=p.readOnly=p.link=true;return p;}
static void Metadata(const char *path)
{auto p=Sentinel();Word(static_cast<unsigned>(Info(path,&p)));Metadata(p);}
static FileHandle Create(const char *path)
{OpenOptions o;o.write=true;o.creation=Creation::CreateExclusive;auto h=Open(path,o);Check(h);return h;}
static void Result(bool value,bool expected) {Word(value);Check(value==expected);}
int main()
{
    auto snapshot=std::getenv("MM_NATIVE_FILES_SNAPSHOT");trace=snapshot?std::fopen(snapshot,"wb"):nullptr;Check(!snapshot||trace);
    char pattern[]="/tmp/mm-native-posix-XXXXXX",saved[8192],buffer[8192];Check(mkdtemp(pattern));root=pattern;
    Check(CurrentDirectory(saved,sizeof saved));Check(ChangeDirectory(root.c_str()));
    Result(IsSeparator('/'),true);Result(IsSeparator('\\'),false);Word(Separator());Result(EqualPaths("Aa","aa"),false);
    Result(EqualPaths("a/b","a\\b"),false);Word(RootMask());Word(VolumeKinds("/"));Word(SuppressMediaErrors());RestoreMediaErrors(0);
    for(unsigned index:{0u,1u,25u,26u})for(size_t capacity:{0u,1u,2u,4u})
    {std::memset(buffer,'X',8);Word(RootPath(index,buffer,capacity));for(unsigned i=0;i<8;++i)Word(buffer[i]);}
    auto file=Create("file.bin");interruptedWrite=2;shortIO=true;Result(Write(file,"abcdef",6),true);Check(!interruptedWrite);
    interruptedSync=2;Result(Flush(file),true);Check(!interruptedSync);Result(Seek(file,0),true);
    interruptedRead=2;char bytes[8];std::memset(bytes,'X',8);Result(Read(file,bytes,6),true);Check(!interruptedRead);Check(!std::memcmp(bytes,"abcdef",6));
    for(char c:bytes)Word(c);shortIO=false;
    Result(Seek(file,4),true);std::memset(bytes,'X',8);Result(Read(file,bytes,4),false);for(char c:bytes)Word(c);
    Result(Read(file,nullptr,0),true);Result(Write(file,nullptr,0),true);
    Result(Seek(file,0x80000000u),false);Result(Resize(file,0x80000000u),false);
    Result(Resize(file,3),true);std::uint32_t size=99;Result(Size(file,&size),true);Word(size);Close(file);Close(nullptr);
    size=99;Result(Size(nullptr,&size),false);Word(size);
    interruptedOpen=2;interruptedLock=2;auto reader=Open("file.bin",OpenOptions{});Check(reader&&!interruptedOpen&&!interruptedLock);
    auto other=Open("file.bin",OpenOptions{});Check(other);Close(other);
    Result(Resize(reader,1),false);Result(Write(reader,"bad",3),false);Result(Read(reader,bytes,2),true);Word(bytes[0]);Word(bytes[1]);
    Result(Delete("file.bin"),false);Result(Rename("file.bin","moved.bin"),false);
    OpenOptions overwrite;overwrite.write=true;overwrite.creation=Creation::CreateAlways;Result(Open("file.bin",overwrite)!=nullptr,false);
    Close(reader);reader=Open("file.bin",OpenOptions{});Check(reader);Check(Size(reader,&size));Word(size);Check(size==3);Close(reader);
    Result(Rename("file.bin","moved.bin"),true);file=Create("collision");Close(file);Result(Rename("moved.bin","collision"),false);
    Check(chmod("moved.bin",0444)==0);Metadata("moved.bin");Result(Delete("moved.bin"),false);Result(Rename("moved.bin","other"),false);Check(chmod("moved.bin",0644)==0);
    Check(symlink("moved.bin",".link")==0);Check(symlink("missing","dangling")==0);Check(symlink("loop","loop")==0);
    Check(MakeDirectories((root+"/one/two").c_str()));Check(symlink("one","dir-link")==0);
    for(const char *path:{"missing","missing/child","moved.bin/child",".","..",".link","dangling","loop","dir-link"})Metadata(path);
    Result(Rename("dangling","renamed-link"),true);Metadata("renamed-link");Result(Delete("renamed-link"),true);
    Result(Delete("dir-link"),false);Result(Delete("one"),false);Result(Rename("one","other"),false);
    Result(MakeDirectories((root+"/one//two/./").c_str()),true);Result(MakeDirectories("relative"),false);Result(MakeDirectories(nullptr),false);
    Result(MakeDirectories((root+"/moved.bin/child").c_str()),false);
    struct stat status;Check(stat("one/two",&status)==0);Word(status.st_mode&0777);Check((status.st_mode&0777)==0700);
    file=Create("back\\slash");Close(file);file=Create("utf8-\xc3\xa9.bin");Close(file);
    for(const char *a:{"/","//","a",".","..","a/..","../","a\\b","dir-link/../x","/../../x","//a///b/../",""})
    for(const char *b:{"","/","/.","/..","/../new","///","/./../"})
    {
        auto path=std::string(a)+b;char *leaf=buffer+7000;
        std::memset(buffer,'X',sizeof buffer);auto n=FullPath(path.c_str(),buffer,sizeof buffer,&leaf);Word(n!=0);
        if(n){Text(buffer);Word(leaf==nullptr);if(leaf)Text(leaf);}else Word(buffer[0]);
        std::strcpy(buffer,path.c_str());Word(FullPath(buffer,buffer,sizeof buffer)!=0);if(buffer[0]&&n)Text(buffer);
        std::memset(buffer,'X',8);leaf=buffer+7000;Word(FullPath(path.c_str(),buffer,1,&leaf));Word(leaf==buffer+7000);for(unsigned i=0;i<8;++i)Word(buffer[i]);
    }
    char *leaf=buffer+7000;buffer[0]='X';Word(FullPath(nullptr,buffer,sizeof buffer,&leaf));Word(buffer[0]);Word(leaf==buffer+7000);
    for(const char *path:{"moved.bin","dangling",".link","loop","utf8-\xc3\xa9.bin","one/","missing","*","back\\slash"})
    {auto n=LookupLeafName(path,buffer,sizeof buffer);Word(n);Text(buffer);if(n){Word(LookupLeafName(path,buffer,n));Word(buffer[0]);}}
    for(const char *search:{"","missing:",":missing","missing::one","one:.","."})
    {buffer[0]='X';Word(FindInPath("moved.bin",search,buffer,sizeof buffer)!=0);Text(buffer);buffer[0]='X';Word(FindInPath("moved.bin",search,buffer,1));Word(buffer[0]);}
    Check(setenv("PATH","missing:.",1)==0);Check(FindInPath("moved.bin",nullptr,buffer,sizeof buffer));Text(buffer);
    Word(FindInPath("one",nullptr,buffer,sizeof buffer));Word(FindInPath("./moved.bin","missing",buffer,sizeof buffer)!=0);Text(buffer);
    Check(unsetenv("PATH")==0);Check(FindInPath("moved.bin",nullptr,buffer,sizeof buffer));Text(buffer);
    for(const char *configured:{"","/tmp","/tmp///","one/../","/does-not-exist/..","relative"})
    {Check(setenv("TMPDIR",configured,1)==0);auto n=TempPath(buffer,sizeof buffer);Word(n!=0);Text(buffer);buffer[0]='X';Word(TempPath(buffer,1));Word(buffer[0]);}
    Check(unsetenv("TMPDIR")==0);Check(TempPath(buffer,sizeof buffer));Text(buffer);
    std::uint32_t error=99;Result(ChangeDirectory(nullptr,&error),false);Word(error);Result(ChangeDirectory("",&error),false);Word(error);
    Result(ChangeDirectory("missing",&error),false);Word(error);Check(CurrentDirectory(buffer,sizeof buffer));Text(buffer);
    for(auto query:{CurrentDirectory,ExecutablePath})
    {auto n=query(buffer,sizeof buffer);Check(n);Word(n!=0);buffer[0]='X';Word(query(buffer,0));Word(buffer[0]);Word(query(nullptr,sizeof buffer));
     Word(query(buffer,n));Word(buffer[0]);Word(query(buffer,n+1)==n);Word(query(buffer,1));Word(buffer[0]);}
    DirectoryHandle directory;DirectoryEntry entry;std::vector<std::string> entries;
    entry.info=Sentinel();std::memset(entry.name,'X',sizeof entry.name);auto iteration=First("./*",&directory,&entry);Check(directory);
    while(iteration!=Iteration::End)
    {
        auto &p=entry.info;entries.push_back(std::string(entry.name)+":"+std::to_string(static_cast<unsigned>(iteration))+":"+
            std::to_string(p.directory)+std::to_string(p.hidden)+std::to_string(p.system)+std::to_string(p.readOnly)+std::to_string(p.link));
        Check(entry.name[300]=='X');entry.info=Sentinel();iteration=Next(directory,&entry);
    }
    Word(static_cast<unsigned>(iteration));CloseDirectory(directory);std::sort(entries.begin(),entries.end());for(auto &e:entries)Text(e);
    for(const char *p:{"","*","missing/*","./","file*"})
    {entry.info=Sentinel();std::strcpy(entry.name,"sentinel");directory=reinterpret_cast<void *>(1);Word(static_cast<unsigned>(First(p,&directory,&entry)));Word(directory==nullptr);Text(entry.name);Metadata(entry.info);CloseDirectory(directory);}
    entry.info=Sentinel();std::strcpy(entry.name,"sentinel");Word(static_cast<unsigned>(Next(nullptr,&entry)));Metadata(entry.info);Text(entry.name);CloseDirectory(nullptr);
    /* Force getcwd's initial 256-byte allocation to grow, without OS path limits. */
    std::string deep=root;std::vector<std::string> parents;
    for(unsigned i=0;i<7;++i){deep+="/"+std::string(70,'a'+i);Check(MakeDirectory(deep.c_str()));parents.push_back(deep);}
    Check(ChangeDirectory(deep.c_str()));Check(FullPath("../new",buffer,sizeof buffer));Text(buffer);Check(ChangeDirectory(root.c_str()));
    for(auto i=parents.rbegin();i!=parents.rend();++i)Check(DeleteDirectory(i->c_str()));
    for(const char *p:{"moved.bin","collision",".link","back\\slash","utf8-\xc3\xa9.bin"})Check(Delete(p));
    Check(unlink("loop")==0);Check(unlink("dir-link")==0);Check(DeleteDirectory("one/two"));Check(DeleteDirectory("one"));
    Check(ChangeDirectory(saved));Check(DeleteDirectory(root.c_str()));
    if(trace)Check(!std::fclose(trace));std::printf("POSIX native runtime fingerprint: %08X\n",hash);return 0;
}
