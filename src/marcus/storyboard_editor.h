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
    const MarcusImage *work, float fly_speed, const char *resources, const char *content);
int storyboard_editor_visible(const MarcusStoryboardEditor *editor);
int storyboard_editor_flying(const MarcusStoryboardEditor *editor);
int storyboard_editor_capturing(const MarcusStoryboardEditor *editor);
void storyboard_editor_release_camera(MarcusStoryboardEditor *editor);
void storyboard_editor_update(MarcusStoryboardEditor *editor, double seconds);
/* Submit light controls during the shared ImGui/ImGuizmo frame, before scene rendering. */
void storyboard_editor_gizmo(MarcusStoryboardEditor *editor, int enabled);
/* Complete a gesture before leaving paint mode. Completed edits already belong to the movie. */
void storyboard_editor_close(MarcusStoryboardEditor *editor);
/* Follow ordinary scene/frame/undo/prop changes without intercepting their controls. */
void storyboard_editor_sync(MarcusStoryboardEditor *editor, MarcusPlayer *player, const char *resources, const char *content);
void storyboard_editor_layout(MarcusStoryboardEditor *editor, const SDL_FRect *view, const SDL_FRect *canvas, const SDL_FRect *tools, const SDL_FRect *bar);
/* Return true only for input owned by the storyboard viewport or tool panel. */
int storyboard_editor_event(MarcusStoryboardEditor *editor, const SDL_Event *event);
void storyboard_editor_draw(MarcusStoryboardEditor *editor, MarcusUI *ui);
int storyboard_editor_take(MarcusStoryboardEditor *editor);
#ifdef __cplusplus
}
#endif
#endif
