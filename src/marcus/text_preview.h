#ifndef MARCUS_TEXT_PREVIEW_H
#define MARCUS_TEXT_PREVIEW_H
#include "player.h"
#ifdef __cplusplus
extern "C" {
#endif

/* Native template/isolated worker services. No UI state or widgets live here.
 * Calls, including template preparation, belong to the player owner thread. */
typedef struct MarcusTextPreview MarcusTextPreview;
MarcusTextPreview *text_preview_create(void);
void text_preview_destroy(MarcusTextPreview *preview);
void text_preview_stop(MarcusTextPreview *preview);
int text_preview_start(MarcusTextPreview *preview, MarcusPlayer *player, int object,
                       const MarcusText *text, const char *resources, float yaw, float pitch, float zoom);
/* 0 pending/idle, 1 new image, -1 failed. Pixels are borrowed until next start,
 * stop or destroy. A completed result is published only once. */
int text_preview_poll(MarcusTextPreview *preview, MarcusImage *image);
const char *text_preview_error(const MarcusTextPreview *preview);
/* Palette lookup and third-party OkHSL conversion for swatch ordering. */
int text_preview_palette(MarcusPlayer *player, unsigned rgb[256], float hue[256]);

#ifdef __cplusplus
}
#endif
#endif
