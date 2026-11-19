#ifndef MARCUS_TRANSPORT_SKIN_H
#define MARCUS_TRANSPORT_SKIN_H
#include <SDL3/SDL.h>
/* Transport artwork and standard cimgui controls. */
void transport_skin_init(void);
void transport_load_slider_artwork(SDL_Renderer *renderer);
void transport_destroy_slider_artwork(void);
int transport_frame_thumb(float x, float y, int sound);
int transport_movie_slider(const SDL_FRect *rect, double *seconds, double duration);
int transport_navigation(const char *id, float x, float y, int action, int enabled, const char *hint);
int transport_readout(const char *id, float x, float y, int scene, const char *value, int enabled, const char *hint);
int transport_play(const char *id, float x, float y, int playing, int waiting, int enabled, const char *hint);
#endif
