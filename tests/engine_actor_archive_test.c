#include "mm/actor_archive.h"
#include "mm/actor_motion.h"
#include "mm/movie_sound.h"
#include "mm/rollcall_storage.h"
#include "mm/template_create.h"
#include "mm/text.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(value) do { if (!(value)) { fprintf(stderr, "line %d: %s\n", __LINE__, #value); exit(1); } } while (0)
#define ACTOR 0x41435452UL
#define FONT 0x464f4e54UL
#define MATERIAL 0x4d54524cUL
#define MOVIE 0x4d564945UL
#define ROLLCALL 0x47535420UL
#define INVALID_FRAME 2147483647L

typedef struct Graph
{
    MMChunkEntry entries[3];
    MMChunkChild children[3][4];
} Graph;
typedef union Payload
{
    MMActorSound sound;
    MMActorCostume costume;
} Payload;
typedef struct Fixture
{
    Graph source, destination;
    MMActorHeader input, written;
    MMActorEvent events[8];
    Payload payloads[8];
    unsigned char descriptor[MM_ACTOR_ARCHIVE_TEXT_BYTES];
    MMU32 copies, writes;
    int fail_write;
} Fixture;

static int entry(void *context, MMU32 i, MMChunkEntry *out)
{
    *out = ((Graph *)context)->entries[i];
    return 1;
}
static int child(void *context, MMU32 parent, MMU32 i, MMChunkChild *out)
{
    *out = ((Graph *)context)->children[parent][i];
    return 1;
}
static void append_child(Graph *graph, MMU32 parent, MMU32 type, MMU32 number, MMU32 slot)
{
    MMChunkChild *link;
    CHECK(graph->entries[parent].child_count < 4);
    link = &graph->children[parent][graph->entries[parent].child_count++];
    link->type = type;
    link->number = number;
    link->slot = slot;
}
static MMChunkAccess destination(void *context)
{
    MMChunkAccess chunks;
    chunks.context = &((Fixture *)context)->destination;
    chunks.count = 3;
    chunks.entry = entry;
    chunks.child = child;
    return chunks;
}
static int clone_actor(void *context, MMU32 actor, MMActorHeader *header, MMU32 *copied)
{
    CHECK(actor == 1);
    *header = ((Fixture *)context)->input;
    *copied = 2;
    return 1;
}
static int copy_asset(void *context, MMU32 type, MMU32 number, MMU32 *copied)
{
    Fixture *f = (Fixture *)context;
    CHECK((type == FONT && number == 9) || (type == MATERIAL && number == 8) ||
          (type == MM_SOUND_CHUNK && (number == 23 || number == 10)));
    ++f->copies;
    *copied = number + 100;
    return 1;
}
static int adopt(void *context, MMU32 parent_type, MMU32 parent, MMU32 type, MMU32 number, MMU32 slot)
{
    Graph *graph = &((Fixture *)context)->destination;
    MMU32 index;
    index = parent_type == ACTOR ? 0 : parent_type == MM_SOUND_SCENE ? 1 : 2;
    CHECK(graph->entries[index].type == parent_type && graph->entries[index].number == parent);
    CHECK((type == FONT && slot == 1) || (type == MATERIAL && slot == 2) ||
          (type == MM_SOUND_CHUNK && (slot == 5 || slot == 6)));
    append_child(graph, index, type, number, slot);
    return 1;
}
static int map_source(void *context, MMI32 source, MMI32 *mapped)
{
    CHECK(context && source == 3);
    *mapped = 7;
    return 1;
}
static int read_text(void *context, MMU32 number, unsigned char *out)
{
    CHECK(number == 5);
    memcpy(out, ((Fixture *)context)->descriptor, MM_ACTOR_ARCHIVE_TEXT_BYTES);
    return 1;
}
static int write_text(void *context, MMU32 number, unsigned char *bytes)
{
    CHECK(number == 5);
    memcpy(((Fixture *)context)->descriptor, bytes, MM_ACTOR_ARCHIVE_TEXT_BYTES);
    return 1;
}
static int write_header(void *context, MMU32 actor, const MMActorHeader *header)
{
    CHECK(actor == 2);
    ((Fixture *)context)->written = *header;
    return 1;
}
static void *read_events(void *context, MMU32 actor)
{
    CHECK(actor == 1);
    return context;
}
static MMU32 count(void *events)
{
    CHECK(events);
    return 8;
}
static void *fixed(void *events, MMU32 index) { return &((Fixture *)events)->events[index]; }
static void *data(void *events, MMU32 index) { return &((Fixture *)events)->payloads[index]; }
static int write_events(void *context, MMU32 actor, void *events)
{
    Fixture *f = (Fixture *)context;
    CHECK(context == events && actor == 2);
    ++f->writes;
    return !f->fail_write;
}

