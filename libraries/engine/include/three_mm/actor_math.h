#ifndef THREE_MM_ENGINE_ACTOR_MATH_H
#define THREE_MM_ENGINE_ACTOR_MATH_H
#include "actor_motion.h"
#ifdef __cplusplus
extern "C"
{
#endif
    /* 16.16 dot product: accumulate full products before the single arithmetic
     * shift, matching BRender. Arithmetic wraps at the underlying word widths. */
    ThreeMMI32 three_mm_fixed_mac3(ThreeMMI32, ThreeMMI32, ThreeMMI32, ThreeMMI32, ThreeMMI32, ThreeMMI32);
    /* Original BAM lookup/interpolation policy. Zero-angle cosine is 65534;
     * rotations must not be elided merely because their angle is zero. */
    ThreeMMI32 three_mm_angle_sin(unsigned short);
    ThreeMMI32 three_mm_angle_cos(unsigned short);
    unsigned short three_mm_angle_atan2(ThreeMMI32 x, ThreeMMI32 y);
    void three_mm_matrix_identity(ThreeMMActorMatrix *);
    /* Retains the original row/column write order, including translation. As in
     * BRender, general multiplication does not promise alias-safe operands.
     * Rotate and Scale use local scratch and support in-place operation. */
    void three_mm_matrix_multiply(ThreeMMActorMatrix *, const ThreeMMActorMatrix *, const ThreeMMActorMatrix *);
    enum
    {
        THREE_MM_MATRIX_X,
        THREE_MM_MATRIX_Y,
        THREE_MM_MATRIX_Z
    };
    void three_mm_matrix_rotate(ThreeMMActorMatrix *, int axis, unsigned short angle, int pre);
    void three_mm_matrix_scale(ThreeMMActorMatrix *, ThreeMMI32 x, ThreeMMI32 y, ThreeMMI32 z);
#ifdef __cplusplus
}
#endif
#endif
