/* Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */
#ifndef PLATFORM_FILES_H
#define PLATFORM_FILES_H
#include <cstddef>
#include <cstdint>

namespace KauaiFiles
{
#ifdef UNICODE
using PathChar = std::uint16_t;
#else
using PathChar = char;
#endif
using FileHandle = void *;
using DirectoryHandle = void *;
enum class Creation
{
    OpenExisting,
    CreateAlways,
    CreateExclusive
};
struct OpenOptions
{
    bool write = false;
    bool denyRead = false;
    bool denyWrite = true;
    Creation creation = Creation::OpenExisting;
};

// Native names: existing ANSI/UTF-16 Windows paths; byte paths on POSIX.
// No conversion of movie strings or stored text takes place here.
FileHandle Open(const PathChar *path, const OpenOptions &options);
void Close(FileHandle handle);
bool Flush(FileHandle handle);
bool Seek(FileHandle handle, std::uint32_t position);
bool Resize(FileHandle handle, std::uint32_t bytes);
bool Size(FileHandle handle, std::uint32_t *bytes);
bool Read(FileHandle handle, void *data, std::uint32_t bytes);
bool Write(FileHandle handle, const void *data, std::uint32_t bytes);

struct PathInfo
{
    bool directory = false;
    bool hidden = false;
    bool system = false;
    bool readOnly = false;
    bool link = false;
};
enum class Status
{
    Found,
    Missing,
    Error
};
Status Info(const PathChar *path, PathInfo *info);
bool Delete(const PathChar *path);
// Never replaces a destination. POSIX fallback uses link/unlink for files.
bool Rename(const PathChar *source, const PathChar *destination);
bool MakeDirectory(const PathChar *path);
// Absolute native path; creates missing parents and accepts existing directories.
// A file in the path is an error. Existing permissions are not changed.
bool MakeDirectories(const PathChar *path);
bool DeleteDirectory(const PathChar *path);
bool EqualPaths(const PathChar *a, const PathChar *b);
bool IsSeparator(PathChar ch);
PathChar Separator();
// Length excluding the terminator, or 0 on failure/insufficient capacity.
std::size_t FullPath(const PathChar *input, PathChar *output, std::size_t capacity, PathChar **leaf = nullptr);
std::size_t FindInPath(const PathChar *input, const PathChar *search, PathChar *output, std::size_t capacity);
std::size_t TempPath(PathChar *output, std::size_t capacity);
// Absolute process paths; length excludes the terminator. Invalid arguments,
// native failure or insufficient capacity return 0 and clear nonempty output.
std::size_t CurrentDirectory(PathChar *output, std::size_t capacity);
// Changes the process working directory. Optional error is zero on success,
// otherwise a native Win32/errno diagnostic code. Failure preserves the cwd.
bool ChangeDirectory(const PathChar *path, std::uint32_t *error = nullptr);
std::size_t ExecutablePath(PathChar *output, std::size_t capacity);
// Leaf name for an existing path. Windows returns on-disk spelling, expanding
// short names and retaining native first-match lookup. POSIX checks existence
// and returns the supplied leaf spelling, treating the path literally.
// Input/output must not overlap. Failure or insufficient capacity returns 0
// and clears nonempty output. Does not resolve or rewrite parent directories.
std::size_t LookupLeafName(const PathChar *path, PathChar *output, std::size_t capacity);
// Bit positions enumerate native roots: Windows A-Z, POSIX bit 0 is '/'.
std::uint32_t RootMask();
bool RootPath(unsigned index, PathChar *output, std::size_t capacity);
// Matches Filename's legacy floppy/network/CD/removable bits (1/2/4/8).
std::uint32_t VolumeKinds(const PathChar *path);
// Preserve the legacy process-wide suppression while probing removable media.
// POSIX needs no error-dialog suppression and returns a zero token.
std::uint32_t SuppressMediaErrors();
void RestoreMediaErrors(std::uint32_t token);

struct DirectoryEntry
{
    PathChar name[1024] = {};
    PathInfo info;
};
enum class Iteration
{
    Entry,
    End,
    Error
};
// Pattern must be a directory followed by '*'. Handles are explicitly owned,
// so legacy enumerator stacks can transfer them without copying C++ objects.
Iteration First(const PathChar *pattern, DirectoryHandle *handle, DirectoryEntry *entry);
Iteration Next(DirectoryHandle handle, DirectoryEntry *entry);
void CloseDirectory(DirectoryHandle handle);
} // namespace KauaiFiles
#endif
