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
    typedef struct MMSceneSoundTag
    {
        MMU32 child;
        MMTag tag;
    } MMSceneSoundTag;
    typedef struct MMSceneSoundWire
    {
        MMSceneSound header;
        const unsigned char *records;
        int swap;
    } MMSceneSoundWire;
    /* Validates the positive count and complete tag span. Borrows the wire
     * bytes without modifying them, and permits trailing bytes. Tag indices
     * are below header.ctagc, and decoded tags have no cache reference. */
    int mm_scene_sound_wire(const void *, MMI32 bytes, int big_endian, MMSceneSoundWire *);
    void mm_scene_sound_wire_tag(const MMSceneSoundWire *, MMI32 index, MMU32 *child, MMTag *);
    typedef struct MMSceneSoundPacketServices
    {
        void *context;
        void *(*allocate)(void *, MMI32 bytes);
        void (*retain)(void *, const MMSceneSoundTag *);
        void *(*group_data)(void *, void *group, MMI32 index);
        MMI32 (*group_size)(void *, void *group, MMI32 index);
        int (*resolve)(void *, MMSceneSoundTag *);
        void *(*fetch)(void *, const MMSceneSoundTag *);
        MMI32 (*priority)(void *, void *sound, int loop_tool);
        void (*enqueue)(void *, void *sound, int loop, int queue, MMI32 volume, MMI32 priority,
                        MMU32 start);
        void (*release)(void *, void *sound);
    } MMSceneSoundPacketServices;
    /* Packet header followed by MMSceneSoundTag records. Live group rows can
     * place records at a four-byte boundary. Get/put copy through aligned values
     * and require a valid packet and index. Effects receive aligned records.
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
    MMI32 mm_scene_sound_size(MMI32 count);
    MMI32 mm_scene_sound_wire_size(MMI32 count);
    /* Returns the byte span of a complete native packet within the supplied data,
     * or zero if the header or its tag records are truncated or invalid. */
    MMI32 mm_scene_sound_span(const void *data, MMI32 bytes);
    void mm_scene_sound_get_tag(const MMSceneSound *, MMI32 index, MMSceneSoundTag *);
    void mm_scene_sound_put_tag(MMSceneSound *, MMI32 index, const MMSceneSoundTag *);
    MMSceneSound *mm_scene_sound_allocate(MMI32 count, const MMSceneSoundPacketServices *);
    MMSceneSound *mm_scene_sound_make(MMI32 volume, MMI32 style, int loop, MMI32 count,
                                      const MMSceneSoundTag *records, const MMSceneSoundPacketServices *);
    MMSceneSound *mm_scene_sound_duplicate(const MMSceneSound *,
                                                      const MMSceneSoundPacketServices *);
    MMSceneSound *mm_scene_sound_group(void *group, MMI32 index, int retain_tags,
                                                  const MMSceneSoundPacketServices *);
    struct MMSceneTrack;
    MMSceneSound *mm_scene_sound_track(const struct MMSceneTrack *, MMI32 index,
                                      const MMSceneSoundPacketServices *);
    MMSceneSound *mm_scene_sound_append(const MMSceneSound *, const MMSceneSoundTag *,
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
