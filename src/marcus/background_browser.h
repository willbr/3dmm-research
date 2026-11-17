#ifndef MARCUS_BACKGROUND_BROWSER_H
#define MARCUS_BACKGROUND_BROWSER_H
#include "ui.h"
#include "player.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct MarcusBackgroundBrowser MarcusBackgroundBrowser;
#define MARCUS_BACKGROUND_STORYBOARD 0xffffffffUL
MarcusBackgroundBrowser *background_browser_create(void);
void background_browser_destroy(MarcusBackgroundBrowser *browser);
void background_browser_prepare(MarcusBackgroundBrowser *browser, MarcusPlayer *player, const char *resources, const char *content);
int background_browser_show(MarcusBackgroundBrowser *browser, MarcusPlayer *player, const char *resources, const char *content);
int background_browser_show_objects(MarcusBackgroundBrowser *browser, MarcusPlayer *player, const char *resources, const char *content);
int background_browser_take_object(MarcusBackgroundBrowser *browser, unsigned long *chunk, int *actor);
int background_browser_visible(const MarcusBackgroundBrowser *browser);
int background_browser_event(MarcusBackgroundBrowser *browser, const SDL_Event *event, int width, int height);
void background_browser_draw(MarcusBackgroundBrowser *browser, MarcusUI *ui, int width, int height);
int background_browser_take(MarcusBackgroundBrowser *browser, unsigned long *chunk);
#ifdef __cplusplus
}
#endif
#endif
