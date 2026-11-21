/* Composition ownership, partial application, import remapping and C storage. */
#include "mm/movie_compose.h"
#include "mm/actor_data.h"
#include "mm/actor_events.h"
#include "mm/actor_motion.h"
#include "mm/background_resource.h"
#include "mm/movie_rollcall.h"
#include "mm/movie_session.h"
#include "mm/movie_sound.h"
#include "mm/scene_data.h"
#include "engine_resource_block_fixture.h"
#include "movie_scene_archive_fixture.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x)                                                                                                       \
    do                                                                                                                 \
    {                                                                                                                  \
        if (!(x))                                                                                                      \
        {                                                                                                              \
            fprintf(stderr, "line %d: %s\n", __LINE__, #x);                                                            \
            return 1;                                                                                                  \
        }                                                                                                              \
    } while (0)
static void fixture_failure(int line)
{
    fprintf(stderr, "invalid composition fixture at line %d\n", line);
    exit(1);
}

enum
{
    TEMP = 1,
    SAVE,
    CHILD,
    READ_TRANS,
    READ_THUMB,
    THUMB,
    CLONE,
    DELETE_ROOT,
    READ_TABLE,
    APPEND_ACTOR,
    ADJUST,
    ADOPT,
    SOUNDS,
    READ_SCENE,
    START,
    ADD_ACTORS,
    ACTOR_TOOLS,
    TEXT_TOOLS,
    DELETE_CHILD,
    WRITE_TRANS,
    WRITE_TITLE,
    SET_TRANS,
    DIRTY,
    NUKED,
    INVALIDATE,
    ROLL_CHANGED,
    CLOSE_SCENE,
    CLOSE_TAG,
    REBIND_MATERIALS,
    REBIND_BACKGROUND,
    LINK_SOUNDS
};
enum { TEST_MATERIAL = 0x4d54524c, SOURCE_MATERIAL = 31, TARGET_MATERIAL = 41,
       SOURCE_BACKGROUND = 32, TARGET_BACKGROUND = 42 };
typedef struct Test Test;
typedef struct Object
{
    Test *test;
    int id, refs;
} Object;
struct Test
{
    MMMovieComposeServices s;
    MMMovieState data;
    MMMovieScenes scenes;
    MMCompositeMovie composite;
    ChunkIndexFixture archives[2];
    Object resources[3], thumbs[3], open, read, tags[3];
    int fail, failAt, calls[40], events[256], n, badRef;
    int withActors, actorIds[2], adjusted[8], adjustCount;
    int deleted[8], deletedCount, adopted, removedActors, closed, diagnostic[4];
    int memoryCalls, allocFailAt, blocks, openTransition;
    int readingSource, actorBlocks, closedTags[2], tagCloses;
    ResourceTestBlock block;
    MMChunkPayloadServices payloads;
    MMResourceCache cache;
    unsigned char bytes[4096];
    MMU32 size, writingActor;
};
static void note(Test *t, int operation)
{
    t->events[t->n++] = operation;
}
static int fails(Test *t, int operation)
{
    note(t, operation);
    ++t->calls[operation];
    return t->fail == operation && t->calls[operation] == t->failAt;
}
static int position(Test *t, int operation)
{
    int i;
    for (i = 0; i < t->n; ++i)
        if (t->events[i] == operation)
            return i;
    return -1;
}
static void retain(void *p)
{
    Object *o = p;
    note(o->test, 100 + o->id);
    ++o->refs;
}
static void release(void *p)
{
    Object *o = p;
    note(o->test, 200 + o->id);
    if (--o->refs < 0)
        o->test->badRef = 1;
}
static int temporary(void *p)
{
    return !fails(p, TEMP);
}
static int save(void *p)
{
    return !fails(p, SAVE);
}
static void *file(void *p)
{
    return p;
}
static ChunkIndexFixture *archive(void *p)
{
    Object *o = p;
    if (o->id != 1 && o->id != 2)
        fixture_failure(__LINE__);
    return &o->test->archives[o->id - 1];
}
static MMU32 edit_count(void *p)
{
    ChunkIndexFixture *index = p;
    Test *t = index->observer;
    if (t->fail == CHILD && !t->calls[CHILD] && fails(t, CHILD))
        return 0;
    return fixture_chunk_count(p);
}
static MMChunkEdit edit_archive(void *p)
{
    MMChunkEdit edit = fixture_chunk_edit(archive(p));
    edit.count = edit_count;
    return edit;
}
static int allocate_payload(void *p, MMU32 bytes, MMChunkLocation *location)
{
    Test *t = ((ChunkIndexFixture *)p)->observer;
    if (bytes > sizeof(t->bytes) - t->size)
        fixture_failure(__LINE__);
    location->file = t;
    location->offset = t->size;
    location->bytes = bytes;
    location->flags = 0;
    t->size += bytes;
    return 1;
}
static int read_payload(void *p, void *f, MMU32 offset, void *bytes, size_t size)
{
    Test *t = p;
    if (f != p || offset > t->size || size > t->size - offset)
        fixture_failure(__LINE__);
    memcpy(bytes, t->bytes + offset, size);
    return 1;
}
static int write_payload(void *p, void *f, MMU32 offset, const void *bytes, size_t size)
{
    Test *t = p;
    if (f != p || offset > t->size || size > t->size - offset)
        fixture_failure(__LINE__);
    if (size == MM_SCENE_HEADER_BYTES && fails(t, WRITE_TRANS))
        return 0;
    if (t->writingActor)
    {
        MMActorHeader header;
        MMU32 actor = t->writingActor;
        t->writingActor = 0;
        if (size != sizeof(header) || (actor != 100 && actor != 101))
            fixture_failure(__LINE__);
        if (fails(t, ADJUST))
            return 0;
        memcpy(&header, bytes, sizeof(header));
        t->actorIds[actor - 100] = header.actor_id;
        t->adjusted[t->adjustCount++] = (int)actor;
    }
    memcpy(t->bytes + offset, bytes, size);
    return 1;
}
static void release_payload(void *p, void *f)
{
    if (f != p)
        fixture_failure(__LINE__);
    ((Test *)p)->readingSource = 0;
}
static MMArchiveWriter compose_archive(void *file)
{
    MMArchiveWriter writer = {0};
    writer.edit = fixture_chunk_edit(archive(file));
    writer.edit.allocate = allocate_payload;
    writer.payloads = &((Object *)file)->test->payloads;
    writer.system = MM_TEXT_WINDOWS;
    return writer;
}
static MMChunkAccess live_chunks(void *file)
{
    return fixture_chunk_access(archive(file));
}
static void graph_access(void *file, MMChunkAccess *out)
{
    *out = live_chunks(file);
}
static int graph_payload(void *file, const MMChunkEntry *entry, MMMovieReceiveBytes read, void *context)
{
    Test *t = ((Object *)file)->test;
    MMMemoryStream payload;
    if (entry->type != MM_ACTOR_CHUNK || entry->offset > t->size || entry->bytes > t->size - entry->offset)
        return MM_MOVIE_BAD_FILE;
    mm_memory_stream_init(&payload, t->bytes + entry->offset, entry->bytes);
    t->writingActor = entry->number;
    return read(context, &payload.stream);
}
static void compose_graph(void *file, MMMovieSessionGraph *out)
{
    MMMovieSessionGraph graph = {NULL, graph_access, NULL, graph_payload, NULL};
    graph.context = file;
    *out = graph;
}
static MMResourceCache *compose_cache(void *resources)
{
    return &((Object *)resources)->test->cache;
}
static int locate_payload(void *file, MMU32 type, MMU32 number, MMBlockFile *out, int *packed)
{
    Test *t = ((Object *)file)->test;
    const MMChunkAccess chunks = live_chunks(file);
    MMChunkEntry entry;
    MMU32 row;
    if ((type == MM_ROLLCALL_CHUNK && fails(t, READ_TABLE)) ||
        (type == MM_SCENE_CHUNK && fails(t, READ_TRANS)) ||
        mm_chunk_find(&chunks, type, number, &row) != MM_CHUNK_OK ||
        mm_chunk_at(&chunks, row, &entry) != MM_CHUNK_OK)
        return 0;
    out->file = t;
    out->offset = entry.offset;
    out->length = entry.bytes;
    *packed = 0;
    t->readingSource = type == MM_ROLLCALL_CHUNK;
    return 1;
}
static int decode_payload(void *c, void **handle)
{
    (void)c;
    (void)handle;
    return 0; /* No packed extents in this fixture. */
}

static void seed_rollcall(Test *t, MMU32 movie)
{
    MMRollCallTable table;
    MMRollCallActor row;
    const MMArchiveWriter writer = compose_archive(&t->resources[0]);
    char name[144];
    MMU32 written;
    int i;
    if (mm_rollcall_table_init(&table, sizeof(row), 1, MM_TEXT_WINDOWS, 0, NULL) != MM_STORAGE_OK)
        fixture_failure(__LINE__);
    for (i = 0; t->withActors && i < 2; ++i)
    {
        memset(name, 'A' + i, sizeof(name));
        memset(&row, 0, sizeof(row));
        row.data.actor_id = i * 2 + 5;
        row.data.reference_count = 3;
        row.data.template_number = (MMU32)i;
        if (!mm_rollcall_table_add(&table, name, sizeof(name), &row))
            fixture_failure(__LINE__);
    }
    if (!mm_movie_write_rollcall(&writer, movie, &table, &written))
        fixture_failure(__LINE__);
    mm_rollcall_table_dispose(&table);
}
static void seed_actor_chunks(Test *t, MMU32 first_scene, int scenes)
{
    MMActorHeader header = {0};
    const MMArchiveWriter writer = compose_archive(&t->resources[0]);
    const MMChunkChild first = {MM_ACTOR_CHUNK, 100, 0}, nested = {MM_ACTOR_CHUNK, 101, 0};
    int i;
    header.order = 1;
    header.system = MM_TEXT_WINDOWS;
    header.actor_id = t->actorIds[0];
    if (!mm_actor_write_header(first.number, &header, &writer))
        fixture_failure(__LINE__);
    header.actor_id = t->actorIds[1];
    if (!mm_actor_write_header(nested.number, &header, &writer) ||
        mm_chunk_adopt(&writer.edit, first.type, first.number, &nested, 1) != MM_CHUNK_OK)
        fixture_failure(__LINE__);
    for (i = 0; i < scenes; ++i)
        if (mm_chunk_adopt(&writer.edit, MM_SCENE_CHUNK, first_scene + (MMU32)i, &first, 1) != MM_CHUNK_OK)
            fixture_failure(__LINE__);
}
static void seed_scene_header(void *file, MMU32 number, MMI32 transition)
{
    MMSceneState scene;
    MMChunkLocation location;
    ChunkIndexFixture *index = archive(file);
    const MMChunkAccess chunks = live_chunks(file);
    MMU32 row;
    if (mm_chunk_find(&chunks, MM_SCENE_CHUNK, number, &row) != MM_CHUNK_OK ||
        !allocate_payload(index, MM_SCENE_HEADER_BYTES, &location) || !mm_scene_init(&scene, 1))
        fixture_failure(__LINE__);
    scene.transition = transition;
    if (!mm_scene_encode_header(&scene, MM_TEXT_WINDOWS, 0, ((Object *)file)->test->bytes + location.offset))
        fixture_failure(__LINE__);
    index->rows[row].entry.bytes = location.bytes;
    index->rows[row].entry.offset = location.offset;
}
static const unsigned char scene_title[] = {3, 3, 5, 'S', 'c', 'e', 'n', 'e', 0};
static int load_scene_title(void *file, MMU32 type, MMU32 number, void *wire, MMU32 capacity, MMU32 *written)
{
    (void)file; (void)number;
    if (type != MM_SCENE_CHUNK || capacity < sizeof(scene_title))
        fixture_failure(__LINE__);
    memcpy(wire, scene_title, sizeof(scene_title));
    *written = sizeof(scene_title);
    return 1;
}
static int save_scene_title(void *file, MMU32 type, MMU32 number, const void *wire, MMU32 bytes)
{
    (void)number;
    if (type != MM_SCENE_CHUNK || bytes != sizeof(scene_title) || memcmp(wire, scene_title, bytes))
        fixture_failure(__LINE__);
    return !fails(((Object *)file)->test, WRITE_TITLE);
}
static MMI32 stored_transition(void *file, MMU32 scene)
{
    const MMChunkAccess chunks = live_chunks(file);
    MMChunkEntry entry;
    MMMemoryStream stream;
    MMSceneState decoded;
    MMU32 row;
    if (mm_chunk_find(&chunks, MM_SCENE_CHUNK, scene, &row) != MM_CHUNK_OK ||
        mm_chunk_at(&chunks, row, &entry) != MM_CHUNK_OK || !mm_scene_init(&decoded, 1) ||
        !mm_memory_stream_init(&stream, ((Object *)file)->test->bytes + entry.offset, entry.bytes) ||
        !mm_scene_decode_header(&stream.stream, &decoded))
        fixture_failure(__LINE__);
    return decoded.transition;
}
static MMI32 transition(void *p)
{
    return ((Object *)p)->test->openTransition;
}
static void *read_thumbnail(void *p, MMU32 number)
{
    Object *o = p;
    Test *t = o->test;
    (void)number;
    if (fails(t, READ_THUMB))
        return 0;
    ++t->thumbs[0].refs;
    return &t->thumbs[0];
}
static void *thumbnail(void *p)
{
    Test *t = ((Object *)p)->test;
    return fails(t, THUMB) ? NULL : &t->thumbs[1];
}
static void seed_embedded_assets(void *source, void *destination, MMU32 scene)
{
    Test *t = ((Object *)destination)->test;
    const MMArchiveWriter output = compose_archive(destination);
    const MMActorEventServices events = mm_actor_events_services(NULL);
    MMActorEventTable *runtime = mm_actor_events_create(NULL, 0, 0), *wire;
    MMActorEvent event = {0};
    MMActorCostume costume = {0};
    MMMovieArchiveFile binding;
    MMMovieArchiveEditor editor;
    MMStorageWriter writer;
    MMChunkChild track = {MM_ACTOR_EVENTS_CHUNK, 0, 0};
    fixture_chunk_add(archive(source), TEST_MATERIAL, SOURCE_MATERIAL);
    fixture_chunk_add(archive(source), MM_BACKGROUND, SOURCE_BACKGROUND);
    if (!mm_movie_archive_file_init(&binding, destination, &output, &t->s.titles))
        fixture_failure(__LINE__);
    editor = mm_movie_archive_file_editor(&binding, 1, NULL);
    if (!mm_scene_archive_opening_background(&editor, scene, 1, 0, SOURCE_BACKGROUND))
        fixture_failure(__LINE__);
    event.aet = MM_ACTOR_COSTUME;
    costume.tag.type = TEST_MATERIAL;
    costume.tag.number = SOURCE_MATERIAL;
    if (!runtime || !mm_actor_events_insert(runtime, 0, sizeof(costume), &costume, &event))
        fixture_failure(__LINE__);
    wire = mm_actor_write_events(runtime, &events);
    if (!wire || !editor.add(editor.context, track.type, mm_actor_events_wire_size(wire), &track.number, &writer) ||
        !mm_actor_events_write(wire, MM_TEXT_WINDOWS, 0, &writer) ||
        mm_chunk_adopt(&output.edit, MM_ACTOR_CHUNK, 100, &track, 1) != MM_CHUNK_OK ||
        mm_chunk_adopt(&output.edit, MM_ACTOR_CHUNK, 101, &track, 1) != MM_CHUNK_OK)
        fixture_failure(__LINE__);
    mm_actor_events_release(wire);
    mm_actor_events_release(runtime);
    mm_movie_archive_file_clear(&binding);
}
static int clone(void *p, MMU32 number, void *destination, MMU32 *out)
{
    Object *o = p;
    const MMChunkAccess source = fixture_chunk_access(archive(p));
    MMChunkEntry entry;
    MMU32 position, i;
    if (fails(o->test, CLONE))
        return 0;
    if (mm_chunk_find(&source, MM_MOVIE_CHUNK, number, &position) != MM_CHUNK_OK ||
        mm_chunk_at(&source, position, &entry) != MM_CHUNK_OK)
        fixture_failure(__LINE__);
    *out = number + 100;
    fixture_scene_seed_movie(archive(destination), *out, *out * 10, entry.child_count);
    for (i = 0; i < entry.child_count; ++i)
        seed_scene_header(destination, *out * 10 + i, stored_transition(p, number * 10 + i));
    seed_rollcall(o->test, *out);
    if (o->test->withActors)
    {
        seed_actor_chunks(o->test, *out * 10, (int)entry.child_count);
        seed_embedded_assets(p, destination, *out * 10);
    }
    if (o->test->fail == LINK_SOUNDS)
    {
        const MMChunkEdit edit = fixture_chunk_edit(archive(destination));
        const MMChunkChild sound = {MM_SOUND_CHUNK, 0, 0};
        fixture_chunk_add(archive(destination), sound.type, sound.number);
        if (mm_chunk_adopt(&edit, MM_MOVIE_CHUNK, *out, &sound, 1) != MM_CHUNK_OK)
            fixture_failure(__LINE__);
    }
    return 1;
}
static void erase_entry(void *p, const MMChunkEntry *entry)
{
    Test *t = p;
    if (entry->type == MM_MOVIE_CHUNK)
    {
        note(t, DELETE_ROOT);
        t->deleted[t->deletedCount++] = (int)entry->number;
    }
}
static int open_actor(void *p, MMRollCallActor *row, void *resources)
{
    Test *t = p;
    if (resources != &t->resources[0] || !t->readingSource || row->data.template_number >= 2)
        fixture_failure(__LINE__);
    row->cache = &t->tags[row->data.template_number];
    ++t->tags[row->data.template_number].refs;
    return 1;
}
static void *resize_actors(void *p, void *data, MMU32 before, MMU32 size, int priority)
{
    Test *t = p;
    int fresh = !data;
    void *result;
    (void)before;
    (void)priority;
    if (!t->readingSource && t->fail == APPEND_ACTOR && t->data.actors.count == (MMU32)t->failAt)
        return NULL;
    result = realloc(data, size);
    if (result && fresh)
        ++t->actorBlocks;
    return result;
}
static void free_actors(void *p, void *data, MMU32 size)
{
    Test *t = p;
    (void)size;
    --t->actorBlocks;
    free(data);
}
static void close_tag(MMTag *tag)
{
    Object *o = tag->cache;
    Test *t = o->test;
    t->closedTags[t->tagCloses++] = o->id;
    note(o->test, CLOSE_TAG);
    release(o);
    tag->cache = 0;
}
static void close_actor(void *p, MMRollCallActor *row)
{
    MMTag tag = mm_movie_rollcall_tag(row);
    if (((Object *)tag.cache)->test != p)
        fixture_failure(__LINE__);
    close_tag(&tag);
}
static int copy_reference(void *source, void *destination, MMU32 type, MMU32 number,
                          MMU32 *copied, int *created)
{
    Test *t = ((Object *)destination)->test;
    const MMChunkAccess chunks = live_chunks(source), target = live_chunks(destination);
    MMU32 row;
    if (source != &t->resources[1] || mm_chunk_find(&chunks, type, number, &row) != MM_CHUNK_OK)
        fixture_failure(__LINE__);
    if (type == TEST_MATERIAL && number == SOURCE_MATERIAL)
    {
        if (fails(t, REBIND_MATERIALS))
            return 0;
        *copied = TARGET_MATERIAL;
    }
    else if (type == MM_BACKGROUND && number == SOURCE_BACKGROUND)
    {
        if (fails(t, REBIND_BACKGROUND))
            return 0;
        *copied = TARGET_BACKGROUND;
    }
    else
        fixture_failure(__LINE__);
    *created = mm_chunk_find(&target, type, *copied, &row) == MM_CHUNK_NOT_FOUND;
    if (*created)
        fixture_chunk_add(archive(destination), type, *copied);
    return 1;
}
static int insert_link(void *p, MMU32 root, const MMChunkChild *child)
{
    Test *t = p;
    if (root != 10 || child->type != MM_SCENE_CHUNK)
        return 1;
    if (fails(t, ADOPT))
        return 0;
    ++t->adopted;
    return 1;
}
static int sounds(void *p, void *f, MMU32 number)
{
    (void)f;
    (void)number;
    return !fails(p, SOUNDS);
}
static void *read_scene(void *p, void *resources, MMU32 number, const struct MMMovieSession *session)
{
    Test *t = p;
    (void)resources;
    (void)number;
    if (session)
        fixture_failure(__LINE__);
    if (fails(t, READ_SCENE))
        return 0;
    t->read.refs = 1;
    return &t->read;
}
static int start(void *p)
{
    return !fails(((Object *)p)->test, START);
}
static int add_actors(void *p)
{
    return !fails(((Object *)p)->test, ADD_ACTORS);
}
static void remove_actors(void *p)
{
    ++((Object *)p)->test->removedActors;
}
static void actor_tools(void *p)
{
    fails(p, ACTOR_TOOLS);
}
static void text_tools(void *p)
{
    fails(p, TEXT_TOOLS);
}
static void erase_link(void *p, MMU32 root, const MMChunkChild *child)
{
    if (root == 10 && child->type == MM_SCENE_CHUNK)
        fails(p, DELETE_CHILD);
}
static void bad_archive_edit(void *p, int issue)
{
    (void)p;
    fprintf(stderr, "unexpected archive issue %d\n", issue);
    fixture_failure(__LINE__);
}
static void set_transition(void *p, MMI32 value)
{
    Object *o = p;
    fails(o->test, SET_TRANS);
    o->test->openTransition = value;
}
static void dirty(void *p)
{
    fails(p, DIRTY);
}
static void nuked(void *p)
{
    fails(p, NUKED);
}
static void invalidate(void *p)
{
    fails(p, INVALIDATE);
}
static void changed(void *p)
{
    fails(p, ROLL_CHANGED);
}
static void diagnostic(void *p, int code)
{
    ++((Test *)p)->diagnostic[code];
}
static void close_scene(void *p)
{
    Object *o = p;
    fails(o->test, CLOSE_SCENE);
    ++o->test->closed;
    release(o);
}
static void *resize(void *p, void *old, MMU32 before, MMU32 bytes, int priority)
{
    Test *t = p;
    void *result;
    (void)before;
    (void)priority;
    ++t->memoryCalls;
    if (t->allocFailAt && t->memoryCalls >= t->allocFailAt)
        return 0;
    result = realloc(old, bytes);
    if (result && !old)
        ++t->blocks;
    return result;
}
static void free_buffer(void *p, void *memory, MMU32 bytes)
{
    Test *t = p;
    (void)bytes;
    --t->blocks;
    free(memory);
}
static void object(Object *o, Test *t, int id, int refs)
{
    o->test = t;
    o->id = id;
    o->refs = refs;
}
static void init(Test *t)
{
    MMMovieComposeServices *s = &t->s;
    MMStorageAllocator actor_allocator = {NULL, resize_actors, free_actors};
    int i;
    memset(t, 0, sizeof(*t));
    mm_movie_state_init(&t->data);
    t->failAt = 1;
    for (i = 0; i < 3; ++i)
    {
        object(&t->resources[i], t, 1 + i, 1);
        object(&t->thumbs[i], t, 4 + i, i == 1);
        object(&t->tags[i], t, 20 + i, i == 2);
    }
    object(&t->open, t, 13, 1);
    object(&t->read, t, 14, 0);
    actor_allocator.context = t;
    if (mm_rollcall_table_init(&t->data.actors, sizeof(MMRollCallActor), 1, 0x0303, 0,
                               &actor_allocator) != MM_STORAGE_OK)
        fixture_failure(__LINE__);
    t->data.actors.storage.min_grow[0] = 0;
    t->actorIds[0] = 5;
    t->actorIds[1] = 7;
    t->data.resources = &t->resources[0];
    t->data.movieNumber = 10;
    t->data.actorIdLimit = 10;
    t->data.sceneCount = 2;
    fixture_scene_seed_movie(&t->archives[0], 10, 100, 2);
    fixture_scene_seed_movie(&t->archives[1], 20, 200, 1);
    fixture_scene_seed_movie(&t->archives[1], 30, 300, 0);
    t->archives[0].observer = t;
    t->archives[1].observer = t;
    seed_scene_header(&t->resources[0], 100, 3);
    seed_scene_header(&t->resources[0], 101, 4);
    seed_scene_header(&t->resources[1], 200, 2);
    t->archives[0].before_insert = insert_link;
    t->archives[0].before_unlink = erase_link;
    t->archives[0].before_delete = erase_entry;
    resource_block_init(&t->block, t, read_payload, write_payload);
    t->block.files.release = release_payload;
    t->payloads.handles = t->block.owner.handles;
    t->payloads.files = &t->block.files;
    t->payloads.locate = locate_payload;
    t->payloads.decode = decode_payload;
    t->cache.file = &t->resources[0];
    t->cache.services.live_chunks = live_chunks;
    t->cache.services.locate = locate_payload;
    t->cache.services.handles = t->payloads.handles;
    t->cache.services.block_files = t->payloads.files;
    t->scenes.index = 1;
    t->scenes.open = &t->open;
    t->openTransition = 4;
    s->context = t;
    s->data = &t->data;
    s->scenes = &t->scenes;
    s->temporary_before_save = 1;
    s->use_temporary = temporary;
    s->autosave = save;
    s->retain = retain;
    s->release = release;
    s->transition = transition;
    s->read_thumbnail = read_thumbnail;
    s->thumbnail = thumbnail;
    s->clone_movie = clone;
    s->copy_reference = copy_reference;
    s->archive = compose_archive;
    s->titles.encoding = MM_TEXT_WINDOWS;
    s->titles.read = load_scene_title;
    s->titles.write = save_scene_title;
    s->rollcall.cache = compose_cache;
    s->rollcall.decode = decode_payload;
    s->rollcall.reader.context = t;
    s->rollcall.reader.open_actor = open_actor;
    s->rollcall.reader.close_actor = close_actor;
    s->adopt_sounds = sounds;
    s->start_scene = start;
    s->add_actors = add_actors;
    s->remove_actors = remove_actors;
    s->actor_tools = actor_tools;
    s->text_tools = text_tools;
    s->set_transition = set_transition;
    s->dirty = dirty;
    s->scene_nuked = nuked;
    s->invalidate = invalidate;
    s->rollcall_changed = changed;
    s->diagnostic = diagnostic;
    s->scene_control.context = t;
    s->scene_control.state = &t->scenes;
    s->scene_control.close = close_scene;
    s->scene_control.file = file;
    s->scene_control.edit = edit_archive;
    s->scene_control.read = read_scene;
    s->scene_control.session.graph = compose_graph;
    s->scene_control.archive_issue = bad_archive_edit;
    s->allocator.context = t;
    s->allocator.resize = resize;
    s->allocator.release = free_buffer;
}
static int add_external(Test *t, MMU32 root, MMI32 scene_count)
{
    MMMovieState *data = &t->data;
    MMI32 index = (MMI32)t->composite.scene_count;
    int ok;
    data->resources = &t->resources[1];
    data->movieNumber = root;
    data->actorIdLimit = 5;
    data->sceneCount = scene_count;
    t->scenes.index = 0;
    ok = mm_movie_compose_add(&t->composite, &index, &t->s);
    data->resources = &t->resources[0];
    data->movieNumber = 10;
    data->actorIdLimit = 10;
    data->sceneCount = 2;
    t->scenes.index = 1;
    return ok;
}
static int apply_setup(Test *t, int external, MMI32 base_limit)
{
    MMI32 index = 0;
    MMU32 i;
    t->data.actorIdLimit = base_limit;
    if (!mm_movie_compose_add(&t->composite, &index, &t->s) || (external && !add_external(t, 20, 1)))
        return 0;
    for (i = 0; i < t->composite.scene_count; ++i)
        if (!mm_composite_set_transition(&t->composite, i, 1 + (MMI32)i))
            return 0;
    memset(t->calls, 0, sizeof(t->calls));
    t->n = 0;
    return 1;
}
static int cleanup_tags(Test *t)
{
    MMTag tag;
    MMRollCallActor row;
    MMU32 i;
    CHECK(t->actorBlocks == 2);
    for (i = 1; i < t->data.actors.count; ++i)
    {
        CHECK(mm_rollcall_table_extra(&t->data.actors, i, &row));
        tag = mm_movie_rollcall_tag(&row);
        close_tag(&tag);
    }
    CHECK(!t->tags[0].refs && !t->tags[1].refs && t->tags[2].refs == 1 && !t->badRef && !t->blocks);
    CHECK(mm_rollcall_table_dispose(&t->data.actors) == MM_STORAGE_OK && !t->actorBlocks);
    return 0;
}
static int seed_destination(Test *t)
{
    MMRollCallActor row;
    memset(&row, 0, sizeof(row));
    row.data.actor_id = 1;
    row.data.reference_count = 2;
    row.cache = &t->tags[2];
    return mm_rollcall_table_add(&t->data.actors, "Base", 4, &row);
}
static int adding(void)
{
    Test t;
    MMComposeScene scene;
    MMI32 index;
    int i;
    const int faults[] = {0, TEMP, SAVE, CHILD, READ_TRANS, READ_THUMB, THUMB};
    for (i = 0; i < (int)(sizeof(faults) / sizeof(faults[0])); ++i)
    {
        init(&t);
        t.fail = faults[i];
        index = 0;
        CHECK(mm_movie_compose_add(&t.composite, &index, &t.s) == !i);
        CHECK(t.events[0] == TEMP && (i == 1 || t.events[1] == SAVE));
        if (!i)
        {
            CHECK(index == 2 && t.composite.scene_count == 2 && t.composite.movie_count == 1 &&
                  t.resources[0].refs == 2);
            CHECK(t.thumbs[0].refs == 1 && t.thumbs[1].refs == 2 &&
                  mm_composite_scene(&t.composite, 1, &scene) && scene.transition == 4);
            CHECK(mm_composite_scene(&t.composite, 0, &scene) && scene.transition == 3);
            mm_composite_empty(&t.composite, release);
        }
        else
            CHECK(!index && !t.composite.scene_count && !t.composite.movie_count);
        CHECK(t.resources[0].refs == 1 && !t.thumbs[0].refs && t.thumbs[1].refs == 1 && !t.badRef && !t.blocks);
    }
    for (i = 1; i <= 2; ++i)
    {
        init(&t);
        t.fail = CHILD;
        t.allocFailAt = i;
        index = 0;
        CHECK(!mm_movie_compose_add(&t.composite, &index, &t.s) && !index && !t.blocks);
        CHECK(!t.composite.scene_count && !t.composite.movie_count && !t.calls[CHILD] && t.resources[0].refs == 1);
    }
    init(&t);
    CHECK(apply_setup(&t, 0, 10));
    t.fail = THUMB;
    index = 1;
    CHECK(!mm_movie_compose_add(&t.composite, &index, &t.s) && index == 1);
    CHECK(t.composite.scene_count == 2 && t.composite.movie_count == 1 && t.blocks == 2 &&
          t.thumbs[0].refs == 1 && t.thumbs[1].refs == 2);
    CHECK(mm_composite_scene(&t.composite, 0, &scene) && scene.transition == 1);
    CHECK(mm_composite_scene(&t.composite, 1, &scene) && scene.transition == 2);
    mm_composite_empty(&t.composite, release);
    CHECK(!t.blocks);
    init(&t);
    t.s.temporary_before_save = 0;
    t.data.sceneCount = 0;
    index = 0;
    CHECK(mm_movie_compose_add(&t.composite, &index, &t.s) && !index && !t.calls[TEMP] && t.calls[SAVE] == 1);
    CHECK(!t.composite.scene_count && t.composite.movie_count == 1);
    mm_composite_empty(&t.composite, release);
    mm_composite_empty(&t.composite, release);
    CHECK(!t.badRef && !t.blocks && t.resources[0].refs == 1);
    return 0;
}
static int merge_fixture(Test *t, MMI32 offset)
{
    seed_rollcall(t, t->data.movieNumber);
    if (t->withActors)
        seed_actor_chunks(t, 100, 2);
    return mm_movie_compose_merge(t->data.movieNumber, offset, &t->s);
}
static int merging(void)
{
    Test t;
    int i, offset;
    MMRollCallActor row;
    const void *name;
    MMU32 count;
    for (offset = -3; offset <= 10; offset += 13)
    {
        init(&t);
        CHECK(seed_destination(&t));
        t.withActors = 1;
        CHECK(merge_fixture(&t, offset));
        CHECK(t.data.actors.count == 3 && !t.blocks);
        CHECK(mm_rollcall_table_extra(&t.data.actors, 1, &row));
        CHECK(row.data.actor_id == 5 + offset && !row.data.reference_count);
        CHECK(mm_rollcall_table_name(&t.data.actors, 1, &name, &count) && count == 144 &&
              ((const char *)name)[143] == 'A');
        CHECK(t.adjustCount == (offset > 0 ? 2 : 0) && t.actorIds[0] == 5 + (offset > 0 ? offset : 0));
        if (offset > 0)
            CHECK(t.adjusted[0] == 100 && t.adjusted[1] == 101 && t.actorIds[1] == 17);
        CHECK(!cleanup_tags(&t));
    }
    for (i = 1; i <= 2; ++i)
    {
        init(&t);
        CHECK(seed_destination(&t));
        t.withActors = 1;
        t.fail = APPEND_ACTOR;
        t.failAt = i;
        CHECK(!merge_fixture(&t, 10) && t.data.actors.count == 1);
        CHECK(t.tagCloses == 2 && t.closedTags[0] == (i == 1 ? 21 : 20) &&
              t.closedTags[1] == (i == 1 ? 20 : 21) && !t.adjustCount);
        CHECK(!cleanup_tags(&t));
    }
    init(&t);
    CHECK(seed_destination(&t));
    t.withActors = 1;
    t.fail = ADJUST;
    t.failAt = 2;
    CHECK(!merge_fixture(&t, 10) && t.data.actors.count == 1 && t.actorIds[0] == 15 &&
          t.actorIds[1] == 7);
    CHECK(t.tagCloses == 2 && t.closedTags[0] == 21 && t.closedTags[1] == 20 && !cleanup_tags(&t));
    init(&t);
    CHECK(seed_destination(&t));
    t.withActors = 1;
    t.allocFailAt = 2;
    CHECK(!merge_fixture(&t, 10) && !t.adjustCount && t.data.actors.count == 1 && !t.blocks);
    CHECK(!cleanup_tags(&t));
    init(&t);
    CHECK(seed_destination(&t));
    t.withActors = 1;
    t.allocFailAt = 1;
    CHECK(merge_fixture(&t, 10) && !t.adjustCount && t.data.actors.count == 3 && !t.blocks);
    CHECK(!cleanup_tags(&t));
    init(&t);
    CHECK(seed_destination(&t));
    t.fail = READ_TABLE;
    CHECK(!merge_fixture(&t, 10) && t.data.actors.count == 1 && !t.tagCloses && !cleanup_tags(&t));
    return 0;
}
static int rebound_assets(Test *t)
{
    const MMChunkAccess chunks = live_chunks(&t->resources[0]);
    const MMActorEventServices services = mm_actor_events_services(NULL);
    MMChunkEntry entry;
    MMMemoryStream stream;
    MMActorCostume costume;
    void *events;
    unsigned char expected[MM_SCENE_BACKGROUND_GROUP_BYTES];
    MMU32 actor, number, row;
    CHECK(mm_chunk_link_number(&chunks, MM_SCENE_CHUNK, 1200, 0, MM_BACKGROUND, &number) == MM_CHUNK_OK &&
          number == TARGET_BACKGROUND);
    CHECK(mm_chunk_link_number(&chunks, MM_SCENE_CHUNK, 1200, 1, MM_SCENE_START_GROUP, &number) == MM_CHUNK_OK &&
          mm_chunk_find(&chunks, MM_SCENE_START_GROUP, number, &row) == MM_CHUNK_OK &&
          mm_chunk_at(&chunks, row, &entry) == MM_CHUNK_OK && entry.bytes == sizeof(expected));
    CHECK(mm_scene_encode_background_group(MM_TEXT_WINDOWS, 0, 1, 0, TARGET_BACKGROUND, expected) &&
          !memcmp(t->bytes + entry.offset, expected, sizeof(expected)));
    for (actor = 100; actor <= 101; ++actor)
    {
        CHECK(mm_chunk_link_number(&chunks, MM_ACTOR_CHUNK, actor, 0, TEST_MATERIAL, &number) == MM_CHUNK_OK &&
              number == TARGET_MATERIAL);
        CHECK(mm_chunk_link_number(&chunks, MM_ACTOR_CHUNK, actor, 0, MM_ACTOR_EVENTS_CHUNK, &number) == MM_CHUNK_OK &&
              mm_chunk_find(&chunks, MM_ACTOR_EVENTS_CHUNK, number, &row) == MM_CHUNK_OK &&
              mm_chunk_at(&chunks, row, &entry) == MM_CHUNK_OK);
        CHECK(mm_memory_stream_init(&stream, t->bytes + entry.offset, entry.bytes) &&
              mm_actor_read_events_stream(&stream.stream, &services, &events) == MM_MOVIE_OK);
        CHECK(mm_actor_events_count(events) == 1 && mm_actor_events_get_payload(events, 0, &costume) &&
              costume.tag.type == TEST_MATERIAL && costume.tag.number == TARGET_MATERIAL);
        mm_actor_events_release(events);
    }
    return 0;
}
static int applying(void)
{
    Test t;
    const MMChunkChild background = {0x424b4744UL, 2, 0};
    MMChunkEdit edit;
    MMChunkEntry asset;
    MMChunkAccess index;
    MMU32 row;
    int i;
    const int failures[] = {0, CLONE, READ_TABLE, CHILD, ADOPT, SOUNDS, WRITE_TRANS, WRITE_TITLE};
    for (i = 0; i < (int)(sizeof(failures) / sizeof(failures[0])); ++i)
    {
        init(&t);
        CHECK(apply_setup(&t, 1, 10));
        t.fail = failures[i];
        CHECK(mm_movie_compose_apply(&t.composite, &t.s) == !i);
        CHECK(t.resources[0].refs == 2 && t.resources[1].refs == 2 && !t.read.refs && t.blocks == 2 && !t.badRef);
        CHECK(t.deletedCount == (i == 1 ? 0 : 1));
        CHECK(t.archives[0].count == 3U + t.adopted);
        if (t.deletedCount)
            CHECK(t.deleted[0] == 120);
        if (!i)
        {
            CHECK(t.data.sceneCount == 3 && t.data.actorIdLimit == 15 && t.scenes.index == 1 && t.openTransition == 2);
            CHECK(stored_transition(&t.resources[0], 100) == 1 && stored_transition(&t.resources[0], 1200) == 3);
            CHECK(t.calls[ACTOR_TOOLS] == 1 && t.calls[TEXT_TOOLS] == 1 && t.calls[DIRTY] == 1 &&
                  t.calls[ROLL_CHANGED] == 1);
            CHECK(position(&t, DIRTY) < position(&t, INVALIDATE) &&
                  position(&t, INVALIDATE) < position(&t, ROLL_CHANGED));
        }
        else
            CHECK(t.data.sceneCount == 2 && t.data.actorIdLimit == 10 && !t.calls[DIRTY]);
        if (i >= 3 && i <= 5)
            CHECK(t.openTransition == 2);
        if (i == 5)
            CHECK(t.adopted == 1 && !t.calls[READ_SCENE]);
        if (t.fail == WRITE_TRANS)
            CHECK(stored_transition(&t.resources[0], 100) == 3 && !t.calls[WRITE_TITLE]);
        if (t.fail == WRITE_TITLE)
            CHECK(stored_transition(&t.resources[0], 100) == 1 &&
                  position(&t, WRITE_TRANS) < position(&t, WRITE_TITLE));
        mm_composite_empty(&t.composite, release);
        CHECK(!t.blocks && t.resources[0].refs == 1 && t.resources[1].refs == 1);
    }
    init(&t);
    CHECK(apply_setup(&t, 1, 10));
    t.allocFailAt = t.memoryCalls + 1;
    CHECK(!mm_movie_compose_apply(&t.composite, &t.s) && !t.calls[CLONE] && !t.deletedCount && t.blocks == 2);
    mm_composite_empty(&t.composite, release);
    CHECK(!t.blocks);
    for (i = 0; i < 3; ++i)
    {
        init(&t);
        CHECK(apply_setup(&t, 1, 0) && seed_destination(&t));
        t.withActors = 1;
        t.fail = i == 1 ? REBIND_MATERIALS : i == 2 ? REBIND_BACKGROUND : 0;
        t.failAt = i == 1 ? 2 : 1;
        CHECK(mm_movie_compose_apply(&t.composite, &t.s) == !i);
        mm_composite_empty(&t.composite, release);
        CHECK(t.calls[REBIND_BACKGROUND] == 1 && t.calls[REBIND_MATERIALS] == (i == 2 ? 0 : 2) &&
              !t.adjustCount && t.deletedCount == 1 && t.deleted[0] == 120);
        if (!i)
            CHECK(!rebound_assets(&t));
        else
        {
            index = live_chunks(&t.resources[0]);
            CHECK(mm_chunk_find(&index, TEST_MATERIAL, TARGET_MATERIAL, &row) == MM_CHUNK_NOT_FOUND &&
                  mm_chunk_find(&index, MM_BACKGROUND, TARGET_BACKGROUND, &row) == MM_CHUNK_NOT_FOUND);
        }
        CHECK(t.data.actors.count == (i ? 1U : 3U) && t.adopted == !i && !cleanup_tags(&t));
    }
    init(&t);
    CHECK(apply_setup(&t, 1, 10));
    t.fail = LINK_SOUNDS;
    CHECK(!mm_movie_compose_apply(&t.composite, &t.s));
    CHECK(position(&t, CLONE) >= 0 && position(&t, CLONE) < position(&t, DELETE_ROOT));
    CHECK(!t.calls[READ_TABLE] && !t.adopted && t.deletedCount == 1 && t.deleted[0] == 120);
    mm_composite_empty(&t.composite, release);
    init(&t);
    CHECK(apply_setup(&t, 1, 2147483647));
    CHECK(mm_movie_compose_apply(&t.composite, &t.s) && t.data.actorIdLimit == -2147483644);
    mm_composite_empty(&t.composite, release);
    init(&t);
    CHECK(apply_setup(&t, 1, 10) && add_external(&t, 30, 0));
    t.fail = READ_TABLE;
    t.failAt = 2;
    CHECK(!mm_movie_compose_apply(&t.composite, &t.s) && t.deletedCount == 2 && t.deleted[0] == 130 &&
          t.deleted[1] == 120);
    CHECK(t.resources[0].refs == 2 && t.resources[1].refs == 3);
    mm_composite_empty(&t.composite, release);
    CHECK(!t.blocks && t.resources[0].refs == 1 && t.resources[1].refs == 1);
    for (i = READ_SCENE; i <= ADD_ACTORS; ++i)
    {
        init(&t);
        CHECK(apply_setup(&t, 1, 10));
        t.fail = i;
        CHECK(mm_movie_compose_apply(&t.composite, &t.s) && t.diagnostic[MM_COMPOSE_IMPORT_ACTORS] == 1);
        CHECK(t.calls[ACTOR_TOOLS] == 1 && t.calls[TEXT_TOOLS] == 1 && !t.read.refs);
        CHECK(t.calls[ADD_ACTORS] == (i == ADD_ACTORS));
        mm_composite_empty(&t.composite, release);
    }
    init(&t);
    fixture_chunk_add(&t.archives[0], background.type, background.number);
    edit = edit_archive(&t.resources[0]);
    t.archives[0].before_insert = NULL;
    CHECK(mm_chunk_adopt(&edit, MM_SCENE_CHUNK, 100, &background, 1) == MM_CHUNK_OK);
    t.archives[0].before_insert = insert_link;
    CHECK(apply_setup(&t, 1, 10));
    CHECK(mm_composite_remove_scene(&t.composite, 0) && mm_composite_move_scene(&t.composite, 2, 0));
    CHECK(mm_movie_compose_apply(&t.composite, &t.s) && t.scenes.index == 0 && t.data.sceneCount == 2 &&
          t.removedActors == 1);
    CHECK(t.calls[DELETE_CHILD] == 1 && t.open.refs == 1);
    CHECK(t.archives[0].count == 4 && fixture_scene_slot(&t.archives[0], 101) == 0 &&
          fixture_scene_slot(&t.archives[0], 1200) == 1 && fixture_scene_slot(&t.archives[0], 100) == -1);
    index = mm_chunk_edit_access(&edit);
    CHECK(mm_chunk_find(&index, background.type, background.number, &row) == MM_CHUNK_OK &&
          mm_chunk_at(&index, row, &asset) == MM_CHUNK_OK && (asset.flags & MM_CHUNK_LONER) && !asset.parent_count);
    mm_composite_empty(&t.composite, release);
    init(&t);
    CHECK(apply_setup(&t, 0, 10));
    CHECK(mm_composite_remove_scene(&t.composite, 0) && mm_composite_remove_scene(&t.composite, 0));
    t.fail = START;
    t.allocFailAt = t.memoryCalls + 1; /* A local-only apply needs no clone table. */
    CHECK(mm_movie_compose_apply(&t.composite, &t.s) && !t.data.sceneCount && !t.scenes.open &&
          t.scenes.index == -1);
    CHECK(t.removedActors == 2 && t.diagnostic[MM_COMPOSE_REMOVE_ACTORS] == 1 && t.closed == 1 &&
          t.calls[NUKED] == 1);
    CHECK(!t.open.refs && !t.read.refs && !t.badRef);
    mm_composite_empty(&t.composite, release);
    CHECK(!t.blocks);
    return 0;
}
static int emptying(void)
{
    Test t;
    MMComposeScene scene;
    int start;
    init(&t);
    CHECK(apply_setup(&t, 1, 10));
    CHECK(mm_composite_move_scene(&t.composite, 0, 3));
    CHECK(mm_composite_scene(&t.composite, 2, &scene) && scene.number == 100);
    CHECK(mm_composite_remove_scene(&t.composite, 0) && !mm_composite_remove_scene(&t.composite, 2));
    CHECK(mm_composite_scene(&t.composite, 2, &scene) && scene.number == 101 && scene.deleted);
    CHECK(!mm_composite_move_scene(&t.composite, 3, 0) && !mm_composite_set_transition(&t.composite, 3, 0));
    start = t.n;
    mm_composite_empty(&t.composite, release);
    CHECK(t.events[start] == 205 && t.events[start + 1] == 204 && t.events[start + 2] == 205 &&
          t.events[start + 3] == 202 && t.events[start + 4] == 201);
    CHECK(!t.composite.scene_count && !t.composite.movie_count && t.thumbs[1].refs == 1 &&
          t.resources[0].refs == 1 && t.resources[1].refs == 1 && !t.badRef && !t.blocks);
    mm_composite_empty(&t.composite, release);
    return 0;
}
int main(void)
{
    if (adding() || merging() || applying() || emptying())
        return 1;
    puts("Movie composition, ownership, rollback and shared actor remapping passed");
    return 0;
}
