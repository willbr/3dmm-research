/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "mm/nick_expansion.h"
#include "nick_metadata.h"
#include <algorithm>
#include <array>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <map>
#include <memory>
#include <set>
#include <sstream>
#include <stdexcept>
#include <vector>
#include "private_archive_host.h"
#include "mm/archive_reader.h"
#include "mm/material_resource.h"
#include "mm/native_archive_file.h"
#include "mm/string_wire.h"
#undef size

namespace MM
{
namespace
{
namespace fs = std::filesystem;
using Bytes = std::vector<byte>;
using Key = std::pair<MMU32, MMU32>;
constexpr MMU32 colorTable = 'GLCR', materialThumbnail = 'MTTH', propThumbnail = 'PRTH';
constexpr MMU32 customMaterial = 'CMTL', studioCreator = 'SOC ';
bool ImageType(MMU32 type)
{
    return type == 'TMAP' || type == 'MBMP' || type == colorTable;
}
struct Chunk
{
    Bytes data;
    std::string name;
    bool loner = false, forest = false;
    std::vector<MMChunkChild> children;
};
using Chunks = std::map<Key, Chunk>;
using File = std::unique_ptr<MMPrivateArchive, decltype(&mm_private_archive_close)>;
struct Names
{
    const char *disc, *expansion;
};
constexpr Names files[] = {{"ACTOR.NTH", "NICKACTOR.3TH"}, {"BKGDS.3CN", "NICKBKGDS.3CN"},
                           {"BKGDS.NTH", "NICKBKGDS.3TH"}, {"MTRL.NTH", "NICKMTRL.3TH"},
                           {"MTRLS.3CN", "NICKMTRLS.3CN"}, {"PROP.NTH", "NICKPROP.3TH"},
                           {"SNDS.3CN", "NICKSNDS.3CN"},   {"SOUND.NTH", "NICKSOUND.3TH"},
                           {"TDFS.3CN", "NICKTDFS.3CN"},   {"TMPLS.3CN", "NICKTMPLS.3CN"}};
[[noreturn]] void Fail(const std::string &message)
{
    throw std::runtime_error(message);
}
File OpenArchive(const fs::path &path, int mode)
{
    const auto value = fs::absolute(path).string();
    if (value.size() > MM_STRING_MAX)
        Fail("Archive path exceeds the core's 255-byte limit");
    const auto profile = KauaiPrivateArchiveProfile();
    return File(mm_native_private_archive_open(value.c_str(), mode, &profile), mm_private_archive_close);
}
Chunks Read(const fs::path &path)
{
    const auto file = OpenArchive(path, MM_PRIVATE_ARCHIVE_READ);
    if (!file)
        Fail("Cannot read chunky archive: " + path.string());
    const auto owner = mm_private_archive_reader(file.get());
    MMArchiveReaderOptions options = {};
    options.max_packed_bytes = 128U * 1024 * 1024;
    MMArchiveReader *opened;
    if (mm_archive_reader_open_owner(owner, &options, &opened) != MM_ARCHIVE_READ_OK)
        Fail("Cannot read chunky archive index: " + path.string());
    const std::unique_ptr<MMArchiveReader, decltype(&mm_archive_reader_close)> reader(opened, mm_archive_reader_close);
    const auto access = mm_archive_reader_access(reader.get());
    Chunks chunks;
    for (MMU32 i = 0; i < access.count; ++i)
    {
        MMChunkEntry entry;
        MMReadStream payload;
        if (mm_chunk_at(&access, i, &entry) != MM_CHUNK_OK ||
            mm_archive_reader_payload(reader.get(), i, &payload) != MM_ARCHIVE_READ_OK ||
            payload.length > options.max_packed_bytes)
            Fail("Cannot decode chunk: " + path.string());
        Chunk chunk;
        chunk.data.resize(payload.length);
        if (payload.length && !mm_read_exact(&payload, 0, chunk.data.data(), payload.length))
            Fail("Cannot read chunk payload: " + path.string());
        String label;
        mm_archive_reader_name(reader.get(), i, [](void *out, const MMReadStream *name) -> int {
            unsigned char wire[kcbMaxDataStn];
            return name->length <= sizeof(wire) && mm_read_exact(name, 0, wire, name->length) &&
                   static_cast<PString>(out)->FSetData(wire, name->length);
        }, &label);
        chunk.name.assign(label.Psz(), label.Cch());
        chunk.loner = (entry.flags & MM_CHUNK_LONER) != 0;
        chunk.forest = (entry.flags & MM_CHUNK_FOREST) != 0;
        for (MMU32 childIndex = 0; childIndex < entry.child_count; ++childIndex)
        {
            MMChunkChild child;
            if (mm_chunk_child_at(&access, i, childIndex, &child) != MM_CHUNK_OK)
                Fail("Cannot read chunk child");
            chunk.children.push_back(child);
        }
        chunks.emplace(Key{entry.type, entry.number}, std::move(chunk));
    }
    return chunks;
}
void Write(const fs::path &path, const Chunks &chunks)
{
    if (fs::exists(path))
        Fail("Output already exists: " + path.string());
    const auto file = OpenArchive(path, MM_PRIVATE_ARCHIVE_CREATE);
    if (!file)
        Fail("Cannot create output archive: " + path.string());
    const auto writer = mm_private_archive_writer(file.get());
    const auto write = [](void *context, const MMStorageWriter *out) -> int {
        const auto bytes = static_cast<const Bytes *>(context);
        return !out->length || out->write_at(out->context, 0, bytes->data(), bytes->size());
    };
    for (const auto &entry : chunks)
    {
        auto key = entry.first;
        const auto &chunk = entry.second;
        if (mm_chunk_put_payload(&writer, key.first, key.second, chunk.data.size(), write,
                                 const_cast<Bytes *>(&chunk.data)) != MM_CHUNK_OK)
            Fail("Cannot write output chunk");
        unsigned char wire[kcbMaxDataStn];
        MMU32 count = 0;
        if (!chunk.name.empty() && !KauaiEncodeLegacyChunkName(nullptr, chunk.name.c_str(), wire, sizeof(wire), &count))
            Fail("Cannot encode output chunk name");
        const MMChunkNameValue name = {wire, count, static_cast<MMU32>(chunk.name.size())};
        if (mm_chunk_name_set_key(&writer.edit, &writer.names, key.first, key.second, &name) != MM_CHUNK_OK)
            Fail("Cannot write output chunk name");
    }
    for (const auto &entry : chunks)
    {
        auto key = entry.first;
        for (const auto &child : entry.second.children)
        {
            if (mm_chunk_adopt(&writer.edit, key.first, key.second, &child, 0) != MM_CHUNK_OK)
                Fail("Invalid output chunk link");
        }
    }
    // Clearing a loner bit deletes a chunk with no parents. Restore flags only
    // after the complete graph has been adopted, including later-sorted parents.
    for (const auto &entry : chunks)
    {
        const auto [type, number] = entry.first;
        if (mm_chunk_set_flag(&writer.edit, type, number, MM_CHUNK_FOREST, entry.second.forest) != MM_CHUNK_OK ||
            mm_chunk_set_flag(&writer.edit, type, number, MM_CHUNK_LONER, entry.second.loner) != MM_CHUNK_OK)
            Fail("Cannot write output chunk flags");
    }
    if (!mm_private_archive_save(file.get(), studioCreator))
        Fail("Cannot save output archive");
}
std::uint16_t U16(const Bytes &bytes, std::size_t offset)
{
    if (offset + 2 > bytes.size())
        Fail("Truncated chunk header");
    return bytes[offset] | (std::uint16_t(bytes[offset + 1]) << 8);
}
std::uint32_t U32(const Bytes &bytes, std::size_t offset)
{
    return U16(bytes, offset) | (std::uint32_t(U16(bytes, offset + 2)) << 16);
}
void WindowsHeader(const Bytes &bytes)
{
    if (U16(bytes, 0) != 1 || U16(bytes, 2) != 0x0303)
        Fail("Unsupported chunk byte order or platform");
}
using Palette = std::array<std::array<byte, 3>, 256>;
using Mapping = std::array<byte, 256>;
Palette BasePalette(const fs::path &path)
{
    auto resources = Read(path);
    auto found = resources.find({colorTable, 0});
    if (found == resources.end())
        Fail("Missing base palette");
    const auto &bytes = found->second.data;
    WindowsHeader(bytes);
    if (bytes.size() != 1036 || U32(bytes, 4) != 4 || U32(bytes, 8) != 256)
        Fail("Invalid base palette");
    Palette palette{};
    for (std::size_t i = 0; i < 256; ++i)
        std::copy_n(bytes.begin() + 12 + 4 * i, 3, palette[i].begin());
    return palette;
}
Mapping MapColours(const Palette &source, const Palette &target)
{
    Mapping mapping{};
    for (unsigned i = 1; i < 256; ++i)
    {
        unsigned minimum = ~0u;
        // 10..155 are 3DMM's fixed material colours. Higher entries are
        // scene-specific; system colours and index-zero transparency are excluded.
        for (unsigned j = 10; j < 156; ++j)
        {
            unsigned distance = 0;
            for (unsigned c = 0; c < 3; ++c)
            {
                int delta = int(source[i][c]) - int(target[j][c]);
                distance += unsigned(delta * delta);
            }
            if (distance < minimum)
            {
                minimum = distance;
                mapping[i] = byte(j);
            }
        }
    }
    return mapping;
}
void Texture(Bytes &bytes, const Mapping &mapping)
{
    WindowsHeader(bytes);
    const auto stride = U16(bytes, 4), width = U16(bytes, 12), height = U16(bytes, 14);
    if (bytes.size() < 20 || bytes[6] != 3 || !width || !height || stride < width ||
        bytes.size() != 20 + std::size_t(stride) * height)
        Fail("Invalid indexed texture");
    for (unsigned y = 0; y < height; ++y)
        for (unsigned x = 0; x < width; ++x)
        {
            auto &pixel = bytes[20 + y * stride + x];
            pixel = mapping[pixel];
        }
}
void Bitmap(Bytes &bytes, const Mapping &mapping)
{
    WindowsHeader(bytes);
    if (bytes.size() < 28 || U32(bytes, 24) != bytes.size() || bytes[4] > 1)
        Fail("Invalid masked bitmap header");
    auto left = std::int32_t(U32(bytes, 8)), top = std::int32_t(U32(bytes, 12));
    auto right = std::int32_t(U32(bytes, 16)), bottom = std::int32_t(U32(bytes, 20));
    const auto width = std::int64_t(right) - left, height = std::int64_t(bottom) - top;
    if (width < 0 || height < 0 || width > 32767 || height > 32767 || 28 + 2 * height > bytes.size())
        Fail("Invalid masked bitmap bounds");
    if (bytes[4])
        bytes[5] = mapping[bytes[5]];
    std::size_t cursor = 28 + 2 * std::size_t(height);
    for (std::size_t row = 0; row < std::size_t(height); ++row)
    {
        const auto rowEnd = cursor + U16(bytes, 28 + 2 * row);
        if (rowEnd > bytes.size())
            Fail("Masked bitmap row extends beyond its chunk");
        std::size_t x = 0;
        while (cursor < rowEnd)
        {
            if (rowEnd - cursor < 2)
                Fail("Truncated masked bitmap run");
            x += bytes[cursor++];
            const unsigned count = bytes[cursor++];
            x += count;
            if (x > std::size_t(width) || (!bytes[4] && count > rowEnd - cursor))
                Fail("Invalid masked bitmap run");
            if (!bytes[4])
                for (unsigned pixel = 0; pixel < count; ++pixel, ++cursor)
                    bytes[cursor] = mapping[bytes[cursor]];
        }
    }
    if (cursor != bytes.size())
        Fail("Trailing masked bitmap data");
}
void RemoveTree(Chunks &chunks, Key key)
{
    auto found = chunks.find(key);
    if (found == chunks.end())
        Fail("Expected browser entry is missing");
    auto children = found->second.children;
    chunks.erase(found);
    for (const auto &child : children)
    {
        Key target{child.type, child.number};
        bool shared = false;
        for (const auto &entry : chunks)
            for (const auto &link : entry.second.children)
                shared |= Key{link.type, link.number} == target;
        if (!shared)
            RemoveTree(chunks, target);
    }
}
void Renumber(Chunks &chunks, const std::map<Key, Key> &mapping)
{
    Chunks output;
    for (auto &entry : chunks)
    {
        auto key = entry.first;
        auto found = mapping.find(key);
        if (found != mapping.end())
            key = found->second;
        for (auto &child : entry.second.children)
        {
            found = mapping.find({child.type, child.number});
            if (found != mapping.end())
            {
                child.type = found->second.first;
                child.number = found->second.second;
            }
        }
        if (!output.emplace(key, std::move(entry.second)).second)
            Fail("Browser renumbering collision");
    }
    chunks.swap(output);
}
void Metadata(Chunks &chunks, const std::string &role)
{
    if (role == "MTRL.NTH")
    {
        for (MMU32 i = 0x1e99c; i <= 0x1e9a8; ++i)
            RemoveTree(chunks, {materialThumbnail, i});
        for (MMU32 i : {0x1eba0u, 0x1eba3u, 0x1eba4u, 0x1eba8u, 0x1ebacu, 0x1ebb0u, 0x1ebb1u, 0x1ebbcu, 0x1ebbeu,
                              0x1ebc1u, 0x1ebc8u, 0x1ebc9u, 0x1ebcfu})
            RemoveTree(chunks, {materialThumbnail, i});
        std::map<Key, Key> moves;
        for (const auto &entry : chunks)
            if (entry.first.first == materialThumbnail)
                for (const auto &child : entry.second.children)
                    if (child.type == 'GOKD')
                        moves[{child.type, child.number}] = {'GOKD', entry.first.second};
        Renumber(chunks, moves);
    }
    if (role == "PROP.NTH")
        RemoveTree(chunks, {propThumbnail, 0x11010});
    if (role == "BKGDS.NTH")
    {
        std::map<Key, Key> moves;
        for (const auto &entry : chunks)
            if (entry.first.first == 'GOKD')
                moves[entry.first] = {'GOKD', entry.first.second + 0xd6d8};
        Renumber(chunks, moves);
    }
    constexpr byte materialBases[] = {87, 23, 15, 127, 111, 143, 95, 79, 71, 39, 87, 79, 103};
    constexpr byte costumeBases[] = {87, 23, 15, 127, 111, 143, 95, 79, 31, 39, 63, 79, 103};
    constexpr const char *materialNames[] = {"Orange",     "Grey",   "Black",  "Green", "Dark Yellow",
                                             "Teal",       "Purple", "Maroon", "Blue",  "Light Blue",
                                             "Bright Red", "Pink",   "Yellow"};
    constexpr const char *costumeNames[] = {"Orange",     "White", "Grey/Black",  "Green", "Aqua", "Purple",
                                            "Violet Red", "Blue",  "Bright Aqua", "Red",   "Pink", "Green Yellow"};
    for (auto &entry : chunks)
    {
        const auto key = entry.first;
        auto &chunk = entry.second;
        if (role == "SOUND.NTH")
            std::replace(chunk.name.begin(), chunk.name.end(), char(0x92), '\'');
        if ((role == "MTRLS.3CN" || role == "TMPLS.3CN") && key.first == MM_MATERIAL_CHUNK && key.second >= 0x1e99c &&
            key.second <= 0x1e9a8)
        {
            WindowsHeader(chunk.data);
            if (chunk.data.size() != 20)
                Fail("Invalid solid material header");
            const auto i = key.second - 0x1e99c;
            chunk.data[14] = role == "MTRLS.3CN" ? materialBases[i] : costumeBases[i];
            chunk.data[15] = 7;
            if (role == "MTRLS.3CN")
                chunk.name = materialNames[i];
        }
        if (role == "TMPLS.3CN" && key.first == customMaterial)
        {
            if (key.second == 0x11024)
                chunk.name = "Yellow";
            else if (key.second >= 0x421b2 && key.second <= 0x421c8 && !(key.second % 2))
                chunk.name = costumeNames[(key.second - 0x421b2) / 2];
        }
        if (role == "TMPLS.3CN" && key == Key{MM_MATERIAL_CHUNK, 0x22221})
            for (auto &child : chunk.children)
                if (child.type == 'TMAP' && child.number == 0x222e3)
                    child.number = 0x222d2;
        if (role == "TMPLS.3CN" && key.first == 'TMAP' && chunk.name.empty())
            for (const auto &labels : Internal::nickTextureLabels)
                if (key.second >= labels.first && key.second <= labels.last)
                {
                    chunk.name = std::to_string(labels.label + key.second - labels.first);
                    break;
                }
    }
}
} // namespace

bool ConvertNickExpansion(const std::string &source, const std::string &baseResources, const std::string &output,
                          NickConversionStats &stats, std::string &error)
{
    stats = {};
    error.clear();
    try
    {
        if (!fs::is_directory(output) || !fs::is_empty(output))
            Fail("Conversion output must be an existing empty directory");
        const auto sourcePalette = BasePalette(fs::path(source) / "NICK3D.CHK");
        const auto targetPalette = BasePalette(baseResources);
        auto editorPalette = targetPalette;
        // Pencil's working palette differed from the game's palette in three
        // fixed slots. Derive the values from disc colours instead of embedding
        // either palette. This reproduces the historical thumbnail lookup.
        editorPalette[11][2] += 1;
        editorPalette[12] = sourcePalette[12];
        editorPalette[21][2] -= 1;
        const auto mapping = MapColours(sourcePalette, editorPalette);
        for (const auto &names : files)
            if (!fs::is_regular_file(fs::path(source) / names.disc))
                Fail(std::string("Missing disc file: ") + names.disc);
        for (const auto &names : files)
        {
            auto chunks = Read(fs::path(source) / names.disc);
            Metadata(chunks, names.disc);
            for (auto &entry : chunks)
            {
                const auto type = entry.first.first;
                if (type == 'TMAP')
                {
                    Texture(entry.second.data, mapping);
                    ++stats.images;
                }
                else if (type == 'MBMP')
                {
                    auto imageMapping = mapping;
                    // Scene colours (156..245) belong to the background itself.
                    if (std::string(names.disc).compare(0, 5, "BKGDS") == 0)
                        for (unsigned i = 156; i < 246; ++i)
                            imageMapping[i] = byte(i);
                    Bitmap(entry.second.data, imageMapping);
                    ++stats.images;
                }
                else if (type == colorTable)
                {
                    auto &bytes = entry.second.data;
                    WindowsHeader(bytes);
                    if (bytes.size() != 956 || U32(bytes, 4) != 4 || U32(bytes, 8) != 236)
                        Fail("Unsupported background palette");
                    // Original backgrounds reserve 10..155 for materials and
                    // retain their own colours in 156..245, including flags.
                    for (unsigned i = 10; i < 156; ++i)
                        std::copy(targetPalette[i].begin(), targetPalette[i].end(), bytes.begin() + 12 + 4 * (i - 10));
                    ++stats.palettes;
                }
            }
            Write(fs::path(output) / names.expansion, chunks);
            stats.chunks += chunks.size();
            ++stats.archives;
        }
        return true;
    }
    catch (const std::exception &failure)
    {
        error = failure.what();
        return false;
    }
}

bool CompareNickExpansion(const std::string &actual, const std::string &reference, NickComparison &comparison,
                          std::string &error)
{
    comparison = {};
    error.clear();
    try
    {
        for (const auto &names : files)
        {
            auto a = Read(fs::path(actual) / names.expansion), b = Read(fs::path(reference) / names.expansion);
            std::set<Key> keys;
            for (const auto &entry : a)
                keys.insert(entry.first);
            for (const auto &entry : b)
                keys.insert(entry.first);
            for (auto key : keys)
            {
                bool other = !a.count(key) || !b.count(key), image = false;
                if (!other)
                {
                    const auto &x = a.at(key), &y = b.at(key);
                    other = x.name != y.name || x.loner != y.loner || x.forest != y.forest ||
                            x.children.size() != y.children.size();
                    if (!other)
                        for (std::size_t i = 0; i < x.children.size(); ++i)
                        {
                            const auto &u = x.children[i], &v = y.children[i];
                            other |= u.slot != v.slot || u.type != v.type || u.number != v.number;
                        }
                    if (x.data != y.data)
                    {
                        image = ImageType(key.first);
                        other |= !image;
                    }
                }
                comparison.otherDifferences += other;
                comparison.imageDifferences += image;
                if ((other || image) && comparison.firstDifference.empty())
                    comparison.firstDifference = std::string(names.expansion) + ":" + std::to_string(key.first) + ":" +
                                                 std::to_string(key.second);
                ++comparison.chunks;
            }
            ++comparison.archives;
        }
        return true;
    }
    catch (const std::exception &failure)
    {
        error = failure.what();
        return false;
    }
}

namespace
{
std::string ImageName(unsigned archive, Key key)
{
    std::ostringstream name;
    name << archive << '-' << std::hex << std::setfill('0') << std::setw(8) << key.first << '-' << std::setw(8)
         << key.second << ".bin";
    return name.str();
}
void EmptyDirectory(const fs::path &path)
{
    if (!fs::is_directory(path) || !fs::is_empty(path))
        Fail("Output must be an existing empty directory");
}
} // namespace
bool ExportNickImages(const std::string &source, const std::string &output, std::string &error)
{
    error.clear();
    try
    {
        EmptyDirectory(output);
        unsigned index = 0;
        for (const auto &names : files)
        {
            auto chunks = Read(fs::path(source) / names.expansion);
            for (const auto &entry : chunks)
                if (ImageType(entry.first.first))
                {
                    std::ofstream stream(fs::path(output) / ImageName(index, entry.first), std::ios::binary);
                    stream.write(reinterpret_cast<const char *>(entry.second.data.data()), entry.second.data.size());
                    if (!stream)
                        Fail("Cannot export decoded image chunk");
                }
            ++index;
        }
        return true;
    }
    catch (const std::exception &failure)
    {
        error = failure.what();
        return false;
    }
}
bool ReplaceNickImages(const std::string &source, const std::string &images, const std::string &output,
                       std::string &error)
{
    error.clear();
    try
    {
        EmptyDirectory(output);
        std::set<std::string> pending;
        for (const auto &entry : fs::directory_iterator(images))
        {
            if (!entry.is_regular_file() || entry.is_symlink())
                Fail("Unexpected image fixup input");
            pending.insert(entry.path().filename().string());
        }
        unsigned index = 0;
        for (const auto &names : files)
        {
            auto chunks = Read(fs::path(source) / names.expansion);
            for (auto &entry : chunks)
                if (ImageType(entry.first.first))
                {
                    const auto name = ImageName(index, entry.first);
                    if (!pending.erase(name))
                        continue;
                    const auto path = fs::path(images) / name;
                    const auto bytes = fs::file_size(path);
                    if (bytes > 128 * 1024 * 1024)
                        Fail("Image fixup is too large");
                    entry.second.data.resize(std::size_t(bytes));
                    std::ifstream stream(path, std::ios::binary);
                    if (!stream.read(reinterpret_cast<char *>(entry.second.data.data()), bytes))
                        Fail("Cannot read image fixup");
                }
            Write(fs::path(output) / names.expansion, chunks);
            ++index;
        }
        if (!pending.empty())
            Fail("Fixup does not identify an existing image chunk");
        return true;
    }
    catch (const std::exception &failure)
    {
        error = failure.what();
        return false;
    }
}
bool WriteNickNonImageState(const std::string &source, const std::string &output, std::string &error)
{
    error.clear();
    try
    {
        if (fs::exists(output))
            Fail("State output already exists");
        std::ofstream stream(output, std::ios::binary);
        const auto word = [&](std::uint32_t value) {
            const char bytes[] = {char(value), char(value >> 8), char(value >> 16), char(value >> 24)};
            stream.write(bytes, sizeof(bytes));
        };
        stream.write("NICKSTATE1", 10);
        for (const auto &names : files)
        {
            auto chunks = Read(fs::path(source) / names.expansion);
            word(std::uint32_t(chunks.size()));
            for (const auto &entry : chunks)
            {
                const auto &chunk = entry.second;
                word(entry.first.first);
                word(entry.first.second);
                word((chunk.loner ? 1u : 0u) | (chunk.forest ? 2u : 0u));
                word(std::uint32_t(chunk.name.size()));
                stream.write(chunk.name.data(), chunk.name.size());
                word(std::uint32_t(chunk.children.size()));
                for (const auto &child : chunk.children)
                {
                    word(child.slot);
                    word(child.type);
                    word(child.number);
                }
                if (ImageType(entry.first.first))
                    word(0);
                else
                {
                    word(std::uint32_t(chunk.data.size()));
                    stream.write(reinterpret_cast<const char *>(chunk.data.data()), chunk.data.size());
                }
            }
        }
        if (!stream)
            Fail("Cannot write canonical non-image state");
        return true;
    }
    catch (const std::exception &failure)
    {
        error = failure.what();
        return false;
    }
}
} // namespace MM
