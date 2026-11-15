/* Text shape/action formulas converted from engine tdt.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "mm/text_generation.h"
#include "mm/wide.h"
#define mul mm_fixed_mul
#define div_fixed mm_fixed_div
static MMI32 half(MMI32 v) { return mm_signed_word(((MMU32)v>>1)|((MMU32)v&0x80000000UL)); }
static MMI32 to_fixed(MMI32 v) { return v*65536L; }
static unsigned short to_angle(MMI32 v) { return (unsigned short)mul(v,10430L); }
static MMI32 bound(MMI32 v,MMI32 lo,MMI32 hi) { return v<lo?lo:v>=hi?hi-1:v; }
static void concatenate(MMActorMatrix *m,const MMActorMatrix *p,int pre)
{ MMActorMatrix out; mm_matrix_multiply(&out,pre?p:m,pre?m:p); *m=out; }
static void translate(MMActorMatrix *m,MMI32 x,MMI32 y,MMI32 z,int pre)
{ MMActorMatrix p; mm_matrix_identity(&p); p.m[3][0]=x; p.m[3][1]=y; p.m[3][2]=z; concatenate(m,&p,pre); }
static void scale(MMActorMatrix *m,MMI32 x,MMI32 y,MMI32 z,int pre)
{ MMActorMatrix p; mm_matrix_identity(&p); p.m[0][0]=x; p.m[1][1]=y; p.m[2][2]=z; concatenate(m,&p,pre); }
static void shear(MMActorMatrix *m,int axis,MMI32 x,MMI32 other)
{ MMActorMatrix p; mm_matrix_identity(&p); p.m[axis][0]=x; p.m[axis][axis==1?2:1]=other; concatenate(m,&p,1); }
#define rotate_pre_x(m,a) mm_matrix_rotate((m),0,(unsigned short)(a),1)
#define rotate_post_x(m,a) mm_matrix_rotate((m),0,(unsigned short)(a),0)
#define rotate_pre_y(m,a) mm_matrix_rotate((m),1,(unsigned short)(a),1)
#define rotate_post_y(m,a) mm_matrix_rotate((m),1,(unsigned short)(a),0)
#define rotate_pre_z(m,a) mm_matrix_rotate((m),2,(unsigned short)(a),1)
#define rotate_post_z(m,a) mm_matrix_rotate((m),2,(unsigned short)(a),0)
#define translate_pre(m,x,y,z) translate((m),(x),(y),(z),1)
#define translate_post(m,x,y,z) translate((m),(x),(y),(z),0)
#define scale_pre(m,x,y,z) scale((m),(x),(y),(z),1)
#define scale_post(m,x,y,z) scale((m),(x),(y),(z),0)
#define shear_y(m,x,y) shear((m),1,(x),(y))
#define shear_z(m,x,y) shear((m),2,(x),(y))

void mm_text_apply_action(MMActorMatrix *pbmat34, MMI32 tda, MMI32 ich, MMI32 ccel, MMI32 icel, MMI32 xrChar, MMI32 dxrText)
{





    MMI32 dxrTextHalf = half(dxrText);
    /* fraction of width of text that this char is at */
    MMI32 xrFract = div_fixed(xrChar + dxrTextHalf, dxrText);
    MMI32 rFractCel = div_fixed(to_fixed(icel), to_fixed(ccel));
    MMI32 rSwing = mm_angle_sin(to_angle(mul(411774L, rFractCel)));

    switch (tda)
    {
    case MM_TEXT_REST:
        break;
    case MM_TEXT_LETTER_ROT_X:
        rotate_pre_x(pbmat34, to_angle(mul(rFractCel, 411774L)));
        break;
    case MM_TEXT_LETTER_ROT_Y:
        rotate_pre_y(pbmat34, to_angle(mul(rFractCel, 411774L)));
        break;
    case MM_TEXT_LETTER_ROT_Z:
        rotate_pre_z(pbmat34, to_angle(mul(rFractCel, 411774L)));
        break;
    case MM_TEXT_SWING_X:
        shear_y(pbmat34, mul(16384L, rSwing), 0);
        break;
    case MM_TEXT_SWING_Y:
        shear_z(pbmat34, 0, half(rSwing));
        break;
    case MM_TEXT_SWING_Z:
        shear_y(pbmat34, 0, half(rSwing));
        break;
    case MM_TEXT_PULSE:
        scale_pre(pbmat34, 65536L + mul(16384L, rSwing), 65536L + mul(16384L, rSwing),
                           65536L + mul(16384L, rSwing));
        break;
    case MM_TEXT_WORD_ROT_X:
        rotate_post_x(pbmat34, to_angle(mul(rFractCel, 411774L)));
        break;
    case MM_TEXT_WORD_ROT_Y:
        rotate_post_y(pbmat34, -to_angle(mul(rFractCel, 411774L)));
        break;
    case MM_TEXT_WORD_ROT_Z:
        rotate_post_z(pbmat34, to_angle(mul(rFractCel, 411774L)));
        break;
    case MM_TEXT_WAVE: {
        MMI32 rTheta;
        unsigned short aTheta;
        MMI32 rScaleY;

        rTheta = mul(xrFract - rFractCel, 617662L);
        if (rTheta < -411774L)
            rTheta += 617662L;
        rTheta = bound(rTheta, 0, 205887L);
        aTheta = to_angle(rTheta);
        rScaleY = 65536L + mm_angle_sin(aTheta);
        scale_pre(pbmat34, 65536L, rScaleY, 65536L);
    }
    break;
    case MM_TEXT_REVEAL: {
        MMI32 dxr;
        MMI32 rTheta;
        unsigned short aTheta;
        MMI32 rScaleY;

        if (icel < ccel / 2)
        {
            dxr = div_fixed(to_fixed(icel), to_fixed(ccel / 2));
            dxr = div_fixed(mul(dxr, 262144L), 196608L);
            rTheta = mul(xrFract - dxr, 617662L);
            rTheta += 205887L + 102943L;
            rTheta = bound(rTheta, 102943L, 205887L + 102943L);
        }
        else
        {
            dxr = div_fixed(to_fixed(icel - ccel / 2), to_fixed(ccel / 2));
            dxr = div_fixed(mul(dxr, 262144L), 196608L);
            rTheta = mul(xrFract - dxr, 617662L);
            rTheta += 205887L;
            rTheta = bound(rTheta, -102943L, 102943L);
        }
        aTheta = to_angle(rTheta);
        rScaleY = 6553L + 32768L + mul(32768L, mm_angle_sin(aTheta));
        scale_pre(pbmat34, 65536L, rScaleY, 65536L);
    }
    break;
    case MM_TEXT_WALK: {
        int fOdd = (ich % 2 != 0);
        MMI32 rPhase;
        MMI32 rTheta;
        unsigned short aTheta;
        MMI32 dyr;
        MMI32 dzr;

        rPhase = div_fixed(to_fixed(icel), to_fixed(ccel));
        rTheta = mul(rPhase, 411774L);
        if (fOdd)
        {
            if (icel < ccel / 2)
                dzr = mul(to_fixed(ccel / 2 - icel), 65536L);
            else
                dzr = mul(to_fixed(icel - ccel / 2), 65536L);
        }
        else
        {
            if (icel < ccel / 2)
                dzr = mul(to_fixed(icel), 65536L);
            else
                dzr = mul(to_fixed(ccel - icel), 65536L);
        }
        if (fOdd)
            rTheta -= 205887L;
        rTheta = bound(rTheta, 0, 205887L);
        aTheta = to_angle(rTheta);
        dyr = mm_angle_sin(aTheta);
        translate_pre(pbmat34, 0, dyr, dzr);
    }
    break;
    case MM_TEXT_HOP: {
        MMI32 rPhase;
        MMI32 rTheta;
        unsigned short aTheta;
        MMI32 dyr = 0;
        MMI32 yrScale = 65536L;

        if (icel < ccel * 2 / 3)
        {
            rPhase = div_fixed(to_fixed(icel), to_fixed(ccel * 2 / 3));
            rTheta = mul(rPhase, 205887L);
            aTheta = to_angle(rTheta);
            dyr = mm_angle_sin(aTheta);
        }
        else
        {
            rPhase = div_fixed(to_fixed(icel - (ccel * 2 / 3)), to_fixed(ccel / 3));
            /* rTheta goes from 205887L to 411774L */
            rTheta = mul(rPhase, 205887L) + 205887L;
            aTheta = to_angle(rTheta);
            yrScale = 65536L + div_fixed(mm_angle_sin(aTheta), 131072L);
        }
        translate_pre(pbmat34, 0, dyr, 0);
        scale_pre(pbmat34, 65536L, yrScale, 65536L);
    }
    break;
    case MM_TEXT_STRETCH: {
        MMI32 rPhase;
        MMI32 rTheta;
        unsigned short aTheta;
        MMI32 dyr = 0;


        rPhase = div_fixed(to_fixed(icel), to_fixed(ccel));
        rTheta = mul(rPhase, 411774L) + 205887L + 102943L;
        aTheta = to_angle(rTheta);
        dyr = mm_angle_sin(aTheta);

        scale_pre(pbmat34, 98304L + half(dyr), 65536L, 65536L);
    }
    break;
    default:
        (void)0;
        break;
    }
}

