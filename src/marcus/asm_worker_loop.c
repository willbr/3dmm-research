#include "asm_wire.h"
#include "player.h"
#include <stdlib.h>
#include <string.h>

typedef char RequestLayout[(sizeof(MarcusASMRequest) == 104 &&
                            offsetof(MarcusASMRequest, edit_revision_low) == 96) ? 1 : -1];
typedef char ReplyLayout[(sizeof(MarcusASMReply) == 216 && offsetof(MarcusASMReply, scene_camera) == 136 &&
                          offsetof(MarcusASMProjection, orthographic) == 72) ? 1 : -1];
typedef char PayloadLayout[(sizeof(MMMovieActorPlacement) == 64 && sizeof(MMPreviewLiveScene) == 12 &&
                            sizeof(MMPreviewLiveProp) == 40 && sizeof(MMMovieCameraEdit) == 12) ? 1 : -1];

static int valid_request(const MarcusASMRequest *request)
{
    return request->magic == MARCUS_ASM_MAGIC && request->width >= 64 && request->height >= 64 &&
           request->width <= 4096 && request->height <= 4096 && request->prop_count <= 100000UL &&
           request->scene_count <= 100000UL && request->added_prop_count <= 100000UL && request->camera_count <= 100000UL;
}

static int apply_request(MarcusPlayer *player, const MarcusASMRequest *request, const unsigned char *payload,
                         const MarcusASMWorkerServices *s, MarcusASMOutput *frame)
{
    const size_t scenes_at = (size_t)request->prop_count * sizeof(MMMovieActorPlacement);
    const size_t props_at = scenes_at + (size_t)request->scene_count * sizeof(MMPreviewLiveScene);
    const size_t cameras_at = props_at + (size_t)request->added_prop_count * sizeof(MMPreviewLiveProp);
    MarcusInfo info = {0};
    s->begin(s->context);
    if ((request->flags & MARCUS_ASM_EDIT_DEFINITIONS) &&
        !s->edits(s->context, request,
                  request->scene_count ? (const MMPreviewLiveScene *)(payload + scenes_at) : NULL,
                  request->added_prop_count ? (const MMPreviewLiveProp *)(payload + props_at) : NULL,
                  request->camera_count ? (const MMMovieCameraEdit *)(payload + cameras_at) : NULL))
        return 0;
    if ((!marcus_info(player, &info) || info.scene != request->scene) && !marcus_select_scene(player, request->scene))
        return 0;
    if (request->first_frame <= request->last_frame && !s->range(s->context, request->first_frame, request->last_frame))
        return 0;
    marcus_info(player, &info);
    if (info.frame != request->frame && !marcus_seek(player, request->frame))
        return 0;
    return s->placements(s->context, (const MMMovieActorPlacement *)payload, request->prop_count) &&
           s->render(s->context, request, frame);
}

static int write_frame(FILE *stream, MarcusPlayer *player, const MarcusASMWorkerServices *s,
                       MarcusASMOutput *frame, int rendered)
{
    const char *error = NULL;
    size_t error_bytes = 0;
    if (!rendered)
    {
        error = s->error(s->context, &error_bytes);
        if (!error_bytes)
        {
            error = marcus_error(player);
            if (!*error) error = "ASM geometry rendering failed.";
            error_bytes = strlen(error);
        }
        memset(&frame->reply, 0, sizeof(frame->reply));
        frame->reply.error_bytes = (MMU32)error_bytes;
        frame->reply.camera.move_speed = 1;
        frame->reply.camera.vertical_fov = 45;
    }
    frame->reply.magic = MARCUS_ASM_MAGIC;
    if (fwrite(&frame->reply, sizeof(frame->reply), 1, stream) != 1)
        return 0;
    if (rendered)
    {
        if (fwrite(frame->rgba, 1, frame->rgba_bytes, stream) != frame->rgba_bytes ||
            (frame->depth_bytes && fwrite(frame->depth, 1, frame->depth_bytes, stream) != frame->depth_bytes))
            return 0;
    }
    else if (fwrite(error, 1, error_bytes, stream) != error_bytes)
        return 0;
    return fflush(stream) == 0;
}

int marcus_asm_worker_run(MarcusPlayer *player, FILE *input, FILE *output, const MarcusASMWorkerServices *s)
{
    static const MMU32 hello[] = {MARCUS_ASM_MAGIC, 32};
    unsigned char *payload = NULL;
    size_t capacity = 0;
    int status = 1;
    MarcusASMRequest request;
    if (fwrite(hello, sizeof(hello), 1, output) != 1 || fflush(output))
        return 1;
    while (fread(&request, sizeof(request), 1, input) == 1)
    {
        MarcusASMOutput frame = {0};
        size_t bytes;
        int rendered;
        if (!valid_request(&request))
        {
            status = 2;
            goto done;
        }
        bytes = (size_t)request.prop_count * sizeof(MMMovieActorPlacement) +
                (size_t)request.scene_count * sizeof(MMPreviewLiveScene) +
                (size_t)request.added_prop_count * sizeof(MMPreviewLiveProp) +
                (size_t)request.camera_count * sizeof(MMMovieCameraEdit);
        if (bytes > capacity)
        {
            unsigned char *grown = realloc(payload, bytes);
            if (!grown)
                goto done;
            payload = grown;
            capacity = bytes;
        }
        if (bytes && fread(payload, 1, bytes, input) != bytes)
        {
            status = 2;
            goto done;
        }
        rendered = apply_request(player, &request, payload, s, &frame);
        if (!write_frame(output, player, s, &frame, rendered))
            goto done;
    }
    status = feof(input) ? 0 : 1;
done:
    free(payload);
    return status;
}
