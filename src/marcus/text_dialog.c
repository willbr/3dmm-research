/* Marcus New/Edit 3D text. Application state and widgets use C89 + cimgui. */
#include "text_dialog.h"
#include "text_preview.h"
#define CIMGUI_DEFINE_ENUMS_AND_STRUCTS
#include <cimgui.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

typedef struct TextFont
{
    MarcusProp entry;
    long source;
} TextFont;
typedef struct TextSwatch
{
    int material;
    unsigned rgb;
    float hue;
} TextSwatch;
typedef struct TextTile
{
    SDL_Texture *texture;
    int wanted, attempted;
} TextTile;
struct MarcusTextDialog
{
    int open, opening, font, material, chosen, object;
    unsigned long document;
    MarcusPlayer *player;
    MarcusText value;
    TextFont *fonts;
    int font_count;
    MarcusProp *materials;
    int material_count;
    TextSwatch swatches[12];
    int swatch_count;
    float uv[5], uv_original[6], uv_start[3];
    int uv_dirty, preview_dirty, view_dirty, wording_dirty;
    float yaw, pitch, zoom;
    Uint64 last_preview;
    Uint64 wording_edited_at;
    char *resources;
    char error[1024];
    SDL_Texture *texture;
    MarcusTextPreview *preview;
    MarcusTextPreview *thumbnails;
    TextTile *font_tiles, shape_tiles[10], *material_tiles;
    int loading_kind, loading_index;
};

static const char *shapes[] = {"Normal", "Arch up", "Circle around Y", "Large middle", "Arch down",
    "Arch around Z", "Circle around Z", "Vertical", "Grow right", "Grow left"};
