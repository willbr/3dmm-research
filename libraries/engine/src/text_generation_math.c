/* Text shape/action formulas converted from engine tdt.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "three_mm/text_generation.h"
#include "three_mm/wide.h"
#define mul three_mm_fixed_mul
#define div_fixed three_mm_fixed_div
static ThreeMMI32 half(ThreeMMI32 v) { return three_mm_signed_word(((ThreeMMU32)v>>1)|((ThreeMMU32)v&0x80000000UL)); }
static ThreeMMI32 to_fixed(ThreeMMI32 v) { return v*65536L; }
static unsigned short to_angle(ThreeMMI32 v) { return (unsigned short)mul(v,10430L); }
static ThreeMMI32 bound(ThreeMMI32 v,ThreeMMI32 lo,ThreeMMI32 hi) { return v<lo?lo:v>=hi?hi-1:v; }
static void concatenate(ThreeMMActorMatrix *m,const ThreeMMActorMatrix *p,int pre)
{ ThreeMMActorMatrix out; three_mm_matrix_multiply(&out,pre?p:m,pre?m:p); *m=out; }
static void translate(ThreeMMActorMatrix *m,ThreeMMI32 x,ThreeMMI32 y,ThreeMMI32 z,int pre)
{ ThreeMMActorMatrix p; three_mm_matrix_identity(&p); p.m[3][0]=x; p.m[3][1]=y; p.m[3][2]=z; concatenate(m,&p,pre); }
static void scale(ThreeMMActorMatrix *m,ThreeMMI32 x,ThreeMMI32 y,ThreeMMI32 z,int pre)
{ ThreeMMActorMatrix p; three_mm_matrix_identity(&p); p.m[0][0]=x; p.m[1][1]=y; p.m[2][2]=z; concatenate(m,&p,pre); }
static void shear(ThreeMMActorMatrix *m,int axis,ThreeMMI32 x,ThreeMMI32 other)
{ ThreeMMActorMatrix p; three_mm_matrix_identity(&p); p.m[axis][0]=x; p.m[axis][axis==1?2:1]=other; concatenate(m,&p,1); }
#define rotate_pre_x(m,a) three_mm_matrix_rotate((m),0,(unsigned short)(a),1)
#define rotate_post_x(m,a) three_mm_matrix_rotate((m),0,(unsigned short)(a),0)
#define rotate_pre_y(m,a) three_mm_matrix_rotate((m),1,(unsigned short)(a),1)
#define rotate_post_y(m,a) three_mm_matrix_rotate((m),1,(unsigned short)(a),0)
#define rotate_pre_z(m,a) three_mm_matrix_rotate((m),2,(unsigned short)(a),1)
#define rotate_post_z(m,a) three_mm_matrix_rotate((m),2,(unsigned short)(a),0)
#define translate_pre(m,x,y,z) translate((m),(x),(y),(z),1)
#define translate_post(m,x,y,z) translate((m),(x),(y),(z),0)
#define scale_pre(m,x,y,z) scale((m),(x),(y),(z),1)
#define scale_post(m,x,y,z) scale((m),(x),(y),(z),0)
#define shear_y(m,x,y) shear((m),1,(x),(y))
#define shear_z(m,x,y) shear((m),2,(x),(y))

void three_mm_text_apply_action(ThreeMMActorMatrix *pbmat34, ThreeMMI32 tda, ThreeMMI32 ich, ThreeMMI32 ccel, ThreeMMI32 icel, ThreeMMI32 xrChar, ThreeMMI32 dxrText)
{





    ThreeMMI32 dxrTextHalf = half(dxrText);
    /* fraction of width of text that this char is at */
    ThreeMMI32 xrFract = div_fixed(xrChar + dxrTextHalf, dxrText);
    ThreeMMI32 rFractCel = div_fixed(to_fixed(icel), to_fixed(ccel));
    ThreeMMI32 rSwing = three_mm_angle_sin(to_angle(mul(411774L, rFractCel)));

    switch (tda)
    {
    case THREE_MM_TEXT_REST:
        break;
    case THREE_MM_TEXT_LETTER_ROT_X:
        rotate_pre_x(pbmat34, to_angle(mul(rFractCel, 411774L)));
        break;
    case THREE_MM_TEXT_LETTER_ROT_Y:
        rotate_pre_y(pbmat34, to_angle(mul(rFractCel, 411774L)));
        break;
    case THREE_MM_TEXT_LETTER_ROT_Z:
        rotate_pre_z(pbmat34, to_angle(mul(rFractCel, 411774L)));
        break;
    case THREE_MM_TEXT_SWING_X:
        shear_y(pbmat34, mul(16384L, rSwing), 0);
        break;
    case THREE_MM_TEXT_SWING_Y:
        shear_z(pbmat34, 0, half(rSwing));
        break;
    case THREE_MM_TEXT_SWING_Z:
        shear_y(pbmat34, 0, half(rSwing));
        break;
    case THREE_MM_TEXT_PULSE:
        scale_pre(pbmat34, 65536L + mul(16384L, rSwing), 65536L + mul(16384L, rSwing),
                           65536L + mul(16384L, rSwing));
        break;
    case THREE_MM_TEXT_WORD_ROT_X:
        rotate_post_x(pbmat34, to_angle(mul(rFractCel, 411774L)));
        break;
    case THREE_MM_TEXT_WORD_ROT_Y:
        rotate_post_y(pbmat34, -to_angle(mul(rFractCel, 411774L)));
        break;
    case THREE_MM_TEXT_WORD_ROT_Z:
        rotate_post_z(pbmat34, to_angle(mul(rFractCel, 411774L)));
        break;
    case THREE_MM_TEXT_WAVE: {
        ThreeMMI32 rTheta;
        unsigned short aTheta;
        ThreeMMI32 rScaleY;

        rTheta = mul(xrFract - rFractCel, 617662L);
        if (rTheta < -411774L)
            rTheta += 617662L;
        rTheta = bound(rTheta, 0, 205887L);
        aTheta = to_angle(rTheta);
        rScaleY = 65536L + three_mm_angle_sin(aTheta);
        scale_pre(pbmat34, 65536L, rScaleY, 65536L);
    }
    break;
    case THREE_MM_TEXT_REVEAL: {
        ThreeMMI32 dxr;
        ThreeMMI32 rTheta;
        unsigned short aTheta;
        ThreeMMI32 rScaleY;

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
        rScaleY = 6553L + 32768L + mul(32768L, three_mm_angle_sin(aTheta));
        scale_pre(pbmat34, 65536L, rScaleY, 65536L);
    }
    break;
    case THREE_MM_TEXT_WALK: {
        int fOdd = (ich % 2 != 0);
        ThreeMMI32 rPhase;
        ThreeMMI32 rTheta;
        unsigned short aTheta;
        ThreeMMI32 dyr;
        ThreeMMI32 dzr;

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
        dyr = three_mm_angle_sin(aTheta);
        translate_pre(pbmat34, 0, dyr, dzr);
    }
    break;
    case THREE_MM_TEXT_HOP: {
        ThreeMMI32 rPhase;
        ThreeMMI32 rTheta;
        unsigned short aTheta;
        ThreeMMI32 dyr = 0;
        ThreeMMI32 yrScale = 65536L;

        if (icel < ccel * 2 / 3)
        {
            rPhase = div_fixed(to_fixed(icel), to_fixed(ccel * 2 / 3));
            rTheta = mul(rPhase, 205887L);
            aTheta = to_angle(rTheta);
            dyr = three_mm_angle_sin(aTheta);
        }
        else
        {
            rPhase = div_fixed(to_fixed(icel - (ccel * 2 / 3)), to_fixed(ccel / 3));
            /* rTheta goes from 205887L to 411774L */
            rTheta = mul(rPhase, 205887L) + 205887L;
            aTheta = to_angle(rTheta);
            yrScale = 65536L + div_fixed(three_mm_angle_sin(aTheta), 131072L);
        }
        translate_pre(pbmat34, 0, dyr, 0);
        scale_pre(pbmat34, 65536L, yrScale, 65536L);
    }
    break;
    case THREE_MM_TEXT_STRETCH: {
        ThreeMMI32 rPhase;
        ThreeMMI32 rTheta;
        unsigned short aTheta;
        ThreeMMI32 dyr = 0;


        rPhase = div_fixed(to_fixed(icel), to_fixed(ccel));
        rTheta = mul(rPhase, 411774L) + 205887L + 102943L;
        aTheta = to_angle(rTheta);
        dyr = three_mm_angle_sin(aTheta);

        scale_pre(pbmat34, 98304L + half(dyr), 65536L, 65536L);
    }
    break;
    default:
        (void)0;
        break;
    }
}

