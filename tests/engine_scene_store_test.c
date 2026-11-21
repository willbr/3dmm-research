#include "mm/scene_store.h"
#include "mm/background_resource.h"
#include "mm/scene_data.h"
#include "mm/movie_sound.h"
#include "mm/text_store.h"
#include "chunk_edit_fixture.h"
#include "scene_sound_fixture.h"

#define THUMBNAIL_TYPE 0x5448554dUL
enum { SCENE_NUMBER = 42, THUMB_NUMBER = 55, FRAMES_NUMBER = 61, STARTS_NUMBER = 62 };

typedef struct StoreEvent
{
    unsigned char fixed[8];
    int bytes;
    unsigned char payload[192];
} StoreEvent;

typedef struct StoreGroup
{
    int count, type, live;
    StoreEvent events[8];
} StoreGroup;

typedef struct StoredChunk
{
    MMU32 type, length;
    int linked;
    unsigned char bytes[512];
} StoredChunk;

typedef struct StoreFixture
{
    Fixture packets;
    MMSceneStoreServices s;
    MMSceneCollectServices collect;
    MMSceneOwner owner;
    MMMovieArchiveEditor editor;
    ChunkIndexFixture asset_index;
    MMSceneSound *sounds[2];
    StoreGroup wire[2];
    StoredChunk chunks[96];
    StoredChunk *output;
    int attempts, fail, deleted, thumb_deleted, with_thumb, thumb, updated, old_thumb;
    int storage_live, exercise_storage, unassigned_scene, titled, live;
    int diagnostics, missing_background, empty_events, actor_error;
    int missing_actor, missing_frames, list_failure, background_adoption_failed;
    int log[512], used, adoptions, objects, swap;
    int copy_local_sound, fail_sound_copy;
    MMU32 actor_id, text_id;
    MMTagList tags;
} StoreFixture;

static void note(StoreFixture *f, int n)
{
    CHECK(f->used < 512);
    f->log[f->used++] = n;
}

static int step(StoreFixture *f, int n)
{
    note(f, n);
    return ++f->attempts != f->fail;
}

static void *resize_track(void *p, void *bytes, MMU32 old_size, MMU32 size, int priority)
{
    StoreFixture *f = p;
    int fresh = !bytes;
    void *result;
    (void)old_size;
    if (f->exercise_storage && (priority == MM_STORAGE_SPEED || !step(f, 20)))
        return NULL;
    result = realloc(bytes, (size_t)size);
    CHECK(result);
    f->storage_live += fresh;
    return result;
}

static void release_track(void *p, void *bytes, MMU32 size)
{
    StoreFixture *f = p;
    (void)size;
    CHECK(f->storage_live > 0);
    --f->storage_live;
    free(bytes);
}

static void free_sound_bytes(void *p, void *value)
{
    StoreFixture *f = p;
    note(f, 30);
    raw_free(&f->packets, value);
}

static void *packet_allocate(void *p, MMI32 bytes)
{
    StoreFixture *f = p;
    return step(f, 31) ? allocate(&f->packets, bytes) : NULL;
}

static void release_packet(void *p, MMSceneSound *sound)
{
    StoreFixture *f = p;
    note(f, 32);
    drop(&f->packets, sound);
}


static int write_bytes(void *p, MMU32 offset, const void *data, size_t size)
{
    StoreFixture *f = p;
    StoredChunk *chunk = f->output;
    CHECK(chunk && !chunk->linked && offset <= chunk->length && size <= chunk->length - offset);
    if (!step(f, chunk->type == THUMBNAIL_TYPE ? 84 : 81))
        return 0;
    memcpy(chunk->bytes + offset, data, size);
    return 1;
}

