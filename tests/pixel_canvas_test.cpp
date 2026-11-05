/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "pixel_canvas.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include "kauai_core.h"
#include "region.h"
#undef size
ASSERTNAME
void WarnProc(schar *, LegacyLong, schar *)
{
}
bool FAssertProc(schar *, LegacyLong, schar *, void *, LegacyLong)
{
    std::abort();
}
#define CHECK(value)                                                                                                   \
    do                                                                                                                 \
    {                                                                                                                  \
        if (!(value))                                                                                                  \
        {                                                                                                              \
            std::fprintf(stderr, "Pixel check: %d\n", __LINE__);                                                       \
            return 1;                                                                                                  \
        }                                                                                                              \
    } while (0)
static int PackedCopies()
{
    using namespace KauaiPixels;
    RC row(0, 0, 4, 1);
    Color palette[256] = {};
    palette[1] = {0, 0, 255, 0};
    palette[2] = {0, 255, 0, 0};
    palette[3] = {255, 0, 0, 0};
    palette[4] = {255, 255, 255, 0};
    std::uint8_t indices[] = {0x12, 0x34, 0xA5, 0xA5};
    PackedSurface indexed{indices, 4, 1, 4, 4, palette, 16};
    std::uint8_t words[12], triples[16], mono[4], bytes[8];
    std::memset(words, 0xA5, sizeof(words));
    std::memset(triples, 0xA5, sizeof(triples));
    std::memset(mono, 0xA5, sizeof(mono));
    std::memset(bytes, 0xA5, sizeof(bytes));
    std::uint32_t rgb[4] = {};
    PackedSurface sixteen{words, 4, 1, 12, 16}, twentyFour{triples, 4, 1, 16, 24};
    PackedSurface thirtyTwo{rgb, 4, 1, 16, 32}, one{mono, 4, 1, 4, 1};
    PackedSurface eight{bytes, 4, 1, 8, 8, palette, 256};
    CHECK(CopyPacked(sixteen, indexed, row, row));
    const std::uint8_t expectedWords[] = {0, 0x7C, 0xE0, 3, 0x1F, 0, 0xFF, 0x7F, 0xA5, 0xA5, 0xA5, 0xA5};
    CHECK(std::memcmp(words, expectedWords, sizeof(words)) == 0);
    CHECK(CopyPacked(twentyFour, sixteen, row, row));
    const std::uint8_t expectedTriples[] = {0, 0, 255, 0, 255, 0, 255, 0, 0, 255, 255, 255, 0xA5, 0xA5, 0xA5, 0xA5};
    CHECK(std::memcmp(triples, expectedTriples, sizeof(triples)) == 0);
    CHECK(CopyPacked(thirtyTwo, twentyFour, row, row));
    CHECK(rgb[0] == 0xFF0000 && rgb[1] == 0x00FF00 && rgb[2] == 0x0000FF && rgb[3] == 0xFFFFFF);
    CHECK(CopyPacked(one, thirtyTwo, row, row));
    CHECK(mono[0] == 0x15 && mono[1] == 0xA5 && mono[3] == 0xA5);
    // Unsupported host palette policy rejects transactionally. A caller may
    // explicitly request ordinary nearest-color stretch conversion instead.
    CHECK(!CopyPacked(eight, thirtyTwo, row, row));
    for (auto value : bytes)
        CHECK(value == 0xA5);
    PackedCopyOptions stretch;
    stretch.nearSizeCopy = false;
    CHECK(CopyPacked(eight, thirtyTwo, row, row, nullptr, nullptr, stretch));
    const std::uint8_t expectedBytes[] = {1, 2, 3, 4, 0xA5, 0xA5, 0xA5, 0xA5};
    CHECK(std::memcmp(bytes, expectedBytes, sizeof(bytes)) == 0);
    // Transition snapshots recover exact palette colors from the SDL display;
    // general near-size quantization above remains deliberately unsupported.
    std::uint8_t snapshot[8];
    std::memset(snapshot, 0xA5, sizeof(snapshot));
    CHECK(SnapshotIndexed({snapshot, 4, 1, 8}, {rgb, 4, 1, 16}, row, row, palette, 256));
    CHECK(std::memcmp(snapshot, expectedBytes, sizeof(snapshot)) == 0);
    rgb[3] = 0x123456;
    std::memset(snapshot, 0xA5, sizeof(snapshot));
    CHECK(!SnapshotIndexed({snapshot, 4, 1, 8}, {rgb, 4, 1, 16}, row, row, palette, 256));
    for (auto value : snapshot)
        CHECK(value == 0xA5);
    rgb[3] = 0xFFFFFF;
    std::uint8_t overlapping[] = {0x12, 0x34, 0x56, 0xA5};
    PackedSurface self{overlapping, 6, 1, 4, 4, palette, 16};
    CHECK(CopyPacked(self, self, row, RC(1, 0, 5, 1)));
    CHECK(overlapping[0] == 0x11 && overlapping[1] == 0x23 && overlapping[2] == 0x46 && overlapping[3] == 0xA5);
    Color reversed[] = {{255, 255, 255, 0}, {0, 0, 0, 0}};
    PackedSurface reverseMono{mono, 4, 1, 4, 1, reversed, 2};
    mono[0] = 0xA5;
    CHECK(CopyPacked(thirtyTwo, reverseMono, row, row));
    CHECK(rgb[0] == 0 && rgb[1] == 0xFFFFFF && rgb[2] == 0 && rgb[3] == 0xFFFFFF);
    std::uint8_t sourceBits[] = {0xA0}, destinationBits[] = {0xA5};
    PackedSurface shortBits{sourceBits, 3, 1, 1, 1}, longBits{destinationBits, 6, 1, 1, 1};
    RC clip(1, 0, 5, 1);
    CHECK(CopyPacked(longBits, shortBits, RC(0, 0, 3, 1), RC(0, 0, 6, 1), &clip));
    CHECK(destinationBits[0] == 0xCD);
    // A region hole leaves the original destination pixel intact.
    auto region = Region::PregnNew(&row);
    RC hole(2, 0, 3, 1);
    CHECK(region && region->FDiffRc(&hole));
    for (auto &pixel : rgb)
        pixel = 0xFF00FF;
    RC colorClip(1, 0, 4, 1);
    CHECK(CopyPacked(thirtyTwo, indexed, row, row, &colorClip, region));
    CHECK(rgb[0] == 0xFF00FF && rgb[1] == 0x00FF00 && rgb[2] == 0xFF00FF && rgb[3] == 0xFFFFFF);
    ReleasePpo(&region);
    words[1] = 0xFC; // RGB555's unused high bit survives a raw same-format copy.
    std::uint8_t duplicate[12] = {};
    PackedSurface duplicateSurface{duplicate, 4, 1, 12, 16};
    CHECK(CopyPacked(duplicateSurface, sixteen, row, row) && duplicate[1] == 0xFC);
    CHECK(CopyPacked(thirtyTwo, sixteen, row, row) && rgb[0] == 0xFF0000);
    auto invalid = indexed;
    invalid.pitch = 1;
    CHECK(!CopyPacked(thirtyTwo, invalid, row, row) && rgb[0] == 0xFF0000);
    CHECK(!CopyPacked(thirtyTwo, indexed, RC(4, 0, 0, 1), row) && rgb[0] == 0xFF0000);
    return 0;
}
static int PackedShapes()
{
    using namespace KauaiPixels;
    RC bounds(0, 0, 5, 3), rectangle(1, 0, 5, 3), clip(0, 1, 4, 3), hole(2, 1, 3, 2);
    auto region = Region::PregnNew(&clip);
    CHECK(region && region->FDiffRc(&hole));
    for (int depth : {16, 24})
    {
        const int pitch = depth == 16 ? 12 : 16, stride = depth / 8;
        std::vector<std::uint8_t> bytes(pitch * 3, 0xA5);
        PackedSurface surface{bytes.data(), 5, 3, pitch, depth};
        Brush brush;
        brush.foreground.rgb = 0x0D62C9;
        CHECK(FillRect(surface, rectangle, brush, &clip, region));
        for (int y = 0; y < 3; ++y)
            for (int x = 0; x < pitch; ++x)
            {
                const int pixel = x / stride, component = x % stride;
                const bool painted = x < 5 * stride && y >= 1 && pixel >= 1 && pixel < 4 && !(y == 1 && pixel == 2);
                const std::uint8_t color16[] = {0x99, 0x05}, color24[] = {0xC9, 0x62, 0x0D};
                CHECK(bytes[y * pitch + x] ==
                      (painted ? (depth == 16 ? color16[component] : color24[component]) : 0xA5));
            }
        const auto painted = bytes;
        CHECK(InvertRect(surface, bounds));
        for (int y = 0; y < 3; ++y)
            for (int x = 0; x < pitch; ++x)
                CHECK(bytes[y * pitch + x] == (x < 5 * stride ? std::uint8_t(painted[y * pitch + x] ^ 255) : 0xA5));
        const auto beforeFailure = bytes;
        auto unsupported = surface;
        unsupported.depth = 8;
        Point triangle[] = {{0, 0}, {5, 0}, {0, 3}};
        CHECK(!FillRect(unsupported, bounds, brush) && !FillOval(unsupported, bounds, brush));
        CHECK(!FillPolygon(unsupported, triangle, 3, brush) && !InvertRect(unsupported, bounds));
        CHECK(!FillOval(surface, RC(0, 0, INT32_MAX, 3), brush) && bytes == beforeFailure);
    }
    ReleasePpo(&region);
    return 0;
}

