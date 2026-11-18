#include "background_browser.h"
#include "background_preview.h"
#include "dialog_host.h"
#include <limits.h>
#include <stdlib.h>
#include <string.h>

enum { MODEL_TEXTURE_LIMIT = 64, BLACK_BACKGROUND = 11 };

typedef struct BackgroundTile
{
    unsigned long chunk;
    SDL_Texture *texture;
    char error[1024];
    int attempted;
} BackgroundTile;

typedef struct ModelTile
{
    MarcusProp entry;
    int actor, order;
    SDL_Texture *texture;
    unsigned long used;
    char error[1024];
} ModelTile;

struct MarcusBackgroundBrowser
{
    int open, chosen, objects, selected, filter;
    MarcusBackground *backgrounds;
    int background_count;
    BackgroundTile *background_tiles;
    int background_tile_count, loading_background;
    MarcusBackgroundPreviews *background_previews;
    ModelTile *models;
    int model_count, cached_models;
    int *filtered, filtered_count;
    unsigned long clock;
    MarcusFramePreviews *model_previews;
    char *resources, *content;
    char error[1024];
};

static void *allocate_entries(int count, size_t size)
{
    if (count <= 0 || (size_t)count > (size_t)-1 / size) return NULL;
    return calloc((size_t)count, size);
}

static void clear_background_tiles(MarcusBackgroundBrowser *browser)
{
    int i;
    for (i = 0; i < browser->background_tile_count; ++i)
        dialog_retire_texture(browser->background_tiles[i].texture);
    free(browser->background_tiles);
    browser->background_tiles = NULL;
    browser->background_tile_count = 0;
    browser->loading_background = -1;
}

static void stop_models(MarcusBackgroundBrowser *browser)
{
    marcus_frame_previews_destroy(browser->model_previews);
    browser->model_previews = NULL;
}

static void clear_dialog(MarcusBackgroundBrowser *browser)
{
    int i;
    stop_models(browser);
    for (i = 0; i < browser->model_count; ++i) dialog_retire_texture(browser->models[i].texture);
    free(browser->models);
    free(browser->filtered);
    free(browser->backgrounds);
    SDL_free(browser->resources);
    SDL_free(browser->content);
    browser->models = NULL;
    browser->filtered = NULL;
    browser->backgrounds = NULL;
    browser->resources = browser->content = NULL;
    browser->open = browser->chosen = 0;
    browser->model_count = browser->filtered_count = browser->background_count = 0;
    browser->cached_models = 0;
    browser->clock = 0;
    browser->selected = -1;
    browser->error[0] = '\0';
}

static int count_items(const MarcusBackgroundBrowser *browser)
{
    return browser->objects ? browser->filtered_count : browser->background_count;
}

static int model_index(const MarcusBackgroundBrowser *browser, int index)
{
    return index >= 0 && index < browser->filtered_count ? browser->filtered[index] : -1;
}

static void filter_models(MarcusBackgroundBrowser *browser, int filter)
{
    int previous, i;
    previous = model_index(browser, browser->selected);
    browser->filter = filter < 0 ? 0 : filter > 2 ? 2 : filter;
    browser->selected = -1;
    browser->filtered_count = 0;
    for (i = 0; i < browser->model_count; ++i)
        if (!browser->filter || browser->models[i].actor == (browser->filter == 1))
        {
            if (i == previous) browser->selected = browser->filtered_count;
            browser->filtered[browser->filtered_count++] = i;
        }
    if (browser->selected < 0 && browser->filtered_count) browser->selected = 0;
}

MarcusBackgroundBrowser *background_browser_create(void)
{
    MarcusBackgroundBrowser *browser;
    browser = calloc(1, sizeof(*browser));
    if (!browser) return NULL;
    browser->background_previews = marcus_background_previews_create();
    if (!browser->background_previews)
    {
        free(browser);
        return NULL;
    }
    browser->loading_background = -1;
    browser->selected = -1;
    return browser;
}

void background_browser_destroy(MarcusBackgroundBrowser *browser)
{
    if (!browser) return;
    marcus_background_previews_destroy(browser->background_previews);
    clear_background_tiles(browser);
    clear_dialog(browser);
    free(browser);
}

int background_browser_visible(const MarcusBackgroundBrowser *browser) { return browser && browser->open; }

