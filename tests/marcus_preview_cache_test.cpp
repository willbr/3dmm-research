#include "preview_cache.h"
#include <SDL3/SDL.h>
#include <chrono>
#include <fstream>
#include <iostream>
#include <stdexcept>

void Check(bool value, const char *message) { if (!value) throw std::runtime_error(message); }
int main(int argc, char **argv)
{
    try
    {
        Check(argc == 2 && SDL_Init(0), "Initialization failed.");
        const auto root = std::filesystem::absolute(std::filesystem::u8path(argv[1]));
        std::filesystem::create_directories(root);
        const auto source = root / "backgrounds.3cn", decoder = root / "decoder";
        std::ofstream(source) << "content";
        std::ofstream(decoder) << "decoder";
        const auto directory = root / "cache";
        MarcusPreviewCache cache(directory, {source, decoder});
        BrowserPreview preview, loaded;
        preview.width = 2; preview.height = 1; preview.rgba = {1, 2, 3, 255, 4, 5, 6, 255};
        cache.Store(42, preview);
        // A newly constructed cache represents a later application launch.
        MarcusPreviewCache reopened(directory, {source, decoder});
        Check(reopened.Key() == cache.Key() && reopened.Load(42, loaded), "Persistent cache miss.");
        Check(loaded.width == 2 && loaded.height == 1 && loaded.rgba == preview.rgba, "Pixels changed.");
        Check(!cache.Load(43, loaded), "Chunks share cached images.");
        preview.error = "Temporary failure";
        cache.Store(43, preview);
        Check(!cache.Load(43, loaded), "Failure cached.");
        preview.error.clear();
        preview.rgba.pop_back();
        cache.Store(43, preview);
        Check(!cache.Load(43, loaded), "Malformed preview cached.");
        preview.rgba.push_back(255);
        const auto file = directory / (cache.Key() + "-42.mpr");
        std::ofstream(file, std::ios::binary | std::ios::trunc) << "broken";
        Check(!cache.Load(42, loaded), "Truncated file accepted.");
        { std::ofstream oversized(file, std::ios::binary | std::ios::trunc); oversized.seekp(6 * 1024 * 1024); oversized.put(0); }
        Check(!cache.Load(42, loaded), "Oversized file accepted.");
        cache.Store(42, preview);
        Check(cache.Load(42, loaded), "Corrupt entry was not replaced.");
        // Same-size edits must invalidate by modification time, too.
        std::filesystem::last_write_time(source, std::filesystem::last_write_time(source) + std::chrono::seconds(2));
        MarcusPreviewCache edited(directory, {source, decoder});
        Check(edited.Key() != cache.Key() && !edited.Load(42, loaded), "Source edit reused stale pixels.");
        edited.Store(42, preview);
        std::ofstream(decoder, std::ios::app) << "new build";
        MarcusPreviewCache rebuilt(directory, {source, decoder});
        Check(rebuilt.Key() != edited.Key() && !rebuilt.Load(42, loaded), "Decoder build reused stale pixels.");
        const auto other = root / "other.3cn";
        std::filesystem::copy_file(source, other, std::filesystem::copy_options::overwrite_existing);
        std::filesystem::last_write_time(other, std::filesystem::last_write_time(source));
        Check(MarcusPreviewCache(directory, {other, decoder}).Key() != rebuilt.Key(), "Source path ignored.");
        MarcusPreviewCache missing(directory, {root / "missing.3cn"});
        missing.Store(42, preview);
        Check(!missing.Load(42, loaded), "Missing sources cached.");
        MarcusPreviewCache unwritable(source, {source});
        unwritable.Store(42, preview);
        Check(!unwritable.Load(42, loaded), "Unwritable cache accepted.");
        MarcusPreviewCache bounded(root / "bounded", {source}, 1);
        bounded.Store(42, preview);
        bounded.Store(43, preview);
        Check(!bounded.Load(42, loaded) && bounded.Load(43, loaded), "Cache did not evict an older preview.");
        SDL_Quit();
        return 0;
    }
    catch (const std::exception &e) { std::cerr << e.what() << '\n'; SDL_Quit(); return 1; }
}