static int PackedScrolling()
{
    using namespace KauaiPixels;
    RC area(0, 0, 5, 4), clip(1, 1, 5, 4), hole(2, 2, 4, 3);
    auto region = Region::PregnNew(&clip);
    CHECK(region && region->FDiffRc(&hole));
    Point deltas[] = {{-2, -1}, {1, 1}, {0, -1}, {1, 0}, {INT32_MIN, INT32_MAX}, {0, 0}};
    for (int depth : {1, 4, 8, 16, 24, 32})
    {
        const int pitch = ((5 * depth + 31) / 32) * 4 + 4;
        std::vector<byte> initial(pitch * 6);
        for (unsigned i = 0; i < initial.size(); ++i)
            initial[i] = static_cast<byte>(i * 37 + 19);
        for (int clipping = 0; clipping < 3; ++clipping)
            for (const auto &delta : deltas)
            {
                auto bytes = initial, expected = initial;
                PackedSurface surface{bytes.data() + pitch, 5, 4, pitch, depth};
                CHECK(Scroll(surface, area, delta.x, delta.y, clipping < 2 ? &clip : nullptr,
                             clipping == 0 ? region : nullptr));
                const auto visible = [&](std::int64_t x, std::int64_t y) {
                    return x >= 0 && x < 5 && y >= 0 && y < 4 && (clipping == 2 || (x >= 1 && y >= 1)) &&
                           (clipping != 0 || !(x >= 2 && x < 4 && y == 2));
                };
                // Independent bit-by-bit oracle, including sub-byte samples.
                // Guard rows, row padding and unused final bits stay unchanged.
                for (int y = 0; y < 4; ++y)
                    for (int x = 0; x < 5; ++x)
                    {
                        const auto sx = std::int64_t(x) - delta.x, sy = std::int64_t(y) - delta.y;
                        if (!visible(x, y) || !visible(sx, sy))
                            continue;
                        for (int bit = 0; bit < depth; ++bit)
                        {
                            const auto target = (y + 1) * pitch + (x * depth + bit) / 8;
                            const auto source = (sy + 1) * pitch + (sx * depth + bit) / 8;
                            const auto mask = 1u << (7 - (x * depth + bit) % 8);
                            const auto value =
                                (initial[static_cast<std::size_t>(source)] >> (7 - (sx * depth + bit) % 8)) & 1;
                            expected[target] = static_cast<byte>((expected[target] & ~mask) | (value ? mask : 0));
                        }
                    }
                CHECK(bytes == expected);
                const auto beforeInvalid = bytes;
                CHECK(!Scroll(surface, RC(4, 3, 1, 0), 1, 1) && bytes == beforeInvalid);
                CHECK(Scroll(surface, RC(1, 0, 1, 4), 1, 1) && bytes == beforeInvalid);
                auto invalid = surface;
                invalid.pitch = 0;
                CHECK(!Scroll(invalid, area, 1, 1) && bytes == beforeInvalid);
                invalid = surface;
                invalid.depth = 2;
                CHECK(!Scroll(invalid, area, 1, 1) && bytes == beforeInvalid);
            }
    }
    ReleasePpo(&region);
    return 0;
}

