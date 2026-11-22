#include "preview_worker.h"
#include "preview_commands.h"
#include "player.h"
#include "timing_job.h"
#include "mm/preview.h"
#include <fstream>
#include <filesystem>
#include <iostream>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <sstream>
#include <iomanip>
#include <memory>
#include <limits>
#include <exception>
#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif

namespace
{
bool OpenPlayer(MarcusPlayer *player, char **argv, const char *fonts, const char *origin = nullptr)
{
    MarcusMovieInput input;
    input.Open(argv[1]);
    auto file = input.Materialize(0);
    if (origin && *origin) file->contentOrigin = std::filesystem::u8path(origin);
    if (fonts && *fonts) file->LoadFonts(std::filesystem::u8path(fonts));
    return marcus_open_input(player, &input, 0, argv[2], argv[4]) != 0;
}
int FrameError(const char *message)
{
    MarcusPreviewPacket packet;
    marcus_preview_packet_error(&packet, message);
    marcus_preview_wire_write_file(&packet, 1, stdout);
    return 1;
}

int ReadPosition(void *context, int *scene, long *frame, int *fits) noexcept
{
    try
    {
        long long value;
        if (!(std::cin >> *scene >> value))
            return MARCUS_PREVIEW_END;
        *fits = value >= (std::numeric_limits<long>::min)() && value <= (std::numeric_limits<long>::max)();
        *frame = *fits ? static_cast<long>(value) : 0;
        return MARCUS_PREVIEW_REQUEST;
    }
    catch (...)
    {
        *static_cast<std::exception_ptr *>(context) = std::current_exception();
        return MARCUS_PREVIEW_END;
    }
}

int FramePreviews(char **argv, const char *fonts, const char *origin, bool cameras)
{
    try
    {
        std::unique_ptr<MarcusPlayer, decltype(&marcus_destroy)> player(marcus_create(), marcus_destroy);
        if (!player) throw std::runtime_error("Cannot initialize frame thumbnails.");
        marcus_set_muted(player.get(), 1);
        std::size_t consumed;
        const auto scene = std::stoi(argv[5], &consumed);
        if (argv[5][consumed] || scene < 0) throw std::runtime_error("Invalid frame thumbnail scene.");
        if (!OpenPlayer(player.get(), argv, fonts, origin))
            throw std::runtime_error(marcus_error(player.get()));
        std::exception_ptr exception;
        const int status = marcus_preview_run_frames(stdout, player.get(), scene, cameras, &exception, ReadPosition);
        if (exception) std::rethrow_exception(exception);
        return status;
    }
    catch (const std::exception &e) { return FrameError(e.what()); }
}

struct ThumbnailRenderer
{
    const char *resources;
    MM::Preview renderer;
    MM::Error error;
    MM::PreviewImage image;
    std::string path;
    std::exception_ptr exception;
    char failure[MARCUS_PREVIEW_MAX_TEXT + 2] = {};
    bool hasFailure = false;

    explicit ThumbnailRenderer(const char *resources) : resources(resources) {}

    template <class Effect> int Run(Effect effect) noexcept
    {
        try { return effect(); }
        catch (const std::exception &e) { std::snprintf(failure, sizeof(failure), "%s", e.what()); }
        catch (...) { std::snprintf(failure, sizeof(failure), "Preview renderer raised an unknown exception."); }
        hasFailure = true;
        return -1;
    }

    bool LoadText(const char *file)
    {
        return renderer.Open(file, resources, error) && renderer.Select({0x544d504c, 0}, error) &&
            renderer.UseTextPreviewMaterial(error);
    }

    bool Render(const MarcusThumbnailView &camera, MarcusPreviewPacket &packet)
    {
        MM::PreviewView view;
        view.width = camera.width;
        view.height = camera.height;
        view.yaw = camera.yaw;
        view.pitch = camera.pitch;
        view.zoom = camera.zoom;
        view.authoredCamera = false;
        if (!renderer.Render(view, image, error))
            return false;
        packet.width = image.width;
        packet.height = image.height;
        packet.pitch = std::size_t(image.width) * 4;
        packet.rgba = {image.rgba.data(), image.rgba.size()};
        return true;
    }

