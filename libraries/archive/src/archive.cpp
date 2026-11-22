/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "mm/archive.h"
#include "diagnostics.h"
#include <algorithm>
#include <codecvt>
#include <cstdio>
#include <exception>
#include <locale>
#include <stdexcept>
#include "kauai_core.h"
#include "mm/actor_data.h"
#include "mm/movie_data.h"
#include "mm/scene_data.h"
#include "engine_movie_source.h"
#include "mm/native_archive_file.h"
#include "mm/sound_resource.h"
#include "platform_text.h"
#include "resource_archive_host.h"
#undef size
ASSERTNAME

namespace MM
{
struct Archive::Impl
{
    Options options;
    MMResourceCache *live = nullptr;
    MMChunkOwnerRead *liveReader = nullptr;
    MMNativeArchiveSource stored = {};
    bool canPreview = false;
    std::string path;
    std::vector<Chunk> chunks;
    MMArchiveReader *reader = nullptr;
    explicit Impl(Options value) : options(value)
    {
    }
    ~Impl()
    {
        mm_archive_reader_close(reader);
        if (live)
            mm_resource_cache_release(live);
        mm_native_archive_source_clear(&stored);
    }
    bool Check(int status, std::size_t index, Error &error) const;
    bool Load(std::size_t index, MMReadStream &stream, Error &error);
};
namespace
{
constexpr std::uint32_t FourCC(const char (&code)[5])
{
    return (std::uint32_t(static_cast<unsigned char>(code[0])) << 24) |
           (std::uint32_t(static_cast<unsigned char>(code[1])) << 16) |
           (std::uint32_t(static_cast<unsigned char>(code[2])) << 8) |
            std::uint32_t(static_cast<unsigned char>(code[3]));
}

std::string Utf8(const void *value, std::size_t unitsCount)
{
    const auto encoding = sizeof(achar) == 1 ? KauaiText::Encoding::WindowsAnsi : KauaiText::Encoding::Utf16LE;
    const auto bytes = unitsCount * sizeof(achar);
    const auto count = KauaiText::ToUtf16(value, bytes, encoding, nullptr, 0);
    std::vector<uint16_t> units(count);
    if (!count || !KauaiText::ToUtf16(value, bytes, encoding, units.data(), count))
        return {};
    std::u16string text(units.begin(), units.end());
    return std::wstring_convert<std::codecvt_utf8_utf16<char16_t>, char16_t>().to_bytes(text);
}
std::string Utf8(String &value)
{
    return Utf8(value.Psz(), value.Cch());
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
bool Archive::CanPreview() const
{
    return _impl->canPreview;
}
MMResourceCache *Archive::NewResourceCache(std::int32_t budget) const
{
    if (_impl->live)
        return NewTypedSiblingCache(_impl->live, budget);
    if (!_impl->canPreview)
        return nullptr;
    return NewTypedSourceCache(&_impl->stored, budget);
}
MMArchiveReader *Archive::Reader() const
{
    return _impl->reader;
}
MMMovieSessionGraph Archive::PlaybackGraph() const
{
    if (_impl->live)
        return mm_movie_owner_graph(_impl->liveReader);
    return mm_archive_reader_graph(_impl->reader);
}
MMMovieSession *Archive::MovieSession() const
{
    return mm_archive_reader_movie(_impl->reader);
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
    case MM_MOVIE_CHUNK:
        description = "Movie";
        break;
    case MM_SCENE_CHUNK:
        description = "Scene";
        break;
    case MM_ACTOR_CHUNK:
        description = "Actor";
        break;
    case FourCC("ACTN"):
        description = "Action";
        break;
    case FourCC("TBOX"):
        description = "Text Box";
        break;
    case kctgRichText:
        description = "Rich Text";
        break;
    case kctgText:
        description = "Text";
        break;
    case FourCC("BKGD"):
        description = "Background";
        break;
    case FourCC("CAM "):
        description = "Camera";
        break;
    case FourCC("TMPL"):
        description = "Actor Template";
        break;
    case FourCC("BMDL"):
        description = "3D Model";
        break;
    case FourCC("MTRL"):
        description = "Material";
        break;
    case FourCC("CMTL"):
        description = "Custom Material";
        break;
    case kctgMbmp:
        description = "Masked Bitmap";
        break;
    case FourCC("BPMP"):
        description = "BRender Pixel Map";
        break;
    case kctgMeta:
        description = "Windows Metafile";
        break;
    case kctgMacPict:
        description = "Macintosh Picture";
        break;
    case FourCC("PICT"):
        description = "Picture";
        break;
    case kctgMidi:
        description = "MIDI Stream";
        break;
    case kctgWave:
        description = "Wave Audio";
        break;
    case FourCC("SND "):
        description = "Sound";
        break;
    case MM_SOUND_TYPE:
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
    case FourCC("GGAE"):
        description = "Actor Events";
        break;
    case FourCC("GGCL"):
        description = "Animation Cels";
        break;
    case FourCC("GLXF"):
        description = "Transforms";
        break;
    case FourCC("GLLT"):
        description = "Lights";
        break;
    case FourCC("GLMS"):
        description = "Motion-Match Sounds";
        break;
    case FourCC("GGFR"):
        description = "Scene Frame Events";
        break;
    case FourCC("GGST"):
        description = "Scene Start Events";
        break;
    case FourCC("THUM"):
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

struct CatalogHost
{
    std::vector<Chunk> &chunks;
    std::exception_ptr exception;

    static int Visit(void *context, const MMChunkEntry *entry, void *encodedName, MMU32 nameBytes,
                     const MMArchiveCatalogLink *links) noexcept
    {
        auto &host = *static_cast<CatalogHost *>(context);
        try
        {
            Chunk chunk;
            chunk.id = {entry->type, entry->number};
            chunk.storedBytes = entry->bytes;
            chunk.packed = (entry->flags & MM_CHUNK_PACKED) != 0;
            chunk.loner = (entry->flags & MM_CHUNK_LONER) != 0;
            chunk.forest = (entry->flags & MM_CHUNK_FOREST) != 0;
            String name;
            if (nameBytes)
                name.FSetData(encodedName, nameBytes);
            chunk.name = Utf8(name);
            for (LegacyLong j = 0; j < name.Cch(); ++j)
                chunk.nameUnits.push_back(sizeof(achar) == 1 ? static_cast<byte>(name.Psz()[j]) : name.Psz()[j]);
            chunk.children.reserve(entry->child_count);
            for (MMU32 j = 0; j < entry->child_count; ++j)
            {
                const auto &link = links[j];
                chunk.children.push_back({link.child.slot, {link.child.type, link.child.number}, link.target_present != 0});
            }
            host.chunks.push_back(std::move(chunk));
            return 1;
        }
        catch (...)
        {
            host.exception = std::current_exception();
            return 0;
        }
    }
};
} // namespace
std::uint32_t Archive::NameCodeUnitBits() const
{
    return sizeof(achar) * 8;
}
bool Archive::Open(const std::string &utf8Path, Error &error)
{
    return OpenCore(utf8Path, error, OpenMode::live);
}

bool Archive::OpenForInspection(const std::string &utf8Path, Error &error)
{
    return OpenCore(utf8Path, error, OpenMode::inspection);
}

bool Archive::OpenForPreview(const std::string &utf8Path, Error &error)
{
    return OpenCore(utf8Path, error, OpenMode::preview);
}

bool Archive::OpenResources(const std::string &utf8Path, Error &error)
{
    return OpenCore(utf8Path, error, OpenMode::resources);
}

bool Archive::OpenCore(const std::string &utf8Path, Error &error, OpenMode mode)
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
        if (!filename.FBuildFromPath(&path))
            return Fail(error, ErrorCode::open_failed, "Cannot open this file as a chunky archive.");
        MMArchiveReaderOptions readerOptions = {
            opened->options.maxDecodedBytes,
            opened->options.maxStringTableBytes,
            koskCur,
            [](void *, const void *input, size_t bytes, int source, void *output, size_t capacity) -> size_t {
                return CchTranslateRgb(const_cast<void *>(input), bytes, source, static_cast<achar *>(output),
                                       capacity);
            },
            nullptr,
            [](void *) -> int { return !Internal::AssertionFailed(); },
            nullptr,
            KauaiEncodeLegacyChunkName,
        };
        bool useLive = false;
        if (mode != OpenMode::inspection)
            opened->live = OpenSharedTypedCache(&filename, mode == OpenMode::live, useLive, &opened->liveReader);
        int status;
        if (useLive)
        {
            if (!opened->live)
                return Fail(error, ErrorCode::open_failed, "Cannot open this file as a chunky archive.");
            status = mm_archive_reader_open_owner(opened->liveReader, &readerOptions, &opened->reader);
        }
        else
        {
            status = OpenNativeArchiveSource(&filename, &opened->stored);
            if (status == MM_ARCHIVE_READ_OK)
                status = mm_native_archive_source_reader(&opened->stored, &readerOptions, &opened->reader);
        }
        if (status == MM_ARCHIVE_MEMORY)
            throw std::bad_alloc();
        if (status != MM_ARCHIVE_READ_OK)
            return useLive ? Fail(error, ErrorCode::corrupt_archive, "Cannot read the chunk index.") :
                             Fail(error, ErrorCode::open_failed, "Cannot open this file as a chunky archive.");
        opened->canPreview = mode != OpenMode::inspection;
        opened->path = utf8Path;
        CatalogHost host{opened->chunks, {}};
        MMChunkEntry failed;
        const int catalog = mm_archive_reader_catalog(opened->reader, CatalogHost::Visit, &host, &failed);
        if (host.exception)
            std::rethrow_exception(host.exception);
        if (catalog == MM_ARCHIVE_CATALOG_MEMORY)
            throw std::bad_alloc();
        if (catalog == MM_ARCHIVE_CATALOG_CHILD)
        {
            const Id parent{failed.type, failed.number};
            return Fail(error, ErrorCode::corrupt_archive, "Cannot read a child reference.", &parent);
        }
        if (catalog != MM_ARCHIVE_CATALOG_OK)
            return Fail(error, ErrorCode::corrupt_archive, "Cannot read the chunk index.");
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
bool Archive::Impl::Check(int status, std::size_t index, Error &error) const
{
    if (status == MM_ARCHIVE_INVALID_POSITION)
        return Fail(error, ErrorCode::invalid_argument, "Chunk index is out of range.");
    const auto &id = chunks[index].id;
    switch (status)
    {
    case MM_ARCHIVE_READ_OK:
        return true;
    case MM_ARCHIVE_MISSING_PAYLOAD:
        return Fail(error, ErrorCode::io_error, "Cannot read this chunk.", &id);
    case MM_ARCHIVE_BAD_PACKED_HEADER:
        return Fail(error, ErrorCode::decode_failed, "Invalid compressed payload header.", &id);
    case MM_ARCHIVE_PACKED_LIMIT:
        return Fail(error, ErrorCode::resource_limit, "Packed payload exceeds this session's decode budget.", &id);
    case MM_ARCHIVE_DECODE_FAILED:
        return Fail(error, ErrorCode::decode_failed, "Cannot decode this payload.", &id);
    case MM_ARCHIVE_INVALID_RECORD:
        return Fail(error, ErrorCode::corrupt_archive, "Invalid or unsupported record header.", &id);
    case MM_ARCHIVE_WRONG_TYPE:
        return Fail(error, ErrorCode::unsupported_format, "This chunk is not a General String Table.", &id);
    case MM_ARCHIVE_STRING_LIMIT:
        return Fail(error, ErrorCode::resource_limit, "String table exceeds this session's preview budget.", &id);
    case MM_ARCHIVE_INVALID_STRINGS:
        return Fail(error, ErrorCode::corrupt_archive, "Invalid or unsupported General String Table.", &id);
    case MM_ARCHIVE_MEMORY:
        throw std::bad_alloc();
    default:
        return Fail(error, ErrorCode::corrupt_archive, "The legacy reader rejected this payload.", &id);
    }
}

bool Archive::Impl::Load(std::size_t index, MMReadStream &stream, Error &error)
{
    const int status = index < chunks.size() ? mm_archive_reader_payload(reader, static_cast<MMU32>(index), &stream) :
                                              MM_ARCHIVE_INVALID_POSITION;
    return Check(status, index, error);
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
        MMReadStream stream;
        if (!_impl->Load(index, stream, error))
            return false;
        const auto &id = _impl->chunks[index].id;
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
        MMArchiveRecord decoded;
        const int status = index < _impl->chunks.size() ?
                               mm_archive_reader_record(_impl->reader, static_cast<MMU32>(index), &decoded) :
                               MM_ARCHIVE_INVALID_POSITION;
        if (!_impl->Check(status, index, error))
            return false;
        const auto &id = _impl->chunks[index].id;
        if (Internal::AssertionFailed())
            return Fail(error, ErrorCode::corrupt_archive, "Invalid or unsupported record header.", &id);
        RecordHeader result;
        switch (decoded.kind)
        {
        case MM_ARCHIVE_MOVIE:
            result.kind = RecordKind::movie;
            result.movie = {decoded.value.movie.version, decoded.value.movie.backward_version};
            break;
        case MM_ARCHIVE_SCENE:
            result.kind = RecordKind::scene;
            result.scene = {decoded.value.scene.first, decoded.value.scene.last, decoded.value.scene.transition};
            break;
        case MM_ARCHIVE_ACTOR:
            result.kind = RecordKind::actor;
            result.actor = {decoded.value.actor.id, decoded.value.actor.first, decoded.value.actor.last};
            break;
        case MM_ARCHIVE_CAPTION:
        {
            const auto &caption = decoded.value.caption;
            result.kind = RecordKind::caption;
            result.caption = {caption.first, caption.end, caption.story != 0,
                              {caption.left, caption.top, caption.right, caption.bottom},
                              {caption.text.type, caption.text.number}};
            break;
        }
        }
        record = result;
        return true;
    }
    catch (const std::bad_alloc &)
    {
        return Fail(error, ErrorCode::out_of_memory, "Insufficient memory to decode record.");
    }
}
namespace
{
struct DirectoryHost
{
    MovieDirectory directory;
    MovieOpenInfo *open = nullptr;
    const std::vector<Chunk> *chunks = nullptr;
    MMChunkAccess access{};
    std::exception_ptr exception;

