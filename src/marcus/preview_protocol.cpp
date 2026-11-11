#include "preview_worker.h"
#include <ostream>
#include <stdexcept>
#include <cstring>

namespace
{
constexpr std::size_t maxText = 16384, maxPixels = 1024 * 1024 * 4;
void Word(std::ostream &output, std::uint32_t value)
{
    for (int i = 0; i < 4; ++i) output.put(static_cast<char>(value >> (i * 8)));
}
}
bool WritePreview(std::ostream &output, const BrowserPreview &preview)
{
    if (preview.title.size() > maxText || preview.details.size() > maxText || preview.error.size() > maxText ||
        preview.rgba.size() > maxPixels || preview.width < 0 || preview.width > 1024 ||
        preview.height < 0 || preview.height > 1024 ||
        preview.rgba.size() != static_cast<std::size_t>(preview.width) * preview.height * 4) return false;
    output.write("MPR1", 4);
    Word(output, preview.width); Word(output, preview.height);
    Word(output, static_cast<std::uint32_t>(preview.title.size()));
    Word(output, static_cast<std::uint32_t>(preview.details.size()));
    Word(output, static_cast<std::uint32_t>(preview.error.size()));
    output.write(preview.title.data(), preview.title.size());
    output.write(preview.details.data(), preview.details.size());
    output.write(preview.error.data(), preview.error.size());
    if (!preview.rgba.empty()) output.write(reinterpret_cast<const char *>(preview.rgba.data()), preview.rgba.size());
    return bool(output);
}
BrowserPreview ReadPreview(const std::vector<std::uint8_t> &bytes)
{
    if (bytes.size() < 24 || std::memcmp(bytes.data(), "MPR1", 4)) throw std::runtime_error("Invalid preview helper response.");
    auto word = [&](std::size_t at) {
        std::uint32_t value = 0;
        for (int i = 0; i < 4; ++i) value |= std::uint32_t(bytes[at + i]) << (i * 8);
        return value;
    };
    const auto width = word(4), height = word(8), title = word(12), details = word(16), error = word(20);
    const auto pixels = std::uint64_t(width) * height * 4;
    if (width > 1024 || height > 1024 || title > maxText || details > maxText || error > maxText ||
        24ull + title + details + error + pixels != bytes.size()) throw std::runtime_error("Preview helper response exceeds its limits.");
    BrowserPreview result;
    std::size_t at = 24;
    auto string = [&](std::uint32_t size) { std::string value(reinterpret_cast<const char *>(bytes.data() + at), size); at += size; return value; };
    result.width = width; result.height = height;
    result.title = string(title); result.details = string(details); result.error = string(error);
    result.rgba.assign(bytes.begin() + at, bytes.end());
    return result;
}
