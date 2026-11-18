#include "content.h"
#include "movie_input.h"
#include "studio_packs.h"
#include "studio_settings.h"
#include "studio_vmm_content.h"
#include "mm/vmm_dependencies.h"
#include <SDL3/SDL.h>
#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <random>
#include <stdexcept>

namespace fs = std::filesystem;
namespace V = MM::V3dmm;
namespace C = MM::Content;
struct MarcusContent::Impl
{
    std::shared_ptr<MarcusMovieFile> input;
    fs::path root, work;
    std::vector<fs::path> ownedFiles, ownedDirectories;
    std::unique_ptr<StudioV3dmm::Store> packages;
    std::vector<V::Dependency> dependencies{{mm_vmm_library_name(MM_VMM_BASE_LIBRARY), {}}};
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
        if (fs::exists(root / "v3dmm/catalog.sqlite3") || fs::exists(root / "v3dmm/catalog.json"))
            packages = std::make_unique<StudioV3dmm::Store>(root / "v3dmm");
        catalogRead = true;
    }
    StudioV3dmm::Installation Materialize(const V::Dependency &dependency, std::size_t index,
                                          const V::Package &metadata)
    {
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
    movie = impl->input->contentOrigin.empty() ? impl->input->path : impl->input->contentOrigin;
    std::ifstream header(movie, std::ios::binary);
    char signature[6] = {};
    header.read(signature, sizeof(signature));
    bundled = header.gcount() == 6 && std::string(signature, 6) == std::string("v3dmm\1", 6);
    if (!bundled)
    {
        movie = impl->input->path;
        return;
    }
    auto bundle = V::ReadBundle(movie);
    impl->dependencies = bundle.dependencies;
    const auto resolved = StudioV3dmm::ResolveMoviePackages(bundle,
        [&](const std::string &name) { return files.Mounted(C::SourceKey(name)); },
        [&] { impl->Catalog(); return impl->packages.get(); },
        [&](const V::Dependency &dependency, std::size_t index, const V::Package &metadata) {
            return impl->Materialize(dependency, index, metadata);
        });
    StudioV3dmm::MountMoviePackages(files, resolved.portable);
    movie = impl->Working() / "movie.3mm";
    impl->ownedFiles.push_back(movie);
    V::Copy(bundle.movie, movie);
    if (!impl->input->contentOrigin.empty()) movie = impl->input->path;
}
MarcusContent::~MarcusContent() = default;
void MarcusContent::PrepareSource(const std::string &name)
{
    if (!bundled)
    {
        const auto key = C::SourceKey(name);
        for (int id = MM_VMM_NICKELODEON; id < MM_VMM_LIBRARY_COUNT; ++id)
        {
            const auto library = mm_vmm_library_name(static_cast<MMVmmLibrary>(id));
            if (key == C::SourceKey(library) &&
                std::none_of(impl->dependencies.begin(), impl->dependencies.end(), [&](const auto &d) { return d.name == library; }))
                impl->dependencies.push_back({library, {}});
        }
    }
    if (bundled || C::SourceKey(name) != "v3dmm" || impl->defaultsRead)
        return;
    impl->Catalog();
    std::vector<StudioV3dmm::Installation> selection;
    if (impl->packages)
        for (const auto &entry : impl->packages->Entries())
            if (entry.enabled)
                selection.push_back(impl->packages->Resolve(entry.hash));
    StudioV3dmm::MountMoviePackages(files, selection);
    impl->defaultsRead = true;
    for (const auto &package : selection)
        impl->dependencies.push_back({package.name, V::WholeFile(package.archive)});
}
std::vector<V::Dependency> MarcusContent::Dependencies() const { return impl->dependencies; }
