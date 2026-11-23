#if !defined(_WIN32) && !defined(__APPLE__) && !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L
#endif
#include "mm/native_files.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#ifdef _WIN32
#include <process.h>
#else
#include <errno.h>
#include <sys/types.h>
#include <unistd.h>
#endif
static void check(int ok,int line) {if(!ok){fprintf(stderr,"Native C files: %d\n",line);exit(1);}}
#define CHECK(x) check(!!(x),__LINE__)
static size_t length(const MMNativePathChar *s) {size_t n=0;while(s[n])++n;return n;}
static void text(MMNativePathChar *out,const char *s) {while(*s)*out++=(unsigned char)*s++;*out=0;}
static void join(MMNativePathChar *out,const MMNativePathChar *base,const char *leaf)
{size_t n=length(base);memcpy(out,base,(n+1)*sizeof(*out));if(!mm_native_is_separator(out[n-1]))out[n++]=mm_native_separator();text(out+n,leaf);}
static unsigned process_id(void)
{
#ifdef _WIN32
    return (unsigned)_getpid();
#else
    return (unsigned)getpid();
#endif
}
static void check_utf8_file(void)
{
    const char path[] = "archive-\xf0\x9f\x8e\xac.3mm";
    MMNativeOpenOptions options;
    void *file;
    char bytes[4];
    mm_native_open_options_init(&options);
    options.write = options.deny_read = 1;
    options.creation = MM_NATIVE_CREATE_EXCLUSIVE;
    file = mm_native_file_open_utf8(path, &options);
    CHECK(file && mm_native_file_write(file, "CHN2", sizeof(bytes)));
    mm_native_open_options_init(&options);
    CHECK(!mm_native_file_open_utf8(path, &options));
    mm_native_file_close(file);
    file = mm_native_file_open_utf8(path, &options);
    CHECK(file && mm_native_file_read(file, bytes, sizeof(bytes)) && !memcmp(bytes, "CHN2", sizeof(bytes)));
    mm_native_file_close(file);
#ifdef _WIN32
    CHECK(_wremove(L"archive-\xd83c\xdfac.3mm") == 0);
    options.write = 1;
    options.creation = MM_NATIVE_CREATE_EXCLUSIVE;
    CHECK(!mm_native_file_open_utf8("bad-\xf0\x9f", &options));
#else
    CHECK(remove(path) == 0);
#endif
}
int main(void)
{
    MMNativePathChar temp[2048], root[2048], file[2048], moved[2048], nested[2048], output[2048], saved[2048], entry[1024];
    MMNativePathChar a[600], b[600], sentinel, *leaf; MMNativeOpenOptions options; MMNativePathInfo info;
    void *handle,*reader,*iterator; MMU32 size,error; size_t n,i; int status,entries=0; char suffix[100],bytes[8];
#ifndef _WIN32
    int saved_input;
    off_t large_size;
#endif
    CHECK(mm_native_temp_path(temp,2048));
    sprintf(suffix,"native-c-%lu-%u",(unsigned long)time(NULL),process_id());join(root,temp,suffix);
    CHECK(mm_native_make_directory(root));join(file,root,"file.bin");join(moved,root,"renamed.bin");join(nested,root,"child");
    mm_native_open_options_init(&options);CHECK(!options.write&&!options.deny_read&&options.deny_write&&options.creation==0);
    options.write=1;options.creation=MM_NATIVE_CREATE_EXCLUSIVE;handle=mm_native_file_open(file,&options);CHECK(handle);
    CHECK(!mm_native_file_open(file,&options));CHECK(mm_native_file_write(handle,"abcdef",6)&&mm_native_file_flush(handle));
    CHECK(mm_native_file_size(handle,&size)&&size==6);CHECK(mm_native_file_seek(handle,4));memset(bytes,'X',8);
    CHECK(!mm_native_file_read(handle,bytes,4)&&bytes[0]=='e'&&bytes[1]=='f'&&bytes[2]=='X');
    CHECK(!mm_native_file_seek(handle,0x80000000UL)&&!mm_native_file_resize(handle,0x80000000UL));
    CHECK(mm_native_file_resize(handle,3)&&mm_native_file_size(handle,&size)&&size==3);mm_native_file_close(handle);mm_native_file_close(NULL);
    mm_native_open_options_init(&options);reader=mm_native_file_open(file,&options);CHECK(reader);
    CHECK(!mm_native_file_write(reader,"bad",3)&&!mm_native_file_resize(reader,1));CHECK(mm_native_file_read(reader,bytes,2));
#ifdef _WIN32
    CHECK(bytes[0]=='b'&&bytes[1]=='c');
#else
    CHECK(bytes[0]=='a'&&bytes[1]=='b');
#endif
    CHECK(!mm_native_path_delete(file)&&!mm_native_path_rename(file,moved));mm_native_file_close(reader);
    CHECK(mm_native_path_rename(file,moved));info.directory=info.hidden=info.system=info.read_only=info.link=17;
    CHECK(mm_native_path_info(file,&info)==MM_NATIVE_MISSING&&info.directory==17&&info.link==17);
    CHECK(mm_native_path_info(moved,&info)==MM_NATIVE_FOUND&&!info.directory&&!info.read_only);
    CHECK(mm_native_make_directories(nested)&&mm_native_make_directories(nested));CHECK(!mm_native_make_directories(moved));
    text(output,"relative");CHECK(!mm_native_make_directories(output)&&!mm_native_make_directories(NULL));
    CHECK(!mm_native_change_directory(NULL,&error));
#ifdef _WIN32
    CHECK(error==87);
#else
    CHECK(error==EINVAL);
#endif
    CHECK(mm_native_current_directory(saved,2048));
    CHECK(mm_native_change_directory(root,&error)&&!error&&mm_native_current_directory(output,2048)&&mm_native_paths_equal(output,root));
    check_utf8_file();
    CHECK(mm_native_change_directory(saved,&error)&&!error);
    n=mm_native_executable_path(output,2048);CHECK(n);sentinel='X';CHECK(!mm_native_executable_path(&sentinel,0)&&sentinel=='X');
    CHECK(!mm_native_executable_path(output,n)&&!output[0]);CHECK(mm_native_executable_path(output,n+1)==n);
    CHECK(!mm_native_current_directory(output,1)&&!output[0]);
    leaf=NULL;CHECK(mm_native_full_path(moved,output,2048,&leaf)&&leaf);text(file,"renamed.bin");CHECK(mm_native_paths_equal(leaf,file));
    leaf=output+100;CHECK(!mm_native_full_path(moved,output,1,&leaf)&&leaf==output+100);
    n=mm_native_lookup_leaf(moved,output,2048);CHECK(n==11);CHECK(!mm_native_lookup_leaf(moved,output,n)&&!output[0]);
    CHECK(mm_native_find_path(file,root,output,2048)&&mm_native_paths_equal(output,moved));
    for(i=0;i<599;++i){a[i]='a';b[i]='A';}a[599]=b[599]=0;
#ifdef _WIN32
    CHECK(mm_native_paths_equal(a,b));
#else
    CHECK(!mm_native_paths_equal(a,b));memcpy(b,a,sizeof(a));CHECK(mm_native_paths_equal(a,b));
#endif
    b[510]='b';CHECK(!mm_native_paths_equal(a,b));
    join(output,root,"*");for(i=0;i<1024;++i)entry[i]='X';
    status=mm_native_directory_first(output,&iterator,entry,&info);
    while(status==MM_NATIVE_ENTRY){CHECK(entry[300]=='X');++entries;status=mm_native_directory_next(iterator,entry,&info);}
    CHECK(status==MM_NATIVE_END&&entries>=2);mm_native_directory_close(iterator);mm_native_directory_close(NULL);
    output[0]='X';CHECK(!mm_native_root_path(26,output,2048)&&output[0]=='X');
#ifdef _WIN32
    CHECK(mm_native_root_path(25,output,4)&&output[0]=='Z');text(output,"");CHECK(mm_native_volume_kinds(output)==2&&mm_native_root_mask());
#else
    CHECK(!mm_native_root_path(25,output,4)&&output[0]=='X');CHECK(mm_native_root_path(0,output,2)&&output[0]=='/');
    CHECK(mm_native_volume_kinds(output)==0&&mm_native_root_mask()==1);
#endif
    error=mm_native_suppress_media_errors();mm_native_restore_media_errors(error);
#ifndef _WIN32
    mm_native_open_options_init(&options);
    if(sizeof(off_t)>4)
    {
        large_size=2147483647L;large_size+=1;CHECK(truncate(moved,large_size)==0);
        handle=mm_native_file_open(moved,&options);CHECK(handle);
        size=99;CHECK(!mm_native_file_size(handle,&size)&&size==99);CHECK(mm_native_file_seek(handle,2147483647UL));mm_native_file_close(handle);
    }
    saved_input=dup(STDIN_FILENO);CHECK(saved_input>=0&&close(STDIN_FILENO)==0);
    handle=mm_native_file_open(moved,&options);CHECK(handle&&mm_native_file_seek(handle,0));mm_native_file_close(handle);
    CHECK(dup2(saved_input,STDIN_FILENO)==STDIN_FILENO);CHECK(close(saved_input)==0);
#endif
    CHECK(mm_native_path_delete(moved)&&mm_native_delete_directory(nested)&&mm_native_delete_directory(root));
    puts("C89 native files: handles, publication, paths and iteration passed");return 0;
}