    static const char *Error(void *context) noexcept
    {
        const auto &host = *static_cast<ThumbnailRenderer *>(context);
        return host.hasFailure ? host.failure : host.error.message.c_str();
    }
};

int ReadModel(void *context, MMU32 *chunk, int *fits) noexcept
{
    auto &host = *static_cast<ThumbnailRenderer *>(context);
    host.hasFailure = false;
    try
    {
        int slot;
        long long value;
        if (!(std::cin >> slot >> value))
            return MARCUS_PREVIEW_END;
        *fits = value >= 0 && value <= 0xffffffffLL;
        *chunk = *fits ? static_cast<MMU32>(value) : 0;
        return MARCUS_PREVIEW_REQUEST;
    }
    catch (...) { host.exception = std::current_exception(); return MARCUS_PREVIEW_END; }
}

int RenderModel(void *context, MMU32 chunk, const MarcusThumbnailView *view, MarcusPreviewPacket *packet) noexcept
{
    auto &host = *static_cast<ThumbnailRenderer *>(context);
    return host.Run([&] { return host.renderer.Select({0x544d504c, chunk}, host.error) && host.Render(*view, *packet); });
}

int ModelPreviews(char **argv)
{
    try
    {
        ThumbnailRenderer host(argv[2]);
        if (!host.renderer.Open(argv[1], host.resources, host.error))
            throw std::runtime_error(host.error.message);
        const MarcusPreviewModelServices services{&host, ReadModel, RenderModel, ThumbnailRenderer::Error};
        const int status = marcus_preview_run_models(stdout, &services);
        if (!status && host.exception) std::rethrow_exception(host.exception);
        return status;
    }
    catch (const std::exception &e) { return FrameError(e.what()); }
}

int ReadText(void *context, MarcusPreviewTextRequest *request) noexcept
{
    auto &host = *static_cast<ThumbnailRenderer *>(context);
    try
    {
        std::string command;
        if (!(std::cin >> command))
            return MARCUS_PREVIEW_END;
        host.hasFailure = false;
        host.error.message.clear();
        request->load_path = nullptr;
        if (command == "load")
        {
            if (!(std::cin >> std::quoted(host.path)))
                return MARCUS_PREVIEW_INVALID;
            request->load_path = host.path.c_str();
        }
        auto &view = request->view;
        if ((command != "view" && command != "load") ||
            !(std::cin >> view.yaw >> view.pitch >> view.zoom >> view.width >> view.height))
            return MARCUS_PREVIEW_INVALID;
        return MARCUS_PREVIEW_REQUEST;
    }
    catch (...) { host.exception = std::current_exception(); return MARCUS_PREVIEW_END; }
}

int LoadText(void *context, const char *path) noexcept
{
    auto &host = *static_cast<ThumbnailRenderer *>(context);
    return host.Run([&] { return host.LoadText(path); });
}

int RenderText(void *context, const MarcusThumbnailView *view, MarcusPreviewPacket *packet) noexcept
{
    auto &host = *static_cast<ThumbnailRenderer *>(context);
    return host.Run([&] { return host.Render(*view, *packet); });
}

int TextPreviews(char **argv)
{
    try
    {
        ThumbnailRenderer host(argv[2]);
        if (!host.LoadText(argv[1]))
            throw std::runtime_error(host.error.message);
        const MarcusPreviewTextServices services{&host, ReadText, LoadText, RenderText, ThumbnailRenderer::Error};
        const int status = marcus_preview_run_text(stdout, &services);
        if (!status && host.exception) std::rethrow_exception(host.exception);
        return status;
    }
    catch (const std::exception &e) { return FrameError(e.what()); }
}
} // namespace

