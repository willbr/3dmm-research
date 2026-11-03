/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "dialog_resource.h"
#include <cstdio>
#include <limits>

#if defined(_WINDOWS_) || defined(_INC_WINDOWS)
#error Dialog resources must be usable without the Windows SDK
#endif
#define CHECK(value)                                                                                                   \
    do                                                                                                                 \
    {                                                                                                                  \
        if (!(value))                                                                                                  \
        {                                                                                                              \
            std::fprintf(stderr, "Dialog resource check failed at %d\n", __LINE__);                                    \
            return 1;                                                                                                  \
        }                                                                                                              \
    } while (0)
namespace
{
struct Writer
{
    std::vector<std::uint8_t> bytes;
    void Word(std::uint16_t value)
    {
        bytes.push_back(value & 255);
        bytes.push_back(value >> 8);
    }
    void Dword(std::uint32_t value)
    {
        Word(value & 65535);
        Word(value >> 16);
    }
    void Text(const std::u16string &text)
    {
        for (auto c : text)
            Word(c);
        Word(0);
    }
    void Ordinal(std::uint16_t value)
    {
        Word(65535);
        Word(value);
    }
    void Bounds(int x, int y, int width, int height)
    {
        Word(x);
        Word(y);
        Word(width);
        Word(height);
    }
    void Align()
    {
        while (bytes.size() & 3)
            bytes.push_back(0xCC);
    }
};
struct Fixture
{
    Writer writer;
    std::size_t firstCreation = 0;
    Fixture(bool extended, bool font)
    {
        auto &w = writer;
        const unsigned style = 0x80C80000u | (font ? 0x40 : 0);
        if (extended)
        {
            w.Word(1);
            w.Word(65535);
            w.Dword(0x12345678);
            w.Dword(0x87654321);
            w.Dword(style);
        }
        else
        {
            w.Dword(style);
            w.Dword(0x87654321);
        }
        w.Word(13);
        w.Bounds(-3, 7, 210, 140);
        w.Ordinal(17);
        w.Text(u"DialogClass");
        w.Text(u"Caf\u00E9 \U0001F3AC");
        if (font)
        {
            w.Word(9);
            if (extended)
            {
                w.Word(650);
                w.bytes.push_back(1);
                w.bytes.push_back(204);
            }
            w.Text(u"Test Font");
        }
        const unsigned ids[] = {1, 10, 20, 65535, 21, 23, 24, 25, 30, 40, 50, 60, extended ? 0xF1234567u : 70};
        const unsigned classes[] = {0x80, 0x80, 0x80, 0x82, 0x80, 0x80, 0x80, 0x80, 0x81, 0x85, 0x80, 0, 0x80};
        const unsigned styles[] = {1, 3, 9 | 0x20000, 0, 9, 4, 9 | 0x20000, 9, 0x80, 3, 5, 0, 0};
        for (unsigned i = 0; i < 13; ++i)
        {
            w.Align();
            if (extended)
            {
                w.Dword(900 + i);
                w.Dword(0x100);
                w.Dword(0x50000000 | styles[i]);
            }
            else
            {
                w.Dword(0x50000000 | styles[i]);
                w.Dword(0x100);
            }
            w.Bounds(-1, 3 + int(i) * 9, 120, 8);
            if (extended)
                w.Dword(ids[i]);
            else
                w.Word(ids[i]);
            if (classes[i])
                w.Ordinal(classes[i]);
            else
                w.Text(u"Widget");
            if (i == 3)
                w.Ordinal(777);
            else
                w.Text(u"Caption & Text");
            if (i == 0)
            {
                firstCreation = w.bytes.size();
                w.Word(3);
                w.bytes.insert(w.bytes.end(), {0xDE, 0xAD, 0xBE});
            }
            else if (i == 12)
            {
                w.Word(2);
                w.bytes.insert(w.bytes.end(), {7, 8});
            }
            else
                w.Word(0);
        }
    }
};
} // namespace
int main()
{
    using namespace KauaiDialogs;
    unsigned rejected = 0;
    for (bool extended : {false, true})
        for (bool font : {false, true})
        {
            Fixture fixture(extended, font);
            const auto &bytes = fixture.writer.bytes;
            Resource model;
            CHECK(ReadResource(bytes.data(), bytes.size(), model));
            CHECK(model.extended == extended && model.style == (0x80C80000u | (font ? 0x40 : 0)) &&
                  model.extendedStyle == 0x87654321);
            CHECK(model.help == (extended ? 0x12345678u : 0) && model.bounds.x == -3 && model.bounds.y == 7 &&
                  model.bounds.width == 210 && model.bounds.height == 140);
            CHECK(model.menu.ordinal && model.menu.number == 17 && model.windowClass.text == u"DialogClass" &&
                  model.title == u"Caf\u00E9 \U0001F3AC");
            CHECK(model.font.present == font);
            if (font)
                CHECK(model.font.points == 9 && model.font.face == u"Test Font" &&
                      model.font.weight == (extended ? 650 : 0) && model.font.italic == (extended ? 1 : 0) &&
                      model.font.charset == (extended ? 204 : 0));
            CHECK(model.controls.size() == 13 && model.controls[0].bounds.x == -1 &&
                  model.controls[0].bounds.height == 8);
            CHECK(model.controls[0].extendedStyle == 0x100 && model.controls[0].help == (extended ? 900u : 0));
            CHECK(model.controls[0].creationData == std::vector<std::uint8_t>({0xDE, 0xAD, 0xBE}));
            CHECK(model.controls[12].creationData == std::vector<std::uint8_t>({7, 8}));
            CHECK(model.controls[3].title.ordinal && model.controls[3].title.number == 777);
            CHECK(!model.controls[11].windowClass.ordinal && model.controls[11].windowClass.text == u"Widget");
            CHECK(model.controls[12].id == (extended ? 0xF1234567u : 70u));
            std::vector<Binding> bindings;
            CHECK(BuildBindings(model, bindings) && bindings.size() == 8);
            CHECK(bindings[0].first == 1 && bindings[0].kind == ItemKind::button &&
                  bindings[1].kind == ItemKind::checkBox);
            CHECK(bindings[2].kind == ItemKind::radioGroup && bindings[2].first == 20 && bindings[2].limit == 22);
            CHECK(bindings[3].first == 23 && bindings[3].limit == 24 && bindings[4].first == 24 &&
                  bindings[4].limit == 26);
            CHECK(bindings[5].kind == ItemKind::editText && bindings[6].kind == ItemKind::combo);
            CHECK(bindings[7].first == model.controls[12].id && bindings[7].limit == model.controls[12].id + 1);
            // Unaligned input, encoded word widths, and arbitrary resource padding.
            std::vector<std::uint8_t> unaligned(1, 0xA5);
            unaligned.insert(unaligned.end(), bytes.begin(), bytes.end());
            CHECK(ReadResource(unaligned.data() + 1, bytes.size(), model));
            unaligned.insert(unaligned.end(), {0xCC, 0xCC, 0xCC});
            CHECK(ReadResource(unaligned.data() + 1, unaligned.size() - 1, model));
            Resource sentinel;
            sentinel.title = u"Keep";
            sentinel.style = 37;
            for (std::size_t length = 0; length < bytes.size(); ++length)
            {
                CHECK(!ReadResource(bytes.data(), length, sentinel));
                CHECK(sentinel.title == u"Keep" && sentinel.style == 37 && sentinel.controls.empty());
                ++rejected;
            }
            auto bad = bytes;
            bad[extended ? 16 : 8] = 255;
            bad[(extended ? 16 : 8) + 1] = 255;
            CHECK(!ReadResource(bad.data(), bad.size(), sentinel));
            bad = bytes;
            bad[fixture.firstCreation] = 255;
            bad[fixture.firstCreation + 1] = 255;
            CHECK(!ReadResource(bad.data(), bad.size(), sentinel));
            if (extended)
            {
                bad = bytes;
                bad[0] = 2;
                CHECK(!ReadResource(bad.data(), bad.size(), sentinel));
            }
            model.controls.back().id = std::numeric_limits<std::uint32_t>::max();
            CHECK(!BuildBindings(model, bindings) && bindings.size() == 8 && bindings[0].first == 1);
        }
    Resource empty;
    Writer noControls;
    noControls.Dword(0);
    noControls.Dword(0);
    noControls.Word(0);
    noControls.Bounds(0, 0, 1, 1);
    noControls.Word(0);
    noControls.Word(0);
    noControls.Word(0);
    CHECK(ReadResource(noControls.bytes.data(), noControls.bytes.size(), empty) && empty.controls.empty());
    CHECK(!ReadResource(nullptr, 40, empty));
    std::printf("Dialog resources: standard/extended layouts, UTF-16, IDs, font metadata, creation data, original item "
                "grouping and %u transactional truncation rejections passed.\n",
                rejected);
}
