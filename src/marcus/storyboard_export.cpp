#include "storyboard_native.h"
#undef size
#include "mm/content_vfs.hpp"
#include <miniz.h>
#include <algorithm>
#include <random>
#include <set>
#include <stdexcept>
ASSERTNAME

namespace
{
namespace fs = std::filesystem;
namespace V = MM::V3dmm;
struct Release { template<class T> void operator()(T *p) const { if (p) p->Release(); } };
using File = std::unique_ptr<ChunkyFile, Release>;
void Require(bool ok, const char *message) { if (!ok) throw std::runtime_error(message); }
File Open(const fs::path &path, bool create)
{
    String text; Filename filename;
    text.SetSzs(const_cast<char *>(path.u8string().c_str()));
    Require(filename.FBuildFromPath(&text), "Invalid storyboard export path.");
    File file(create ? ChunkyFile::PcflCreate(&filename, fcflWriteEnable) : ChunkyFile::PcflOpen(&filename, fcflWriteEnable));
    Require(bool(file), "Cannot open storyboard export staging file.");
    return file;
}
struct Staging
{
    fs::path directory;
    std::vector<fs::path> files;
    Staging()
    {
        for (int attempt = 0; attempt < 100; ++attempt)
        {
            auto path = fs::temp_directory_path() / ("marcus-export-" + std::to_string(std::random_device{}()));
            if (fs::create_directory(path)) { directory = path; return; }
        }
        throw std::runtime_error("Cannot reserve storyboard export staging.");
    }
    fs::path Path(const std::string &name) { auto path = directory / name; files.push_back(path); return path; }
    ~Staging()
    {
        std::error_code ignored;
        for (const auto &file : files) fs::remove(file, ignored);
        fs::remove(directory, ignored);
    }
};
void PortableSource(PChunkyFile file)
{
    ChunkIdentification root; ChildChunkIdentification child; DataBlock block;
    Require(file->FGetCkiCtg(kctgMvie, 0, &root), "Missing movie root.");
    const bool hadSources = file->FGetKidChidCtg(kctgMvie, root.cno, 1, kctgGst, &child);
    short order = kboCur;
    std::unique_ptr<StringTable_GST, Release> sources;
    if (hadSources)
    {
        Require(file->FFind(kctgGst, child.cki.cno, &block), "Cannot read movie sources.");
        sources.reset(StringTable_GST::PgstRead(&block, &order));
    }
    else sources.reset(StringTable_GST::PgstNew(sizeof(LegacyLong)));
    Require(sources && sources->CbExtra() == sizeof(LegacyLong), "Invalid movie sources.");
    bool found = false;
    for (LegacyLong i = 0; i < sources->IvMac(); ++i)
    {
        LegacyLong id; sources->GetExtra(i, &id);
        if (order == kboOther) { SwapBytesRglw(&id, 1); sources->PutExtra(i, &id); }
        if (id != 6) continue;
        String title; schar text[1024]; sources->GetStn(i, &title); title.GetSzs(text);
        Require(MM::Content::SourceKey(text) == "v3dmm", "Source ID 6 is already used by a different library.");
        found = true;
    }
    if (!found)
    {
        String name; name.SetSzs(PszLit("Virtual3dmm/v3dmm")); LegacyLong id = 6;
        Require(sources->FAddStn(&name, &id), "Cannot add portable background source.");
    }
    ChunkNumber number;
    Require(file->FAdd(sources->CbOnFile(), kctgGst, &number, &block) && sources->FWrite(&block), "Cannot write movie sources.");
    if (hadSources) file->DeleteChild(kctgMvie, root.cno, kctgGst, child.cki.cno, 1);
    Require(file->FAdoptChild(kctgMvie, root.cno, kctgGst, number, 1), "Cannot link movie sources.");
}
void SceneBackground(PChunkyFile file, ChunkNumber scene, ChunkNumber background)
{
    ChildChunkIdentification child; DataBlock block;
    Require(file->FGetKidChidCtg(kctgScen, scene, 1, kctgStartGg, &child) && file->FFind(kctgStartGg, child.cki.cno, &block),
        "Cannot read storyboard scene events.");
    short order;
    std::unique_ptr<GeneralGroup, Release> starts(SceneData::PggReadEvents(&block, &order, true));
    Require(bool(starts), "Invalid storyboard scene events.");
    bool found = false;
    for (LegacyLong i = 0; i < starts->IvMac(); ++i)
    {
        SceneEvent event; starts->GetFixed(i, &event);
        if (order == kboOther) SwapBytesBom(&event, kbomSev);
        if (event.sevt != sevtSetBkgd) continue;
        TAGOnFile tag = {6, 0, kctgBkgd, background};
        if (order == kboOther) SwapBytesBom(&tag, kbomTag);
        starts->Put(i, &tag); found = true;
    }
    ChunkNumber number;
    Require(found && file->FAdd(starts->CbOnFile(), kctgStartGg, &number, &block) && starts->FWrite(&block, order),
        "Cannot write portable background references.");
    file->DeleteChild(kctgScen, scene, kctgStartGg, child.cki.cno, 1);
    Require(file->FAdoptChild(kctgScen, scene, kctgStartGg, number, 1), "Cannot link portable background references.");
}
}