int main(int argc, char **argv)
{
    const bool duration = (argc == 5 || argc == 6) && !std::strcmp(argv[3], "--duration");
    const bool timeline = (argc == 6 || argc == 8 || argc == 9 || argc == 10) && !std::strcmp(argv[3], "--timing");
    const bool cameras = (argc == 6 || argc == 7 || argc == 8) && !std::strcmp(argv[3], "--cameras");
    const bool frames = cameras || ((argc == 6 || argc == 7 || argc == 8) && !std::strcmp(argv[3], "--frames"));
    const bool background = argc == 5 && !std::strcmp(argv[3], "--background");
    const bool prop = argc == 5 && !std::strcmp(argv[3], "--prop");
    const bool models = argc == 8 && !std::strcmp(argv[3], "--models");
    const bool text = argc == 5 && !std::strcmp(argv[3], "--text");
    const bool textLive = argc == 4 && !std::strcmp(argv[3], "--text-live");
    if (argc != 3 && !duration && !timeline && !frames && !background && !prop && !models && !text && !textLive) return 2;
#ifdef _WIN32
    _setmode(_fileno(stdout), _O_BINARY);
    if (frames || models || textLive) _setmode(_fileno(stdin), _O_BINARY);
#endif
    if (frames) return FramePreviews(argv, argc >= 7 ? argv[6] : nullptr, argc == 8 ? argv[7] : nullptr, cameras);
    if (models) return ModelPreviews(argv);
    if (textLive) return TextPreviews(argv);
    BrowserPreview preview;
    try
    {
        if (text)
        {
            MM::Preview renderer; MM::Error error; MM::PreviewView view; MM::PreviewImage image;
            view.width = 480; view.height = 360; view.authoredCamera = false;
            std::istringstream settings(argv[4]);
            settings >> view.yaw >> view.pitch >> view.zoom;
            if (!settings || !renderer.Open(argv[1], argv[2], error) || !renderer.Select({0x544d504c, 0}, error) ||
                !renderer.UseTextPreviewMaterial(error) || !renderer.Render(view, image, error)) preview.error = error.message.empty() ? "Cannot preview this text." : error.message;
            else { preview.width = image.width; preview.height = image.height; preview.rgba = std::move(image.rgba); }
        }
        else if (prop)
        {
            std::size_t end;
            const auto chunk = std::stoul(argv[1], &end);
            MM::Preview renderer;
            MM::Error error;
            MM::PreviewView view;
            MM::PreviewImage image;
            view.width = 480; view.height = 270;
            view.authoredCamera = false;
            view.yaw = 25; view.pitch = -10;
            if (argv[1][end] || chunk > 0xffffffffUL ||
                !renderer.Open((std::filesystem::u8path(argv[4]) / "tmpls.3cn").u8string(), argv[2], error) ||
                !renderer.Select({0x544d504c, static_cast<std::uint32_t>(chunk)}, error) ||
                !renderer.Render(view, image, error))
                preview.error = error.message.empty() ? "Cannot preview the selected object." : error.message;
            else
            {
                preview.width = image.width; preview.height = image.height;
                preview.rgba = std::move(image.rgba);
            }
        }
        else if (background)
        {
            std::size_t end;
            const auto chunk = std::stoul(argv[1], &end);
            std::unique_ptr<MarcusPlayer, decltype(&marcus_destroy)> player(marcus_create(), marcus_destroy);
            MarcusImage image = {};
            if (!player || argv[1][end] || !marcus_add_scene(player.get(), chunk, argv[2], argv[4]) ||
                !marcus_render(player.get(), 480, 270, 1, 0, 0, 1, &image))
                preview.error = marcus_error(player.get());
            else
            {
                preview.width = image.width;
                preview.height = image.height;
                preview.rgba.assign(image.rgba, image.rgba + image.pitch * image.height);
            }
        }
        else if (duration || timeline)
        {
            const auto started = std::chrono::steady_clock::now();
            auto player = marcus_create();
            if (!player) preview.error = "Cannot initialize duration analysis.";
            else
            {
                marcus_set_muted(player, 1);
                MarcusMovieTime timing = {};
                if (timeline && argc >= 8)
                {
                    std::size_t rateEnd, modifierEnd;
                    const int rate = std::stoi(argv[6], &rateEnd), modifier = std::stoi(argv[7], &modifierEnd);
                    if (argv[6][rateEnd] || argv[7][modifierEnd] || !marcus_set_playback_rate(player, rate, modifier))
                    {
                        marcus_destroy(player);
                        return 2;
                    }
                }
                const char *fonts = timeline && argc >= 9 ? argv[8] : duration && argc == 6 ? argv[5] : nullptr;
                if (!OpenPlayer(player, argv, fonts, timeline && argc == 10 ? argv[9] : nullptr) || !marcus_movie_time(player, &timing))
                    preview.error = marcus_error(player);
                else
                {
                    if (timeline)
                    {
                        std::ofstream output(std::filesystem::u8path(argv[5]), std::ios::binary);
                        const bool written = MarcusWriteTiming(player, output);
                        output.close();
                        marcus_destroy(player);
                        return written && !output.fail() ? 0 : 1;
                    }
                    const auto seconds = static_cast<long long>(std::llround(timing.duration_seconds));
                    char value[192];
                    const auto elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
                    if (seconds >= 3600)
                        std::snprintf(value, sizeof(value), "Calculated %lld:%02lld:%02lld\nAnalysis %.1fs; click waits excluded", seconds / 3600, seconds / 60 % 60, seconds % 60, elapsed);
                    else
                        std::snprintf(value, sizeof(value), "Calculated %lld:%02lld\nAnalysis %.1fs; click waits excluded", seconds / 60, seconds % 60, elapsed);
                    preview.details = value;
                }
                marcus_destroy(player);
            }
        }
        else preview = ReadBrowserPreview(argv[1], argv[2]);
    }
    catch (const std::exception &e) { preview.error = e.what(); }
    if (timeline) return 1;
    return WritePreview(stdout, preview) ? 0 : 1;
}
