#ifndef BREN_ZB_CAPTURE_H
#define BREN_ZB_CAPTURE_H

/* Optional, thread-confined triangle sink after BRender lighting/clipping.
 * A non-null sink replaces triangle rasterization. Borrowed maps are valid
 * only during the callback. Clear the sink before destroying its context. */
#ifdef __cplusplus
extern "C"
{
#endif
/* The engine uses four-byte structure packing even on x64. Keep this boundary
 * independent of each consumer's compiler packing switches. */
#pragma pack(push, 4)
    typedef struct BrRasterVertex
    {
        float x, y, z, w, u, v, intensity;
    } BrRasterVertex;
    typedef struct BrRasterMap
    {
        const unsigned char *pixels;
        int width, height, stride;
    } BrRasterMap;
    typedef struct BrRasterTriangle
    {
        BrRasterVertex vertices[3];
        BrRasterMap texture, shade;
        unsigned int flags;
        int index_base, index_range;
    } BrRasterTriangle;
#pragma pack(pop)
    typedef void (*BrTriangleSink)(void *context, const BrRasterTriangle *triangle);
    void BrZbSetTriangleSink(BrTriangleSink sink, void *context);
#ifdef __cplusplus
}
#endif
#endif