static ImVec2 vec(float x, float y)
{
    ImVec2 value;
    value.x = x;
    value.y = y;
    return value;
}
static ImVec4 colour(unsigned rgb)
{
    ImVec4 value;
    value.x = ((rgb >> 16) & 255) / 255.0f;
    value.y = ((rgb >> 8) & 255) / 255.0f;
    value.z = (rgb & 255) / 255.0f;
    value.w = 1;
    return value;
}
static float clamp(float value, float low, float high)
{
    return value < low ? low : value > high ? high : value;
}
static void changed(MarcusTextDialog *d)
{
    d->preview_dirty = 1;
    d->error[0] = 0;
}
static void view_changed(MarcusTextDialog *d)
{
    d->view_dirty = 1;
}
static void read_uv(MarcusTextDialog *d)
{
    const float *m;
    m = d->value.appearance.mapping;
    memcpy(d->uv_original, m, sizeof(d->uv_original));
    d->uv[0] = (float)sqrt((double)m[0] * m[0] + (double)m[1] * m[1]);
    d->uv[1] = (float)sqrt((double)m[2] * m[2] + (double)m[3] * m[3]);
    d->uv[2] = (float)atan2(m[1], m[0]) * 57.295779513f;
    d->uv[3] = m[4];
    d->uv[4] = m[5];
    memcpy(d->uv_start, d->uv, sizeof(d->uv_start));
    d->uv_dirty = 0;
}
static MarcusText value(const MarcusTextDialog *d)
{
    MarcusText result;
    float angle, c, s, ratio;
    int row;
    result = d->value;
    result.font = d->fonts[d->font].entry.chunk;
    result.font_source = d->fonts[d->font].source;
    if (d->uv_dirty)
    {
        angle = (d->uv[2] - d->uv_start[2]) * .01745329252f;
        c = (float)cos(angle);
        s = (float)sin(angle);
        for (row = 0; row < 2; ++row)
        {
            ratio = d->uv_start[row] > .00001f ? d->uv[row] / d->uv_start[row] : 1;
            result.appearance.mapping[row * 2] =
                (d->uv_original[row * 2] * c - d->uv_original[row * 2 + 1] * s) * ratio;
            result.appearance.mapping[row * 2 + 1] =
                (d->uv_original[row * 2] * s + d->uv_original[row * 2 + 1] * c) * ratio;
        }
        result.appearance.mapping[4] = d->uv[3];
        result.appearance.mapping[5] = d->uv[4];
    }
    return result;
}
static void choose_material(MarcusTextDialog *d, int index)
{
    if (!marcus_text_material_preset(d->player, d->materials[index].chunk, &d->value.appearance))
    {
        SDL_strlcpy(d->error, marcus_error(d->player), sizeof(d->error));
        return;
    }
    d->material = index;
    read_uv(d);
    changed(d);
}
static void close_dialog(MarcusTextDialog *d)
{
    text_preview_stop(d->preview);
    text_preview_stop(d->thumbnails);
    d->loading_index = -1;
    d->open = d->opening = 0;
}
static void submit(MarcusTextDialog *d)
{
    MarcusText text;
    if (!d->value.text[0] || SDL_utf8strlen(d->value.text) > 50)
        return;
    if (d->document != marcus_document(d->player))
    {
        SDL_strlcpy(d->error, "The movie changed. Close and reopen the text editor.", sizeof(d->error));
        return;
    }
    text = value(d);
    if (d->object >= 0)
    {
        if (!marcus_set_text(d->player, d->object, &text))
        {
            SDL_strlcpy(d->error, marcus_error(d->player), sizeof(d->error));
            return;
        }
        d->chosen = marcus_document(d->player) != d->document ? d->object : -1;
    }
    else
    {
        d->chosen = marcus_add_text_options(d->player, &text);
        if (d->chosen < 0)
        {
            SDL_strlcpy(d->error, marcus_error(d->player), sizeof(d->error));
            return;
        }
    }
    close_dialog(d);
}
static int compare_swatches(const void *left, const void *right)
{
    const TextSwatch *a, *b;
    a = (const TextSwatch *)left;
    b = (const TextSwatch *)right;
    if (a->hue < b->hue) return -1;
    if (a->hue > b->hue) return 1;
    return a->material - b->material;
}
static void free_catalogues(MarcusTextDialog *d)
{
    int i;
    if (d->font_tiles)
        for (i = 0; i < d->font_count; ++i) SDL_DestroyTexture(d->font_tiles[i].texture);
    for (i = 0; i < 10; ++i)
    {
        SDL_DestroyTexture(d->shape_tiles[i].texture);
        memset(&d->shape_tiles[i], 0, sizeof(d->shape_tiles[i]));
    }
    if (d->material_tiles)
        for (i = 0; i <= d->material_count; ++i) SDL_DestroyTexture(d->material_tiles[i].texture);
    free(d->font_tiles);
    free(d->material_tiles);
    d->font_tiles = d->material_tiles = NULL;
    free(d->fonts);
    free(d->materials);
    free(d->resources);
    d->fonts = NULL;
    d->materials = NULL;
    d->resources = NULL;
    d->font_count = d->material_count = d->swatch_count = 0;
}
MarcusTextDialog *text_dialog_create(void)
{
    MarcusTextDialog *d;
    d = (MarcusTextDialog *)calloc(1, sizeof(*d));
    if (!d) return NULL;
    d->preview = text_preview_create();
    d->thumbnails = text_preview_create();
    if (!d->preview || !d->thumbnails)
    { text_preview_destroy(d->preview); text_preview_destroy(d->thumbnails); free(d); return NULL; }
    d->loading_index = -1;
    d->chosen = d->object = d->material = -1;
    SDL_strlcpy(d->value.text, "Text", sizeof(d->value.text));
    return d;
}
void text_dialog_destroy(MarcusTextDialog *d)
{
    if (!d) return;
    text_preview_destroy(d->preview);
    text_preview_destroy(d->thumbnails);
    SDL_DestroyTexture(d->texture);
    free_catalogues(d);
    free(d);
}
int text_dialog_visible(const MarcusTextDialog *d)
{
    return d && d->open;
}
static int show(MarcusTextDialog *d, MarcusPlayer *player, SDL_Window *window,
                const char *content, const char *resources, int object)
{
    MarcusText current;
    MarcusTextMaterial appearance;
    MarcusProp entry;
    TextSwatch *swatches;
    unsigned palette[256], index, rgb;
    float hues[256];
    int count, material_count, i, j, found, swatch_count;
    if (!d || !window || !resources) return 0;
    memset(&current, 0, sizeof(current));
    if (object >= 0 && !marcus_text(player, object, &current)) return 0;
    count = marcus_font_count(player, content);
    if (count <= 0 && object < 0) return 0;
    if (count < 0) count = 0; /* Existing text can retain an imported font. */
    close_dialog(d);
    free_catalogues(d);
    d->fonts = (TextFont *)calloc((size_t)count + 1, sizeof(*d->fonts));
    d->resources = (char *)malloc(strlen(resources) + 1);
    if (!d->fonts || !d->resources) { free_catalogues(d); return 0; }
    memcpy(d->resources, resources, strlen(resources) + 1);
    for (i = 0; i < count; ++i)
        if (marcus_font(player, i, &entry))
        {
            d->fonts[d->font_count].entry = entry;
            d->fonts[d->font_count++].source = -1;
        }
    if (object >= 0)
    {
        d->value = current;
        found = -1;
        for (i = 0; i < d->font_count; ++i)
            if (current.font_source == -1 && d->fonts[i].entry.chunk == current.font) found = i;
        if (found < 0)
        {
            found = d->font_count++;
            d->fonts[found].entry.chunk = current.font;
            SDL_strlcpy(d->fonts[found].entry.name, "Current font", sizeof(d->fonts[found].entry.name));
            d->fonts[found].source = current.font_source;
        }
        d->font = found;
    }
    else
    {
        memset(&d->value.appearance, 0, sizeof(d->value.appearance));
        d->value.appearance.enabled = 1;
        d->value.appearance.index_base = d->value.appearance.index_range = 15;
        d->value.appearance.ambient = .1f;
        d->value.appearance.diffuse = d->value.appearance.specular = .6f;
        d->value.appearance.shininess = 50;
        d->value.appearance.mapping[0] = d->value.appearance.mapping[3] = 1;
    }
    if (!d->font_count) { free_catalogues(d); return 0; }
    d->font_tiles = (TextTile *)calloc((size_t)d->font_count, sizeof(*d->font_tiles));
    if (!d->font_tiles) { free_catalogues(d); return 0; }
    if (d->font >= d->font_count) d->font = d->font_count - 1;
    d->player = player;
    d->object = object;
    d->document = marcus_document(player);
    d->chosen = d->material = -1;
    material_count = marcus_material_count(player, content);
    swatches = NULL;
    if (material_count > 0)
    {
        d->materials = (MarcusProp *)calloc((size_t)material_count, sizeof(*d->materials));
        swatches = (TextSwatch *)calloc((size_t)material_count, sizeof(*swatches));
        if (!d->materials || !swatches) { free(swatches); free_catalogues(d); return 0; }
    }
    memset(palette, 0, sizeof(palette));
    memset(hues, 0, sizeof(hues));
    text_preview_palette(player, palette, hues);
    swatch_count = 0;
    for (i = 0; i < material_count; ++i)
    {
        if (!marcus_material(player, i, &entry)) continue;
        d->materials[d->material_count++] = entry;
        if (!marcus_text_material_preset(player, entry.chunk, &appearance) || appearance.textured) continue;
        index = appearance.index_base + appearance.index_range / 2;
        if (index > 255) index = 255;
        rgb = palette[index];
        found = 0;
        for (j = 0; j < swatch_count; ++j)
            if (swatches[j].rgb == rgb) found = 1;
        if (found) continue;
        swatches[swatch_count].material = d->material_count - 1;
        swatches[swatch_count].rgb = rgb;
        swatches[swatch_count++].hue = hues[index];
    }
    if (swatch_count) qsort(swatches, (size_t)swatch_count, sizeof(*swatches), compare_swatches);
    d->swatch_count = swatch_count > 12 ? 12 : swatch_count;
    for (i = 0; i < d->swatch_count; ++i)
        d->swatches[i] = swatches[swatch_count > 12 ? i * (swatch_count - 1) / 11 : i];
    free(swatches);
    d->material_tiles = (TextTile *)calloc((size_t)d->material_count + 1, sizeof(*d->material_tiles));
    if (!d->material_tiles) { free_catalogues(d); return 0; }
    read_uv(d);
    SDL_DestroyTexture(d->texture);
    d->texture = NULL;
    d->yaw = 20;
    d->pitch = -10;
    d->zoom = 1;
    d->preview_dirty = 1;
    d->view_dirty = 0;
    d->wording_dirty = 0;
    d->wording_edited_at = 0;
    d->last_preview = 0;
    d->error[0] = 0;
    d->open = d->opening = 1;
    return 1;
}
int text_dialog_show(MarcusTextDialog *d, MarcusPlayer *player, SDL_Window *window,
                     const char *content, const char *resources)
{
    return show(d, player, window, content, resources, -1);
}
int text_dialog_edit(MarcusTextDialog *d, MarcusPlayer *player, SDL_Window *window,
                     const char *content, const char *resources, int object)
{
    return object >= 0 && show(d, player, window, content, resources, object);
}
int text_dialog_event(MarcusTextDialog *d, const SDL_Event *event)
{
    /* prop_gizmo_event has already fed the shared ImGui SDL backend. Keep
     * modal input away from the legacy application controls; ImGui edits it. */
    if (!text_dialog_visible(d)) return 0;
    switch (event->type)
    {
    case SDL_EVENT_KEY_DOWN:
    case SDL_EVENT_KEY_UP:
    case SDL_EVENT_TEXT_INPUT:
    case SDL_EVENT_TEXT_EDITING:
    case SDL_EVENT_MOUSE_MOTION:
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
    case SDL_EVENT_MOUSE_BUTTON_UP:
    case SDL_EVENT_MOUSE_WHEEL:
        return 1;
    default:
        return 0;
    }
}
static void update_preview(MarcusTextDialog *d, SDL_Renderer *renderer)
{
    MarcusImage image;
    MarcusText text;
    Uint64 now;
    if (text_preview_poll(d->preview, &image) == 1)
    {
        if (!d->texture)
        {
            d->texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STREAMING,
                                           image.width, image.height);
            if (d->texture) SDL_SetTextureScaleMode(d->texture, SDL_SCALEMODE_LINEAR);
        }
        if (d->texture) SDL_UpdateTexture(d->texture, NULL, image.rgba, image.pitch);
    }
    now = SDL_GetTicks();
    /* Debounce template edits only. Camera movement reuses the loaded renderer
     * and can submit every frame without waiting for its previous response. */
    if (d->preview_dirty && now - d->last_preview >= 180 && d->value.text[0] &&
        SDL_utf8strlen(d->value.text) <= 50)
    {
        d->preview_dirty = 0;
        d->last_preview = now;
        text = value(d);
        text_preview_start(d->preview, d->player, d->object, &text, d->resources, d->yaw, d->pitch, d->zoom);
    }
    if (d->view_dirty && text_preview_view(d->preview, d->yaw, d->pitch, d->zoom))
        d->view_dirty = 0;
}
static int text_edited(ImGuiInputTextCallbackData *data)
{
    MarcusTextDialog *d;
    d = (MarcusTextDialog *)data->UserData;
    d->wording_dirty = 1;
    d->wording_edited_at = SDL_GetTicks();
    changed(d);
    return 0;
}
static int tile_count(const MarcusTextDialog *d, int kind)
{
    return kind == 0 ? d->font_count : kind == 1 ? 10 : d->material_count + 1;
}
static TextTile *tile_at(MarcusTextDialog *d, int kind, int index)
{
    return kind == 0 ? &d->font_tiles[index] : kind == 1 ? &d->shape_tiles[index] : &d->material_tiles[index];
}
static const char *tile_name(const MarcusTextDialog *d, int kind, int index)
{
    char generated[64];
    if (kind == 0) return d->fonts[index].entry.name;
    if (kind == 1) return shapes[index];
    if (index < d->material_count)
    {
        SDL_snprintf(generated, sizeof(generated), "Material %lu", d->materials[index].chunk);
        return strcmp(d->materials[index].name, generated) ? d->materials[index].name : "";
    }
    return d->object >= 0 ? "Current material" : "Default material";
}
static void reset_wanted(MarcusTextDialog *d)
{
    int kind, i;
    for (kind = 0; kind < 3; ++kind)
        for (i = 0; i < tile_count(d, kind); ++i) tile_at(d, kind, i)->wanted = 0;
}
static void invalidate_thumbnails(MarcusTextDialog *d, int first_kind)
{
    int kind, i;
    if (d->loading_index >= 0 && d->loading_kind >= first_kind)
    {
        text_preview_stop(d->thumbnails);
        d->loading_index = -1;
    }
    for (kind = first_kind; kind < 3; ++kind)
        for (i = 0; i < tile_count(d, kind); ++i)
        {
            SDL_DestroyTexture(tile_at(d, kind, i)->texture);
            memset(tile_at(d, kind, i), 0, sizeof(TextTile));
        }
}
static void font_changed(MarcusTextDialog *d)
{
    invalidate_thumbnails(d, 1);
    changed(d);
}
static int tile_button(MarcusTextDialog *d, int kind, int index, ImVec2 size, int selected)
{
    TextTile *tile;
    ImTextureRef texture;
    int clicked;
    tile = tile_at(d, kind, index);
    if (selected) igPushStyleColor_Vec4(ImGuiCol_Button, colour(0x375e64));
    if (tile->texture)
    {
        memset(&texture, 0, sizeof(texture));
        texture._TexID = (ImTextureID)(size_t)tile->texture;
        clicked = igImageButton("##sample", texture, size, vec(0, 0), vec(1, 1), colour(0x13161a), colour(0xffffff));
    }
    else clicked = igButton(!d->value.text[0] || SDL_utf8strlen(d->value.text) > 50 ? "##sample" :
                            tile->attempted == 2 ? "No preview" : "Loading...", size);
    if (igIsItemVisible() && !tile->attempted) tile->wanted = 1;
    if (igIsItemHovered(0) && tile_name(d, kind, index)[0]) igSetTooltip("%s", tile_name(d, kind, index));
    if (selected) igPopStyleColor(1);
    return clicked;
}
static void gallery_picker(MarcusTextDialog *d, int kind, const char *label, int selected)
{
    int i, count, columns;
    const char *name;
    ImGuiViewport *viewport;
    ImVec2 available, image_size;
    const ImGuiStyle *style;
    igPushID_Int(kind);
    igTextUnformatted(label, NULL);
    if (tile_button(d, kind, selected, vec(96, 72), 0)) igOpenPopup_Str("Gallery", 0);
    name = tile_name(d, kind, selected);
    if (name[0])
    {
        igSameLine(0, 12);
        igTextWrapped("%s", name);
    }
    viewport = igGetMainViewport();
    igSetNextWindowPos(viewport->Pos, ImGuiCond_Always, vec(0, 0));
    igSetNextWindowSize(viewport->Size, ImGuiCond_Always);
    if (igBeginPopup("Gallery", ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize))
    {
        if (igButton("Back", vec(0, 0))) igCloseCurrentPopup();
        igSameLine(0, -1);
        igSeparatorText(label);
        available = igGetContentRegionAvail();
        style = igGetStyle();
        columns = (int)(available.x / 176);
        if (columns < 1) columns = 1;
        if (columns > 32) columns = 32;
        count = tile_count(d, kind) - (kind == 2 ? 1 : 0);
        if (igBeginTable("Samples", columns, ImGuiTableFlags_SizingStretchSame, vec(0, 0), 0))
        {
            for (i = 0; i < count; ++i)
            {
                igTableNextColumn();
                igPushID_Int(i);
                image_size = igGetContentRegionAvail();
                image_size.x = clamp(image_size.x - style->FramePadding.x * 2, 1, 1024);
                image_size.y = image_size.x * .75f;
                if (tile_button(d, kind, i, image_size, selected == i))
                {
                    if (kind == 0) { d->font = i; font_changed(d); }
                    else if (kind == 1) { d->value.shape = i; changed(d); }
                    else choose_material(d, i);
                    igCloseCurrentPopup();
                }
                name = tile_name(d, kind, i);
                if (name[0]) igTextWrapped("%s", name);
                igPopID();
            }
            igEndTable();
        }
        igEndPopup();
    }
    igPopID();
}
static void update_thumbnails(MarcusTextDialog *d, SDL_Renderer *renderer)
{
    MarcusImage image;
    MarcusText sample;
    TextTile *tile;
    SDL_Surface *surface, *small;
    int result, kind, i, object;
    if (!d->value.text[0] || SDL_utf8strlen(d->value.text) > 50 || SDL_GetTicks() - d->wording_edited_at < 180) return;
    if (d->loading_index >= 0)
    {
        result = text_preview_poll(d->thumbnails, &image);
        if (!result) return;
        tile = tile_at(d, d->loading_kind, d->loading_index);
        if (result > 0)
        {
            surface = SDL_CreateSurfaceFrom(image.width, image.height, SDL_PIXELFORMAT_RGBA32, (void *)image.rgba, image.pitch);
            small = surface ? SDL_ScaleSurface(surface, 128, 96, SDL_SCALEMODE_LINEAR) : NULL;
            tile->texture = small ? SDL_CreateTextureFromSurface(renderer, small) : NULL;
            SDL_DestroySurface(small);
            SDL_DestroySurface(surface);
            if (tile->texture) SDL_SetTextureScaleMode(tile->texture, SDL_SCALEMODE_LINEAR);
        }
        if (!tile->texture) tile->attempted = 2;
        d->loading_index = -1;
    }
    for (kind = 0; kind < 3; ++kind)
        for (i = 0; i < tile_count(d, kind); ++i)
        {
            tile = tile_at(d, kind, i);
            if (!tile->wanted || tile->attempted) continue;
            sample = value(d);
            sample.shape = 0;
            object = -1;
            if (kind < 2)
            {
                memset(&sample.appearance, 0, sizeof(sample.appearance));
                sample.appearance.enabled = 1;
                sample.appearance.index_base = sample.appearance.index_range = 15;
                sample.appearance.ambient = .1f;
                sample.appearance.diffuse = sample.appearance.specular = .6f;
                sample.appearance.shininess = 50;
                sample.appearance.mapping[0] = sample.appearance.mapping[3] = 1;
            }
            if (kind == 0) { sample.font = d->fonts[i].entry.chunk; sample.font_source = d->fonts[i].source; }
            else if (kind == 1) sample.shape = i;
            else if (i == d->material_count) object = d->object;
            else if (!marcus_text_material_preset(d->player, d->materials[i].chunk, &sample.appearance))
            { tile->attempted = 2; continue; }
            tile->attempted = 1;
            if (!text_preview_start(d->thumbnails, d->player, object, &sample, d->resources, 20, -10, 1))
            { tile->attempted = 2; return; }
            d->loading_kind = kind;
            d->loading_index = i;
            return;
        }
}
static void catalogues(MarcusTextDialog *d)
{
    int i;
    gallery_picker(d, 0, "3D font", d->font);
    gallery_picker(d, 1, "Shape", d->value.shape);
    gallery_picker(d, 2, "Material", d->material >= 0 ? d->material : d->material_count);
    for (i = 0; i < d->swatch_count; ++i)
    {
        igPushID_Int(i);
        if (i && i != 6) igSameLine(0, 6);
        if (igColorButton("##swatch", colour(d->swatches[i].rgb), ImGuiColorEditFlags_NoTooltip, vec(36, 24)))
            choose_material(d, d->swatches[i].material);
        if (igIsItemHovered(0) && tile_name(d, 2, d->swatches[i].material)[0])
            igSetTooltip("%s", tile_name(d, 2, d->swatches[i].material));
        igPopID();
    }
}
static void field(MarcusTextDialog *d, const char *label, float *number, float step, float low, float high, int uv)
{
    igTextUnformatted(label, NULL);
    igSetNextItemWidth(-1);
    igPushID_Str(label);
    if (igDragFloat("##value", number, step, low, high, "%.2f", ImGuiSliderFlags_AlwaysClamp))
    {
        if (uv) d->uv_dirty = 1;
        changed(d);
    }
    igPopID();
}
static void fields(MarcusTextDialog *d)
{
    if (igBeginTable("Material properties", 2, ImGuiTableFlags_SizingStretchSame, vec(0, 0), 0))
    {
        igTableNextColumn();
        igSeparatorText("Lighting");
        field(d, "Ambient", &d->value.appearance.ambient, .01f, 0, 1, 0);
        field(d, "Diffuse", &d->value.appearance.diffuse, .01f, 0, 1, 0);
        field(d, "Specular", &d->value.appearance.specular, .01f, 0, 1, 0);
        field(d, "Shininess", &d->value.appearance.shininess, 1, 0, 1000, 0);
        igTableNextColumn();
        igSeparatorText("Texture placement");
        igBeginDisabled(!d->value.appearance.textured);
        field(d, "U scale", &d->uv[0], .01f, .01f, 32, 1);
        field(d, "V scale", &d->uv[1], .01f, .01f, 32, 1);
        field(d, "Rotation", &d->uv[2], 1, -180, 180, 1);
        field(d, "U offset", &d->uv[3], .01f, -128, 128, 1);
        field(d, "V offset", &d->uv[4], .01f, -128, 128, 1);
        igEndDisabled();
        igEndTable();
    }
}
static void preview_controls(MarcusTextDialog *d)
{
    ImTextureRef texture;
    ImVec2 size;
    ImGuiIO *io;
    const ImGuiStyle *style;
    size = igGetContentRegionAvail();
    style = igGetStyle();
    /* ImageButton's image size excludes its frame padding. */
    size.x = clamp(size.x - style->FramePadding.x * 2, 1, 4096);
    size.y = size.x * .75f;
    memset(&texture, 0, sizeof(texture));
    texture._TexID = (ImTextureID)(size_t)d->texture;
    if (d->texture)
        igImageButton("Preview", texture, size, vec(0, 0), vec(1, 1), colour(0x13161a), colour(0xffffff));
    else
        igButton(text_preview_error(d->preview)[0] ? "Preview unavailable" : "Loading preview...",
                 vec(size.x + style->FramePadding.x * 2, size.y + style->FramePadding.y * 2));
    io = igGetIO_Nil();
    if (igIsItemActive() && io->MouseDown[0] && (io->MouseDelta.x || io->MouseDelta.y))
    {
        d->yaw -= io->MouseDelta.x * .6f;
        d->pitch = clamp(d->pitch + io->MouseDelta.y * .6f, -89, 89);
        view_changed(d);
    }
    if (igIsItemHovered(0))
    {
        igSetItemKeyOwner_Nil(ImGuiKey_MouseWheelY);
        if (io->MouseWheel)
        {
            d->zoom = clamp(d->zoom * (float)pow(1.15, io->MouseWheel), .25f, 4);
            view_changed(d);
        }
    }
    if (text_preview_error(d->preview)[0])
    {
        igPushStyleColor_Vec4(ImGuiCol_Text, colour(0xe4a1a1));
        igTextWrapped("%s", text_preview_error(d->preview));
        igPopStyleColor(1);
    }
}
void text_dialog_draw(MarcusTextDialog *d, MarcusUI *ui, int width, int height)
{
    const char *title;
    ImVec2 size;
    ImGuiStorage *storage;
    ImGuiID visible_key;
    int opening, save, count, valid;
    if (!text_dialog_visible(d)) return;
    reset_wanted(d);
    opening = d->opening;
    title = d->object >= 0 ? "Edit 3D text###MarcusText" : "New 3D text###MarcusText";
    size = vec((float)width - 32, (float)height - 32);
    if (size.x > 1060) size.x = 1060;
    igPushStyleVar_Vec2(ImGuiStyleVar_WindowPadding, vec(20, 16));
    igPushStyleVar_Vec2(ImGuiStyleVar_FramePadding, vec(8, 6));
    igPushStyleVar_Vec2(ImGuiStyleVar_ItemSpacing, vec(10, 8));
    if (opening)
    {
        igOpenPopup_Str(title, 0);
        igSetNextWindowPos(vec(width * .5f, height * .5f), ImGuiCond_Always, vec(.5f, .5f));
    }
    igSetNextWindowSize(size, ImGuiCond_Always);
    /* Let ImGui store its bool ABI value; application state stays C89 ints. */
    storage = igGetStateStorage();
    visible_key = igGetID_Str("MarcusTextVisible");
    ImGuiStorage_SetBool(storage, visible_key, 1);
    if (igBeginPopupModal(title, ImGuiStorage_GetBoolRef(storage, visible_key, 1),
                         ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoResize))
    {
        d->opening = 0;
        update_preview(d, ui->renderer);
        count = (int)SDL_utf8strlen(d->value.text);
        igText("Text / %d of 50 characters", count);
        igSetNextItemWidth(-1);
        if (opening) igSetKeyboardFocusHere(0);
        save = igInputText("##words", d->value.text, sizeof(d->value.text),
                           ImGuiInputTextFlags_AutoSelectAll | ImGuiInputTextFlags_EnterReturnsTrue |
                           ImGuiInputTextFlags_CallbackEdit, text_edited, d);
        /* InputText has copied its edited buffer back before cache invalidation.
         * No thumbnail images have been submitted to this frame's draw list yet. */
        if (d->wording_dirty)
        {
            invalidate_thumbnails(d, 0);
            d->wording_dirty = 0;
        }
        count = (int)SDL_utf8strlen(d->value.text);
        valid = count > 0 && count <= 50;
        if (!valid) igTextColored(colour(0xe4a1a1), "Enter between 1 and 50 characters.");
        if (igBeginChild_Str("Text options", vec(0, -108), 0, 0))
        {
            if (igBeginTable("Text and preview", 2, ImGuiTableFlags_SizingStretchSame, vec(0, 0), 0))
            {
                igTableNextColumn();
                if (igBeginChild_Str("Properties", vec(0, 0), 0, 0))
                {
                    catalogues(d);
                    fields(d);
                    igTextDisabled("Drag values; Ctrl+click to type.");
                }
                igEndChild();
                igTableNextColumn();
                preview_controls(d);
                igEndTable();
            }
        }
        igEndChild();
        igSeparator();
        igTextWrapped("%s", d->object >= 0 ? "Keeps placement and animation. Appearance applies to the whole text." :
                      "Inserted at this frame using the camera's next insertion point.");
        if (d->error[0]) igTextColored(colour(0xe4a1a1), "%s", d->error);
        else igTextDisabled("Tab to switch controls / Enter in the text field to save / Escape to cancel");
        if (igButton("Cancel", vec(100, 0))) close_dialog(d);
        igSameLine(0, -1);
        igBeginDisabled(!valid);
        if (igButton(d->object >= 0 ? "Save changes" : "Add 3D text", vec(140, 0))) save = 1;
        igEndDisabled();
        if (igShortcut_Nil(ImGuiKey_Escape, 0)) close_dialog(d);
        if (d->open && save && valid) submit(d);
        if (d->open) update_thumbnails(d, ui->renderer);
        if (!d->open) igCloseCurrentPopup();
        igEndPopup();
    }
    if (!ImGuiStorage_GetBool(storage, visible_key, 1)) close_dialog(d);
    igPopStyleVar(3);
}
int text_dialog_take(MarcusTextDialog *d, int *edited)
{
    int chosen;
    if (!d) return -1;
    if (edited) *edited = d->object >= 0 && d->chosen >= 0;
    chosen = d->chosen;
    d->chosen = -1;
    return chosen;
}