static void init(Fixture *f, int big)
{
    MMU32 i;
    memset(f, 0, sizeof(*f));
    f->source.entries[0].type = MM_SOUND_SCENE;
    f->source.entries[0].number = 1;
    append_child(&f->source, 0, MM_SOUND_CHUNK, 23, 2);
    f->destination.entries[0].type = ACTOR;
    f->destination.entries[0].number = 2;
    append_child(&f->destination, 0, MM_TMPL, 4, 0);
    append_child(&f->destination, 0, MATERIAL, 7, 1);
    f->destination.entries[1].type = MM_SOUND_SCENE;
    f->destination.entries[1].number = 3;
    append_child(&f->destination, 1, MM_SOUND_CHUNK, 555, 4);
    f->destination.entries[2].type = MM_TMPL;
    f->destination.entries[2].number = 4;
    append_child(&f->destination, 2, FONT, 6, 0);
    append_child(&f->destination, 2, MM_TDT, 5, 0);
    f->input.actor_id = 6;
    f->input.first_frame = 9;
    f->input.last_frame = INVALID_FRAME;
    f->input.template_number = 99;
    f->descriptor[big ? 1 : 0] = 1;
    f->descriptor[3] = 0x61;
    f->descriptor[7] = 0x62;
    f->descriptor[12] = 0x63;
    for (i = 0; i < 4; ++i)
        f->descriptor[big ? 19 - i : 16 + i] = (unsigned char)(FONT >> (i * 8));
    f->descriptor[big ? 23 : 20] = 9;
    for (i = 0; i < 8; ++i)
    {
        f->events[i].nfrm = 10 + (MMI32)i;
        f->events[i].rtel.irpt = 15;
        if (i < 6)
        {
            f->events[i].aet = MM_ACTOR_SOUND;
            f->payloads[i].sound.tag.type = MM_SOUND_CHUNK;
        }
        else
        {
            f->events[i].aet = MM_ACTOR_COSTUME;
            f->payloads[i].costume.tag.type = MATERIAL;
        }
    }
    f->payloads[0].sound.child = 2;
    f->payloads[0].sound.tag.number = 777;
    f->payloads[1].sound.tag.number = 23;
    f->payloads[2].sound.tag.number = 10;
    f->payloads[3].sound.tag.sid = 3;
    f->payloads[3].sound.child = 8;
    f->payloads[4].sound.tag.sid = -1;
    f->payloads[5].sound.no_sound = 1;
    f->payloads[6].costume.tag.number = 8;
    f->payloads[7].costume.custom = 1;
    f->events[5].nfrm = INVALID_FRAME;
}

typedef struct RollEdit
{
    Graph graph;
    MMMemoryStream source;
    unsigned char output[128];
    MMU32 size;
    int step;
} RollEdit;