static int LegacyScrollFixtures()
{
    using namespace KauaiPixels;
    // Captured from direct Windows ScrollDC calls on 37x29 top-down DIBs,
    // with byte i initialized to i*37+19. These run without Windows on POSIX.
    struct Fixture
    {
        int depth, dx, dy;
        RC rectangle;
        bool clipped;
        std::uint32_t digest;
    };
    Fixture fixtures[] = {
        {1, -8, -5, RC(4, 3, 33, 26), false, 0x8DC3AC38},   {1, -1, -5, RC(4, 3, 33, 26), false, 0x3DF6C439},
        {1, -8, -5, RC(0, 0, 37, 29), true, 0xAFE0E6EE},    {1, 1, 5, RC(0, 0, 37, 29), true, 0x25BC286E},
        {4, -8, -5, RC(25, 17, 44, 38), false, 0x2DAE5111}, {4, -1, -5, RC(25, 17, 44, 38), false, 0x8B0CDEEF},
        {4, -8, 5, RC(0, 0, 37, 29), true, 0xF4756CF1},     {4, 1, -5, RC(0, 0, 37, 29), true, 0x5BE5FF6B}};
    RC clip(2, 1, 35, 28), hole(13, 9, 24, 19);
    auto region = Region::PregnNew(&clip);
    CHECK(region && region->FDiffRc(&hole));
    for (const auto &fixture : fixtures)
    {
        const int pitch = ((37 * fixture.depth + 31) / 32) * 4;
        std::vector<byte> bytes(pitch * 29);
        for (unsigned i = 0; i < bytes.size(); ++i)
            bytes[i] = static_cast<byte>(i * 37 + 19);
        PackedSurface surface{bytes.data(), 37, 29, pitch, fixture.depth};
        CHECK(Scroll(surface, fixture.rectangle, fixture.dx, fixture.dy, nullptr, fixture.clipped ? region : nullptr,
                     {true}));
        std::uint32_t digest = 2166136261u;
        for (auto byte : bytes)
            digest = (digest ^ byte) * 16777619u;
        if (digest != fixture.digest)
            std::fprintf(stderr, "Legacy scroll fixture depth %d delta %d,%d expected %08x actual %08x\n",
                         fixture.depth, fixture.dx, fixture.dy, fixture.digest, digest);
        CHECK(digest == fixture.digest);
    }
    ReleasePpo(&region);
    return 0;
}

static int SubByteShapes()
{
    using namespace KauaiPixels;
    Color palette[16];
    for (int i=0; i<16; ++i) palette[i] = {byte(i*17),byte(i*17),byte(i*17),255};
    std::vector<byte> bytes(4*5,0xA5);
    PackedSurface surface{bytes.data()+4,5,3,4,4,palette,16};
    RC bounds(0,0,5,3), clip(1,0,5,3), hole(2,1,3,2);
    auto region = Region::PregnNew(&clip);
    CHECK(region && region->FDiffRc(&hole));
    Brush brush; brush.foreground.rgb=0x111111;
    CHECK(FillRect(surface,bounds,brush,&clip,region));
    const byte expected[] = {0xA5,0xA5,0xA5,0xA5,
                            0xA1,0x11,0x15,0xA5, 0xA1,0xA1,0x15,0xA5, 0xA1,0x11,0x15,0xA5,
                            0xA5,0xA5,0xA5,0xA5};
    CHECK(!std::memcmp(bytes.data(),expected,sizeof(expected)));
    auto before = bytes;
    brush.foreground.mode=InkMode::invert;
    CHECK(FillRect(surface,bounds,brush) && FillRect(surface,bounds,brush) && bytes==before);
    // Reversed private palette: a white brush has index zero, so ordinary
    // brush inversion changes no stored bits. Highlighting still toggles them.
    for (int i=0; i<16; ++i) palette[i] = {byte(255-i*17),byte(255-i*17),byte(255-i*17),0};
    CHECK(FillRect(surface,bounds,brush) && bytes==before);
    CHECK(InvertRect(surface,bounds));
    for (int y=0; y<3; ++y)
    {
        CHECK(bytes[4+y*4]==byte(before[4+y*4]^255));
        CHECK(bytes[5 + y * 4] == byte(before[5 + y * 4] ^ 255));
        CHECK(bytes[6 + y * 4] == byte(before[6 + y * 4] ^ 0xF0));
        CHECK(bytes[7 + y * 4] == 0xA5);
    }
    CHECK(InvertRect(surface, bounds) && bytes == before);
    auto invalid = surface;
    invalid.palette = nullptr;
    CHECK(!FillRect(invalid, bounds, brush) && !FillOval(invalid, bounds, brush) && bytes == before);
    std::vector<byte> monoBytes(16, 0xA5);
    PackedSurface mono{monoBytes.data() + 4, 9, 2, 4, 1};
    CHECK(InvertRect(mono, RC(1, 0, 9, 2)));
    for (int y = 0; y < 2; ++y)
        CHECK(monoBytes[4 + y * 4] == 0xDA && monoBytes[5 + y * 4] == 0x25 && monoBytes[6 + y * 4] == 0xA5 &&
              monoBytes[7 + y * 4] == 0xA5);
    CHECK(monoBytes[0] == 0xA5 && monoBytes[3] == 0xA5 && monoBytes[12] == 0xA5 && monoBytes[15] == 0xA5);
    const auto monoBefore = monoBytes;
    brush.foreground = {0x0D62C9,InkMode::color};
    brush.background = {0xD22A08, InkMode::color};
    for (auto &row : brush.pattern)
        row = 0xA5;
    CHECK(!FillRect(mono, RC(0, 0, 9, 2), brush) && monoBytes == monoBefore);
    ReleasePpo(&region);
    return 0;
}