static int add_chunk(void *p, MMU32 type, MMU32 size, MMU32 *number, MMStorageWriter *writer)
{
    StoreFixture *f = p;
    MMU32 assigned;
    int effect;
    if (type == MM_SCENE_CHUNK)
    {
        CHECK(!size && !writer && *number == 0xffffffffUL);
        if (f->unassigned_scene)
            return 0;
        assigned = SCENE_NUMBER;
        effect = 1;
    }
    else if (type == MM_SCENE_ACTOR_CHUNK || type == MM_TEXT_BOX)
    {
        CHECK(!size && !writer);
        assigned = type == MM_SCENE_ACTOR_CHUNK ? 80 + f->actor_id++ : 88 + f->text_id++;
        effect = 71;
    }
    else if (type == THUMBNAIL_TYPE)
    {
        CHECK(size == 65 && writer && f->updated);
        assigned = THUMB_NUMBER;
        effect = 83;
    }
    else
    {
        CHECK((type == MM_SCENE_FRAME_GROUP || type == MM_SCENE_START_GROUP) && writer);
        assigned = type == MM_SCENE_FRAME_GROUP ? FRAMES_NUMBER : STARTS_NUMBER;
        effect = 2;
    }
    f->output = &f->chunks[assigned];
    CHECK(!f->output->type && size <= sizeof(f->output->bytes));
    if (!step(f, effect))
        return 0;
    *number = assigned;
    f->output->type = type;
    f->output->length = size;
    fixture_chunk_add(&f->asset_index, type, assigned);
    if (type == MM_SCENE_CHUNK)
    {
        const MMChunkChild actor = {MM_SCENE_ACTOR_CHUNK, 77, 1}, sound = {MM_SOUND_CHUNK, 1, 3};
        int (*observe)(void *, MMU32, const MMChunkChild *) = f->asset_index.before_insert;
        /* Seed links normally supplied by the archive, before observing edits. */
        f->asset_index.before_insert = NULL;
        CHECK(mm_chunk_adopt(&f->editor.edit, type, assigned, &actor, 0) == MM_CHUNK_OK);
        CHECK(mm_chunk_adopt(&f->editor.edit, type, assigned, &sound, 0) == MM_CHUNK_OK);
        f->asset_index.before_insert = observe;
    }
    if (writer)
    {
        writer->context = f;
        writer->length = size;
        writer->write_at = write_bytes;
    }
    return 1;
}

static int insert_asset(void *p, MMU32 scene, const MMChunkChild *child)
{
    StoreFixture *f = p;
    StoredChunk *chunk = &f->chunks[child->number];
    CHECK(scene == SCENE_NUMBER);
    if (child->type == MM_BACKGROUND)
    {
        CHECK(child->number == 19 && child->slot == 0);
        f->background_adoption_failed = !step(f, 79);
        return !f->background_adoption_failed;
    }
    if (child->type == MM_SOUND_CHUNK)
    {
        CHECK(child->number == (f->copy_local_sound ? 47U : 1U) && child->slot == 1 && !f->adoptions);
        if (!step(f, 70))
            return 0;
        ++f->adoptions;
        return 1;
    }
    CHECK(chunk->type == child->type && !chunk->linked);
    if (child->type == MM_SCENE_ACTOR_CHUNK || child->type == MM_TEXT_BOX)
        CHECK(child->slot == child->number - (child->type == MM_SCENE_ACTOR_CHUNK ? 80 : 88));
    else
        CHECK(child->slot == (MMU32)(child->type == MM_SCENE_START_GROUP));
    if (!step(f, child->type == THUMBNAIL_TYPE ? 85 : 74))
        return 0;
    chunk->linked = 1;
    return 1;
}

static int write_object(void *p, void *value, int kind, MMU32 number, MMU32 scene)
{
    StoreFixture *f = p;
    CHECK(value == f && scene == SCENE_NUMBER && f->chunks[number].linked);
    CHECK(f->chunks[number].type == (MMU32)(kind == MM_SCENE_ADD_ACTOR ? MM_SCENE_ACTOR_CHUNK : MM_TEXT_BOX));
    ++f->objects;
    return step(f, 75 + kind);
}

static int copy_background(void *p, const MMTag *tag, MMU32 *number, int *created)
{
    StoreFixture *f = p;
    CHECK(tag->sid == 0 && tag->number == 9 && tag->cache == &f->packets && tag->type == MM_BACKGROUND);
    *number = 19;
    *created = 1;
    if (!step(f, 80))
        return 0;
    fixture_chunk_add(&f->asset_index, tag->type, *number);
    return 1;
}

static int copy_sound(void *p, const MMTag *tag, MMU32 *number, int *created)
{
    StoreFixture *f = p;
    CHECK(tag->sid == 0 && tag->type == MM_SOUND_CHUNK && tag->cache == &f->packets);
    if (tag->number == 4)
        return MM_CHUNK_NOT_FOUND;
    CHECK(tag->number == 1);
    if (f->fail_sound_copy)
        return MM_CHUNK_IO;
    *number = 47;
    *created = 1;
    fixture_chunk_add(&f->asset_index, tag->type, *number);
    return MM_CHUNK_OK;
}

