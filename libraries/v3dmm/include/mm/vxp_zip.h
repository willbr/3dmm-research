/* Licensed under the MIT License. */
#ifndef MM_VXP_ZIP_H
#define MM_VXP_ZIP_H

#include "mm/vxp_package.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct MMVxpZip MMVxpZip;
typedef struct MMVxpZipInput
{
    void *context;
    MMVxpSize length;
    int (*read)(void *, MMVxpSize offset, void *, size_t);
} MMVxpZipInput;
typedef int (*MMVxpZipSink)(void *, const void *, size_t);

/* The returned handle owns the ZIP reader and borrows the input's context.
 * The source range must stay readable until the handle is closed. */
MMVxpResult mm_vxp_zip_open(const MMVxpZipInput *, MMVxpZip **);
void mm_vxp_zip_close(MMVxpZip *);
MMVxpResult mm_vxp_zip_package(MMVxpZip *, const MMVxpLimits *, MMVxpPackage *, MMU32 *failed);
/* Returns the required byte count including the terminator, or zero for
 * an invalid slot. A null buffer queries the size. */
size_t mm_vxp_zip_member_name(MMVxpZip *, MMU32 slot, char *buffer, size_t capacity);
int mm_vxp_zip_extract(MMVxpZip *, MMU32 slot, MMVxpZipSink, void *context);

/* Input paths are staged chunky archives. On failure the caller owns any
 * partial output. The dependency name is published after ZIP close succeeds. */
MMVxpResult mm_vxp_zip_write_storyboard(const char *output, const char *content, const char *thumbnails,
                                       MMU32 nonce, MMU32 sequence, char name[MM_VXP_STORYBOARD_NAME_CAPACITY]);

#ifdef __cplusplus
}
#endif
#endif
