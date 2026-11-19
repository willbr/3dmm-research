#include "media.h"
#include "iso_cache.h"
#include "pack_hash.h"
#include "studio_packs.h"
#include <SDL3/SDL.h>
#include <miniz.h>
#include <fstream>
#include <memory>
#include <random>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{
namespace fs = std::filesystem;
using StudioPacks::Definition;

void Require(bool ok, const char *message)
{
    if (!ok)
        throw std::runtime_error(message);
}

fs::path FindMedia(const char *explicitPath, const fs::path &beside, const fs::path &workspace)
{
    if (explicitPath)
        return fs::canonical(fs::u8path(explicitPath));
    for (const auto &candidate : {beside, workspace})
        if (!candidate.empty() && fs::is_regular_file(candidate))
            return fs::canonical(candidate);
    return {};
}

std::string Identity(const fs::path &path)
{
    if (path.empty())
        return "\n";
    return path.u8string() + '\n' + std::to_string(fs::file_size(path)) + '\n' +
           std::to_string(fs::last_write_time(path).time_since_epoch().count()) + '\n';
}

struct Staging
{
    fs::path parent, path;
    explicit Staging(const fs::path &directory) : parent(fs::canonical(directory))
    {
        std::random_device random;
        for (int attempt = 0; attempt != 32; ++attempt)
        {
            path = parent / (".media-" + std::to_string(random()) + "-" + std::to_string(random()));
            if (fs::create_directory(path))
                return;
        }
        throw std::runtime_error("Cannot create a private expansion staging directory");
    }
    ~Staging()
    {
        std::error_code error;
        if (!fs::is_symlink(path, error) && fs::weakly_canonical(path, error).parent_path() == parent && !error)
            fs::remove_all(path, error);
    }
};

void ExtractInstaller(const fs::path &archive, const fs::path &installer)
{
    constexpr std::size_t limit = 64u * 1024 * 1024;
    const auto length = fs::file_size(archive);
    Require(length && length <= limit, "The community Nickelodeon ZIP is too large or empty");
    std::vector<char> raw(static_cast<std::size_t>(length));
    std::ifstream file(archive, std::ios::binary);
    Require(bool(file.read(raw.data(), raw.size())), "Cannot read the community Nickelodeon ZIP");
    mz_zip_archive zip{};
    Require(mz_zip_reader_init_mem(&zip, raw.data(), raw.size(), 0) != 0,
            "Cannot open the community Nickelodeon ZIP");
    const auto closeZip = [](mz_zip_archive *value) { mz_zip_reader_end(value); };
    const std::unique_ptr<mz_zip_archive, decltype(closeZip)> guard(&zip, closeZip);
    const auto index = mz_zip_reader_locate_file(&zip, "N3DMMEXPb1a.exe", nullptr, 0);
    mz_zip_archive_file_stat member{};
    Require(index >= 0 && mz_zip_reader_file_stat(&zip, index, &member) && !member.m_is_directory &&
                !member.m_is_encrypted && member.m_uncomp_size && member.m_uncomp_size <= limit,
            "The community ZIP must contain N3DMMEXPb1a.exe");
    std::ofstream output(installer, std::ios::binary);
    const auto write = [](void *context, mz_uint64, const void *data, std::size_t count) -> std::size_t {
        auto &stream = *static_cast<std::ofstream *>(context);
        return stream.write(static_cast<const char *>(data), count) ? count : 0;
    };
    Require(mz_zip_reader_extract_to_callback(&zip, index, write, &output, 0) != 0,
            "Cannot read the installer payload from the community ZIP");
    output.close();
    Require(bool(output), "Cannot write the private installer payload");
}

void ExtractCommunity(const Definition &pack, const fs::path &archive, const fs::path &installer,
                      const fs::path &output)
{
    ExtractInstaller(archive, installer);
    const auto input = installer.u8string();
    const auto destination = "-o" + output.u8string();
    std::vector<std::string> names;
    for (const auto &name : pack.files)
        names.push_back(std::string(pack.sourceTitle) + "/" + name);
    const char *programs[] = {"7z", "7zz", "C:/Program Files/7-Zip/7z.exe", "C:/Program Files (x86)/7-Zip/7z.exe"};
    for (const auto *program : programs)
    {
        std::vector<const char *> args{program, "e", "-tNsis", "-y", "-spd", "-p-", destination.c_str(),
                                       "--", input.c_str()};
        for (const auto &name : names)
            args.push_back(name.c_str());
        args.push_back(nullptr);
        const auto properties = SDL_CreateProperties();
        SDL_SetPointerProperty(properties, SDL_PROP_PROCESS_CREATE_ARGS_POINTER, args.data());
        SDL_SetBooleanProperty(properties, SDL_PROP_PROCESS_CREATE_BACKGROUND_BOOLEAN, true);
        auto process = SDL_CreateProcessWithProperties(properties);
        SDL_DestroyProperties(properties);
        if (!process)
            continue;
        int status = -1;
        const auto exited = SDL_WaitProcess(process, true, &status);
        SDL_DestroyProcess(process);
        Require(exited && status == 0, "7-Zip could not read the Nickelodeon expansion installer");
        return;
    }
    throw std::runtime_error("Install 7-Zip to read the community Nickelodeon ZIP, or select the ISO and fixups");
}

