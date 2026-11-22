#ifndef MM_ENGINE_PREVIEW_LIGHT_H
#define MM_ENGINE_PREVIEW_LIGHT_H

/* World pose in work-camera angles. Intensity is brightness in [0, 1]. */
typedef struct MMPreviewLightValues
{
    float x, y, z, yaw, pitch, intensity;
    int point;
} MMPreviewLightValues;

#endif
