// Licensed under the MIT License.
#include "studio_vxp_store.h"
#include "studio_pack_runtime.h"
#include "studio_settings.h"
#include "sdl_dialog.h"
#include "platform_text.h"
#include "text_utf8.h"
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
namespace V = ThreeMM::V3dmm;
std::unique_ptr<Store> store;
std::vector<Installation> active;
std::vector<fs::path> contentFiles, thumbnailFiles;
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
std::vector<fs::path> Files(const std::vector<Installation> &selection, const std::string &extension)
{
    // PhysicsFS exposes a sorted, deduplicated filename set. The first mount
    // supplying a name wins; keep package files separate in physical storage.
    std::map<std::string, fs::path> names;
    std::map<std::string, std::string> spellings;
    for (const auto &entry : selection)
        for (const auto &member : entry.members)
        {
            if (V::Extension(member.first) != extension)
                continue;
            auto lower = V::Fold(member.first);
            auto spelling = spellings.emplace(lower, member.first);
            if (!spelling.second && spelling.first->second != member.first)
                throw std::runtime_error("VXP packages have ambiguous filename casing: " + member.first);
            names.emplace(member.first, member.second);
        }
    std::vector<fs::path> result;
    for (const auto &name : names)
        result.push_back(name.second);
    return result;
}
void Activate(const std::vector<Installation> &selection)
{
    auto content = Files(selection, ".3cn");
    auto thumbnails = Files(selection, ".3th");
    std::uint64_t bytes = 0;
    for (auto extension : {".3cn", ".3th"})
        for (const auto &path : Files(selection, extension))
        {
            Filename name;
            if (!FilenameOf(path, &name))
                throw std::runtime_error("VXP content path cannot be represented by the movie engine");
            bytes += fs::file_size(path);
            if (bytes > V::Limits{}.expandedBytes)
                throw std::runtime_error("Active VXP content exceeds its disk budget");
        }
    active = selection;
    contentFiles = std::move(content);
    thumbnailFiles = std::move(thumbnails);
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
    auto lower = V::Fold(name);
    if (lower == "3dmm")
        return true;
    int index = lower == "nickelodeon" ? 0 : lower == "doraemon" ? 1 : lower == "3dmm expansion pack" ? 2 : -1;
    if (index < 0)
        return false;
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
    std::vector<fs::path> previousContent, previousThumbnails;
    std::vector<std::string> libraries;
    fs::path working;
    ~Document() override
    {
        if (changed && !committed && revision == activatedRevision)
        {
            active = previous;
            contentFiles = previousContent;
            thumbnailFiles = previousThumbnails;
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
        fs::path candidate;
        try
        {
            if (!movie || !MovieContainer::IsVmm(destination))
                throw std::runtime_error("This movie uses V3DMM content. Save it with a .vmm filename.");
            auto path = Path(destination);
            candidate = path.parent_path() / (".vmm-" + std::to_string(std::random_device{}()) + ".tmp");
            if (fs::exists(candidate))
                throw std::runtime_error("VMM save staging path already exists");
            std::vector<V::Dependency> dependencies{{"3dmm", {}}};
            std::set<std::string> names{"3dmm"};
            for (const auto &library : libraries)
                if (names.insert(V::Fold(library)).second)
                    dependencies.push_back({library, {}});
            for (const auto &entry : selection)
                if (names.insert(V::Fold(entry.name)).second)
                    dependencies.push_back({entry.name, V::WholeFile(entry.archive)});
            V::WriteBundle(V::WholeFile(Path(movie)), dependencies, candidate);
            V::Publish(candidate, path);
            bundled = true;
            return true;
        }
        catch (const std::exception &error)
        {
            if (!candidate.empty())
            {
                std::error_code ignored;
                fs::remove(candidate, ignored);
            }
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
        document->previousContent = contentFiles;
        document->previousThumbnails = thumbnailFiles;
        for (const auto &entry : store->Entries())
            if (entry.enabled)
                document->selection.push_back(entry);
        // Preserve legacy library dependencies, even when a movie does not use
        // every enabled pack. Portable exports must never embed these packs.
        const char *libraryNames[] = {"Nickelodeon", "Doraemon", "3DMM Expansion Pack"};
        for (auto name : libraryNames)
            if (LibraryAvailable(name))
                document->libraries.push_back(name);
        if (MovieContainer::IsVmm(original))
        {
            document->bundled = true;
            // A VMM records its own applied set. Installed/default packages
            // from another movie must not silently reappear in this document.
            document->selection.clear();
            auto bundle = V::ReadBundle(Path(original));
            for (const auto &dependency : bundle.dependencies)
                if (!dependency.archive.length && !LibraryAvailable(dependency.name))
                    throw std::runtime_error("This VMM needs an external expansion that is not installed or enabled: " +
                                             dependency.name + ". Use Options > Expansions.");
            for (const auto &dependency : bundle.dependencies)
            {
                if (!dependency.archive.length)
                {
                    document->libraries.push_back(dependency.name);
                    continue;
                }
                auto installed =
                    std::find_if(store->Entries().begin(), store->Entries().end(), [&](const Installation &entry) {
                        return V::Fold(entry.name) == V::Fold(dependency.name);
                    });
                Installation package;
                if (installed != store->Entries().end())
                {
                    package = *installed;
                }
                else
                {
                    auto metadata = V::ReadPackage(dependency.archive);
                    if (V::Fold(metadata.name) != V::Fold(dependency.name))
                        throw std::runtime_error("Embedded VXP name does not match its VMM dependency");
                    package = store->Install(dependency.archive);
                }
                auto found = std::find_if(
                    document->selection.begin(), document->selection.end(),
                    [&](const Installation &entry) { return V::Fold(entry.name) == V::Fold(package.name); });
                if (found == document->selection.end())
                    document->selection.push_back(std::move(package));
            }
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
            active.push_back(entry);
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
        selection.push_back(*installed);
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
    if (sid != 6 || !store)
        return false;
    if (index < 0)
        return true;
    const auto &files = type == kftgContent ? contentFiles : thumbnailFiles;
    return std::size_t(index) < files.size() && FilenameOf(files[index], path);
}
} // namespace StudioV3dmm
