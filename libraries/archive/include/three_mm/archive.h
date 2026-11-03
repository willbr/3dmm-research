/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef THREE_MM_ARCHIVE_H
#define THREE_MM_ARCHIVE_H
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace ThreeMM
{
struct Id
{
    std::uint32_t type = 0, number = 0;
    bool operator==(const Id &other) const
    {
        return type == other.type && number == other.number;
    }
};
enum class ErrorCode
{
    none,
    invalid_argument,
    invalid_path,
    open_failed,
    corrupt_archive,
    decode_failed,
    unsupported_format,
    resource_limit,
    out_of_memory,
    io_error
};
struct Error
{
    ErrorCode code = ErrorCode::none;
    std::string message;
    bool hasChunk = false;
    Id chunk;
    explicit operator bool() const
    {
        return code != ErrorCode::none;
    }
};
struct Options
{
    // Decompression is whole-chunk. Callers choose a budget; the CLI retains
    // the legacy reader's full range, while interactive hosts use a smaller one.
    std::uint32_t maxDecodedBytes = 0x7fffffffu;
    std::uint32_t maxStringTableBytes = 64u * 1024 * 1024;
};
struct Child
{
    std::uint32_t slot = 0;
    Id target;
    bool present = false;
};
struct Chunk
{
    Id id;
    std::string name; // UTF-8, without display sanitization.
    // Current legacy reader code units, retained for strict dump compatibility.
    std::vector<std::uint16_t> nameUnits;
    std::uint32_t storedBytes = 0;
    bool packed = false, loner = false, forest = false;
    std::vector<Child> children;
};
struct PayloadPage
{
    std::uint32_t totalBytes = 0, offset = 0;
    std::vector<std::uint8_t> bytes;
};
enum class RecordKind
{
    opaque,
    movie,
    scene,
    actor,
    caption
};
struct MovieHeader
{
    std::int32_t version = 0, backVersion = 0;
};
struct SceneHeader
{
    std::int32_t firstFrame = 0, lastFrame = 0, transition = 0;
};
struct ActorHeader
{
    std::int32_t actorId = 0, firstFrame = 0, lastFrame = 0;
};
struct Bounds
{
    std::int32_t left = 0, top = 0, right = 0, bottom = 0;
};
struct CaptionHeader
{
    std::int32_t firstFrame = 0, maxFrame = 0;
    bool story = false;
    Bounds bounds;
    Id text;
};
// Only the member identified by kind is meaningful. Unknown types are opaque.
struct RecordHeader
{
    RecordKind kind = RecordKind::opaque;
    MovieHeader movie;
    SceneHeader scene;
    ActorHeader actor;
    CaptionHeader caption;
};
struct StringTableEntry
{
    std::uint32_t index = 0;
    std::string text;                // UTF-8. Valid legacy Pascal strings contain no nulls.
    std::vector<std::uint8_t> extra; // Exact stored bytes, including byte order.
};
struct StringTablePage
{
    std::uint32_t totalEntries = 0, firstEntry = 0, extraBytes = 0;
    std::vector<StringTableEntry> entries;
};
// Owns one read-only archive and its decode cache. No host globals or callbacks
// are required. A session is used on one thread at a time; legacy core globals
// still preclude promising concurrent independent sessions.
class Archive
{
    friend class Preview;
    void *NativeFile() const; // Private bridge to the engine preview adapter.
    struct Impl;
    std::unique_ptr<Impl> _impl;

  public:
    explicit Archive(Options options = {});
    ~Archive();
    Archive(const Archive &) = delete;
    Archive &operator=(const Archive &) = delete;
    // UTF-8 path; failed Open preserves the previous session. Windows retains
    // the legacy ANSI path repertoire and the core's 255-byte path limit.
    bool Open(const std::string &utf8Path, Error &error);
    void Close();
    // References remain valid until a successful Open, Close, or destruction.
    const std::vector<Chunk> &Chunks() const;
    const std::string &Path() const;
    std::uint32_t NameCodeUnitBits() const;
    // Outputs are cleared on failure. Payload offsets beyond EOF return empty
    // data at EOF. String pages cap count at 128; payload pages cap at 1 MiB.
    bool ReadPayload(std::size_t index, std::uint32_t offset, std::uint32_t count, PayloadPage &page, Error &error);
    bool ReadRecord(std::size_t index, RecordHeader &record, Error &error);
    bool ReadStrings(std::size_t index, std::uint32_t first, std::uint32_t count, StringTablePage &page, Error &error);
};
std::string TypeLabel(std::uint32_t type);
std::string TypeDisplayName(std::uint32_t type);
std::string Identity(const Id &id);
} // namespace ThreeMM
#endif
