/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "studio_iso.h"
#include "iso_cache.h"
#include "embedded_midi.h"
#include <SDL3/SDL.h>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <stdexcept>
namespace StudioContent
{
namespace
{
namespace fs = std::filesystem;
Text requested;
ISOStatus status;
const char *Environment(const char *name)
{
    const auto value = SDL_getenv(name);
    return value && *value ? value : nullptr;
}
fs::path Native(const Text &text)
{
#ifdef UNICODE
    return fs::path(std::u16string(text.begin(), text.end()));
#else
    return fs::path(text);
#endif
}
Root NativeRoot(const fs::path &path)
{
#ifdef UNICODE
    const auto wide = path.u16string();
    Text text(wide.begin(), wide.end());
#else
    Text text = path.string();
    if (fs::path(text) != path)
        throw std::runtime_error("The ISO cache path is not representable by this build");
#endif
    auto root = ValidateRoot(std::move(text),
#ifdef _WIN32
                             true
#else
                             false
#endif
    );
    if (root.status != Status::Ready)
        throw std::runtime_error("ISO cache path exceeds Studio's 255-character path limit; set THREEDMM_CACHE_DIR");
    return root;
}
void DefaultPath(const char *name, const fs::path &path)
{
    if (Environment(name) || !fs::exists(path))
        return;
    const auto utf8 = path.u8string();
    // Font discovery reads the process environment; SDL MIDI reads SDL's copy.
    if (SDL_setenv_unsafe(name, utf8.c_str(), 1) != 0 ||
        !SDL_SetEnvironmentVariable(SDL_GetEnvironment(), name, utf8.c_str(), true))
        throw std::runtime_error(std::string("Cannot configure ") + name);
}
fs::path Discover()
{
    std::vector<fs::path> directories;
    if (const auto base = SDL_GetBasePath())
        directories.push_back(fs::u8path(base));
    directories.push_back(fs::current_path());
    for (const auto &directory : directories)
    {
        std::error_code error;
        std::vector<fs::path> images;
        for (fs::directory_iterator it(directory, error), end; !error && it != end; it.increment(error))
        {
            auto extension = it->path().extension().u8string();
            for (auto &c : extension)
                if (c >= 'A' && c <= 'Z')
                    c += 'a' - 'A';
            if (extension == ".iso" && it->is_regular_file(error))
                images.push_back(it->path());
        }
        if (images.size() == 1)
            return images.front();
        if (images.size() > 1)
            throw std::runtime_error("More than one ISO was found; select one with --iso PATH");
    }
    return {};
}
} // namespace
void SetISORequest(const Text &path)
{
    requested = path;
}
bool HasISORequest()
{
    return !requested.empty() || Environment("THREEDMM_ISO");
}
const ISOStatus &CurrentISO()
{
    return status;
}
Root PrepareISO(bool automaticDiscovery)
{
    try
    {
        fs::path image;
        if (!requested.empty())
            image = Native(requested);
        else if (const auto configured = Environment("THREEDMM_ISO"))
            image = fs::u8path(configured);
        else if (automaticDiscovery)
            image = Discover();
        if (image.empty())
            return {};
        fs::path cacheDirectory;
        if (const auto configured = Environment("THREEDMM_CACHE_DIR"))
        {
            cacheDirectory = fs::u8path(configured);
            if (!cacheDirectory.is_absolute())
                throw std::runtime_error("THREEDMM_CACHE_DIR must be an absolute path");
        }
        else
        {
            std::unique_ptr<char, decltype(&SDL_free)> preferences(SDL_GetPrefPath("3DMMForever", "Studio"), SDL_free);
            if (!preferences)
                throw std::runtime_error("Cannot find a writable application cache directory");
            cacheDirectory = fs::u8path(preferences.get()) / "iso-cache";
        }
        const auto result = StudioISO::Prepare(image, cacheDirectory, StudioISO::BundledMIDI());
        auto root = NativeRoot(result.root);
        const auto entry = result.root.parent_path();
        DefaultPath("KAUAI_SOUNDFONT", entry / "assets/GeneralUserGS.sf2");
        DefaultPath("KAUAI_FONT_PATH", entry / "fonts");
        status.used = true;
        status.reused = result.reused;
        status.image = fs::canonical(image).u8string();
        status.root = result.root.u8string();
        std::fprintf(stderr, "ISO content: %s (%s cache)\n", status.image.c_str(),
                     result.reused ? "reused" : "created");
        return root;
    }
    catch (const std::exception &error)
    {
        status.error = error.what();
        std::fprintf(stderr, "ISO content error: %s\n", error.what());
        return {Status::Error, {}, 0};
    }
}
} // namespace StudioContent
