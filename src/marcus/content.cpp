#include "content.h"
#include "movie_input.h"
#include "studio_packs.h"
#include "studio_settings.h"
#include "studio_vxp_store.h"
#include <SDL3/SDL.h>
#include <cstdlib>
#include <fstream>
#include <random>
#include <map>
#include <stdexcept>

namespace fs = std::filesystem;
namespace V = ThreeMM::V3dmm;
namespace C = ThreeMM::Content;
struct MarcusContent::Impl
{
    std::shared_ptr<MarcusMovieFile> input;
    fs::path root, work;
    std::vector<fs::path> ownedFiles, ownedDirectories;
    std::unique_ptr<StudioV3dmm::Store> packages;
    bool catalogRead = false, defaultsRead = false;
    ~Impl()
    {
        // Only remove files created by this session, never a shared installation.
        std::error_code ignored;
        for (auto i = ownedFiles.rbegin(); i != ownedFiles.rend(); ++i)
            fs::remove(*i, ignored);
        for (auto i = ownedDirectories.rbegin(); i != ownedDirectories.rend(); ++i)
            fs::remove(*i, ignored);
    }
    fs::path Working()
    {
        if (work.empty())
        {
            for (int attempt = 0; attempt < 16 && work.empty(); ++attempt)
            {
                auto candidate =
                    fs::temp_directory_path() / ("marcus-content-" + std::to_string(std::random_device{}()));
                if (fs::create_directory(candidate))
                {
                    work = candidate;
                    ownedDirectories.push_back(work);
                }
            }
            if (work.empty())
                throw std::runtime_error("Cannot create a private movie content directory");
        }
        return work;
    }
    void Catalog()
    {
        if (catalogRead)
            return;
        if (fs::exists(root / "v3dmm/catalog.json"))
            packages = std::make_unique<StudioV3dmm::Store>(root / "v3dmm");
        catalogRead = true;
    }
    void Activate(C::Vfs &files, const std::vector<StudioV3dmm::Installation> &selection)
    {
        std::vector<C::File> mounts;
        for (const auto &package : selection)
            for (const auto &member : package.members)
            {
                const auto extension = V::Extension(member.first);
                if (extension == ".3cn" || extension == ".3th")
                    mounts.push_back({member.first, member.second});
            }
        C::Vfs candidate;
        candidate.Replace("v3dmm", mounts);
        std::uint64_t bytes = 0;
        for (const auto &file : candidate.Entries("v3dmm"))
        {
            bytes += fs::file_size(file.physical);
            if (bytes > V::Limits{}.expandedBytes)
                throw std::runtime_error("Applied portable content exceeds its disk budget");
        }
        files.Replace("v3dmm", candidate.Entries("v3dmm"));
    }
    StudioV3dmm::Installation Package(const V::Dependency &dependency, std::size_t index, bool repeatedName)
    {
        Catalog();
        // A display name can describe multiple distinct shortnames in a VMM.
        // Name-only installed lookup must not replace every occurrence with
        // the first package. Disambiguate repeated names using their CFG roots.
        V::Package metadata;
        if (repeatedName)
        {
            metadata = V::ReadPackage(dependency.archive);
            if (V::Fold(metadata.name) != V::Fold(dependency.name))
                throw std::runtime_error("Embedded VXP name does not match its VMM dependency");
        }
        if (packages)
            for (const auto &entry : packages->Entries())
                if (V::Fold(entry.name) == V::Fold(dependency.name))
                {
                    if (!repeatedName ||
                        V::Fold(V::ReadPackage(V::WholeFile(entry.archive)).shortName) == V::Fold(metadata.shortName))
                        return entry;
                }
        if (!repeatedName)
            metadata = V::ReadPackage(dependency.archive);
        if (V::Fold(metadata.name) != V::Fold(dependency.name))
            throw std::runtime_error("Embedded VXP name does not match its VMM dependency");
        auto directory = Working() / ("package-" + std::to_string(index));
        if (!fs::create_directory(directory))
            throw std::runtime_error("Portable package staging path already exists");
        ownedDirectories.push_back(directory);
        for (const auto &member : metadata.members)
        {
            const auto extension = V::Extension(member.name);
            if (extension == ".3cn" || extension == ".3th")
                ownedFiles.push_back(directory / (std::to_string(member.index) + extension));
        }
        return {metadata.name, metadata.author,         {},
                true,          dependency.archive.file, V::Materialize(dependency.archive, directory)};
    }
};
MarcusContent::MarcusContent(const std::string &input, const std::string &baseDirectory)
    : MarcusContent(std::make_shared<MarcusMovieFile>(fs::u8path(input)), baseDirectory)
{
}
MarcusContent::MarcusContent(std::shared_ptr<MarcusMovieFile> input, const std::string &baseDirectory) : impl(new Impl)
{
    impl->input = std::move(input);
    const auto base = baseDirectory.empty() ? fs::path{} : fs::absolute(fs::u8path(baseDirectory));
    if (!base.empty() && fs::is_directory(base))
        files.MountDirectory("3dmovie", base);
    auto settings = fs::u8path(StudioSettings::IniPath());
    const auto overridePath = std::getenv("THREEDMM_PACK_DIR");
    impl->root = overridePath && *overridePath ? fs::u8path(overridePath) : settings.parent_path() / "content/packs";
    if (!impl->root.is_absolute())
        throw std::runtime_error("THREEDMM_PACK_DIR must be absolute");
    std::string preferenceText;
    if (StudioSettings::ReadString("User", "ExpansionPacks", preferenceText) == KauaiSettings::ReadResult::Error)
        throw std::runtime_error("Cannot read Studio's expansion preferences");
    const auto preferences = StudioPacks::Parse(preferenceText);
    StudioPacks::Store libraries(impl->root);
    for (std::size_t i = 0; i < preferences.size(); ++i)
    {
        const auto &preference = preferences[i];
        const auto &definition = StudioPacks::Definitions()[i];
        if (!preference.enabled || preference.remove)
            continue;
        const auto info = libraries.Inspect(definition.id, preference);
        if (info.installed)
        {
            if (info.valid)
                files.MountDirectory(definition.sourceFolder, fs::u8path(info.path));
            continue; // Managed installations take precedence, including invalid ones.
        }
        for (const auto &parent : {base, base.parent_path()})
        {
            if (parent.empty())
                continue;
            for (const auto *folder : {definition.sourceFolder, definition.sourceTitle})
                if (fs::is_directory(parent / folder))
                {
                    files.MountDirectory(definition.sourceFolder, parent / folder);
                    break;
                }
            if (files.Mounted(definition.sourceFolder))
                break;
        }
    }
    movie = impl->input->path;
    std::ifstream header(movie, std::ios::binary);
    char signature[6] = {};
    header.read(signature, sizeof(signature));
    bundled = header.gcount() == 6 && std::string(signature, 6) == std::string("v3dmm\1", 6);
    if (!bundled)
        return;
    auto bundle = V::ReadBundle(movie);
    std::vector<StudioV3dmm::Installation> selection;
    std::map<std::string, std::size_t> names;
    for (const auto &dependency : bundle.dependencies)
        if (dependency.archive.length)
            ++names[V::Fold(dependency.name)];
    for (std::size_t i = 0; i < bundle.dependencies.size(); ++i)
    {
        const auto &dependency = bundle.dependencies[i];
        if (!dependency.archive.length)
        {
            const auto key = C::SourceKey(dependency.name);
            if (!files.Mounted(key))
                throw std::runtime_error("This VMM requires an unavailable or disabled library: " + dependency.name);
        }
        else
            selection.push_back(impl->Package(dependency, i, names[V::Fold(dependency.name)] > 1));
    }
    impl->Activate(files, selection);
    movie = impl->Working() / "movie.3mm";
    impl->ownedFiles.push_back(movie);
    V::Copy(bundle.movie, movie);
}
MarcusContent::~MarcusContent() = default;
void MarcusContent::PrepareSource(const std::string &name)
{
    if (bundled || C::SourceKey(name) != "v3dmm" || impl->defaultsRead)
        return;
    impl->Catalog();
    std::vector<StudioV3dmm::Installation> selection;
    if (impl->packages)
        for (const auto &entry : impl->packages->Entries())
            if (entry.enabled)
                selection.push_back(entry);
    impl->Activate(files, selection);
    impl->defaultsRead = true;
}
