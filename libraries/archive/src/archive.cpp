/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "mm/archive.h"
#include "mm/native_sound.h"
#include "diagnostics.h"
#include <algorithm>
#include <codecvt>
#include <cstdio>
#include <exception>
#include <locale>
#include <stdexcept>
#include "soc_core.h"
#include "engine_movie_graph.h"
#include "engine_movie_source.h"
#include "mm/archive_reader.h"
#include "mm/native_files.h"
#include "mm/sound_resource.h"
#include "platform_text.h"
#undef size
ASSERTNAME

namespace MM
{
struct Archive::Impl
{
    Options options;
    PChunkyFile file = pvNil;
    PFileObject previewFile = pvNil;
    void *streamFile = nullptr;
    std::string path;
    std::vector<Chunk> chunks;
    MMArchiveReader *reader = nullptr;
    explicit Impl(Options value) : options(value)
    {
    }
    ~Impl()
    {
        mm_archive_reader_close(reader);
        ReleasePpo(&file);
        ReleasePpo(&previewFile);
        mm_native_file_close(streamFile);
    }
    bool Check(int status, std::size_t index, Error &error) const;
    bool Load(std::size_t index, MMReadStream &stream, Error &error);
};
namespace
{
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
void *Archive::NativeFile() const
{
    return _impl->file;
}
MMArchiveReader *Archive::Reader() const
{
    return _impl->reader;
}
MMMovieSessionGraph Archive::PlaybackGraph() const
{
    if (mm_archive_reader_wire_index(_impl->reader))
        return mm_archive_reader_graph(_impl->reader);
    return EngineMovieGraph::FromFile(_impl->file);
}
bool ReadSoundCatalogStyle(const Archive &archive, std::uint32_t number, std::int32_t &style)
{
    const auto reader = archive.Reader();
    if (!reader)
        return false;
    const auto graph = mm_archive_reader_graph(reader);
    MMI32 value;
    if (!mm_sound_catalog_style(&graph, number, 0, &value))
        return false;
    style = value;
    return true;
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

bool Archive::PrepareNativeFile(Error &error)
{
    if (_impl->file || !_impl->previewFile)
        return true;
    Internal::ClearDiagnostic();
    _impl->file = ChunkyFile::PcflOpenIndex(_impl->previewFile, mm_archive_reader_wire_index(_impl->reader));
    if (!_impl->file || Internal::AssertionFailed())
    {
        ReleasePpo(&_impl->file);
        return Fail(error, ErrorCode::open_failed, "Cannot prepare the archive for preview.");
    }
    return true;
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
        const MMArchiveReaderOptions readerOptions = {
            opened->options.maxDecodedBytes,
            opened->options.maxStringTableBytes,
            koskCur,
            [](void *, const void *input, size_t bytes, int source, void *output, size_t capacity) -> size_t {
                return CchTranslateRgb(const_cast<void *>(input), bytes, source, static_cast<achar *>(output),
                                       capacity);
            },
            nullptr,
            [](void *) -> int { return !Internal::AssertionFailed(); },
            EngineMovieGraph::Name,
            [](void *, const char *legacy, void *out, MMU32 capacity, MMU32 *written) -> int {
                String name;
                name.SetSzs(const_cast<char *>(legacy));
                const auto bytes = static_cast<MMU32>(name.CbData());
                if (bytes > capacity)
                    return false;
                name.GetData(out);
                *written = bytes;
                return true;
            },
        };
        const bool useKauaiFile = mode == OpenMode::preview || mode == OpenMode::resources;
        const auto existingFile = useKauaiFile ? FileObject::PfilFromFni(&filename) : nullptr;
        const bool nativeGraph = mode == OpenMode::live ||
            (existingFile && (mode == OpenMode::preview || (existingFile->GrffilCur() & ffilWriteEnable) ||
                              ChunkyFile::PcflFromFni(&filename)));
        int status;
        if (nativeGraph)
        {
            opened->file = ChunkyFile::PcflOpen(&filename, fcflNil);
            if (!opened->file)
                return Fail(error, ErrorCode::open_failed, "Cannot open this file as a chunky archive.");
            const auto access = opened->file->ReadChunkAccess();
            status = mm_archive_reader_open(&access, opened->file, KauaiChunkPayloadServices(), &readerOptions,
                                            &opened->reader);
        }
        else
        {
            MMReadStream stream = {};
            if (useKauaiFile)
            {
                opened->previewFile = FileObject::PfilOpen(&filename);
                if (!opened->previewFile)
                    return Fail(error, ErrorCode::open_failed, "Cannot open this file as a chunky archive.");
                stream = opened->previewFile->ReadStream();
            }
            else
            {
                filename.GetStnPath(&path);
                MMNativeOpenOptions options;
                mm_native_open_options_init(&options);
                opened->streamFile =
                    mm_native_file_open(reinterpret_cast<const MMNativePathChar *>(path.Psz()), &options);
                if (!opened->streamFile || !mm_native_file_read_stream(opened->streamFile, &stream))
                    return Fail(error, ErrorCode::open_failed, "Cannot open this file as a chunky archive.");
            }
            const unsigned short order = 1;
            MMChunkPolicy policy = {};
#ifdef CHUNK_BIG_INDEX
            policy.expanded_runtime = 1;
#endif
            policy.native_big_endian = *reinterpret_cast<const unsigned char *>(&order) == 0;
            policy.max_name_bytes = kcbMaxDataStn;
            status = mm_archive_reader_open_stream(&stream, &policy, &readerOptions, &opened->reader);
        }
        if (status == MM_ARCHIVE_MEMORY)
            throw std::bad_alloc();
        if (status != MM_ARCHIVE_READ_OK)
            return nativeGraph ? Fail(error, ErrorCode::corrupt_archive, "Cannot read the chunk index.") :
                                 Fail(error, ErrorCode::open_failed, "Cannot open this file as a chunky archive.");
        opened->path = utf8Path;
        const auto access = mm_archive_reader_access(opened->reader);
        for (MMU32 i = 0; i < access.count; ++i)
        {
            MMChunkEntry entry;
            if (mm_chunk_at(&access, i, &entry) != MM_CHUNK_OK)
                return Fail(error, ErrorCode::corrupt_archive, "Cannot read the chunk index.");
            Chunk chunk;
            chunk.id = {entry.type, entry.number};
            chunk.storedBytes = entry.bytes;
            chunk.packed = (entry.flags & MM_CHUNK_PACKED) != 0;
            chunk.loner = (entry.flags & MM_CHUNK_LONER) != 0;
            chunk.forest = (entry.flags & MM_CHUNK_FOREST) != 0;
            String name;
            const auto decodeName = [](void *out, const MMReadStream *stream) -> int {
                unsigned char bytes[kcbMaxDataStn];
                return stream->length <= sizeof(bytes) && mm_read_exact(stream, 0, bytes, stream->length) &&
                       static_cast<PString>(out)->FSetData(bytes, stream->length);
            };
            mm_archive_reader_name(opened->reader, i, decodeName, &name);
            chunk.name = Utf8(name);
            for (LegacyLong j = 0; j < name.Cch(); ++j)
                chunk.nameUnits.push_back(sizeof(achar) == 1 ? static_cast<byte>(name.Psz()[j]) : name.Psz()[j]);
            for (MMU32 j = 0; j < entry.child_count; ++j)
            {
                MMChunkChild child;
                if (mm_chunk_child_at(&access, i, j, &child) != MM_CHUNK_OK)
                    return Fail(error, ErrorCode::corrupt_archive, "Cannot read a child reference.", &chunk.id);
                MMU32 position;
                chunk.children.push_back({child.slot, {child.type, child.number},
                                          mm_chunk_find(&access, child.type, child.number, &position) == MM_CHUNK_OK});
            }
            std::sort(chunk.children.begin(), chunk.children.end(), [](const Child &a, const Child &b) {
                if (a.slot != b.slot)
                    return a.slot < b.slot;
                return a.target.type < b.target.type ||
                       (a.target.type == b.target.type && a.target.number < b.target.number);
            });
            opened->chunks.push_back(std::move(chunk));
        }
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
                out.root = {kctgMvie, number};
                return 1;
            },
            [](void *context, MMU32 slot, const MMMovieSessionScene *scene) -> int {
                auto &host = *static_cast<DirectoryHost *>(context);
                return host.Append([&] {
                    host.directory.scenes.push_back({{kctgScen, scene->number}, slot,
                                                     {scene->first_frame, scene->last_frame, scene->transition}});
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
    error = {};
    Internal::ClearDiagnostic();
    if (!_impl->reader)
        return Fail(error, ErrorCode::invalid_argument, "Open an archive before listing its movie scenes.");
    try
    {
        DirectoryHost host;
        const auto sink = host.Sink();
        MMArchiveDirectoryError detail;
        const int status = mm_archive_reader_directory(_impl->reader, &sink, &detail);
        if (host.exception)
            std::rethrow_exception(host.exception);
        const Id root = {kctgMvie, detail.root};
        switch (status)
        {
        case MM_ARCHIVE_DIRECTORY_OK:
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
