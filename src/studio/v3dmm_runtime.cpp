// Licensed under the MIT License.
#include "studio_vmm_content.h"
#include "mm/vmm_dependencies.h"
#include "studio_pack_runtime.h"
#include "studio_settings.h"
#include "sdl_dialog.h"
#include "platform_text.h"
#include "text_utf8.h"
#include "mm/content_vfs.hpp"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <random>
#include <set>
#include <stdexcept>
#include "studio.h"
#include "studio_pack_options.h"
#include "studio_v3dmm.h"
#undef size
ASSERTNAME
namespace StudioV3dmm
{
namespace
{
namespace fs = std::filesystem;
namespace V = MM::V3dmm;
std::unique_ptr<Store> store;
std::vector<Installation> active;
std::uint64_t revision = 0;
fs::path Path(PFilename filename)
{
    String text;
    filename->GetStnPath(&text);
    return fs::u8path(StudioPacks::UTF8(&text));
}
bool FilenameOf(const fs::path &path, PFilename result, FileType type = ftgNil)
{
    String text;
    return StudioPacks::SetText(&text, path.u8string()) && result->FBuildFromPath(&text, type);
}
void Report(const std::exception &error)
{
    std::fprintf(stderr, "v3dmm: %s\n", error.what());
    const auto batch = std::getenv("THREEDMM_BATCH_TEST");
    if (!batch || std::string(batch) != "1")
    {
        KauaiDialogs::Session alert;
        if (alert.InitAlert(error.what(), "V3DMM content", KauaiDialogs::AlertButtons::ok))
            KauaiSDL::RunDialog(alert);
    }
    PushErc(ercSocBadFile);
}
void Activate(const std::vector<Installation> &selection)
{
    auto next = selection;
    MountMoviePackages(StudioPacks::Filesystem(), next, [](const fs::path &path) {
        Filename name;
        if (!FilenameOf(path, &name))
            throw std::runtime_error("VXP content path cannot be represented by the movie engine");
    });
    active.swap(next);
    ++revision;
    if (vptagm)
        vptagm->InvalidateSource(6);
}
bool SameSelection(const std::vector<Installation> &left, const std::vector<Installation> &right)
{
    return left.size() == right.size() &&
           std::equal(left.begin(), left.end(), right.begin(),
                      [](const Installation &a, const Installation &b) { return a.hash == b.hash; });
}
bool LibraryAvailable(const std::string &name)
{
    const auto library = mm_vmm_library_id(name.c_str());
    if (library == MM_VMM_BASE_LIBRARY)
        return true;
    if (library == MM_VMM_UNKNOWN_LIBRARY)
        return false;
    const auto index = library - MM_VMM_NICKELODEON;
    auto products = StudioPacks::Products();
    auto source = std::to_string(StudioPacks::ActivePreferences()[index].sourceId);
    return std::any_of(products.entries.begin(), products.entries.end(), [&](const StudioContent::Product &product) {
        return std::string(product.sourceId.begin(), product.sourceId.end()) == source;
    });
}
struct Document : MovieContainer::Document
{
    bool bundled = false, committed = false, changed = false, explicitSelection = false;
    std::uint64_t activatedRevision = 0;
    std::vector<Installation> previous, selection;
    std::vector<MM::Content::File> previousMounts;
    std::vector<std::string> libraries;
    fs::path working;
    ~Document() override
    {
        if (changed && !committed && revision == activatedRevision)
        {
            active = previous;
            StudioPacks::Filesystem().Replace("v3dmm", previousMounts);
            ++revision;
            if (vptagm)
                vptagm->InvalidateSource(6);
        }
        if (!working.empty())
        {
            std::error_code ignored;
            fs::remove(working, ignored); // The engine may still own it; never force deletion.
        }
    }
    bool Bundled() const override
    {
        return bundled || (explicitSelection && !selection.empty());
    }
    bool Commit() override
    {
        committed = true;
        return changed;
    }
    bool Save(PFilename movie, PFilename destination) override
    {
        try
        {
            if (!movie || !MovieContainer::IsVmm(destination))
                throw std::runtime_error("This movie uses V3DMM content. Save it with a .vmm filename.");
            V::SaveAtomically(Path(destination), [&](const fs::path &candidate) {
                const auto dependencies = ActiveMovieDependencies(libraries, selection);
                V::WriteBundle(V::WholeFile(Path(movie)), dependencies, candidate);
            });
            bundled = true;
            return true;
        }
        catch (const std::exception &error)
        {
            Report(error);
            return false;
        }
    }
};
bool Prepare(PFilename original, PFilename working, std::shared_ptr<MovieContainer::Document> &result)
{
    try
    {
        auto document = std::make_shared<Document>();
        document->previous = active;
        document->previousMounts = StudioPacks::Filesystem().Entries("v3dmm");
        if (!MovieContainer::IsVmm(original))
            for (const auto &entry : store->Entries())
                if (entry.enabled)
                    document->selection.push_back(store->Resolve(entry.hash));
        // Preserve legacy library dependencies, even when a movie does not use
        // every enabled pack. Portable exports must never embed these packs.
        for (int id = MM_VMM_NICKELODEON; id < MM_VMM_LIBRARY_COUNT; ++id)
        {
            const auto name = mm_vmm_library_name(static_cast<MMVmmLibrary>(id));
            if (LibraryAvailable(name))
                document->libraries.push_back(name);
        }
        if (MovieContainer::IsVmm(original))
        {
            document->bundled = true;
            // A VMM records its own applied set. Installed/default packages
            // from another movie must not silently reappear in this document.
            auto bundle = V::ReadBundle(Path(original));
            auto resolved = ResolveMoviePackages(bundle, LibraryAvailable, [] { return store.get(); });
            document->selection = std::move(resolved.portable);
            document->libraries.insert(document->libraries.end(), resolved.libraries.begin(), resolved.libraries.end());
            auto work = store->Root() / "work";
            fs::create_directories(work);
            document->working = work / ("movie-" + std::to_string(std::random_device{}()) + ".3mm");
            if (fs::exists(document->working))
                throw std::runtime_error("VMM working path already exists");
            V::Copy(bundle.movie, document->working);
            if (!FilenameOf(document->working, working))
                throw std::runtime_error("VMM working path cannot be represented by the movie engine");
        }
        if (!SameSelection(document->selection, active))
        {
            Activate(document->selection);
            document->activatedRevision = revision;
            document->changed = true;
        }
        result = std::move(document);
        return true;
    }
    catch (const std::exception &error)
    {
        Report(error);
        return false;
    }
}
} // namespace
void Initialize()
{
    auto overridePath = std::getenv("THREEDMM_PACK_DIR");
    auto root = overridePath && *overridePath ? fs::u8path(overridePath)
                                              : fs::u8path(StudioSettings::IniPath()).parent_path() / "content/packs";
    store.reset(new Store(root / "v3dmm"));
    active.clear();
    for (const auto &entry : store->Entries())
        if (entry.enabled)
            active.push_back(store->Resolve(entry.hash));
    Activate(active);
    MovieContainer::prepare = Prepare;
}
Store &Packages()
{
    return *store;
}
std::vector<std::string> AppliedPackages(PMovie movie)
{
    auto document = movie ? dynamic_cast<Document *>(movie->ContainerDocument()) : nullptr;
    std::vector<std::string> result;
    if (document)
        for (const auto &entry : document->selection)
            result.push_back(entry.hash);
    return result;
}
void SetAppliedPackages(PMovie movie, const std::vector<std::string> &hashes)
{
    auto document = movie ? dynamic_cast<Document *>(movie->ContainerDocument()) : nullptr;
    if (!document)
        throw std::runtime_error("Open a movie before applying packages.");
    if (movie->FPlaying())
        throw std::runtime_error("Stop playback before changing the movie's packages.");
    std::vector<Installation> selection;
    std::set<std::string> seen;
    for (const auto &hash : hashes)
    {
        const auto &entries = store->Entries();
        const auto installed =
            std::find_if(entries.begin(), entries.end(), [&](const Installation &entry) { return entry.hash == hash; });
        if (installed == entries.end() || !seen.insert(hash).second)
            throw std::runtime_error("An applied package is missing or selected more than once.");
        selection.push_back(store->Resolve(installed->hash));
    }
    if (SameSelection(selection, document->selection))
        return;
    Activate(selection); // Validate the complete new source list before changing the document.
    document->selection = std::move(selection);
    document->explicitSelection = true;
    document->changed = document->committed = true;
    document->activatedRevision = revision;
    movie->SetDirty();
    if (auto studio = vpapp->Pstdio())
        studio->ReleaseBrcn();
}
std::string Display(const std::string &bytes)
{
    auto count = KauaiText::ToUtf16(bytes.data(), bytes.size(), KauaiText::Encoding::WindowsAnsi, nullptr, 0);
    std::vector<std::uint16_t> units(count);
    std::string text;
    if (count && KauaiText::ToUtf16(bytes.data(), bytes.size(), KauaiText::Encoding::WindowsAnsi, units.data(), count))
        KauaiText::EncodeUtf8(units, text);
    return text;
}
bool Resolve(PString longName, PString shortName, PFilename path)
{
    auto title = V::Fold(StudioPacks::UTF8(longName));
    auto folder = V::Fold(StudioPacks::UTF8(shortName));
    if (title != "virtual3dmm" && folder != "v3dmm")
        return false;
    if (active.empty())
        return false;
    return FilenameOf(store->Root(), path, kftgDir);
}
bool Archive(LegacyLong sid, FileType type, LegacyLong index, PFilename path)
{
    std::string source;
    if (sid == 6)
        source = "v3dmm";
    else
        for (const auto &product : StudioPacks::Products().entries)
            if (std::string(product.sourceId.begin(), product.sourceId.end()) == std::to_string(sid))
                source = MM::Content::SourceKey(std::string(product.title.begin(), product.title.end()));
    auto &vfs = StudioPacks::Filesystem();
    if (source.empty() || !vfs.Mounted(source))
        return false;
    if (index < 0)
        return true;
    const auto files = vfs.Files(source, type == kftgContent ? ".3cn" : ".3th");
    return std::size_t(index) < files.size() && FilenameOf(files[index], path);
}
} // namespace StudioV3dmm