static void erase_asset(void *p, const MMChunkEntry *entry)
{
    StoreFixture *f = p;
    StoredChunk *chunk = &f->chunks[entry->number];
    if (entry->type == MM_BACKGROUND)
    {
        CHECK(entry->number == 19 && !entry->parent_count);
        if (f->background_adoption_failed)
            note(f, 78);
    }
    else if (entry->type == MM_SCENE_CHUNK)
    {
        CHECK(entry->number == SCENE_NUMBER);
        ++f->deleted;
        note(f, 90);
    }
    else if (entry->type == THUMBNAIL_TYPE && !chunk->linked)
    {
        ++f->thumb_deleted;
        note(f, 86);
    }
    chunk->type = chunk->linked = 0;
}

static void update_thumbnail(void *p)
{
    StoreFixture *f = p;
    CHECK(f->chunks[FRAMES_NUMBER].linked && f->chunks[STARTS_NUMBER].linked && !f->updated);
    f->updated = 1;
    note(f, 82);
    f->owner.owned[MM_SCENE_THUMBNAIL] = f->with_thumb ? &f->thumb : NULL;
}

static MMI32 thumbnail_size(void *p, void *thumbnail)
{
    StoreFixture *f = p;
    CHECK(thumbnail == &f->thumb && f->updated);
    return 65;
}

static int write_thumbnail(void *p, void *thumbnail, const MMStorageWriter *writer)
{
    StoreFixture *f = p;
    unsigned char bytes[65] = {0};
    CHECK(thumbnail == &f->thumb && writer->length == sizeof(bytes));
    return writer->write_at(writer->context, 0, bytes, sizeof(bytes));
}

static int put_scene(void *p, MMU32 type, MMU32 scene, const void *data, MMU32 size)
{
    StoreFixture *f = p;
    MMMemoryStream stream;
    MMSceneState state;
    CHECK(type == MM_SCENE_CHUNK && scene == SCENE_NUMBER && f->updated);
    CHECK(f->chunks[THUMB_NUMBER].linked == f->with_thumb);
    CHECK(mm_scene_init(&state, 1) && mm_memory_stream_init(&stream, data, size));
    CHECK(mm_scene_decode_header(&stream.stream, &state) && state.firstFrame == 2 && state.lastFrame == 9);
    return step(f, 88);
}

static int set_title(void *p, MMU32 type, MMU32 number, const MMMovieTitle *title)
{
    StoreFixture *f = p;
    CHECK(type == MM_SCENE_CHUNK && number == SCENE_NUMBER &&
          title->count == 5 && !memcmp(mm_movie_title_text(title), "Scene", 5));
    f->titled = step(f, 89);
    return f->titled;
}

static void diagnostic(void *p, int start)
{
    StoreFixture *f = p;
    (void)start;
    ++f->diagnostics;
}

static void add_event(MMSceneTrack *track, int kind, const void *value, MMI32 bytes)
{
    MMSceneTrackEvent event;
    event.frame = 3;
    event.kind = kind;
    CHECK(mm_scene_track_insert(track, track->rows.count, bytes, value, &event));
}

