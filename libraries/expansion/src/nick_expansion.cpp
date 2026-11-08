/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "three_mm/nick_expansion.h"
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
#include "soc_core.h"
#undef size

namespace ThreeMM
{
namespace
{
namespace fs = std::filesystem;
using Bytes = std::vector<byte>;
using Key = std::pair<ChunkTagOrType, ChunkNumber>;
struct Chunk
{
    Bytes data;
    std::string name;
    bool loner = false, forest = false;
    std::vector<ChildChunkIdentification> children;
};
using Chunks = std::map<Key, Chunk>;
struct ReleaseFile
{
    void operator()(PChunkyFile file) const
    {
        ReleasePpo(&file);
    }
};
using File = std::unique_ptr<ChunkyFile, ReleaseFile>;
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
Filename FilenameFor(const fs::path &path)
{
    const auto value = fs::absolute(path).string();
    if (value.size() > 255)
        Fail("Archive path exceeds the core's 255-byte limit");
    String text;
    text.SetSzs(const_cast<char *>(value.c_str()));
    Filename name;
    if (!name.FBuildFromPath(&text))
        Fail("Invalid archive path: " + value);
    return name;
}
Chunks Read(const fs::path &path)
{
    auto name = FilenameFor(path);
    File file(ChunkyFile::PcflOpen(&name, fcflNil));
    if (!file)
        Fail("Cannot read chunky archive: " + path.string());
    Chunks chunks;
    for (LegacyLong i = 0; i < file->Ccki(); ++i)
    {
        ChunkIdentification id;
        DataBlock block;
        if (!file->FGetCki(i, &id, nullptr, &block) || !block.FUnpackData() || block.Cb() > 128 * 1024 * 1024)
            Fail("Cannot decode chunk: " + path.string());
        Chunk chunk;
        chunk.data.resize(block.Cb());
        if (block.Cb() && !block.FReadRgb(chunk.data.data(), block.Cb(), 0))
            Fail("Cannot read chunk payload: " + path.string());
        String label;
        file->FGetName(id.ctg, id.cno, &label); // False means an unnamed chunk.
        chunk.name.assign(label.Psz(), label.Cch());
        chunk.loner = file->FLoner(id.ctg, id.cno);
        chunk.forest = file->FForest(id.ctg, id.cno);
        for (LegacyLong childIndex = 0; childIndex < file->Ckid(id.ctg, id.cno); ++childIndex)
        {
            ChildChunkIdentification child;
            if (!file->FGetKid(id.ctg, id.cno, childIndex, &child))
                Fail("Cannot read chunk child");
            chunk.children.push_back(child);
        }
        chunks.emplace(Key{id.ctg, id.cno}, std::move(chunk));
    }
    return chunks;
}
void Write(const fs::path &path, const Chunks &chunks)
{
    if (fs::exists(path))
        Fail("Output already exists: " + path.string());
    auto filename = FilenameFor(path);
    File file(ChunkyFile::PcflCreate(&filename, fcflWriteEnable));
    if (!file)
        Fail("Cannot create output archive: " + path.string());
    for (const auto &entry : chunks)
    {
        auto key = entry.first;
        const auto &chunk = entry.second;
        if (!file->FPutPv(const_cast<byte *>(chunk.data.data()), LegacyLong(chunk.data.size()), key.first, key.second))
            Fail("Cannot write output chunk");
        String name;
        name.SetSzs(const_cast<char *>(chunk.name.c_str()));
        if (!file->FSetName(key.first, key.second, &name))
            Fail("Cannot write output chunk name");
    }
    for (const auto &entry : chunks)
    {
        auto key = entry.first;
        for (const auto &child : entry.second.children)
        {
            if (!chunks.count({child.cki.ctg, child.cki.cno}) ||
                !file->FAdoptChild(key.first, key.second, child.cki.ctg, child.cki.cno, child.chid, false))
                Fail("Invalid output chunk link");
        }
    }
    // Clearing a loner bit deletes a chunk with no parents. Restore flags only
    // after the complete graph has been adopted, including later-sorted parents.
    for (const auto &entry : chunks)
    {
        file->SetForest(entry.first.first, entry.first.second, entry.second.forest);
        file->SetLoner(entry.first.first, entry.first.second, entry.second.loner);
    }
    if (!file->FSave(kctgSoc))
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
    auto found = resources.find({kctgColorTable, 0});
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
        Key target{child.cki.ctg, child.cki.cno};
        bool shared = false;
        for (const auto &entry : chunks)
            for (const auto &link : entry.second.children)
                shared |= Key{link.cki.ctg, link.cki.cno} == target;
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
            found = mapping.find({child.cki.ctg, child.cki.cno});
            if (found != mapping.end())
            {
                child.cki.ctg = found->second.first;
                child.cki.cno = found->second.second;
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
        for (ChunkNumber i = 0x1e99c; i <= 0x1e9a8; ++i)
            RemoveTree(chunks, {kctgMtth, i});
        for (ChunkNumber i : {0x1eba0u, 0x1eba3u, 0x1eba4u, 0x1eba8u, 0x1ebacu, 0x1ebb0u, 0x1ebb1u, 0x1ebbcu, 0x1ebbeu,
                              0x1ebc1u, 0x1ebc8u, 0x1ebc9u, 0x1ebcfu})
            RemoveTree(chunks, {kctgMtth, i});
        std::map<Key, Key> moves;
        for (const auto &entry : chunks)
            if (entry.first.first == kctgMtth)
                for (const auto &child : entry.second.children)
                    if (child.cki.ctg == 'GOKD')
                        moves[{child.cki.ctg, child.cki.cno}] = {'GOKD', entry.first.second};
        Renumber(chunks, moves);
    }
    if (role == "PROP.NTH")
        RemoveTree(chunks, {kctgPrth, 0x11010});
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
        if ((role == "MTRLS.3CN" || role == "TMPLS.3CN") && key.first == kctgMtrl && key.second >= 0x1e99c &&
            key.second <= 0x1e9a8)
        {
            WindowsHeader(chunk.data);
            if (chunk.data.size() != sizeof(MaterialOnFile))
                Fail("Invalid solid material header");
            const auto i = key.second - 0x1e99c;
            chunk.data[14] = role == "MTRLS.3CN" ? materialBases[i] : costumeBases[i];
            chunk.data[15] = 7;
            if (role == "MTRLS.3CN")
                chunk.name = materialNames[i];
        }
        if (role == "TMPLS.3CN" && key.first == kctgCmtl)
        {
            if (key.second == 0x11024)
                chunk.name = "Yellow";
            else if (key.second >= 0x421b2 && key.second <= 0x421c8 && !(key.second % 2))
                chunk.name = costumeNames[(key.second - 0x421b2) / 2];
        }
        if (role == "TMPLS.3CN" && key == Key{kctgMtrl, 0x22221})
            for (auto &child : chunk.children)
                if (child.cki.ctg == 'TMAP' && child.cki.cno == 0x222e3)
                    child.cki.cno = 0x222d2;
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
                else if (type == kctgColorTable)
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
                            other |= u.chid != v.chid || u.cki.ctg != v.cki.ctg || u.cki.cno != v.cki.cno;
                        }
                    if (x.data != y.data)
                    {
                        image = key.first == 'TMAP' || key.first == 'MBMP' || key.first == kctgColorTable;
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
bool ImageType(ChunkTagOrType type)
{
    return type == 'TMAP' || type == 'MBMP' || type == kctgColorTable;
}
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
                    word(child.chid);
                    word(child.cki.ctg);
                    word(child.cki.cno);
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
} // namespace ThreeMM