void mm_text_apply_shape(MMActorMatrix *pbmat34, MMI32 tdts, MMI32 cch, MMI32 ich, MMI32 xrChar, MMI32 dxrText, MMI32 yrChar, MMI32 dyrMax,
                      MMI32 dyrTotal)
{





    MMI32 dyr = 0;
    MMI32 dzr = 0;

    MMI32 dxrTextHalf = half(dxrText);
    MMI32 dxrTextQuarter = half(dxrTextHalf);
    /* fraction of width of text that this char is at */
    MMI32 xrFract = div_fixed(xrChar + dxrTextHalf, dxrText);
    MMI32 rRadius = div_fixed(dxrText, 411774L);

    (void)cch; (void)ich; (void)dyrMax;
    switch (tdts)
    {
    case MM_TEXT_NORMAL:
        translate_post(pbmat34, xrChar, 0, 0);
        break;
    case MM_TEXT_ARCH_POSITIVE:
        dyr = mul(dxrTextQuarter, mm_angle_sin(to_angle(mul(205887L, xrFract))));
        translate_post(pbmat34, xrChar, dyr, 0);
        break;
    case MM_TEXT_ARCH_NEGATIVE:
        dyr = mul(dxrTextQuarter, 65536L - mm_angle_sin(to_angle(mul(205887L, xrFract))));
        translate_post(pbmat34, xrChar, dyr, 0);
        break;
    case MM_TEXT_CIRCLE_Y:
        translate_post(pbmat34, 0, 0, rRadius);
        rotate_post_y(pbmat34, to_angle(mul(411774L, xrFract)));
        break;
    case MM_TEXT_LARGE_MIDDLE:
        translate_post(pbmat34, xrChar, 0, 0);
        scale_post(pbmat34, 65536L, 65536L + mm_angle_sin(to_angle(mul(205887L, xrFract))), 65536L);
        break;
    case MM_TEXT_ARCH_Z:
        dzr = mul(dxrTextQuarter, mm_angle_sin(to_angle(mul(205887L, xrFract))));
        translate_post(pbmat34, xrChar, 0, dzr);
        break;
    case MM_TEXT_CIRCLE_Z:
        translate_post(pbmat34, 0, rRadius, 0);
        rotate_post_z(pbmat34, -to_angle(mul(411774L, xrFract)));
        break;
    case MM_TEXT_VERTICAL:
        translate_post(pbmat34, 0, dyrTotal - yrChar, 0);
        break;
    case MM_TEXT_GROW_RIGHT:
        translate_post(pbmat34, xrChar, 0, 0);
        scale_post(pbmat34, 65536L, 65536L + xrFract, 65536L);
        break;
    case MM_TEXT_GROW_LEFT:
        translate_post(pbmat34, xrChar, 0, 0);
        scale_post(pbmat34, 65536L, 65536L + (65536L - xrFract), 65536L);
        break;
    default:
        (void)0;
        break;
    }
}
