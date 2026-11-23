#include "preview_worker.h"
#include <exception>
#include <ostream>
#include <stdexcept>

namespace
{
MarcusPreviewPacket Packet(const BrowserPreview &preview)
{
    return {static_cast<MMU32>(preview.width), static_cast<MMU32>(preview.height),
            {preview.title.data(), preview.title.size()}, {preview.details.data(), preview.details.size()},
            {preview.error.data(), preview.error.size()}, {preview.rgba.data(), preview.rgba.size()},
            std::size_t(preview.width) * 4};
}
}

bool WritePreview(std::ostream &output, const BrowserPreview &preview)
{
    struct Stream
    {
        std::ostream &output;
        std::exception_ptr exception;
    } stream{output, {}};
    const auto packet = Packet(preview);
    const int result = marcus_preview_wire_write(&packet, 0, &stream,
        [](void *context, const void *bytes, std::size_t size) -> int {
            auto &stream = *static_cast<Stream *>(context);
            try { return bool(stream.output.write(static_cast<const char *>(bytes), size)); }
            catch (...) { stream.exception = std::current_exception(); return 0; }
        });
    if (stream.exception)
        std::rethrow_exception(stream.exception);
    return result != 0;
}

bool WritePreview(FILE *output, const BrowserPreview &preview)
{
    const auto packet = Packet(preview);
    return marcus_preview_wire_write_file(&packet, 0, output) != 0;
}

BrowserPreview ReadPreview(const std::vector<std::uint8_t> &bytes)
{
    MarcusPreviewPacket packet;
    const auto status = marcus_preview_wire_read(bytes.data(), bytes.size(), &packet);
    if (status == MARCUS_PREVIEW_WIRE_HEADER)
        throw std::runtime_error("Invalid preview helper response.");
    if (status != MARCUS_PREVIEW_WIRE_OK)
        throw std::runtime_error("Preview helper response exceeds its limits.");
    return PreviewFromPacket(packet);
}

BrowserPreview PreviewFromPacket(const MarcusPreviewPacket &packet)
{
    BrowserPreview result;
    result.width = packet.width;
    result.height = packet.height;
    result.title.assign(static_cast<const char *>(packet.title.data), packet.title.size);
    result.details.assign(static_cast<const char *>(packet.details.data), packet.details.size);
    result.error.assign(static_cast<const char *>(packet.error.data), packet.error.size);
    const auto pixels = static_cast<const std::uint8_t *>(packet.rgba.data);
    result.rgba.assign(pixels, pixels + packet.rgba.size);
    return result;
}
