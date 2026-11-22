#include "preview_commands.h"
#include <float.h>
#include <math.h>

static const char *error_or(const char *message, const char *fallback)
{
    return *message ? message : fallback;
}

static void renderer_error(MarcusPreviewPacket *packet, int status, const char *message, const char *fallback)
{
    marcus_preview_packet_error(packet, status < 0 ? message : error_or(message, fallback));
}

static int bound_dimension(float size, int limit)
{
    const int pixels = (int)floor(size);
    if (pixels < 1)
        return 1;
    return pixels > limit ? limit : pixels;
}

static void frame_pixels(const MarcusImage *image, MarcusPreviewPacket *packet)
{
    const float aspect = image->scene_camera.aspect;
    int left, top;
    int width = image->width, height = image->height;
    /* Remove the fitted canvas while keeping all pixels authored inside it. */
    if (image->scene_camera.valid && aspect > 0 && aspect <= FLT_MAX)
    {
        if (aspect >= (float)width / height)
            height = bound_dimension(width / aspect + 0.001f, height);
        else
            width = bound_dimension(height * aspect + 0.001f, width);
    }
    left = (image->width - width) / 2;
    top = (image->height - height) / 2;
    packet->width = (MMU32)width;
    packet->height = (MMU32)height;
    packet->pitch = (size_t)image->pitch;
    packet->rgba.data = image->rgba + (size_t)top * image->pitch + (size_t)left * 4;
    packet->rgba.size = (size_t)width * height * 4;
}

int marcus_preview_run_frames(FILE *output, MarcusPlayer *player, int selected_scene, int cameras,
                              void *input, MarcusPreviewPositionRead read)
{
    int scene, fits;
    long frame;
    if (!marcus_select_scene(player, selected_scene))
    {
        MarcusPreviewPacket failure;
        marcus_preview_packet_error(&failure, marcus_error(player));
        marcus_preview_wire_write_file(&failure, 1, output);
        return 1;
    }
    while (read(input, &scene, &frame, &fits) == MARCUS_PREVIEW_REQUEST)
    {
        MarcusPreviewPacket packet = {0};
        MarcusImage image = {0};
        int selected = 1;
        if (!cameras && scene != selected_scene)
        {
            selected = marcus_select_scene(player, scene);
            if (selected)
                selected_scene = scene;
        }
        if (selected && fits && marcus_seek(player, frame) &&
            (cameras ? scene >= 0 && marcus_camera_thumbnail(player, scene, &image)
                     : marcus_render(player, 160, 120, 1, 0, 0, 1, &image)))
            frame_pixels(&image, &packet);
        else
            marcus_preview_packet_error(&packet, error_or(marcus_error(player), "Invalid preview frame."));
        if (!marcus_preview_wire_write_file(&packet, 1, output))
            return 1;
    }
    return 0;
}

int marcus_preview_run_models(FILE *output, const MarcusPreviewModelServices *services)
{
    static const MarcusThumbnailView view = {25, -10, 1, 240, 180};
    MMU32 chunk;
    int fits;
    while (services->read(services->context, &chunk, &fits) == MARCUS_PREVIEW_REQUEST)
    {
        MarcusPreviewPacket packet = {0};
        const int status = fits ? services->render(services->context, chunk, &view, &packet) : 0;
        if (status <= 0)
            renderer_error(&packet, status, services->error(services->context), "Cannot preview this model.");
        if (!marcus_preview_wire_write_file(&packet, 1, output))
            return 1;
    }
    return 0;
}

static int valid_text_view(const MarcusThumbnailView *view)
{
    return view->yaw >= -FLT_MAX && view->yaw <= FLT_MAX && view->pitch >= -89 && view->pitch <= 89 &&
        view->zoom >= 0.25f && view->zoom <= 4 && view->width >= 1 && view->width <= 1024 &&
        view->height >= 1 && view->height <= 1024;
}

int marcus_preview_run_text(FILE *output, const MarcusPreviewTextServices *services)
{
    MarcusPreviewTextRequest request;
    int read;
    while ((read = services->read(services->context, &request)) == MARCUS_PREVIEW_REQUEST)
    {
        MarcusPreviewPacket packet = {0};
        int status = request.load_path ? services->load(services->context, request.load_path) : 1;
        if (status <= 0)
            renderer_error(&packet, status, services->error(services->context), "Cannot load this text preview.");
        else if (!valid_text_view(&request.view))
            marcus_preview_packet_error(&packet, "Invalid text preview camera.");
        else
        {
            status = services->render(services->context, &request.view, &packet);
            if (status <= 0)
                renderer_error(&packet, status, services->error(services->context), "Cannot preview this text.");
        }
        if (!marcus_preview_wire_write_file(&packet, 1, output) || status < 0)
            return 1;
    }
    return read == MARCUS_PREVIEW_INVALID ? 2 : 0;
}
