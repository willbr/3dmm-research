/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "iso_cache.h"
#if defined(_WINDOWS_) || defined(_INC_WINDOWS)
#error ISO cache interfaces must not import Windows SDK headers
#endif
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <future>
#include <random>
#include <stdexcept>
#include <vector>
namespace fs = std::filesystem;
static void Check(bool value, int line)
{
    if (!value)
        throw std::runtime_error("ISO cache check failed at line " + std::to_string(line));
}
#define CHECK(value) Check((value), __LINE__)
static void Both32(unsigned char *p, std::uint32_t value)
{
    for (int i = 0; i < 4; ++i)
    {
        p[i] = static_cast<unsigned char>(value >> (i * 8));
        p[7 - i] = p[i];
    }
}
static std::vector<unsigned char> Record(const std::string &name, unsigned sector, unsigned bytes, bool directory)
{
    std::vector<unsigned char> result(33 + name.size() + (name.size() % 2 == 0));
    result[0] = static_cast<unsigned char>(result.size());
    Both32(result.data() + 2, sector);
    Both32(result.data() + 10, bytes);
    result[25] = directory ? 2 : 0;
    result[28] = result[31] = 1;
    result[32] = static_cast<unsigned char>(name.size());
    std::copy(name.begin(), name.end(), result.begin() + 33);
    return result;
}
static std::string Read(const fs::path &path)
{
    std::ifstream input(path, std::ios::binary);
    CHECK(input.good());
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}
static void Write(const fs::path &path, const void *data, std::size_t bytes)
{
    std::ofstream output(path, std::ios::binary);
    output.write(static_cast<const char *>(data), bytes);
    output.close();
    CHECK(output.good());
}
static std::vector<unsigned char> Fixture(bool unsafe = false, bool multiExtent = false)
{
    std::vector<unsigned char> image(64 * 2048);
    auto pvd = image.data() + 16 * 2048;
    pvd[0] = pvd[6] = 1;
    std::memcpy(pvd + 1, "CD001", 5);
    Both32(pvd + 80, 64);
    pvd[129] = pvd[130] = 8;
    const auto self = std::string(1, '\0'), parent = std::string(1, '\1');
    auto root = Record(self, 18, 2048, true);
    std::copy(root.begin(), root.end(), pvd + 156);
    const auto directory = [&](unsigned sector, const std::vector<std::vector<unsigned char>> &entries) {
        auto offset = sector * 2048;
        for (const auto &record : entries)
        {
            std::copy(record.begin(), record.end(), image.begin() + offset);
            offset += static_cast<unsigned>(record.size());
        }
    };
    directory(18, {root, Record(parent, 18, 2048, true), Record("3DMOVIE", 19, 2048, true),
                   Record("SAMPLES", 20, 2048, true), Record("SETUP", 21, 2048, true)});
    std::vector<std::vector<unsigned char>> product = {Record(self, 19, 2048, true), Record(parent, 18, 2048, true)};
    unsigned sector = 24;
    for (const auto name :
         {"3DMOVIE.CHK", "BLDGDATA.CHK", "BLDGHD.CHK", "BUILDING.CHK", "STUDIO.CHK", "SHARED.CHK", "SHARECD.CHK",
          "HELP.CHK", "HELPAUD.CHK", "TMPLS.3CN", "BKGDS.3CN", "MTRLS.3CN", "TDFS.3CN", "SNDS.3CN", "3DMOVIE.EXE"})
    {
        const auto content = std::string("ISO ") + name;
        const auto filename = unsafe && sector == 24 ? "../ESCAPE.CHK" : name;
        auto record = Record(std::string(filename) + ";1", sector, static_cast<unsigned>(content.size()), false);
        if (multiExtent && sector == 24)
            record[25] |= 0x80;
        product.push_back(record);
        std::copy(content.begin(), content.end(), image.begin() + sector++ * 2048);
    }
    directory(19, product);
    directory(20, {Record(self, 20, 2048, true), Record(parent, 18, 2048, true), Record("BONGO.3MM;1", 40, 6, false)});
    std::memcpy(image.data() + 40 * 2048, "sample", 6);
    directory(21, {Record(self, 21, 2048, true), Record(parent, 18, 2048, true), Record("COMIC.TTF;1", 41, 4, false)});
    std::memcpy(image.data() + 41 * 2048, "font", 4);
    return image;
}
template <class Callback> static void Reject(Callback callback)
{
    bool failed = false;
    try
    {
        callback();
    }
    catch (const std::exception &)
    {
        failed = true;
    }
    CHECK(failed);
}
int main(int argc, char **argv)
{
    try
    {
        CHECK(argc == 2);
        const auto root = fs::absolute(fs::path(argv[1])) / ("run-" + std::to_string(std::random_device()()));
        fs::create_directories(root);
        const auto image = root / "retail image.iso", cache = root / "cache";
        auto bytes = Fixture();
        Write(image, bytes.data(), bytes.size());
        const unsigned char bank[] = {2, 4, 6, 8};
        const std::vector<StudioISO::Supplement> supplements = {{"assets/bank.sf2", bank, sizeof(bank)}};
        const auto first = StudioISO::Prepare(image, cache, supplements);
        CHECK(!first.reused);
        CHECK(Read(first.root / "3D Movie Maker/BLDGDATA.CHK") == "ISO BLDGDATA.CHK");
        CHECK(Read(first.root / "Users/Melanie/BONGO.3MM") == "sample");
        CHECK(Read(first.root.parent_path() / "fonts/COMIC.TTF") == "font");
        CHECK(Read(first.root.parent_path() / "assets/bank.sf2") ==
              std::string(reinterpret_cast<const char *>(bank), 4));
        CHECK(!fs::exists(first.root / "3D Movie Maker/3DMOVIE.EXE"));
        const auto stamp = fs::last_write_time(first.root / "3D Movie Maker/BLDGDATA.CHK");
        const auto second = StudioISO::Prepare(image, cache, supplements);
        CHECK(second.reused && second.root == first.root);
        CHECK(fs::last_write_time(first.root / "3D Movie Maker/BLDGDATA.CHK") == stamp);
        Write(first.root / "3D Movie Maker/BKGDS.3CN", "", 0);
        const auto repaired = StudioISO::Prepare(image, cache, supplements);
        CHECK(!repaired.reused && repaired.root != first.root);
        CHECK(Read(repaired.root / "3D Movie Maker/BKGDS.3CN") == "ISO BKGDS.3CN");
        CHECK(fs::exists(first.root)); // Existing caches and user changes are preserved.
        CHECK(StudioISO::Prepare(image, cache, supplements).root == repaired.root);
        CHECK(StudioISO::Prepare(image, cache).root !=
              repaired.root); // Changed supplemental resources invalidate cache.
        Reject([&] { StudioISO::Prepare(image, cache, {{"../escape", bank, sizeof(bank)}}); });
        Reject([&] { StudioISO::Prepare(image, cache, {{"Microsoft Kids/3D Movie Maker/BLDGDATA.CHK", bank, 4}}); });
        const auto original = Read(image);
        for (const auto malformed : {Fixture(true), Fixture(false, true), std::vector<unsigned char>(512)})
        {
            const auto invalid = root / "invalid.iso";
            Write(invalid, malformed.data(), malformed.size());
            Reject([&] { StudioISO::Prepare(invalid, cache); });
        }
        bytes[16 * 2048 + 1] = 'X';
        const auto invalid = root / "bad-signature.iso";
        Write(invalid, bytes.data(), bytes.size());
        Reject([&] { StudioISO::Prepare(invalid, cache); });
        bytes = Fixture();
        bytes.resize(bytes.size() - 2048);
        Write(invalid, bytes.data(), bytes.size());
        Reject([&] { StudioISO::Prepare(invalid, cache); });
        const auto concurrent = root / "concurrent-cache";
        auto task = [&] { return StudioISO::Prepare(image, concurrent); };
        auto left = std::async(std::launch::async, task), right = std::async(std::launch::async, task);
        const auto a = left.get(), b = right.get();
        CHECK(a.root == b.root && a.reused != b.reused);
        CHECK(Read(image) == original);
        std::puts("ISO cache: retail assets, samples/fonts, reuse, repair, supplements, malformed input and "
                  "concurrency passed");
        return 0;
    }
    catch (const std::exception &error)
    {
        std::fprintf(stderr, "%s\n", error.what());
        return 1;
    }
}
