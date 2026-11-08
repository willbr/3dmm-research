// Licensed under the MIT License.
#include "studio_vxp_store.h"
#include "pack_hash.h"
#include "three_mm/archive.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <fstream>
#include <random>
#include <stdexcept>
namespace StudioV3dmm
{
namespace
{
namespace fs = std::filesystem;
namespace V = ThreeMM::V3dmm;
using Json = nlohmann::json;
void Require(bool value, const std::string &message)
{
    if (!value)
        throw std::runtime_error(message);
}
fs::path Checked(const fs::path &root, const std::string &hash)
{
    Require(hash.size() == 64 && std::all_of(hash.begin(), hash.end(),
                                             [](char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); }),
            "Invalid VXP installation identity");
    auto path = root / hash;
    Require(!fs::is_symlink(path), "VXP installation is a symbolic link");
    Require(fs::weakly_canonical(path).parent_path() == fs::canonical(root), "VXP installation escaped its store");
    return path;
}
void Validate(const fs::path &path)
{
    ThreeMM::Options options;
    options.maxDecodedBytes = 256u * 1024 * 1024;
    ThreeMM::Archive archive(options);
    ThreeMM::Error error;
    Require(archive.Open(path.u8string(), error) && !archive.Chunks().empty(), "Invalid VXP chunky content");
    for (std::size_t i = 0; i < archive.Chunks().size(); ++i)
    {
        ThreeMM::PayloadPage payload;
        Require(archive.ReadPayload(i, 0, 1, payload, error), "Invalid compressed VXP content");
        for (const auto &child : archive.Chunks()[i].children)
            Require(child.present, "VXP content has a missing child");
    }
}
} // namespace
Store::Store(std::filesystem::path directory) : root(fs::absolute(std::move(directory)))
{
    fs::create_directories(root);
    root = fs::canonical(root);
    auto catalog = root / "catalog.json";
    if (!fs::exists(catalog))
        return;
    Require(!fs::is_symlink(catalog) && fs::file_size(catalog) <= 4 * 1024 * 1024, "Invalid VXP catalog");
    std::ifstream stream(catalog);
    auto rows = Json::parse(stream);
    Require(rows.is_array() && rows.size() <= 10000, "Invalid VXP catalog entries");
    for (const auto &row : rows)
    {
        auto entry = Read(row.at("hash").get<std::string>(), row.at("enabled").get<bool>());
        for (const auto &prior : entries)
            Require(V::Fold(prior.name) != V::Fold(entry.name), "Ambiguous installed VXP names");
        entries.push_back(std::move(entry));
    }
}
Installation Store::Read(const std::string &hash, bool enabled) const
{
    auto directory = Checked(root, hash);
    auto archive = directory / "package.vxp";
    Require(!fs::is_symlink(archive) && StudioPacks::HashFile(archive) == hash, "Installed VXP is missing or changed");
    auto package = V::ReadPackage(V::WholeFile(archive));
    auto manifestPath = directory / "content.json";
    Require(!fs::is_symlink(manifestPath) && fs::file_size(manifestPath) <= 4 * 1024 * 1024,
            "Invalid VXP content manifest");
    std::ifstream manifestStream(manifestPath);
    auto manifest = Json::parse(manifestStream);
    Require(manifest.is_object(), "Invalid VXP content hashes");
    Installation result{package.name, package.author, hash, enabled, archive, {}};
    for (const auto &member : package.members)
    {
        auto extension = V::Extension(member.name);
        if (extension == ".3cn" || extension == ".3th")
        {
            auto path = directory / (std::to_string(member.index) + extension);
            Require(!fs::is_symlink(path) && fs::is_regular_file(path) && fs::file_size(path) == member.bytes,
                    "Installed VXP content is missing or changed");
            Require(manifest.at(path.filename().u8string()).get<std::string>() == StudioPacks::HashFile(path),
                    "Installed VXP content has changed");
            result.members.emplace(member.name, path);
        }
    }
    return result;
}
void Store::Save(const std::vector<Installation> &next)
{
    Json rows = Json::array();
    for (const auto &entry : next)
        rows.push_back({{"hash", entry.hash}, {"enabled", entry.enabled}});
    auto candidate = root / ("catalog-" + std::to_string(std::random_device{}()) + ".tmp");
    Require(!fs::exists(candidate), "VXP catalog staging path already exists");
    try
    {
        std::ofstream stream(candidate, std::ios::binary);
        stream << rows.dump(2) << '\n';
        stream.close();
        Require(bool(stream), "Cannot save VXP catalog");
        V::Publish(candidate, root / "catalog.json");
    }
    catch (...)
    {
        fs::remove(candidate);
        throw;
    }
    entries = next;
}
Installation Store::Install(const ThreeMM::V3dmm::Range &source)
{
    auto package = V::ReadPackage(source);
    auto stage = root / (".pending-" + std::to_string(std::random_device{}()));
    Require(fs::create_directory(stage), "Cannot create VXP staging directory");
    std::vector<fs::path> owned;
    try
    {
        auto archive = stage / "package.vxp";
        owned.push_back(archive);
        V::Copy(source, archive);
        package = V::ReadPackage(V::WholeFile(archive));
        auto hash = StudioPacks::HashFile(archive);
        for (const auto &entry : entries)
            if (V::Fold(entry.name) == V::Fold(package.name))
            {
                Require(entry.hash == hash, "A different VXP with this name is already installed: " + package.name);
                fs::remove(archive);
                fs::remove(stage);
                return entry;
            }
        auto destination = Checked(root, hash);
        if (!fs::exists(destination))
        {
            // Record every potential member before extraction, including partial writes.
            for (const auto &member : package.members)
            {
                auto ext = V::Extension(member.name);
                if (ext == ".3cn" || ext == ".3th")
                    owned.push_back(stage / (std::to_string(member.index) + ext));
            }
            auto files = V::Materialize(V::WholeFile(archive), stage);
            Json manifest = Json::object();
            for (const auto &file : files)
            {
                Validate(file.second);
                manifest[file.second.filename().u8string()] = StudioPacks::HashFile(file.second);
            }
            owned.push_back(stage / "content.json");
            std::ofstream record(owned.back(), std::ios::binary);
            record << manifest.dump(2) << '\n';
            record.close();
            Require(bool(record), "Cannot save VXP content hashes");
            fs::rename(stage, destination);
            owned.clear();
        }
        else
        {
            fs::remove(archive);
            fs::remove(stage);
        }
        auto result = Read(hash, true);
        auto next = entries;
        next.push_back(result);
        Save(next);
        return result;
    }
    catch (...)
    {
        // No recursive deletion; preserve anything not created by this import.
        for (const auto &path : owned)
        {
            std::error_code ignored;
            fs::remove(path, ignored);
        }
        std::error_code ignored;
        if (fs::is_directory(stage, ignored) && fs::is_empty(stage, ignored))
            fs::remove(stage, ignored);
        throw;
    }
}
void Store::SetEnabled(const std::string &hash, bool enabled)
{
    auto next = entries;
    auto found = std::find_if(next.begin(), next.end(), [&](const Installation &entry) { return entry.hash == hash; });
    Require(found != next.end(), "VXP is not installed");
    found->enabled = enabled;
    Save(next);
}
} // namespace StudioV3dmm
