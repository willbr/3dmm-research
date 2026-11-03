/* Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */
// The backend must work without legacy, Windows, SDL or application headers.
#include "platform_files.h"
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <sys/stat.h>
#ifdef _WIN32
#include <io.h>
#else
#include <unistd.h>
#endif

using namespace KauaiFiles;
using Path = std::basic_string<PathChar>;
static void CheckAt(bool ok, int line)
{
    if (!ok)
    {
        std::fprintf(stderr, "File backend check failed at line %d\n", line);
        std::exit(1);
    }
}
#define Check(ok) CheckAt((ok), __LINE__)
static Path P(const char *text)
{
    Path path;
    while (*text)
        path += static_cast<PathChar>(*text++);
    return path;
}
static Path Join(const Path &a, const char *b)
{
    return a + (IsSeparator(a.back()) ? Path{} : Path(1, Separator())) + P(b);
}
static FileHandle Create(const Path &path)
{
    OpenOptions options;
    options.write = true;
    options.creation = Creation::CreateExclusive;
    return Open(path.c_str(), options);
}
static void ReadOnly(const Path &path, bool value)
{
#ifdef _WIN32
#ifdef UNICODE
    Check(_wchmod(reinterpret_cast<const wchar_t *>(path.c_str()), value ? _S_IREAD : _S_IREAD | _S_IWRITE) == 0);
#else
    Check(_chmod(path.c_str(), value ? _S_IREAD : _S_IREAD | _S_IWRITE) == 0);
#endif
#else
    Check(chmod(path.c_str(), value ? 0444 : 0644) == 0);
#endif
}
static void CheckBytes(const Path &path, const char *expected, unsigned count)
{
    FileHandle file = Open(path.c_str(), OpenOptions{});
    Check(file != nullptr);
    char data[16] = {};
    Check(count <= sizeof(data) && Read(file, data, count) && !std::memcmp(data, expected, count));
    Close(file);
}
int main()
{
    PathChar buffer[1024];
    for (auto query : {CurrentDirectory, ExecutablePath})
    {
        const auto length = query(buffer, 1024);
        Check(length && length == std::char_traits<PathChar>::length(buffer));
        const Path path(buffer);
        PathInfo info;
        Check(Info(path.c_str(), &info) == Status::Found);
        Check(info.directory == (query == CurrentDirectory));
        Check(!query(nullptr, 1024));
        PathChar untouched = static_cast<PathChar>('x');
        Check(!query(&untouched, 0) && untouched == static_cast<PathChar>('x'));
        Check(!query(buffer, 1) && buffer[0] == 0);
        Check(!query(buffer, length) && buffer[0] == 0);
        Check(query(buffer, length + 1) == length && EqualPaths(buffer, path.c_str()));
        if (query == CurrentDirectory)
        {
            Check(FullPath(P(".").c_str(), buffer, 1024) != 0);
            Check(EqualPaths(buffer, path.c_str()));
        }
    }
    Check(TempPath(buffer, 1024) != 0 && IsSeparator(buffer[std::char_traits<PathChar>::length(buffer) - 1]));
    const Path temp(buffer);
    Path root;
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    bool created = false;
    for (unsigned i = 0; i < 100 && !created; ++i)
    {
        root = Join(temp, ("3mm-file-tests-" + std::to_string(stamp) + "-" + std::to_string(i)).c_str());
        created = MakeDirectory(root.c_str());
    }
    Check(created);
    const Path file = Join(root, "path space.bin");
    const Path renamed = Join(root, "renamed.bin");
    const Path nested = Join(root, "nested");
    Check(MakeDirectory(nested.c_str()));
    Check(!MakeDirectory(nested.c_str()));
    const Path tree = Join(root, "parents/child/grandchild");
    Check(MakeDirectories(tree.c_str()) && MakeDirectories(tree.c_str()));
    Check(DeleteDirectory(tree.c_str()));
    Check(DeleteDirectory(Join(root, "parents/child").c_str()));
    Check(DeleteDirectory(Join(root, "parents").c_str()));
    Check(!MakeDirectories(nullptr) && !MakeDirectories(P("").c_str()));
    Check(CurrentDirectory(buffer, 1024) != 0);
    const Path savedDirectory(buffer);
    std::uint32_t directoryError = 99;
    Check(ChangeDirectory(nested.c_str(), &directoryError) && directoryError == 0);
    Check(!MakeDirectories(P("relative-test-directory").c_str()));
    Check(!ChangeDirectory(Join(root, "missing").c_str(), &directoryError) && directoryError != 0);
    Check(!ChangeDirectory(nullptr, &directoryError) && directoryError != 0);
    auto marker = Create(P("cwd-marker"));
    Check(marker != nullptr);
    Close(marker);
    Check(Delete(Join(nested, "cwd-marker").c_str()));
    Check(ChangeDirectory(savedDirectory.c_str(), &directoryError) && directoryError == 0);
    Check(CurrentDirectory(buffer, 1024) != 0 && EqualPaths(buffer, savedDirectory.c_str()));
    const Path lexical = nested + Separator() + P("..") + Separator() + P("path space.bin");
    Check(FullPath(lexical.c_str(), buffer, 1024) != 0 && EqualPaths(buffer, file.c_str()));
    Check(!FullPath(file.c_str(), buffer, file.size())); // No silent truncation.
    Check(FullPath(file.c_str(), buffer, file.size() + 1) == file.size());
    PathInfo info;
    Check(Info(nested.c_str(), &info) == Status::Found && info.directory);
    Check(Info(file.c_str(), &info) == Status::Missing);

    FileHandle writer = Create(file);
    Check(writer != nullptr);
    Check(Write(writer, "abcdef", 6));
    std::uint32_t length;
    Check(Size(writer, &length) && length == 6);
    Check(Seek(writer, 2) && Write(writer, "XY", 2));
    Check(Seek(writer, 0));
    char data[16] = {};
    Check(Read(writer, data, 6) && !std::memcmp(data, "abXYef", 6));
    Check(!Read(writer, data, 1)); // Short reads do not report success.
    Check(!Seek(writer, 0x80000000));
    Check(Resize(writer, 3) && Size(writer, &length) && length == 3);
    Check(Resize(writer, 8) && Seek(writer, 3) && Write(writer, "12345", 5));
    Check(Flush(writer));
    Check(Create(file) == nullptr); // Exclusive create never truncates.
    OpenOptions readSharingWriter;
    readSharingWriter.denyWrite = false;
    FileHandle reader = Open(file.c_str(), readSharingWriter);
#ifdef _WIN32
    Check(reader != nullptr); // Preserve Windows read-sharing while writing.
    Close(reader);
#else
    Check(reader == nullptr); // POSIX backend deliberately takes a conservative lock.
#endif
    Check(Open(file.c_str(), OpenOptions{}) == nullptr);
    Close(writer);
    CheckBytes(file, "abX12345", 8);
    Check(!MakeDirectories(file.c_str()) && !MakeDirectories(Join(file, "child").c_str()));
    Check(!ChangeDirectory(file.c_str(), &directoryError) && directoryError != 0);
    CheckBytes(file, "abX12345", 8);

    // Leaf lookup preserves the spelling stored on disk and never truncates.
    const Path leaf = P("path space.bin");
    Check(LookupLeafName(file.c_str(), buffer, 1024) == leaf.size() && Path(buffer) == leaf);
    Check(!LookupLeafName(file.c_str(), buffer, leaf.size()) && buffer[0] == 0);
    Check(LookupLeafName(file.c_str(), buffer, leaf.size() + 1) == leaf.size() && Path(buffer) == leaf);
    Check(!LookupLeafName(file.c_str(), nullptr, 1024));
    PathChar sentinel = static_cast<PathChar>('x');
    Check(!LookupLeafName(file.c_str(), &sentinel, 0) && sentinel == static_cast<PathChar>('x'));
    Check(!LookupLeafName(nullptr, buffer, 1024) && buffer[0] == 0);
    Check(!LookupLeafName(P("").c_str(), buffer, 1024) && buffer[0] == 0);
    Check(!LookupLeafName(renamed.c_str(), buffer, 1024) && buffer[0] == 0);
    Check(LookupLeafName(nested.c_str(), buffer, 1024) == 6 && Path(buffer) == P("nested"));
    Check(LookupLeafName(lexical.c_str(), buffer, 1024) == leaf.size() && Path(buffer) == leaf);
#ifdef _WIN32
    Check(LookupLeafName(Join(root, "PATH SPACE.BIN").c_str(), buffer, 1024) == leaf.size() && Path(buffer) == leaf);
    Check(LookupLeafName(Join(root, "path*.bin").c_str(), buffer, 1024) == leaf.size() && Path(buffer) == leaf);
    // This isolated directory has no alias collisions; some volumes disable
    // short-name creation entirely, so only exercise an alias when it exists.
    const Path shortPath = Join(root, "PATHSP~1.BIN");
    if (Info(shortPath.c_str(), &info) == Status::Found)
    {
        Check(LookupLeafName(shortPath.c_str(), buffer, 1024) == leaf.size() && Path(buffer) == leaf);
        std::puts("Short filename expansion passed.");
    }
    else
        std::puts("Short filename expansion skipped: fixture alias unavailable.");
    // Native lookup accepts the trailing quote left by legacy startup parsing.
    Check(LookupLeafName((file + P("\"")).c_str(), buffer, 1024) == leaf.size() && Path(buffer) == leaf);
#else
    Check(!LookupLeafName(Join(root, "path*.bin").c_str(), buffer, 1024) && buffer[0] == 0);
#endif

    OpenOptions denyRead;
    denyRead.denyRead = true;
    reader = Open(file.c_str(), denyRead);
    Check(reader != nullptr && Open(file.c_str(), OpenOptions{}) == nullptr);
    Close(reader);

    reader = Open(file.c_str(), OpenOptions{});
    FileHandle secondReader = Open(file.c_str(), OpenOptions{});
    Check(reader != nullptr && secondReader != nullptr);
    Check(!Delete(file.c_str()) && !Rename(file.c_str(), renamed.c_str()));
    OpenOptions overwrite;
    overwrite.write = true;
    overwrite.creation = Creation::CreateAlways;
    Check(Open(file.c_str(), overwrite) == nullptr); // A denied create must not truncate.
    Check(!Write(reader, "bad", 3) && !Resize(reader, 0));
    Close(secondReader);
    Close(reader);
    CheckBytes(file, "abX12345", 8);

    writer = Create(renamed);
    Check(writer != nullptr && Write(writer, "other", 5));
    Close(writer);
    Check(!Rename(file.c_str(), renamed.c_str()));
    CheckBytes(file, "abX12345", 8);
    CheckBytes(renamed, "other", 5);
    Check(Delete(renamed.c_str()));
    ReadOnly(file, true);
    Check(Info(file.c_str(), &info) == Status::Found && info.readOnly);
    Check(!Rename(file.c_str(), renamed.c_str()) && !Delete(file.c_str()));
    ReadOnly(file, false);
    Check(Rename(file.c_str(), renamed.c_str()));
    Check(Info(file.c_str(), &info) == Status::Missing);
    CheckBytes(renamed, "abX12345", 8);
    Check(FindInPath(P("renamed.bin").c_str(), root.c_str(), buffer, 1024) != 0 && EqualPaths(buffer, renamed.c_str()));
    Check(!FindInPath(P("missing.bin").c_str(), root.c_str(), buffer, 1024));

    DirectoryHandle directory;
    DirectoryEntry entry;
    const Path pattern = root + Separator() + P("*");
    Iteration result = First(pattern.c_str(), &directory, &entry);
    unsigned files = 0, directories = 0;
    while (result == Iteration::Entry)
    {
        const Path name(entry.name);
        if (name != P(".") && name != P(".."))
        {
            if (entry.info.directory)
            {
                Check(name == P("nested"));
                ++directories;
            }
            else
            {
                Check(name == P("renamed.bin"));
                ++files;
            }
        }
        result = Next(directory, &entry);
    }
    Check(result == Iteration::End && files == 1 && directories == 1);
    CloseDirectory(directory);
    const Path emptyPattern = nested + Separator() + P("*");
    result = First(emptyPattern.c_str(), &directory, &entry);
    while (result == Iteration::Entry)
    {
        Check(Path(entry.name) == P(".") || Path(entry.name) == P(".."));
        result = Next(directory, &entry);
    }
    Check(result == Iteration::End);
    CloseDirectory(directory);
    Check(First(Join(root, "missing/*").c_str(), &directory, &entry) == Iteration::Error);
    CloseDirectory(directory);

    const auto roots = RootMask();
    Check(roots != 0);
    for (unsigned i = 0; i < 26; ++i)
        if (roots & (1u << i))
            Check(RootPath(i, buffer, 1024) && IsSeparator(buffer[std::char_traits<PathChar>::length(buffer) - 1]));
#ifdef _WIN32
    Check(EqualPaths(P("C:\\Example").c_str(), P("c:\\example").c_str()));
#else
    Check(!EqualPaths("/Example", "/example") && !IsSeparator('\\'));
    const Path backslash = Join(root, "literal\\name");
    writer = Create(backslash);
    Check(writer != nullptr);
    Close(writer);
    Check(Delete(backslash.c_str()));
    const Path dangling = Join(root, "dangling");
    const Path movedLink = Join(root, "moved-link");
    Check(symlink("missing-target", dangling.c_str()) == 0);
    Check(Info(dangling.c_str(), &info) == Status::Found && info.link && !info.directory);
    Check(Rename(dangling.c_str(), movedLink.c_str()));
    Check(Info(movedLink.c_str(), &info) == Status::Found && info.link);
    Check(Delete(movedLink.c_str()));
    const Path directoryLink = Join(root, "directory-link");
    Check(symlink(nested.c_str(), directoryLink.c_str()) == 0);
    Check(Info(directoryLink.c_str(), &info) == Status::Found && info.link && info.directory);
    Check(unlink(directoryLink.c_str()) == 0);
#endif
    Check(Delete(renamed.c_str()));
    Check(DeleteDirectory(nested.c_str()));
    Check(DeleteDirectory(root.c_str()));
    return 0;
}
