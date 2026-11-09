#include "three_mm/content_vfs.hpp"
#include <cstdint>
#include <limits>
#include <memory>
#include <stdexcept>
#include <utility>

namespace ThreeMM::Content
{
namespace
{
std::string Fold(std::string value)
{
    for (auto &ch : value)
        if (ch >= 'A' && ch <= 'Z')
            ch += 'a' - 'A';
    return value;
}
void *OpenFile(void *, const char *location)
{
    try
    {
        auto stream = std::make_unique<std::ifstream>(std::filesystem::u8path(location), std::ios::binary);
        return *stream ? stream.release() : nullptr;
    }
    catch (...)
    {
        return nullptr;
    }
}
std::size_t ReadAt(void *, void *handle, ThreeMMVfsPosition position, void *buffer, std::size_t capacity)
{
    auto &stream = *static_cast<std::ifstream *>(handle);
    const auto offset = (std::uint64_t(position.high) << 32) | position.low;
    if (offset > std::uint64_t(std::numeric_limits<std::streamoff>::max()))
        return 0;
    if (capacity > std::uint64_t(std::numeric_limits<std::streamsize>::max()))
        capacity = std::size_t(std::numeric_limits<std::streamsize>::max());
    stream.clear();
    stream.seekg(std::streamoff(offset));
    stream.read(static_cast<char *>(buffer), std::streamsize(capacity));
    return std::size_t(stream.gcount());
}
void CloseFile(void *, void *handle)
{
    delete static_cast<std::ifstream *>(handle);
}
void Mount(ThreeMMVfs *vfs, const std::string &source, const std::vector<File> &files, bool replace)
{
    std::vector<std::string> locations;
    std::vector<ThreeMMVfsMount> mounts;
    locations.reserve(files.size());
    mounts.reserve(files.size());
    for (const auto &file : files)
    {
        if (file.name.find('\0') != std::string::npos || file.physical.empty())
            throw std::runtime_error("Invalid mounted content file: " + file.name);
        locations.push_back(std::filesystem::absolute(file.physical).lexically_normal().u8string());
        mounts.push_back({file.name.c_str(), locations.back().c_str()});
    }
    const auto error = three_mm_vfs_mount(vfs, source.c_str(), mounts.data(), mounts.size(), replace);
    if (error != THREE_MM_VFS_OK)
        throw std::runtime_error(std::string(three_mm_vfs_error(error)) + " in " + source);
}
} // namespace
std::string SourceKey(const std::string &title, const std::string &folder)
{
    std::vector<char> output(title.size() + folder.size() + 32);
    return three_mm_vfs_source_key(title.c_str(), folder.c_str(), output.data(), output.size())
               ? std::string(output.data())
               : std::string();
}
Vfs::Vfs()
{
    const ThreeMMVfsProvider provider{nullptr, OpenFile, ReadAt, CloseFile};
    vfs = three_mm_vfs_create(&provider);
    if (!vfs)
        throw std::bad_alloc();
}
Vfs::~Vfs()
{
    three_mm_vfs_destroy(vfs);
}
Vfs::Vfs(Vfs &&other) noexcept : vfs(std::exchange(other.vfs, nullptr))
{
}
Vfs &Vfs::operator=(Vfs &&other) noexcept
{
    if (this != &other)
    {
        three_mm_vfs_destroy(vfs);
        vfs = std::exchange(other.vfs, nullptr);
    }
    return *this;
}
void Vfs::Mount(const std::string &source, const std::vector<File> &files)
{
    Content::Mount(vfs, source, files, false);
}
void Vfs::Replace(const std::string &source, const std::vector<File> &files)
{
    Content::Mount(vfs, source, files, true);
}
void Vfs::MountDirectory(const std::string &source, const std::filesystem::path &directory)
{
    std::vector<File> files;
    for (const auto &entry : std::filesystem::directory_iterator(directory))
    {
        const auto extension = Fold(entry.path().extension().u8string());
        if (entry.is_regular_file() && (extension == ".3cn" || extension == ".3th"))
            files.push_back({entry.path().filename().u8string(), entry.path()});
    }
    Mount(source, files);
}
bool Vfs::Mounted(const std::string &source) const
{
    return three_mm_vfs_mounted(vfs, source.c_str()) != 0;
}
std::vector<File> Vfs::Entries(const std::string &source) const
{
    std::vector<File> result;
    ThreeMMVfsMount file;
    std::size_t cursor = 0;
    while (three_mm_vfs_next(vfs, source.c_str(), &cursor, &file))
        result.push_back({file.name, std::filesystem::u8path(file.location)});
    return result;
}
std::vector<std::filesystem::path> Vfs::Files(const std::string &source, const std::string &extension) const
{
    std::vector<std::filesystem::path> result;
    for (const auto &file : Entries(source))
    {
        // ZIP virtual names may retain legacy bytes; only physical paths are UTF-8.
        const auto dot = file.name.find_last_of('.');
        if (dot != std::string::npos && Fold(file.name.substr(dot)) == Fold(extension))
            result.push_back(file.physical);
    }
    return result;
}
std::filesystem::path Vfs::Resolve(const std::string &source, const std::string &name) const
{
    const auto location = three_mm_vfs_resolve(vfs, source.c_str(), name.c_str());
    if (!location)
        throw std::runtime_error("Content file is not mounted: " + source + "/" + name);
    return std::filesystem::u8path(location);
}
std::ifstream Vfs::Open(const std::string &source, const std::string &name) const
{
    std::ifstream stream(Resolve(source, name), std::ios::binary);
    if (!stream)
        throw std::runtime_error("Cannot open mounted content: " + source + "/" + name);
    return stream;
}
} // namespace ThreeMM::Content
