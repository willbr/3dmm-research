/* Background resources converted from engine/bkgd.cpp and preview_background.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef THREE_MM_ENGINE_BACKGROUND_RESOURCE_H
#define THREE_MM_ENGINE_BACKGROUND_RESOURCE_H
#include "background_camera.h"
#include "tag.h"
#ifdef __cplusplus
extern "C"
{
#endif
    enum
    {
        THREE_MM_BACKGROUND = 0x424b4744,
        THREE_MM_BACKGROUND_SOUND = 0x42445320,
        THREE_MM_BACKGROUND_COLORS = 0x474c4352,
        THREE_MM_BACKGROUND_LIGHTS = 0x474c4c54,
        THREE_MM_BACKGROUND_CAMERA = 0x43414d20
    };
    enum
    {
        THREE_MM_BACKGROUND_STUDIO,
        THREE_MM_BACKGROUND_PREVIEW
    };
    enum
    {
        THREE_MM_BACKGROUND_READ_ERROR,
        THREE_MM_BACKGROUND_CAMERA_GAPS
    };
    typedef struct ThreeMMBackgroundHeader
    {
        unsigned system, palette_base;
    } ThreeMMBackgroundHeader;
    typedef struct ThreeMMBackgroundSound
    {
        unsigned system;
        ThreeMMI32 volume;
        int loop;
        ThreeMMTag tag;
    } ThreeMMBackgroundSound;
    typedef struct ThreeMMBackgroundLight
    {
        ThreeMMActorMatrix matrix;
        ThreeMMI32 intensity, type;
    } ThreeMMBackgroundLight;
    typedef struct ThreeMMBackgroundResource
    {
        void *actors, *lights, *palette;
        ThreeMMI32 light_count;
        unsigned palette_base;
        int enabled, leave_on;
        ThreeMMBackgroundSound sound;
    } ThreeMMBackgroundResource;
    typedef struct ThreeMMBackgroundServices
    {
        void *context;
        int host_big_endian, debug_checks;
        ThreeMMI32 actor_bytes, light_bytes;
        int (*child)(void *, ThreeMMU32 type, ThreeMMU32 number, ThreeMMU32 id, ThreeMMU32 child_type, ThreeMMU32 *out);
        int (*child_at)(void *, ThreeMMU32 type, ThreeMMU32 number, ThreeMMI32 index, ThreeMMU32 *child_type);
        int (*find)(void *, ThreeMMU32 type, ThreeMMU32 number, int unpack);
        ThreeMMReadStream (*stream)(void *);
        int (*name)(void *, ThreeMMU32 type, ThreeMMU32 number);
        void *(*read_list)(void *, int *big_endian);
        ThreeMMI32 (*count)(void *, void *), (*stride)(void *, void *);
        void (*get)(void *, void *, ThreeMMI32 index, void *, size_t bytes);
        void (*put)(void *, void *, ThreeMMI32 index, const void *, size_t bytes);
        void (*release)(void *, void *);
        int (*allocate)(void *, ThreeMMI32 bytes, void **);
        void (*free_memory)(void *, void **);
        int (*configure_light)(void *, ThreeMMBackgroundResource *, ThreeMMI32, const ThreeMMBackgroundLight *);
        void (*add_light)(void *, ThreeMMBackgroundResource *, ThreeMMI32);
        void (*enable_light)(void *, ThreeMMBackgroundResource *, ThreeMMI32);
        void (*disable_light)(void *, ThreeMMBackgroundResource *, ThreeMMI32);
        void (*remove_light)(void *, ThreeMMBackgroundResource *, ThreeMMI32);
        void *(*new_palette)(void *), *(*duplicate_palette)(void *, void *);
        void (*palette_color)(void *, ThreeMMI32, const unsigned char native_bytes[4]);
        void (*resolve_palette)(void *);
        int (*append_camera)(void *, ThreeMMU32 number);
        int (*select_camera)(void *, ThreeMMI32 index);
        void (*diagnostic)(void *, int issue);
    } ThreeMMBackgroundServices;
    /* Exact 8/28/56-byte records. Wire pointer slots and padding are ignored.
     * Header/sound failure leaves output unchanged. Light input is already bounded. */
    int three_mm_background_header_read(const ThreeMMReadStream *, ThreeMMBackgroundHeader *);
    int three_mm_background_sound_read(const ThreeMMReadStream *, ThreeMMBackgroundSound *);
    void three_mm_background_light_decode(const unsigned char bytes[56], int big, ThreeMMBackgroundLight *);
    void three_mm_background_resource_init(ThreeMMBackgroundResource *);
    /* Services are synchronous, non-reentrant and bound to one file/object. find
     * replaces the borrowed block behind stream/read_list. Fresh owners are required.
     * Studio retains partial arrays/palette on failure for factory disposal; temporary
     * light lists always release, after Studio's failure diagnostic. A failed optional palette read is tolerated only
     * by Studio. Preview keeps its stricter palette/light admission, skips sound/name
     * loading, publishes visual entries in order, then selects camera zero.
     * SDK arrays and native lists are opaque owners; typed callbacks provide their
     * primitives. Camera numbering starts at zero and stops at the first missing ID. */
    int three_mm_background_read(ThreeMMBackgroundResource *, ThreeMMBackgroundCameraState *, ThreeMMU32 type,
                                 ThreeMMU32 number, int profile, const ThreeMMBackgroundServices *);
    void three_mm_background_lights_on(ThreeMMBackgroundResource *, const ThreeMMBackgroundServices *);
    void three_mm_background_lights_off(ThreeMMBackgroundResource *, const ThreeMMBackgroundServices *);
    /* Caller must clear leave_on before teardown. Disposal preserves off/free/free/
     * palette-release order, including after partially completed construction. */
    void three_mm_background_resource_dispose(ThreeMMBackgroundResource *, const ThreeMMBackgroundServices *);
    void *three_mm_background_palette(const ThreeMMBackgroundResource *, ThreeMMI32 *base,
                                      const ThreeMMBackgroundServices *);
    typedef struct ThreeMMBackgroundFactoryServices
    {
        void *context;
        ThreeMMI32 object_bytes, actor_bytes, light_bytes;
        void *(*create)(void *);
        int (*initialize)(void *, void *, ThreeMMU32 type, ThreeMMU32 number);
        ThreeMMI32 (*lights)(void *, void *);
        void (*release)(void *, void *);
        void (*size)(void *, ThreeMMI32);
        void (*publish)(void *, void *);
        void (*failure)(void *);
    } ThreeMMBackgroundFactoryServices;
    /* Estimate publishes even without materialization. Failure invalidates outputs
     * before releasing the partial object; success publishes object before final size. */
    int three_mm_background_factory(ThreeMMU32 type, ThreeMMU32 number, int materialize,
                                    const ThreeMMBackgroundFactoryServices *);
    typedef struct ThreeMMBackgroundTagServices
    {
        void *context;
        int (*insert)(void *, int parent, ThreeMMU32 child_type);
        int (*child)(void *, ThreeMMU32 child_type, ThreeMMTag *);
        int (*cache)(void *, const ThreeMMTag *, int children);
    } ThreeMMBackgroundTagServices;
    /* Preserve BDS/GLLT/GLCR/CAM-zero order. Cache resolves every child before any
     * cache effect. Like the native controller, this does not close returned child
     * tags: cache references acquired by child services retain that lifetime. */
    int three_mm_background_tags(const ThreeMMBackgroundTagServices *);
    int three_mm_background_cache(const ThreeMMTag *, const ThreeMMBackgroundTagServices *);
#ifdef __cplusplus
}
#endif
#endif
