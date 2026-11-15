#include "asm_renderer.h"
#include "asm_protocol.h"
#include "player.h"
#include <iostream>
#include <memory>
#include <fcntl.h>
#include <io.h>
#ifndef MARCUS_X86_ASM
#error The ASM worker must link the original 32-bit MASM implementation.
#endif
int main(int argc, char **argv)
{
    if (argc != 4)
        return 2;
    _setmode(_fileno(stdin), _O_BINARY);
    _setmode(_fileno(stdout), _O_BINARY);
    std::unique_ptr<MarcusPlayer, decltype(&marcus_destroy)> player(marcus_create(), marcus_destroy);
    const std::uint32_t hello[2] = {MarcusASM::Magic, 32};
    MM::Error openingError;
    if (!player || (argv[1][0] ? !marcus_open(player.get(), argv[1], argv[2], argv[3])
                             : !MarcusPrepareEmpty(player.get(), argv[2], argv[3], openingError))) return 1;
    marcus_set_muted(player.get(), 1);
    std::cout.write(reinterpret_cast<const char *>(hello), sizeof(hello)).flush();
    MarcusASM::Request request;
    while (std::cin.read(reinterpret_cast<char *>(&request), sizeof(request)))
    {
        if (request.magic != MarcusASM::Magic || request.width < 64 || request.height < 64 || request.width > 4096 ||
            request.height > 4096 || request.propCount > 100000 || request.sceneCount > 100000 ||
            request.addedPropCount > 100000 || request.cameraCount > 100000)
            return 2;
        std::vector<MarcusASM::PropOffset> offsets(request.propCount);
        if (!offsets.empty() && !std::cin.read(reinterpret_cast<char *>(offsets.data()), offsets.size() * sizeof(offsets[0])))
            return 2;
        std::vector<MM::PreviewSceneDefinition> scenes(request.sceneCount);
        std::vector<MM::PreviewPropDefinition> props(request.addedPropCount);
        std::vector<MarcusASM::CameraEdit> cameras(request.cameraCount);
        for (auto &scene : scenes) if (!std::cin.read(reinterpret_cast<char *>(&scene), sizeof(scene))) return 2;
        for (auto &prop : props) if (!std::cin.read(reinterpret_cast<char *>(&prop), sizeof(prop))) return 2;
        for (auto &camera : cameras) if (!std::cin.read(reinterpret_cast<char *>(&camera), sizeof(camera))) return 2;
        MM::PreviewImage image;
        MM::Error error;
        MarcusInfo info = {};
        bool ok = !(request.flags & 256) || MarcusApplyLiveEdits(player.get(), scenes, props, cameras, request.editRevision, error);
        const bool loaded = marcus_info(player.get(), &info) != 0;
        if (ok) ok = (loaded && info.scene == request.scene) || marcus_select_scene(player.get(), request.scene);
        if (ok && request.firstFrame <= request.lastFrame)
            ok = MarcusApplySceneRange(player.get(), request.firstFrame, request.lastFrame, error);
        if (ok)
        {
            marcus_info(player.get(), &info);
            ok = info.frame == request.frame || marcus_seek(player.get(), request.frame);
        }
        if (ok) ok = MarcusApplyPropOffsets(player.get(), offsets, error);
        if (ok)
            ok = MarcusRenderLocalWorld(player.get(), MarcusASM::Decode(request), image, error);
        if (!ok && error.message.empty())
            error.message = marcus_error(player.get());
        if (!ok && error.message.empty())
            error.message = "ASM geometry rendering failed.";
        MarcusASM::Reply reply = {};
        if (!ok)
            reply.errorBytes = static_cast<std::uint32_t>(error.message.size());
        else
        {
            reply.width = image.width;
            reply.height = image.height;
            reply.covered = image.coveredPixels;
            reply.depth = !image.depth.empty();
            reply.camera = image.camera;
            reply.projection = image.projection;
            reply.sceneCamera = image.sceneCamera;
        }
        std::cout.write(reinterpret_cast<const char *>(&reply), sizeof(reply));
        if (!ok)
            std::cout.write(error.message.data(), error.message.size());
        else
        {
            std::cout.write(reinterpret_cast<const char *>(image.rgba.data()), image.rgba.size());
            if (reply.depth)
                std::cout.write(reinterpret_cast<const char *>(image.depth.data()), image.depth.size() * sizeof(float));
        }
        std::cout.flush();
        if (!std::cout)
            return 1;
    }
    return std::cin.eof() ? 0 : 1;
}