static int MonochromeSolids()
{
    using namespace KauaiPixels;
    // Native solid-brush fixtures captured with direct RGB and palette-relative
    // color references. Equal RGB values with different origins stay distinct.
    struct Fixture
    {
        std::uint32_t background;
        std::uint8_t foregroundRef, backgroundRef, index;
    };
    const Fixture fixtures[] = {{0x0D62C9, 0, 0, 0}, {0x0D62C9, 2, 0, 1}, {0x0D62C9, 0, 2, 1},
                                {0x0D62C9, 2, 2, 0}, {0xFFFFFF, 0, 0, 0}, {0xFFFFFF, 2, 2, 0},
                                {0, 0, 0, 1},        {0, 2, 2, 1},        {0xD22A08, 2, 2, 1}};
    byte pixels[8];
    PackedSurface mono{pixels, 9, 2, 4, 1};
    Brush brush;
    brush.foreground.rgb = 0x0D62C9;
    for (const auto &fixture : fixtures)
    {
        std::memset(pixels, 0xA5, sizeof(pixels));
        MonochromeBrushState state;
        state.backgroundRgb = fixture.background;
        state.foregroundReference = fixture.foregroundRef;
        state.backgroundReference = fixture.backgroundRef;
        CHECK(FillRect(mono, RC(0, 0, 9, 2), brush, nullptr, nullptr, -1, -1, state));
        for (int y = 0; y < 2; ++y)
            CHECK(pixels[y * 4] == (fixture.index ? 255 : 0) && pixels[y * 4 + 1] == (fixture.index ? 0xA5 : 0x25) &&
                  pixels[y * 4 + 2] == 0xA5 && pixels[y * 4 + 3] == 0xA5);
    }
    // A mixed-row brush may collapse only when its source color identities
    // agree. Other patterned realization is left to the existing host fallback.
    Brush same;
    same.foreground.rgb = same.background.rgb = 0x0D62C9;
    for (auto &row : same.pattern)
        row = 0xA5;
    MonochromeBrushState sameState;
    sameState.backgroundRgb = 0x0D62C9;
    sameState.backgroundReference = sameState.foregroundReference = sameState.alternateReference = 2;
    CHECK(FillRect(mono, RC(0, 0, 9, 2), same, nullptr, nullptr, -1, -1, sameState) && pixels[0] == 0);
    byte before[8];
    std::memcpy(before, pixels, sizeof(before));
    sameState.alternateReference = 0;
    CHECK(!FillRect(mono, RC(0, 0, 9, 2), same, nullptr, nullptr, -1, -1, sameState) &&
          !std::memcmp(before, pixels, sizeof(before)));
    sameState.alternateReference = 2;
    sameState.collapseEqualColors = false;
    CHECK(!FillRect(mono, RC(0, 0, 9, 2), same, nullptr, nullptr, -1, -1, sameState) &&
          !std::memcmp(before, pixels, sizeof(before)));
    // Canonical palette colors retain their index, independent of background.
    brush.foreground.rgb = 0xFFFFFF;
    MonochromeBrushState state;
    state.backgroundRgb = 0;
    CHECK(FillRect(mono, RC(0, 0, 9, 2), brush, nullptr, nullptr, -1, -1, state) && pixels[0] == 255);
    brush.foreground.rgb = 0;
    CHECK(FillRect(mono, RC(0, 0, 9, 2), brush, nullptr, nullptr, -1, -1, state) && pixels[0] == 0);
    // A constant background pattern chooses the alternate color's origin.
    for (auto &row : brush.pattern)
        row = 0;
    brush.background.rgb = 0x0D62C9;
    state.backgroundRgb = 0x0D62C9;
    state.backgroundReference = 2;
    state.alternateReference = 0;
    CHECK(FillRect(mono, RC(0, 0, 9, 2), brush, nullptr, nullptr, -1, -1, state) && pixels[0] == 255);
    return 0;
}