void background_browser_prepare(MarcusBackgroundBrowser *browser, MarcusPlayer *player,
                                const char *resources, const char *content)
{
    MarcusBackground entry;
    int result, i, count;
    if (!browser || !player || !resources || !content) return;
    result = marcus_background_previews_configure(browser->background_previews, resources, content);
    if (result == MARCUS_BACKGROUND_PREVIEWS_FAILED) return;
    if (result == MARCUS_BACKGROUND_PREVIEWS_CONFIGURED) clear_background_tiles(browser);
    if (browser->background_tiles) return;
    count = marcus_background_count(player, content);
    browser->background_tiles = allocate_entries(count, sizeof(*browser->background_tiles));
    if (!browser->background_tiles) return;
    for (i = 0; i < count; ++i)
        if (marcus_background(player, i, &entry))
            browser->background_tiles[browser->background_tile_count++].chunk = entry.chunk;
}

static const BackgroundTile *find_background(const MarcusBackgroundBrowser *browser, unsigned long chunk)
{
    int i;
    for (i = 0; i < browser->background_tile_count; ++i)
        if (browser->background_tiles[i].chunk == chunk) return &browser->background_tiles[i];
    return NULL;
}

/* Background images are warmed even while the browser is closed. An open
 * browser gives its selected background first claim on the next worker job. */
static void warm_backgrounds(MarcusBackgroundBrowser *browser, SDL_Renderer *renderer)
{
    BackgroundTile *tile;
    MarcusImage image;
    int i, next, status;
    unsigned long selected;
    if (browser->loading_background >= 0)
    {
        tile = &browser->background_tiles[browser->loading_background];
        status = marcus_background_previews_take(browser->background_previews, &image, tile->error, sizeof(tile->error));
        if (!status) return;
        browser->loading_background = -1;
        tile->attempted = 1;
        if (status > 0 && image.rgba)
        {
            tile->texture = dialog_preview_texture(renderer, image.rgba, image.width, image.height, image.pitch);
            if (!tile->texture) SDL_strlcpy(tile->error, SDL_GetError(), sizeof(tile->error));
        }
    }
    selected = MARCUS_BACKGROUND_STORYBOARD;
    if (browser->open && !browser->objects && browser->selected >= 0)
        selected = browser->backgrounds[browser->selected].chunk;
    next = -1;
    for (i = 0; i < browser->background_tile_count; ++i)
        if (!browser->background_tiles[i].attempted)
        {
            if (next < 0) next = i;
            if (browser->background_tiles[i].chunk == selected)
            {
                next = i;
                break;
            }
        }
    if (next < 0) return;
    tile = &browser->background_tiles[next];
    if (marcus_background_previews_start(browser->background_previews, tile->chunk, tile->error, sizeof(tile->error)))
        browser->loading_background = next;
    else tile->attempted = 1;
}

static int compare_models(const void *left, const void *right)
{
    const ModelTile *a, *b;
    int order;
    a = left;
    b = right;
    order = SDL_strcasecmp(a->entry.name, b->entry.name);
    return order ? order : a->order - b->order;
}

static void append_models(MarcusBackgroundBrowser *browser, MarcusPlayer *player, int actor, int count)
{
    ModelTile *tile;
    int i, loaded;
    for (i = 0; i < count; ++i)
    {
        tile = &browser->models[browser->model_count];
        loaded = actor ? marcus_actor(player, i, &tile->entry) : marcus_prop(player, i, &tile->entry);
        if (!loaded) continue;
        tile->actor = actor;
        tile->order = browser->model_count++;
    }
}

int background_browser_show_objects(MarcusBackgroundBrowser *browser, MarcusPlayer *player,
                                     const char *resources, const char *content)
{
    int actors, props;
    if (!browser || !resources || !content) return 0;
    clear_dialog(browser);
    browser->objects = 1;
    actors = marcus_actor_count(player, content);
    if (actors < 0) return 0;
    props = marcus_prop_count(player, content);
    if (props < 0 || props > INT_MAX - actors || actors + props == 0) return 0;
    browser->models = allocate_entries(actors + props, sizeof(*browser->models));
    if (!browser->models) return 0;
    append_models(browser, player, 1, actors);
    append_models(browser, player, 0, props);
    browser->filtered = allocate_entries(browser->model_count, sizeof(*browser->filtered));
    browser->resources = SDL_strdup(resources);
    browser->content = SDL_strdup(content);
    if (!browser->filtered || !browser->resources || !browser->content)
    {
        clear_dialog(browser);
        return 0;
    }
    qsort(browser->models, (size_t)browser->model_count, sizeof(*browser->models), compare_models);
    filter_models(browser, browser->filter);
    browser->open = 1;
    return 1;
}

