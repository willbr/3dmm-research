/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "iso_cache.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <map>
#include <random>
#include <set>
#include <sstream>
#include <stdexcept>
#include <thread>
namespace StudioISO
{
namespace
{
namespace fs = std::filesystem;
constexpr std::uint64_t Sector = 2048;
[[noreturn]] void Fail(const std::string &message)
{
    throw std::runtime_error(message);
}
std::string Lower(std::string value)
{
    for (auto &c : value)
        if (c >= 'A' && c <= 'Z')
            c += 'a' - 'A';
    return value;
}
bool Leaf(const std::string &name)
{
    return !name.empty() && name != "." && name != ".." && name.size() <= 128 &&
           std::all_of(name.begin(), name.end(),
                       [](unsigned char c) {
                           return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
                                  c == '.' || c == '_' || c == '-' || c == ' ';
                       }) &&
           name.back() != '.' && name.back() != ' ';
}
bool Relative(const std::string &path)
{
    if (path.empty() || path.size() > 240)
        return false;
    for (std::size_t first = 0;;)
    {
        const auto end = path.find('/', first);
        if (!Leaf(path.substr(first, end == std::string::npos ? end : end - first)))
            return false;
        if (end == std::string::npos)
            return true;
        first = end + 1;
    }
}
std::uint32_t Both32(const unsigned char *p)
{
    const auto little =
        std::uint32_t(p[0]) | (std::uint32_t(p[1]) << 8) | (std::uint32_t(p[2]) << 16) | (std::uint32_t(p[3]) << 24);
    const auto big = (std::uint32_t(p[4]) << 24) | (std::uint32_t(p[5]) << 16) | (std::uint32_t(p[6]) << 8) | p[7];
    if (little != big)
        Fail("Inconsistent ISO directory record");
    return little;
}
struct Entry
{
    std::string name;
    std::uint64_t offset = 0, bytes = 0;
    bool directory = false;
    const unsigned char *memory = nullptr;
};
class Disc
{
    std::ifstream file;
    std::uint64_t bytes;
    Entry root;
    Entry Record(const unsigned char *p, std::size_t available)
    {
        if (available < 34 || p[0] < 34 || p[0] > available || p[32] > p[0] - 33 || p[1] || p[26] || p[27] ||
            (p[25] & 0x80))
            Fail("Unsupported or corrupt ISO directory record");
        Entry result;
        result.offset = std::uint64_t(Both32(p + 2)) * Sector;
        result.bytes = Both32(p + 10);
        result.directory = (p[25] & 2) != 0;
        if (result.offset > bytes || result.bytes > bytes - result.offset)
            Fail("ISO file extends beyond the image");
        if (p[32] == 1 && p[33] < 2)
            return result; // Self/parent directory records are never paths.
        result.name.assign(reinterpret_cast<const char *>(p + 33), p[32]);
        const auto version = result.name.find(';');
        if (version != std::string::npos)
        {
            if (result.name.substr(version) != ";1")
                Fail("Unsupported ISO file version");
            result.name.resize(version);
        }
        if (!result.name.empty() && result.name.back() == '.')
            result.name.pop_back();
        if (!Leaf(result.name))
            Fail("Unsafe ISO filename");
        return result;
    }

