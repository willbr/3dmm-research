#include "preview_worker.h"
#include <SDL3/SDL.h>
#include <sstream>
#include <stdexcept>
#include <iostream>

void Check(bool value) { if (!value) throw std::runtime_error("Preview worker check failed."); }
int main()
{
    try
    {
        Check(SDL_Init(0));
        BrowserPreview source;
        source.title = "A movie"; source.details = "2 scenes";
        source.width = 2; source.height = 1; source.rgba = {1, 2, 3, 255, 4, 5, 6, 255};
        std::ostringstream output(std::ios::binary);
        Check(WritePreview(output, source));
        const auto data = output.str();
        std::vector<std::uint8_t> bytes(data.begin(), data.end());
        const auto decoded = ReadPreview(bytes);
        Check(decoded.title == source.title && decoded.details == source.details && decoded.width == 2 && decoded.height == 1 && decoded.rgba == source.rgba);
        for (int mode = 0; mode < 4; ++mode)
        {
            auto corrupt = bytes;
            if (mode == 0) corrupt.resize(23);
            if (mode == 1) corrupt[0] = 'X';
            if (mode == 2) corrupt[7] = 127; // Oversized width.
            if (mode == 3) corrupt.push_back(0); // Trailing data.
            bool rejected = false;
            try { ReadPreview(corrupt); } catch (const std::exception &) { rejected = true; }
            Check(rejected);
        }
        source.width = 1025;
        Check(!WritePreview(output, source));
        // Runs the actual isolated decoder, with no UI or player instance.
        const auto missing = RunPreviewWorker("missing-preview-movie.3mm", "missing-resources.chk", [] { return false; });
        Check(!missing.error.empty() && missing.rgba.empty());
        bool cancelled = false;
        try { RunPreviewWorker("missing-preview-movie.3mm", "missing-resources.chk", [] { return true; }); }
        catch (const std::exception &) { cancelled = true; }
        Check(cancelled);
        SDL_Quit();
        return 0;
    }
    catch (const std::exception &e) { std::cerr << e.what() << '\n'; SDL_Quit(); return 1; }
}
