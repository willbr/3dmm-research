#include "preview_cache.h"
#include "preview_worker.h"
#include <SDL3/SDL.h>
#include <fstream>
#include <iomanip>
#include <sstream>

MarcusPreviewCache::MarcusPreviewCache(const std::filesystem::path &directory,
    const std::vector<std::filesystem::path> &sources) : directory(directory), available(!directory.empty())
{
    std::uint64_t hash = 14695981039346656037ull;
    auto add = [&](const std::string &value) {
        for (const unsigned char byte : value) { hash ^= byte; hash *= 1099511628211ull; }
        hash ^= 0; hash *= 1099511628211ull;
    };
    add("Marcus background preview v1");
    for (const auto &source : sources)
    {
        std::error_code ec;
        const auto path = std::filesystem::weakly_canonical(source, ec);
        add((ec ? source : path).u8string());
        const auto size = std::filesystem::file_size(source, ec);
        if (ec) { available = false; add("missing"); continue; }
        add(std::to_string(size));
        const auto modified = std::filesystem::last_write_time(source, ec);
        if (ec) { available = false; add("unreadable"); continue; }
        add(std::to_string(modified.time_since_epoch().count()));
    }
    std::ostringstream name;
    name << std::hex << std::setfill('0') << std::setw(16) << hash;
    key = name.str();
}

std::filesystem::path MarcusPreviewCache::File(unsigned long chunk) const
{ return directory / (key + "-" + std::to_string(chunk) + ".mpr"); }

bool MarcusPreviewCache::Load(unsigned long chunk, BrowserPreview &preview) const
{
    if (!available) return false;
    std::ifstream input(File(chunk), std::ios::binary | std::ios::ate);
    const auto size = input.tellg();
    if (size < 24 || size > 5 * 1024 * 1024) return false;
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
    input.seekg(0);
    if (!input.read(reinterpret_cast<char *>(bytes.data()), bytes.size())) return false;
    try
    {
        auto result = ReadPreview(bytes);
        if (!result.error.empty() || result.rgba.empty()) return false;
        preview = std::move(result);
        return true;
    }
    catch (const std::exception &) { return false; }
}

void MarcusPreviewCache::Store(unsigned long chunk, const BrowserPreview &preview) const
{
    if (!available || !preview.error.empty() || preview.rgba.empty()) return;
    std::error_code ec;
    std::filesystem::create_directories(directory, ec);
    if (ec) return;
    const auto destination = File(chunk);
    const auto temporary = destination.u8string() + "." + std::to_string(SDL_GetTicksNS()) + ".tmp";
    bool written = false;
    {
        std::ofstream output(std::filesystem::u8path(temporary), std::ios::binary | std::ios::trunc);
        if (output && WritePreview(output, preview)) { output.close(); written = bool(output); }
    }
    if (written && SDL_RenamePath(temporary.c_str(), destination.u8string().c_str())) return;
    std::filesystem::remove(std::filesystem::u8path(temporary), ec);
}
