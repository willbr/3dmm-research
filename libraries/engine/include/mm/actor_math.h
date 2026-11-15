#ifndef MM_ENGINE_ACTOR_MATH_H
#define MM_ENGINE_ACTOR_MATH_H
#include "actor_motion.h"
#ifdef __cplusplus
extern "C"
{
#endif
    /* 16.16 dot product: accumulate full products before the single arithmetic
     * shift, matching BRender. Arithmetic wraps at the underlying word widths. */
    MMI32 mm_fixed_mac3(MMI32, MMI32, MMI32, MMI32, MMI32, MMI32);
    /* Original BAM lookup/interpolation policy. Zero-angle cosine is 65534;
     * rotations must not be elided merely because their angle is zero. */
    MMI32 mm_angle_sin(unsigned short);
    MMI32 mm_angle_cos(unsigned short);
    unsigned short mm_angle_atan2(MMI32 x, MMI32 y);
    void mm_matrix_identity(MMActorMatrix *);
    /* Retains the original row/column write order, including translation. As in
     * BRender, general multiplication does not promise alias-safe operands.
     * Rotate and Scale use local scratch and support in-place operation. */
    void mm_matrix_multiply(MMActorMatrix *, const MMActorMatrix *, const MMActorMatrix *);
    enum
    {
        MM_MATRIX_X,
        MM_MATRIX_Y,
        MM_MATRIX_Z
    };
    void mm_matrix_rotate(MMActorMatrix *, int axis, unsigned short angle, int pre);
    void mm_matrix_scale(MMActorMatrix *, MMI32 x, MMI32 y, MMI32 z);
#ifdef __cplusplus
}
#endif
#endif
