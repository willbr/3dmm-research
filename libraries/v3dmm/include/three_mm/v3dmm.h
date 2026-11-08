// Licensed under the MIT License.
#ifndef THREE_MM_V3DMM_H
#define THREE_MM_V3DMM_H
#include <cstdint>
#include <filesystem>
#include <map>
#include <string>
#include <vector>
namespace ThreeMM::V3dmm
{
struct Range
{
    std::filesystem::path file;
    std::uint64_t offset = 0, length = 0;
};
struct Dependency
{
    std::string name; // Original legacy bytes, retained on save.
    Range archive;    // A zero length is an external library dependency.
};
struct Bundle
{
    std::vector<Dependency> dependencies;
    Range movie;
};
struct Member
{
    std::string name;
    std::uint64_t bytes = 0;
    unsigned index = 0;
};
struct Package
{
    std::string name, author, shortName;
    std::map<std::string, std::string> metadata;
    std::vector<Member> members;
};
struct Limits
{
    std::uint32_t dependencies = 10000, members = 10000;
    std::uint64_t memberBytes = 256ull * 1024 * 1024;
    std::uint64_t expandedBytes = 4ull * 1024 * 1024 * 1024;
};
std::string Fold(std::string text);
std::string Extension(const std::string &name);
Range WholeFile(const std::filesystem::path &path);
Bundle ReadBundle(const std::filesystem::path &path, const Limits &limits = {});
Package ReadPackage(const Range &archive, const Limits &limits = {});
void Copy(const Range &range, const std::filesystem::path &output);
// Extract only recognized chunky content members to caller-owned staging.
// Returns original ZIP member names mapped to materialized paths.
std::map<std::string, std::filesystem::path> Materialize(const Range &archive, const std::filesystem::path &directory,
                                                         const Limits &limits = {});
// Writes a complete candidate. Publication/replacement is the caller's job.
void WriteBundle(const Range &movie, const std::vector<Dependency> &dependencies, const std::filesystem::path &output);
// Atomically replace destination with a completed adjacent candidate.
void Publish(const std::filesystem::path &candidate, const std::filesystem::path &destination);
} // namespace ThreeMM::V3dmm
#endif