  public:
    explicit Disc(const fs::path &path) : file(path, std::ios::binary), bytes(fs::file_size(path))
    {
        if (!file || bytes < 17 * Sector)
            Fail("Cannot read ISO image");
        std::array<unsigned char, Sector> descriptor;
        bool found = false;
        for (unsigned sector = 16; sector < 80 && (sector + 1) * Sector <= bytes; ++sector)
        {
            Read(sector * Sector, descriptor.data(), descriptor.size());
            if (std::memcmp(descriptor.data() + 1, "CD001", 5) || descriptor[6] != 1)
                Fail("Expected an ISO 9660 image");
            if (descriptor[0] == 255)
                break;
            if (descriptor[0] != 1)
                continue;
            if (descriptor[128] != 0 || descriptor[129] != 8 || descriptor[130] != 8 || descriptor[131] != 0)
                Fail("Unsupported ISO sector size");
            if (std::uint64_t(Both32(descriptor.data() + 80)) * Sector > bytes)
                Fail("ISO image is truncated");
            root = Record(descriptor.data() + 156, descriptor.size() - 156);
            found = root.directory;
            break;
        }
        if (!found)
            Fail("ISO primary volume descriptor is missing");
    }
    void Read(std::uint64_t offset, void *destination, std::size_t count)
    {
        if (offset > bytes || count > bytes - offset)
            Fail("ISO read extends beyond the image");
        file.clear();
        file.seekg(static_cast<std::streamoff>(offset));
        if (!file.read(static_cast<char *>(destination), static_cast<std::streamsize>(count)))
            Fail("Cannot read ISO contents");
    }
    std::vector<Entry> List(const Entry &directory)
    {
        if (!directory.directory || directory.bytes > 16 * 1024 * 1024)
            Fail("Invalid ISO directory");
        std::vector<unsigned char> data(static_cast<std::size_t>(directory.bytes));
        if (!data.empty())
            Read(directory.offset, data.data(), data.size());
        std::vector<Entry> entries;
        std::set<std::string> names;
        for (std::size_t offset = 0; offset < data.size();)
        {
            if (!data[offset])
            {
                offset = (offset / Sector + 1) * Sector;
                continue;
            }
            const auto available = (std::min)(data.size() - offset, static_cast<std::size_t>(Sector - offset % Sector));
            auto entry = Record(data.data() + offset, available);
            offset += data[offset];
            if (entry.name.empty())
                continue;
            if (!names.insert(Lower(entry.name)).second || entries.size() >= 4096)
                Fail("Ambiguous or excessive ISO directory entries");
            entries.push_back(std::move(entry));
        }
        return entries;
    }
    std::vector<Entry> Root()
    {
        return List(root);
    }
};
void Hash(std::uint64_t &hash, const void *data, std::size_t count)
{
    const auto bytes = static_cast<const unsigned char *>(data);
    for (std::size_t i = 0; i < count; ++i)
        hash = (hash ^ bytes[i]) * UINT64_C(1099511628211);
}
void Hash(std::uint64_t &hash, const std::string &text)
{
    Hash(hash, text.data(), text.size());
}
std::string Hex(std::uint64_t hash)
{
    std::ostringstream result;
    result << std::hex << std::setfill('0') << std::setw(16) << hash;
    return result.str();
}
std::string ReadText(const fs::path &path)
{
    std::error_code error;
    const auto bytes = fs::file_size(path, error);
    if (error || bytes > 1024 * 1024 || !fs::is_regular_file(path, error))
        return {};
    std::ifstream file(path, std::ios::binary);
    if (!file)
        return {};
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}
bool Complete(const fs::path &root, const std::string &manifest, const std::map<std::string, Entry> &files)
{
    std::error_code error;
    if (fs::is_symlink(root, error) || ReadText(root / ".complete-v1") != manifest)
        return false;
    for (const auto &file : files)
    {
        const auto path = root / fs::u8path(file.second.name);
        if (fs::is_symlink(path, error) || !fs::is_regular_file(path, error) ||
            fs::file_size(path, error) != file.second.bytes || error)
            return false;
    }
    return true;
}
} // namespace
std::map<std::string, std::vector<unsigned char>> ReadFiles(const fs::path &image, const std::string &directory,
                                                            const std::vector<std::string> &names)
{
    Disc disc(image);
    std::map<std::string, Entry> entries;
    for (const auto &folder : disc.Root())
        if (folder.directory && Lower(folder.name) == Lower(directory))
            for (const auto &entry : disc.List(folder))
                entries.emplace(Lower(entry.name), entry);
    std::map<std::string, std::vector<unsigned char>> result;
    for (const auto &name : names)
    {
        if (!Leaf(name))
            Fail("Invalid requested ISO filename");
        auto found = entries.find(Lower(name));
        if (found == entries.end() || found->second.directory || !found->second.bytes ||
            found->second.bytes > 256 * 1024 * 1024)
            Fail("Missing or invalid ISO file: " + name);
        auto &data = result[name];
        data.resize(std::size_t(found->second.bytes));
        disc.Read(found->second.offset, data.data(), data.size());
    }
    return result;
}
Cache Prepare(const fs::path &image, const fs::path &cacheDirectory, const std::vector<Supplement> &supplements)
{
    const auto source = fs::canonical(image);
    const auto cache = fs::absolute(cacheDirectory).lexically_normal();
    Disc disc(source);
    std::map<std::string, Entry> files;
    for (const auto &folder : disc.Root())
    {
        const auto kind = Lower(folder.name);
        if (!folder.directory || (kind != "3dmovie" && kind != "samples" && kind != "setup" && kind != "pss"))
            continue;
        for (auto file : disc.List(folder))
        {
            const auto name = Lower(file.name);
            if (file.directory)
                continue;
            if (kind == "3dmovie" && fs::path(name).extension() != ".exe")
                file.name = "Microsoft Kids/3D Movie Maker/" + file.name;
            else if (kind == "samples" && fs::path(name).extension() == ".3mm")
                file.name = "Microsoft Kids/Users/Melanie/" + file.name;
            else if (kind == "setup" && (name == "comic.ttf" || name == "comicbd.ttf"))
                file.name = "fonts/" + file.name;
            else if (kind == "pss" && name.compare(0, 5, "3dmm.") == 0)
                file.name = "Microsoft Kids/3D Movie Maker/" + file.name;
            else
                continue;
            if (!files.emplace(Lower(file.name), file).second)
                Fail("Duplicate retail file in ISO");
        }
    }
    for (const auto name :
         {"3dmovie.chk", "bldgdata.chk", "bldghd.chk", "building.chk", "studio.chk", "shared.chk", "sharecd.chk",
          "help.chk", "helpaud.chk", "tmpls.3cn", "bkgds.3cn", "mtrls.3cn", "tdfs.3cn", "snds.3cn"})
        if (!files.count(std::string("microsoft kids/3d movie maker/") + name))
            Fail(std::string("ISO does not contain the retail 3D Movie Maker file ") + name);

    std::uint64_t hash = UINT64_C(14695981039346656037);
    Hash(hash, source.u8string());
    Hash(hash, std::to_string(fs::file_size(source)));
    Hash(hash, std::to_string(static_cast<long long>(fs::last_write_time(source).time_since_epoch().count())));
    for (const auto &supplement : supplements)
    {
        if (!Relative(supplement.name) || (!supplement.data && supplement.bytes))
            Fail("Invalid supplemental cache file");
        Hash(hash, supplement.name);
        Hash(hash, supplement.data, supplement.bytes);
        Entry file;
        file.name = supplement.name;
        file.bytes = supplement.bytes;
        file.memory = supplement.data;
        if (!files.emplace(Lower(file.name), file).second)
            Fail("Supplement would replace an ISO asset");
    }
    std::string manifest = "3DMM ISO cache v1\n" + Hex(hash) + "\n";
    for (const auto &file : files)
        manifest += file.second.name + "\t" + std::to_string(file.second.bytes) + "\n";
    const auto prefix = "retail-v1-" + Hex(hash);
    fs::create_directories(cache);
    fs::path destination;
    for (unsigned generation = 0; generation < 1000; ++generation)
    {
        destination = cache / (prefix + (generation ? "-" + std::to_string(generation) : ""));
        if (!fs::exists(destination))
            break;
        if (Complete(destination, manifest, files))
            return {destination / "Microsoft Kids", true};
        if (generation == 999)
            Fail("Too many incomplete ISO caches; select another cache directory");
    }
    // A private staging directory lets concurrent launches prepare safely.
    // No existing cache (which may contain user-edited samples) is deleted.
    fs::path staging;
    std::random_device random;
    for (unsigned attempt = 0; attempt < 100; ++attempt)
    {
        staging = cache / (".partial-" + prefix + "-" + Hex((std::uint64_t(random()) << 32) ^ random()));
        if (fs::create_directory(staging))
            break;
        if (attempt == 99)
            Fail("Cannot create ISO cache staging directory");
    }
    try
    {
        std::array<char, 1024 * 128> buffer;
        for (const auto &item : files)
        {
            const auto &file = item.second;
            const auto path = staging / fs::u8path(file.name);
            fs::create_directories(path.parent_path());
            std::ofstream output(path, std::ios::binary | std::ios::trunc);
            if (!output)
                Fail("Cannot create ISO cache file");
            for (std::uint64_t offset = 0; offset < file.bytes;)
            {
                const auto count =
                    static_cast<std::size_t>((std::min)(file.bytes - offset, std::uint64_t(buffer.size())));
                if (file.memory)
                    output.write(reinterpret_cast<const char *>(file.memory + offset), count);
                else
                {
                    disc.Read(file.offset + offset, buffer.data(), count);
                    output.write(buffer.data(), count);
                }
                if (!output)
                    Fail("Cannot write ISO cache file");
                offset += count;
            }
            output.close();
            if (!output)
                Fail("Cannot finish ISO cache file");
        }
        fs::create_directories(staging / "Microsoft Kids/Users/Melanie");
        std::ofstream marker(staging / ".complete-v1", std::ios::binary);
        marker << manifest;
        marker.close();
        if (!marker)
            Fail("Cannot finish ISO cache manifest");
#ifdef _WIN32
        unsigned publicationRetries = 0;
#endif
        for (unsigned race = 0;; ++race)
        {
            std::error_code error;
            fs::rename(staging, destination, error);
            if (!error)
                return {destination / "Microsoft Kids", false};
#ifdef _WIN32
            // A scanner may briefly hold a newly extracted file without delete
            // sharing, preventing its parent directory from being renamed.
            if (!fs::exists(destination) && (error == std::errc::permission_denied || error.value() == 32) &&
                publicationRetries++ < 20)
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
                continue;
            }
#endif
            if (!fs::exists(destination) || race >= 1000)
                Fail("Cannot publish ISO cache: " + error.message());
            if (Complete(destination, manifest, files))
            {
                fs::remove_all(staging);
                return {destination / "Microsoft Kids", true};
            }
            destination = cache / (prefix + "-concurrent-" + std::to_string(race));
        }
    }
    catch (...)
    {
        std::error_code ignored;
        fs::remove_all(staging, ignored); // Only the private directory created above.
        throw;
    }
}
} // namespace StudioISO