static void init_store(StoreFixture *f)
{
    MMSceneStoreServices *s = &f->s;
    MMStorageAllocator allocator = {NULL, resize_track, release_track};
    MMSceneTrack *frames = &f->owner.tracks[MM_SCENE_FRAMES];
    MMSceneTrack *starts = &f->owner.tracks[MM_SCENE_STARTS];
    MMI32 pause[] = {3, 230}, camera = 0x12345678;
    const unsigned short one = 1;
    const MMChunkChild bank_sound = {MM_SOUND_CHUNK, 1, 0};
    MMTag tag;
    void *ptr = f;
    int i;
    memset(f, 0, sizeof(*f));
    init(&f->packets);
    allocator.context = f;
    CHECK(mm_scene_track_init(frames, &allocator, 0) && mm_scene_track_init(starts, &allocator, 0));
    f->owner.owned[MM_SCENE_THUMBNAIL] = &f->old_thumb;
    f->with_thumb = 1;
    f->packets.input[3].tag.sid = 0;
    for (i = 0; i < 2; ++i)
    {
        f->sounds[i] = make(&f->packets, 4);
        add_event(frames, MM_SCENE_PLAY_SOUND, f->sounds[i], mm_scene_sound_size(4));
    }
    add_event(frames, MM_SCENE_CHANGE_CAMERA, &camera, 4);
    add_event(frames, MM_SCENE_PAUSE, pause, 8);
    for (i = 0; i < 2; ++i)
    {
        add_event(starts, MM_SCENE_ADD_ACTOR, &ptr, sizeof(ptr));
        add_event(starts, MM_SCENE_ADD_TEXT, &ptr, sizeof(ptr));
    }
    tag.sid = 0;
    tag.cache = &f->packets;
    tag.type = MM_BACKGROUND;
    tag.number = 9;
    add_event(starts, MM_SCENE_SET_BACKGROUND, &tag, sizeof(tag));
    add_event(starts, MM_SCENE_CHANGE_CAMERA, &camera, 4);

    f->editor.context = f;
    f->editor.text_unit = 1;
    f->editor.system = 0x0303;
    f->editor.big_endian = !*(const unsigned char *)&one;
    f->editor.add = add_chunk;
    f->editor.edit = fixture_chunk_edit(&f->asset_index);
    f->editor.put = put_scene;
    f->editor.set_title = set_title;
    CHECK(mm_scene_init(&f->owner.data, 1) && mm_movie_title_set(&f->owner.data.name, "Scene", 5));
    f->owner.data.firstFrame = 2;
    f->owner.data.lastFrame = 9;

    s->context = f;
    s->owner = &f->owner;
    s->editor = &f->editor;
    s->values.context = f;
    s->values.packets = f->packets.s;
    s->values.packets.context = f; /* The embedded sound fixture is the first member. */
    s->values.packets.allocate = packet_allocate;
    s->values.release = release_packet;
    s->free_sound_bytes = free_sound_bytes;
    s->write_object = write_object;
    s->assets.context = f;
    s->assets.copy_reference = copy_background;
    s->assets.edit = fixture_chunk_edit(&f->asset_index);
    fixture_chunk_add(&f->asset_index, MM_SCENE_ACTOR_CHUNK, 77);
    fixture_chunk_add(&f->asset_index, MM_SOUND_MOVIE, 0);
    fixture_chunk_add(&f->asset_index, MM_SOUND_CHUNK, 1);
    CHECK(mm_chunk_adopt(&f->editor.edit, MM_SOUND_MOVIE, 0, &bank_sound, 1) == MM_CHUNK_OK);
    f->asset_index.observer = f;
    f->asset_index.before_insert = insert_asset;
    f->asset_index.before_delete = erase_asset;
    s->update_thumbnail = update_thumbnail;
    s->thumbnail_size = thumbnail_size;
    s->write_thumbnail = write_thumbnail;
    s->diagnostic = diagnostic;
    f->exercise_storage = 1;
}

static void clean_store(StoreFixture *f)
{
    MMTag background;
    const MMSceneTrack *starts = &f->owner.tracks[MM_SCENE_STARTS];
    int i;
    CHECK(f->storage_live == 4);
    CHECK(f->packets.refs[1] == 2 && f->packets.refs[3] == 2 && f->packets.refs[4] == 2);
    memcpy(&background, mm_scene_track_payload(starts, 4, NULL), sizeof(background));
    CHECK(background.number == 9 && background.cache == &f->packets);
    for (i = 0; i < 2; ++i)
    {
        drop(&f->packets, f->sounds[i]);
        mm_scene_track_dispose(&f->owner.tracks[i]);
    }
    CHECK(!f->storage_live);
    clean(&f->packets);
}

static void check_group(const StoredChunk *chunk, const MMI32 *kinds, const MMI32 *sizes, int count, MMU32 sound)
{
    MMMemoryStream stream;
    MMCollectionHeader header;
    MMU32 offset, value, i;
    MMI32 bytes, kind, frame;
    const unsigned char *rows, *data;
    CHECK(mm_memory_stream_init(&stream, chunk->bytes, chunk->length));
    CHECK(mm_collection_header_read(&stream.stream, MM_COLLECTION_GROUP, 0, &header) == MM_STORAGE_OK);
    CHECK(header.count == (MMU32)count && header.width == MM_SCENE_EVENT_BYTES);
    rows = chunk->bytes + header.header_bytes;
    for (i = 0; i < header.count; ++i)
    {
        CHECK(mm_collection_group_row(&header, rows, i, &offset, &bytes) && bytes == sizes[i]);
        memcpy(&frame, rows + offset, 4);
        memcpy(&kind, rows + offset + 4, 4);
        CHECK(frame == 3 && kind == kinds[i]);
        data = rows + offset + MM_SCENE_EVENT_BYTES;
        if (kind == MM_SCENE_ADD_ACTOR || kind == MM_SCENE_ADD_TEXT)
        {
            memcpy(&value, data, 4);
            CHECK(value == i / 2);
        }
        if (kind == MM_SCENE_SET_BACKGROUND)
        {
            memcpy(&value, data + 4, 4);
            CHECK(!value);
            memcpy(&value, data + 12, 4);
            CHECK(value == 19);
        }
        if (kind == MM_SCENE_PLAY_SOUND)
        {
            MMSceneSoundWire wire;
            MMTag tag;
            MMU32 slot;
            CHECK(mm_scene_sound_wire(data, bytes, header.big_endian, &wire));
            mm_scene_sound_wire_tag(&wire, 0, &slot, &tag);
            CHECK(slot == 1 && tag.number == sound && !tag.cache);
            mm_scene_sound_wire_tag(&wire, 2, &slot, &tag);
            CHECK(slot == 3 && tag.number == 1 && !tag.cache);
            mm_scene_sound_wire_tag(&wire, 3, &slot, &tag);
            CHECK(slot == 4 && tag.number == 4 && !tag.cache);
        }
    }
}

