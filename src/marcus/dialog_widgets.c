#include "dialog_widgets.h"
#include "dialog_host.h"
#include <stdlib.h>
static SDL_Texture **retired_textures;
static size_t retired_count, retired_capacity;
SDL_Texture *dialog_preview_texture(SDL_Renderer *renderer, const unsigned char *rgba, int width, int height, int pitch)
{
    SDL_Surface *surface;
    SDL_Texture *texture;
    if (!rgba || width <= 0 || height <= 0) return NULL;
    surface = SDL_CreateSurfaceFrom(width, height, SDL_PIXELFORMAT_RGBA32, (void *)rgba, pitch);
    if (!surface) return NULL;
    texture = SDL_CreateTextureFromSurface(renderer, surface);
    SDL_DestroySurface(surface);
    return texture;
}
void dialog_retire_texture(SDL_Texture *texture)
{
    SDL_Texture **next;
    size_t capacity;
    if (!texture) return;
    if (retired_count == retired_capacity) {
        capacity = retired_capacity ? retired_capacity * 2 : 32;
        next = (SDL_Texture **)realloc(retired_textures, capacity * sizeof(*next));
        if (!next) { SDL_Log("Unable to retire a dialog texture; retaining it until process exit."); return; }
        retired_textures = next; retired_capacity = capacity;
    }
    retired_textures[retired_count++] = texture;
}
void dialog_release_textures(void)
{
    size_t i;
    for (i = 0; i < retired_count; ++i) SDL_DestroyTexture(retired_textures[i]);
    free(retired_textures); retired_textures = NULL; retired_count = retired_capacity = 0;
}
ImVec2 dialog_vec(float x, float y) { ImVec2 v; v.x = x; v.y = y; return v; }
int dialog_input_event(const SDL_Event *event)
{
    switch (event->type) {
    case SDL_EVENT_KEY_DOWN: case SDL_EVENT_KEY_UP: case SDL_EVENT_TEXT_INPUT: case SDL_EVENT_TEXT_EDITING:
    case SDL_EVENT_MOUSE_MOTION: case SDL_EVENT_MOUSE_BUTTON_DOWN: case SDL_EVENT_MOUSE_BUTTON_UP: case SDL_EVENT_MOUSE_WHEEL:
    case SDL_EVENT_DROP_FILE: return 1;
    default: return 0;
    }
}
int dialog_begin(const char *name, int *open, int width, int height, float preferred_width, float preferred_height)
{
    ImVec2 size;
    ImGuiStorage *storage;
    ImGuiID key;
    ImVec4 background;
    int shown;
    if (!igGetCurrentContext() || !*open) return 0;
    size = dialog_vec((float)width - 32, (float)height - 32);
    if (preferred_width > 0 && size.x > preferred_width) size.x = preferred_width;
    if (preferred_height > 0 && size.y > preferred_height) size.y = preferred_height;
    if (size.x < 200) size.x = 200;
    if (size.y < 120) size.y = 120;
    if (!igIsPopupOpen_Str(name, 0)) igOpenPopup_Str(name, 0);
    igSetNextWindowPos(dialog_vec(width * .5f, height * .5f), ImGuiCond_Always, dialog_vec(.5f, .5f));
    igSetNextWindowSize(size, ImGuiCond_Always);
    storage = igGetStateStorage(); key = igGetID_Str(name); ImGuiStorage_SetBool(storage, key, 1);
    background = igGetStyle()->Colors[ImGuiCol_PopupBg]; background.w = 1;
    igPushStyleColor_Vec4(ImGuiCol_PopupBg, background);
    igPushStyleVar_Vec2(ImGuiStyleVar_WindowPadding, dialog_vec(16, 14));
    igPushStyleVar_Vec2(ImGuiStyleVar_ItemSpacing, dialog_vec(10, 8));
    shown = igBeginPopupModal(name, ImGuiStorage_GetBoolRef(storage, key, 1),
        ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize);
    if (!ImGuiStorage_GetBool(storage, key, 1)) *open = 0;
    if (!shown) { igPopStyleVar(2); igPopStyleColor(1); }
    return shown;
}
void dialog_end(int closed)
{
    if (closed) igCloseCurrentPopup();
    igEndPopup(); igPopStyleVar(2); igPopStyleColor(1);
}
void dialog_close(const char *name)
{
    if (igGetCurrentContext() && igIsPopupOpen_Str(name, 0) && igBeginPopupModal(name, NULL, ImGuiWindowFlags_NoSavedSettings))
    { igCloseCurrentPopup(); igEndPopup(); }
}
int dialog_checkbox(const char *label, int *value)
{
    ImGuiStorage *storage;
    ImGuiID key;
    int changed;
    storage = igGetStateStorage(); key = igGetID_Str(label); ImGuiStorage_SetBool(storage, key, *value);
    changed = igCheckbox(label, ImGuiStorage_GetBoolRef(storage, key, *value));
    *value = ImGuiStorage_GetBool(storage, key, *value); return changed;
}
int dialog_okhsl(float hsl[3])
{
    int changed;
    igPushItemWidth(124);
    changed = igSliderFloat("Hue", hsl, 0, 360, "%.0f", ImGuiSliderFlags_AlwaysClamp);
    changed |= igSliderFloat("Saturation", hsl + 1, 0, 1, "%.3f", ImGuiSliderFlags_AlwaysClamp);
    changed |= igSliderFloat("Lightness", hsl + 2, 0, 1, "%.3f", ImGuiSliderFlags_AlwaysClamp);
    igPopItemWidth();
    return changed;
}
void dialog_image(SDL_Texture *texture, ImVec2 size)
{
    ImTextureRef ref;
    ImVec2 cursor, fitted;
    float w, h, scale;
    if (!texture) { igDummy(size); return; }
    SDL_GetTextureSize(texture, &w, &h);
    if (w <= 0 || h <= 0) { igDummy(size); return; }
    scale = size.x / w; if (h * scale > size.y) scale = size.y / h;
    fitted = dialog_vec(w * scale, h * scale); cursor = igGetCursorScreenPos();
    igDummy(size); ref._TexData = NULL; ref._TexID = (ImTextureID)(size_t)texture;
    ImDrawList_AddImage(igGetWindowDrawList(), ref,
        dialog_vec(cursor.x + (size.x - fitted.x) / 2, cursor.y + (size.y - fitted.y) / 2),
        dialog_vec(cursor.x + (size.x + fitted.x) / 2, cursor.y + (size.y + fitted.y) / 2),
        dialog_vec(0, 0), dialog_vec(1, 1), 0xffffffffU);
}
void dialog_object_picture(SDL_Texture *texture, ImVec2 origin, ImVec2 size)
{
    ImTextureRef ref;
    ImVec2 fitted, start;
    ImDrawList *draw;
    float width, height, scale;
    if (size.x <= 0 || size.y <= 0) return;
    draw = igGetWindowDrawList();
    ImDrawList_AddRectFilled(draw, origin, dialog_vec(origin.x + size.x, origin.y + size.y), 0xff1a1613U, 0, 0);
    if (!texture || !SDL_GetTextureSize(texture, &width, &height) || width <= 0 || height <= 0) return;
    scale = SDL_min(size.x / width, size.y / height);
    fitted = dialog_vec(width * scale, height * scale);
    start = dialog_vec(origin.x + (size.x - fitted.x) * .5f, origin.y + (size.y - fitted.y) * .5f);
    ref._TexData = NULL; ref._TexID = (ImTextureID)(size_t)texture;
    ImDrawList_AddImage(draw, ref, start, dialog_vec(start.x + fitted.x, start.y + fitted.y),
                        dialog_vec(0, 0), dialog_vec(1, 1), 0xffffffffU);
}
int dialog_thumbnail(const char *id, SDL_Texture *texture, ImVec2 size, int selected, const char *placeholder)
{
    ImVec2 origin, padding, fitted;
    ImVec4 colour;
    ImTextureRef ref;
    char label[128];
    float w, h, scale;
    int clicked;
    if (selected) { colour = igGetStyle()->Colors[ImGuiCol_ButtonActive]; igPushStyleColor_Vec4(ImGuiCol_Button, colour); }
    origin = igGetCursorScreenPos(); padding = igGetStyle()->FramePadding;
    igPushID_Str(id); SDL_snprintf(label, sizeof(label), "%s###Thumbnail", texture ? "" : placeholder);
    clicked = igButton(label, size); igPopID();
    if (texture) {
        SDL_GetTextureSize(texture, &w, &h);
        if (w > 0 && h > 0) {
            scale = (size.x - padding.x * 2) / w;
            if (h * scale > size.y - padding.y * 2) scale = (size.y - padding.y * 2) / h;
            fitted = dialog_vec(w * scale, h * scale); ref._TexData = NULL; ref._TexID = (ImTextureID)(size_t)texture;
            ImDrawList_AddImage(igGetWindowDrawList(), ref,
                dialog_vec(origin.x + (size.x - fitted.x) / 2, origin.y + (size.y - fitted.y) / 2),
                dialog_vec(origin.x + (size.x + fitted.x) / 2, origin.y + (size.y + fitted.y) / 2),
                dialog_vec(0, 0), dialog_vec(1, 1), 0xffffffffU);
        }
    }
    if (selected) igPopStyleColor(1);
    return clicked;
}
