/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "studio_packs.h"
#include "pack_hash.h"
#include "three_mm/archive.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <fstream>
#include <iomanip>
#include <map>
#include <random>
#include <set>
#include <sstream>
#include <stdexcept>
namespace StudioPacks
{
namespace
{
namespace fs = std::filesystem;
using Json = nlohmann::json;
std::string Lower(std::string value)
{
    for (auto &c : value)
        if (c >= 'A' && c <= 'Z')
            c += 'a' - 'A';
    return value;
}
bool Generation(const std::string &value)
{
    return value.size() == 34 && value.substr(0, 2) == "g-" && std::all_of(value.begin() + 2, value.end(), [](char c) {
               return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
           });
}
std::string NewGeneration()
{
    std::random_device random;
    std::ostringstream text;
    text << "g-" << std::hex << std::setfill('0');
    for (unsigned i = 0; i < 4; ++i)
        text << std::setw(8) << std::uint32_t(random());
    return text.str();
}
const Definition &Def(Id id)
{
    return Definitions().at(std::size_t(id));
}
fs::path Checked(const fs::path &root, const std::string &folder, const std::string &generation)
{
    if (!Generation(generation))
        throw std::runtime_error("Invalid managed pack generation");
    auto parent = root / folder;
    auto path = parent / generation;
    if (fs::is_symlink(parent) || fs::is_symlink(path))
        throw std::runtime_error("A managed pack path is a symbolic link");
    auto actualRoot = fs::weakly_canonical(root);
    auto actualPath = fs::weakly_canonical(path);
    auto relative = actualPath.lexically_relative(actualRoot);
    if (relative.empty() || relative.is_absolute() || *relative.begin() == "..")
        throw std::runtime_error("Managed pack path escaped its content folder");
    return path;
}
Json Manifest(const fs::path &directory, Id id)
{
    auto path = directory / "pack.json";
    if (fs::is_symlink(path) || !fs::is_regular_file(path) || fs::file_size(path) > 1024 * 1024)
        throw std::runtime_error("The pack installation manifest is missing or invalid");
    std::ifstream stream(path);
    auto manifest = Json::parse(stream);
    if (manifest.at("schema") != "3dmm-managed-pack-1" || manifest.at("pack") != Def(id).key ||
        !manifest.at("files").is_array() || manifest.at("files").size() != Def(id).files.size())
        throw std::runtime_error("The pack installation manifest does not match this pack");
    std::set<std::string> expected(Def(id).files.begin(), Def(id).files.end());
    for (const auto &file : manifest.at("files"))
    {
        auto name = file.at("name").get<std::string>();
        auto hash = file.at("sha256").get<std::string>();
        if (!expected.erase(name) || hash.size() != 64 || !file.at("size").is_number_unsigned())
            throw std::runtime_error("Invalid owned-file record");
    }
    return manifest;
}
void ValidateArchive(const fs::path &path)
{
    ThreeMM::Options options;
    options.maxDecodedBytes = 128 * 1024 * 1024;
    ThreeMM::Archive archive(options);
    ThreeMM::Error error;
    if (!archive.Open(path.u8string(), error) || archive.Chunks().empty())
        throw std::runtime_error("Invalid content archive: " + path.filename().u8string());
    for (std::size_t i = 0; i < archive.Chunks().size(); ++i)
    {
        ThreeMM::PayloadPage page;
        if (!archive.ReadPayload(i, 0, 1, page, error))
            throw std::runtime_error("Cannot decode content archive: " + path.filename().u8string());
        for (const auto &child : archive.Chunks()[i].children)
            if (!child.present)
                throw std::runtime_error("Content archive has a missing child chunk");
    }
}
unsigned Cleanup(const fs::path &directory, Id id)
{
    auto manifest = Manifest(directory, id);
    unsigned kept = 0;
    for (const auto &record : manifest.at("files"))
    {
        auto path = directory / record.at("name").get<std::string>();
        if (!fs::exists(path) && !fs::is_symlink(path))
            continue;
        if (fs::is_symlink(path) || !fs::is_regular_file(path) || HashFile(path) != record.at("sha256"))
        {
            ++kept;
            continue;
        }
        if (!fs::remove(path))
            throw std::runtime_error("Cannot remove a managed pack file");
    }
    fs::remove(directory / "pack.json");
    if (fs::is_empty(directory))
        fs::remove(directory);
    return kept;
}
} // namespace
const std::array<Definition, 3> &Definitions()
{
    static const std::array<Definition, 3> definitions = {
        {{Id::Nickelodeon,
          "nickelodeon",
          "Nickelodeon",
          "N3DMM EXPANSION",
          "N3DMMExp",
          3,
          {"NICKACTOR.3TH", "NICKBKGDS.3CN", "NICKBKGDS.3TH", "NICKMTRL.3TH", "NICKMTRLS.3CN", "NICKPROP.3TH",
           "NICKSNDS.3CN", "NICKSOUND.3TH", "NICKTDFS.3CN", "NICKTMPLS.3CN"}},
         {Id::Doraemon,
          "doraemon",
          "Doraemon",
          "Doraemon Character Kit",
          "DORAEMON",
          4,
          {"DORAACTR.3TH", "DORABKGD.3CN", "DORABKGD.3TH", "DORAPROP.3CN", "DORAPROP.3TH", "DORASNDS.3CN",
           "DORASNDS.3TH", "DORATMPL.3CN"}},
         {Id::Frankie,
          "frankie",
          "Frankie's Expansion",
          "Expansions",
          "EXPANSIONS",
          5,
          {"3DFONTPACK1.3CN", "3DFONTPACK1.3TH", "MTRLPACK1-2.3CN", "MTRLPACK1.3CN", "MTRLPACK1.3TH",
           "MTRLPACK1old.3CN", "SCENEPACK1.3CN", "SCENEPACK1.3TH", "SOLIDMTRLPACK.3CN", "SOLIDMTRLPACK.3TH",
           "SOUNDPACK1.3CN", "SOUNDPACK1.3TH", "TMPLPACK1.3CN", "TMPLPACK1actors.3TH", "WACKYDINGS.3CN",
           "WACKYDINGS.3TH"}}}};
    return definitions;
}
int Identify(const std::string &sourceName)
{
    auto name = Lower(sourceName);
    if (name == "n3dmm expansion" || name == "n3dmmexp")
        return 0;
    if (name == "doraemon" || name == "doraemon character kit")
        return 1;
    if (name == "expansions")
        return 2;
    return -1;
}
std::string Serialize(const Preferences &preferences)
{
    Json json = Json::object();
    for (std::size_t i = 0; i < 3; ++i)
        json[Definitions()[i].key] = {{"generation", preferences[i].generation},
                                      {"enabled", preferences[i].enabled},
                                      {"remove", preferences[i].remove},
                                      {"source", preferences[i].sourceId}};
    return json.dump();
}
Preferences Parse(const std::string &text)
{
    Preferences result;
    for (std::size_t i = 0; i < 3; ++i)
        result[i].sourceId = Definitions()[i].sourceId;
    if (text.empty())
        return result;
    if (text.size() > 16384)
        throw std::runtime_error("Expansion settings are too large");
    auto json = Json::parse(text);
    if (!json.is_object())
        throw std::runtime_error("Invalid expansion settings");
    for (std::size_t i = 0; i < 3; ++i)
        if (json.contains(Definitions()[i].key))
        {
            const auto &row = json.at(Definitions()[i].key);
            result[i].generation = row.at("generation").get<std::string>();
            result[i].enabled = row.at("enabled").get<bool>();
            result[i].remove = row.at("remove").get<bool>();
            result[i].sourceId = row.at("source").get<int>();
            if ((!result[i].generation.empty() && !Generation(result[i].generation)) || result[i].sourceId <= 0)
                throw std::runtime_error("Invalid managed pack settings");
        }
    return result;
}
Store::Store(fs::path directory) : root(fs::absolute(directory).lexically_normal())
{
}
Info Store::Inspect(Id id, const Preference &preference, bool hashes) const
{
    Info info;
    if (preference.generation.empty())
        return info;
    info.installed = true;
    try
    {
        auto directory = Checked(root, Def(id).key, preference.generation);
        info.path = directory.u8string();
        auto manifest = Manifest(directory, id);
        for (const auto &record : manifest.at("files"))
        {
            auto path = directory / record.at("name").get<std::string>();
            if (fs::is_symlink(path) || !fs::is_regular_file(path) ||
                fs::file_size(path) != record.at("size").get<std::uintmax_t>() ||
                (hashes && HashFile(path) != record.at("sha256")))
                throw std::runtime_error("A managed pack file is missing or changed");
        }
        info.valid = true;
    }
    catch (const std::exception &error)
    {
        info.error = error.what();
    }
    return info;
}
Staged Store::StageFolder(Id id, const fs::path &source)
{
    std::map<std::string, fs::path> inputs;
    for (const auto &entry : fs::directory_iterator(source))
    {
        auto name = Lower(entry.path().filename().u8string());
        if (!inputs.emplace(name, entry.path()).second)
            throw std::runtime_error("Ambiguous source filenames");
    }
    for (const auto &name : Def(id).files)
    {
        auto found = inputs.find(Lower(name));
        if (found == inputs.end() || fs::is_symlink(found->second) || !fs::is_regular_file(found->second) ||
            !fs::file_size(found->second) || fs::file_size(found->second) > 256 * 1024 * 1024)
            throw std::runtime_error("Missing or invalid expansion file: " + name);
    }
    Staged stage{id, NewGeneration(), {}};
    stage.path = Checked(root, ".pending", stage.generation);
    fs::create_directories(stage.path.parent_path());
    if (!fs::create_directory(stage.path))
        throw std::runtime_error("Pack staging directory already exists");
    std::vector<fs::path> owned;
    try
    {
        Json manifest = {{"schema", "3dmm-managed-pack-1"}, {"pack", Def(id).key}, {"files", Json::array()}};
        for (const auto &name : Def(id).files)
        {
            auto target = stage.path / name;
            owned.push_back(target);
            fs::copy_file(inputs.at(Lower(name)), target);
            ValidateArchive(target);
            manifest["files"].push_back(
                {{"name", name}, {"size", fs::file_size(target)}, {"sha256", HashFile(target)}});
        }
        owned.push_back(stage.path / "pack.json");
        std::ofstream stream(owned.back(), std::ios::binary);
        stream << manifest.dump(2) << '\n';
        stream.close();
        if (!stream)
            throw std::runtime_error("Cannot save the pack manifest");
        return stage;
    }
    catch (...)
    {
        for (const auto &path : owned)
        {
            std::error_code error;
            fs::remove(path, error);
        }
        std::error_code error;
        fs::remove(stage.path, error);
        throw;
    }
}
Staged Store::StageISO(Id id, const fs::path &iso, const fs::path &baseISO, const fs::path &fixups)
{
    auto generation = NewGeneration();
    auto temporary = Checked(root, ".imports", generation);
    fs::create_directories(temporary.parent_path());
    if (!fs::create_directory(temporary))
        throw std::runtime_error("ISO staging directory already exists");
    try
    {
        ImportISO(id, iso, baseISO, fixups, temporary);
        auto result = StageFolder(id, temporary);
        // ImportISO creates only this pack's known output archives.
        for (const auto &name : Def(id).files)
            fs::remove(temporary / name);
        fs::remove(temporary);
        return result;
    }
    catch (...)
    {
        for (const auto &name : Def(id).files)
        {
            std::error_code error;
            fs::remove(temporary / name, error);
        }
        std::error_code error;
        fs::remove(temporary, error);
        throw;
    }
}
void Store::Publish(Staged &stage)
{
    if (stage.path.empty())
        return;
    if (stage.path != Checked(root, ".pending", stage.generation))
        throw std::runtime_error("Invalid staged pack path");
    auto manifest = Manifest(stage.path, stage.id);
    for (const auto &record : manifest.at("files"))
        if (HashFile(stage.path / record.at("name").get<std::string>()) != record.at("sha256"))
            throw std::runtime_error("Staged content changed before installation");
    auto destination = Checked(root, Def(stage.id).key, stage.generation);
    fs::create_directories(destination.parent_path());
    if (fs::exists(destination))
        throw std::runtime_error("Pack generation already exists");
    fs::rename(stage.path, destination);
    stage.path.clear();
}
void Store::Discard(Staged &stage)
{
    if (stage.generation.empty())
        return;
    auto path = stage.path.empty() ? Checked(root, Def(stage.id).key, stage.generation)
                                   : Checked(root, ".pending", stage.generation);
    if (fs::exists(path))
        Cleanup(path, stage.id);
    stage.path.clear();
    stage.generation.clear();
}
unsigned Store::Remove(Id id, const Preference &preference)
{
    if (preference.generation.empty())
        return 0;
    auto path = Checked(root, Def(id).key, preference.generation);
    if (!fs::exists(path))
        return 0;
    // A prior removal may have finished before preferences could be saved.
    // Without the ownership manifest all remaining files must be preserved.
    if (!fs::exists(path / "pack.json"))
        return fs::is_empty(path) ? (fs::remove(path), 0u) : 1u;
    return Cleanup(path, id);
}
} // namespace StudioPacks