static MMChunkAccess movie_chunks(void *context)
{
    MMChunkAccess chunks = {NULL, 1, entry, child};
    chunks.context = &((RollEdit *)context)->graph;
    return chunks;
}
static int open_roll(void *context, MMU32 type, MMU32 number, MMReadStream *stream)
{
    RollEdit *edit = (RollEdit *)context;
    CHECK(type == ROLLCALL && number == 8 && edit->step == 0);
    *stream = edit->source.stream;
    return 1;
}
static int write_roll(void *context, MMU32 offset, const void *data, size_t bytes)
{
    RollEdit *edit = (RollEdit *)context;
    CHECK(edit->step == 2 && offset <= edit->size && bytes <= edit->size - offset);
    memcpy(edit->output + offset, data, bytes);
    return 1;
}
static int add_roll(void *context, MMU32 type, MMU32 size, MMU32 *number, MMStorageWriter *writer)
{
    RollEdit *edit = (RollEdit *)context;
    CHECK(type == ROLLCALL && size <= sizeof(edit->output) && edit->step == 1);
    ++edit->step;
    edit->size = size;
    *number = 9;
    writer->context = edit;
    writer->length = size;
    writer->write_at = write_roll;
    return 1;
}
static int adopt_roll(void *context, MMU32 type, MMU32 parent, MMU32 child_type, MMU32 child, MMU32 slot)
{
    RollEdit *edit = (RollEdit *)context;
    CHECK(type == MOVIE && parent == 1 && child_type == ROLLCALL && child == 9 && slot == 0);
    CHECK(edit->step == 2);
    ++edit->step;
    return 1;
}
static void unlink_roll(void *context, MMU32 type, MMU32 parent, MMU32 child_type, MMU32 child, MMU32 slot)
{
    RollEdit *edit = (RollEdit *)context;
    CHECK(type == MOVIE && parent == 1 && child_type == ROLLCALL && child == 8 && slot == 0);
    CHECK(edit->step == 0);
    ++edit->step;
}

static void rename_rollcall(void)
{
    RollEdit edit;
    MMMovieArchiveEditor editor = {NULL, 1, MM_TEXT_WINDOWS, 0, NULL, movie_chunks,
                                   open_roll, add_roll, adopt_roll, unlink_roll};
    MMActorArchiveName name = {{4, 1, 0, 2, MM_TMPL, 19}, "edited", 6, 1};
    MMMovieActor original = {4, 3, 5, 0, MM_TMPL, 1}, other = {6, 2, 5, 1, MM_TMPL, 2}, actor;
    MMCollectionHeader header = {0};
    MMRollCallTable table;
    MMMemoryStream output, row;
    unsigned char input[93] = {0}, bytes[MM_MOVIE_ACTOR_BYTES];
    const void *text;
    MMU32 characters;
    int big;

    memset(&edit, 0, sizeof(edit));
    editor.context = &edit;
    edit.graph.entries[0].type = MOVIE;
    edit.graph.entries[0].number = 1;
    append_child(&edit.graph, 0, ROLLCALL, 8, 0);
    header.kind = MM_COLLECTION_STRINGS;
    header.system = MM_TEXT_WINDOWS;
    header.big_endian = 1;
    header.width = 32;
    header.count = 2;
    header.bytes = 9;
    header.free_count = -1;
    CHECK(mm_collection_header_write(&header, input, sizeof(input)) == MM_STORAGE_OK);
    memcpy(input + 20, "\003old\004kept", 9);
    mm_movie_write_actor(&original, input + 33, 1);
    input[64] = 4;
    mm_movie_write_actor(&other, input + 65, 1);
    input[81] = 0xba; /* A legacy pointer placeholder has no runtime meaning. */
    CHECK(mm_memory_stream_init(&edit.source, input, sizeof(input)));
    CHECK(mm_actor_archive_register(&editor, 1, NULL, &name) == MM_ACTOR_REGISTER_OK && edit.step == 3);

    CHECK(mm_rollcall_table_init(&table, MM_MOVIE_ACTOR_BYTES, 1, MM_TEXT_WINDOWS, 0, NULL) == MM_STORAGE_OK);
    CHECK(mm_memory_stream_init(&output, edit.output, edit.size));
    CHECK(mm_rollcall_table_read(&table, &output.stream, NULL, NULL, &big) == MM_STORAGE_OK);
    CHECK(big == 0 && table.count == 2);
    CHECK(mm_rollcall_table_name(&table, 0, &text, &characters) && characters == 6 && !memcmp(text, "edited", 6));
    CHECK(mm_rollcall_table_extra(&table, 0, bytes) && mm_memory_stream_init(&row, bytes, sizeof(bytes)));
    CHECK(mm_movie_read_actor(&row.stream, big, &actor, NULL) == MM_MOVIE_OK);
    CHECK(actor.actor_id == 4 && actor.reference_count == 3 && actor.browser_flags == 2 &&
          actor.source_id == 0 && actor.template_type == MM_TMPL && actor.template_number == 19);
    CHECK(mm_rollcall_table_name(&table, 1, &text, &characters) && characters == 4 && !memcmp(text, "kept", 4));
    CHECK(mm_rollcall_table_extra(&table, 1, bytes));
    CHECK(mm_movie_read_actor(&row.stream, big, &actor, NULL) == MM_MOVIE_OK);
    CHECK(actor.actor_id == 6 && actor.reference_count == 2 && actor.source_id == 5 && actor.browser_flags == 1 &&
          actor.template_number == 2 && bytes[16] == 0 && bytes[17] == 0 && bytes[18] == 0 && bytes[19] == 0);
    CHECK(mm_rollcall_table_dispose(&table) == MM_STORAGE_OK);
}