int background_browser_show(MarcusBackgroundBrowser *browser, MarcusPlayer *player,
                             const char *resources, const char *content)
{
    MarcusBackground entry;
    int count, i, index;
    if (!browser || !resources || !content) return 0;
    clear_dialog(browser);
    browser->objects = 0;
    background_browser_prepare(browser, player, resources, content);
    for (i = 0; i < browser->background_tile_count; ++i)
        if (!browser->background_tiles[i].texture && browser->background_tiles[i].attempted)
        {
            browser->background_tiles[i].attempted = 0;
            browser->background_tiles[i].error[0] = '\0';
        }
    count = marcus_background_count(player, content);
    if (count == INT_MAX) return 0;
    if (count < 0) count = 0;
    browser->backgrounds = allocate_entries(count + 1, sizeof(*browser->backgrounds));
    if (!browser->backgrounds) return 0;
    browser->backgrounds[0].chunk = MARCUS_BACKGROUND_STORYBOARD;
    SDL_strlcpy(browser->backgrounds[0].name, "Storyboard - draw a background", sizeof(browser->backgrounds[0].name));
    browser->background_count = 1;
    for (i = 0; i < count; ++i)
        if (marcus_background(player, i, &entry))
        {
            index = browser->background_count;
            if (entry.chunk == BLACK_BACKGROUND)
            {
                memmove(browser->backgrounds + 2, browser->backgrounds + 1,
                        (size_t)(index - 1) * sizeof(*browser->backgrounds));
                index = 1;
            }
            browser->backgrounds[index] = entry;
            ++browser->background_count;
        }
    browser->selected = 0;
    browser->open = 1;
    return 1;
}

static void collect_model_previews(MarcusBackgroundBrowser *browser, SDL_Renderer *renderer)
{
    MarcusImage image;
    SDL_Texture *texture;
    ModelTile *tile;
    char message[1024];
    int status;
    message[0] = '\0';
    while ((status = marcus_frame_previews_take(browser->model_previews, &image, message, sizeof(message))) > 0)
    {
        if (image.scene < 0 || image.scene >= browser->model_count) continue;
        tile = &browser->models[image.scene];
        SDL_strlcpy(tile->error, message, sizeof(tile->error));
        if (!image.rgba || !image.width || !image.height) continue;
        texture = dialog_preview_texture(renderer, image.rgba, image.width, image.height, image.pitch);
        if (!texture)
        {
            SDL_strlcpy(tile->error, SDL_GetError(), sizeof(tile->error));
            continue;
        }
        if (!tile->texture) ++browser->cached_models;
        dialog_retire_texture(tile->texture);
        tile->texture = texture;
        tile->used = ++browser->clock;
        SDL_SetTextureScaleMode(texture, SDL_SCALEMODE_LINEAR);
    }
    if (status < 0) SDL_strlcpy(browser->error, message, sizeof(browser->error));
}

static void trim_model_textures(MarcusBackgroundBrowser *browser, int first, int end)
{
    int i, j, oldest;
    for (i = first; i < end; ++i)
        if (browser->models[browser->filtered[i]].texture)
            browser->models[browser->filtered[i]].used = ++browser->clock;
    while (browser->cached_models > MODEL_TEXTURE_LIMIT)
    {
        oldest = -1;
        for (i = 0; i < browser->model_count; ++i)
            if (browser->models[i].texture &&
                (oldest < 0 || browser->clock - browser->models[i].used > browser->clock - browser->models[oldest].used))
            {
                for (j = first; j < end && browser->filtered[j] != i; ++j) {}
                if (j == end) oldest = i;
            }
        if (oldest < 0) break;
        dialog_retire_texture(browser->models[oldest].texture);
        browser->models[oldest].texture = NULL;
        --browser->cached_models;
    }
}

static int request_model(const MarcusBackgroundBrowser *browser, int index, MarcusFramePosition *position)
{
    int model;
    model = browser->filtered[index];
    if (browser->models[model].texture || browser->models[model].error[0]) return 0;
    position->scene = model;
    position->frame = (long)browser->models[model].entry.chunk;
    return 1;
}

static void gallery_previews(MarcusBackgroundBrowser *browser, SDL_Renderer *renderer, int first, int end)
{
    MarcusFramePosition requests[MARCUS_FRAME_PREVIEW_REQUEST_LIMIT];
    int i, count;
    if (!browser->model_previews && !browser->error[0])
        browser->model_previews = marcus_model_previews_create(browser->resources, browser->content,
                                                              browser->error, sizeof(browser->error));
    if (!browser->model_previews) return;
    collect_model_previews(browser, renderer);
    if (first > browser->filtered_count) first = browser->filtered_count;
    if (end > browser->filtered_count) end = browser->filtered_count;
    trim_model_textures(browser, first, end);
    if (browser->error[0]) return;
    count = 0;
    if (browser->selected >= first && browser->selected < end)
        count = request_model(browser, browser->selected, requests);
    for (i = first; i < end && count < MARCUS_FRAME_PREVIEW_REQUEST_LIMIT; ++i)
        if (i != browser->selected) count += request_model(browser, i, requests + count);
    if (!marcus_frame_previews_request_positions(browser->model_previews, requests, count))
        SDL_strlcpy(browser->error, "Cannot queue model thumbnails.", sizeof(browser->error));
}

