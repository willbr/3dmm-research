/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "font_host.h"
#include "font_locations.h"
#include "font_names.h"
#include <cstdio>
#include <cstdlib>
#define CHECK(value)                                                                                                   \
    do                                                                                                                 \
    {                                                                                                                  \
        if (!(value))                                                                                                  \
        {                                                                                                              \
            std::fprintf(stderr, "Font host failed at line %d\n", __LINE__);                                           \
            return 1;                                                                                                  \
        }                                                                                                              \
    } while (0)
using namespace KauaiFonts;
FontPath Path(const char *text)
{
    FontPath result;
    for (; *text; ++text)
        result += static_cast<KauaiFiles::PathChar>(*text);
    return result;
}
FontPath Child(FontPath root, const char *leaf)
{
    return root + KauaiFiles::Separator() + Path(leaf);
}
bool Read(const FontPath &path, std::vector<std::uint8_t> &bytes)
{
    auto file = KauaiFiles::Open(path.c_str(), {});
    if (!file)
        return false;
    std::uint32_t size;
    bool result = KauaiFiles::Size(file, &size);
    if (result)
    {
        bytes.resize(size);
        result = KauaiFiles::Read(file, bytes.data(), size);
    }
    KauaiFiles::Close(file);
    return result;
}
bool Copy(const FontPath &source, const FontPath &destination)
{
    std::vector<std::uint8_t> bytes;
    if (!Read(source, bytes))
        return false;
    KauaiFiles::OpenOptions options;
    options.write = true;
    options.creation = KauaiFiles::Creation::CreateAlways;
    auto file = KauaiFiles::Open(destination.c_str(), options);
    if (!file)
        return false;
    const bool result = KauaiFiles::Write(file, bytes.data(), static_cast<std::uint32_t>(bytes.size()));
    KauaiFiles::Close(file);
    return result;
}
struct Cleanup
{
    FontPath root, nested, deeper, outline, bitmap;
    ~Cleanup()
    {
        KauaiFiles::Delete(outline.c_str());
        KauaiFiles::Delete(bitmap.c_str());
        KauaiFiles::DeleteDirectory(deeper.c_str());
        KauaiFiles::DeleteDirectory(nested.c_str());
        KauaiFiles::DeleteDirectory(root.c_str());
    }
};
int main(int argc, char **argv)
{
    if (argc != 4)
        return 2;
    Cleanup fixture;
    fixture.root = Path(argv[3]);
    fixture.nested = Child(fixture.root, "nested");
    fixture.deeper = Child(fixture.nested, "deeper");
    fixture.outline = Child(fixture.deeper, "outline.ttf");
    fixture.bitmap = Child(fixture.root, "system.fon");
    CHECK(KauaiFiles::MakeDirectory(fixture.root.c_str()));
    CHECK(KauaiFiles::MakeDirectory(fixture.nested.c_str()));
    CHECK(KauaiFiles::MakeDirectory(fixture.deeper.c_str()));
    CHECK(Copy(Path(argv[1]), fixture.outline) && Copy(Path(argv[2]), fixture.bitmap));
    FontCatalog catalog;
    CHECK(catalog.AddTree(fixture.root, 0) && catalog.Families().empty());
    CHECK(catalog.AddTree(fixture.root, 1) && catalog.Families().empty());
    CHECK(catalog.AddTree(fixture.root, 2) && !catalog.Families().empty());
    CHECK(!catalog.AddTree(fixture.root, 33));
    const auto family = catalog.Families().front().name;
    FontLocations config;
    config.roots.push_back({fixture.root, true});
    config.systemBitmap = fixture.bitmap;
    FileFontHost host;
    std::vector<FamilyInfo> families;
    CHECK(!host.Families(families) && host.Initialize(config) && host.Families(families) && families.size() >= 2);
    CHECK(!host.Open({"Missing family", 14}, 16, false, false, false));
    auto system = host.Open({"System", 6}, 16, false, false, false);
    Metrics bitmapMetrics;
    CHECK(system && system->Measure({"AV fj", 5}, bitmapMetrics) && bitmapMetrics.cellHeight > 0);
    // A failed reconfiguration must preserve both the catalog and System face.
    auto invalid = config;
    invalid.systemBitmap = Child(fixture.root, "missing.fon");
    CHECK(!host.Initialize(invalid));
    CHECK(host.Open({"System", 6}, 16, false, false, false));
    config.systemBitmap.clear();
    config.systemFamilies = {"Not an installed family", family};
    CHECK(host.Initialize(config));
    for (auto encoding : {Encoding::legacyBytes, Encoding::utf16})
    {
        std::vector<std::uint8_t> name;
        CHECK(NameFromUtf8(family, encoding, name));
        auto actual = host.Open({name.data(), name.size() / (encoding == Encoding::utf16 ? 2 : 1), encoding}, 16, true,
                                true, true);
        auto alias = host.Open({"System", 6}, 16, true, true, true);
        Metrics expected, measured;
        Mask mask;
        CHECK(actual && alias && actual->Measure({"AV fj", 5}, expected) && alias->Measure({"AV fj", 5}, measured));
        CHECK(expected.cellHeight == measured.cellHeight && expected.advance == measured.advance &&
              expected.ascent == measured.ascent);
        CHECK(alias->Rasterize({"AV fj", 5}, mask) && !mask.coverage.empty());
    }
    config.roots[0].recursive = false;
    CHECK(!host.Initialize(config)); // The outline lives two levels down.
    CHECK(host.Open({"System", 6}, 16, false, false, false));
#ifdef _WIN32
    CHECK(_putenv_s("KAUAI_FONT_PATH", argv[3]) == 0);
#else
    CHECK(setenv("KAUAI_FONT_PATH", argv[3], 1) == 0);
#endif
    auto locations = DefaultFontLocations();
    CHECK(!locations.roots.empty() && locations.roots.front().path == fixture.root &&
          locations.roots.front().recursive);
    CHECK(StudioFontFamilies(families) && !families.empty());
    CHECK(CreateStudioFace({"System", 6}, 16, false, false, false));
    const auto stats = FontProviderStats();
    CHECK(stats.portable > 0);
    CHECK(!stats.native && !stats.nativeBootstraps && !stats.nativeDraws && !stats.nativeEnumerations);
    std::puts("Portable font host: discovery depth, bitmap/outline System policy, encoding, reconfiguration and "
              "rendering passed.");
    return 0;
}
