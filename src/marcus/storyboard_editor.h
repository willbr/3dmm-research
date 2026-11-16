#ifndef MARCUS_STORYBOARD_EDITOR_H
#define MARCUS_STORYBOARD_EDITOR_H
#include "ui.h"
#include "player.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct MarcusStoryboardEditor MarcusStoryboardEditor;
MarcusStoryboardEditor *storyboard_editor_create(void);
void storyboard_editor_destroy(MarcusStoryboardEditor *editor);
/* angle -1 opens the active shot; -2 adds a blank shot with the same camera. */
int storyboard_editor_show(MarcusStoryboardEditor *editor, MarcusPlayer *player, int new_scene, int angle,
    const MarcusImage *work, const char *resources, const char *content);
int storyboard_editor_visible(const MarcusStoryboardEditor *editor);
int storyboard_editor_event(MarcusStoryboardEditor *editor, const SDL_Event *event, int width, int height);
void storyboard_editor_draw(MarcusStoryboardEditor *editor, MarcusUI *ui, int width, int height);
int storyboard_editor_take(MarcusStoryboardEditor *editor);
#ifdef __cplusplus
}
#endif
#endif
