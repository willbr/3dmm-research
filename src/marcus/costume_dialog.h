#ifndef MARCUS_COSTUME_DIALOG_H
#define MARCUS_COSTUME_DIALOG_H
#include "ui.h"
#include "player.h"
typedef struct MarcusCostumeDialog MarcusCostumeDialog;
MarcusCostumeDialog *costume_dialog_create(void);
void costume_dialog_destroy(MarcusCostumeDialog *dialog);
int costume_dialog_visible(const MarcusCostumeDialog *dialog);
int costume_dialog_event(MarcusCostumeDialog *dialog, const SDL_Event *event);
void costume_dialog_button(MarcusCostumeDialog *dialog, MarcusPlayer *player, int object,
    float x, float y, int enabled);
void costume_dialog_draw(MarcusCostumeDialog *dialog, MarcusUI *ui, int width, int height);
int costume_dialog_take(MarcusCostumeDialog *dialog);
#endif
