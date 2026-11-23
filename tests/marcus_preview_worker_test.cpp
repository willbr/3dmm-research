#include "preview_worker.h"
#include "preview_process.h"
#include <memory>
#include <sstream>
#include <stdexcept>
#include <iostream>
#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif

void Check(bool value) { if (!value) throw std::runtime_error("Preview worker check failed."); }

static int ReplyChild(const std::string &mode)
{
#ifdef _WIN32
    if (_setmode(_fileno(stdout), _O_BINARY) == -1) return 2;
#endif
    if (mode == "flood")
    {
        const unsigned char block[65536]{};
        for (int i = 0; i < 80; ++i)
            if (std::fwrite(block, 1, sizeof(block), stdout) != sizeof(block)) return 2;
        return std::fputc(0, stdout) == EOF ? 2 : 0;
    }
    std::string request;
    if (!std::getline(std::cin, request) || request != "test") return 2;
    if (mode == "truncated")
    {
        const unsigned char incomplete[] = {24, 0, 0, 0, 'M', 'P', 'R', '1'};
        return std::fwrite(incomplete, 1, sizeof(incomplete), stdout) == sizeof(incomplete) ? 0 : 2;
    }
    const std::vector<unsigned char> pixels(320 * 240 * 4, 17);
    const MarcusPreviewPacket reply{320, 240, {}, {}, {}, {pixels.data(), pixels.size()}, 320 * 4};
    return marcus_preview_wire_write_file(&reply, 1, stdout) ? 0 : 2;
}

static void CheckTransfers()
{
    const auto base = SDL_GetBasePath();
    Check(base != nullptr);
    const auto executable = std::string(base) + "marcus-preview-worker-test"
#ifdef _WIN32
                           ".exe"
#endif
        ;
    MarcusPreviewReply reply{};
    const std::unique_ptr<MarcusPreviewReply, decltype(&marcus_preview_reply_dispose)> owner(&reply,
                                                                                           marcus_preview_reply_dispose);
    const auto request = [&](const char *mode, bool text, MarcusPreviewCancelled *cancelled = nullptr) {
        const char *args[] = {executable.c_str(), "--reply-child", mode, nullptr};
        const std::unique_ptr<SDL_Process, decltype(&marcus_preview_process_close)>
            process(marcus_preview_process_start(args, 1, 1), marcus_preview_process_close);
        Check(process != nullptr);
        return marcus_preview_process_request(process.get(), "test\n", 5, text, nullptr, cancelled, &reply);
    };
    Check(request("complete", true) == MARCUS_PREVIEW_TRANSFER_OK);
    const auto *pixels = static_cast<const unsigned char *>(reply.packet.rgba.data);
    Check(reply.packet.width == 320 && reply.packet.height == 240 && pixels && pixels[307199] == 17);
    Check(request("complete", false) == MARCUS_PREVIEW_TRANSFER_LIMIT);
    Check(request("truncated", false) == MARCUS_PREVIEW_TRANSFER_STOPPED);
    Check(request("complete", true, [](void *) { return 1; }) == MARCUS_PREVIEW_TRANSFER_CANCELLED);
    const char *args[] = {executable.c_str(), "--reply-child", "flood", nullptr};
    Check(marcus_preview_process_capture(args, 0, nullptr, nullptr, &reply) == MARCUS_PREVIEW_TRANSFER_LIMIT);
    Check(reply.packet.rgba.data == pixels && pixels[307199] == 17);
}

int main(int argc, char **argv)
{
    if (argc == 3 && std::string(argv[1]) == "--reply-child") return ReplyChild(argv[2]);
    try
    {
        Check(SDL_Init(0));
        CheckTransfers();
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
        const auto missingTiming = RunPreviewWorker("missing-preview-movie.3mm", "missing-resources.chk", [] { return false; }, "", true);
        Check(!missingTiming.error.empty() && missingTiming.rgba.empty());
        bool cancelled = false;
        try { RunPreviewWorker("missing-preview-movie.3mm", "missing-resources.chk", [] { return true; }); }
        catch (const std::exception &) { cancelled = true; }
        Check(cancelled);
        cancelled = false;
        try { RunPreviewWorker("missing-preview-movie.3mm", "missing-resources.chk", [] { return true; }, "", true); }
        catch (const std::exception &) { cancelled = true; }
        Check(cancelled);
        SDL_Quit();
        return 0;
    }
    catch (const std::exception &e) { std::cerr << e.what() << '\n'; SDL_Quit(); return 1; }
}
