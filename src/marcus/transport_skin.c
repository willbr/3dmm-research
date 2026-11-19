#include "transport_skin.h"
#include "dialog_widgets.h"
#include "transport_sprites.h"
#include "ui_vectors.h"
#include <float.h>

static ImFont *label_font, *number_font;
static SDL_Texture *frame_thumb, *frame_sound_thumb, *scene_thumb, *scene_track;

static SDL_Texture *load_sprite(SDL_Renderer *renderer, const unsigned char *bmp, size_t length)
{
    SDL_IOStream *input;
    SDL_Surface *surface;
    SDL_Texture *texture;
    input = SDL_IOFromConstMem(bmp, length);
    if (!input) return NULL;
    surface = SDL_LoadBMP_IO(input, 1);
    if (!surface) return NULL;
    /* Studio's masked bitmap resources use palette index zero as transparent. */
    texture = SDL_SetSurfaceColorKey(surface, 1, 0) ? SDL_CreateTextureFromSurface(renderer, surface) : NULL;
    SDL_DestroySurface(surface);
    if (texture) SDL_SetTextureScaleMode(texture, SDL_SCALEMODE_NEAREST);
    return texture;
}
void transport_load_slider_artwork(SDL_Renderer *renderer)
{
    frame_thumb = load_sprite(renderer, fthm_bmp, sizeof(fthm_bmp));
    frame_sound_thumb = load_sprite(renderer, fthms_bmp, sizeof(fthms_bmp));
    scene_thumb = load_sprite(renderer, stmb_bmp, sizeof(stmb_bmp));
    scene_track = load_sprite(renderer, sscl_bmp, sizeof(sscl_bmp));
}
void transport_destroy_slider_artwork(void)
{
    SDL_DestroyTexture(frame_thumb);
    SDL_DestroyTexture(frame_sound_thumb);
    SDL_DestroyTexture(scene_thumb);
    SDL_DestroyTexture(scene_track);
    frame_thumb = frame_sound_thumb = scene_thumb = scene_track = NULL;
}