int main(void)
{
    Fixture f;
    const MMActorArchiveRequest request = {1, 1, 3, 7, 12, 9};
    const MMActorArchiveRequest forward = {1, 1, 3, 7, 9, 12};
    MMActorArchiveHost host = {0};
    MMU32 actor, i;
    MMActorHeader header;
    unsigned char descriptor[MM_ACTOR_ARCHIVE_TEXT_BYTES];
    int big;
    host.context = &f;
    host.source_chunks.context = &f.source;
    host.source_chunks.count = 1;
    host.source_chunks.entry = entry;
    host.source_chunks.child = child;
    host.destination_chunks = destination;
    host.clone_actor = clone_actor;
    host.copy_asset = copy_asset;
    host.adopt = adopt;
    host.map_source = map_source;
    host.read_text = read_text;
    host.write_text = write_text;
    host.write_header = write_header;
    host.read_events = read_events;
    host.write_events = write_events;
    host.events.count = count;
    host.events.fixed = fixed;
    host.events.data = data;
    for (big = 0; big <= 1; ++big)
    {
        init(&f, big);
        memcpy(descriptor, f.descriptor, sizeof(descriptor));
        descriptor[big ? 23 : 20] = 109;
        CHECK(mm_actor_archive_transfer(&request, &host, &actor, &header) == MM_ACTOR_ARCHIVE_OK);
        CHECK(actor == 2 && header.actor_id == 7 && header.first_frame == 6 &&
              header.last_frame == INVALID_FRAME && header.template_number == 4);
        CHECK(f.written.actor_id == 7 && f.writes == 1 && f.copies == 4);
        CHECK(!memcmp(f.descriptor, descriptor, sizeof(descriptor)));
        for (i = 0; i < 8; ++i)
            CHECK(f.events[i].nfrm == (i == 5 ? INVALID_FRAME : 7 + (MMI32)i) && f.events[i].rtel.irpt == 15);
        CHECK(f.payloads[0].sound.child == 5 && f.payloads[0].sound.tag.number == 123);
        CHECK(f.payloads[1].sound.child == 5 && f.payloads[1].sound.tag.number == 123);
        CHECK(f.payloads[2].sound.child == 6 && f.payloads[2].sound.tag.number == 110);
        CHECK(f.payloads[3].sound.tag.sid == 7 && f.payloads[3].sound.child == 8);
        CHECK(f.payloads[4].sound.tag.sid == -1 && f.payloads[5].sound.tag.number == 0);
        CHECK(f.payloads[6].costume.tag.number == 108 && f.payloads[7].costume.tag.number == 0);
    }

    init(&f, 0);
    f.input.first_frame = -2147483647L;
    actor = 777;
    header.actor_id = 888;
    CHECK(mm_actor_archive_transfer(&request, &host, &actor, &header) == MM_ACTOR_ARCHIVE_FRAME);
    CHECK(actor == 777 && header.actor_id == 888 && !f.copies && !f.writes);

    init(&f, 0);
    f.events[0].nfrm = INVALID_FRAME - 3;
    CHECK(mm_actor_archive_transfer(&forward, &host, &actor, &header) == MM_ACTOR_ARCHIVE_FRAME);
    CHECK(actor == 777 && header.actor_id == 888 && !f.writes);

    init(&f, 0);
    f.fail_write = 1;
    CHECK(mm_actor_archive_transfer(&request, &host, &actor, &header) == MM_ACTOR_ARCHIVE_WRITE_EVENTS);
    CHECK(actor == 777 && header.actor_id == 888 && f.writes == 1);
    rename_rollcall();
    return 0;
}
