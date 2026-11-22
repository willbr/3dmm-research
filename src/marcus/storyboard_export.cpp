#include "storyboard.h"
#include "archive_file.h"
#include "soc_core.h"
#undef size
#include "engine_archive_stream.h"
#include "mm/movie_portable.h"
#include "mm/vxp_zip.h"
#include <fstream>
#include <random>
#include <stdexcept>
ASSERTNAME

namespace
{
namespace fs = std::filesystem;
namespace V = MM::V3dmm;
void Require(bool ok, const char *message) { if (!ok) throw std::runtime_error(message); }
MarcusArchive Open(const fs::path &path, int mode)
{
    auto file = MarcusOpenArchive(path, mode);
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
void PortableSource(const MMMovieArchiveEditor &editor)
{
    const auto result = mm_movie_archive_require_portable_source(&editor);
    Require(result != MM_PORTABLE_SOURCE_MISSING_ROOT, "Missing movie root.");
    Require(result != MM_PORTABLE_SOURCE_READ_FAILED, "Cannot read movie sources.");
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

void MarcusWriteStoryboardBundle(const MarcusMovieFile &movie, const MarcusBoardStore &boards,
    const std::string &resources, const std::string &content, const std::vector<V::Dependency> &dependencies,
    const std::vector<fs::path> &portableContent, const fs::path &destination)
{
    Staging work;
    const auto innerPath = work.Path("movie.3mm"), contentPath = work.Path("backgrounds.3cn"), thumbnailsPath = work.Path("backgrounds.3th");
    fs::copy_file(movie.path, innerPath);
    fs::permissions(innerPath, fs::perms::owner_write, fs::perm_options::add);
    auto inner = Open(innerPath, MM_PRIVATE_ARCHIVE_EDIT), contents = Open(contentPath, MM_PRIVATE_ARCHIVE_CREATE);
    SceneBackgrounds backgrounds;
    const auto entries = marcus_board_store_data(&boards);
    for (MMU32 i = 0; i < boards.count; ++i)
    {
        // Regenerate from editor data, repairing older storyboard palettes too.
        const auto &entry = entries[i];
        const auto resource = MarcusBoardContent(*marcus_stored_board_view(&entry), resources, content);
        auto source = Open(resource.file->path, MM_PRIVATE_ARCHIVE_READ);
        ChunkNumber background;
        Require(mm_private_archive_copy(source.get(), contents.get(), kctgBkgd, resource.background, 0, &background),
                "Cannot package storyboard background.");
        backgrounds.emplace_back(entry.scene, background);
    }
    Require(mm_private_archive_save(contents.get(), kctgSoc), "Cannot finish portable storyboard content.");
    contents.reset();
    std::random_device random;
    RekeyBackgrounds(contentPath, portableContent, random, backgrounds);
    const auto editor = mm_private_archive_editor(inner.get());
    for (const auto &entry : backgrounds)
        Require(mm_movie_archive_retarget_background(&editor, entry.first, MM_PORTABLE_SOURCE_ID, entry.second),
                "Cannot write storyboard background references.");
    PortableSource(editor);
    Require(mm_private_archive_save(inner.get(), kctgSoc), "Cannot finish portable storyboard movie.");
    inner.reset();
    auto thumbnails = Open(thumbnailsPath, MM_PRIVATE_ARCHIVE_CREATE);
    Require(mm_private_archive_save(thumbnails.get(), kctgSoc), "Cannot write storyboard thumbnail archive."); thumbnails.reset();
    // New names avoid the original manager reusing a stale, installed package.
    const auto archivePath = work.Path("backgrounds.vxp");
    const MMU32 nonce = random(), sequence = random();
    char name[MM_VXP_STORYBOARD_NAME_CAPACITY];
    const auto status = mm_vxp_zip_write_storyboard(archivePath.u8string().c_str(), contentPath.u8string().c_str(),
        thumbnailsPath.u8string().c_str(), nonce, sequence, name);
    Require(status == MM_VXP_OK, mm_vxp_error(status));
    V::WriteStoryboardBundle(V::WholeFile(innerPath), dependencies, {name, V::WholeFile(archivePath)}, destination);
}
