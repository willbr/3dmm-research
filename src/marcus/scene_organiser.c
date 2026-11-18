#include "dialog_host.h"
#include "scene_organiser.h"
#include <limits.h>
#include <stdlib.h>
#include <string.h>

enum { SCENE_THUMBNAIL_LIMIT = 64, FRAME_THUMBNAIL_LIMIT = 4096 };

typedef struct Thumbnail
{
    int index;
    unsigned long used;
    SDL_Texture *texture;
} Thumbnail;

typedef struct SceneFrames
{
    long first;
    int count, offset;
} SceneFrames;

struct MarcusSceneOrganiser
{
    MarcusPlayer *player;
    int open, count, current, selected, result;
    Thumbnail thumbnails[FRAME_THUMBNAIL_LIMIT];
    Thumbnail *thumbnail_order[FRAME_THUMBNAIL_LIMIT];
    int thumbnail_count, loaded_this_frame;
    unsigned long clock;
    int frames, source_scene, scene_count, movie_frame_count, whole_movie;
    SceneFrames *scenes;
    MarcusFramePreviews *frame_previews;
    char frame_error[256];
    float frame_zoom;
    int preview_width, preview_height, preview_size_known, fit_frames;
};

static const char *transition_name(int transition)
{
    switch (transition)
    {
    case MARCUS_TRANSITION_NONE: return "None";
    case MARCUS_TRANSITION_CUT: return "Cut";
    case MARCUS_TRANSITION_DISSOLVE: return "Dissolve";
    case MARCUS_TRANSITION_FADE_BLACK: return "Fade to black";
    case MARCUS_TRANSITION_FADE_WHITE: return "Fade to white";
    case MARCUS_TRANSITION_BLACK: return "Black";
    default: return "Unknown";
    }
}

static float minimum_zoom(const MarcusSceneOrganiser *s)
{
    float width, height, minimum;
    width = 16.0f / s->preview_width;
    height = 9.0f / s->preview_height;
    minimum = width > height ? width : height;
    if (minimum < 0.10f) return 0.10f;
    return minimum < 1.0f ? minimum : 1.0f;
}

static float clamp_zoom(const MarcusSceneOrganiser *s, float zoom)
{
    float minimum;
    minimum = minimum_zoom(s);
    if (zoom < minimum) return minimum;
    return zoom > 1.0f ? 1.0f : zoom;
}

static int frame_count(long first, long last)
{
    unsigned long distance;
    if (last < first) return 0;
    distance = (unsigned long)last - (unsigned long)first;
    return distance < INT_MAX ? (int)distance + 1 : 0;
}

static MarcusFramePosition position_at(const MarcusSceneOrganiser *s, int index)
{
    MarcusFramePosition position;
    int first, end, middle;
    position.scene = s->source_scene;
    if (s->whole_movie)
    {
        first = 1;
        end = s->scene_count;
        while (first < end)
        {
            middle = first + (end - first) / 2;
            if (s->scenes[middle].offset <= index) first = middle + 1;
            else end = middle;
        }
        position.scene = first - 1;
        index -= s->scenes[position.scene].offset;
    }
    position.frame = s->scenes[position.scene].first + index;
    return position;
}

static int index_at(const MarcusSceneOrganiser *s, int scene, long frame)
{
    const SceneFrames *range;
    unsigned long local;
    if (scene < 0 || scene >= s->scene_count || (!s->whole_movie && scene != s->source_scene)) return -1;
    range = &s->scenes[scene];
    if (frame < range->first) return -1;
    local = (unsigned long)frame - (unsigned long)range->first;
    if (local >= (unsigned long)range->count) return -1;
    return (int)local + (s->whole_movie ? range->offset : 0);
}

static void clear_thumbnails(MarcusSceneOrganiser *s)
{
    int i;
    for (i = 0; i < s->thumbnail_count; ++i) dialog_retire_texture(s->thumbnails[i].texture);
    s->thumbnail_count = 0;
    s->clock = 0;
}

/* Keep lookups logarithmic when a zoomed-out grid shows thousands of tiles.
 * Only cache insertions shift entries. The number of uploads per draw is bounded. */
