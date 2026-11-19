#ifndef MARCUS_STORYBOARD_RESOURCE_H
#define MARCUS_STORYBOARD_RESOURCE_H

#include "mm/actor_motion.h"
#include "mm/movie_archive_edit.h"
#include "storyboard_edit_data.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct MarcusBoardTransforms
{
    void (*matrix)(MMActorMatrix *, float x, float y, float z, unsigned short yaw,
                   unsigned short pitch, unsigned short roll);
    int (*inverse)(MMActorMatrix *, const MMActorMatrix *);
    void (*apply)(MMActorPoint *, const MMActorPoint *, const MMActorMatrix *);
} MarcusBoardTransforms;

/* The target is a private archive and must be discarded on failure.
 * Returns NULL and publishes the BKGD number on success. */
const char *marcus_board_write_resources(const MarcusBoardWire *, const MMMovieArchiveEditor *resources,
                                         const MMMovieArchiveEditor *retail, const MMMovieArchiveEditor *target,
                                         const MarcusBoardTransforms *, MMU32 *background);

#ifdef __cplusplus
}
#endif
#endif
