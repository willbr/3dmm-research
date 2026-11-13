#ifndef MARCUS_UI_H
#define MARCUS_UI_H
#include <SDL3/SDL.h>
#include <ft2build.h>
#include FT_FREETYPE_H
#ifdef __cplusplus
extern "C" {
#endif

typedef struct MarcusGlyph
{
    SDL_FRect source;
    int bearing_x, bearing_y, advance;
} MarcusGlyph;
typedef struct MarcusUI
{
    SDL_Renderer *renderer;
    SDL_Texture *font;
    SDL_Texture *heading_font;
    SDL_Texture *transport_labels[2];
    SDL_Texture *transport_numbers;
    SDL_Texture *settings_icons[2]; /* Native 28px and 40px controls. */
    MarcusGlyph transport_digits[13]; /* '-' through '9'. */
    int transport_space;
    MarcusGlyph glyphs[224];
    MarcusGlyph heading_glyphs[224];
    float mouse_x, mouse_y;
    const char *hint;
    float opacity;
} MarcusUI;

int ui_init(MarcusUI *ui, SDL_Renderer *renderer, const char *font_path);
void ui_destroy(MarcusUI *ui);
void ui_opacity(MarcusUI *ui, float opacity);
void ui_fill(MarcusUI *ui, float x, float y, float w, float h, unsigned long colour);
void ui_line(MarcusUI *ui, float x, float y, float x2, float y2, unsigned long colour);
void ui_text(MarcusUI *ui, float x, float y, const char *text, unsigned long colour);
/* Arial Black, matching the FRAME/SCENE labels; uses the same bold fallback. */
void ui_heading(MarcusUI *ui, float x, float y, const char *text, unsigned long colour);
void ui_heading_label(MarcusUI *ui, float x, float y, float width, const char *text, unsigned long colour);
void ui_transport_label(MarcusUI *ui, float x, float y, int scene, unsigned long colour);
void ui_label(MarcusUI *ui, float x, float y, float width, const char *text, unsigned long colour);
int ui_inside(float px, float py, float x, float y, float w, float h);
void ui_button(MarcusUI *ui, float x, float y, float w, const char *text, int active, int enabled);
void ui_button_sized(MarcusUI *ui, float x, float y, float w, float h, const char *text, int active, int enabled);
#define UI_NAVIGATION_HEIGHT 20
#define UI_PLAY_WIDTH 40
#define UI_PLAY_HEIGHT 44
void ui_play_button(MarcusUI *ui, float x, float y, int playing, int waiting, int enabled);
#define UI_SETTINGS_SIZE 40
void ui_settings_button(MarcusUI *ui, float x, float y, float size, int active);
/* action: 0 first, 1 previous, 2 next, 3 last. */
void ui_navigation_button(MarcusUI *ui, float x, float y, int action, int enabled, const char *hint);
void ui_frame_button(MarcusUI *ui, float x, float y, int action, int enabled, const char *hint);
void ui_slider_thumb(MarcusUI *ui, float x, float y, int scene, int enabled, int active, int sound);
void ui_transport_readout(MarcusUI *ui, float x, float y, int scene, const char *text, int enabled);
#define UI_DIALOG_BUTTON_WIDTH 46
#define UI_DIALOG_BUTTON_HEIGHT 43
void ui_dialog_button(MarcusUI *ui, float x, float y, int confirm, int enabled, const char *hint);
#ifdef __cplusplus
}
#endif
#endif
