#include "animation_dialog.h"
#include "dialog_widgets.h"
#include "dialog_host.h"

typedef struct ActionTile
{
    MarcusObjectAction action;
    SDL_Texture *texture;
    int attempted;
} ActionTile;

struct MarcusAnimationDialog
{
    MarcusPlayer *player;
    ActionTile *tiles;
    SDL_Texture *preview;
    int open, object, scene, count, action, start_cel, freeze;
    int displayed_cel, preview_width, preview_height, scroll_to_action;
    unsigned long document;
    long frame;
    Uint64 start_tick;
    float yaw, pitch, zoom;
    char name[192], error[512];
};

static const char title[] = "Animation###MarcusAnimation";

static void release_images(MarcusAnimationDialog *d)
{
    int i;
    dialog_retire_texture(d->preview);
    d->preview = NULL;
    for (i = 0; i < d->count; ++i) dialog_retire_texture(d->tiles[i].texture);
    SDL_free(d->tiles);
    d->tiles = NULL;
    d->count = 0;
}

MarcusAnimationDialog *animation_dialog_create(void)
{
    return (MarcusAnimationDialog *)SDL_calloc(1, sizeof(MarcusAnimationDialog));
}

void animation_dialog_destroy(MarcusAnimationDialog *d)
{
    if (!d) return;
    release_images(d);
    SDL_free(d);
}

int animation_dialog_visible(const MarcusAnimationDialog *d) { return d && d->open; }

static void restart_preview(MarcusAnimationDialog *d)
{
    d->start_tick = SDL_GetTicks();
    d->displayed_cel = -1;
}

static void show(MarcusAnimationDialog *d, MarcusPlayer *player, int object, const MarcusObject *value)
{
    MarcusInfo info;
    int count, i;
    if (!marcus_info(player, &info)) return;
    d->player = player;
    d->object = object;
    d->scene = info.scene;
    d->frame = info.frame;
    d->document = marcus_document(player);
    SDL_strlcpy(d->name, value->name, sizeof(d->name));
    d->error[0] = 0;
    d->yaw = 25; d->pitch = -10; d->zoom = 1;
    d->freeze = 0; d->start_cel = 1;
    count = marcus_object_action_count(player, object);
    if (count > 0)
    {
        d->tiles = (ActionTile *)SDL_calloc((size_t)count, sizeof(*d->tiles));
        if (!d->tiles) SDL_strlcpy(d->error, "Cannot allocate the action gallery.", sizeof(d->error));
        else for (i = 0; i < count && marcus_object_action(player, i, &d->tiles[i].action); ++i) ++d->count;
    }
    d->action = (int)SDL_clamp(value->action, 0, SDL_max(0, d->count - 1));
    d->scroll_to_action = 1;
    restart_preview(d);
    d->open = 1;
}

void animation_dialog_button(MarcusAnimationDialog *d, MarcusPlayer *player, int object,
                             const MarcusObject *value, int enabled)
{
    igBeginDisabled(!enabled || !value->visible);
    if (igButton("Animation...", dialog_vec(134, 28))) show(d, player, object, value);
    igEndDisabled();
}

static SDL_Texture *render(MarcusAnimationDialog *d, SDL_Renderer *renderer, int action, int cel, int clip,
                           float yaw, float pitch, float zoom, int width, int height)
{
    MarcusImage image;
    SDL_Texture *texture;
    if (!marcus_action_preview(d->player, d->object, action, cel, clip, yaw, pitch, zoom, width, height, &image))
        return NULL;
    texture = dialog_preview_texture(renderer, image.rgba, image.width, image.height, image.pitch);
    if (texture)
    {
        SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND);
        SDL_SetTextureScaleMode(texture, SDL_SCALEMODE_LINEAR);
    }
    return texture;
}

static int gallery(MarcusAnimationDialog *d)
{
    ImVec2 avail, size, origin;
    float spacing;
    int columns, i, next, selected, clicked;
    avail = igGetContentRegionAvail();
    spacing = igGetStyle()->ItemSpacing.x;
    columns = SDL_max(1, (int)((avail.x + spacing) / (112 + spacing)));
    size.x = (avail.x - (columns - 1) * spacing) / columns;
    size.y = SDL_clamp(size.x * .9f, 72.f, 140.f);
    next = -1;
    for (i = 0; i < d->count; ++i)
    {
        ActionTile *tile;
        tile = &d->tiles[i];
        igPushID_Int(i);
        igBeginGroup();
        igBeginDisabled(tile->action.cels <= 0);
        selected = i == d->action;
        if (selected) igPushStyleColor_Vec4(ImGuiCol_Button, igGetStyle()->Colors[ImGuiCol_ButtonActive]);
        clicked = igButton(tile->texture ? "##Action" : tile->attempted ? "Unavailable###Action" : "Loading...###Action", size);
        if (selected) igPopStyleColor(1);
        if (clicked)
        {
            d->action = i;
            d->start_cel = 1;
            d->error[0] = 0;
            restart_preview(d);
        }
        if (tile->texture)
        {
            origin = igGetItemRectMin();
            dialog_object_picture(tile->texture, dialog_vec(origin.x + 4, origin.y + 4),
                                  dialog_vec(size.x - 8, size.y - 8));
        }
        igEndDisabled();
        if (igIsItemVisible() && !tile->attempted && next < 0) next = i;
        igSetItemTooltip("%s", tile->action.name);
        origin = igGetCursorScreenPos();
        igDummy(dialog_vec(size.x, igGetTextLineHeight()));
        if (igIsItemVisible())
        {
            igPushClipRect(origin, igGetItemRectMax(), 1);
            ImDrawList_AddText_Vec2(igGetWindowDrawList(), origin, igGetColorU32_Col(ImGuiCol_Text, 1),
                                   tile->action.name, NULL);
            igPopClipRect();
        }
        igEndGroup();
        if (selected && d->scroll_to_action) igSetScrollHereY(.5f);
        igPopID();
        if ((i + 1) % columns && i + 1 < d->count) igSameLine(0, spacing);
    }
    d->scroll_to_action = 0;
    return next;
}

