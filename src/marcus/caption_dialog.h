#ifndef MARCUS_CAPTION_DIALOG_H
#define MARCUS_CAPTION_DIALOG_H
#include "player.h"

typedef struct MarcusCaptionDialog MarcusCaptionDialog;
MarcusCaptionDialog *caption_dialog_create(void);
void caption_dialog_destroy(MarcusCaptionDialog *dialog);
int caption_dialog_show(MarcusCaptionDialog *dialog, MarcusPlayer *player, const MarcusInfo *info, int add);
int caption_dialog_visible(const MarcusCaptionDialog *dialog);
/* Returns one after a caption edit commits. */
int caption_dialog_draw(MarcusCaptionDialog *dialog, MarcusPlayer *player, int width, int height);
#endif
