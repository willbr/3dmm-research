#ifndef MARCUS_ANIMATION_DIALOG_H
#define MARCUS_ANIMATION_DIALOG_H
#include "player.h"
#include <SDL3/SDL.h>
typedef struct MarcusAnimationDialog MarcusAnimationDialog;
MarcusAnimationDialog *animation_dialog_create(void);
void animation_dialog_destroy(MarcusAnimationDialog *);
int animation_dialog_visible(const MarcusAnimationDialog *);
void animation_dialog_button(MarcusAnimationDialog *, MarcusPlayer *, int object, const MarcusObject *, int enabled);
int animation_dialog_draw(MarcusAnimationDialog *, SDL_Renderer *, int width, int height);
#endif