static void test_store(void)
{
    const MMI32 frames[] = {MM_SCENE_PLAY_SOUND, MM_SCENE_PLAY_SOUND, MM_SCENE_CHANGE_CAMERA, MM_SCENE_PAUSE};
    const MMI32 starts[] = {MM_SCENE_ADD_ACTOR, MM_SCENE_ADD_TEXT, MM_SCENE_ADD_ACTOR, MM_SCENE_ADD_TEXT,
                            MM_SCENE_SET_BACKGROUND, MM_SCENE_CHANGE_CAMERA};
    const MMI32 frame_sizes[] = {96, 96, 4, 8}, start_sizes[] = {4, 4, 4, 4, 16, 4};
    StoreFixture f;
    MMChunkAccess chunks;
    MMChunkEntry entry;
    MMU32 number, row;
    int attempts, fail, j;
    init_store(&f);
    CHECK(mm_scene_store(&number, &f.s) && number == SCENE_NUMBER && !f.deleted && f.titled);
    CHECK(f.objects == 4 && f.actor_id == 2 && f.text_id == 2 && f.adoptions == 1);
    chunks = fixture_chunk_access(&f.asset_index);
    CHECK(chunks.count == 12 && mm_chunk_find(&chunks, MM_BACKGROUND, 19, &row) == MM_CHUNK_OK &&
          mm_chunk_at(&chunks, row, &entry) == MM_CHUNK_OK && entry.parent_count == 1 && !(entry.flags & MM_CHUNK_LONER));
    check_group(&f.chunks[FRAMES_NUMBER], frames, frame_sizes, 4, 1);
    check_group(&f.chunks[STARTS_NUMBER], starts, start_sizes, 6, 0);
    attempts = f.attempts;
    clean_store(&f);
    for (fail = 1; fail <= attempts; ++fail)
    {
        init_store(&f);
        f.fail = fail;
        CHECK(!mm_scene_store(&number, &f.s) && f.asset_index.count == 3);
        CHECK(fail == 1 ? number == 0xffffffffUL && !f.deleted
                        : number == SCENE_NUMBER && f.deleted == 1 && f.log[f.used - 1] == 90);
        chunks = fixture_chunk_access(&f.asset_index);
        CHECK(mm_chunk_find(&chunks, MM_SOUND_CHUNK, 1, &row) == MM_CHUNK_OK &&
              mm_chunk_at(&chunks, row, &entry) == MM_CHUNK_OK && entry.parent_count == 1);
        for (j = 0; j < (int)(sizeof(f.chunks) / sizeof(f.chunks[0])); ++j)
            CHECK(!f.chunks[j].type && !f.chunks[j].linked);
        if (f.thumb_deleted)
            CHECK(f.log[f.used - 2] == 86);
        if (f.background_adoption_failed)
            CHECK(f.used > 2 && f.log[f.used - 3] == 79 && f.log[f.used - 2] == 78);
        clean_store(&f);
    }
    init_store(&f);
    f.with_thumb = 0;
    CHECK(mm_scene_store(&number, &f.s) && f.updated && f.titled);
    clean_store(&f);
    init_store(&f);
    f.unassigned_scene = 1;
    CHECK(!mm_scene_store(&number, &f.s) && number == 0xffffffffUL && !f.deleted);
    clean_store(&f);
    for (fail = 0; fail < 2; ++fail)
    {
        MMSceneSoundTag live;
        init_store(&f);
        f.copy_local_sound = 1;
        f.fail_sound_copy = fail;
        f.s.assets.copy_sound = copy_sound;
        CHECK(mm_scene_store(&number, &f.s) == !fail);
        chunks = fixture_chunk_access(&f.asset_index);
        CHECK(mm_chunk_find(&chunks, MM_SOUND_CHUNK, 47, &row) == (fail ? MM_CHUNK_NOT_FOUND : MM_CHUNK_OK));
        if (!fail)
        {
            CHECK(mm_chunk_at(&chunks, row, &entry) == MM_CHUNK_OK && entry.parent_count == 1);
            check_group(&f.chunks[FRAMES_NUMBER], frames, frame_sizes, 4, 47);
        }
        mm_scene_sound_get_tag(f.sounds[0], 0, &live);
        CHECK(live.tag.number == 1 && live.tag.cache == &f.packets);
        clean_store(&f);
    }
}

