#include "asm_renderer.h"
#include "asm_protocol.h"
#include "player.h"
#include "movie_input.h"
#include <cstdio>
#include <cstring>
#include <exception>
#include <memory>
#include <fcntl.h>
#include <io.h>
#ifndef MARCUS_X86_ASM
#error The ASM worker must link the original 32-bit MASM implementation.
#endif

namespace
{
struct WorkerHost
{
    MarcusPlayer *player;
    MM::Error error;
    MM::PreviewImage image;
    char exceptionMessage[1024] = {};

    template <class Effect> int Run(Effect effect) noexcept
    {
        try { return effect(); }
        catch (const std::exception &e)
        {
            std::snprintf(exceptionMessage, sizeof(exceptionMessage), "%s", e.what());
        }
        catch (...)
        {
            std::snprintf(exceptionMessage, sizeof(exceptionMessage), "ASM renderer raised an unknown exception.");
        }
        return 0;
    }
};

void Begin(void *context) noexcept
{
    auto &host = *static_cast<WorkerHost *>(context);
    host.error = {};
    host.exceptionMessage[0] = '\0';
}

int ApplyEdits(void *context, const MarcusASMRequest *request, const MMPreviewLiveScene *scenes,
               const MMPreviewLiveProp *props, const MMMovieCameraEdit *cameras) noexcept
{
    auto &host = *static_cast<WorkerHost *>(context);
    return host.Run([&] { return MarcusApplyLiveEdits(host.player, *request, scenes, props, cameras, host.error); });
}

int ApplyRange(void *context, MMI32 first, MMI32 last) noexcept
{
    auto &host = *static_cast<WorkerHost *>(context);
    return host.Run([&] { return MarcusApplySceneRange(host.player, first, last, host.error); });
}

int ApplyPlacements(void *context, const MMMovieActorPlacement *offsets, MMU32 count) noexcept
{
    auto &host = *static_cast<WorkerHost *>(context);
    return host.Run([&] { return MarcusApplyPropOffsets(host.player, offsets, count, host.error); });
}

int Render(void *context, const MarcusASMRequest *request, MarcusASMOutput *frame) noexcept
{
    auto &host = *static_cast<WorkerHost *>(context);
    if (!host.Run([&] { return MarcusRenderLocalWorld(host.player, MarcusASM::Decode(*request), host.image, host.error); }))
        return 0;
    const auto &image = host.image;
    frame->reply.width = image.width;
    frame->reply.height = image.height;
    frame->reply.covered = image.coveredPixels;
    frame->reply.depth = !image.depth.empty();
    frame->reply.camera = MarcusASM::Encode(image.camera);
    frame->reply.projection = MarcusASM::Encode(image.projection);
    frame->reply.scene_camera = MarcusASM::Encode(image.sceneCamera);
    frame->rgba = image.rgba.data();
    frame->rgba_bytes = image.rgba.size();
    frame->depth = image.depth.data();
    frame->depth_bytes = image.depth.size() * sizeof(float);
    return 1;
}

const char *ErrorMessage(void *context, size_t *bytes) noexcept
{
    const auto &host = *static_cast<WorkerHost *>(context);
    if (host.exceptionMessage[0])
    {
        *bytes = std::strlen(host.exceptionMessage);
        return host.exceptionMessage;
    }
    *bytes = host.error.message.size();
    return host.error.message.c_str();
}
} // namespace

int main(int argc, char **argv)
{
    if (argc != 4 && argc != 5)
        return 2;
    _setmode(_fileno(stdin), _O_BINARY);
    _setmode(_fileno(stdout), _O_BINARY);
    std::unique_ptr<MarcusPlayer, decltype(&marcus_destroy)> player(marcus_create(), marcus_destroy);
    MM::Error openingError;
    bool opened = false;
    if (player && argc == 5)
    {
        try
        {
            MarcusMovieInput input; input.Open(argv[1]);
            const auto file = input.Materialize(0);
            file->contentOrigin = std::filesystem::u8path(argv[4]);
            opened = marcus_open_input(player.get(), &input, 0, argv[2], argv[3]) != 0;
        }
        catch (const std::exception &error) { openingError.message = error.what(); }
    }
    else if (player)
        opened = argv[1][0] ? marcus_open(player.get(), argv[1], argv[2], argv[3]) != 0
                            : MarcusPrepareEmpty(player.get(), argv[2], argv[3], openingError);
    if (!opened)
    {
        std::fprintf(stderr, "%s%s\n", player ? marcus_error(player.get()) : "Cannot create ASM player",
                     openingError.message.c_str());
        return 1;
    }
    marcus_set_muted(player.get(), 1);
    WorkerHost host{player.get(), {}, {}};
    const MarcusASMWorkerServices services = {&host, Begin, ApplyEdits, ApplyRange, ApplyPlacements, Render, ErrorMessage};
    return marcus_asm_worker_run(player.get(), stdin, stdout, &services);
}
