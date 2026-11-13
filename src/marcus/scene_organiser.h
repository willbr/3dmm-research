#ifndef MARCUS_SCENE_ORGANISER_H
#define MARCUS_SCENE_ORGANISER_H
#include "player.h"
#include "ui.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct MarcusSceneOrganiser MarcusSceneOrganiser;
MarcusSceneOrganiser *scene_organiser_create(void);
void scene_organiser_destroy(MarcusSceneOrganiser *organiser);
void scene_organiser_close(MarcusSceneOrganiser *organiser);
void scene_organiser_show(MarcusSceneOrganiser *organiser, MarcusPlayer *player);
int scene_organiser_show_frames(MarcusSceneOrganiser *organiser, MarcusPlayer *player);
int scene_organiser_event(MarcusSceneOrganiser *organiser, const SDL_Event *event, int width, int height);
void scene_organiser_draw(MarcusSceneOrganiser *organiser, MarcusUI *ui, int width, int height);
/* Returns a confirmed scene once, or -1. */
int scene_organiser_take(MarcusSceneOrganiser *organiser);
int scene_organiser_take_frame(MarcusSceneOrganiser *organiser, int *scene, long *frame);
#ifdef __cplusplus
}
#endif
#endif
