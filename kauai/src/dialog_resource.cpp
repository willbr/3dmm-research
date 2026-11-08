/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "dialog_resource.h"
#include <limits>
#include <new>
#include <stdexcept>
#include <utility>

namespace KauaiDialogs
{
bool TabLayout::Visible(std::uint32_t control, std::size_t selected) const
{
    for (std::size_t page = 0; page < pages.size(); ++page)
        for (const auto member : pages[page].controls)
            if (member == control)
                return page == selected;
    return true;
}
namespace
{
class Reader
{
    const std::uint8_t *bytes;
    std::size_t count, offset = 0;

  public:
    Reader(const void *data, std::size_t length) : bytes(static_cast<const std::uint8_t *>(data)), count(length)
    {
    }
    bool U8(std::uint8_t &value)
    {
        if (offset == count)
            return false;
        value = bytes[offset++];
        return true;
    }
    bool U16(std::uint16_t &value)
    {
        if (count - offset < 2)
            return false;
        value = std::uint16_t(bytes[offset]) | (std::uint16_t(bytes[offset + 1]) << 8);
        offset += 2;
        return true;
    }
    bool U32(std::uint32_t &value)
    {
        std::uint16_t low, high;
        if (!U16(low) || !U16(high))
            return false;
        value = std::uint32_t(low) | (std::uint32_t(high) << 16);
        return true;
    }
    bool Rectangle(Bounds &bounds)
    {
        std::uint16_t values[4];
        for (auto &value : values)
            if (!U16(value))
                return false;
        const auto signedValue = [](std::uint16_t value) {
            return static_cast<std::int16_t>(value < 0x8000 ? value : std::int32_t(value) - 0x10000);
        };
        bounds = {signedValue(values[0]), signedValue(values[1]), signedValue(values[2]), signedValue(values[3])};
        return true;
    }
    bool Text(std::u16string &text)
    {
        std::uint16_t value;
        while (U16(value))
        {
            if (!value)
                return true;
            text.push_back(static_cast<char16_t>(value));
        }
        return false;
    }
    bool Identifier(Name &name)
    {
        std::uint16_t value;
        if (!U16(value))
            return false;
        if (value == 0xFFFF)
        {
            name.ordinal = true;
            return U16(name.number);
        }
        if (!value)
            return true;
        name.text.push_back(static_cast<char16_t>(value));
        return Text(name.text);
    }
    bool AlignControl()
    {
        const auto padding = (4 - (offset & 3)) & 3;
        if (count - offset < padding)
            return false;
        offset += padding;
        return true;
    }
    bool Creation(std::vector<std::uint8_t> &data)
    {
        std::uint16_t length;
        if (!U16(length))
            return false;
        if (!length)
            return true;
        if (count - offset < length)
            return false;
        data.assign(bytes + offset, bytes + offset + length);
        offset += length;
        return true;
    }
    std::size_t Remaining() const
    {
        return count - offset;
    }
};
bool Parse(const void *bytes, std::size_t count, Resource &resource)
{
    if (!bytes || count < 4)
        return false;
    Reader input(bytes, count);
    std::uint16_t first, second, controls;
    if (!input.U16(first) || !input.U16(second))
        return false;
    resource.extended = second == 0xFFFF;
    if (resource.extended)
    {
        if (first != 1 || !input.U32(resource.help) || !input.U32(resource.extendedStyle) || !input.U32(resource.style))
            return false;
    }
    else
    {
        resource.style = std::uint32_t(first) | (std::uint32_t(second) << 16);
        if (!input.U32(resource.extendedStyle))
            return false;
    }
    if (!input.U16(controls) || !input.Rectangle(resource.bounds) || !input.Identifier(resource.menu) ||
        !input.Identifier(resource.windowClass) || !input.Text(resource.title))
        return false;
    resource.font.present = (resource.style & 0x40) != 0; // DS_SETFONT, also present in DS_SHELLFONT.
    if (resource.font.present)
    {
        if (!input.U16(resource.font.points))
            return false;
        if (resource.extended &&
            (!input.U16(resource.font.weight) || !input.U8(resource.font.italic) || !input.U8(resource.font.charset)))
            return false;
        if (!input.Text(resource.font.face))
            return false;
    }
    // A standard item needs at least 24 bytes; extended items need 30. Bound
    // the reserve before allocating, even for a corrupt declared item count.
    if (controls > input.Remaining() / (resource.extended ? 30 : 24))
        return false;
    resource.controls.reserve(controls);
    for (unsigned i = 0; i < controls; ++i)
    {
        Control control;
        if (!input.AlignControl())
            return false;
        if (resource.extended)
        {
            if (!input.U32(control.help) || !input.U32(control.extendedStyle) || !input.U32(control.style) ||
                !input.Rectangle(control.bounds) || !input.U32(control.id))
                return false;
        }
        else
        {
            std::uint16_t id;
            if (!input.U32(control.style) || !input.U32(control.extendedStyle) || !input.Rectangle(control.bounds) ||
                !input.U16(id))
                return false;
            control.id = id;
        }
        if (!input.Identifier(control.windowClass) || !input.Identifier(control.title) ||
            !input.Creation(control.creationData))
            return false;
        resource.controls.push_back(std::move(control));
    }
    return true;
}
} // namespace
bool ReadResource(const void *bytes, std::size_t count, Resource &output)
{
    try
    {
        Resource loaded;
        if (!Parse(bytes, count, loaded))
            return false;
        output = std::move(loaded);
        return true;
    }
    catch (const std::bad_alloc &)
    {
        return false;
    }
    catch (const std::length_error &)
    {
        return false;
    }
}
bool BuildBindings(const Resource &resource, std::vector<Binding> &output)
{
    try
    {
        std::vector<Binding> loaded;
        for (const auto &control : resource.controls)
        {
            if (!control.windowClass.ordinal)
                continue;
            ItemKind kind;
            switch (control.windowClass.number)
            {
            case 0x80:
                switch (control.style & 15)
                {
                case 0:
                case 1:
                    kind = ItemKind::button;
                    break;
                case 2:
                case 3:
                    kind = ItemKind::checkBox;
                    break;
                case 4:
                case 9:
                    kind = ItemKind::radioGroup;
                    break;
                default:
                    continue;
                }
                break;
            case 0x81:
                kind = ItemKind::editText;
                break;
            case 0x85:
                kind = ItemKind::combo;
                break;
            default:
                continue;
            }
            if (control.id == std::numeric_limits<std::uint32_t>::max())
                return false;
            if (kind == ItemKind::radioGroup && !(control.style & 0x20000) && !loaded.empty() &&
                loaded.back().kind == ItemKind::radioGroup && loaded.back().limit == control.id)
                ++loaded.back().limit;
            else
                loaded.push_back({control.id, control.id + 1, kind});
        }
        output.swap(loaded);
        return true;
    }
    catch (const std::bad_alloc &)
    {
        return false;
    }
    catch (const std::length_error &)
    {
        return false;
    }
}
} // namespace KauaiDialogs