static int PackedCoverage()
{
    using namespace KauaiPixels;
    constexpr int width = 13, height = 7;
    const std::uint8_t maskBytes[] = {0, 127, 128, 255, 1, 254, 255, 0, 127, 128, 255, 0};
    Coverage8 mask{maskBytes, 4, 3, 4};
    RC clip(1, 1, 12, 6), hole(4, 2, 6, 4);
    auto region = Region::PregnNew(&clip);
    CHECK(region && region->FDiffRc(&hole));
    for (int depth : {1, 4, 8, 16, 24, 32})
    {
        const int pitch = ((width * depth + 31) / 32) * 4;
        std::vector<std::uint8_t> pixels(pitch * (height + 2)), expected;
        for (int x : {-2, 2, 10})
            for (int y : {-1, 2, 5})
            {
                for (std::size_t i = 0; i < pixels.size(); ++i)
                    pixels[i] = static_cast<std::uint8_t>(i * 37 + 19);
                expected = pixels;
                constexpr std::uint32_t raw = 0xA5B6C7D9;
                // Independent bit-by-bit packing oracle, including guard rows.
                for (int row = 0; row < 3; ++row)
                    for (int column = 0; column < 4; ++column)
                    {
                        const int px = x + column, py = y + row;
                        if (maskBytes[row * 4 + column] < 128 || px < 1 || px >= 12 || py < 1 || py >= 6 ||
                            (px >= 4 && px < 6 && py >= 2 && py < 4))
                            continue;
                        for (int bit = 0; bit < depth; ++bit)
                        {
                            const int position = px * depth + bit;
                            const int shift = depth < 8 ? 7 - position % 8 : position % 8;
                            const int sourceBit = depth < 8 ? depth - 1 - bit : bit;
                            auto &byte = expected[(py + 1) * pitch + position / 8];
                            byte =
                                static_cast<std::uint8_t>((byte & ~(1 << shift)) | (((raw >> sourceBit) & 1) << shift));
                        }
                    }
                PackedSurface surface{pixels.data() + pitch, width, height, pitch, depth};
                CHECK(DrawCoverage(surface, mask, x, y, raw, &clip, region));
                CHECK(pixels == expected);
                const auto before = pixels;
                Coverage8 invalid{maskBytes, 4, 3, 3};
                CHECK(!DrawCoverage(surface, invalid, x, y, raw));
                CHECK(pixels == before);
            }
    }
    ReleasePpo(&region);
    return 0;
}
static int PackedMaskedTransfers()
{
    using namespace KauaiPixels;
    const std::uint8_t bytes[] = {1, 2, 1, 2};
    const std::int16_t rows[] = {4, 0}, malformed[] = {4, 1};
    RleBitmap8 source{bytes, sizeof(bytes), rows, 4, 2, false, 0};
    const Color colors[] = {{0, 0, 0, 0}, {0, 0, 255, 0}, {255, 255, 255, 0}};
    for (int mode : {0, 1, 2})
    {
        std::uint8_t pixels[8], expected[8];
        std::memset(pixels, 255, sizeof(pixels));
        std::memset(expected, 255, sizeof(expected));
        expected[0] = mode == 0 ? 0xBF : mode == 1 ? 0x7F : 0xFF;
        PackedSurface target{pixels, 5, 2, 4, 1};
        CHECK(DrawRle(target, source, mode == 0 ? RC(0, 0, 4, 2) : RC(0, 0, 2, 1), colors, 3, nullptr, nullptr,
                      mode == 1));
        CHECK(!std::memcmp(pixels, expected, sizeof(pixels)));
        auto invalid = source;
        invalid.rowBytes = malformed;
        CHECK(!DrawRle(target, invalid, RC(0, 0, 4, 2), colors, 3));
        CHECK(!std::memcmp(pixels, expected, sizeof(pixels)));
    }
    for (int mode : {0, 1, 2})
    {
        std::uint8_t pixels[8];
        std::memset(pixels, 255, sizeof(pixels));
        std::uint8_t expected[] = {0x21, 0x22, 0xFF, 0xFF, 0x22, 0x22, 0xFF, 0xFF};
        if (mode)
        {
            std::memset(expected, 255, sizeof(expected));
            expected[0] = mode == 1 ? 0x02 : 0x22;
        }
        PackedSurface target{pixels, 5, 2, 4, 4, colors, 3};
        CHECK(DrawRle(target, source, mode == 0 ? RC(0, 0, 4, 2) : RC(0, 0, 2, 1), colors, 3, nullptr, nullptr,
                      mode == 1));
        CHECK(!std::memcmp(pixels, expected, sizeof(pixels)));
        target.palette = nullptr;
        CHECK(!DrawRle(target, source, RC(0, 0, 4, 2), colors, 3));
        CHECK(!std::memcmp(pixels, expected, sizeof(pixels)));
    }
    for (int depth : {16, 24})
        for (int mode : {0, 1, 2})
        {
            std::uint8_t pixels[32], expected[32];
            std::memset(pixels, 255, sizeof(pixels));
            std::memset(expected, 255, sizeof(expected));
            PackedSurface target{pixels, 4, 2, 16, depth};
            const auto set = [&](int x, int y, std::uint32_t raw) {
                for (int b = 0; b < depth / 8; ++b)
                    expected[y * 16 + x * (depth / 8) + b] = static_cast<std::uint8_t>(raw >> (8 * b));
            };
            const auto white = depth == 16 ? 0x7FFFu : 0xFFFFFFu;
            if (mode == 0)
            {
                for (int y = 0; y < 2; ++y)
                    for (int x = 0; x < 4; ++x)
                        set(x, y, y == 0 && x == 1 ? (depth == 16 ? 0x7C00u : 0xFF0000u) : white);
            }
            else
            {
                set(0, 0, mode == 1 ? 0 : white);
                set(1, 0, white);
            }
            CHECK(DrawRle(target, source, mode == 0 ? RC(0, 0, 4, 2) : RC(0, 0, 2, 1), colors, 3, nullptr, nullptr,
                          mode == 1));
            CHECK(!std::memcmp(pixels, expected, sizeof(pixels)));
            auto invalid = source;
            invalid.rowBytes = malformed;
            CHECK(!DrawRle(target, invalid, RC(0, 0, 4, 2), colors, 3));
            CHECK(!std::memcmp(pixels, expected, sizeof(pixels)));
        }
    return 0;
}
int main()
{
    CHECK(PackedMaskedTransfers() == 0);
    if (PackedCoverage()) return 1;
    using namespace KauaiPixels;
    CHECK(PackedCopies() == 0);
    CHECK(PackedShapes() == 0);
    CHECK(PackedScrolling() == 0);
    CHECK(LegacyScrollFixtures() == 0);
    CHECK(SubByteShapes() == 0);
    CHECK(MonochromeSolids() == 0);
    // Padding and guard rows must survive all drawing, including inverted and
    // transparent pattern cells and a clip region with a hole.
    std::vector<std::uint32_t> pixels(16 * 13, 0x123456);
    Surface32 surface = {pixels.data() + 16, 13, 11, 16 * 4};
    RC rectangle(-3, -2, 12, 12), clip(1, 2, 11, 10), hole(4, 4, 7, 7);
    auto region = Region::PregnNew(&clip);
    CHECK(region && region->FDiffRc(&hole));
    Brush brush;
    brush.foreground = {0, InkMode::invert};
    brush.background = {0, InkMode::clear};
    for (auto &row : brush.pattern)
        row = 0xA6;
    FillRect(surface, rectangle, brush, &clip, region, 5, 4);
    for (int y = -1; y <= 11; ++y)
        for (int x = 0; x < 16; ++x)
        {
            const bool inside = x >= 1 && x < 11 && y >= 2 && y < 10 && !(x >= 4 && x < 7 && y >= 4 && y < 7) &&
                                !(x >= 2 && x < 7 && y >= 2 && y < 8);
            const auto expected = inside && (0xA6 & (0x80 >> (x & 7))) ? 0xEDCBA9u : 0x123456u;
            CHECK(pixels[(y + 1) * 16 + x] == expected);
        }
    {
        std::vector<std::uint32_t> ovalPixels(16 * 13, 0x123456);
        Surface32 ovalSurface = {ovalPixels.data() + 16, 13, 11, 16 * 4};
        Brush ovalBrush;
        ovalBrush.foreground.rgb = 0x112233;
        RC oval(2, 3, 7, 8);
        CHECK(FillOval(ovalSurface, oval, ovalBrush, &clip, region));
        const char *expected[] = {"..#..", ".###.", "#####", ".###.", "..#.."};
        for (int y = -1; y <= 11; ++y)
            for (int x = 0; x < 16; ++x)
            {
                const bool inOval = x >= 2 && x < 7 && y >= 3 && y < 8 && expected[y - 3][x - 2] == '#';
                const bool inHole = x >= 4 && x < 7 && y >= 4 && y < 7;
                CHECK(ovalPixels[(y + 1) * 16 + x] == (inOval && !inHole ? 0x112233u : 0x123456u));
            }
        const auto saved = ovalPixels;
        RC tooWide(INT32_MIN, 0, INT32_MAX, 10);
        CHECK(!FillOval(ovalSurface, tooWide, ovalBrush) && ovalPixels == saved);
        RC empty(1, 1, 1, 8);
        CHECK(FillOval(ovalSurface, empty, ovalBrush) && ovalPixels == saved);
    }
    ReleasePpo(&region);
    // Clipping a stretch must preserve the sampling origin. These are known
    // 2x2 source cells scaled to 4x4, with the left/top cells clipped away.
    std::uint32_t source[] = {1, 2, 3, 4};
    Surface32 input = {source, 2, 2, 8};
    RC src(0, 0, 2, 2), dst(0, 0, 4, 4), smallClip(1, 1, 4, 4);
    CHECK(Copy(surface, input, src, dst, &smallClip));
    CHECK(pixels[2 * 16 + 1] == 1 && pixels[2 * 16 + 2] == 2 && pixels[3 * 16 + 1] == 3 && pixels[3 * 16 + 2] == 4);
    // Self-copy must read a snapshot instead of feeding overwritten pixels
    // back into later rows or columns.
    std::uint32_t overlap[] = {10, 20, 30, 40, 50, 60};
    Surface32 self = {overlap, 3, 2, 12};
    RC first(0, 0, 2, 2), shifted(1, 0, 3, 2);
    CHECK(Copy(self, self, first, shifted));
    CHECK(overlap[0] == 10 && overlap[1] == 10 && overlap[2] == 20 && overlap[3] == 40 && overlap[4] == 40 &&
          overlap[5] == 50);
    self.pitch = 1;
    CHECK(!Copy(self, input, src, dst));
    // Scrolling clips both source and destination. It preserves exposed cells,
    // holes, padding and guard rows while overlapping moves read a snapshot.
    RC scrollRect(-2, 0, 6, 5), scrollClip(1, 1, 5, 4), scrollHole(2, 2, 4, 3);
    auto scrollRegion = Region::PregnNew(&scrollClip);
    CHECK(scrollRegion && scrollRegion->FDiffRc(&scrollHole));
    struct Delta
    {
        std::int32_t x, y;
    } deltas[] = {{-2, -1}, {1, 1}, {0, -1}, {1, 0}, {INT32_MIN, INT32_MAX}, {0, 0}};
    std::vector<byte> scrolling8(8 * 6);
    std::vector<std::uint32_t> scrolling32(8 * 6);
    for (std::size_t i = 0; i < scrolling8.size(); ++i)
    {
        scrolling8[i] = static_cast<byte>(i * 7);
        scrolling32[i] = 0xA5123400 + static_cast<std::uint32_t>(i * 7);
    }
    const auto original8 = scrolling8;
    const auto original32 = scrolling32;
    Surface8 scroll8 = {scrolling8.data() + 8, 5, 4, 8};
    Surface32 scroll32 = {scrolling32.data() + 8, 5, 4, 8 * 4};
    for (int clipping = 0; clipping < 3; ++clipping)
        for (auto delta : deltas)
        {
            scrolling8 = original8;
            scrolling32 = original32;
            scroll8.pixels = scrolling8.data() + 8;
            scroll32.pixels = scrolling32.data() + 8;
            const auto clipPointer = clipping < 2 ? &scrollClip : nullptr;
            const auto regionPointer = clipping == 0 ? scrollRegion : nullptr;
            CHECK(Scroll(scroll8, scrollRect, delta.x, delta.y, clipPointer, regionPointer));
            CHECK(Scroll(scroll32, scrollRect, delta.x, delta.y, clipPointer, regionPointer));
            const auto visible = [&](std::int64_t x, std::int64_t y) {
                return x >= 0 && x < 5 && y >= 0 && y < 4 && (clipping == 2 || (x >= 1 && y >= 1)) &&
                       (clipping != 0 || !(x >= 2 && x < 4 && y == 2));
            };
            for (int y = -1; y <= 4; ++y)
                for (int x = 0; x < 8; ++x)
                {
                    const auto sx = std::int64_t(x) - delta.x, sy = std::int64_t(y) - delta.y;
                    const auto index = (y + 1) * 8 + x;
                    const auto sample =
                        visible(x, y) && visible(sx, sy) ? std::size_t((sy + 1) * 8 + sx) : std::size_t(index);
                    CHECK(scrolling8[index] == original8[sample]);
                    CHECK(scrolling32[index] == original32[sample]);
                }
        }
    RC invertedScroll(4, 3, 1, 0), emptyScroll(1, 0, 1, 4);
    const auto beforeInvalid = scrolling8;
    CHECK(!Scroll(scroll8, invertedScroll, 1, 1));
    CHECK(Scroll(scroll8, emptyScroll, 1, 1) && scrolling8 == beforeInvalid);
    auto badScroll = scroll8;
    badScroll.pitch = 1;
    CHECK(!Scroll(badScroll, scrollRect, 1, 1) && scrolling8 == beforeInvalid);
    ReleasePpo(&scrollRegion);
    byte indices[] = {0, 1, 200, 77, 1, 0, 1, 77};
    Color palette[] = {{3, 2, 1, 255}, {9, 8, 7, 255}};
    Indexed8 indexed = {indices, 3, 2, 4, palette, 2};
    std::uint32_t converted[6] = {};
    Surface32 rgb = {converted, 3, 2, 12};
    RC indexedBounds(0, 0, 3, 2);
    CHECK(CopyIndexed(rgb, indexed, indexedBounds, indexedBounds));
    CHECK(converted[0] == 0x010203 && converted[1] == 0x070809 && converted[2] == 0 && converted[3] == 0x070809 &&
          converted[4] == 0x010203 && converted[5] == 0x070809);
    palette[0].bRed = 17;
    CHECK(CopyIndexed(rgb, indexed, indexedBounds, indexedBounds));
    CHECK(converted[0] == 0x110203 && converted[4] == 0x110203);
    // Indexed copies preserve duplicate-color indices for identical palettes,
    // translate differing palettes, and treat absent source entries as black.
    Color gray[256], reverseGray[256], duplicateGray[256];
    for (int i = 0; i < 256; ++i)
    {
        gray[i] = {static_cast<byte>(i), static_cast<byte>(i), static_cast<byte>(i), 255};
        reverseGray[i] = {static_cast<byte>(255 - i), static_cast<byte>(255 - i), static_cast<byte>(255 - i), 0};
        duplicateGray[i] = gray[i];
        duplicateGray[i].bZero = 0;
    }
    gray[17] = gray[200] = {255, 255, 255, 255};
    duplicateGray[17] = duplicateGray[200] = {255, 255, 255, 0};
    byte copySource[] = {17, 200, 4, 8, 255, 77, 77, 77, 2, 3, 6, 16, 32, 77, 77, 77, 64, 128, 0, 17, 200, 77, 77, 77};
    byte copyDestination[12 * 10];
    Indexed8 indexedSource = {copySource, 5, 3, 8, gray, 256};
    Surface8 indexedDestination = {copyDestination + 12, 10, 8, 12};
    RC copyBounds(0, 0, 5, 3), copyTarget(-1, 1, 9, 7), copyClip(1, 0, 9, 8), copyHole(3, 2, 5, 4);
    auto copyRegion = Region::PregnNew(&copyClip);
    CHECK(copyRegion && copyRegion->FDiffRc(&copyHole));
    for (int pass = 0; pass < 3; ++pass)
    {
        for (auto &value : copyDestination)
            value = 77;
        indexedSource.colors = pass == 2 ? 33 : 256;
        CHECK(CopyIndexed(indexedDestination, indexedSource, copyBounds, copyTarget, pass ? reverseGray : duplicateGray,
                          256, &copyClip, copyRegion));
        for (int y = -1; y <= 8; ++y)
            for (int x = 0; x < 12; ++x)
            {
                byte expected = 77;
                if (x >= 1 && x < 9 && y >= 1 && y < 7 && !(x >= 3 && x < 5 && y >= 2 && y < 4))
                {
                    const byte index = copySource[(y - 1) / 2 * 8 + (x + 1) / 2];
                    expected = !pass                           ? index
                               : index >= indexedSource.colors ? 255
                               : index == 17 || index == 200   ? 0
                                                               : 255 - index;
                }
                CHECK(copyDestination[(y + 1) * 12 + x] == expected);
            }
    }
    ReleasePpo(&copyRegion);
    // Partial overlap with different starting addresses must also use a
    // snapshot, including when palette translation changes the copied bytes.
    byte overlappingIndices[12];
    for (int i = 0; i < 12; ++i)
        overlappingIndices[i] = static_cast<byte>(i * 7);
    Indexed8 overlappingInput = {overlappingIndices, 3, 2, 4, gray, 256};
    Surface8 overlappingOutput = {overlappingIndices + 1, 3, 2, 4};
    CHECK(CopyIndexed(overlappingOutput, overlappingInput, indexedBounds, indexedBounds, reverseGray, 256));
    for (int i = 0; i < 12; ++i)
    {
        const byte expected = (i >= 1 && i <= 3) || (i >= 5 && i <= 7) ? 255 - (i - 1) * 7 : i * 7;
        CHECK(overlappingIndices[i] == expected);
    }
    for (auto &value : copyDestination)
        value = 77;
    CHECK(!CopyIndexed(indexedDestination, indexedSource, copyBounds, copyTarget, reverseGray, 0));
    indexedSource.pitch = 1;
    CHECK(!CopyIndexed(indexedDestination, indexedSource, copyBounds, copyTarget, reverseGray, 256));
    for (auto value : copyDestination)
        CHECK(value == 77);
    const std::uint8_t runs[] = {1, 2, 0, 1};
    const std::int16_t rowBytes[] = {4, 0};
    RleBitmap8 rle = {runs, sizeof(runs), rowBytes, 4, 2, false, 0};
    std::uint32_t sprite[32];
    for (auto &pixel : sprite)
        pixel = 0x224466;
    Surface32 spriteSurface = {sprite, 8, 4, 8 * 4};
    RC spriteBounds(0, 0, 8, 4);
    CHECK(DrawRle(spriteSurface, rle, spriteBounds, palette, 2));
    for (int y = 0; y < 4; ++y)
        for (int x = 0; x < 8; ++x)
        {
            const auto expected = y < 2 && x >= 2 && x < 4   ? 0x110203u
                                  : y < 2 && x >= 4 && x < 6 ? 0x070809u
                                                             : 0x224466u;
            CHECK(sprite[y * 8 + x] == expected);
        }
    // A malformed second row must be rejected before the valid first row can
    // change the destination. No partially rendered corrupt sprites.
    const std::int16_t badRows[] = {4, 1};
    rle.rowBytes = badRows;
    for (auto &pixel : sprite)
        pixel = 0x556677;
    CHECK(!DrawRle(spriteSurface, rle, spriteBounds, palette, 2));
    std::uint32_t polygonPixels[64] = {};
    Surface32 polygonSurface = {polygonPixels, 8, 8, 8 * 4};
    Brush polygonBrush;
    polygonBrush.foreground.rgb = 0xFFFFFF;
    const Point holePolygon[] = {{0, 0}, {8, 0}, {8, 8}, {0, 8}, {0, 0}, {2, 2},
                                 {2, 6}, {6, 6}, {6, 2}, {2, 2}, {0, 0}};
    CHECK(FillPolygon(polygonSurface, holePolygon, 11, polygonBrush));
    for (int y = 0; y < 8; ++y)
        for (int x = 0; x < 8; ++x)
            CHECK(polygonPixels[y * 8 + x] == (x >= 2 && x < 6 && y >= 2 && y < 6 ? 0u : 0xFFFFFFu));
    // An overlapping contour is filled once under nonzero winding (an
    // even-odd fill would incorrectly cancel this twice-traced rectangle).
    const Point repeated[] = {{1, 1}, {7, 1}, {7, 7}, {1, 7}, {1, 1}, {7, 1}, {7, 7}, {1, 7}};
    polygonBrush.foreground.mode = InkMode::invert;
    CHECK(FillPolygon(polygonSurface, repeated, 8, polygonBrush));
    CHECK(polygonPixels[1 * 8 + 1] == 0 && polygonPixels[3 * 8 + 3] == 0xFFFFFF);
    for (auto &pixel : polygonPixels)
        pixel = 0;
    polygonBrush.foreground.mode = InkMode::color;
    const Point extreme[] = {{INT32_MIN, INT32_MIN}, {INT32_MAX, INT32_MAX}, {INT32_MIN, INT32_MAX}};
    CHECK(FillPolygon(polygonSurface, extreme, 3, polygonBrush));
    for (int y = 0; y < 8; ++y)
        for (int x = 0; x < 8; ++x)
            CHECK(polygonPixels[y * 8 + x] == (x < y ? 0xFFFFFFu : 0u));
    for (auto pixel : sprite)
        CHECK(pixel == 0x556677);
    const std::uint8_t overflow[] = {3, 2, 0, 1};
    rle.rowBytes = rowBytes;
    rle.data = overflow;
    CHECK(!DrawRle(spriteSurface, rle, spriteBounds, palette, 2));
    const std::uint8_t coverageBytes[] = {0, 128, 255, 64};
    Coverage8 coverage = {coverageBytes, 2, 2, 2};
    std::uint32_t blended[16];
    for (auto &pixel : blended)
        pixel = 0x112233;
    Surface32 blendedSurface = {blended, 4, 4, 16};
    DrawCoverage(blendedSurface, coverage, 1, 1, 0xFF0000u);
    CHECK(blended[5] == 0x112233 && blended[6] == 0x881119 && blended[9] == 0xFF0000 && blended[10] == 0x4D1926);
    std::uint8_t indexedGlyph[20];
    for (auto &pixel : indexedGlyph)
        pixel = 3;
    Surface8 glyphSurface = {indexedGlyph, 4, 4, 5};
    DrawCoverage(glyphSurface, coverage, 1, 1, std::uint8_t(7));
    CHECK(indexedGlyph[6] == 3 && indexedGlyph[7] == 7 && indexedGlyph[11] == 7 && indexedGlyph[12] == 3 &&
          indexedGlyph[9] == 3);
    {
        // Indexed operations share the RGB geometry but retain byte operations,
        // clipping, holes and row padding without expanding through RGB colors.
        std::uint8_t indexedPixels[12 * 16];
        for (auto &value : indexedPixels)
            value = 17;
        Surface8 indexedSurface = {indexedPixels + 16, 12, 10, 16};
        Brush8 indexedBrush;
        indexedBrush.foreground = Ink8::Color(71);
        indexedBrush.background = Ink8::Invert();
        for (auto &row : indexedBrush.pattern)
            row = 0xA6;
        RC indexedBounds(-3, -2, 15, 12), indexedClip(1, 1, 11, 9), indexedHole(4, 3, 7, 6);
        auto indexedRegion = Region::PregnNew(&indexedClip);
        CHECK(indexedRegion && indexedRegion->FDiffRc(&indexedHole));
        FillRect(indexedSurface, indexedBounds, indexedBrush, &indexedClip, indexedRegion);
        for (int y = -1; y < 11; ++y)
            for (int x = 0; x < 16; ++x)
            {
                const bool inside = x >= 1 && x < 11 && y >= 1 && y < 9 && !(x >= 4 && x < 7 && y >= 3 && y < 6);
                CHECK(indexedPixels[(y + 1) * 16 + x] == (inside ? (0xA6 & (0x80 >> (x & 7))) ? 71 : (17 ^ 255) : 17));
            }
        ReleasePpo(&indexedRegion);
        for (auto &value : indexedPixels)
            value = 17;
        const Point indexedTriangle[] = {{1, 1}, {9, 1}, {1, 9}};
        indexedBrush.foreground = Ink8::Invert();
        for (auto &row : indexedBrush.pattern)
            row = 255;
        CHECK(FillPolygon(indexedSurface, indexedTriangle, 3, indexedBrush));
        for (int y = -1; y < 11; ++y)
            for (int x = 0; x < 16; ++x)
                CHECK(indexedPixels[(y + 1) * 16 + x] == (x >= 1 && y >= 1 && x + y < 10 ? (17 ^ 255) : 17));
        Color reversed[256];
        for (int i = 0; i < 256; ++i)
            reversed[i] = {static_cast<std::uint8_t>(255 - i), static_cast<std::uint8_t>(255 - i),
                           static_cast<std::uint8_t>(255 - i), 0};
        Brush legacy;
        legacy.foreground = {0, InkMode::clear};
        legacy.background = {0, InkMode::invert};
        for (auto &row : legacy.pattern)
            row = 0xA6;
        CHECK(ResolveBrush8(legacy, reversed, 256, indexedBrush));
        for (auto &value : indexedPixels)
            value = 17;
        RC entire(0, 0, 12, 10);
        FillRect(indexedSurface, entire, indexedBrush);
        CHECK(indexedPixels[16] == (17 ^ 255) && indexedPixels[17] == 17);
        CHECK(ResolveBrush8(legacy, reversed, 256, indexedBrush, true));
        FillRect(indexedSurface, entire, indexedBrush);
        CHECK(indexedPixels[16] == 17 && indexedPixels[17] == (17 ^ 255));
    }
    std::puts(
        "Portable pixels: RGB/indexed shapes, Boolean palette brushes, clipping, padding, stretch and overlap passed.");
    return 0;
}
