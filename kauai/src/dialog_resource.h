/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef KAUAI_DIALOG_RESOURCE_H
#define KAUAI_DIALOG_RESOURCE_H
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace KauaiDialogs
{
// Encoded names and text remain UTF-16 code units, independent of wchar_t.
struct Name
{
    bool ordinal = false;
    std::uint16_t number = 0;
    std::u16string text;
};
struct Bounds
{
    std::int16_t x = 0, y = 0, width = 0, height = 0;
};
struct Font
{
    bool present = false;
    std::uint16_t points = 0, weight = 0;
    std::uint8_t italic = 0, charset = 0;
    std::u16string face;
};
struct Control
{
    std::uint32_t id = 0, style = 0, extendedStyle = 0, help = 0;
    Bounds bounds;
    Name windowClass, title;
    // Payload excludes the count word. Both native template formats count the
    // following bytes, verified against User32 with a subsequent control.
    std::vector<std::uint8_t> creationData;
};
struct Resource
{
    bool extended = false;
    std::uint32_t style = 0, extendedStyle = 0, help = 0;
    Bounds bounds;
    Name menu, windowClass;
    std::u16string title;
    Font font;
    std::vector<Control> controls;
};
// Reads standard or version-1 extended dialog templates. Offsets are relative
// to the byte span, so unaligned input is supported. Failure leaves output
// intact. Resource padding after the last control is permitted.
bool ReadResource(const void *bytes, std::size_t count, Resource &output);

enum class ItemKind : std::int32_t
{
    button,
    checkBox,
    radioGroup,
    editText,
    combo
};
struct Binding
{
    std::uint32_t first = 0, limit = 0;
    ItemKind kind = ItemKind::button;
};
// Original Kauai item numbering: recognized ordinal classes only; consecutive
// radio IDs join the preceding radio group unless WS_GROUP starts a new one.
// Ignored static/custom controls remain available in Resource::controls.
// Unrepresentable half-open ID ranges fail without changing output.
bool BuildBindings(const Resource &resource, std::vector<Binding> &output);
} // namespace KauaiDialogs
#endif
