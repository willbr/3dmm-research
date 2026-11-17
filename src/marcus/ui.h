#ifndef MARCUS_UI_H
#define MARCUS_UI_H
#include <SDL3/SDL.h>
#ifdef __cplusplus
extern "C" {
#endif
/* Platform handles and transient hints; all widgets are standard cimgui. */
typedef struct MarcusUI {
    SDL_Renderer *renderer;
    float mouse_x, mouse_y, opacity;
    const char *hint;
} MarcusUI;
int ui_init(MarcusUI *ui, SDL_Renderer *renderer, const char *font_path);
void ui_configure(const char *font_path);
void ui_destroy(MarcusUI *ui);
/* Specialized viewport/timeline artwork goes into ImGui's background draw list. */
void ui_fill(MarcusUI *ui, float x, float y, float w, float h, unsigned long colour);
void ui_line(MarcusUI *ui, float x, float y, float x2, float y2, unsigned long colour);
void ui_text(MarcusUI *ui, float x, float y, const char *text, unsigned long colour);
void ui_label(MarcusUI *ui, float x, float y, float width, const char *text, unsigned long colour);
float ui_text_width(const char *text);
void ui_image(MarcusUI *ui, SDL_Texture *texture, const SDL_FRect *rect);
void ui_clip(const SDL_Rect *rect);
void ui_unclip(void);
void ui_triangles(MarcusUI *ui, const SDL_Vertex *vertices, const int *indices, int count);
int ui_inside(float px, float py, float x, float y, float w, float h);
/* Fixed editor regions use normal ImGui windows and widgets. Always call End. */
int ui_panel(const char *name, float x, float y, float w, float h, int transparent, int scroll);
void ui_panel_end(void);
int ui_button_at(const char *id, float x, float y, float w, float h, int active, int enabled, const char *hint);
int ui_transform_controls(const char *title, const SDL_FRect *rect, int *mode, int count, int editing);
int ui_keyboard_busy(void);
void ui_tooltip(const char *text);
#define UI_NAVIGATION_HEIGHT 20
#define UI_PLAY_WIDTH 40
#define UI_PLAY_HEIGHT 44
#define UI_SETTINGS_SIZE 40
#ifdef __cplusplus
}
#endif
#endif