bool Complete(const fs::path &directory, const Definition &pack)
{
    if (!fs::is_directory(fs::symlink_status(directory)))
        return false;
    for (const auto &name : pack.files)
    {
        const auto member = directory / fs::u8path(name);
        if (!fs::is_regular_file(fs::symlink_status(member)) || !fs::file_size(member))
            return false;
    }
    return true;
}

void PreparePack(const Definition &pack, const fs::path &root, const fs::path &iso,
                 const fs::path &base, const fs::path &fixups, const fs::path &community)
{
    const auto target = root / pack.sourceTitle;
    if (Complete(target, pack))
        return;
    Staging staging(root);
    const auto output = staging.path / pack.sourceTitle;
    fs::create_directory(output);
    if (community.empty())
        StudioPacks::ImportISO(pack.id, iso, base, fixups, output);
    else
        ExtractCommunity(pack, community, staging.path / "installer.exe", output);
    Require(Complete(output, pack), "The selected media is missing expansion archives");
    for (unsigned attempt = 0; ; ++attempt)
    {
        std::error_code error;
        fs::rename(output, target, error);
        if (!error || Complete(target, pack))
            return;
        if (attempt == 19 || fs::exists(target))
            throw std::runtime_error("Cannot publish " + std::string(pack.title) + " media: " + error.message());
        SDL_Delay(50);
    }
}

void CopyPath(char *output, std::size_t capacity, const std::string &value)
{
    Require(value.size() < capacity, "A Marcus media path is too long");
    SDL_strlcpy(output, value.c_str(), capacity);
}
}

extern "C" int marcus_prepare_media(const MarcusMediaOptions *options, const char *workspace,
                                    char *resources, std::size_t resourcesSize, char *content, std::size_t contentSize,
                                    char *description, std::size_t descriptionSize, char *error, std::size_t errorSize)
try
{
    const auto executable = SDL_GetBasePath();
    const auto beside = fs::u8path(executable ? executable : "");
    const auto source = fs::u8path(workspace);
    const auto iso = FindMedia(options->iso, beside / "3DMOVIE100.iso", source / "dist/3DMOVIE100.iso");
    const bool useFixups = options->nick_iso || options->nick_fixups;
    Require(!useFixups || (options->nick_iso && options->nick_fixups && !options->expansion_zip),
            "Select either --expansion-zip or both --nick-iso and --nick-fixups");
    if (iso.empty())
    {
        Require(!useFixups && !options->doraemon_iso && !options->expansion_zip, "Select a base game with --iso");
        return 1;
    }
    const auto doraemon = FindMedia(options->doraemon_iso, beside / "DORAEMON100.iso", source / "dist/DORAEMON100.iso");
    const auto community = useFixups ? fs::path{} : FindMedia(options->expansion_zip, beside / "n3dmmexpb1a-1.zip",
                                                            source / "expansions/n3dmmexpb1a-1.zip");
    const auto nick = useFixups ? FindMedia(options->nick_iso, {}, {}) : fs::path{};
    const auto fixups = useFixups ? FindMedia(options->nick_fixups, {}, {}) : fs::path{};
    const auto identity = Identity(doraemon) + Identity(community) + Identity(nick) + Identity(fixups);
    const auto key = StudioPacks::Hash(identity.data(), identity.size()).substr(0, 24);
    const std::unique_ptr<char, decltype(&SDL_free)> preferences(SDL_GetPrefPath("3DMMForever", "Marcus"), SDL_free);
    Require(bool(preferences), "Cannot find the private Marcus media cache");
    const auto cache = fs::u8path(preferences.get()) / "media-cache-v1" / key;
    const auto root = StudioISO::Prepare(iso, cache, {}, StudioISO::ProductNames::Lowercase).root;
    const auto &packs = StudioPacks::Definitions();
    if (useFixups || !community.empty())
        PreparePack(packs[0], root, nick, iso, fixups, community);
    if (!doraemon.empty())
        PreparePack(packs[1], root, doraemon, {}, {}, {});
    const auto product = root / "3D Movie Maker";
    std::ofstream marker(product / ".marcus-media", std::ios::app);
    Require(bool(marker), "Cannot mark the VFS media root");
    if (resources)
        CopyPath(resources, resourcesSize, (product / "3dmovie.chk").u8string());
    CopyPath(content, contentSize, product.u8string());
    std::string summary = iso.u8string();
    for (const auto &path : {doraemon, community, nick, fixups})
        if (!path.empty())
            summary += "\n" + path.u8string();
    CopyPath(description, descriptionSize, summary);
    return 1;
}
catch (const std::exception &failure)
{
    SDL_strlcpy(error, failure.what(), errorSize);
    return 0;
}