static ImFont *installed_font(const char *windows_name, const char *const *fallbacks, float size, ImFontConfig *config)
{
    SDL_PathInfo info;
    ImFont *font;
    char **environment;
    char path[1024];
    int i;
    /* Windows variable names ignore case, but SDL's environment cache does not. */
    environment = SDL_GetEnvironmentVariables(SDL_GetEnvironment());
    font = NULL;
    for (i = 0; environment && environment[i]; ++i)
        if (!SDL_strncasecmp(environment[i], "windir=", 7)) {
            SDL_snprintf(path, sizeof(path), "%s/Fonts/%s", environment[i] + 7, windows_name);
            if (SDL_GetPathInfo(path, &info) && info.type == SDL_PATHTYPE_FILE)
                font = ImFontAtlas_AddFontFromFileTTF(igGetIO_Nil()->Fonts, path, size, config, NULL);
            break;
        }
    SDL_free(environment);
    if (font) return font;
    for (i = 0; fallbacks[i]; ++i)
        if (SDL_GetPathInfo(fallbacks[i], &info) && info.type == SDL_PATHTYPE_FILE)
            return ImFontAtlas_AddFontFromFileTTF(igGetIO_Nil()->Fonts, fallbacks[i], size, config, NULL);
    return NULL;
}
void transport_skin_init(void)
{
    ImFontAtlas *atlas;
    ImFontConfig *config;
    int i;
    static const char *const headings[] = {
        "/System/Library/Fonts/Supplemental/Arial Black.ttf",
        "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf",
        "/usr/share/fonts/truetype/liberation2/LiberationSans-Bold.ttf", NULL
    };
    static const char *const numbers[] = {
        "/System/Library/Fonts/Supplemental/Comic Sans MS.ttf", NULL
    };
    /* Keep the ordinary UI font first when the atlas has not baked it yet. */
    atlas = igGetIO_Nil()->Fonts;
    if (!atlas->Fonts.Size) ImFontAtlas_AddFontDefault(atlas, NULL);
    /* Preserve ordinary UI fonts while the transport uses ImGui's hinted loader. */
    for (i = 0; i < atlas->Sources.Size; ++i)
        atlas->Sources.Data[i].FontLoader = igImFontAtlasGetFontLoaderForStbTruetype();
    ImFontAtlas_SetFontLoader(atlas, ImGuiFreeType_GetFontLoader());
    config = ImFontConfig_ImFontConfig();
    /* Nominal pixel sizes match the former FT_Set_Pixel_Sizes rendering. */
    config->FontLoaderFlags = ImGuiFreeTypeLoaderFlags_Bitmap;
    label_font = installed_font("ariblk.ttf", headings, 11, config);
    number_font = installed_font("comic.ttf", numbers, 12, config);
    ImFontConfig_destroy(config);
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
static void draw_thumb(ImDrawList *draw, SDL_Texture *texture, float center, float y, ImVec2 size)
{
    ImTextureRef ref;
    float left;
    left = SDL_roundf(center - size.x * 0.5f);
    ref._TexData = NULL;
    ref._TexID = (ImTextureID)(size_t)texture;
    ImDrawList_AddImage(draw, ref, dialog_vec(left, y), dialog_vec(left + size.x, y + size.y),
                        dialog_vec(0, 0), dialog_vec(1, 1), colour(0xffffff, 1, 1));
}
int transport_frame_thumb(float x, float y, int sound)
{
    SDL_Texture *texture;
    ImVec2 size;
    texture = sound ? frame_sound_thumb : frame_thumb;
    if (!texture || !SDL_GetTextureSize(texture, &size.x, &size.y)) return 0;
    draw_thumb(igGetBackgroundDrawList_Nil(), texture, x, y, size);
    return 1;
}
static void draw_scene_track(float x, float y, float width, ImVec2 size)
{
    ImDrawList *draw;
    ImTextureRef ref;
    ImU32 ink;
    float edge_u, right, bottom, next;
    draw = igGetWindowDrawList();
    ref._TexData = NULL;
    ref._TexID = (ImTextureID)(size_t)scene_track;
    ink = colour(0xffffff, 1, 1);
    edge_u = 1 / size.x;
    right = x + width;
    bottom = y + size.y;
    /* Tile the center at native scale and keep the original edge pixels. */
    ImDrawList_AddImage(draw, ref, dialog_vec(x, y), dialog_vec(x + 1, bottom),
                        dialog_vec(0, 0), dialog_vec(edge_u, 1), ink);
    for (x += 1; x < right - 1; x = next)
    {
        next = SDL_min(x + size.x - 2, right - 1);
        ImDrawList_AddImage(draw, ref, dialog_vec(x, y), dialog_vec(next, bottom),
                            dialog_vec(edge_u, 0), dialog_vec((1 + next - x) / size.x, 1), ink);
    }
    ImDrawList_AddImage(draw, ref, dialog_vec(right - 1, y), dialog_vec(right, bottom),
                        dialog_vec(1 - edge_u, 0), dialog_vec(1, 1), ink);
}
int transport_movie_slider(const SDL_FRect *rect, double *seconds, double duration)
{
    ImVec2 size, track_size, padding, left, right;
    ImVec4 highlight;
    const double low = 0;
    double fraction;
    float inset, center, y, frame_height;
    int artwork_ready, track_ready, changed;
    artwork_ready = scene_thumb && SDL_GetTextureSize(scene_thumb, &size.x, &size.y) && rect->w >= size.x + 4;
    track_ready = scene_track && SDL_GetTextureSize(scene_track, &track_size.x, &track_size.y) &&
                  track_size.x > 2 && rect->w > 2;
    if (track_ready)
    {
        padding = igGetStyle()->FramePadding;
        padding.y = SDL_max(0, (track_size.y - igGetFontSize()) * 0.5f);
        igPushStyleVar_Vec2(ImGuiStyleVar_FramePadding, padding);
        igPushStyleColor_U32(ImGuiCol_FrameBg, 0);
        highlight.x = highlight.y = highlight.z = 1;
        highlight.w = 0.08f;
        igPushStyleColor_Vec4(ImGuiCol_FrameBgHovered, highlight);
        highlight.w = 0.13f;
        igPushStyleColor_Vec4(ImGuiCol_FrameBgActive, highlight);
    }
    if (artwork_ready)
    {
        igPushStyleVar_Float(ImGuiStyleVar_GrabMinSize, size.x);
        igPushStyleColor_U32(ImGuiCol_SliderGrab, 0);
        igPushStyleColor_U32(ImGuiCol_SliderGrabActive, 0);
    }
    frame_height = igGetFrameHeight();
    y = SDL_roundf(rect->y + (rect->h - frame_height) * 0.5f);
    if (track_ready)
        draw_scene_track(rect->x, SDL_roundf(y + (frame_height - track_size.y) * 0.5f), rect->w, track_size);
    igSetCursorScreenPos(dialog_vec(rect->x, y));
    igSetNextItemWidth(rect->w);
    changed = igSliderScalar("##Movie position", ImGuiDataType_Double, seconds, &low, &duration, "",
                             ImGuiSliderFlags_AlwaysClamp);
    if (artwork_ready)
    {
        igPopStyleColor(2);
        igPopStyleVar(1);
        left = igGetItemRectMin();
        right = igGetItemRectMax();
        /* Match ImGui's linear grab travel, including its two-pixel end inset. */
        inset = 2 + size.x * 0.5f;
        fraction = duration > 0 ? SDL_clamp(*seconds / duration, 0.0, 1.0) : 0;
        center = left.x + inset + (float)fraction * (right.x - left.x - 2 * inset);
        draw_thumb(igGetWindowDrawList(), scene_thumb, center, SDL_roundf((left.y + right.y - size.y) * 0.5f), size);
    }
    if (track_ready)
    {
        igPopStyleColor(3);
        igPopStyleVar(1);
    }
    return changed;
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
static void readout_text(ImDrawList *draw, ImFont *font, float size, float x, float y, float width, const char *text, ImU32 ink)
{
    ImFontBaked *baked;
    ImVec2 extent;
    float left;
    if (!font) font = igGetFont();
    extent = ImFont_CalcTextSizeA(font, size, FLT_MAX, 0, text, NULL, NULL);
    if (width > 0 && extent.x > 100) {
        size *= 100 / extent.x;
        extent = ImFont_CalcTextSizeA(font, size, FLT_MAX, 0, text, NULL, NULL);
    }
    baked = ImFont_GetFontBaked(font, size, 1);
    left = width > 0 ? SDL_floorf(x + (width - extent.x) / 2) : x;
    ImDrawList_PushClipRect(draw, dialog_vec(x, y), dialog_vec(x + (width > 0 ? width : 48), y + 20), 1);
    ImDrawList_AddText_FontPtr(draw, font, size,
        dialog_vec(left, y + 14 - baked->Ascent),
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
    readout_text(draw, label_font, 11, x + 6, y, 0, scene ? "SCENE" : "FRAME", ink);
    readout_text(draw, number_font, 12, x + 54, y, 108, value, ink);
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
