/* Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */
#include "platform_files.h"
#include "mm/native_files.h"
namespace {
MMNativePathInfo NativeInfo(const KauaiFiles::PathInfo &value)
{return {value.directory,value.hidden,value.system,value.readOnly,value.link};}
void Info(const MMNativePathInfo &value,KauaiFiles::PathInfo *out)
{
    out->directory=value.directory!=0; out->hidden=value.hidden!=0; out->system=value.system!=0;
    out->readOnly=value.read_only!=0; out->link=value.link!=0;
}
}
KauaiFiles::FileHandle KauaiFiles::Open(const PathChar *path,const OpenOptions &options)
{
    const MMNativeOpenOptions native{options.write,options.denyRead,options.denyWrite,static_cast<int>(options.creation)};
    return mm_native_file_open(path,&native);
}
void KauaiFiles::Close(FileHandle h) {mm_native_file_close(h);}
bool KauaiFiles::Flush(FileHandle h) {return mm_native_file_flush(h)!=0;}
bool KauaiFiles::Seek(FileHandle h,std::uint32_t n) {return mm_native_file_seek(h,n)!=0;}
bool KauaiFiles::Resize(FileHandle h,std::uint32_t n) {return mm_native_file_resize(h,n)!=0;}
bool KauaiFiles::Size(FileHandle h,std::uint32_t *n) {return mm_native_file_size(h,n)!=0;}
bool KauaiFiles::Read(FileHandle h,void *p,std::uint32_t n) {return mm_native_file_read(h,p,n)!=0;}
bool KauaiFiles::Write(FileHandle h,const void *p,std::uint32_t n) {return mm_native_file_write(h,p,n)!=0;}
KauaiFiles::Status KauaiFiles::Info(const PathChar *path,PathInfo *out)
{auto value=NativeInfo(*out); const auto status=mm_native_path_info(path,&value); ::Info(value,out); return static_cast<Status>(status);}
bool KauaiFiles::Delete(const PathChar *p) {return mm_native_path_delete(p)!=0;}
bool KauaiFiles::Rename(const PathChar *a,const PathChar *b) {return mm_native_path_rename(a,b)!=0;}
bool KauaiFiles::MakeDirectory(const PathChar *p) {return mm_native_make_directory(p)!=0;}
bool KauaiFiles::MakeDirectories(const PathChar *p) {return mm_native_make_directories(p)!=0;}
bool KauaiFiles::DeleteDirectory(const PathChar *p) {return mm_native_delete_directory(p)!=0;}
bool KauaiFiles::EqualPaths(const PathChar *a,const PathChar *b) {return mm_native_paths_equal(a,b)!=0;}
bool KauaiFiles::IsSeparator(PathChar c) {return mm_native_is_separator(c)!=0;}
KauaiFiles::PathChar KauaiFiles::Separator() {return mm_native_separator();}
std::size_t KauaiFiles::FullPath(const PathChar *p,PathChar *out,std::size_t n,PathChar **leaf) {return mm_native_full_path(p,out,n,leaf);}
std::size_t KauaiFiles::FindInPath(const PathChar *p,const PathChar *search,PathChar *out,std::size_t n) {return mm_native_find_path(p,search,out,n);}
std::size_t KauaiFiles::TempPath(PathChar *out,std::size_t n) {return mm_native_temp_path(out,n);}
std::size_t KauaiFiles::CurrentDirectory(PathChar *out,std::size_t n) {return mm_native_current_directory(out,n);}
bool KauaiFiles::ChangeDirectory(const PathChar *p,std::uint32_t *error) {return mm_native_change_directory(p,error)!=0;}
std::size_t KauaiFiles::ExecutablePath(PathChar *out,std::size_t n) {return mm_native_executable_path(out,n);}
std::size_t KauaiFiles::LookupLeafName(const PathChar *p,PathChar *out,std::size_t n) {return mm_native_lookup_leaf(p,out,n);}
std::uint32_t KauaiFiles::RootMask() {return mm_native_root_mask();}
bool KauaiFiles::RootPath(unsigned i,PathChar *out,std::size_t n) {return mm_native_root_path(i,out,n)!=0;}
std::uint32_t KauaiFiles::VolumeKinds(const PathChar *p) {return mm_native_volume_kinds(p);}
std::uint32_t KauaiFiles::SuppressMediaErrors() {return mm_native_suppress_media_errors();}
void KauaiFiles::RestoreMediaErrors(std::uint32_t n) {mm_native_restore_media_errors(n);}
KauaiFiles::Iteration KauaiFiles::First(const PathChar *p,DirectoryHandle *h,DirectoryEntry *out)
{auto value=NativeInfo(out->info); const auto result=mm_native_directory_first(p,h,out->name,&value); ::Info(value,&out->info); return static_cast<Iteration>(result);}
KauaiFiles::Iteration KauaiFiles::Next(DirectoryHandle h,DirectoryEntry *out)
{auto value=NativeInfo(out->info); const auto result=mm_native_directory_next(h,out->name,&value); ::Info(value,&out->info); return static_cast<Iteration>(result);}
void KauaiFiles::CloseDirectory(DirectoryHandle h) {mm_native_directory_close(h);}
