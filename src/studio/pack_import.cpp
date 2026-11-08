/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "studio_packs.h"
#include "pack_hash.h"
#include "iso_cache.h"
#include "three_mm/nick_expansion.h"
#include <nlohmann/json.hpp>
#include <miniz.h>
#include <algorithm>
#include <fstream>
#include <map>
#include <random>
#include <regex>
#include <set>
#include <stdexcept>
namespace StudioPacks
{
namespace
{
namespace fs = std::filesystem;
using Bytes = std::vector<unsigned char>;
using Json = nlohmann::json;
Bytes Read(const fs::path &path, std::uintmax_t limit = 128 * 1024 * 1024)
{
    auto length = fs::file_size(path);
    if (length > limit)
        throw std::runtime_error("Pack input is too large");
    Bytes bytes(static_cast<std::size_t>(length));
    std::ifstream stream(path, std::ios::binary);
    if (!stream.read(reinterpret_cast<char *>(bytes.data()), bytes.size()))
        throw std::runtime_error("Cannot read pack input");
    return bytes;
}
void Write(const fs::path &path, const Bytes &bytes)
{
    std::ofstream stream(path, std::ios::binary);
    if (!stream.write(reinterpret_cast<const char *>(bytes.data()), bytes.size()))
        throw std::runtime_error("Cannot write pack staging data");
}
struct Workspace
{
    fs::path path, parent;
    explicit Workspace(const fs::path &output)
    {
        parent = fs::canonical(output);
        std::random_device random;
        path = parent / (".import-" + std::to_string(random()) + "-" + std::to_string(random()));
        if (!fs::create_directory(path))
            throw std::runtime_error("Cannot create ISO work directory");
    }
    ~Workspace()
    {
        std::error_code error;
        // The tree was created exclusively by this operation. Check its resolved
        // parent again before any recursive cleanup; never follow a replaced root.
        if (!fs::is_symlink(path, error) && fs::weakly_canonical(path, error).parent_path() == parent)
            fs::remove_all(path, error);
    }
};
struct Zip
{
    Bytes bytes;
    mz_zip_archive archive{};
    std::map<std::string, mz_uint> entries;
    explicit Zip(const fs::path &path) : bytes(Read(path))
    {
        if (!mz_zip_reader_init_mem(&archive, bytes.data(), bytes.size(), 0))
            throw std::runtime_error("Cannot read image fixups ZIP");
        try
        {
            auto count = mz_zip_reader_get_num_files(&archive);
            if (count > 20001)
                throw std::runtime_error("Too many image fixup entries");
            for (mz_uint i = 0; i < count; ++i)
            {
                mz_zip_archive_file_stat stat{};
                if (!mz_zip_reader_file_stat(&archive, i, &stat) || stat.m_is_directory || stat.m_is_encrypted ||
                    !entries.emplace(stat.m_filename, i).second)
                    throw std::runtime_error("Invalid or duplicate image fixup member");
            }
        }
        catch (...)
        {
            mz_zip_reader_end(&archive);
            throw;
        }
    }
    ~Zip()
    {
        mz_zip_reader_end(&archive);
    }
    Bytes Get(const std::string &name, std::uint64_t limit)
    {
        auto found = entries.find(name);
        if (found == entries.end())
            throw std::runtime_error("Missing image fixup member");
        mz_zip_archive_file_stat stat{};
        if (!mz_zip_reader_file_stat(&archive, found->second, &stat) || stat.m_uncomp_size > limit)
            throw std::runtime_error("Image fixup member is too large");
        Bytes result(std::size_t(stat.m_uncomp_size));
        if (!mz_zip_reader_extract_to_mem(&archive, found->second, result.data(), result.size(), 0))
            throw std::runtime_error("Damaged image fixup member");
        return result;
    }
};
void Require(bool result, const std::string &error)
{
    if (!result)
        throw std::runtime_error(error);
}
std::set<std::string> Export(const fs::path &source, const fs::path &destination)
{
    fs::create_directory(destination);
    std::string error;
    Require(ThreeMM::ExportNickImages(source.string(), destination.string(), error), error);
    std::set<std::string> names;
    for (const auto &entry : fs::directory_iterator(destination))
        names.insert(entry.path().filename().string());
    return names;
}
std::string State(const fs::path &source, const fs::path &destination)
{
    std::string error;
    Require(ThreeMM::WriteNickNonImageState(source.string(), destination.string(), error), error);
    return HashFile(destination);
}
bool Digest(const Json &value)
{
    if (!value.is_string())
        return false;
    auto text = value.get<std::string>();
    return text.size() == 64 && std::all_of(text.begin(), text.end(),
                                            [](char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); });
}
} // namespace
void ImportISO(Id id, const fs::path &iso, const fs::path &baseISO, const fs::path &fixups, const fs::path &output)
{
    if (!fs::is_directory(output) || !fs::is_empty(output))
        throw std::runtime_error("ISO output must be empty staging");
    if (id == Id::Frankie)
        throw std::runtime_error("Install Frankie's Expansion from its extracted content folder");
    if (id == Id::Doraemon)
    {
        auto files = StudioISO::ReadFiles(iso, "CONTENTS", Definitions()[1].files);
        for (const auto &file : files)
            Write(output / file.first, file.second);
        return;
    }
    if (baseISO.empty() || fixups.empty())
        throw std::runtime_error("Nickelodeon ISO installation needs the base-game ISO and image fixups");
    const std::vector<std::string> discNames = {"ACTOR.NTH", "BKGDS.3CN", "BKGDS.NTH", "MTRL.NTH",
                                                "MTRLS.3CN", "PROP.NTH",  "SNDS.3CN",  "SOUND.NTH",
                                                "TDFS.3CN",  "TMPLS.3CN", "NICK3D.CHK"};
    auto source = StudioISO::ReadFiles(iso, "NICK3D", discNames);
    auto base = StudioISO::ReadFiles(baseISO, "3DMOVIE", {"3DMOVIE.CHK"}).at("3DMOVIE.CHK");
    Zip zip(fixups);
    auto rawManifest = zip.Get("manifest.json", 8 * 1024 * 1024);
    auto manifest = Json::parse(rawManifest.begin(), rawManifest.end());
    if (manifest.at("schema") != "3dmm-nick-image-fixups-1" ||
        manifest.at("rules") != "nick-chunk-rules-1-pencil-palette" || !manifest.at("images").is_array() ||
        manifest.at("images").empty() || manifest.at("images").size() > 20000 ||
        !Digest(manifest.at("non_image_state_sha256")))
        throw std::runtime_error("Unsupported image fixup package");
    Json inputs = Json::object();
    for (const auto &file : source)
        inputs[file.first] = Hash(file.second.data(), file.second.size());
    inputs["BASE/3DMOVIE.CHK"] = Hash(base.data(), base.size());
    if (inputs != manifest.at("inputs"))
        throw std::runtime_error("The selected discs do not match these image fixups");
    std::regex identity("[0-9]-(474c4352|4d424d50|544d4150)-[0-9a-f]{8}\\.bin");
    std::set<std::string> expectedImages, expectedMembers{"manifest.json"};
    std::uint64_t total = 0;
    for (const auto &row : manifest.at("images"))
    {
        auto name = row.at("name").get<std::string>();
        if (!std::regex_match(name, identity) || !expectedImages.insert(name).second ||
            !row.at("size").is_number_unsigned() || !row.at("size").get<std::uint64_t>() ||
            row.at("size").get<std::uint64_t>() > 128 * 1024 * 1024 || !Digest(row.at("sha256")))
            throw std::runtime_error("Invalid image chunk identity, size or digest");
        if (row.contains("mode"))
        {
            if ((row.at("mode") != "xor" && row.at("mode") != "add") || !Digest(row.at("source_sha256")) ||
                !Digest(row.at("delta_sha256")))
                throw std::runtime_error("Invalid image residual");
            expectedMembers.insert("images/" + name);
            total += row.at("size").get<std::uint64_t>();
        }
    }
    std::set<std::string> members;
    for (const auto &entry : zip.entries)
        members.insert(entry.first);
    if (members != expectedMembers || total > 512 * 1024 * 1024)
        throw std::runtime_error("Unexpected or oversized image fixup members");
    Workspace workspace(output);
    auto root = workspace.path;
    fs::create_directory(root / "source");
    fs::create_directory(root / "baseline");
    fs::create_directory(root / "corrections");
    fs::create_directory(root / "final");
    for (const auto &file : source)
        Write(root / "source" / file.first, file.second);
    Write(root / "base.chk", base);
    std::string error;
    ThreeMM::NickConversionStats stats;
    Require(ThreeMM::ConvertNickExpansion((root / "source").string(), (root / "base.chk").string(),
                                          (root / "baseline").string(), stats, error),
            error);
    if (State(root / "baseline", root / "baseline.state") != manifest.at("non_image_state_sha256"))
        throw std::runtime_error("Chunk conversion changed non-image data or metadata");
    if (Export(root / "baseline", root / "images") != expectedImages)
        throw std::runtime_error("Image identities differ from the fixup package");
    for (const auto &row : manifest.at("images"))
    {
        auto name = row.at("name").get<std::string>();
        auto image = Read(root / "images" / name);
        auto size = row.at("size").get<std::size_t>();
        if (row.contains("mode"))
        {
            if (Hash(image.data(), image.size()) != row.at("source_sha256"))
                throw std::runtime_error("Image predictor does not match the fixups");
            auto delta = zip.Get("images/" + name, size);
            if (delta.size() != size || Hash(delta.data(), delta.size()) != row.at("delta_sha256"))
                throw std::runtime_error("Damaged image residual");
            image.resize(size, 0);
            const bool xorMode = row.at("mode") == "xor";
            for (std::size_t i = 0; i < size; ++i)
                image[i] = xorMode ? image[i] ^ delta[i] : image[i] + delta[i];
            Write(root / "corrections" / name, image);
        }
        if (image.size() != size || Hash(image.data(), image.size()) != row.at("sha256"))
            throw std::runtime_error("Historical image verification failed");
    }
    Require(ThreeMM::ReplaceNickImages((root / "baseline").string(), (root / "corrections").string(),
                                       (root / "final").string(), error),
            error);
    if (State(root / "final", root / "final.state") != manifest.at("non_image_state_sha256") ||
        Export(root / "final", root / "saved") != expectedImages)
        throw std::runtime_error("Saving changed pack data or identities");
    for (const auto &row : manifest.at("images"))
        if (HashFile(root / "saved" / row.at("name").get<std::string>()) != row.at("sha256"))
            throw std::runtime_error("Saved image verification failed");
    for (const auto &name : Definitions()[0].files)
        fs::copy_file(root / "final" / name, output / name);
}
} // namespace StudioPacks
