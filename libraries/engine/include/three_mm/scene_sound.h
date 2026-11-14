/* Scene sound packets converted from engine/scene.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef THREE_MM_ENGINE_SCENE_SOUND_H
#define THREE_MM_ENGINE_SCENE_SOUND_H
#include "tag.h"
#ifdef __cplusplus
extern "C"
{
#endif
    enum
    {
        THREE_MM_SCENE_SOUND_HEADER_BYTES = 16,
        THREE_MM_SCENE_SOUND_WIRE_TAG_BYTES = 20
    };
    typedef struct ThreeMMSceneSound
    {
        ThreeMMI32 vlm, sty, fLoop, ctagc;
    } ThreeMMSceneSound;
    typedef struct ThreeMMSceneSoundPacketServices
    {
        void *context;
        size_t tag_bytes;
        void *(*allocate)(void *, ThreeMMI32 bytes);
        void (*copy_record)(void *, void *destination, const void *source);
        ThreeMMI32 (*sid)(void *, const void *record);
        void (*retain)(void *, void *record), (*clear_cache)(void *, void *record);
        void (*read_tag)(void *, const void *record, ThreeMMU32 *child, ThreeMMTag *);
        void (*write_tag)(void *, void *record, ThreeMMU32 child, const ThreeMMTag *);
        void *(*group_data)(void *, void *group, ThreeMMI32 index);
        ThreeMMI32 (*group_size)(void *, void *group, ThreeMMI32 index);
        int (*resolve)(void *, void *record);
        void *(*fetch)(void *, void *record);
        ThreeMMI32 (*priority)(void *, void *sound, int loop_tool);
        void (*enqueue)(void *, void *sound, int loop, int queue, ThreeMMI32 volume, ThreeMMI32 priority,
                        ThreeMMU32 start);
        void (*release)(void *, void *sound);
    } ThreeMMSceneSoundPacketServices;
    /* Packet header followed by native tag records of tag_bytes bytes. The caller
     * supplies valid record/packet storage and non-failing typed tag primitives.
     * Allocation sizes fit the legacy signed-32-bit allocator; zero means invalid.
     * allocate leaves header/records uninitialized. make initializes fields and
     * retains every tag; duplicate retains all tags. append retains only its new
     * tag: the caller transfers the old prefix and frees the old allocation raw.
     * Group duplication reacquires its payload after allocation. Allocation may
     * relocate group storage, but must not change its contents/count. Duplication
     * from a group retains only sid-zero tags, or clears every cache when
     * retain_tags is false. Other inputs must remain valid across allocation.
     * Decode validates spans before mutation; valid swapped input is normalized
     * in place before allocation, including on allocation failure. Decoded caches
     * are NULL and unopened. Encode writes native order and zeros reserved cache
     * words. Trailing bytes are accepted/ignored; decode requires positive counts.
     * Playback skips failed sid-zero resolutions, aborts on fetch failure, ignores
     * enqueue failure and releases every fetched reference. Queue/start depend on
     * original index, not successful plays. Tool selection is captured on entry;
     * other header fields remain live. Callbacks must not resize packet storage
     * or reenter packet operations. */
    ThreeMMI32 three_mm_scene_sound_size(ThreeMMI32 count, size_t tag_bytes);
    ThreeMMSceneSound *three_mm_scene_sound_allocate(ThreeMMI32 count, const ThreeMMSceneSoundPacketServices *);
    ThreeMMSceneSound *three_mm_scene_sound_make(ThreeMMI32 volume, ThreeMMI32 style, int loop, ThreeMMI32 count,
                                                 const void *records, const ThreeMMSceneSoundPacketServices *);
    ThreeMMSceneSound *three_mm_scene_sound_duplicate(const ThreeMMSceneSound *,
                                                      const ThreeMMSceneSoundPacketServices *);
    ThreeMMSceneSound *three_mm_scene_sound_group(void *group, ThreeMMI32 index, int retain_tags,
                                                  const ThreeMMSceneSoundPacketServices *);
    ThreeMMSceneSound *three_mm_scene_sound_append(const ThreeMMSceneSound *, const void *record,
                                                   const ThreeMMSceneSoundPacketServices *);
    ThreeMMSceneSound *three_mm_scene_sound_decode(void *bytes, ThreeMMI32 size, int swap,
                                                   const ThreeMMSceneSoundPacketServices *);
    void *three_mm_scene_sound_encode(const ThreeMMSceneSound *, ThreeMMI32 *size,
                                      const ThreeMMSceneSoundPacketServices *);
    void three_mm_scene_sound_play(ThreeMMSceneSound *, ThreeMMU32 start, const ThreeMMSceneSoundPacketServices *);
#ifdef __cplusplus
}
#endif
#endif
