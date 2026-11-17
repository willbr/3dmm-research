#include "transport_skin.h"
#include "dialog_widgets.h"
#include "ui_vectors.h"
#include <float.h>

static ImFont *label_font, *number_font;
static ImFont *installed_font(const char *windows_name, const char *const *fallbacks, float size)
{
    SDL_PathInfo info;
    const char *windows_directory;
    char path[1024];
    int i;
    windows_directory = SDL_getenv("WINDIR");
    if (windows_directory) {
        SDL_snprintf(path, sizeof(path), "%s/Fonts/%s", windows_directory, windows_name);
        if (SDL_GetPathInfo(path, &info) && info.type == SDL_PATHTYPE_FILE)
            return ImFontAtlas_AddFontFromFileTTF(igGetIO_Nil()->Fonts, path, size, NULL, NULL);
    }
    for (i = 0; fallbacks[i]; ++i)
        if (SDL_GetPathInfo(fallbacks[i], &info) && info.type == SDL_PATHTYPE_FILE)
            return ImFontAtlas_AddFontFromFileTTF(igGetIO_Nil()->Fonts, fallbacks[i], size, NULL, NULL);
    return NULL;
}
void transport_skin_init(void)
{
    static const char *const headings[] = {
        "/System/Library/Fonts/Supplemental/Arial Black.ttf",
        "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf",
        "/usr/share/fonts/truetype/liberation2/LiberationSans-Bold.ttf", NULL
    };
    static const char *const numbers[] = {
        "/System/Library/Fonts/Supplemental/Comic Sans MS.ttf", NULL
    };
    /* Keep the ordinary UI font first when the atlas has not baked it yet. */
    if (!igGetIO_Nil()->Fonts->Fonts.Size) ImFontAtlas_AddFontDefault(igGetIO_Nil()->Fonts, NULL);
    label_font = installed_font("ariblk.ttf", headings, 11);
    number_font = installed_font("comic.ttf", numbers, 12);
}
static ImU32 colour(unsigned long rgb, float brightness, int enabled)
{
    float r, g, b, grey;
    ImVec4 c;
    r = (float)((rgb >> 16) & 255); g = (float)((rgb >> 8) & 255); b = (float)(rgb & 255);
    if (!enabled) { grey = (r + g + b) / 3; r = g = b = grey; }
    c.x = r * brightness / 255; c.y = g * brightness / 255; c.z = b * brightness / 255;
    c.w = igGetStyle()->Alpha;
    return igColorConvertFloat4ToU32(c);
}
static void artwork(ImDrawList *draw, ImVec2 at, const MarcusVectorVertex *vertices, int count, float brightness, int enabled)
{
    ImDrawListFlags flags;
    int i;
    flags = draw->Flags; draw->Flags &= ~ImDrawListFlags_AntiAliasedFill;
    for (i = 0; i + 2 < count; i += 3)
        ImDrawList_AddTriangleFilled(draw,
            dialog_vec(at.x + vertices[i].x, at.y + vertices[i].y),
            dialog_vec(at.x + vertices[i + 1].x, at.y + vertices[i + 1].y),
            dialog_vec(at.x + vertices[i + 2].x, at.y + vertices[i + 2].y),
            colour(vertices[i].colour, brightness, enabled));
    draw->Flags = flags;
}
static int button(const char *id, float x, float y, float w, float h, int enabled, const char *hint,
    float *brightness, int *pressed)
{
    int clicked, hovered;
    igPushID_Str(id); igPushStyleVar_Float(ImGuiStyleVar_FrameRounding, 0);
    igSetCursorScreenPos(dialog_vec(x, y)); igBeginDisabled(!enabled);
    clicked = igButton("##Retail artwork", dialog_vec(w, h));
    hovered = igIsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled);
    *pressed = enabled && igIsItemActive();
    igEndDisabled(); igPopStyleVar(1); igPopID();
    *brightness = !enabled ? .55f : *pressed ? .82f : hovered ? 1.10f : 1;
    if (hovered && hint) igSetTooltip("%s", hint);
    return clicked;
}
static void centred_text(ImDrawList *draw, ImFont *font, float size, ImVec2 at, ImVec2 area, const char *text, ImU32 ink)
{
    ImVec2 extent;
    if (!font) font = igGetFont();
    extent = ImFont_CalcTextSizeA(font, size, FLT_MAX, 0, text, NULL, NULL);
    if (extent.x > area.x) {
        size *= area.x / extent.x;
        extent = ImFont_CalcTextSizeA(font, size, FLT_MAX, 0, text, NULL, NULL);
    }
    ImDrawList_PushClipRect(draw, at, dialog_vec(at.x + area.x, at.y + area.y), 1);
    ImDrawList_AddText_FontPtr(draw, font, size,
        dialog_vec(at.x + SDL_floorf((area.x - extent.x) / 2), at.y + SDL_floorf((area.y - extent.y) / 2)),
        ink, text, NULL, 0, NULL);
    ImDrawList_PopClipRect(draw);
}
int transport_navigation(const char *id, float x, float y, int action, int enabled, const char *hint)
{
    static const MarcusVectorVertex *const icons[] = {
        marcus_vector_first_frame, marcus_vector_previous_frame, marcus_vector_next_frame, marcus_vector_last_frame
    };
    static const int counts[] = {MARCUS_VECTOR_FIRST_FRAME_COUNT, MARCUS_VECTOR_PREVIOUS_FRAME_COUNT,
        MARCUS_VECTOR_NEXT_FRAME_COUNT, MARCUS_VECTOR_LAST_FRAME_COUNT};
    float brightness;
    int clicked, pressed;
    if (action < 0 || action > 3) return 0;
    clicked = button(id, x, y, 28, 20, enabled, hint, &brightness, &pressed);
    artwork(igGetWindowDrawList(), dialog_vec(x, y), icons[action], counts[action], brightness, enabled);
    return clicked;
}
int transport_readout(const char *id, float x, float y, int scene, const char *value, int enabled, const char *hint)
{
    ImDrawList *draw;
    float brightness;
    int clicked, pressed;
    ImU32 ink;
    clicked = button(id, x, y, 164, 20, enabled, hint, &brightness, &pressed);
    draw = igGetWindowDrawList();
    artwork(draw, dialog_vec(x, y), marcus_vector_transport_readout, MARCUS_VECTOR_TRANSPORT_READOUT_COUNT,
        enabled ? brightness : 1, 1);
    ink = colour(enabled ? 0x111111 : 0x626262, 1, 1);
    centred_text(draw, label_font, 11, dialog_vec(x + 3, y + 3), dialog_vec(48, 14), scene ? "SCENE" : "FRAME", ink);
    centred_text(draw, number_font, 12, dialog_vec(x + 54, y + 2), dialog_vec(106, 16), value, ink);
    return clicked;
}
int transport_play(const char *id, float x, float y, int playing, int waiting, int enabled, const char *hint)
{
    ImDrawList *draw;
    float brightness;
    int clicked, pressed;
    clicked = button(id, x, y, 40, 44, enabled, hint, &brightness, &pressed);
    draw = igGetWindowDrawList();
    artwork(draw, dialog_vec(x, y), marcus_vector_frame, MARCUS_VECTOR_FRAME_COUNT, brightness, enabled);
    artwork(draw, dialog_vec(x, y + pressed), playing && waiting ? marcus_vector_continue : playing ? marcus_vector_pause : marcus_vector_play,
        playing && waiting ? MARCUS_VECTOR_CONTINUE_COUNT : playing ? MARCUS_VECTOR_PAUSE_COUNT : MARCUS_VECTOR_PLAY_COUNT,
        brightness, enabled);
    return clicked;
}
