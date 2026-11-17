#ifndef MARCUS_SOUND_DIALOG_H
#define MARCUS_SOUND_DIALOG_H
#include "ui.h"
#include "player.h"
typedef struct MarcusSoundDialog MarcusSoundDialog;
MarcusSoundDialog *sound_dialog_create(void);
void sound_dialog_destroy(MarcusSoundDialog *);
int sound_dialog_visible(const MarcusSoundDialog *);
int sound_dialog_event(MarcusSoundDialog *, const SDL_Event *);
void sound_dialog_toolbar(MarcusSoundDialog *, MarcusPlayer *, SDL_Window *, int selected, float x, float y, int enabled);
void sound_dialog_draw(MarcusSoundDialog *, MarcusPlayer *, MarcusUI *, int width, int height);
int sound_dialog_take(MarcusSoundDialog *, int *selected);
#endif
