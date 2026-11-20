#ifndef SCENE_MEMBER_FIXTURE_H
#define SCENE_MEMBER_FIXTURE_H
#include "mm/scene_owner.h"

typedef struct SceneMemberMemory
{
    void *buffers[MM_SCENE_MEMBER_LISTS][16];
    unsigned used;
    int attempts, fail_at;
} SceneMemberMemory;

static void *resize_scene_members(void *p, void *previous, MMU32 old_bytes, MMU32 bytes, int priority)
{
    SceneMemberMemory *memory = (SceneMemberMemory *)p;
    unsigned i;
    (void)old_bytes;
    (void)priority;
    ++memory->attempts;
    if ((memory->fail_at && memory->attempts >= memory->fail_at) || bytes > sizeof(memory->buffers[0]))
        return NULL;
    if (previous)
        return previous;
    for (i = 0; i < MM_SCENE_MEMBER_LISTS; ++i)
        if (!(memory->used & (1U << i)))
        {
            memory->used |= 1U << i;
            return memory->buffers[i];
        }
    return NULL;
}

static void release_scene_members(void *p, void *data, MMU32 bytes)
{
    SceneMemberMemory *memory = (SceneMemberMemory *)p;
    unsigned i;
    (void)bytes;
    for (i = 0; i < MM_SCENE_MEMBER_LISTS; ++i)
        if (data == memory->buffers[i])
            memory->used &= ~(1U << i);
}

/* The test owns the backing buffers for the entire fixture. */
static void init_scene_members(MMSceneOwner *owner, SceneMemberMemory *memory)
{
    MMStorageAllocator allocator = {NULL, resize_scene_members, release_scene_members};
    allocator.context = memory;
    mm_scene_owner_init(owner, NULL, 1, &allocator);
}
#endif
