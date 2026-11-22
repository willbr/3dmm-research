#ifndef MARCUS_STORYBOARD_RESOURCE_H
#define MARCUS_STORYBOARD_RESOURCE_H

#include "mm/movie_archive_edit.h"
#include "storyboard_edit_data.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The target is a private archive and must be discarded on failure.
 * Returns NULL and publishes the BKGD number on success. */
const char *marcus_board_write_resources(const MarcusBoardWire *, const MMMovieArchiveEditor *resources,
                                         const MMMovieArchiveEditor *retail, const MMMovieArchiveEditor *target,
                                         MMU32 *background);

#ifdef __cplusplus
}
#endif
#endif
