/* Licensed under the MIT License. */
#ifndef MM_VXP_ZIP_H
#define MM_VXP_ZIP_H

#include "mm/vxp_package.h"
#include "mm/zip.h"

#ifdef __cplusplus
extern "C" {
#endif

MMVxpResult mm_vxp_zip_package(MMZip *, const MMVxpLimits *, MMVxpPackage *, MMU32 *failed);

/* Input paths are staged chunky archives. On failure the caller owns any
 * partial output. The dependency name is published after ZIP close succeeds. */
MMVxpResult mm_vxp_zip_write_storyboard(const char *output, const char *content, const char *thumbnails,
                                       MMU32 nonce, MMU32 sequence, char name[MM_VXP_STORYBOARD_NAME_CAPACITY]);

#ifdef __cplusplus
}
#endif
#endif
