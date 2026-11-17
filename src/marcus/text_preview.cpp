#include "text_preview.h"
#include "storyboard.h"
#include "preview_worker.h"
#include "colour_picker.h"
#include <atomic>
#include <future>
#include <sstream>

struct MarcusTextPreview
{
    std::shared_ptr<std::atomic<bool>> cancelled;
    std::future<BrowserPreview> pending;
    BrowserPreview result;
    std::string error;
    void Stop()
    {
        if (cancelled) *cancelled = true;
        if (pending.valid()) pending.wait();
        pending = {}; cancelled.reset(); result = {};
    }
    ~MarcusTextPreview() { Stop(); }
};

extern "C" {
MarcusTextPreview *text_preview_create(void)
{
    try { return new MarcusTextPreview; } catch (...) { return nullptr; }
}
void text_preview_destroy(MarcusTextPreview *preview) { delete preview; }
void text_preview_stop(MarcusTextPreview *preview) { if (preview) preview->Stop(); }
const char *text_preview_error(const MarcusTextPreview *preview)
{ return preview ? preview->error.c_str() : "Cannot allocate the preview worker."; }
int text_preview_start(MarcusTextPreview *preview, MarcusPlayer *player, int object,
                       const MarcusText *text, const char *resources, float yaw, float pitch, float zoom)
{
    if (!preview || !text || !resources) return 0;
    preview->Stop(); preview->error.clear();
    try
    {
        auto file = MarcusPrepareTextPreview(player, object, *text);
        if (!file) { preview->error = marcus_error(player); return 0; }
        preview->cancelled = std::make_shared<std::atomic<bool>>(false);
        const auto cancel = preview->cancelled;
        const std::string resource = resources;
        std::ostringstream camera; camera << yaw << ' ' << pitch << ' ' << zoom;
        const auto view = camera.str();
        preview->pending = std::async(std::launch::async, [file, cancel, resource, view] {
            return RunPreviewWorker(file->path.u8string(), resource, [cancel] { return cancel->load(); }, view,
                                    false, {}, false, false, true);
        });
        return 1;
    }
    catch (const std::exception &e) { preview->error = e.what(); }
    catch (...) { preview->error = "Cannot start the text preview."; }
    return 0;
}
int text_preview_poll(MarcusTextPreview *preview, MarcusImage *image)
{
    if (!preview || !image || !preview->pending.valid() ||
        preview->pending.wait_for(std::chrono::seconds(0)) != std::future_status::ready) return 0;
    try
    {
        preview->result = preview->pending.get();
        preview->error = preview->result.error;
        if (preview->result.rgba.empty()) return -1;
        *image = {};
        image->width = preview->result.width; image->height = preview->result.height;
        image->pitch = image->width * 4; image->rgba = preview->result.rgba.data();
        return 1;
    }
    catch (const std::exception &e) { preview->error = e.what(); }
    catch (...) { preview->error = "Cannot read the text preview."; }
    return -1;
}
int text_preview_palette(MarcusPlayer *player, unsigned rgb[256], float hue[256])
{
    if (!rgb || !hue) return 0;
    try
    {
        MarcusBoardPalette palette = {};
        if (!MarcusGetStoryboardPalette(player, palette)) return 0;
        for (unsigned i = 0; i < palette.size(); ++i)
        { rgb[i] = palette[i]; hue[i] = MarcusToOkHSL(palette[i])[0]; }
        return 1;
    }
    catch (...) { return 0; }
}
}