static int thumbnail_slot(const MarcusSceneOrganiser *s, int index)
{
    int first, end, middle;
    first = 0;
    end = s->thumbnail_count;
    while (first < end)
    {
        middle = first + (end - first) / 2;
        if (s->thumbnail_order[middle]->index < index) first = middle + 1;
        else end = middle;
    }
    return first;
}

static Thumbnail *find_thumbnail(const MarcusSceneOrganiser *s, int index)
{
    int slot;
    slot = thumbnail_slot(s, index);
    if (slot == s->thumbnail_count || s->thumbnail_order[slot]->index != index) return NULL;
    return s->thumbnail_order[slot];
}

static Thumbnail *store_thumbnail(MarcusSceneOrganiser *s, SDL_Renderer *renderer, int index, const MarcusImage *image)
{
    Thumbnail *thumbnail;
    Thumbnail **order;
    int slot, i, limit;
    order = s->thumbnail_order;
    slot = thumbnail_slot(s, index);
    if (slot < s->thumbnail_count && order[slot]->index == index)
    {
        thumbnail = order[slot];
        dialog_retire_texture(thumbnail->texture);
    }
    else
    {
        limit = s->frames ? FRAME_THUMBNAIL_LIMIT : SCENE_THUMBNAIL_LIMIT;
        if (s->thumbnail_count == limit)
        {
            thumbnail = &s->thumbnails[0];
            for (i = 1; i < limit; ++i)
                if (s->clock - s->thumbnails[i].used > s->clock - thumbnail->used) thumbnail = &s->thumbnails[i];
            dialog_retire_texture(thumbnail->texture);
            i = thumbnail_slot(s, thumbnail->index);
            --s->thumbnail_count;
            memmove(order + i, order + i + 1, (s->thumbnail_count - i) * sizeof(*order));
            if (i < slot) --slot;
        }
        else thumbnail = &s->thumbnails[s->thumbnail_count];
        memmove(order + slot + 1, order + slot, (s->thumbnail_count - slot) * sizeof(*order));
        order[slot] = thumbnail;
        ++s->thumbnail_count;
    }
    thumbnail->index = index;
    thumbnail->used = ++s->clock;
    thumbnail->texture = NULL;
    if (!image->rgba || image->width <= 0 || image->height <= 0) return thumbnail;
    thumbnail->texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC,
                                           image->width, image->height);
    if (thumbnail->texture && !SDL_UpdateTexture(thumbnail->texture, NULL, image->rgba, image->pitch))
    {
        SDL_DestroyTexture(thumbnail->texture);
        thumbnail->texture = NULL;
    }
    if (thumbnail->texture) SDL_SetTextureScaleMode(thumbnail->texture, SDL_SCALEMODE_LINEAR);
    return thumbnail;
}

static SDL_Texture *get_thumbnail(MarcusSceneOrganiser *s, SDL_Renderer *renderer, int index)
{
    Thumbnail *thumbnail;
    MarcusImage image;
    thumbnail = find_thumbnail(s, index);
    if (thumbnail)
    {
        thumbnail->used = ++s->clock;
        return thumbnail->texture;
    }
    /* Decode at most one saved thumbnail per draw. Frame previews use the worker. */
    if (!s->open || s->frames || s->loaded_this_frame) return NULL;
    s->loaded_this_frame = 1;
    memset(&image, 0, sizeof(image));
    marcus_scene_thumbnail(s->player, index, &image);
    return store_thumbnail(s, renderer, index, &image)->texture;
}

static void request_previews(MarcusSceneOrganiser *s, const MarcusFramePosition *positions, int count)
{
    if (!marcus_frame_previews_request_positions(s->frame_previews, positions, count))
        SDL_strlcpy(s->frame_error, "Cannot queue frame thumbnails.", sizeof(s->frame_error));
}

static void toggle_whole_movie(MarcusSceneOrganiser *s)
{
    MarcusFramePosition position;
    if (!s->frames || s->selected < 0 || s->selected >= s->count) return;
    position = position_at(s, s->selected);
    s->whole_movie = !s->whole_movie;
    s->source_scene = position.scene;
    s->count = s->whole_movie ? s->movie_frame_count : s->scenes[position.scene].count;
    s->selected = index_at(s, position.scene, position.frame);
    clear_thumbnails(s);
    /* In-flight results still carry their authored scene and frame. */
    request_previews(s, NULL, 0);
}

