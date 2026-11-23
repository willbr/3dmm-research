#include "preview_worker.h"
#include "preview_process.h"
#include <exception>
#include <memory>
#include <stdexcept>

namespace
{
enum class PreviewKind { browser, frame, text };

struct Cancellation
{
    const std::function<bool()> &test;
    std::exception_ptr failure;

    static int Check(void *p)
    {
        auto &self = *static_cast<Cancellation *>(p);
        try { return self.test(); }
        catch (...) { self.failure = std::current_exception(); return 1; }
    }
};

const char *TransferError(int status, PreviewKind kind)
{
    switch (status)
    {
    case MARCUS_PREVIEW_TRANSFER_START: return "Cannot launch marcus-preview. Keep it beside Marcus.";
    case MARCUS_PREVIEW_TRANSFER_IO:
        if (kind == PreviewKind::browser) return "Cannot read the preview helper output.";
        return kind == PreviewKind::text ? "Cannot communicate with the text preview helper."
                                         : "Cannot communicate with the frame preview helper.";
    case MARCUS_PREVIEW_TRANSFER_CANCELLED:
        if (kind == PreviewKind::browser) return "Preview cancelled or timed out.";
        return kind == PreviewKind::text ? "Text preview cancelled or timed out." : "Frame preview cancelled or timed out.";
    case MARCUS_PREVIEW_TRANSFER_WRITE:
        if (kind == PreviewKind::frame) return "Cannot request a frame thumbnail.";
        return "The text preview helper stopped unexpectedly.";
    case MARCUS_PREVIEW_TRANSFER_STOPPED:
        return kind == PreviewKind::text ? "The text preview helper stopped unexpectedly."
                                         : "The frame preview helper stopped unexpectedly.";
    case MARCUS_PREVIEW_TRANSFER_LIMIT:
        if (kind == PreviewKind::browser) return "Preview helper output exceeds its limit.";
        return kind == PreviewKind::text ? "Text preview response exceeds its limit."
                                         : "Frame preview response exceeds its limit.";
    case MARCUS_PREVIEW_TRANSFER_EXIT: return "The movie preview could not be decoded.";
    case MARCUS_PREVIEW_TRANSFER_PACKET: return "Preview helper response exceeds its limits.";
    default: return "Invalid preview helper response.";
    }
}

BrowserPreview PublishPreview(int status, const MarcusPreviewReply &reply, const Cancellation &cancel, PreviewKind kind)
{
    if (cancel.failure) std::rethrow_exception(cancel.failure);
    if (status == MARCUS_PREVIEW_TRANSFER_MEMORY) throw std::bad_alloc();
    if (status != MARCUS_PREVIEW_TRANSFER_OK) throw std::runtime_error(TransferError(status, kind));
    return PreviewFromPacket(reply.packet);
}
}

BrowserPreview RunPreviewWorker(const std::string &movie, const std::string &resources,
                               const std::function<bool()> &cancelled, const std::string &content, bool duration,
                               const std::string &fonts, bool background, bool prop, bool text)
{
    const std::unique_ptr<char, decltype(&SDL_free)> executable(marcus_preview_executable(), SDL_free);
    if (!executable) throw std::runtime_error("Cannot locate the preview helper.");
    const char *args[] = {executable.get(), movie.c_str(), resources.c_str(), text ? "--text" : prop ? "--prop" : background ? "--background" : duration ? "--duration" : nullptr,
                          content.c_str(), (background || prop || text) ? nullptr : fonts.c_str(), nullptr};
    MarcusPreviewReply reply{};
    const std::unique_ptr<MarcusPreviewReply, decltype(&marcus_preview_reply_dispose)> owner(&reply,
                                                                                           marcus_preview_reply_dispose);
    Cancellation cancel{cancelled, {}};
    const int status = marcus_preview_process_capture(args, duration, &cancel, Cancellation::Check, &reply);
    return PublishPreview(status, reply, cancel, PreviewKind::browser);
}

BrowserPreview RequestPreviewWorker(SDL_Process *process, const std::string &request, bool text,
                                    const std::function<bool()> &cancelled)
{
    MarcusPreviewReply reply{};
    const std::unique_ptr<MarcusPreviewReply, decltype(&marcus_preview_reply_dispose)> owner(&reply,
                                                                                           marcus_preview_reply_dispose);
    Cancellation cancel{cancelled, {}};
    const int status = marcus_preview_process_request(process, request.data(), request.size(), text,
                                                       &cancel, Cancellation::Check, &reply);
    return PublishPreview(status, reply, cancel, text ? PreviewKind::text : PreviewKind::frame);
}
