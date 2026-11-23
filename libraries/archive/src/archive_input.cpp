#include "archive_input.h"
#include "diagnostics.h"
#include <algorithm>
#include <codecvt>
#include <cstdio>
#include <locale>
#include <stdexcept>
#include <utility>
#include "kauai_core.h"
#include "platform_text.h"
#include "resource_archive_host.h"
#undef size
ASSERTNAME

namespace MM {
namespace Internal {
namespace {
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

bool Fail(Error &error, ErrorCode code, const char *message)
{
    error.code = code;
    error.message = message;
    return false;
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
    std::string message;
    if (!NativePath(utf8Path, pathString, message))
        return Fail(error, ErrorCode::invalid_path, message.c_str());
    if (!filename.FBuildFromPath(&pathString))
        return Fail(error, ErrorCode::open_failed, "Cannot open this file as a chunky archive.");
    const MMArchiveReaderOptions readerOptions = {
        options.maxDecodedBytes,
        options.maxStringTableBytes,
        koskCur,
        [](void *, const void *input, size_t bytes, int source, void *output, size_t capacity) -> size_t {
            return CchTranslateRgb(const_cast<void *>(input), bytes, source, static_cast<achar *>(output), capacity);
        },
        nullptr,
        [](void *) -> int { return !AssertionFailed(); },
        nullptr,
        KauaiEncodeLegacyChunkName,
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
        return NewTypedSiblingCache(input.live, budget);
    return CanPreview() ? NewTypedSourceCache(&input.stored, budget) : nullptr;
}

std::string ArchiveInput::ChunkName(MMU32 type, MMU32 number) const
{
    const auto chunks = mm_archive_reader_access(Reader());
    MMU32 position;
    if (mm_chunk_find(&chunks, type, number, &position) != MM_CHUNK_OK)
        return {};
    String name;
    const auto status = mm_archive_reader_name(Reader(), position,
        [](void *p, const MMReadStream *stream) -> int {
            unsigned char wire[MM_CHUNK_NAME_WIRE_BYTES];
            return stream->length <= sizeof(wire) && mm_read_exact(stream, 0, wire, stream->length) &&
                static_cast<String *>(p)->FSetData(wire, stream->length) ? MM_MOVIE_OK : MM_MOVIE_BAD_FILE;
        }, &name);
    return status == MM_MOVIE_OK ? ArchiveTextUtf8(name.Psz(), name.Cch()) : std::string();
}

std::string ArchiveTextUtf8(const void *value, std::size_t unitsCount)
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
