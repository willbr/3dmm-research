/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "three_mm/nick_expansion.h"
#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>
#include "soc_core.h"
#undef size
ASSERTNAME
namespace fs = std::filesystem;
using Bytes = std::vector<byte>;
void WarnProc(schar *file, LegacyLong line, schar *message)
{
    std::fprintf(stderr, "%s:%d %s\n", file, int(line), message);
}
bool FAssertProc(schar *file, LegacyLong line, schar *message, void *, LegacyLong)
{
    WarnProc(file, line, message);
    std::exit(1);
}
void Check(bool condition)
{
    if (!condition)
        std::exit(1);
}
Filename Name(const fs::path &path)
{
    String text;
    auto value = fs::absolute(path).string();
    text.SetSzs(value.data());
    Filename name;
    Check(name.FBuildFromPath(&text));
    return name;
}
void Word(Bytes &bytes, std::size_t offset, unsigned value, unsigned width = 4)
{
    for (unsigned i = 0; i < width; ++i)
        bytes[offset + i] = byte(value >> (8 * i));
}
Bytes Palette()
{
    Bytes bytes(1036);
    Word(bytes, 0, 0x03030001);
    Word(bytes, 4, 4);
    Word(bytes, 8, 256);
    for (unsigned i = 0; i < 256; ++i)
        bytes[12 + 4 * i] = bytes[13 + 4 * i] = bytes[14 + 4 * i] = byte(i);
    return bytes;
}
Bytes Texture()
{
    Bytes bytes(20 + 12 * 8, 0x99);
    std::fill_n(bytes.begin(), 20, 0);
    Word(bytes, 0, 0x03030001);
    Word(bytes, 4, 12, 2);
    bytes[6] = 3;
    bytes[7] = 2;
    Word(bytes, 12, 8, 2);
    Word(bytes, 14, 8, 2);
    for (unsigned y = 0; y < 8; ++y)
        for (unsigned x = 0; x < 8; ++x)
            bytes[20 + 12 * y + x] = byte(x + 10);
    bytes[20] = 0; // Texture transparency remains index zero.
    return bytes;
}
Bytes Bitmap()
{
    Bytes bytes(40);
    Word(bytes, 0, 0x03030001);
    Word(bytes, 16, 4); // Rectangle width.
    Word(bytes, 20, 2); // Rectangle height.
    Word(bytes, 24, 40);
    Word(bytes, 28, 4, 2);
    Word(bytes, 30, 4, 2);
    bytes[32] = bytes[36] = 1; // One transparent pixel before each run.
    bytes[33] = bytes[37] = 2;
    bytes[34] = bytes[38] = 12;
    bytes[35] = bytes[39] = 31;
    return bytes;
}
void Put(PChunkyFile file, ChunkTagOrType type, unsigned number, Bytes bytes)
{
    Check(file->FPutPv(bytes.data(), LegacyLong(bytes.size()), type, number));
}
void Label(PChunkyFile file, ChunkTagOrType type, unsigned number, const char *name)
{
    String text;
    text.SetSzs(const_cast<char *>(name));
    Check(file->FSetName(type, number, &text));
}
Bytes Read(PChunkyFile file, ChunkTagOrType type, unsigned number)
{
    DataBlock block;
    Check(file->FFind(type, number, &block) && block.FUnpackData());
    Bytes bytes(block.Cb());
    Check(block.FReadRgb(bytes.data(), block.Cb(), 0));
    return bytes;
}
const char *discNames[] = {"ACTOR.NTH", "BKGDS.3CN", "BKGDS.NTH", "MTRL.NTH", "MTRLS.3CN",
                           "PROP.NTH",  "SNDS.3CN",  "SOUND.NTH", "TDFS.3CN", "TMPLS.3CN"};
const char *expNames[] = {"NICKACTOR.3TH", "NICKBKGDS.3CN", "NICKBKGDS.3TH", "NICKMTRL.3TH", "NICKMTRLS.3CN",
                          "NICKPROP.3TH",  "NICKSNDS.3CN",  "NICKSOUND.3TH", "NICKTDFS.3CN", "NICKTMPLS.3CN"};
