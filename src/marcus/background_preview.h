#ifndef MARCUS_BACKGROUND_PREVIEW_H
#define MARCUS_BACKGROUND_PREVIEW_H
#include "player.h"
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif
typedef struct MarcusBackgroundPreviews MarcusBackgroundPreviews;
MarcusBackgroundPreviews *marcus_background_previews_create(void);
void marcus_background_previews_destroy(MarcusBackgroundPreviews *);

enum
{
    MARCUS_BACKGROUND_PREVIEWS_FAILED,
    MARCUS_BACKGROUND_PREVIEWS_UNCHANGED,
    MARCUS_BACKGROUND_PREVIEWS_CONFIGURED
};
/* Reconfiguration cancels the old job when the source identity changes.
 * Take returns 1 with borrowed pixels, 0 while pending, or -1 on failure.
 * Pixel storage lasts until the next completion, reconfiguration, or destroy. */
int marcus_background_previews_configure(MarcusBackgroundPreviews *, const char *resources, const char *content);
int marcus_background_previews_start(MarcusBackgroundPreviews *, unsigned long chunk, char *error, size_t capacity);
int marcus_background_previews_take(MarcusBackgroundPreviews *, MarcusImage *, char *error, size_t capacity);
MarcusFramePreviews *marcus_model_previews_create(const char *resources, const char *content,
                                                  char *error, size_t capacity);
#ifdef __cplusplus
}
#endif
#endif