static void update_frame_thumbnails(MarcusSceneOrganiser *s, SDL_Renderer *renderer, int first, int end)
{
    MarcusFramePosition requests[MARCUS_FRAME_PREVIEW_REQUEST_LIMIT];
    MarcusImage image;
    int n, index, count;
    for (n = 0; n < 4; ++n)
    {
        if (marcus_frame_previews_take(s->frame_previews, &image, s->frame_error, sizeof(s->frame_error)) <= 0) break;
        if (!s->preview_size_known)
        {
            s->preview_width = image.width;
            s->preview_height = image.height;
            s->frame_zoom = clamp_zoom(s, s->frame_zoom);
            s->preview_size_known = 1;
        }
        index = index_at(s, image.scene, image.frame);
        if (index >= 0 && index < s->count) store_thumbnail(s, renderer, index, &image);
    }
    if (s->frame_error[0]) return;
    if (end > s->count) end = s->count;
    count = 0;
    if (s->selected >= first && s->selected < end && !find_thumbnail(s, s->selected))
        requests[count++] = position_at(s, s->selected);
    for (index = first; index < end && count < MARCUS_FRAME_PREVIEW_REQUEST_LIMIT; ++index)
        if (index != s->selected && !find_thumbnail(s, index)) requests[count++] = position_at(s, index);
    request_previews(s, requests, count);
}

MarcusSceneOrganiser *scene_organiser_create(void)
{
    MarcusSceneOrganiser *s;
    s = calloc(1, sizeof(*s));
    if (s)
    {
        s->result = -1;
        s->frame_zoom = 1.0f;
        s->preview_width = 160;
        s->preview_height = 120;
    }
    return s;
}

void scene_organiser_close(MarcusSceneOrganiser *s)
{
    if (!s) return;
    s->open = 0;
    s->result = -1;
    marcus_frame_previews_destroy(s->frame_previews);
    s->frame_previews = NULL;
    s->frames = 0;
    s->frame_error[0] = 0;
    s->preview_size_known = 0;
    s->preview_width = 160;
    s->preview_height = 120;
    free(s->scenes);
    s->scenes = NULL;
    s->scene_count = s->movie_frame_count = 0;
    s->count = 0;
    clear_thumbnails(s);
}

void scene_organiser_destroy(MarcusSceneOrganiser *s)
{
    scene_organiser_close(s);
    free(s);
}

void scene_organiser_show(MarcusSceneOrganiser *s, MarcusPlayer *player)
{
    MarcusInfo info;
    if (!s) return;
    scene_organiser_close(s);
    if (!marcus_info(player, &info) || info.scene_count <= 0) return;
    s->player = player;
    s->count = info.scene_count;
    s->current = s->selected = info.scene;
    s->open = 1;
}

int scene_organiser_show_frames(MarcusSceneOrganiser *s, MarcusPlayer *player)
{
    MarcusInfo info;
    MarcusScene scene;
    int i, length, total;
    if (!s) return 0;
    scene_organiser_close(s);
    if (!marcus_info(player, &info) || info.scene < 0 || info.scene >= info.scene_count ||
        (size_t)info.scene_count > (size_t)-1 / sizeof(*s->scenes)) return 0;
    s->scenes = malloc((size_t)info.scene_count * sizeof(*s->scenes));
    if (!s->scenes) return 0;
    total = 0;
    for (i = 0; i < info.scene_count; ++i)
    {
        if (!marcus_scene(player, i, &scene)) goto failed;
        length = frame_count(scene.first_frame, scene.last_frame);
        if (!length || length > INT_MAX - total) goto failed;
        s->scenes[i].first = scene.first_frame;
        s->scenes[i].count = length;
        s->scenes[i].offset = total;
        total += length;
    }
    s->scene_count = info.scene_count;
    s->source_scene = info.scene;
    s->movie_frame_count = total;
    s->count = s->whole_movie ? total : s->scenes[info.scene].count;
    s->current = s->selected = index_at(s, info.scene, info.frame);
    if (s->selected < 0) goto failed;
    s->frame_previews = marcus_frame_previews_create(player);
    if (!s->frame_previews) goto failed;
    s->player = player;
    s->frames = s->open = 1;
    return 1;

failed:
    scene_organiser_close(s);
    return 0;
}

int scene_organiser_visible(const MarcusSceneOrganiser *s) { return s && s->open; }