void three_mm_text_apply_shape(ThreeMMActorMatrix *pbmat34, ThreeMMI32 tdts, ThreeMMI32 cch, ThreeMMI32 ich, ThreeMMI32 xrChar, ThreeMMI32 dxrText, ThreeMMI32 yrChar, ThreeMMI32 dyrMax,
                      ThreeMMI32 dyrTotal)
{





    ThreeMMI32 dyr = 0;
    ThreeMMI32 dzr = 0;

    ThreeMMI32 dxrTextHalf = half(dxrText);
    ThreeMMI32 dxrTextQuarter = half(dxrTextHalf);
    /* fraction of width of text that this char is at */
    ThreeMMI32 xrFract = div_fixed(xrChar + dxrTextHalf, dxrText);
    ThreeMMI32 rRadius = div_fixed(dxrText, 411774L);

    (void)cch; (void)ich; (void)dyrMax;
    switch (tdts)
    {
    case THREE_MM_TEXT_NORMAL:
        translate_post(pbmat34, xrChar, 0, 0);
        break;
    case THREE_MM_TEXT_ARCH_POSITIVE:
        dyr = mul(dxrTextQuarter, three_mm_angle_sin(to_angle(mul(205887L, xrFract))));
        translate_post(pbmat34, xrChar, dyr, 0);
        break;
    case THREE_MM_TEXT_ARCH_NEGATIVE:
        dyr = mul(dxrTextQuarter, 65536L - three_mm_angle_sin(to_angle(mul(205887L, xrFract))));
        translate_post(pbmat34, xrChar, dyr, 0);
        break;
    case THREE_MM_TEXT_CIRCLE_Y:
        translate_post(pbmat34, 0, 0, rRadius);
        rotate_post_y(pbmat34, to_angle(mul(411774L, xrFract)));
        break;
    case THREE_MM_TEXT_LARGE_MIDDLE:
        translate_post(pbmat34, xrChar, 0, 0);
        scale_post(pbmat34, 65536L, 65536L + three_mm_angle_sin(to_angle(mul(205887L, xrFract))), 65536L);
        break;
    case THREE_MM_TEXT_ARCH_Z:
        dzr = mul(dxrTextQuarter, three_mm_angle_sin(to_angle(mul(205887L, xrFract))));
        translate_post(pbmat34, xrChar, 0, dzr);
        break;
    case THREE_MM_TEXT_CIRCLE_Z:
        translate_post(pbmat34, 0, rRadius, 0);
        rotate_post_z(pbmat34, -to_angle(mul(411774L, xrFract)));
        break;
    case THREE_MM_TEXT_VERTICAL:
        translate_post(pbmat34, 0, dyrTotal - yrChar, 0);
        break;
    case THREE_MM_TEXT_GROW_RIGHT:
        translate_post(pbmat34, xrChar, 0, 0);
        scale_post(pbmat34, 65536L, 65536L + xrFract, 65536L);
        break;
    case THREE_MM_TEXT_GROW_LEFT:
        translate_post(pbmat34, xrChar, 0, 0);
        scale_post(pbmat34, 65536L, 65536L + (65536L - xrFract), 65536L);
        break;
    default:
        (void)0;
        break;
    }
}