static void wire_word(void *p, MMU32 value, int swap)
{
    unsigned char b[4];
    int i;
    memcpy(b, &value, 4);
    for (i = 0; i < 4; ++i)
        ((unsigned char *)p)[i] = b[swap ? 3 - i : i];
}
static void wire_event(StoreFixture *f, int start, int kind, const MMU32 *words, int n)
{
    StoreGroup *g = &f->wire[start];
    StoreEvent *e = &g->events[g->count++];
    int i;
    e->bytes = n * 4;
    wire_word(e->fixed, 3, f->swap);
    wire_word(e->fixed + 4, (MMU32)kind, f->swap);
    for (i = 0; i < n; ++i)
        wire_word(e->payload + i * 4, words[i], f->swap);
}
static int collect_entry(void *p, MMU32 row, MMChunkEntry *entry)
{
    StoreFixture *f = (StoreFixture *)p;
    memset(entry, 0, sizeof(*entry));
    if (row == 0)
    {
        entry->type = MM_SCENE_ACTOR_CHUNK;
        entry->number = 77;
        entry->child_count = 1;
    }
    else if (row == 1)
    {
        entry->type = 0x47474145UL;
        entry->number = 37;
    }
    else
    {
        CHECK(row == 2);
        entry->type = MM_SCENE_CHUNK;
        entry->number = 42;
        entry->child_count = !f->missing_actor;
    }
    return 1;
}
static int collect_link(void *p, MMU32 row, MMU32 i, MMChunkChild *out)
{
    (void)p;
    CHECK((row == 0 || row == 2) && i == 0);
    out->type = row == 0 ? 0x47474145UL : MM_SCENE_ACTOR_CHUNK;
    out->number = row == 0 ? 37 : 77;
    out->slot = row == 0 ? 0 : 7;
    return 1;
}
static void collect_access(void *p, MMChunkAccess *out)
{
    MMChunkAccess chunks = {NULL, 3, collect_entry, collect_link};
    chunks.context = p;
    *out = chunks;
}
static int actor_payload(void *p, const MMChunkEntry *entry, MMMovieReceiveBytes receive, void *out)
{
    StoreFixture *f = (StoreFixture *)p;
    unsigned char bytes[76] = {0};
    MMMemoryStream memory;
    size_t size;
    const unsigned short marker = 1;
    const unsigned char *bo = (const unsigned char *)&marker;
    bytes[0] = bo[f->swap];
    bytes[1] = bo[!f->swap];
    if (entry->type == MM_SCENE_ACTOR_CHUNK)
    {
        CHECK(entry->number == 77);
        size = MM_ACTOR_HEADER_BYTES;
        wire_word(bytes + 28, 4, f->swap);
        wire_word(bytes + 36, 0x544d504cUL, f->swap);
        wire_word(bytes + 40, 65, f->swap);
    }
    else
    {
        CHECK(entry->type == 0x47474145UL && entry->number == 37);
        if (f->actor_error || !step(f, 7))
            return MM_MOVIE_IO;
        size = f->empty_events ? 20 : sizeof(bytes);
        wire_word(bytes + 4, !f->empty_events, f->swap);
        wire_word(bytes + 8, f->empty_events ? 0 : 48, f->swap);
        wire_word(bytes + 12, 0xffffffffUL, f->swap);
        wire_word(bytes + 16, MM_ACTOR_EVENT_BYTES, f->swap);
        wire_word(bytes + 20, MM_ACTOR_COSTUME, f->swap);
        wire_word(bytes + 52, 8, f->swap);
        wire_word(bytes + 60, 0x4d54524cUL, f->swap);
        wire_word(bytes + 64, 9, f->swap);
        wire_word(bytes + 72, 48, f->swap);
    }
    CHECK(mm_memory_stream_init(&memory, bytes, size));
    return receive(out, &memory.stream);
}
static int read_group(void *p, MMU32 scene, int start, MMMovieSceneGroup *out)
{
    StoreFixture *f = (StoreFixture *)p;
    StoreGroup *g = &f->wire[start];
    CHECK(scene == 42);
    if (!start && f->missing_frames)
        return MM_MOVIE_ASSET_NOT_FOUND;
    if (!step(f, 4 + start))
        return MM_MOVIE_IO;
    CHECK(!g->live);
    g->live = 1;
    ++f->live;
    out->handle = g;
    out->count = g->count;
    return MM_MOVIE_OK;
}
static void read_event(void *p, const MMMovieSceneGroup *group, MMU32 i, MMMovieSceneEvent *out)
{
    const unsigned short one = 1;
    StoreFixture *f = (StoreFixture *)p;
    StoreGroup *g = (StoreGroup *)group->handle;
    StoreEvent *e = &g->events[i];
    CHECK(g->live && i < group->count);
    out->big_endian = (*(const unsigned char *)&one == 0) != f->swap;
    out->data = e->fixed;
    out->frame = (MMI32)mm_movie_scene_event_word(out, 0);
    out->kind = (MMI32)mm_movie_scene_event_word(out, 4);
    out->bytes = e->bytes;
    out->data = e->payload;
}
static void close_group(void *p, MMMovieSceneGroup *group)
{
    StoreFixture *f = p;
    StoreGroup *events = group->handle;
    CHECK(events && events->live);
    events->live = 0;
    --f->live;
    note(f, 100 + events->type);
}
static void *collect_resize(void *p, void *old, MMU32 old_bytes, MMU32 bytes, int priority)
{
    StoreFixture *f = (StoreFixture *)p;
    (void)old_bytes;
    (void)priority;
    if (!f->list_failure)
        f->list_failure = !step(f, 8);
    return f->list_failure ? NULL : realloc(old, bytes);
}
static void collect_free(void *p, void *memory, MMU32 bytes)
{
    (void)p;
    (void)bytes;
    free(memory);
}
static void collect_diagnostic(void *p, int start, int missing)
{
    StoreFixture *f = (StoreFixture *)p;
    (void)start;
    ++f->diagnostics;
    f->missing_background += missing;
}
static void init_collect(StoreFixture *f, int swap)
{
    MMSceneCollectServices *s;
    MMStorageAllocator allocator = {NULL, collect_resize, collect_free};
    MMU32 child[] = {7}, bg[] = {1, 999, MM_BACKGROUND, 9}, camera[] = {0x12345678U},
               sound[] = {80, 2, 0, 2, 1, 0, 999, 23, 1, 2, 3, 999, 23, 2}, pause[] = {3, 230};
    memset(f, 0, sizeof(*f));
    init(&f->packets);
    allocator.context = f;
    CHECK(mm_tag_list_init(&f->tags, &allocator) == MM_STORAGE_OK);
    f->swap = swap;
    s = &f->collect;
    s->context = f;
    s->read.context = f;
    s->read.group = read_group;
    s->read.event = read_event;
    s->read.close = close_group;
    s->graph.context = f;
    s->graph.access = collect_access;
    s->graph.payload = actor_payload;
    s->diagnostic = collect_diagnostic;
    f->wire[0].type = 20;
    f->wire[1].type = 21;
    wire_event(f, 1, 0, child, 1);
    wire_event(f, 1, 4, bg, 4);
    bg[3] = 19;
    wire_event(f, 1, 4, bg, 4);
    wire_event(f, 1, 2, child, 1);
    wire_event(f, 0, 3, camera, 1);
    wire_event(f, 0, 1, sound, 14);
    wire_event(f, 0, 5, pause, 2);
}
static void clean_collect(StoreFixture *f)
{
    CHECK(!f->live && mm_tag_list_dispose(&f->tags) == MM_STORAGE_OK);
    clean(&f->packets);
}
static void check_collected(StoreFixture *f)
{
    MMGroup group = {NULL, sizeof(MMTagCached), 0, 0, -1, 0};
    static const MMU32 numbers[] = {1, 9, 19, 2, 65, 9};
    static const MMI32 sources[] = {0, 1, 1, 3, 4, 8};
    MMGroupLocation loc;
    MMTagCached tag;
    MMTagChild child;
    MMU32 i, j, count;
    int camera;
    group.storage = &f->tags.storage;
    group.count = f->tags.count;
    group.bytes = f->tags.bytes;
    CHECK(f->tags.count == 6);
    for (i = 0; i < 6; ++i)
    {
        CHECK(mm_tag_list_get(&f->tags, i, &tag) == MM_STORAGE_OK);
        CHECK(tag.tag.number == numbers[i] && tag.tag.sid == sources[i] && !tag.tag.cache);
        CHECK(tag.children == (i != 1 && i != 2));
        CHECK(mm_group_location(&group, i, &loc) == MM_STORAGE_OK);
        count = (loc.bytes - sizeof(tag)) / sizeof(child);
        CHECK(count == (i == 1 ? 4U : i == 2 ? 5U : 0U));
        camera = 0;
        for (j = 0; j < count; ++j)
        {
            memcpy(&child, group.storage->buffers[0].data + loc.offset + sizeof(tag) + j * sizeof(child), sizeof(child));
            if (child.slot == 0x12345678U && child.type == MM_BACKGROUND_CAMERA)
                ++camera;
        }
        CHECK(camera == (i == 2));
    }
}
static void test_collect(void)
{
    StoreFixture f;
    int attempts, fail, swap;
    MMU32 child = 7;
    MMGroup group = {NULL, sizeof(MMTagCached), 0, 0, -1, 0};
    MMGroupLocation loc;
    MMTagCached tag;
    for (swap = 0; swap < 2; ++swap)
    {
        init_collect(&f, swap);
        f.wire[0].events[1].bytes += 4;
        CHECK(mm_scene_collect_tags(42, &f.tags, &f.collect));
        attempts = f.attempts;
        CHECK(!f.diagnostics);
        check_collected(&f);
        clean_collect(&f);
        init_collect(&f, swap);
        wire_word(f.wire[1].events[2].payload, 0, swap);
        CHECK(mm_scene_collect_tags(42, &f.tags, &f.collect));
        group.storage = &f.tags.storage;
        group.count = f.tags.count;
        group.bytes = f.tags.bytes;
        CHECK(mm_tag_list_get(&f.tags, 1, &tag) == MM_STORAGE_OK &&
              tag.tag.sid == 0 && tag.tag.type == MM_BACKGROUND && tag.tag.number == 19);
        CHECK(mm_group_location(&group, 1, &loc) == MM_STORAGE_OK && loc.bytes == sizeof(tag));
        clean_collect(&f);
        for (fail = 1; fail <= attempts; ++fail)
        {
            init_collect(&f, swap);
            f.fail = fail;
            CHECK(!mm_scene_collect_tags(42, &f.tags, &f.collect));
            CHECK(f.tags.count < 6);
            clean_collect(&f);
        }
        init_collect(&f, swap);
        f.missing_actor = 1;
        CHECK(!mm_scene_collect_tags(42, &f.tags, &f.collect) && !f.tags.count);
        clean_collect(&f);
        init_collect(&f, swap);
        f.missing_frames = 1;
        CHECK(!mm_scene_collect_tags(42, &f.tags, &f.collect) && f.tags.count == 4);
        clean_collect(&f);
        init_collect(&f, swap);
        f.empty_events = 1;
        CHECK(mm_scene_collect_tags(42, &f.tags, &f.collect) && f.tags.count == 5);
        clean_collect(&f);
        init_collect(&f, swap);
        f.actor_error = 1;
        CHECK(!mm_scene_collect_tags(42, &f.tags, &f.collect) && !f.tags.count && f.log[f.used - 1] == 121);
        clean_collect(&f);
        init_collect(&f, swap);
        f.wire[1].count = 1;
        CHECK(!mm_scene_collect_tags(42, &f.tags, &f.collect) && f.missing_background == 1);
        clean_collect(&f);
        init_collect(&f, swap);
        f.wire[1].events[0].bytes = 0;
        CHECK(!mm_scene_collect_tags(42, &f.tags, &f.collect) && !f.tags.count);
        clean_collect(&f);
        init_collect(&f, swap);
        f.wire[1].events[1].bytes = 20;
        CHECK(!mm_scene_collect_tags(42, &f.tags, &f.collect) && f.tags.count == 2);
        clean_collect(&f);
        init_collect(&f, swap);
        f.wire[0].events[0].bytes = 0;
        CHECK(!mm_scene_collect_tags(42, &f.tags, &f.collect) && f.tags.count == 4);
        clean_collect(&f);
        init_collect(&f, swap);
        f.wire[0].events[1].bytes = 16;
        CHECK(!mm_scene_collect_tags(42, &f.tags, &f.collect) && f.tags.count == 4);
        clean_collect(&f);
        init_collect(&f, swap);
        wire_event(&f, 1, 3, &child, 1);
        wire_event(&f, 0, 0, &child, 1);
        CHECK(mm_scene_collect_tags(42, &f.tags, &f.collect) && f.diagnostics == 2);
        clean_collect(&f);
    }
}
int main(void)
{
    test_store();
    test_collect();
    puts("C89 Scene store and collect checks passed");
    return 0;
}
