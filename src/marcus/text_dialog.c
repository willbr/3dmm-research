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
    int uv_dirty, preview_dirty;
    float yaw, pitch, zoom;
    Uint64 last_preview;
    char *resources;
    char error[1024];
    SDL_Texture *texture;
    MarcusTextPreview *preview;
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
    if (!d->preview) { free(d); return NULL; }
    d->chosen = d->object = d->material = -1;
    SDL_strlcpy(d->value.text, "Text", sizeof(d->value.text));
    return d;
}
void text_dialog_destroy(MarcusTextDialog *d)
{
    if (!d) return;
    text_preview_destroy(d->preview);
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
    read_uv(d);
    SDL_DestroyTexture(d->texture);
    d->texture = NULL;
    d->yaw = 20;
    d->pitch = -10;
    d->zoom = 1;
    d->preview_dirty = 1;
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
    SDL_Surface *surface;
    SDL_Texture *texture;
    Uint64 now;
    if (text_preview_poll(d->preview, &image) == 1)
    {
        surface = SDL_CreateSurfaceFrom(image.width, image.height, SDL_PIXELFORMAT_RGBA32,
                                       (void *)image.rgba, image.pitch);
        texture = surface ? SDL_CreateTextureFromSurface(renderer, surface) : NULL;
        SDL_DestroySurface(surface);
        if (texture)
        {
            SDL_DestroyTexture(d->texture);
            d->texture = texture;
            SDL_SetTextureScaleMode(texture, SDL_SCALEMODE_LINEAR);
        }
    }
    now = SDL_GetTicks();
    if (!d->preview_dirty || now - d->last_preview < 180 || !d->value.text[0] ||
        SDL_utf8strlen(d->value.text) > 50) return;
    d->preview_dirty = 0;
    d->last_preview = now;
    text = value(d);
    text_preview_start(d->preview, d->player, d->object, &text, d->resources, d->yaw, d->pitch, d->zoom);
}
static int text_edited(ImGuiInputTextCallbackData *data)
{
    changed((MarcusTextDialog *)data->UserData);
    return 0;
}
static void catalogues(MarcusTextDialog *d)
{
    const char *name;
    int i, selected;
    igSetNextItemWidth(-1);
    if (igBeginCombo("##font", d->fonts[d->font].entry.name, 0))
    {
        for (i = 0; i < d->font_count; ++i)
        {
            igPushID_Int(i);
            selected = i == d->font;
            if (igSelectable_Bool(d->fonts[i].entry.name, selected != 0, 0, vec(0, 0)))
            { d->font = i; changed(d); }
            if (selected) igSetItemDefaultFocus();
            igPopID();
        }
        igEndCombo();
    }
    igTextUnformatted("Shape", NULL);
    igSetNextItemWidth(-1);
    if (igBeginCombo("##shape", shapes[d->value.shape], 0))
    {
        for (i = 0; i < 10; ++i)
        {
            selected = i == d->value.shape;
            if (igSelectable_Bool(shapes[i], selected != 0, 0, vec(0, 0)))
            { d->value.shape = i; changed(d); }
            if (selected) igSetItemDefaultFocus();
        }
        igEndCombo();
    }
    name = d->material >= 0 ? d->materials[d->material].name : d->object >= 0 ? "Current material" : "Default material";
    igTextUnformatted("Material", NULL);
    igSetNextItemWidth(-1);
    if (igBeginCombo("##material", name, 0))
    {
        for (i = 0; i < d->material_count; ++i)
        {
            igPushID_Int(i);
            selected = i == d->material;
            if (igSelectable_Bool(d->materials[i].name, selected != 0, 0, vec(0, 0))) choose_material(d, i);
            if (selected) igSetItemDefaultFocus();
            igPopID();
        }
        igEndCombo();
    }
    for (i = 0; i < d->swatch_count; ++i)
    {
        igPushID_Int(i);
        if (i && i != 6) igSameLine(0, 6);
        if (igColorButton("##swatch", colour(d->swatches[i].rgb), ImGuiColorEditFlags_NoTooltip, vec(36, 24)))
            choose_material(d, d->swatches[i].material);
        if (igIsItemHovered(0)) igSetTooltip("%s", d->materials[d->swatches[i].material].name);
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
    size = igGetContentRegionAvail();
    size.y = size.x * .75f;
    memset(&texture, 0, sizeof(texture));
    texture._TexID = (ImTextureID)(size_t)d->texture;
    if (d->texture)
        igImageButton("Preview", texture, size, vec(0, 0), vec(1, 1), colour(0x13161a), colour(0xffffff));
    else
        igButton(text_preview_error(d->preview)[0] ? "Preview unavailable" : "Loading preview...", size);
    io = igGetIO_Nil();
    if (igIsItemActive() && io->MouseDown[0] && (io->MouseDelta.x || io->MouseDelta.y))
    {
        d->yaw += io->MouseDelta.x * .6f;
        d->pitch = clamp(d->pitch + io->MouseDelta.y * .6f, -89, 89);
        changed(d);
    }
    if (igIsItemHovered(0))
    {
        igSetItemKeyOwner_Nil(ImGuiKey_MouseWheelY);
        if (io->MouseWheel)
        {
            d->zoom = clamp(d->zoom * (float)pow(1.15, io->MouseWheel), .25f, 4);
            changed(d);
        }
    }
    if (igButton("Rotate <", vec(0, 0))) { d->yaw -= 30; changed(d); }
    igSameLine(0, -1);
    if (igButton("Rotate >", vec(0, 0))) { d->yaw += 30; changed(d); }
    igSameLine(0, -1);
    if (igButton("Zoom -", vec(0, 0))) { d->zoom = clamp(d->zoom * .8f, .25f, 4); changed(d); }
    igSameLine(0, -1);
    if (igButton("Zoom +", vec(0, 0))) { d->zoom = clamp(d->zoom * 1.25f, .25f, 4); changed(d); }
    igSameLine(0, -1);
    if (igButton("Reset", vec(0, 0))) { d->yaw = 20; d->pitch = -10; d->zoom = 1; changed(d); }
    igTextWrapped("Drag to rotate / Wheel to zoom. Preview pose does not change the movie.");
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
    opening = d->opening;
    title = d->object >= 0 ? "Edit 3D text###MarcusText" : "New 3D text###MarcusText";
    size = vec((float)width - 32, (float)height - 32);
    if (size.x > 1060) size.x = 1060;
    if (size.y > 760) size.y = 760;
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
        count = (int)SDL_utf8strlen(d->value.text);
        valid = count > 0 && count <= 50;
        if (!valid) igTextColored(colour(0xe4a1a1), "Enter between 1 and 50 characters.");
        if (igBeginChild_Str("Text options", vec(0, -108), 0, 0))
        {
            if (igBeginTable("Text and preview", 2, ImGuiTableFlags_SizingStretchSame, vec(0, 0), 0))
            {
                igTableNextColumn();
                igTextUnformatted("3D font", NULL);
                catalogues(d);
                fields(d);
                igTextDisabled("Drag values; Ctrl+click to type.");
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
