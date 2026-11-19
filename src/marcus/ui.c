#include "ui.h"
#include "dialog_widgets.h"
#include "transport_skin.h"
#include <string.h>
static ImU32 ink(MarcusUI *ui, unsigned long rgb)
{
    return (ImU32)(((rgb >> 16) & 255) | (rgb & 0xff00) | ((rgb & 255) << 16) |
        ((unsigned long)(255 * ui->opacity) << 24));
}
int ui_init(MarcusUI *ui, SDL_Renderer *renderer, const char *font_path)
{
    (void)font_path; memset(ui, 0, sizeof(*ui)); ui->renderer = renderer; ui->opacity = 1;
    if (renderer) transport_load_slider_artwork(renderer);
    return renderer != NULL;
}
void ui_configure(const char *font_path)
{
    ImGuiStyle *style;
    SDL_PathInfo font_info;
    igStyleColorsDark(NULL);
    igGetIO_Nil()->IniFilename = NULL; igGetIO_Nil()->LogFilename = NULL;
    igGetIO_Nil()->ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    if (font_path && *font_path && SDL_GetPathInfo(font_path, &font_info) && font_info.type == SDL_PATHTYPE_FILE)
        ImFontAtlas_AddFontFromFileTTF(igGetIO_Nil()->Fonts, font_path, 14, NULL, NULL);
    transport_skin_init();
    style = igGetStyle(); style->WindowPadding = dialog_vec(10, 8); style->ItemSpacing = dialog_vec(8, 5);
    style->WindowBorderSize = 1; style->FrameRounding = 2;
    style->WindowMinSize = dialog_vec(1, 1);
    igGetIO_Nil()->ConfigFlags &= ~ImGuiConfigFlags_NoMouseCursorChange;
}
void ui_destroy(MarcusUI *ui) { (void)ui; transport_destroy_slider_artwork(); }
void ui_fill(MarcusUI *ui, float x, float y, float w, float h, unsigned long colour)
{
    if (w > 0 && h > 0) ImDrawList_AddRectFilled(igGetBackgroundDrawList_Nil(), dialog_vec(x, y), dialog_vec(x + w, y + h), ink(ui, colour), 0, 0);
}
void ui_line(MarcusUI *ui, float x, float y, float x2, float y2, unsigned long colour)
{ ImDrawList_AddLine(igGetBackgroundDrawList_Nil(), dialog_vec(x, y), dialog_vec(x2, y2), ink(ui, colour), 1); }
void ui_text(MarcusUI *ui, float x, float y, const char *text, unsigned long colour)
{ ImDrawList_AddText_Vec2(igGetBackgroundDrawList_Nil(), dialog_vec(x, y), ink(ui, colour), text, NULL); }
void ui_label(MarcusUI *ui, float x, float y, float width, const char *text, unsigned long colour)
{
    ImDrawList *draw;
    if (width <= 0) return;
    draw = igGetBackgroundDrawList_Nil();
    ImDrawList_PushClipRect(draw, dialog_vec(x, y), dialog_vec(x + width, y + igGetTextLineHeight()), 1);
    ui_text(ui, x, y, text, colour); ImDrawList_PopClipRect(draw);
}
float ui_text_width(const char *text) { return igCalcTextSize(text, NULL, 0, -1).x; }
void ui_image(MarcusUI *ui, SDL_Texture *texture, const SDL_FRect *rect)
{
    ImTextureRef ref;
    if (!texture) return;
    ref._TexData = NULL; ref._TexID = (ImTextureID)(size_t)texture;
    ImDrawList_AddImage(igGetBackgroundDrawList_Nil(), ref, dialog_vec(rect->x, rect->y),
        dialog_vec(rect->x + rect->w, rect->y + rect->h), dialog_vec(0, 0), dialog_vec(1, 1), ink(ui, 0xffffff));
}
void ui_clip(const SDL_Rect *rect)
{ ImDrawList_PushClipRect(igGetBackgroundDrawList_Nil(), dialog_vec((float)rect->x, (float)rect->y), dialog_vec((float)(rect->x + rect->w), (float)(rect->y + rect->h)), 1); }
void ui_unclip(void) { ImDrawList_PopClipRect(igGetBackgroundDrawList_Nil()); }
void ui_triangles(MarcusUI *ui, const SDL_Vertex *vertices, const int *indices, int count)
{
    ImDrawList *draw;
    ImVec2 uv;
    ImVec4 colour;
    ImDrawIdx base;
    int i, j;
    const SDL_Vertex *vertex;
    draw = igGetBackgroundDrawList_Nil(); uv = igGetFontTexUvWhitePixel();
    for (i = 0; i + 2 < count; i += 3) {
        ImDrawList_PrimReserve(draw, 3, 3); base = (ImDrawIdx)draw->_VtxCurrentIdx;
        for (j = 0; j < 3; ++j) {
            vertex = vertices + indices[i + j]; colour.x = vertex->color.r; colour.y = vertex->color.g;
            colour.z = vertex->color.b; colour.w = vertex->color.a * ui->opacity;
            ImDrawList_PrimWriteIdx(draw, (ImDrawIdx)(base + j));
            ImDrawList_PrimWriteVtx(draw, dialog_vec(vertex->position.x, vertex->position.y), uv, igColorConvertFloat4ToU32(colour));
        }
    }
}
int ui_inside(float px, float py, float x, float y, float w, float h) { return px >= x && py >= y && px < x + w && py < y + h; }
int ui_panel(const char *name, float x, float y, float w, float h, int transparent, int scroll)
{
    ImGuiWindowFlags flags;
    flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoBringToFrontOnFocus;
    if (transparent) flags |= ImGuiWindowFlags_NoBackground;
    if (!scroll) flags |= ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;
    igSetNextWindowPos(dialog_vec(x, y), ImGuiCond_Always, dialog_vec(0, 0));
    igSetNextWindowSize(dialog_vec(w > 1 ? w : 1, h > 1 ? h : 1), ImGuiCond_Always);
    if (transparent) igPushStyleVar_Vec2(ImGuiStyleVar_WindowPadding, dialog_vec(0, 0));
    scroll = igBegin(name, NULL, flags);
    if (transparent) igPopStyleVar(1);
    return scroll;
}
void ui_panel_end(void) { igEnd(); }
int ui_button_at(const char *id, float x, float y, float w, float h, int active, int enabled, const char *hint)
{
    int clicked;
    if (active) igPushStyleColor_Vec4(ImGuiCol_Button, igGetStyle()->Colors[ImGuiCol_ButtonActive]);
    igSetCursorScreenPos(dialog_vec(x, y)); igBeginDisabled(!enabled); clicked = igButton(id, dialog_vec(w, h)); igEndDisabled();
    if (hint && igIsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) igSetTooltip("%s", hint);
    if (active) igPopStyleColor(1);
    return clicked;
}
int ui_transform_controls(const char *title, const SDL_FRect *rect, int *mode, int count, int editing)
{
    static const char *names[] = {"Move", "Rotate", "Size"};
    int i, over;
    over = 0;
    igPushStyleVar_Vec2(ImGuiStyleVar_WindowPadding, dialog_vec(0, 0));
    if (ui_panel(title, rect->x, rect->y, rect->w, rect->h, 0, 0)) {
        igBeginDisabled(editing);
        for (i = 0; i < count; ++i) {
            if (i) igSameLine(0, 4);
            if (igSelectable_Bool(names[i], *mode == i, 0, dialog_vec(56, 24))) *mode = i;
        }
        over = igIsWindowHovered(0); igEndDisabled();
    }
    ui_panel_end(); igPopStyleVar(1); return over;
}
int ui_keyboard_busy(void)
{
    return igGetCurrentContext() && (igGetIO_Nil()->WantTextInput || igIsAnyItemActive() ||
        igGetIO_Nil()->NavVisible ||
        igIsPopupOpen_Str(NULL, ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel));
}
void ui_tooltip(const char *text) { igSetTooltip("%s", text); }
