#ifndef MARCUS_TEXT_DIALOG_H
#define MARCUS_TEXT_DIALOG_H
#include "ui.h"
#include "player.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct MarcusTextDialog MarcusTextDialog;
MarcusTextDialog *text_dialog_create(void);
void text_dialog_destroy(MarcusTextDialog *dialog);
int text_dialog_show(MarcusTextDialog *dialog, MarcusPlayer *player, SDL_Window *window, const char *content);
int text_dialog_edit(MarcusTextDialog *dialog, MarcusPlayer *player, SDL_Window *window, const char *content, int object);
int text_dialog_visible(const MarcusTextDialog *dialog);
int text_dialog_event(MarcusTextDialog *dialog, const SDL_Event *event, int width, int height);
void text_dialog_draw(MarcusTextDialog *dialog, MarcusUI *ui, int width, int height);
int text_dialog_take(MarcusTextDialog *dialog, int *edited);
#ifdef __cplusplus
}
#endif
#endif
