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
    if (!player || !marcus_open(player.get(), argv[1], argv[2], argv[3]))
        return 1;
    marcus_set_muted(player.get(), 1);
    std::cout.write(reinterpret_cast<const char *>(hello), sizeof(hello)).flush();
    MarcusASM::Request request;
    while (std::cin.read(reinterpret_cast<char *>(&request), sizeof(request)))
    {
        if (request.magic != MarcusASM::Magic || request.width < 64 || request.height < 64 || request.width > 4096 ||
            request.height > 4096)
            return 2;
        MM::PreviewImage image;
        MM::Error error;
        MarcusInfo info = {};
        marcus_info(player.get(), &info);
        bool ok =
            (info.scene == request.scene || marcus_select_scene(player.get(), request.scene)) &&
            (info.scene == request.scene && info.frame == request.frame || marcus_seek(player.get(), request.frame));
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
