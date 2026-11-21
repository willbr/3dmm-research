/* Studio Kidspace descriptor and monochrome cursor resources. */
#ifndef MM_ENGINE_STUDIO_RESOURCE_H
#define MM_ENGINE_STUDIO_RESOURCE_H
#include "collection_storage.h"
#ifdef __cplusplus
extern "C"
{
#endif
    typedef struct MMKidspaceLocation
    {
        MMI32 hidPar, xp, yp, zp;
    } MMKidspaceLocation;
    typedef struct MMKidspaceCursor
    {
        MMU32 grfcustMask, grfcust, grfbitSno, cnoCurs, chidScript;
        MMI32 cidDefault;
        MMU32 cnoTopic;
    } MMKidspaceCursor;
    typedef struct MMKidspaceDescriptor
    {
        MMCollectionStorage storage;
        MMI32 kind;
        MMU32 locations, cursors;
        int big_endian;
    } MMKidspaceDescriptor;
    /* Read into a fresh descriptor. On success it owns immutable wire records
     * until disposed. The final location always has parent zero. */
    int mm_kidspace_read(MMKidspaceDescriptor *, const MMReadStream *, const MMStorageAllocator *);
    void mm_kidspace_dispose(MMKidspaceDescriptor *);
    void mm_kidspace_location(const MMKidspaceDescriptor *, MMI32 parent, MMKidspaceLocation *);
    int mm_kidspace_cursor(const MMKidspaceDescriptor *, MMU32 flags, MMU32 state, MMKidspaceCursor *);

    typedef struct MMStudioCursor
    {
        int width, height, pitch, hot_x, hot_y;
        unsigned char and_bits[128], xor_bits[128];
    } MMStudioCursor;
    /* Pick the first largest supported monochrome image that fits. Bounds and
     * hotspots use pixels. Output is unchanged on failure. */
    int mm_studio_cursor_read(const MMReadStream *, int width, int height, MMStudioCursor *);
#ifdef __cplusplus
}
#endif
#endif
