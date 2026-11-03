/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "font_freetype.h"
#include "font_catalog.h"
#include "pixel_canvas.h"
#include <fstream>
#include <iterator>
#include <cstdio>
#include <cstring>
#define CHECK(value)                                                                                                   \
    do                                                                                                                 \
    {                                                                                                                  \
        if (!(value))                                                                                                  \
        {                                                                                                              \
            std::fprintf(stderr, "FreeType font check failed at line %d\n", __LINE__);                                 \
            return 1;                                                                                                  \
        }                                                                                                              \
    } while (0)
static std::vector<std::uint8_t> Read(const char *path)
{
    std::ifstream file(path, std::ios::binary);
    return std::vector<std::uint8_t>(std::istreambuf_iterator<char>(file), {});
}
int main(int argc, char **argv)
{
    using namespace KauaiFonts;
    if (argc != 3 && argc != 4)
        return 2;
    FreeTypeOptions options;
    FontCatalog catalog;
    for (int source = 1; source < argc; ++source)
    {
        FontPath path;
        for (auto ch = argv[source]; *ch; ++ch)
            path += static_cast<KauaiFiles::PathChar>(*ch);
        CHECK(catalog.AddFile(path));
        auto count = catalog.Sources().size();
        CHECK(count > 0 && catalog.AddFile(path) && catalog.Sources().size() == count);
    }
    CHECK(!catalog.Find("Missing font family", options));
    CHECK(!catalog.Open("Missing font family", options));
    CHECK(!catalog.AddFile(FontPath()));
    const auto outlines = catalog.Families();
    const auto allFamilies = catalog.Families(true);
    CHECK(!outlines.empty() && allFamilies.size() > outlines.size());
    for (const auto &family : outlines)
    {
        bool outline = false;
        for (const auto &source : catalog.Sources())
            outline |= source.info.family == family.name && source.info.bitmapPixels == 0;
        CHECK(outline);
    }
    for (const auto &family : allFamilies)
        CHECK(catalog.Find(family.name, options));
    for (const auto &source : catalog.Sources())
    {
        CHECK(!source.info.family.empty() && source.info.faceIndex >= 0);
        options.legacyCellMetrics = true;
        auto selected = catalog.Open(source.info.family, options);
        CHECK(selected);
        Metrics metrics;
        CHECK(selected->Measure({"AV fj", 5}, metrics) && metrics.cellHeight > 0 && metrics.ascent > 0);
    }
    FontPath fixtureDirectory;
    for (auto ch = argv[1]; *ch; ++ch)
        fixtureDirectory += static_cast<KauaiFiles::PathChar>(*ch);
    const auto slash = fixtureDirectory.find_last_of(
        FontPath{static_cast<KauaiFiles::PathChar>('/'), static_cast<KauaiFiles::PathChar>('\\')});
    fixtureDirectory = slash == FontPath::npos ? FontPath(1, static_cast<KauaiFiles::PathChar>('.'))
                                               : fixtureDirectory.substr(0, slash + 1);
    FontCatalog scanned;
    CHECK(scanned.AddDirectory(fixtureDirectory));
    CHECK(!scanned.Sources().empty());
    for (const auto &source : scanned.Sources())
    {
        FreeTypeOptions style;
        style.bold = source.info.bold;
        style.italic = source.info.italic;
        const auto selected = scanned.Find(source.info.family, style);
        CHECK(selected && selected->info.bold == style.bold && selected->info.italic == style.italic);
    }
    for (const auto &source : catalog.Sources())
        if (source.path.find(fixtureDirectory) == 0)
            CHECK(scanned.Find(source.info.family, options));
    if (argc == 4)
    {
        std::vector<FontInfo> collection;
        const auto bytes = Read(argv[3]);
        CHECK(InspectFreeTypeFont(bytes, collection) && collection.size() > 1);
        for (const auto &info : collection)
        {
            options = {};
            options.faceIndex = info.faceIndex;
            auto face = CreateFreeTypeFace(bytes, options);
            Metrics metrics;
            CHECK(face && face->Measure({"AV fj", 5}, metrics) && metrics.cellHeight > 0);
        }
    }
    options = {};
    CHECK(!CreateFreeTypeFace({}, options));
    CHECK(!CreateFreeTypeFace(std::vector<std::uint8_t>(128, 0), options));
    for (int source = 1; source < 3; ++source)
    {
        const auto bytes = Read(argv[source]);
        CHECK(!bytes.empty());
        for (int style = 0; style < 8; ++style)
        {
            options.bold = (style & 1) != 0;
            options.italic = (style & 2) != 0;
            options.underline = (style & 4) != 0;
            auto face = CreateFreeTypeFace(bytes, options);
            CHECK(face);
            const char narrow[] = "AV fj\xE9";
            const char16_t wide[] = u"AV fj\u00E9";
            Metrics a, b;
            Mask first, again;
            if (!face->Measure({narrow, 6}, a))
            {
                std::fprintf(stderr, "Measure failed: source=%d style=%d\n", source, style);
                return 1;
            }
            CHECK(face->Measure({wide, 6, Encoding::utf16}, b));
            CHECK(a.advance > 0 && a.advance == b.advance && a.cellHeight == b.cellHeight && a.ascent > 0);
            CHECK(face->Rasterize({narrow, 6}, first, true));
            CHECK(face->Rasterize({wide, 6, Encoding::utf16}, again, true));
            CHECK(first.width == again.width && first.height == again.height && first.left == again.left &&
                  first.top == again.top && first.coverage == again.coverage && first.background == again.background &&
                  !first.coverage.empty());
            bool ink = false;
            for (auto alpha : first.coverage)
            {
                CHECK(alpha == 0 || alpha == 255);
                ink |= alpha != 0;
            }
            CHECK(ink);
            Mask repeat;
            CHECK(face->Rasterize({narrow, 6}, repeat, true));
            CHECK(repeat.coverage == first.coverage);
            std::vector<std::uint32_t> pixels(256 * 128, 0x112233);
            KauaiPixels::Surface32 surface = {pixels.data(), 256, 128, 256 * 4};
            Placement placement;
            CHECK(Place(a, 15, 15, Horizontal::left, Vertical::top, placement));
            Image32 image;
            CHECK(face->Paint32({narrow, 6}, surface, placement, 0xFF0000, Background::opaque, 0xFFFFFF, image));
            CHECK(image.width > 0 && image.height > 0 && !image.pixels.empty());
            for (auto pixel : pixels)
                CHECK(pixel == 0x112233);
            CHECK(!face->Measure({nullptr, 1}, b));
        }
    }
    options = {};
    options.monochrome = false;
    // Non-Windows outline fonts can omit OS/2. Rename the table in a private
    // copy of the local fixture and require the host's legacy request to use
    // FreeType's normal cell metrics rather than reject the face.
    auto noWindowsTables = Read(argv[1]);
    CHECK(noWindowsTables.size() >= 12);
    const auto tables = (unsigned(noWindowsTables[4]) << 8) | noWindowsTables[5];
    CHECK(12 + std::size_t(tables) * 16 <= noWindowsTables.size());
    for (unsigned table = 0; table < tables; ++table)
    {
        auto tag = noWindowsTables.data() + 12 + table * 16;
        if (!std::memcmp(tag, "OS/2", 4))
            std::memcpy(tag, "zOS2", 4);
    }
    auto generic = CreateFreeTypeFace(noWindowsTables, options);
    options.legacyCellMetrics = true;
    auto legacy = CreateFreeTypeFace(noWindowsTables, options);
    Metrics genericMetrics, legacyMetrics;
    CHECK(generic && legacy && generic->Measure({"AV", 2}, genericMetrics) &&
          legacy->Measure({"AV", 2}, legacyMetrics));
    CHECK(genericMetrics.cellHeight == legacyMetrics.cellHeight && genericMetrics.ascent == legacyMetrics.ascent &&
          genericMetrics.externalLeading == legacyMetrics.externalLeading);
    options.legacyCellMetrics = false;
    auto smooth = CreateFreeTypeFace(Read(argv[1]), options);
    CHECK(smooth);
    Mask gray;
    CHECK(smooth->Rasterize({"AV fj", 5}, gray));
    bool fractional = false;
    for (auto alpha : gray.coverage)
        fractional |= alpha != 0 && alpha != 255;
    CHECK(fractional);
    options.useCellMetrics = true;
    options.cellMetrics = {0, 23, 18, 2};
    auto compatible = CreateFreeTypeFace(Read(argv[1]), options);
    CHECK(compatible);
    Metrics retained;
    CHECK(compatible->Measure({"AV", 2}, retained));
    CHECK(retained.cellHeight == 23 && retained.ascent == 18 && retained.externalLeading == 2);
    options = {};
    options.legacyCellMetrics = true;
    auto originalBitmap = CreateFreeTypeFace(Read(argv[2]), options);
    options.bitmapScale = 2;
    auto enlargedBitmap = CreateFreeTypeFace(Read(argv[2]), options);
    Metrics originalMetrics, enlargedMetrics;
    Mask originalMask, enlargedMask;
    CHECK(originalBitmap && enlargedBitmap);
    CHECK(originalBitmap->Measure({"AV", 2}, originalMetrics) && enlargedBitmap->Measure({"AV", 2}, enlargedMetrics));
    CHECK(enlargedMetrics.advance == originalMetrics.advance * 2 &&
          enlargedMetrics.cellHeight == originalMetrics.cellHeight * 2 &&
          enlargedMetrics.ascent == originalMetrics.ascent * 2);
    CHECK(originalBitmap->Rasterize({"AV", 2}, originalMask, true) &&
          enlargedBitmap->Rasterize({"AV", 2}, enlargedMask, true));
    CHECK(enlargedMask.width == originalMask.width * 2 && enlargedMask.height == originalMask.height * 2 &&
          enlargedMask.left == originalMask.left * 2 && enlargedMask.top == originalMask.top * 2);
    for (int y = 0; y < enlargedMask.height; ++y)
        for (int x = 0; x < enlargedMask.width; ++x)
            CHECK(enlargedMask.coverage[std::size_t(y) * enlargedMask.pitch + x] ==
                  originalMask.coverage[std::size_t(y / 2) * originalMask.pitch + x / 2]);
    std::puts("FreeType: TrueType/WinFNT, encoding, styles, coverage, cell metrics, repeat rendering and color "
              "painting passed.");
    return 0;
}
