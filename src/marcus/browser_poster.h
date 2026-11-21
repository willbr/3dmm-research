#ifndef MARCUS_BROWSER_POSTER_H
#define MARCUS_BROWSER_POSTER_H

#include "mm/text.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct MarcusBrowserPoster
{
    char title[1021], details[128];
    const char *error;
} MarcusBrowserPoster;

/* Reads the first movie's stored metadata and poster through independent
 * read-only streams. The optional image is written through the supplied
 * allocator, which owns width * height * 4 bytes on success. */
void marcus_browser_poster(const char *movie, const char *resources, const MMTextHost *,
                          MarcusBrowserPoster *, void *image_context,
                          unsigned char *(*image)(void *, MMU32 width, MMU32 height));

#ifdef __cplusplus
}
#endif
#endif
