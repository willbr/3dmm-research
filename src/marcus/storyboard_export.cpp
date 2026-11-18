#include "storyboard_native.h"
#undef size
#include "engine_archive_stream.h"
#include "engine_read_stream.h"
#include "mm/chunk_edit.h"
#include "mm/movie_portable.h"
#include <miniz.h>
#include <fstream>
#include <random>
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
struct ChildReplacement
{
    PChunkyFile file;
    MMU32 parentType, parentNumber;
    MMChunkChild child;
    bool linked;
    static int Receive(void *context, const unsigned char *bytes, MMU32 count)
    {
        const auto &host = *static_cast<ChildReplacement *>(context);
        const MMChunkChildPayload services = {
            host.file,
            [](void *c, MMU32 type, const unsigned char *data, MMU32 length, MMU32 *number) -> int {
                return static_cast<PChunkyFile>(c)->FAddPv(const_cast<unsigned char *>(data), length, type, number);
            },
            [](void *c, MMU32 parentType, MMU32 parent, const MMChunkChild *child) -> int {
                return static_cast<PChunkyFile>(c)->FAdoptChild(parentType, parent, child->type, child->number, child->slot);
            },
            [](void *c, MMU32 parentType, MMU32 parent, const MMChunkChild *child, int linked) {
                const auto file = static_cast<PChunkyFile>(c);
                if (linked) file->DeleteChild(parentType, parent, child->type, child->number, child->slot);
                else file->Delete(child->type, child->number);
            }};
        return mm_chunk_replace_child_bytes(host.parentType, host.parentNumber, &host.child,
                                             host.linked, bytes, count, &services);
    }
};

void PortableSource(PChunkyFile file)
{
    ChunkIdentification root;
    ChildChunkIdentification child{};
    DataBlock block;
    Require(file->FGetCkiCtg(kctgMvie, 0, &root), "Missing movie root.");
    const bool hadSources = file->FGetKidChidCtg(kctgMvie, root.cno, 1, kctgGst, &child);
    MMReadStream stream{};
    if (hadSources)
    {
        Require(file->FFind(kctgGst, child.cki.cno, &block) && block.FUnpackData(), "Cannot read movie sources.");
        stream = EngineReadStream::FromBlock(block);
    }
    ChildReplacement output{file, kctgMvie, root.cno, {kctgGst, child.cki.cno, 1}, hadSources};
    const unsigned short one = 1;
    const auto result = mm_movie_ensure_portable_source(hadSources ? &stream : nullptr, koskCur,
        *reinterpret_cast<const unsigned char *>(&one) == 0, ChildReplacement::Receive, &output);
    Require(result != MM_PORTABLE_SOURCE_CONFLICT, "Source ID 6 is already used by a different library.");
    Require(result != MM_PORTABLE_SOURCE_INVALID, "Invalid movie sources.");
    Require(result == MM_PORTABLE_SOURCE_OK, "Cannot write movie sources.");
}

using Index = std::unique_ptr<MMChunkIndex, decltype(&mm_chunk_index_close)>;
using SceneBackgrounds = std::vector<std::pair<ChunkNumber, ChunkNumber>>;

Index ReadIndex(std::istream &file)
{
    const auto policy = EngineArchiveStream::CompactPolicy();
    MMChunkIndex *index = nullptr;
    MMReadStream stream;
    if (EngineArchiveStream::FromFile(file, stream))
        mm_chunk_index_open(&stream, &policy, &index);
    return Index(index, mm_chunk_index_close);
}

