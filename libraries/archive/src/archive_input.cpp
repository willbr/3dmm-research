#include "archive_input.h"
#include "diagnostics.h"
#include <cstdio>
#include <stdexcept>
#include <utility>
#include "kauai_core.h"
#include "mm/native_text.h"
#include "private_archive_host.h"
#include "resource_archive_host.h"
#undef size
ASSERTNAME

namespace MM {
namespace Internal {
namespace {
bool Fail(Error &error, ErrorCode code, const char *message)
{
    error.code = code;
    error.message = message;
    return false;
}

bool ReadArchiveTextUtf8(const void *value, std::size_t count, unsigned unit, std::string &out)
{
    char text[MM_STRING_UTF8_MAX];
    std::size_t written;
    const int status = mm_string_text_utf8(mm_native_text_host(), value, count, unit, text, sizeof(text), &written);
    if (status < 0)
        throw std::range_error("Archive text cannot be decoded.");
    if (!status)
        return false;
    out.assign(text, written);
    return true;
}
} // namespace

bool ArchiveCatalogError(int status, const MMChunkEntry &parent, Error &error)
{
    if (status == MM_ARCHIVE_CATALOG_CHILD)
    {
        error.hasChunk = true;
        error.chunk = {parent.type, parent.number};
        return Fail(error, ErrorCode::corrupt_archive, "Cannot read a child reference.");
    }
    if (status == MM_ARCHIVE_CATALOG_MEMORY)
        return Fail(error, ErrorCode::out_of_memory, "Insufficient memory to open archive.");
    return Fail(error, ErrorCode::corrupt_archive, "Cannot read the chunk index.");
}

bool ArchiveInput::Open(const std::string &utf8Path, Error &error, int mode, const Options &options, bool checkLiveLinks)
try
{
    error = {};
    ClearDiagnostic();
    String pathString;
    Filename filename;
    char nativePath[MM_STRING_MAX + 1];
#ifdef _WIN32
    constexpr int ansi = 1;
#else
    constexpr int ansi = 0;
#endif
    switch (mm_native_archive_legacy_path(utf8Path.data(), utf8Path.size(), ansi, mm_native_text_host(), nativePath))
    {
    case MM_ARCHIVE_PATH_OK:
        break;
    case MM_ARCHIVE_PATH_EMPTY:
        return Fail(error, ErrorCode::invalid_path, "Enter a file path.");
    case MM_ARCHIVE_PATH_TOO_LONG:
        return Fail(error, ErrorCode::invalid_path, "The core file API supports paths up to 255 bytes.");
    case MM_ARCHIVE_PATH_BAD_UTF8:
        return Fail(error, ErrorCode::unsupported_format, "Archive text cannot be decoded.");
    case MM_ARCHIVE_PATH_LOSSY:
        return Fail(error, ErrorCode::invalid_path,
                    "This path is not representable in the legacy Windows file encoding. Move the file to a simpler path.");
    default:
        return Fail(error, ErrorCode::invalid_path, "Cannot convert this path to the legacy Windows file encoding.");
    }
    pathString.SetSzs(nativePath);
    if (!filename.FBuildFromPath(&pathString))
        return Fail(error, ErrorCode::open_failed, "Cannot open this file as a chunky archive.");
    const auto profile = KauaiPrivateArchiveProfile();
    const MMArchiveReaderOptions readerOptions = {
        options.maxDecodedBytes,
        options.maxStringTableBytes,
        profile.encoding,
        profile.convert,
        nullptr,
        [](void *) -> int { return !AssertionFailed(); },
        nullptr,
        profile.legacy_name,
    };
    const MMNativeArchiveInputHost host = {
        &filename,
        [](void *p, int working, int *selected, MMChunkOwnerRead **reader) {
            bool useLive;
            auto cache = OpenSharedTypedCache(static_cast<Filename *>(p), working != 0, useLive, reader);
            *selected = useLive;
            return cache;
        },
        [](void *p, MMNativeArchiveSource *out) { return OpenNativeArchiveSource(static_cast<Filename *>(p), out); },
    };
    ArchiveInput next;
    switch (mm_native_archive_input_open(&next.input, mode, &readerOptions, &host))
    {
    case MM_NATIVE_ARCHIVE_INPUT_OK:
        break;
    case MM_NATIVE_ARCHIVE_INPUT_MEMORY:
        return Fail(error, ErrorCode::out_of_memory, "Insufficient memory to open archive.");
    case MM_NATIVE_ARCHIVE_INPUT_LIVE_INDEX:
        return Fail(error, ErrorCode::corrupt_archive, "Cannot read the chunk index.");
    default:
        return Fail(error, ErrorCode::open_failed, "Cannot open this file as a chunky archive.");
    }
    if (checkLiveLinks && next.input.live)
    {
        MMChunkEntry failed{};
        const int status = mm_archive_reader_catalog(next.Reader(), nullptr, nullptr, &failed);
        if (status != MM_ARCHIVE_CATALOG_OK)
            return ArchiveCatalogError(status, failed, error);
    }
    if (AssertionFailed())
        return Fail(error, ErrorCode::corrupt_archive, "The legacy reader rejected the archive index.");
    next.path = utf8Path;
    Swap(next);
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

void ArchiveInput::Swap(ArchiveInput &other) noexcept
{
    std::swap(input, other.input);
    path.swap(other.path);
}

MMResourceCache *ArchiveInput::NewResourceCache(MMI32 budget) const
{
    if (input.live)
        return mm_resource_cache_typed_sibling(input.live, budget);
    return CanPreview() ? NewTypedSourceCache(&input.stored, budget) : nullptr;
}

std::string ArchiveInput::ChunkName(MMU32 type, MMU32 number) const
{
    const auto chunks = mm_archive_reader_access(Reader());
    MMU32 position;
    if (mm_chunk_find(&chunks, type, number, &position) != MM_CHUNK_OK)
        return {};
    MMMovieTitle name;
    const auto codec = KauaiChunkTitleIO();
    mm_movie_title_init(&name, sizeof(achar));
    if (!mm_archive_reader_title(Reader(), position, &name, &codec))
        return {};
    return ArchiveTextUtf8(mm_movie_title_text(&name), name.count);
}

std::string ArchiveTextUtf8(const void *value, std::size_t unitsCount)
{
    std::string result;
    ReadArchiveTextUtf8(value, unitsCount, sizeof(achar), result);
    return result;
}

bool ArchiveTextUtf8(const MMStringValue &value, std::string &out)
{
    const auto *text = static_cast<const unsigned char *>(value.buffer);
    const auto count = value.unit == 1 ? text[0] : static_cast<const unsigned short *>(value.buffer)[0];
    return ReadArchiveTextUtf8(text + value.unit, count, value.unit, out);
}
} // namespace Internal

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
std::string Identity(const Id &id)
{
    char buffer[64];
    std::snprintf(buffer, sizeof(buffer), "%s / %08x", TypeLabel(id.type).c_str(), static_cast<unsigned>(id.number));
    return buffer;
}
} // namespace MM
