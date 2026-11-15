/* Scene sound packets converted from engine/scene.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef MM_ENGINE_SCENE_SOUND_H
#define MM_ENGINE_SCENE_SOUND_H
#include "tag.h"
#ifdef __cplusplus
extern "C"
{
#endif
    enum
    {
        MM_SCENE_SOUND_HEADER_BYTES = 16,
        MM_SCENE_SOUND_WIRE_TAG_BYTES = 20
    };
    typedef struct MMSceneSound
    {
        MMI32 vlm, sty, fLoop, ctagc;
    } MMSceneSound;
    typedef struct MMSceneSoundPacketServices
    {
        void *context;
        size_t tag_bytes;
        void *(*allocate)(void *, MMI32 bytes);
        void (*copy_record)(void *, void *destination, const void *source);
        MMI32 (*sid)(void *, const void *record);
        void (*retain)(void *, void *record), (*clear_cache)(void *, void *record);
        void (*read_tag)(void *, const void *record, MMU32 *child, MMTag *);
        void (*write_tag)(void *, void *record, MMU32 child, const MMTag *);
        void *(*group_data)(void *, void *group, MMI32 index);
        MMI32 (*group_size)(void *, void *group, MMI32 index);
        int (*resolve)(void *, void *record);
        void *(*fetch)(void *, void *record);
        MMI32 (*priority)(void *, void *sound, int loop_tool);
        void (*enqueue)(void *, void *sound, int loop, int queue, MMI32 volume, MMI32 priority,
                        MMU32 start);
        void (*release)(void *, void *sound);
    } MMSceneSoundPacketServices;
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
    MMI32 mm_scene_sound_size(MMI32 count, size_t tag_bytes);
    MMSceneSound *mm_scene_sound_allocate(MMI32 count, const MMSceneSoundPacketServices *);
    MMSceneSound *mm_scene_sound_make(MMI32 volume, MMI32 style, int loop, MMI32 count,
                                                 const void *records, const MMSceneSoundPacketServices *);
    MMSceneSound *mm_scene_sound_duplicate(const MMSceneSound *,
                                                      const MMSceneSoundPacketServices *);
    MMSceneSound *mm_scene_sound_group(void *group, MMI32 index, int retain_tags,
                                                  const MMSceneSoundPacketServices *);
    MMSceneSound *mm_scene_sound_append(const MMSceneSound *, const void *record,
                                                   const MMSceneSoundPacketServices *);
    MMSceneSound *mm_scene_sound_append_tag(const MMSceneSound *, MMU32 child, const MMTag *,
                                                       const MMSceneSoundPacketServices *);
    MMSceneSound *mm_scene_sound_decode(void *bytes, MMI32 size, int swap,
                                                   const MMSceneSoundPacketServices *);
    void *mm_scene_sound_encode(const MMSceneSound *, MMI32 *size,
                                      const MMSceneSoundPacketServices *);
    void mm_scene_sound_play(MMSceneSound *, MMU32 start, const MMSceneSoundPacketServices *);
#ifdef __cplusplus
}
#endif
#endif