void Fixtures(const fs::path &root)
{
    fs::create_directory(root / "disc");
    fs::create_directory(root / "baseline");
    fs::create_directory(root / "reference");
    for (const auto &path : {root / "disc/NICK3D.CHK", root / "base.chk"})
    {
        auto name = Name(path);
        auto file = ChunkyFile::PcflCreate(&name, fcflWriteEnable);
        Check(file != nullptr);
        Put(file, kctgColorTable, 0, Palette());
        Check(file->FSave(kctgSoc));
        ReleasePpo(&file);
    }
    for (unsigned index = 0; index < 10; ++index)
    {
        auto name = Name(root / "disc" / discNames[index]);
        auto file = ChunkyFile::PcflCreate(&name, fcflWriteEnable);
        Check(file != nullptr);
        Put(file, 'KEEP', 1, {1, 3, 5, 7}); // Opaque model/audio stand-in, never modified.
        if (index != 6 && index != 8)
            Put(file, 'TMAP', 0x100, Texture());
        if (index == 0)
            Put(file, 'MBMP', 77, Bitmap());
        if (index == 2)
        {
            Put(file, 'GOKD', 20, {1});
            Put(file, kctgBkth, 0x10000, {2});
            Check(file->FAdoptChild(kctgBkth, 0x10000, 'GOKD', 20, 0));
        }
        if (index == 3)
        {
            for (unsigned i = 0x1e99c; i <= 0x1e9a8; ++i)
                Put(file, kctgMtth, i, {1});
            for (unsigned i : {0x1eba0u, 0x1eba3u, 0x1eba4u, 0x1eba8u, 0x1ebacu, 0x1ebb0u, 0x1ebb1u, 0x1ebbcu, 0x1ebbeu,
                               0x1ebc1u, 0x1ebc8u, 0x1ebc9u, 0x1ebcfu})
                Put(file, kctgMtth, i, {1});
            Put(file, kctgMtth, 0x1eba1, {1});
            Put(file, 'GOKD', 50, {2});
            Check(file->FAdoptChild(kctgMtth, 0x1eba1, 'GOKD', 50, 0));
            Check(file->FAdoptChild(kctgMtth, 0x1e99c, 'GOKD', 50, 0)); // Shared child survives removal.
        }
        if (index == 4 || index == 9)
        {
            Bytes bytes(20);
            Word(bytes, 0, 0x03030001);
            Put(file, kctgMtrl, 0x1e99c, bytes);
        }
        if (index == 5)
            Put(file, kctgPrth, 0x11010, {1});
        if (index == 7)
        {
            Put(file, kctgSvth, 1, {1});
            Label(file, kctgSvth, 1,
                  "a\x92"
                  "b");
        }
        Check(file->FSave(kctgSoc));
        ReleasePpo(&file);
    }
    ThreeMM::NickConversionStats stats;
    std::string error;
    Check(ThreeMM::ConvertNickExpansion((root / "disc").string(), (root / "base.chk").string(),
                                        (root / "baseline").string(), stats, error));
    Check(stats.archives == 10 && stats.images == 9);
    auto name = Name(root / "baseline/NICKMTRL.3TH");
    auto file = ChunkyFile::PcflOpen(&name, fcflNil);
    Check(file && !file->FFind(kctgMtth, 0x1e99c) && file->FFind('GOKD', 0x1eba1) && !file->FFind('GOKD', 50));
    ChildChunkIdentification child;
    Check(file->FGetKid(kctgMtth, 0x1eba1, 0, &child) && child.cki.cno == 0x1eba1);
    ReleasePpo(&file);
    auto materialName = Name(root / "baseline/NICKMTRLS.3CN");
    file = ChunkyFile::PcflOpen(&materialName, fcflNil);
    Check(file != nullptr);
    auto material = Read(file, kctgMtrl, 0x1e99c);
    Check(material[14] == 87 && material[15] == 7);
    auto texture = Read(file, 'TMAP', 0x100);
    Check(texture[20] == 0 && texture[28] == 0x99); // Transparency and row padding.
    ReleasePpo(&file);
    auto soundName = Name(root / "baseline/NICKSOUND.3TH");
    file = ChunkyFile::PcflOpen(&soundName, fcflNil);
    String label;
    Check(file && file->FGetName(kctgSvth, 1, &label) && !std::strcmp(label.Psz(), "a'b"));
    ReleasePpo(&file);
    // A local reference with independently edited synthetic images. No game art.
    for (const auto &path : fs::directory_iterator(root / "baseline"))
        fs::copy_file(path.path(), root / "reference" / path.path().filename());
    auto actorName = Name(root / "reference/NICKACTOR.3TH");
    file = ChunkyFile::PcflOpen(&actorName, fcflWriteEnable);
    Check(file != nullptr);
    auto bitmap = Read(file, 'MBMP', 77);
    bitmap[34] = 91;
    Put(file, 'MBMP', 77, bitmap);
    texture = Read(file, 'TMAP', 0x100);
    Word(texture, 14, 9, 2);
    texture.insert(texture.end(), 12, 21); // Different image dimensions/length.
    Put(file, 'TMAP', 0x100, texture);
    Check(file->FSave(kctgSoc));
    ReleasePpo(&file);
    ThreeMM::NickComparison comparison;
    Check(
        ThreeMM::CompareNickExpansion((root / "baseline").string(), (root / "reference").string(), comparison, error));
    Check(comparison.otherDifferences == 0 && comparison.imageDifferences == 2);
    fs::create_directory(root / "images");
    fs::create_directory(root / "restored");
    Check(ThreeMM::ExportNickImages((root / "reference").string(), (root / "images").string(), error));
    Check(ThreeMM::ReplaceNickImages((root / "baseline").string(), (root / "images").string(),
                                     (root / "restored").string(), error));
    Check(
        ThreeMM::CompareNickExpansion((root / "restored").string(), (root / "reference").string(), comparison, error));
    Check(comparison.otherDifferences == 0 && comparison.imageDifferences == 0);
    Check(!ThreeMM::ConvertNickExpansion((root / "disc").string(), (root / "base.chk").string(),
                                         (root / "baseline").string(), stats, error));
}
int main(int argc, char **argv)
{
    if (argc != 2 && argc != 3)
        return 2;
    fs::path root(argv[argc - 1]);
    if (argc == 2)
        root /= "run-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    fs::create_directories(root);
    Check(fs::is_empty(root));
    Fixtures(root);
    return 0;
}
