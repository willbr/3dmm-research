#ifndef THREE_MM_CONTENT_VFS_HPP
#define THREE_MM_CONTENT_VFS_HPP
#include "content_vfs.h"
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace ThreeMM::Content
{
// Logical product identity, independent of an installation's numeric source ID.
// Accepts both a long/short pair and the movie table's "long/short" spelling.
std::string SourceKey(const std::string &title, const std::string &folder = {});
struct File
{
    std::string name;
    std::filesystem::path physical;
};
class Vfs
{
    ThreeMMVfs *vfs;

  public:
    Vfs();
    ~Vfs();
    Vfs(const Vfs &) = delete;
    Vfs &operator=(const Vfs &) = delete;
    Vfs(Vfs &&other) noexcept;
    Vfs &operator=(Vfs &&other) noexcept;
    ThreeMMVfs *Handle() const
    {
        return vfs;
    }
    // Read-only source namespaces. First mount wins for identical virtual
    // names; ambiguous casing is rejected. Replace publishes atomically.
    void Mount(const std::string &source, const std::vector<File> &files);
    void Replace(const std::string &source, const std::vector<File> &files);
    void MountDirectory(const std::string &source, const std::filesystem::path &directory);
    bool Mounted(const std::string &source) const;
    std::vector<File> Entries(const std::string &source) const;
    std::vector<std::filesystem::path> Files(const std::string &source, const std::string &extension) const;
    std::ifstream Open(const std::string &source, const std::string &name) const;
    // Legacy chunky readers still require an ordinary seekable physical file.
    std::filesystem::path Resolve(const std::string &source, const std::string &name) const;
};
} // namespace ThreeMM::Content
#endif
