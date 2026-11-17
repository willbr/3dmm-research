/* Marcus costume authoring. Application logic and widgets use C89 + cimgui. */
#include "costume_dialog.h"
#define CIMGUI_DEFINE_ENUMS_AND_STRUCTS
#include <cimgui.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

typedef struct CostumeTile
{
    MarcusCostume value;
    SDL_Texture *texture;
    int wanted, attempted;
} CostumeTile;
struct MarcusCostumeDialog
{
    MarcusPlayer *player;
    CostumeTile *tiles;
    int count, object, open, opening, chosen, invalid, preview_dirty;
    int layout_width, layout_height;
    int preview_width, preview_height;
    MarcusPlayer *availability_player;
    unsigned long availability_document;
    long availability_frame;
    int availability_object, available;
    int button_drawn, button_visible;
    SDL_FRect button_rect;
    unsigned long document;
    float yaw, pitch, zoom;
    SDL_Texture *preview;
    char name[192], error[1024];
};
static ImVec2 vec(float x, float y)
{
    ImVec2 v;
    v.x = x; v.y = y; return v;
}
static ImVec4 colour(unsigned rgb)
{
    ImVec4 v;
    v.x = ((rgb >> 16) & 255) / 255.0f;
    v.y = ((rgb >> 8) & 255) / 255.0f;
    v.z = (rgb & 255) / 255.0f; v.w = 1; return v;
}
static float clamp(float x, float low, float high) { return x < low ? low : x > high ? high : x; }
static void clear_images(MarcusCostumeDialog *d)
{
    int i;
    SDL_DestroyTexture(d->preview); d->preview = NULL;
    for (i = 0; i < d->count; ++i)
    {
        SDL_DestroyTexture(d->tiles[i].texture);
        d->tiles[i].texture = NULL; d->tiles[i].attempted = 0;
    }
    d->preview_dirty = 1; d->invalid = 0;
}
MarcusCostumeDialog *costume_dialog_create(void)
{
    MarcusCostumeDialog *d;
    d = (MarcusCostumeDialog *)calloc(1, sizeof(*d));
    if (d) d->chosen = -1;
    return d;
}
void costume_dialog_destroy(MarcusCostumeDialog *d)
{
    if (!d) return;
    clear_images(d); free(d->tiles); free(d);
}
int costume_dialog_visible(const MarcusCostumeDialog *d) { return d && d->open; }
int costume_dialog_event(MarcusCostumeDialog *d, const SDL_Event *event)
{
    float x, y;
    if (!d) return 0;
    if (!costume_dialog_visible(d))
    {
        /* The shared backend already received the event. Route clicks over
         * the ImGui launch button away from the legacy properties pane. */
        if (!d->button_visible) return 0;
        if (event->type == SDL_EVENT_MOUSE_MOTION) { x = event->motion.x; y = event->motion.y; }
        else if (event->type == SDL_EVENT_MOUSE_BUTTON_DOWN || event->type == SDL_EVENT_MOUSE_BUTTON_UP)
        { x = event->button.x; y = event->button.y; }
        else return 0;
        return x >= d->button_rect.x && x < d->button_rect.x + d->button_rect.w &&
            y >= d->button_rect.y && y < d->button_rect.y + d->button_rect.h;
    }
    switch (event->type)
    {
    case SDL_EVENT_KEY_DOWN: case SDL_EVENT_KEY_UP:
    case SDL_EVENT_TEXT_INPUT: case SDL_EVENT_TEXT_EDITING:
    case SDL_EVENT_MOUSE_MOTION: case SDL_EVENT_MOUSE_BUTTON_DOWN:
    case SDL_EVENT_MOUSE_BUTTON_UP: case SDL_EVENT_MOUSE_WHEEL: return 1;
    default: return 0;
    }
}
static void show(MarcusCostumeDialog *d, MarcusPlayer *player, int object)
{
    MarcusObject value;
    int count, i;
    if (!marcus_object(player, object, &value) || value.text || !value.visible) return;
    clear_images(d); free(d->tiles); d->tiles = NULL; d->count = 0;
    d->player = player; d->object = object; d->document = marcus_document(player);
    d->chosen = -1; d->error[0] = 0; d->yaw = 25; d->pitch = -10; d->zoom = 1;
    d->preview_width = 480; d->preview_height = 600;
    SDL_strlcpy(d->name, value.name, sizeof(d->name));
    count = marcus_costume_count(player, object);
    if (count < 0) SDL_strlcpy(d->error, marcus_error(player), sizeof(d->error));
    else if (count > 512) SDL_strlcpy(d->error, "This object has more than 512 costume variants.", sizeof(d->error));
    else if (count)
    {
        d->tiles = (CostumeTile *)calloc((size_t)count, sizeof(*d->tiles));
        if (!d->tiles) SDL_strlcpy(d->error, "Cannot allocate costume choices.", sizeof(d->error));
        else for (i = 0; i < count; ++i)
        {
            if (!marcus_costume(player, object, i, &d->tiles[i].value))
            { SDL_strlcpy(d->error, marcus_error(player), sizeof(d->error)); break; }
            ++d->count;
        }
    }
    d->open = d->opening = d->preview_dirty = 1;
}
void costume_dialog_button(MarcusCostumeDialog *d, MarcusPlayer *player, int object,
    float x, float y, int enabled)
{
    int clicked, count, i;
    MarcusInfo info;
    MarcusCostume value;
    if (!d || !igGetCurrentContext()) return;
    if (!marcus_info(player, &info)) return;
    if (d->availability_player != player || d->availability_document != marcus_document(player) ||
        d->availability_object != object || d->availability_frame != info.frame)
    {
        d->availability_player = player; d->availability_document = marcus_document(player);
        d->availability_object = object; d->availability_frame = info.frame; d->available = 0;
        count = marcus_costume_count(player, object);
        for (i = 0; i < count; ++i)
            if (marcus_costume(player, object, i, &value) && !value.selected) { d->available = 1; break; }
    }
    d->button_drawn = 1;
    d->button_rect.x = x; d->button_rect.y = y; d->button_rect.w = 134; d->button_rect.h = 28;
    igSetNextWindowPos(vec(x, y), ImGuiCond_Always, vec(0, 0));
    igSetNextWindowSize(vec(134, 28), ImGuiCond_Always);
    igPushStyleVar_Vec2(ImGuiStyleVar_WindowPadding, vec(0, 0));
    igBegin("##MarcusCostumeButton", NULL, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings |
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoScrollWithMouse);
    igBeginDisabled(!enabled || !d->available);
    clicked = igButton(d->available ? "Costume" : "No costumes", vec(134, 28));
    igEndDisabled(); igEnd(); igPopStyleVar(1);
    if (clicked) show(d, player, object);
}
static int choices(MarcusCostumeDialog *d, MarcusCostume *out, int sample)
{
    int i, count;
    count = 0;
    for (i = 0; i < d->count; ++i)
        if (d->tiles[i].value.selected && (sample < 0 ||
            d->tiles[i].value.body_set != d->tiles[sample].value.body_set)) out[count++] = d->tiles[i].value;
    if (sample >= 0) out[count++] = d->tiles[sample].value;
    return count;
}
static SDL_Rect content_bounds(const MarcusImage *image, float zoom)
{
    SDL_Rect rect;
    int x, y, left, top, right, bottom, width, height;
    left = image->width; top = image->height; right = bottom = -1;
    for (y = 0; y < image->height; ++y) for (x = 0; x < image->width; ++x)
        if (image->rgba[y * image->pitch + x * 4 + 3])
        {
            if (x < left) left = x;
            if (x > right) right = x;
            if (y < top) top = y;
            if (y > bottom) bottom = y;
        }
    rect.x = rect.y = 0; rect.w = image->width; rect.h = image->height;
    if (right < left || bottom < top) return rect;
    /* Trim renderer padding, keep a small visual margin, and retain zoom-out. */
    width = (int)ceil((right - left + 1) * 1.06f / zoom);
    height = (int)ceil((bottom - top + 1) * 1.06f / zoom);
    if (width > image->width) width = image->width;
    if (height > image->height) height = image->height;
    rect.x = (left + right + 1 - width) / 2;
    rect.y = (top + bottom + 1 - height) / 2;
    if (rect.x < 0) rect.x = 0;
    if (rect.y < 0) rect.y = 0;
    if (rect.x + width > image->width) rect.x = image->width - width;
    if (rect.y + height > image->height) rect.y = image->height - height;
    rect.w = width; rect.h = height;
    return rect;
}
static SDL_Texture *render(MarcusCostumeDialog *d, SDL_Renderer *renderer, int sample)
{
    MarcusCostume selected[512];
    MarcusImage image;
    SDL_Texture *texture;
    SDL_Surface *surface;
    SDL_Rect bounds;
    int count, width, height, ok;
    count = choices(d, selected, sample);
    width = sample < 0 ? d->preview_width : 240; height = sample < 0 ? d->preview_height : 300;
    if (sample < 0) ok = marcus_costume_preview(d->player, d->object, selected, count,
        d->yaw, d->pitch, d->zoom, width, height, &image);
    else ok = marcus_costume_part_preview(d->player, d->object, d->tiles[sample].value.body_set,
        selected, count, 25, -10, 1, width, height, &image);
    if (!ok)
    { SDL_strlcpy(d->error, marcus_error(d->player), sizeof(d->error)); return NULL; }
    bounds.x = bounds.y = 0; bounds.w = image.width; bounds.h = image.height;
    if (sample < 0) bounds = content_bounds(&image, d->zoom);
    else if (image.content_rect[2] > 0 && image.content_rect[3] > 0)
    {
        bounds.x = image.content_rect[0]; bounds.y = image.content_rect[1];
        bounds.w = image.content_rect[2]; bounds.h = image.content_rect[3];
    }
    surface = SDL_CreateSurfaceFrom(bounds.w, bounds.h, SDL_PIXELFORMAT_RGBA32,
        (void *)(image.rgba + bounds.y * image.pitch + bounds.x * 4), image.pitch);
    texture = surface ? SDL_CreateTextureFromSurface(renderer, surface) : NULL;
    SDL_DestroySurface(surface);
    if (texture)
    {
        SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND);
        SDL_SetTextureScaleMode(texture, SDL_SCALEMODE_LINEAR);
    }
    return texture;
}
static void picture(SDL_Texture *texture, ImVec2 origin, ImVec2 size)
{
    ImTextureRef ref;
    ImVec2 fitted, start;
    ImDrawList *draw;
    float width, height, aspect;
    memset(&ref, 0, sizeof(ref)); ref._TexID = (ImTextureID)(size_t)texture;
    fitted = size;
    aspect = .8f;
    if (texture && SDL_GetTextureSize(texture, &width, &height) && height > 0) aspect = width / height;
    if (size.x / aspect > size.y) fitted.x = size.y * aspect;
    else fitted.y = size.x / aspect;
    start = vec(origin.x + (size.x - fitted.x) * .5f, origin.y + (size.y - fitted.y) * .5f);
    draw = igGetWindowDrawList();
    ImDrawList_AddRectFilled(draw, origin, vec(origin.x + size.x, origin.y + size.y), igGetColorU32_Vec4(colour(0x13161a)), 0, 0);
    if (texture) ImDrawList_AddImage(draw, ref, start, vec(start.x + fitted.x, start.y + fitted.y), vec(0, 0), vec(1, 1), 0xffffffffU);
}
static void gallery(MarcusCostumeDialog *d)
{
    const ImGuiStyle *style;
    ImVec2 avail, size, origin;
    float aspect, width, height;
    int first, end, i, j, columns, clicked, visible;
    style = igGetStyle();
    first = 0;
    while (first < d->count)
    {
        end = first + 1;
        while (end < d->count && d->tiles[end].value.body_set == d->tiles[first].value.body_set) ++end;
        igPushID_Int(d->tiles[first].value.body_set);
        igSeparatorText(d->tiles[first].value.accessory ? "Accessories" : "Clothing / surface");
        igTextDisabled("Part %d", d->tiles[first].value.body_set + 1);
        avail = igGetContentRegionAvail();
        columns = (int)((avail.x + style->ItemSpacing.x) / (170 + style->ItemSpacing.x));
        if (columns < 1) columns = 1;
        size.x = (avail.x - (columns - 1) * style->ItemSpacing.x) / columns;
        aspect = .8f;
        for (i = first; i < end; ++i) if (d->tiles[i].texture && SDL_GetTextureSize(d->tiles[i].texture, &width, &height) && height > 0)
        { aspect = width / height; break; }
        size.y = clamp(size.x / aspect, 80, 360);
        for (i = first; i < end; ++i)
        {
            igPushID_Int(i);
            if (d->tiles[i].value.selected) igPushStyleColor_Vec4(ImGuiCol_Button, colour(0x409aff));
            clicked = igButton("##costume", size);
            visible = igIsItemVisible(); d->tiles[i].wanted = visible;
            origin = igGetItemRectMin();
            picture(d->tiles[i].texture, vec(origin.x + 4, origin.y + 4), vec(size.x - 8, size.y - 8));
            if (!d->tiles[i].texture && visible) igSetItemTooltip(d->tiles[i].attempted ? "Preview unavailable" : "Loading preview...");
            if (d->tiles[i].value.selected) igPopStyleColor(1);
            if (clicked)
            {
                for (j = first; j < end; ++j) d->tiles[j].value.selected = j == i;
                d->invalid = 1; d->error[0] = 0;
            }
            igPopID();
            if ((i - first + 1) % columns && i + 1 < end) igSameLine(0, -1);
        }
        igPopID(); first = end;
    }
}
static void preview(MarcusCostumeDialog *d)
{
    ImVec2 size, origin;
    ImGuiIO *io;
    float scale, texture_width, texture_height, aspect;
    int width, height;
    size = igGetContentRegionAvail(); size.y -= 28;
    if (size.x < 1) size.x = 1;
    if (size.y < 1) size.y = 1;
    scale = 960 / (size.x > size.y ? size.x : size.y);
    if (scale > 1) scale = 1;
    width = (int)clamp((size.x - 8) * scale, 64, 1024);
    height = (int)clamp((size.y - 8) * scale, 64, 1024);
    if (width != d->preview_width || height != d->preview_height)
    { d->preview_width = width; d->preview_height = height; d->preview_dirty = 1; }
    if (d->preview && SDL_GetTextureSize(d->preview, &texture_width, &texture_height) && texture_height > 0)
    {
        aspect = texture_width / texture_height;
        if ((size.x - 8) / aspect > size.y - 8) size.x = (size.y - 8) * aspect + 8;
        else size.y = (size.x - 8) / aspect + 8;
    }
    igButton("##actorPreview", size); origin = igGetItemRectMin();
    picture(d->preview, vec(origin.x + 4, origin.y + 4), vec(size.x - 8, size.y - 8));
    io = igGetIO_Nil();
    if (igIsItemActive() && io->MouseDown[0] && (io->MouseDelta.x || io->MouseDelta.y))
    {
        d->yaw -= io->MouseDelta.x * .6f;
        d->pitch = clamp(d->pitch + io->MouseDelta.y * .6f, -89, 89); d->preview_dirty = 1;
    }
    if (igIsItemHovered(0))
    {
        igSetItemKeyOwner_Nil(ImGuiKey_MouseWheelY);
        if (io->MouseWheel) { d->zoom = clamp(d->zoom * (float)pow(1.15, io->MouseWheel), .25f, 1); d->preview_dirty = 1; }
    }
    igTextDisabled("Drag to rotate / Wheel to zoom");
}
void costume_dialog_draw(MarcusCostumeDialog *d, MarcusUI *ui, int width, int height)
{
    ImGuiStorage *storage;
    ImGuiID key;
    ImVec2 size;
    MarcusCostume selected[512];
    int i, count;
    if (!d) return;
    d->button_visible = d->button_drawn; d->button_drawn = 0;
    if (!costume_dialog_visible(d)) return;
    if (d->document != marcus_document(d->player))
    {
        if (igBeginPopupModal("Costume###MarcusCostume", NULL, ImGuiWindowFlags_NoSavedSettings))
        { igCloseCurrentPopup(); igEndPopup(); }
        d->open = 0; return;
    }
    if (d->invalid) clear_images(d);
    if (d->preview_dirty)
    {
        SDL_DestroyTexture(d->preview); d->preview = render(d, ui->renderer, -1); d->preview_dirty = 0;
    }
    for (i = 0; i < d->count; ++i) d->tiles[i].wanted = 0;
    size = vec((float)width - 32, (float)height - 32);
    if (size.x > 1400) size.x = 1400;
    if (d->opening) igOpenPopup_Str("Costume###MarcusCostume", 0);
    if (d->opening || width != d->layout_width || height != d->layout_height)
    {
        igSetNextWindowPos(vec(width * .5f, height * .5f), ImGuiCond_Always, vec(.5f, .5f));
        d->layout_width = width; d->layout_height = height;
    }
    igSetNextWindowSize(size, ImGuiCond_Always);
    storage = igGetStateStorage(); key = igGetID_Str("MarcusCostumeVisible");
    ImGuiStorage_SetBool(storage, key, 1);
    if (igBeginPopupModal("Costume###MarcusCostume", ImGuiStorage_GetBoolRef(storage, key, 1),
        ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoResize))
    {
        d->opening = 0;
        igText("%s", d->name);
        igTextDisabled("Choose clothing, surfaces and accessories. Changes start at this frame.");
        if (igBeginChild_Str("Costume body", vec(0, -90), 0, 0))
        {
            if (igBeginTable("Costume choices and preview", 2, ImGuiTableFlags_SizingStretchProp, vec(0, 0), 0))
            {
                igTableSetupColumn("Choices", ImGuiTableColumnFlags_WidthStretch, 3, 0);
                igTableSetupColumn("Preview", ImGuiTableColumnFlags_WidthStretch, 2, 0);
                igTableNextColumn();
                if (igBeginChild_Str("Costume choices", vec(0, 0), 0, 0))
                {
                    if (d->count) gallery(d);
                    else igTextWrapped("This object has no costume variants.");
                }
                igEndChild(); igTableNextColumn(); preview(d); igEndTable();
            }
        }
        igEndChild();
        igSeparator();
        if (d->error[0]) igTextWrapped("%s", d->error);
        else igTextDisabled("Cancel leaves the movie unchanged.");
        if (igButton("Cancel", vec(100, 0)) || igShortcut_Nil(ImGuiKey_Escape, 0)) d->open = 0;
        igSameLine(0, -1); igBeginDisabled(!d->count);
        if (igButton("Apply costume", vec(150, 0)))
        {
            count = choices(d, selected, -1);
            if (marcus_set_costumes(d->player, d->object, selected, count))
            {
                if (marcus_document(d->player) != d->document) d->chosen = d->object;
                d->open = 0;
            }
            else SDL_strlcpy(d->error, marcus_error(d->player), sizeof(d->error));
        }
        igEndDisabled();
        if (!d->open) igCloseCurrentPopup();
        igEndPopup();
    }
    if (!ImGuiStorage_GetBool(storage, key, 1)) d->open = 0;
    /* One uncached visible thumbnail per frame; unchanged images stay cached. */
    if (d->open && !d->invalid) for (i = 0; i < d->count; ++i)
        if (d->tiles[i].wanted && !d->tiles[i].attempted)
        { d->tiles[i].attempted = 1; d->tiles[i].texture = render(d, ui->renderer, i); break; }
}
int costume_dialog_take(MarcusCostumeDialog *d)
{
    int object;
    if (!d) return -1;
    object = d->chosen; d->chosen = -1; return object;
}