    template <class Write> int Append(Write write)
    {
        try
        {
            write();
            return 1;
        }
        catch (...)
        {
            exception = std::current_exception();
            return 0;
        }
    }

    MMArchiveDirectorySink Sink()
    {
        return {
            this,
            [](void *context, MMU32 number) -> int {
                auto &out = static_cast<DirectoryHost *>(context)->directory;
                out.hasRoot = true;
                out.root = {MM_MOVIE_CHUNK, number};
                return 1;
            },
            [](void *context, MMU32 slot, const MMMovieSessionScene *scene) -> int {
                auto &host = *static_cast<DirectoryHost *>(context);
                return host.Append([&] {
                    host.directory.scenes.push_back({{MM_SCENE_CHUNK, scene->number}, slot,
                                                     {scene->first_frame, scene->last_frame, scene->transition}});
                    if (host.open)
                    {
                        MMU32 position;
                        if (mm_chunk_find(&host.access, MM_SCENE_CHUNK, scene->number, &position) != MM_CHUNK_OK)
                            throw std::runtime_error("Movie scene is missing from the index.");
                        host.open->sceneNames.push_back((*host.chunks)[position].name);
                    }
                });
            },
            [](void *context, const MMMovieSourceName *row) -> int {
                auto &host = *static_cast<DirectoryHost *>(context);
                return host.Append([&] {
                    String text;
                    EngineMovieSource::ToString(*row, text);
                    host.directory.sources.push_back({row->id, Utf8(text)});
                });
            },
        };
    }
};
} // namespace

bool Archive::ReadMovieDirectory(MovieDirectory &directory, Error &error)
{
    return ReadMovieMetadata(directory, nullptr, error);
}
bool Archive::ReadMovieOpenInfo(MovieOpenInfo &out, Error &error)
{
    MovieOpenInfo opened;
    if (!ReadMovieMetadata(opened.directory, &opened, error))
        return false;
    out = std::move(opened);
    return true;
}
bool Archive::ReadMovieMetadata(MovieDirectory &directory, MovieOpenInfo *open, Error &error)
{
    error = {};
    Internal::ClearDiagnostic();
    if (!_impl->reader)
        return Fail(error, ErrorCode::invalid_argument, "Open an archive before listing its movie scenes.");
    try
    {
        DirectoryHost host;
        host.open = open;
        host.chunks = &_impl->chunks;
        host.access = mm_archive_reader_access(_impl->reader);
        const auto sink = host.Sink();
        MMArchiveDirectoryError detail;
        MMArchiveMovieIds ids;
        const MMArchiveMovieOpenSink movieSink{sink, [](void *context, MMI32 id, const void *text, MMU32 units) {
            auto &host = *static_cast<DirectoryHost *>(context);
            return host.Append([&] { host.open->actorNames.push_back({id, Utf8(text, units)}); });
        }};
        const int status = open ? mm_archive_reader_movie_open(_impl->reader, &movieSink, &ids, &detail)
                                : mm_archive_reader_directory(_impl->reader, &sink, &detail);
        if (host.exception)
            std::rethrow_exception(host.exception);
        const Id root = {MM_MOVIE_CHUNK, detail.root};
        switch (status)
        {
        case MM_ARCHIVE_DIRECTORY_OK:
            if (open)
            {
                open->nextScene = ids.scene_exhausted ? std::uint64_t{1} << 32 : ids.next_scene;
                open->nextActorChunk = ids.actor_chunk_exhausted ? std::uint64_t{1} << 32 : ids.next_actor_chunk;
                open->nextActorId = ids.next_actor_id;
            }
            directory = std::move(host.directory);
            return true;
        case MM_ARCHIVE_DIRECTORY_ROOT:
            return Fail(error, ErrorCode::corrupt_archive, "Invalid or unsupported record header.", &root);
        case MM_ARCHIVE_DIRECTORY_SCENE:
            return Fail(error, ErrorCode::corrupt_archive, "The movie has an invalid scene link or header.", &root);
        case MM_ARCHIVE_DIRECTORY_SOURCE_LINK:
            return Fail(error, ErrorCode::corrupt_archive, "The movie has an invalid source table link.");
        case MM_ARCHIVE_DIRECTORY_SOURCE_LIMIT:
            return Fail(error, ErrorCode::resource_limit, "Source table exceeds this session's preview budget.",
                        &_impl->chunks[detail.position].id);
        case MM_ARCHIVE_DIRECTORY_SOURCE_TABLE:
            return Fail(error, ErrorCode::corrupt_archive, "Invalid movie source table.",
                        &_impl->chunks[detail.position].id);
        case MM_ARCHIVE_DIRECTORY_SOURCE_ROW:
            return Fail(error, ErrorCode::io_error, "Cannot read a movie source name.", &_impl->chunks[detail.position].id);
        case MM_ARCHIVE_DIRECTORY_PAYLOAD:
            return _impl->Check(detail.payload_status, detail.position, error);
        case MM_ARCHIVE_DIRECTORY_MEMORY:
            throw std::bad_alloc();
        case MM_ARCHIVE_DIRECTORY_INDEX:
            return Fail(error, ErrorCode::corrupt_archive, "Cannot read the movie chunk index.");
        case MM_ARCHIVE_DIRECTORY_NO_SCENES:
            return Fail(error, ErrorCode::corrupt_archive, "This document contains no movie scenes.");
        case MM_ARCHIVE_DIRECTORY_ROLLCALL:
            return Fail(error, ErrorCode::corrupt_archive, "The movie roll-call record is invalid.");
        case MM_ARCHIVE_DIRECTORY_ROLLCALL_LIMIT:
            return Fail(error, ErrorCode::resource_limit, "The movie roll-call table has too many entries.");
        default:
            return Fail(error, ErrorCode::unsupported_format, "Movie source names cannot be decoded.");
        }
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
    try
    {
        const MMMovieStringTable *strings;
        if (!_impl->Check(mm_archive_reader_strings(_impl->reader, static_cast<MMU32>(index), &strings),
                          index, error))
            return false;
        MMMovieStringPage range;
        mm_movie_string_table_page(strings, first, count, &range);
        StringTablePage result;
        result.totalEntries = range.total;
        result.extraBytes = range.extra_bytes;
        result.firstEntry = range.first;
        for (auto i = range.first; i < range.first + range.count; ++i)
        {
            MMMovieStringRow row;
            if (!mm_movie_string_table_row(strings, i, &row))
                return Fail(error, ErrorCode::corrupt_archive, "Invalid General String Table entry.", &id);
            StringTableEntry entry;
            entry.index = i;
            entry.text = Utf8(row.text, row.units);
            entry.extra.assign(row.extra, row.extra + range.extra_bytes);
            result.entries.push_back(std::move(entry));
        }
        if (Internal::AssertionFailed())
            return Fail(error, ErrorCode::corrupt_archive, "Invalid General String Table entry.", &id);
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