int scene_organiser_take(MarcusSceneOrganiser *s)
{
    int result;
    if (!s || s->frames) return -1;
    result = s->result;
    s->result = -1;
    return result;
}

int scene_organiser_take_frame(MarcusSceneOrganiser *s, int *scene, long *frame)
{
    MarcusFramePosition position;
    if (!s || !s->frames || s->result < 0 || !scene || !frame) return 0;
    position = position_at(s, s->result);
    *scene = position.scene;
    *frame = position.frame;
    s->result = -1;
    return 1;
}

static void organiser_state(void *model, MarcusDialogState *out)
{
    MarcusSceneOrganiser *s;
    s = model;
    memset(out, 0, sizeof(*out));
    out->open = s->open;
    out->kind = s->frames ? DIALOG_FRAMES : DIALOG_SCENES;
    out->count = s->count;
    out->selected = s->selected;
    out->current = s->current;
    out->whole_movie = s->whole_movie;
    out->fit = s->fit_frames;
    out->zoom = s->frame_zoom;
    out->minimum_zoom = minimum_zoom(s);
    out->preview_width = (float)s->preview_width;
    out->preview_height = (float)s->preview_height;
    SDL_strlcpy(out->title, s->frames ? "Frame organiser" : "Scene organiser", sizeof(out->title));
    SDL_strlcpy(out->confirm, s->frames ? "Open frame" : "Open scene", sizeof(out->confirm));
    SDL_snprintf(out->status, sizeof(out->status), "%d %s / Select a thumbnail, then Open or double-click.",
                 s->count, s->frames ? "frames" : "scenes");
    SDL_strlcpy(out->error, s->frame_error, sizeof(out->error));
}

static void organiser_prepare(void *model, SDL_Renderer *renderer, int first, int end)
{
    MarcusSceneOrganiser *s;
    s = model;
    if (first < 0) s->loaded_this_frame = 0;
    else if (s->open && s->frames) update_frame_thumbnails(s, renderer, first, end);
}

static int organiser_item(void *model, SDL_Renderer *renderer, int index, MarcusDialogItem *out)
{
    MarcusSceneOrganiser *s;
    MarcusFramePosition position;
    MarcusScene scene;
    s = model;
    memset(out, 0, sizeof(*out));
    if (index < 0 || index >= s->count) return 0;
    out->texture = get_thumbnail(s, renderer, index);
    if (s->frames)
    {
        position = position_at(s, index);
        out->scene = position.scene;
        out->frame = position.frame;
        SDL_snprintf(out->label, sizeof(out->label), "Scene %d / frame %ld", position.scene + 1,
                     position.frame - s->scenes[position.scene].first + 1);
    }
    else if (marcus_scene(s->player, index, &scene))
    {
        out->scene = index;
        out->transition = scene.transition;
        SDL_snprintf(out->label, sizeof(out->label), "Scene %d / %s", index + 1, scene.name);
        SDL_snprintf(out->detail, sizeof(out->detail), "%.0f frames / After scene: %s",
                     (double)scene.last_frame - scene.first_frame + 1, transition_name(scene.transition));
    }
    return 1;
}

static void organiser_action(void *model, int command, int index, float value, const char *text)
{
    MarcusSceneOrganiser *s;
    s = model;
    (void)text;
    if (!s->open) return;
    switch (command)
    {
    case DIALOG_SELECT:
        s->selected = index < 0 ? 0 : index >= s->count ? s->count - 1 : index;
        break;
    case DIALOG_ACCEPT:
        if (s->selected >= 0 && s->selected < s->count)
        {
            s->result = s->selected;
            s->open = 0;
            marcus_frame_previews_destroy(s->frame_previews);
            s->frame_previews = NULL;
        }
        break;
    case DIALOG_CANCEL:
        scene_organiser_close(s);
        break;
    case DIALOG_SCOPE:
        toggle_whole_movie(s);
        break;
    case DIALOG_ZOOM:
        s->frame_zoom = clamp_zoom(s, value);
        if (!index) s->fit_frames = 0;
        break;
    case DIALOG_FIT:
        s->fit_frames = 1;
        break;
    }
}

const MarcusDialogHost marcus_scene_dialog_host = {organiser_state, organiser_prepare, organiser_item, organiser_action};