static void preview(MarcusAnimationDialog *d, SDL_Renderer *renderer)
{
    const MarcusObjectAction *action;
    ImVec2 size, origin;
    ImGuiIO *io;
    int cel, width, height, changed;
    Uint64 elapsed_cels;
    action = &d->tiles[d->action].action;
    igTextWrapped("%s", action->name);
    igSetNextItemWidth(108);
    changed = igInputInt("Start cel", &d->start_cel, 1, 0, 0);
    d->start_cel = SDL_clamp(d->start_cel, 1, SDL_max(1, action->cels));
    igSameLine(0, 12);
    changed |= dialog_checkbox("Freeze pose", &d->freeze);
    if (changed) restart_preview(d);

    size = igGetContentRegionAvail();
    size.x = SDL_max(1, size.x);
    size.y = SDL_max(1, size.y - igGetFrameHeightWithSpacing());
    width = SDL_clamp((int)size.x, 64, 480);
    height = SDL_clamp((int)(size.y * width / size.x), 64, 544);
    if (height == 544) width = SDL_clamp((int)(size.x * height / size.y), 64, 480);
    elapsed_cels = d->freeze ? 0 : (SDL_GetTicks() - d->start_tick) * 6 / 1000;
    cel = action->cels > 0 ? (int)((elapsed_cels + d->start_cel - 1) % action->cels) : 0;
    if (cel != d->displayed_cel || width != d->preview_width || height != d->preview_height)
    {
        dialog_retire_texture(d->preview);
        d->preview = render(d, renderer, d->action, cel, 1, d->yaw, d->pitch, d->zoom, width, height);
        d->displayed_cel = cel;
        d->preview_width = width;
        d->preview_height = height;
    }
    igButton(d->preview ? "##Action preview" : "Preview unavailable###Action preview", size);
    if (d->preview)
    {
        origin = igGetItemRectMin();
        dialog_object_picture(d->preview, dialog_vec(origin.x + 4, origin.y + 4),
                              dialog_vec(SDL_max(1, size.x - 8), SDL_max(1, size.y - 8)));
    }
    io = igGetIO_Nil();
    if (igIsItemActive() && io->MouseDown[0] && (io->MouseDelta.x || io->MouseDelta.y))
    {
        d->yaw -= io->MouseDelta.x * .6f;
        d->pitch = SDL_clamp(d->pitch + io->MouseDelta.y * .6f, -89.f, 89.f);
        d->displayed_cel = -1;
    }
    if (igIsItemHovered(0))
    {
        igSetItemKeyOwner_Nil(ImGuiKey_MouseWheelY);
        if (io->MouseWheel)
        {
            d->zoom = SDL_clamp(d->zoom * SDL_powf(1.15f, io->MouseWheel), .25f, 1.f);
            d->displayed_cel = -1;
        }
    }
    igTextDisabled("Drag to rotate / Wheel to zoom");
}

int animation_dialog_draw(MarcusAnimationDialog *d, SDL_Renderer *renderer, int width, int height)
{
    MarcusInfo info;
    int committed, next;
    if (!d->open) return 0;
    if (marcus_document(d->player) != d->document || !marcus_info(d->player, &info) ||
        info.scene != d->scene || info.frame != d->frame)
    {
        dialog_close(title);
        d->open = 0;
        release_images(d);
        return 0;
    }
    committed = 0;
    next = -1;
    if (dialog_begin(title, &d->open, width, height, 1120, 740))
    {
        igTextUnformatted(d->name, NULL);
        if (igBeginChild_Str("Animation content", dialog_vec(0, -66), 0, 0))
        {
            if (!d->count) igTextDisabled("This object has no available actions.");
            else if (igBeginTable("Actions and preview", 2, ImGuiTableFlags_SizingStretchProp, dialog_vec(0, 0), 0))
            {
                igTableSetupColumn("Actions", ImGuiTableColumnFlags_WidthStretch, 3, 0);
                igTableSetupColumn("Preview", ImGuiTableColumnFlags_WidthStretch, 2, 0);
                igTableNextColumn();
                if (igBeginChild_Str("Actions", dialog_vec(0, 0), 0, 0)) next = gallery(d);
                igEndChild();
                igTableNextColumn();
                preview(d, renderer);
                igEndTable();
            }
        }
        igEndChild();
        igSeparator();
        if (d->error[0]) igTextWrapped("%s", d->error);
        if (igButton("Cancel", dialog_vec(100, 0)) || igShortcut_Nil(ImGuiKey_Escape, 0)) d->open = 0;
        igSameLine(0, -1);
        igBeginDisabled(!d->count || d->tiles[d->action].action.cels <= 0);
        if (igButton("Apply action", dialog_vec(145, 0)) && d->open)
        {
            if (marcus_set_object_action(d->player, d->object, d->action, d->start_cel - 1, d->freeze))
            {
                committed = 1;
                d->open = 0;
            }
            else SDL_strlcpy(d->error, marcus_error(d->player), sizeof(d->error));
        }
        igEndDisabled();
        dialog_end(!d->open);
    }
    if (!d->open) release_images(d);
    else if (next >= 0)
    {
        d->tiles[next].attempted = 1;
        d->tiles[next].texture = render(d, renderer, next, 0, 0, 25, -10, 1, 152, 140);
    }
    return committed;
}
