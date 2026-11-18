/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "mm/archive.h"
#include "diagnostics.h"
#include <algorithm>
#include <codecvt>
#include <cstdio>
#include <locale>
#include <stdexcept>
#include "soc_core.h"
#include "engine_movie_graph.h"
#include "engine_movie_source.h"
#include "mm/actor_data.h"
#include "platform_text.h"
#undef size
ASSERTNAME

namespace MM
{
struct Archive::Impl
{
    Options options;
    PChunkyFile file = pvNil;
    MMMovieSession *movie = nullptr;
    std::string path;
    std::vector<Chunk> chunks;
    MMChunkPayloadCache payload = {};
    PStringTable_GST strings = pvNil;
    bool stringsAttempted = false;
    Error stringsError;
    explicit Impl(Options value) : options(value)
    {
        AssertDo(mm_chunk_payload_cache_init(&payload, KauaiChunkPayloadServices()) == MM_CHUNK_PAYLOAD_OK,
                 "invalid archive payload services");
    }
    ~Impl()
    {
        mm_movie_session_close(movie);
        ReleasePpo(&strings);
        ReleasePpo(&file);
        mm_chunk_payload_cache_clear(&payload);
    }
    bool Load(std::size_t index, Error &error);
    bool LoadSources(const MMMovieSession *, std::vector<MovieSourceEntry> &sources, Error &error);
};
namespace
{
std::string Utf8(String &value)
{
    const auto count = KauaiText::ToUtf16(
        value.Psz(), value.Cch() * sizeof(achar),
        sizeof(achar) == 1 ? KauaiText::Encoding::WindowsAnsi : KauaiText::Encoding::Utf16LE, nullptr, 0);
    std::vector<uint16_t> units(count);
    if (!count ||
        !KauaiText::ToUtf16(value.Psz(), value.Cch() * sizeof(achar),
                            sizeof(achar) == 1 ? KauaiText::Encoding::WindowsAnsi : KauaiText::Encoding::Utf16LE,
                            units.data(), count))
        return {};
    std::u16string text(units.begin(), units.end());
    return std::wstring_convert<std::codecvt_utf8_utf16<char16_t>, char16_t>().to_bytes(text);
}
bool ValidStringTable(const MMReadStream &stream, std::uint32_t limit)
{
    const int result = mm_movie_validate_strings(&stream, limit);
    if (result == MM_MOVIE_MEMORY)
        throw std::bad_alloc();
    return result == MM_MOVIE_OK;
}

bool NativePath(const std::string &input, String &path, std::string &error)
{
    if (input.empty() || input.find('\0') != std::string::npos)
    {
        error = "Enter a file path.";
        return false;
    }
    std::string native = input;
#ifdef _WIN32
    const auto units = std::wstring_convert<std::codecvt_utf8_utf16<char16_t>, char16_t>().from_bytes(input);
    const auto count = KauaiText::ToAnsi(units.data(), units.size() * 2, KauaiText::Encoding::Utf16LE, nullptr, 0);
    native.resize(count);
    if (!count || !KauaiText::ToAnsi(units.data(), units.size() * 2, KauaiText::Encoding::Utf16LE, &native[0], count))
    {
        error = "Cannot convert this path to the legacy Windows file encoding.";
        return false;
    }
    const auto backCount =
        KauaiText::ToUtf16(native.data(), native.size(), KauaiText::Encoding::WindowsAnsi, nullptr, 0);
    std::vector<uint16_t> back(backCount);
    KauaiText::ToUtf16(native.data(), native.size(), KauaiText::Encoding::WindowsAnsi, back.data(), backCount);
    if (back.size() != units.size() || !std::equal(back.begin(), back.end(), units.begin()))
    {
        error = "This path is not representable in the legacy Windows file encoding. Move the file to a simpler path.";
        return false;
    }
#endif
    if (native.size() > static_cast<std::size_t>(kcchMaxStn))
    {
        error = "The core file API supports paths up to 255 bytes.";
        return false;
    }
    path.SetSzs(const_cast<char *>(native.c_str()));
    return true;
}
} // namespace
Archive::Archive(Options options) : _impl(new Impl(options))
{
}
Archive::~Archive() = default;
void *Archive::NativeFile() const
{
    return _impl->file;
}
MMMovieSession *Archive::MovieSession() const
{
    return _impl->movie;
}
void Archive::Close()
{
    _impl.reset(new Impl(_impl->options));
}
const std::vector<Chunk> &Archive::Chunks() const
{
    return _impl->chunks;
}
const std::string &Archive::Path() const
{
    return _impl->path;
}
std::string TypeLabel(std::uint32_t type)
{
    std::string result;
    for (int shift = 24; shift >= 0; shift -= 8)
    {
        const auto ch = (type >> shift) & 255;
        result += ch >= 32 && ch < 127 ? static_cast<char>(ch) : '.';
    }
    return result;
}
std::string TypeDisplayName(std::uint32_t type)
{
    // Descriptions come from the framework and engine's chunk definitions.
    // Keep the FourCC alongside the name, and preserve unknown codes verbatim.
    const char *description = nullptr;
    switch (type)
    {
    case kctgGst:
        description = "General String Table";
        break;
    case kctgAst:
        description = "Allocated String Table";
        break;
    case kctgGl:
        description = "General List";
        break;
    case kctgAl:
        description = "Allocated List";
        break;
    case kctgGg:
        description = "General Group";
        break;
    case kctgAg:
        description = "Allocated Group";
        break;
    case kctgMvie:
        description = "Movie";
        break;
    case kctgScen:
        description = "Scene";
        break;
    case kctgActr:
        description = "Actor";
        break;
    case kctgActn:
        description = "Action";
        break;
    case kctgTbox:
        description = "Text Box";
        break;
    case kctgRichText:
        description = "Rich Text";
        break;
    case kctgText:
        description = "Text";
        break;
    case kctgBkgd:
        description = "Background";
        break;
    case kctgCam:
        description = "Camera";
        break;
    case kctgTmpl:
        description = "Actor Template";
        break;
    case kctgBmdl:
        description = "3D Model";
        break;
    case kctgMtrl:
        description = "Material";
        break;
    case kctgCmtl:
        description = "Custom Material";
        break;
    case kctgMbmp:
        description = "Masked Bitmap";
        break;
    case kctgBpmp:
        description = "BRender Pixel Map";
        break;
    case kctgMeta:
        description = "Windows Metafile";
        break;
    case kctgMacPict:
        description = "Macintosh Picture";
        break;
    case kctgPict:
        description = "Picture";
        break;
    case kctgMidi:
        description = "MIDI Stream";
        break;
    case kctgWave:
        description = "Wave Audio";
        break;
    case kctgSnd:
        description = "Sound";
        break;
    case kctgMsnd:
        description = "Movie Sound";
        break;
    case kctgScript:
        description = "Script";
        break;
    case kctgScriptStrs:
        description = "Script String Table";
        break;
    case kctgColorTable:
        description = "Color Table";
        break;
    case kctgGgae:
        description = "Actor Events";
        break;
    case kctgGgcl:
        description = "Animation Cels";
        break;
    case kctgGlxf:
        description = "Transforms";
        break;
    case kctgGllt:
        description = "Lights";
        break;
    case kctgGlms:
        description = "Motion-Match Sounds";
        break;
    case kctgFrmGg:
        description = "Scene Frame Events";
        break;
    case kctgStartGg:
        description = "Scene Start Events";
        break;
    case kctgThumbMbmp:
        description = "Thumbnail";
        break;
    default:
        break;
    }
    auto code = TypeLabel(type);
    if (!description)
        return code;
    while (!code.empty() && code.back() == ' ')
        code.pop_back();
    return std::string(description) + " (" + code + ")";
}
std::string Identity(const Id &id)
{
    char buffer[64];
    std::snprintf(buffer, sizeof(buffer), "%s / %08x", TypeLabel(id.type).c_str(), static_cast<unsigned>(id.number));
    return buffer;
}
namespace
{
bool Fail(Error &error, ErrorCode code, const char *message, const Id *id = nullptr)
{
    error.code = code;
    error.message = message;
    error.hasChunk = id != nullptr;
    if (id)
        error.chunk = *id;
    return false;
}
} // namespace
std::uint32_t Archive::NameCodeUnitBits() const
{
    return sizeof(achar) * 8;
}
bool Archive::Open(const std::string &utf8Path, Error &error)
{
    error = {};
    Internal::ClearDiagnostic();
    try
    {
        std::unique_ptr<Impl> opened(new Impl(_impl->options));
        String path;
        Filename filename;
        std::string message;
        if (!NativePath(utf8Path, path, message))
            return Fail(error, ErrorCode::invalid_path, message.c_str());
        if (!filename.FBuildFromPath(&path) || !(opened->file = ChunkyFile::PcflOpen(&filename, fcflNil)))
            return Fail(error, ErrorCode::open_failed, "Cannot open this file as a chunky archive.");
        opened->path = utf8Path;
        for (LegacyLong i = 0; i < opened->file->Ccki(); ++i)
        {
            ChunkIdentification id;
            MMBlockFile extent;
            int packed;
            if (!opened->file->FGetCki(i, &id) ||
                !opened->file->FFindBlockFile(id.ctg, id.cno, &extent, &packed))
                return Fail(error, ErrorCode::corrupt_archive, "Cannot read the chunk index.");
            Chunk chunk;
            chunk.id = {id.ctg, id.cno};
            chunk.storedBytes = extent.length;
            chunk.packed = packed != 0;
            chunk.loner = opened->file->FLoner(id.ctg, id.cno);
            chunk.forest = opened->file->FForest(id.ctg, id.cno);
            String name;
            opened->file->FGetName(id.ctg, id.cno, &name);
            chunk.name = Utf8(name);
            for (LegacyLong j = 0; j < name.Cch(); ++j)
                chunk.nameUnits.push_back(sizeof(achar) == 1 ? static_cast<byte>(name.Psz()[j]) : name.Psz()[j]);
            for (LegacyLong j = 0; j < opened->file->Ckid(id.ctg, id.cno); ++j)
            {
                ChildChunkIdentification child;
                if (!opened->file->FGetKid(id.ctg, id.cno, j, &child))
                    return Fail(error, ErrorCode::corrupt_archive, "Cannot read a child reference.", &chunk.id);
                chunk.children.push_back(
                    {child.chid, {child.cki.ctg, child.cki.cno}, opened->file->FFind(child.cki.ctg, child.cki.cno)});
            }
            std::sort(chunk.children.begin(), chunk.children.end(), [](const Child &a, const Child &b) {
                if (a.slot != b.slot)
                    return a.slot < b.slot;
                return a.target.type < b.target.type ||
                       (a.target.type == b.target.type && a.target.number < b.target.number);
            });
            opened->chunks.push_back(std::move(chunk));
        }
        std::sort(opened->chunks.begin(), opened->chunks.end(), [](const Chunk &a, const Chunk &b) {
            return a.id.type < b.id.type || (a.id.type == b.id.type && a.id.number < b.id.number);
        });
        if (Internal::AssertionFailed())
            return Fail(error, ErrorCode::corrupt_archive, "The legacy reader rejected the archive index.");
        _impl.swap(opened);
        return true;
    }
    catch (const std::bad_alloc &)
    {
        return Fail(error, ErrorCode::out_of_memory, "Insufficient memory to open archive.");
    }
    catch (const std::exception &)
    {
        return Fail(error, ErrorCode::unsupported_format, "Archive text cannot be decoded.");
    }
}
bool Archive::Impl::Load(std::size_t index, Error &error)
{
    if (index >= chunks.size())
        return Fail(error, ErrorCode::invalid_argument, "Chunk index is out of range.");
    const auto &chunk = chunks[index];
    MMChunkEntry entry = {};
    entry.type = chunk.id.type;
    entry.number = chunk.id.number;
    entry.flags = chunk.packed ? MM_CHUNK_PACKED : 0;
    const MMChunkPayloadCacheCallbacks callbacks = {
        this,
        [](void *context) {
            auto archive = static_cast<Impl *>(context);
            ReleasePpo(&archive->strings);
            archive->stringsAttempted = false;
            archive->stringsError = {};
        },
        [](void *) -> int { return !Internal::AssertionFailed(); },
    };
    switch (mm_chunk_payload_cache_load(&payload, file, index, &entry, options.maxDecodedBytes, &callbacks))
    {
    case MM_CHUNK_PAYLOAD_OK:
        return true;
    case MM_CHUNK_PAYLOAD_MISSING:
        return Fail(error, ErrorCode::io_error, "Cannot read this chunk.", &chunk.id);
    case MM_CHUNK_PAYLOAD_BAD_HEADER:
        return Fail(error, ErrorCode::decode_failed, "Invalid compressed payload header.", &chunk.id);
    case MM_CHUNK_PAYLOAD_LIMIT:
        return Fail(error, ErrorCode::resource_limit, "Packed payload exceeds this session's decode budget.", &chunk.id);
    case MM_CHUNK_PAYLOAD_DECODE_FAILED:
        return Fail(error, ErrorCode::decode_failed, "Cannot decode this payload.", &chunk.id);
    default:
        return Fail(error, ErrorCode::corrupt_archive, "The legacy reader rejected this payload.", &chunk.id);
    }
}
bool Archive::ReadPayload(std::size_t index, std::uint32_t offset, std::uint32_t count, PayloadPage &page, Error &error)
{
    page = {};
    error = {};
    Internal::ClearDiagnostic();
    if (count > 1024 * 1024)
        return Fail(error, ErrorCode::invalid_argument, "Payload pages are limited to 1 MiB.");
    try
    {
        if (!_impl->Load(index, error))
            return false;
        MMBlockIO io;
        MMReadStream stream;
        const auto &id = _impl->chunks[index].id;
        if (!mm_chunk_payload_cache_stream(&_impl->payload, &io, &stream))
            return Fail(error, ErrorCode::corrupt_archive, "The legacy reader rejected this payload.", &id);
        PayloadPage result;
        result.totalBytes = stream.length;
        result.offset = std::min(offset, result.totalBytes);
        result.bytes.resize(std::min(count, result.totalBytes - result.offset));
        if (!mm_read_exact(&stream, result.offset, result.bytes.data(), result.bytes.size()))
            return Fail(error, ErrorCode::io_error, "Cannot read payload bytes.", &id);
        if (Internal::AssertionFailed())
            return Fail(error, ErrorCode::corrupt_archive, "The legacy reader rejected payload bytes.", &id);
        page = std::move(result);
        return true;
    }
    catch (const std::bad_alloc &)
    {
        return Fail(error, ErrorCode::out_of_memory, "Insufficient memory to read payload.");
    }
}
bool Archive::ReadRecord(std::size_t index, RecordHeader &record, Error &error)
{
    record = {};
    error = {};
    Internal::ClearDiagnostic();
    try
    {
        if (!_impl->Load(index, error))
            return false;
        const auto &id = _impl->chunks[index].id;
        RecordHeader result;
        bool valid = true;
        if (id.type == kctgMvie)
        {
            result.kind = RecordKind::movie;
            MMMovieHeader prefix;
            ChunkNumber number = id.number;
            valid = MovieData::FVerifyVersion(_impl->file, &number, &prefix);
            if (valid)
            {
                result.movie = {static_cast<short>(prefix.current_version), static_cast<short>(prefix.backward_version)};
            }
        }
        else if (id.type == kctgScen)
        {
            result.kind = RecordKind::scene;
            SceneData scene;
            valid = scene.FRead(_impl->file, id.number);
            if (valid)
                result.scene = {scene.firstFrame, scene.lastFrame, static_cast<int32_t>(scene.transition)};
        }
        else if (id.type == kctgActr)
        {
            result.kind = RecordKind::actor;
            ActorChunkOnFile actor;
            MMBlockIO io;
            MMReadStream stream;
            valid = mm_chunk_payload_cache_stream(&_impl->payload, &io, &stream) &&
                    mm_actor_read_header(&stream, &actor);
            if (valid)
                result.actor = {actor.arid, actor.nfrmFirst, actor.nfrmLast};
        }
        else if (id.type == kctgTbox)
        {
            result.kind = RecordKind::caption;
            TextBoxData text;
            ChunkIdentification rich;
            valid = text.FRead(_impl->file, id.number, &rich);
            if (valid)
            {
                result.caption.firstFrame = text.state.first;
                result.caption.maxFrame = text.state.end;
                result.caption.story = text.state.story;
                result.caption.bounds = {text.state.left, text.state.top, text.state.right,
                                         text.state.bottom};
                result.caption.text = {rich.ctg, rich.cno};
            }
        }
        if (!valid || Internal::AssertionFailed())
            return Fail(error, ErrorCode::corrupt_archive, "Invalid or unsupported record header.", &id);
        record = result;
        return true;
    }
    catch (const std::bad_alloc &)
    {
        return Fail(error, ErrorCode::out_of_memory, "Insufficient memory to decode record.");
    }
}
bool Archive::Impl::LoadSources(const MMMovieSession *session, std::vector<MovieSourceEntry> &sources, Error &error)
{
    MMChunkEntry entry;
    MMU32 position;
    const int status = mm_movie_session_source_table(session, &entry, &position);
    if (status == MM_MOVIE_ASSET_NOT_FOUND)
    {
        sources.push_back({MM_MOVIE_RETAIL_SOURCE_ID, MM_MOVIE_RETAIL_SOURCE_NAME});
        return true;
    }
    if (status != MM_MOVIE_OK)
        return Fail(error, ErrorCode::corrupt_archive, "The movie has an invalid source table link.");
    const Id id = {entry.type, entry.number};
    if (position >= chunks.size() || !(chunks[position].id == id))
        return Fail(error, ErrorCode::corrupt_archive, "Cannot locate the movie source table.", &id);
    if (!Load(position, error))
        return false;
    MMBlockIO io;
    MMReadStream stream;
    if (!mm_chunk_payload_cache_stream(&payload, &io, &stream))
        return Fail(error, ErrorCode::corrupt_archive, "Invalid movie source table.", &id);
    MMMovieSourceTable table;
    if (stream.length > options.maxStringTableBytes)
        return Fail(error, ErrorCode::resource_limit, "Source table exceeds this session's preview budget.", &id);
    const int opened = mm_movie_source_table_open(&stream, options.maxStringTableBytes, &table);
    if (opened == MM_MOVIE_MEMORY)
        throw std::bad_alloc();
    if (opened != MM_MOVIE_OK)
        return Fail(error, ErrorCode::corrupt_archive, "Invalid movie source table.", &id);
    for (MMU32 i = 0; i < table.count; ++i)
    {
        MMMovieSourceName row;
        if (mm_movie_source_table_at(&table, i, &row) != MM_MOVIE_OK)
            return Fail(error, ErrorCode::io_error, "Cannot read a movie source name.", &id);
        String text;
        EngineMovieSource::ToString(row, text);
        sources.push_back({row.id, Utf8(text)});
    }
    return true;
}
bool Archive::ReadMovieDirectory(MovieDirectory &directory, Error &error)
{
    error = {};
    Internal::ClearDiagnostic();
    if (!_impl->file)
        return Fail(error, ErrorCode::invalid_argument, "Open an archive before listing its movie scenes.");
    try
    {
        const MMMovieSessionGraph graph = {
            _impl.get(),
            [](void *context, MMChunkAccess *access) {
                *access = static_cast<Impl *>(context)->file->ReadChunkAccess();
            },
            [](void *context, void *request, MMU32 position, const MMChunkEntry *entry, void *bytes, MMU32 size) -> int {
                auto &archive = *static_cast<Impl *>(context);
                const Id id = {entry->type, entry->number};
                if (position >= archive.chunks.size() || !(archive.chunks[position].id == id))
                    return MM_MOVIE_BAD_FILE;
                try
                {
                    Error scratch;
                    auto &error = request ? *static_cast<Error *>(request) : scratch;
                    if (!archive.Load(position, error))
                        return MM_MOVIE_IO;
                    MMBlockIO io;
                    MMReadStream stream;
                    if (!mm_chunk_payload_cache_stream(&archive.payload, &io, &stream))
                        return MM_MOVIE_BAD_FILE;
                    return mm_movie_read_fixed(&stream, bytes, size);
                }
                catch (const std::bad_alloc &)
                {
                    return MM_MOVIE_MEMORY;
                }
                catch (const std::exception &)
                {
                    return MM_MOVIE_BAD_FILE;
                }
            },
            [](void *context, const MMChunkEntry *entry, MMMovieReceiveBytes receive, void *out) -> int {
                auto &archive = *static_cast<Impl *>(context);
                const auto access = archive.file->ReadChunkAccess();
                MMU32 position;
                if (mm_chunk_find(&access, entry->type, entry->number, &position) != MM_CHUNK_OK ||
                    position >= archive.chunks.size() || !(archive.chunks[position].id == Id{entry->type, entry->number}))
                    return MM_MOVIE_BAD_FILE;
                try
                {
                    Error error;
                    if (!archive.Load(position, error))
                        return error.code == ErrorCode::out_of_memory ? MM_MOVIE_MEMORY : MM_MOVIE_BAD_FILE;
                    MMBlockIO io;
                    MMReadStream stream;
                    return mm_chunk_payload_cache_stream(&archive.payload, &io, &stream) ?
                               receive(out, &stream) : MM_MOVIE_BAD_FILE;
                }
                catch (const std::bad_alloc &)
                {
                    return MM_MOVIE_MEMORY;
                }
                catch (const std::exception &)
                {
                    return MM_MOVIE_BAD_FILE;
                }
            },
            [](void *context, const MMChunkEntry *entry, MMMovieReceiveBytes receive, void *out) -> int {
                return EngineMovieGraph::Name(static_cast<Impl *>(context)->file, entry, receive, out);
            }};
        MovieDirectory result;
        std::unique_ptr<MMMovieSession, decltype(&mm_movie_session_close)> opened(nullptr, mm_movie_session_close);
        auto session = _impl->movie;
        if (!session)
        {
            MMU32 root = cnoNil;
            const int status = mm_movie_session_open_graph(&graph, &error, &root, &session);
            if (status == MM_MOVIE_ROOT_NOT_FOUND)
            {
                directory = {};
                return true;
            }
            if (status == MM_MOVIE_MEMORY)
                throw std::bad_alloc();
            const Id id = {kctgMvie, root};
            if (status != MM_MOVIE_OK)
                return error ? false : Fail(error, ErrorCode::corrupt_archive, "Invalid or unsupported record header.", &id);
            opened.reset(session);
        }
        const Id id = {kctgMvie, mm_movie_session_root(session)->number};
        result.hasRoot = true;
        result.root = id;
        MMU32 cursor = 0, slot;
        MMMovieSessionScene scene;
        int next;
        while ((next = mm_movie_session_next_scene(session, &error, &cursor, &scene, &slot)) == MM_MOVIE_OK)
            result.scenes.push_back({{kctgScen, scene.number}, slot, {scene.first_frame, scene.last_frame, scene.transition}});
        if (next == MM_MOVIE_MEMORY)
            throw std::bad_alloc();
        if (next != MM_MOVIE_SCENE_NOT_FOUND)
            return error ? false : Fail(error, ErrorCode::corrupt_archive, "The movie has an invalid scene link or header.", &id);
        if (!_impl->LoadSources(session, result.sources, error))
            return false;
        directory = std::move(result);
        if (opened)
            _impl->movie = opened.release();
        return true;
    }
    catch (const std::bad_alloc &)
    {
        return Fail(error, ErrorCode::out_of_memory, "Insufficient memory to read the movie directory.");
    }
    catch (const std::exception &)
    {
        return Fail(error, ErrorCode::unsupported_format, "Movie source names cannot be decoded.");
    }
}
bool Archive::ReadStrings(std::size_t index, std::uint32_t first, std::uint32_t count, StringTablePage &page,
                          Error &error)
{
    page = {};
    error = {};
    Internal::ClearDiagnostic();
    if (count > 128)
        return Fail(error, ErrorCode::invalid_argument, "String pages are limited to 128 entries.");
    if (index >= _impl->chunks.size())
        return Fail(error, ErrorCode::invalid_argument, "Chunk index is out of range.");
    const auto &id = _impl->chunks[index].id;
    if (id.type != kctgGst)
        return Fail(error, ErrorCode::unsupported_format, "This chunk is not a General String Table.", &id);
    try
    {
        if (!_impl->Load(index, error))
            return false;
        if (!_impl->stringsAttempted)
        {
            MMBlockIO io;
            MMReadStream stream;
            if (!mm_chunk_payload_cache_stream(&_impl->payload, &io, &stream))
                return Fail(error, ErrorCode::corrupt_archive, "Invalid or unsupported General String Table.", &id);
            if (stream.length > _impl->options.maxStringTableBytes)
                Fail(_impl->stringsError, ErrorCode::resource_limit,
                     "String table exceeds this session's preview budget.", &id);
            else
            {
                DataBlock view(_impl->payload.block);
                if (!ValidStringTable(stream, _impl->options.maxStringTableBytes) ||
                    !(_impl->strings = StringTable_GST::PgstRead(&view)) || Internal::AssertionFailed())
                    Fail(_impl->stringsError, ErrorCode::corrupt_archive, "Invalid or unsupported General String Table.",
                         &id);
            }
            _impl->stringsAttempted = true;
        }
        if (_impl->stringsError)
        {
            error = _impl->stringsError;
            return false;
        }
        StringTablePage result;
        result.totalEntries = _impl->strings->IstnMac();
        result.extraBytes = _impl->strings->CbExtra();
        result.firstEntry = std::min(first, result.totalEntries);
        const auto end = std::min(result.totalEntries, result.firstEntry + count);
        for (auto i = result.firstEntry; i < end; ++i)
        {
            String text;
            _impl->strings->GetStn(i, &text);
            StringTableEntry entry;
            entry.index = i;
            entry.text = Utf8(text);
            entry.extra.resize(result.extraBytes);
            if (result.extraBytes)
                _impl->strings->GetExtra(i, entry.extra.data());
            result.entries.push_back(std::move(entry));
        }
        if (Internal::AssertionFailed())
            return Fail(error, ErrorCode::corrupt_archive, "The legacy reader rejected a string entry.", &id);
        page = std::move(result);
        return true;
    }
    catch (const std::bad_alloc &)
    {
        return Fail(error, ErrorCode::out_of_memory, "Insufficient memory to read string table.");
    }
    catch (const std::exception &)
    {
        return Fail(error, ErrorCode::unsupported_format, "String table text cannot be decoded.", &id);
    }
}
} // namespace MM
