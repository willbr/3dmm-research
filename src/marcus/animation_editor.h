#ifndef MARCUS_ANIMATION_EDITOR_H
#define MARCUS_ANIMATION_EDITOR_H
#include "player.h"
#include <SDL3/SDL.h>
typedef struct MarcusAnimationEditor MarcusAnimationEditor;
MarcusAnimationEditor *animation_editor_create(void);
void animation_editor_destroy(MarcusAnimationEditor *);
int animation_editor_recording(const MarcusAnimationEditor *);
int animation_editor_event(MarcusAnimationEditor *, const SDL_Event *);
/* -1 reports an edit error, 0 means no commit, and 1 reports a commit. */
int animation_editor_properties(MarcusAnimationEditor *, MarcusPlayer *, int object,
                                const MarcusObject *, int enabled);
int animation_editor_draw_path(MarcusAnimationEditor *, MarcusPlayer *, const MarcusImage *, const SDL_FRect *);
#endif
