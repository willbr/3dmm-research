#ifndef MM_ENGINE_ELLIPSE_GEOMETRY_H
#define MM_ENGINE_ELLIPSE_GEOMETRY_H
#include "types.h"
#ifdef __cplusplus
extern "C" {
#endif

#define MM_ELLIPSE_MAX_EXTENT 67108864L

typedef struct MMEllipseSpan
{
    MMI32 left, right;
} MMEllipseSpan;

/* Write count rows in local coordinates to the caller's output. Rows outside
 * the ellipse and zero extents are empty. Failures preserve the output. */
int mm_ellipse_rows(MMI32 width, MMI32 height, MMI32 first_row, MMI32 count, MMEllipseSpan *output);

#ifdef __cplusplus
}
#endif
#endif