int background_browser_take(MarcusBackgroundBrowser *browser, unsigned long *chunk)
{
    if (!browser || browser->objects || !browser->chosen || !chunk) return 0;
    browser->chosen = 0;
    *chunk = browser->backgrounds[browser->selected].chunk;
    return 1;
}

int background_browser_take_object(MarcusBackgroundBrowser *browser, unsigned long *chunk, int *actor)
{
    const ModelTile *model;
    if (!browser || !browser->objects || !browser->chosen || !chunk || !actor) return 0;
    browser->chosen = 0;
    model = &browser->models[browser->filtered[browser->selected]];
    *chunk = model->entry.chunk;
    *actor = model->actor;
    return 1;
}

static void background_state(void *model, MarcusDialogState *out)
{
    const MarcusBackgroundBrowser *browser;
    const char *confirm, *status;
    int selected;
    browser = model;
    memset(out, 0, sizeof(*out));
    out->open = browser->open;
    out->kind = browser->objects ? DIALOG_MODELS : DIALOG_BACKGROUNDS;
    out->count = count_items(browser);
    out->selected = browser->selected;
    out->filter = browser->filter;
    confirm = "Add scene";
    status = "Starts at frame 1 with no actors or sounds.";
    if (browser->objects)
    {
        selected = model_index(browser, browser->selected);
        confirm = selected < 0 ? "Add object" : browser->models[selected].actor ? "Add actor" : "Add prop";
        status = out->count ? "Appears at the camera's next insertion point." : "No objects match this filter.";
    }
    SDL_strlcpy(out->title, browser->objects ? "Add actor or prop" : "New scene", sizeof(out->title));
    SDL_strlcpy(out->confirm, confirm, sizeof(out->confirm));
    SDL_snprintf(out->status, sizeof(out->status), "%d of %d / %s", out->selected + 1, out->count, status);
    SDL_strlcpy(out->error, browser->error, sizeof(out->error));
}

static void background_prepare(void *model, SDL_Renderer *renderer, int first, int end)
{
    MarcusBackgroundBrowser *browser;
    browser = model;
    if (first < 0) warm_backgrounds(browser, renderer);
    else if (browser->open && browser->objects) gallery_previews(browser, renderer, first, end);
}

static int background_item(void *model, SDL_Renderer *renderer, int index, MarcusDialogItem *out)
{
    const MarcusBackgroundBrowser *browser;
    const ModelTile *tile;
    const BackgroundTile *background;
    const MarcusBackground *entry;
    browser = model;
    (void)renderer;
    memset(out, 0, sizeof(*out));
    if (index < 0 || index >= count_items(browser)) return 0;
    if (browser->objects)
    {
        tile = &browser->models[browser->filtered[index]];
        out->texture = tile->texture;
        out->preview_unavailable = !tile->texture && (tile->error[0] || browser->error[0]);
        SDL_strlcpy(out->label, tile->entry.name, sizeof(out->label));
        SDL_snprintf(out->detail, sizeof(out->detail), "%s%s%s", tile->actor ? "Actor" : "Prop",
                     tile->error[0] ? "\n" : "", tile->error);
        return 1;
    }
    entry = &browser->backgrounds[index];
    SDL_strlcpy(out->label, entry->name, sizeof(out->label));
    if (entry->chunk == MARCUS_BACKGROUND_STORYBOARD)
        SDL_strlcpy(out->detail, "Draw your own background, camera angles and lighting.", sizeof(out->detail));
    else if ((background = find_background(browser, entry->chunk)) != NULL)
    {
        out->texture = background->texture;
        SDL_strlcpy(out->detail, background->error, sizeof(out->detail));
    }
    return 1;
}

static void background_action(void *model, int command, int index, float value, const char *text)
{
    MarcusBackgroundBrowser *browser;
    int count;
    browser = model;
    (void)value;
    (void)text;
    if (!browser->open) return;
    count = count_items(browser);
    switch (command)
    {
    case DIALOG_SELECT:
        browser->selected = !count ? -1 : index < 0 ? 0 : index >= count ? count - 1 : index;
        break;
    case DIALOG_FILTER:
        if (browser->objects) filter_models(browser, index);
        break;
    case DIALOG_ACCEPT:
    case DIALOG_CANCEL:
        browser->chosen = command == DIALOG_ACCEPT && browser->selected >= 0 && browser->selected < count;
        browser->open = 0;
        stop_models(browser);
        break;
    }
}

const MarcusDialogHost marcus_background_dialog_host = {
    background_state, background_prepare, background_item, background_action
};
