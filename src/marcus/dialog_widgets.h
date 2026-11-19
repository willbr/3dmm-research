#ifndef MARCUS_DIALOG_WIDGETS_H
#define MARCUS_DIALOG_WIDGETS_H
#include <SDL3/SDL.h>
#define CIMGUI_DEFINE_ENUMS_AND_STRUCTS
#include <cimgui.h>
ImVec2 dialog_vec(float x, float y);
int dialog_input_event(const SDL_Event *event);
int dialog_begin(const char *name, int *open, int width, int height, float preferred_width, float preferred_height);
void dialog_end(int closed);
void dialog_close(const char *name);
int dialog_checkbox(const char *label, int *value);
int dialog_okhsl(float hsl[3]);
void dialog_image(SDL_Texture *texture, ImVec2 size);
void dialog_object_picture(SDL_Texture *texture, ImVec2 origin, ImVec2 size);
int dialog_thumbnail(const char *id, SDL_Texture *texture, ImVec2 size, int selected, const char *placeholder);
void dialog_release_textures(void);
#endif