void MarcusWriteStoryboardBundle(const MarcusMovieFile &movie, const MarcusStoryboards &boards,
    const std::string &resources, const std::string &content, std::vector<V::Dependency> dependencies,
    const std::vector<fs::path> &portableContent, const fs::path &destination)
{
    Staging work;
    const auto innerPath = work.Path("movie.3mm"), contentPath = work.Path("backgrounds.3cn"), thumbnailsPath = work.Path("backgrounds.3th");
    fs::copy_file(movie.path, innerPath);
    fs::permissions(innerPath, fs::perms::owner_write, fs::perm_options::add);
    auto inner = Open(innerPath, false), contents = Open(contentPath, true);
    std::set<std::pair<unsigned long, unsigned long>> occupied;
    for (const auto &path : portableContent)
    {
        MM::Archive archive; MM::Error error;
        Require(archive.Open(path.u8string(), error), "Cannot inspect existing portable content.");
        for (const auto &chunk : archive.Chunks()) occupied.insert({chunk.id.type, chunk.id.number});
    }
    std::map<ChunkNumber, ChunkNumber> backgrounds;
    for (const auto &entry : boards)
    {
        // Regenerate from editor data, repairing older storyboard palettes too.
        const auto resource = MarcusBoardContent(*entry.second.board, 6, resources, content);
        auto source = Open(resource.file->path, false);
        ChunkNumber background;
        Require(source->FCopy(kctgBkgd, resource.background, contents.get(), &background), "Cannot package storyboard background.");
        backgrounds[entry.first] = background;
    }
    // Source 6 is shared by portable packages. Every resource needs a unique ID.
    std::vector<ChunkIdentification> chunks;
    ChunkIdentification chunk;
    for (LegacyLong i = 0; contents->FGetCki(i, &chunk); ++i) chunks.push_back(chunk);
    std::random_device random;
    for (const auto &old : chunks)
    {
        ChunkNumber number;
        do { number = 0x80000000UL | (random() & 0x7ffffffeUL); }
        while (occupied.count({old.ctg, number}) || contents->FFind(old.ctg, number));
        contents->Move(old.ctg, old.cno, old.ctg, number);
        occupied.insert({old.ctg, number});
        if (old.ctg == kctgBkgd)
            for (auto &entry : backgrounds) if (entry.second == old.cno) entry.second = number;
    }
    for (const auto &entry : backgrounds) SceneBackground(inner.get(), entry.first, entry.second);
    PortableSource(inner.get());
    Require(contents->FSave(kctgSoc) && inner->FSave(kctgSoc), "Cannot finish portable storyboard content.");
    contents.reset(); inner.reset();
    auto thumbnails = Open(thumbnailsPath, true);
    Require(thumbnails->FSave(kctgSoc), "Cannot write storyboard thumbnail archive."); thumbnails.reset();
    // New names avoid the original manager reusing a stale, installed package.
    const auto shortName = "marcus_storyboard_" + std::to_string(random()) + "_" + std::to_string(random());
    const auto name = "Marcus Storyboard " + shortName.substr(18);
    const auto archivePath = work.Path("backgrounds.vxp");
    const std::string cfg = "Name=" + name + "\r\nAuthor=Marcus\r\nType=Portable\r\nContent=Backgrounds\r\nMarcusStoryboard=1\r\n";
    struct Zip { mz_zip_archive archive = {}; bool closed = false; ~Zip() { if (!closed) mz_zip_writer_end(&archive); } } zip;
    Require(mz_zip_writer_init_file(&zip.archive, archivePath.u8string().c_str(), 0) &&
        mz_zip_writer_add_mem(&zip.archive, (shortName + ".cfg").c_str(), cfg.data(), cfg.size(), MZ_BEST_COMPRESSION) &&
        mz_zip_writer_add_file(&zip.archive, (shortName + ".3cn").c_str(), contentPath.u8string().c_str(), nullptr, 0, MZ_BEST_COMPRESSION) &&
        mz_zip_writer_add_file(&zip.archive, (shortName + ".3th").c_str(), thumbnailsPath.u8string().c_str(), nullptr, 0, MZ_BEST_COMPRESSION) &&
        mz_zip_writer_finalize_archive(&zip.archive), "Cannot write storyboard VXP package.");
    Require(mz_zip_writer_end(&zip.archive), "Cannot close storyboard VXP package."); zip.closed = true;
    // Replace our old background-only packages; retain all other dependencies.
    dependencies.erase(std::remove_if(dependencies.begin(), dependencies.end(), [](const auto &dependency) {
        return dependency.archive.length && V::ReadPackage(dependency.archive).metadata["marcusstoryboard"] == "1";
    }), dependencies.end());
    dependencies.push_back({name, V::WholeFile(archivePath)});
    V::WriteBundle(V::WholeFile(innerPath), dependencies, destination);
}
