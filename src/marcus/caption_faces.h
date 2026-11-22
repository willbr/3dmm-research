#ifndef MARCUS_CAPTION_FACES_H
#define MARCUS_CAPTION_FACES_H

#include "mm/caption_layout.h"

#ifdef __cplusplus
extern "C" {
#endif

enum { MARCUS_CAPTION_FACE_LIMIT = 128 };
typedef struct MarcusCaptionFaces
{
    struct MarcusCaptionFaceEntry *entries[MARCUS_CAPTION_FACE_LIMIT];
    size_t count;
} MarcusCaptionFaces;

typedef struct MarcusCaptionFaceOptions
{
    MMI32 pixels;
    int bold, italic, underline;
} MarcusCaptionFaceOptions;

enum
{
    MARCUS_CAPTION_FACE_MISSING = 0, MARCUS_CAPTION_FACE_READY = 1,
    MARCUS_CAPTION_FACE_FAILED = -1, MARCUS_CAPTION_FACE_MEMORY = -2
};

typedef struct MarcusCaptionFaceServices
{
    void *context;
    /* MISSING means the provider cannot supply the family. An identified
     * bundled face that fails to open returns FAILED. READY transfers a face. */
    int (*bundled)(void *, const MMCaptionFont *, const MarcusCaptionFaceOptions *, void **);
    int (*installed)(void *, const void *, size_t units, int utf16, const MarcusCaptionFaceOptions *, void **);
    void (*release)(void *);
} MarcusCaptionFaceServices;

/* Initialize to zero. The cache owns each loaded face until it is cleared or
 * replaced at its limit. Callbacks are synchronous and do not reenter or throw.
 * A failed lookup does not publish a face. */
int marcus_caption_face_get(MarcusCaptionFaces *, const MMCaptionStyle *, const MarcusCaptionFaceServices *, void **);
void marcus_caption_faces_clear(MarcusCaptionFaces *);

#ifdef __cplusplus
}
#endif
#endif
