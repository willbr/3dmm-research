#include "scene_sound_fixture.h"
static void factories(void)
{
    Fixture f;
    MMSceneSound *p, *copy, *added;
    int before;
    init(&f);
    CHECK(mm_scene_sound_size(0, 32) == 16 && !mm_scene_sound_size(-1, 32));
    CHECK(!mm_scene_sound_size(1, 0) && !mm_scene_sound_size(1, (size_t)-1));
    CHECK(mm_scene_sound_size(67108863, 32) == 2147483632 && !mm_scene_sound_size(67108864, 32));
    CHECK(!mm_scene_sound_size(2147483647, 32));
    p = mm_scene_sound_allocate(2, &f.s);
    CHECK(p && ((unsigned char *)p)[0] == 0xa5);
    raw_free(&f, p);
    f.fail = 1;
    CHECK(!mm_scene_sound_make(1, 2, 0, 3, f.input, &f.s) && !f.retains);
    f.fail = 0;
    p = make(&f, 3);
    CHECK(f.retains == 3 && f.refs[1] == 1 && f.refs[3] == 1 && at(p, 1)->tag.cache == &f);
    before = f.retains;
    f.fail = 1;
    CHECK(!mm_scene_sound_duplicate(p, &f.s) && f.retains == before);
    CHECK(!mm_scene_sound_append(p, f.input + 3, &f.s) && f.retains == before);
    f.fail = 0;
    p->fLoop = 0x01020304;
    copy = mm_scene_sound_duplicate(p, &f.s);
    CHECK(copy && f.retains == before + 3 && f.refs[1] == 2 && copy->fLoop == p->fLoop);
    CHECK(!memcmp(p, copy, (size_t)mm_scene_sound_size(3, sizeof(Record))));
    drop(&f, copy);
    f.input[3].tag.sid = 0;
    before = f.retains;
    added = mm_scene_sound_append(p, f.input + 3, &f.s);
    CHECK(added && added->ctagc == 4 && f.retains == before + 1);
    CHECK(f.refs[1] == 1 && f.refs[3] == 1 && f.refs[4] == 1 && added->fLoop == p->fLoop);
    raw_free(&f, p);
    drop(&f, added);
    clean(&f);
    init(&f);
    p = make(&f, 0);
    CHECK(p->ctagc == 0 && !f.retains);
    copy = mm_scene_sound_duplicate(p, &f.s);
    CHECK(copy);
    drop(&f, copy);
    drop(&f, p);
    clean(&f);
}
static void groups(void)
{
    Fixture f;
    MMSceneSound *p, *copy;
    int mode, previous;
    for (mode = 0; mode < 2; ++mode)
    {
        init(&f);
        p = make(&f, 3);
        f.group_bytes = mm_scene_sound_size(3, sizeof(Record));
        f.group = (unsigned char *)malloc((size_t)f.group_bytes);
        CHECK(f.group);
        memcpy(f.group, p, (size_t)f.group_bytes);
        f.fail = 1;
        previous = f.retains;
        CHECK(!mm_scene_sound_group(&f, 0, mode, &f.s));
        CHECK(f.lookups == 1 && f.retains == previous);
        f.fail = 0;
        f.relocate = 1;
        f.lookups = 0;
        copy = mm_scene_sound_group(&f, 0, mode, &f.s);
        CHECK(copy && f.lookups == 2 && f.stale);
        CHECK(f.retains == previous + (mode ? 2 : 0) && f.clears == (mode ? 0 : 3));
        CHECK(at(copy, 1)->tag.cache == (mode ? (void *)&f : NULL));
        drop(&f, copy);
        f.group_bytes = 15;
        f.lookups = 0;
        previous = f.allocs;
        CHECK(!mm_scene_sound_group(&f, 0, 1, &f.s) && !f.lookups && f.allocs == previous);
        f.group_bytes = 16;
        CHECK(!mm_scene_sound_group(&f, 0, 1, &f.s) && f.allocs == previous);
        f.group_bytes = mm_scene_sound_size(3, sizeof(Record));
        ((MMSceneSound *)f.group)->ctagc = -1;
        CHECK(!mm_scene_sound_group(&f, 0, 1, &f.s));
        drop(&f, p);
        clean(&f);
    }
}
static MMU32 load(const unsigned char *p)
{
    MMU32 n;
    memcpy(&n, p, 4);
    return n;
}
static void store(unsigned char *p, MMU32 n)
{
    memcpy(p, &n, 4);
}
static void swap_words(unsigned char *p, int bytes)
{
    int i;
    MMU32 n;
    for (i = 0; i < bytes; i += 4)
    {
        n = load(p + i);
        store(p + i, ((n & 255) << 24) | ((n & 65280) << 8) | ((n >> 8) & 65280) | (n >> 24));
    }
}
static void codecs(void)
{
    Fixture f;
    MMSceneSound *p, *copy;
    MMSceneSoundWire view;
    MMTag tag;
    MMU32 child;
    const unsigned short one = 1;
    const int native_big = *(const unsigned char *)&one == 0;
    unsigned char *wire, *buffer, *encoded;
    unsigned char canonical[56], saved[61];
    MMI32 bytes, output;
    int swapped, length, i, before;
    init(&f);
    p = mm_scene_sound_allocate(2, &f.s);
    CHECK(p);
    p->vlm = -123;
    p->sty = 2147483647;
    p->fLoop = 0x01020304;
    p->ctagc = 2;
    memcpy(at(p, 0), f.input, 2 * sizeof(Record));
    at(p, 0)->tag.sid = -1;
    at(p, 0)->tag.type = 0xfedcba98U;
    at(p, 0)->tag.number = 0x89abcdefU;
    at(p, 0)->child = 0x80000000U;
    f.fail = 1;
    bytes = 99;
    CHECK(!mm_scene_sound_encode(p, &bytes, &f.s) && !bytes);
    f.fail = 0;
    wire = (unsigned char *)mm_scene_sound_encode(p, &bytes, &f.s);
    CHECK(wire && bytes == 56);
    CHECK(load(wire + 16) == 0x80000000U && load(wire + 20) == 0xffffffffU && !load(wire + 24));
    CHECK(load(wire + 28) == 0xfedcba98U && load(wire + 32) == 0x89abcdefU && !load(wire + 44));
    memcpy(canonical, wire, 56);
    store(canonical + 24, 0x12345678U);
    for (swapped = 0; swapped < 2; ++swapped)
    {
        buffer = (unsigned char *)malloc(63);
        CHECK(buffer);
        memset(buffer, 0xee, 63);
        memcpy(buffer + 1, canonical, 56);
        if (swapped)
            swap_words(buffer + 1, 56);
        memcpy(saved, buffer + 1, 61);
        before = f.allocs;
        for (length = 0; length < 56; ++length)
        {
            CHECK(!mm_scene_sound_wire(buffer + 1, length, native_big != swapped, &view));
            CHECK(!mm_scene_sound_decode(buffer + 1, length, swapped, &f.s));
            CHECK(!memcmp(saved, buffer + 1, 61));
        }
        CHECK(mm_scene_sound_wire(buffer + 1, 61, native_big != swapped, &view) &&
              !memcmp(&view.header, p, sizeof(view.header)));
        mm_scene_sound_wire_tag(&view, 0, &child, &tag);
        CHECK(child == 0x80000000U && tag.sid == -1 && tag.type == 0xfedcba98U &&
              tag.number == 0x89abcdefU && !tag.cache);
        CHECK(!memcmp(saved, buffer + 1, 61));
        CHECK(f.allocs == before);
        f.fail = 1;
        CHECK(!mm_scene_sound_decode(buffer + 1, 61, swapped, &f.s));
        f.fail = 0;
        CHECK(!memcmp(buffer + 1, canonical, 56) && buffer[0] == 0xee && buffer[62] == 0xee);
        if (swapped)
            swap_words(buffer + 1, 56);
        copy = mm_scene_sound_decode(buffer + 1, 61, swapped, &f.s);
        CHECK(copy && !memcmp(copy, p, 16));
        for (i = 0; i < 2; ++i)
            CHECK(!at(copy, i)->tag.cache && at(copy, i)->tag.sid == at(p, i)->tag.sid &&
                  at(copy, i)->child == at(p, i)->child);
        encoded = (unsigned char *)mm_scene_sound_encode(copy, &output, &f.s);
        CHECK(encoded && output == 56 && !memcmp(encoded, wire, 56));
        raw_free(&f, encoded);
        raw_free(&f, copy);
        free(buffer);
    }
    for (i = 0; i < 3; ++i)
    {
        memcpy(saved, wire, 56);
        store(saved + 12, i == 0 ? 0 : i == 1 ? 0xffffffffU : 0x7fffffffU);
        before = f.allocs;
        CHECK(!mm_scene_sound_decode(saved, 56, 0, &f.s) && f.allocs == before);
    }
    CHECK(!mm_scene_sound_decode(NULL, 56, 0, &f.s));
    p->ctagc = -1;
    output = 99;
    CHECK(!mm_scene_sound_encode(p, &output, &f.s) && !output);
    CHECK(!mm_scene_sound_duplicate(p, &f.s) && !mm_scene_sound_append(p, f.input, &f.s));
    raw_free(&f, wire);
    raw_free(&f, p);
    clean(&f);
}
static void playback(void)
{
    Fixture f;
    MMSceneSound *p;
    int i;
    static const int normal[] = {11, 21, 31, 41, 51, 22, 32, 42, 52, 13, 23, 33, 43, 53};
    static const int skipped[] = {11, 22, 32, 42, 52, 13, 23, 33, 43, 53};
    init(&f);
    p = f.packet = make(&f, 3);
    f.enqueue_fail = 1;
    mm_scene_sound_play(p, 0xffffffffU, &f.s);
    CHECK(f.used == (int)(sizeof(normal) / sizeof(*normal)) && !memcmp(f.ops, normal, sizeof(normal)) && f.plays == 3 &&
          !f.resources);
    CHECK(f.starts[1] == 0xffffffffU && !f.starts[2] && !f.starts[3] && !f.queues[1] && f.queues[2] && f.queues[3]);
    for (i = 1; i < 4; ++i)
        CHECK(f.loop_tools[i] == 1 && f.loops[i] == 1 && f.volumes[i] == 80);
    f.used = f.plays = 0;
    p->fLoop = 0;
    f.resolve_fail = 1 << 1;
    f.mutate = 1;
    mm_scene_sound_play(p, 99, &f.s);
    CHECK(f.used == (int)(sizeof(skipped) / sizeof(*skipped)) && !memcmp(f.ops, skipped, sizeof(skipped)) &&
          f.plays == 2);
    CHECK(f.queues[2] && !f.starts[2] && f.loops[2] == 0 && f.loops[3] == 1 && f.volumes[3] == 33 &&
          f.loop_tools[3] == 0);
    f.used = f.plays = 0;
    f.resolve_fail = 0;
    f.fetch_fail = 2;
    f.mutate = 0;
    mm_scene_sound_play(p, 99, &f.s);
    CHECK(f.plays == 1 && !f.resources && f.ops[f.used - 1] == 22);
    f.used = f.plays = 0;
    f.fetch_fail = 0;
    f.resolve_fail = (1 << 1) | (1 << 3);
    mm_scene_sound_play(p, 99, &f.s);
    CHECK(f.plays == 1 && f.queues[2] && !f.starts[2]);
    f.used = f.plays = 0;
    f.resolve_fail = 0;
    f.shrink = 1;
    mm_scene_sound_play(p, 99, &f.s);
    CHECK(f.plays == 1 && p->ctagc == 1);
    p->ctagc = 0;
    f.used = 0;
    mm_scene_sound_play(p, 99, &f.s);
    CHECK(!f.used);
    p->ctagc = 3;
    drop(&f, p);
    clean(&f);
}
int main(void)
{
    factories();
    groups();
    codecs();
    playback();
    puts("C89 Scene sound packets: ownership, relocation/faults, wire codecs and indexed playback passed.");
    return 0;
}