void RekeyBackgrounds(const fs::path &contentPath, const std::vector<fs::path> &portableContent,
                      std::random_device &random, SceneBackgrounds &backgrounds)
{
    std::vector<Index> indexes;
    std::vector<MMChunkAccess> occupied;
    for (const auto &path : portableContent)
    {
        std::ifstream file(path, std::ios::binary);
        auto index = ReadIndex(file);
        Require(bool(index), "Cannot inspect existing portable content.");
        occupied.push_back(mm_chunk_index_access(index.get()));
        indexes.push_back(std::move(index));
    }
    std::fstream file(contentPath, std::ios::binary | std::ios::in | std::ios::out);
    const auto index = ReadIndex(file);
    Require(bool(index), "Cannot inspect packaged storyboard content.");
    const auto access = mm_chunk_index_access(index.get());
    std::vector<MMU32> numbers(access.count);
    const MMU32 seed = random(), step = random();
    const auto result = mm_movie_rekey_portable_chunks(index.get(), occupied.data(), occupied.size(),
        seed, step, numbers.data(), [](void *context, MMU32 offset, const unsigned char *bytes, MMU32 count) -> int {
            auto &out = *static_cast<std::fstream *>(context);
            out.seekp(offset).write(reinterpret_cast<const char *>(bytes), count).flush();
            return bool(out);
        }, &file);
    Require(result == MM_CHUNK_OK, "Cannot assign storyboard package chunk numbers.");
    for (auto &entry : backgrounds)
    {
        MMU32 position;
        Require(mm_chunk_find(&access, kctgBkgd, entry.second, &position) == MM_CHUNK_OK,
            "Cannot locate packaged storyboard background.");
        entry.second = numbers[position];
    }
}
}

void MarcusRetargetStoryboardScene(PChunkyFile file, ChunkNumber scene, LegacyLong source, ChunkNumber background)
{
    ChildChunkIdentification child;
    DataBlock block;
    Require(file->FGetKidChidCtg(kctgScen, scene, 1, kctgStartGg, &child) &&
        file->FFind(kctgStartGg, child.cki.cno, &block) && block.FUnpackData(), "Cannot read storyboard scene events.");
    const auto stream = EngineReadStream::FromBlock(block);
    ChildReplacement output{file, kctgScen, scene, {kctgStartGg, child.cki.cno, 1}, true};
    Require(mm_scene_retarget_start_background(&stream, source, background, ChildReplacement::Receive, &output),
        "Cannot write storyboard background references.");
}

void MarcusWriteStoryboardBundle(const MarcusMovieFile &movie, const MarcusStoryboards &boards,
    const std::string &resources, const std::string &content, const std::vector<V::Dependency> &dependencies,
    const std::vector<fs::path> &portableContent, const fs::path &destination)
{
    Staging work;
    const auto innerPath = work.Path("movie.3mm"), contentPath = work.Path("backgrounds.3cn"), thumbnailsPath = work.Path("backgrounds.3th");
    fs::copy_file(movie.path, innerPath);
    fs::permissions(innerPath, fs::perms::owner_write, fs::perm_options::add);
    auto inner = Open(innerPath, false), contents = Open(contentPath, true);
    SceneBackgrounds backgrounds;
    for (const auto &entry : boards)
    {
        // Regenerate from editor data, repairing older storyboard palettes too.
        const auto resource = MarcusBoardContent(*entry.second.board, MM_PORTABLE_SOURCE_ID, resources, content);
        auto source = Open(resource.file->path, false);
        ChunkNumber background;
        Require(source->FCopy(kctgBkgd, resource.background, contents.get(), &background), "Cannot package storyboard background.");
        backgrounds.emplace_back(entry.first, background);
    }
    Require(contents->FSave(kctgSoc), "Cannot finish portable storyboard content.");
    contents.reset();
    std::random_device random;
    RekeyBackgrounds(contentPath, portableContent, random, backgrounds);
    for (const auto &entry : backgrounds)
        MarcusRetargetStoryboardScene(inner.get(), entry.first, MM_PORTABLE_SOURCE_ID, entry.second);
    PortableSource(inner.get());
    Require(inner->FSave(kctgSoc), "Cannot finish portable storyboard movie.");
    inner.reset();
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
    V::WriteStoryboardBundle(V::WholeFile(innerPath), dependencies, {name, V::WholeFile(archivePath)}, destination);
}
