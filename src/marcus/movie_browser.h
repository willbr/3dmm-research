#ifndef MARCUS_MOVIE_BROWSER_H
#define MARCUS_MOVIE_BROWSER_H
#include "ui.h"
#include "player.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct MarcusBrowser MarcusBrowser;
MarcusBrowser *browser_create(SDL_Window *window, SDL_Renderer *renderer);
void browser_destroy(MarcusBrowser *browser);
void browser_show(MarcusBrowser *browser, const char *path, const char *resources, const char *content);
int browser_visible(const MarcusBrowser *browser);
/* Returns nonzero for events consumed by the modal browser. */
int browser_event(MarcusBrowser *browser, const SDL_Event *event);
void browser_draw(MarcusBrowser *browser, MarcusUI *ui, int width, int height);
/* Transfers ownership of the chosen input to the caller. */
MarcusMovieInput *browser_take(MarcusBrowser *browser, int *entry, char *path, size_t capacity);
void browser_remember(MarcusBrowser *browser, const char *path, int entry);
#ifdef __cplusplus
}
#endif
#endif
