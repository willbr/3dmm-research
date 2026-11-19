#include "mm/movie_title.h"
#include "mm/movie_root.h"
#include "mm/movie_archive_edit.h"
#include "mm/collections.h"
#include "mm/scene_data.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static void check(int ok, int line)
{
    if (!ok)
    {
        fprintf(stderr, "movie title/root failed at %d\n", line);
        exit(1);
    }
}
#define CHECK(x) check((x), __LINE__)
static void title_set(MMMovieTitle *t, const char *s)
{
    unsigned short wide[256];
    size_t i, n = strlen(s);
    CHECK(n <= 255);
    for (i = 0; i < n; ++i)
        wide[i] = (unsigned char)s[i];
    CHECK(mm_movie_title_set(t, t->unit == 1 ? (const void *)s : wide, (MMU32)n));
}
static int title_is(const MMMovieTitle *t, const char *s)
{
    size_t i, n = strlen(s);
    const unsigned char *bytes = (const unsigned char *)mm_movie_title_text(t);
    unsigned short value;
    if (t->count != n)
        return 0;
    for (i = 0; i < n; ++i)
    {
        if (t->unit == 1)
            value = bytes[i];
        else
            memcpy(&value, bytes + i * 2, 2);
        if (value != (unsigned char)s[i])
            return 0;
    }
    if (t->unit == 1)
        return bytes[n] == 0;
    memcpy(&value, bytes + n * 2, 2);
    return value == 0;
}
typedef struct TitleHost
{
    char log[16];
    int used, mutate, empty_default;
    const char *leaf;
    MMMovieTitle *current;
} TitleHost;
static void event(TitleHost *h, char c)
{
    CHECK(h->used < 15);
    h->log[h->used++] = c;
    h->log[h->used] = 0;
}
static void leaf(void *c, MMMovieTitle *t)
{
    TitleHost *h = (TitleHost *)c;
    event(h, 'l');
    title_set(t, h->leaf);
}
static void fallback(void *c, MMMovieTitle *t)
{
    TitleHost *h = (TitleHost *)c;
    event(h, 'd');
    CHECK(t == h->current && !t->count);
    title_set(t, h->empty_default ? "" : "Default");
}
static void notify(void *c, MMMovieTitle *t)
{
    TitleHost *h = (TitleHost *)c;
    event(h, 'n');
    CHECK(t == h->current);
    if (h->mutate)
        title_set(t, "Changed by host");
}
static void titles(void)
{
    MMMovieTitle t, before;
    TitleHost h;
    MMMovieTitleServices s = {NULL, leaf, fallback, notify};
    const char *inputs[] = {"Movie.3mm", "Movie.part.3mm", ".hidden", "trailing.", "plain", "", "a.b.c", "a..b"};
    const char *expected[] = {"Movie", "Movie.part", "Default", "trailing", "plain", "Default", "a.b", "a."};
    char full[256];
    unsigned unit;
    size_t i;
    memset(&h, 0, sizeof(h));
    s.context = &h;
    h.current = &t;
    memset(full, 'x', 255);
    full[255] = 0;
    for (unit = 1; unit <= 2; ++unit)
    {
        CHECK(mm_movie_title_init(&t, unit));
        mm_movie_title_choose(&t, 0, &s);
        CHECK(title_is(&t, "Default") && !strcmp(h.log, "dn"));
        h.used = 0;
        h.log[0] = 0;
        mm_movie_title_choose(&t, 0, &s);
        CHECK(title_is(&t, "Default") && !h.used);
        for (i = 0; i < sizeof(inputs) / sizeof(inputs[0]); ++i)
        {
            h.used = 0;
            h.log[0] = 0;
            h.leaf = inputs[i];
            mm_movie_title_choose(&t, 1, &s);
            CHECK(title_is(&t, expected[i]) && !strcmp(h.log, (i == 2 || i == 5) ? "ldn" : "ln"));
        }
        title_set(&t, "ABCDE");
        CHECK(mm_movie_title_set(&t, (const unsigned char *)mm_movie_title_text(&t) + unit, 3) &&
              title_is(&t, "BCD"));
        title_set(&t, full);
        before = t;
        CHECK(!mm_movie_title_set(&t, full, 256) && !memcmp(&before, &t, sizeof(t)));
        CHECK(!mm_movie_title_set(&t, NULL, 1) && !memcmp(&before, &t, sizeof(t)));
        CHECK(mm_movie_title_set(&t, NULL, 0) && title_is(&t, ""));
        h.mutate = 1;
        h.used = 0;
        h.log[0] = 0;
        mm_movie_title_choose(&t, 0, &s);
        CHECK(title_is(&t, "Changed by host") && !strcmp(h.log, "dn"));
        h.mutate = 0;
        h.empty_default = 1;
        CHECK(mm_movie_title_set(&t, NULL, 0));
        mm_movie_title_choose(&t, 0, &s);
        CHECK(!t.count);
        h.empty_default = 0;
        h.used = 0;
        h.log[0] = 0;
    }
    before = t;
    CHECK(!mm_movie_title_init(&t, 3) && !memcmp(&before, &t, sizeof(t)));
}
typedef struct RootHost
{
    char log[32];
    int used, first_fail, read_fail, io_fail, add_fail, write_fail, error;
    MMU32 *number;
    unsigned char bytes[8];
    MMU32 length;
} RootHost;
static void root_event(RootHost *h, char c)
{
    CHECK(h->used < 31);
    h->log[h->used++] = c;
    h->log[h->used] = 0;
}
static int first(void *c, MMU32 *out)
{
    RootHost *h = (RootHost *)c;
    root_event(h, 'f');
    *out = 17;
    return !h->first_fail;
}
static int read_at(void *c, MMU32 offset, void *bytes, size_t size)
{
    RootHost *h = (RootHost *)c;
    root_event(h, 'i');
    if (h->io_fail)
        return 0;
    CHECK(offset + size <= 8);
    memcpy(bytes, h->bytes + offset, size);
    return 1;
}
static int read_root(void *c, MMU32 number, MMReadStream *s)
{
    RootHost *h = (RootHost *)c;
    root_event(h, 'r');
    CHECK(number == 17 || number == 99);
    s->context = h;
    s->length = h->length;
    s->read_at = read_at;
    return !h->read_fail;
}
static int add(void *c, MMU32 bytes, MMU32 *number)
{
    RootHost *h = (RootHost *)c;
    root_event(h, 'a');
    CHECK(bytes == 8);
    *number = 17;
    return !h->add_fail;
}
static int write_root(void *c, const void *bytes, MMU32 count)
{
    RootHost *h = (RootHost *)c;
    root_event(h, 'w');
    CHECK(count == 8);
    memcpy(h->bytes, bytes, 8);
    return !h->write_fail;
}
static void remove_root(void *c, MMU32 n)
{
    RootHost *h = (RootHost *)c;
    root_event(h, 'd');
    CHECK(n == 17 && h->number && *h->number == 17);
}
static void error(void *c, int result)
{
    RootHost *h = (RootHost *)c;
    root_event(h, 'e');
    h->error = result;
}
static void reset(RootHost *h)
{
    h->used = 0;
    h->log[0] = 0;
    h->error = 0;
}
static void roots(void)
{
    RootHost h;
    MMMovieRootServices s = {NULL, first, read_root, add, write_root, remove_root, error};
    MMMovieHeader header, unchanged;
    MMU32 number;
    int big;
    memset(&h, 0, sizeof(h));
    s.context = &h;
    h.number = &number;
    h.length = 8;
    for (big = 0; big < 2; ++big)
    {
        reset(&h);
        number = 99;
        CHECK(mm_movie_create_root(&number, 0x0303, big, &s) && number == 17 && !strcmp(h.log, "aw"));
        reset(&h);
        number = 0xffffffffUL;
        CHECK(mm_movie_verify_root(&number, &header, &s) && number == 17 && !strcmp(h.log, "fri"));
        CHECK(header.current_version == 2 && header.backward_version == 2 && header.system == 0x0303 &&
              header.big_endian == big);
        reset(&h);
        CHECK(mm_movie_verify_root(&number, NULL, &s) && !strcmp(h.log, "ri"));
        reset(&h);
        CHECK(mm_movie_verify_root(NULL, &header, &s) && !strcmp(h.log, "fri"));
        unchanged = header;
        h.read_fail = 1;
        number = 0xffffffffUL;
        reset(&h);
        CHECK(!mm_movie_verify_root(&number, &header, &s) && number == 17 && h.error == MM_MOVIE_BAD_FILE &&
              !memcmp(&header, &unchanged, sizeof(header)) && !strcmp(h.log, "fre"));
        h.read_fail = 0;
        h.first_fail = 1;
        number = 0xffffffffUL;
        reset(&h);
        CHECK(!mm_movie_verify_root(&number, &header, &s) && number == 0xffffffffUL && !strcmp(h.log, "fe"));
        h.first_fail = 0;
        h.io_fail = 1;
        number = 99;
        reset(&h);
        CHECK(!mm_movie_verify_root(&number, &header, &s) && number == 99 && h.error == MM_MOVIE_BAD_FILE &&
              !strcmp(h.log, "rie"));
        h.io_fail = 0;
        h.length = 7;
        reset(&h);
        CHECK(!mm_movie_verify_root(&number, &header, &s) && h.error == MM_MOVIE_BAD_FILE &&
              !strcmp(h.log, "re"));
        h.length = 8;
        h.bytes[big ? 5 : 4] = 0;
        reset(&h);
        number = 0xffffffffUL;
        CHECK(!mm_movie_verify_root(&number, &header, &s) && number == 17 &&
              h.error == MM_MOVIE_BAD_VERSION && !memcmp(&header, &unchanged, sizeof(header)));
        h.bytes[0] = h.bytes[1] = 0;
        reset(&h);
        CHECK(!mm_movie_verify_root(&number, &header, &s) && h.error == MM_MOVIE_BAD_FILE);
        reset(&h);
        h.add_fail = 1;
        number = 99;
        CHECK(!mm_movie_create_root(&number, 0x0303, big, &s) && number == 17 && !strcmp(h.log, "a"));
        h.add_fail = 0;
        reset(&h);
        h.write_fail = 1;
        number = 99;
        CHECK(!mm_movie_create_root(&number, 0x0303, big, &s) && number == 0xffffffffUL && !strcmp(h.log, "awd"));
        h.write_fail = 0;
    }
}
#define ARCHIVE_MOVIE 0x4d564945UL
#define ARCHIVE_SCENE 0x5343454eUL
#define ARCHIVE_FRAMES 0x47474652UL
#define ARCHIVE_STARTS 0x47475354UL
#define ARCHIVE_ROLLCALL 0x47535420UL

typedef struct ArchiveHost
{
    unsigned char records[7][MM_SCENE_BACKGROUND_GROUP_BYTES];
    MMU32 lengths[7], adds, linked_scenes, linked_groups, old_groups;
    int fail_link, rekeyed, titled, rollcall;
} ArchiveHost;

static int archive_entry(void *context, MMU32 position, MMChunkEntry *entry)
{
    ArchiveHost *host = (ArchiveHost *)context;
    memset(entry, 0, sizeof(*entry));
    if (host->adds && position == 0)
    {
        entry->type = ARCHIVE_MOVIE;
        entry->number = 5;
        entry->child_count = host->linked_scenes + 1 + host->rollcall;
    }
    else
    {
        entry->type = ARCHIVE_SCENE;
        entry->number = position == (host->adds != 0 ? 1U : 0U) ? 7 : 20;
    }
    return 1;
}
static int archive_child(void *context, MMU32 parent, MMU32 i, MMChunkChild *out)
{
    ArchiveHost *host = (ArchiveHost *)context;
    if (host->rollcall && i == 0)
    {
        out->type = ARCHIVE_ROLLCALL;
        out->number = 9;
        out->slot = 0;
        return 1;
    }
    i -= host->rollcall;
    CHECK(parent == 0 && i <= host->linked_scenes);
    out->type = i < host->linked_scenes ? ARCHIVE_SCENE : ARCHIVE_ROLLCALL;
    out->number = i == 0 ? 7 : 20;
    out->slot = i < host->linked_scenes ? i : 23;
    return 1;
}
static MMChunkAccess archive_chunks(void *context)
{
    MMChunkAccess access = {NULL, 2, archive_entry, archive_child};
    access.context = context;
    if (((ArchiveHost *)context)->adds)
        ++access.count;
    return access;
}
static int archive_write(void *context, MMU32 offset, const void *data, size_t length)
{
    ArchiveHost *host = (ArchiveHost *)context;
    CHECK(!offset && length == host->lengths[host->adds - 1]);
    memcpy(host->records[host->adds - 1], data, length);
    return 1;
}
static int archive_add(void *context, MMU32 type, MMU32 size, MMU32 *number, MMStorageWriter *writer)
{
    ArchiveHost *host = (ArchiveHost *)context;
    static const MMU32 types[] = {ARCHIVE_MOVIE, ARCHIVE_SCENE, ARCHIVE_FRAMES, ARCHIVE_STARTS,
                                  ARCHIVE_FRAMES, ARCHIVE_STARTS, ARCHIVE_ROLLCALL};
    static const MMU32 numbers[] = {5, 0, 3, 4, 5, 6, 9};
    MMU32 index = host->adds++;
    CHECK(index < 7 && type == types[index] && size <= sizeof(host->records[0]));
    CHECK((writer == NULL) == (type == ARCHIVE_SCENE));
    host->lengths[index] = size;
    *number = numbers[index];
    if (writer)
    {
        writer->context = context;
        writer->length = size;
        writer->write_at = archive_write;
    }
    return 1;
}
static int archive_put(void *context, MMU32 type, MMU32 number, const void *data, MMU32 size)
{
    CHECK(type == ARCHIVE_SCENE && number == 0 && size == MM_SCENE_HEADER_BYTES);
    return archive_write(context, 0, data, size);
}
static int archive_title(void *context, MMU32 type, MMU32 number, const MMMovieTitle *title)
{
    ArchiveHost *host = (ArchiveHost *)context;
    CHECK(type == ARCHIVE_SCENE && number == 0 && host->adds == 2);
    CHECK(title->unit == 2 && title_is(title, "Scene 3"));
    host->titled = 1;
    return 1;
}
static int archive_rekey(void *context, MMU32 type, MMU32 old, MMU32 number)
{
    ArchiveHost *host = (ArchiveHost *)context;
    CHECK(type == ARCHIVE_SCENE && old == 0 && number == 80 && host->linked_groups == 2);
    host->rekeyed = 1;
    return 1;
}
static int archive_adopt(void *context, MMU32 type, MMU32 parent, MMU32 child_type, MMU32 number, MMU32 slot)
{
    ArchiveHost *host = (ArchiveHost *)context;
    if (child_type == ARCHIVE_ROLLCALL)
    {
        CHECK(type == ARCHIVE_MOVIE && parent == 5 && number == 9 && slot == 0 && !host->rollcall);
        host->rollcall = 1;
        return 1;
    }
    if (type == ARCHIVE_MOVIE)
    {
        CHECK(parent == 5 && child_type == ARCHIVE_SCENE && slot == host->linked_scenes);
        CHECK(number == (slot == 0 ? 7U : slot == 1 ? 20U : 80U));
        if (slot == 2)
            CHECK(host->old_groups == 2);
        if (host->fail_link && slot == 1)
            return 0;
        ++host->linked_scenes;
    }
    else
    {
        CHECK(type == ARCHIVE_SCENE && parent == (host->rekeyed ? 80U : 0U));
        CHECK(slot == host->linked_groups % 2 && child_type == (slot ? ARCHIVE_STARTS : ARCHIVE_FRAMES));
        CHECK(number == 3 + host->linked_groups);
        ++host->linked_groups;
    }
    return 1;
}
static void archive_unlink(void *context, MMU32 type, MMU32 parent, MMU32 child_type, MMU32 number, MMU32 slot)
{
    ArchiveHost *host = (ArchiveHost *)context;
    CHECK(type == ARCHIVE_SCENE && parent == 80 && host->linked_groups == 4 && slot == host->old_groups);
    CHECK(number == 3 + slot && child_type == (slot ? ARCHIVE_STARTS : ARCHIVE_FRAMES));
    ++host->old_groups;
}

static void archived_scenes(void)
{
    static const unsigned char start_row[] = {
        0,0,0,1, 0,0,0,4, 0,0,0,2, 0,0,0,0, 'B','K','G','D', 0,0,0,11, 0,0,0,0, 0,0,0,24
    };
    ArchiveHost host;
    MMMovieArchiveEditor editor = {NULL, 2, MM_TEXT_UTF16_LE, 1, NULL, archive_chunks, NULL, archive_add,
                                   archive_adopt, archive_unlink, archive_put, archive_rekey, archive_title};
    MMMovieArchiveRoot root = {999, 0};
    MMMovieHeader header;
    MMCollectionHeader rollcall;
    MMSceneState scene;
    MMMemoryStream stream;
    editor.context = &host;
    memset(&host, 0, sizeof(host));
    host.fail_link = 1;
    CHECK(mm_movie_archive_ensure_root(&editor, &root) == MM_MOVIE_SCENE_ARCHIVE_ROOT_LINK);
    CHECK(root.number == 999 && !root.created && host.linked_scenes == 1);
    memset(&host, 0, sizeof(host));
    CHECK(mm_movie_archive_ensure_root(&editor, &root) == MM_MOVIE_SCENE_ARCHIVE_OK);
    CHECK(root.number == 5 && root.created && host.linked_scenes == 2);
    CHECK(mm_memory_stream_init(&stream, host.records[0], host.lengths[0]));
    CHECK(mm_movie_read_header(&stream.stream, &header) == MM_MOVIE_OK && header.big_endian);
    CHECK(mm_movie_archive_ensure_root(&editor, &root) == MM_MOVIE_SCENE_ARCHIVE_OK);
    CHECK(!root.created && host.adds == 1 && host.linked_scenes == 2);
    CHECK(mm_movie_archive_append_scene(&editor, root.number, 20, 2, 11) == MM_MOVIE_SCENE_ARCHIVE_CREATE);
    CHECK(host.adds == 1);
    CHECK(mm_movie_archive_append_scene(&editor, root.number, 80, 2, 11) == MM_MOVIE_SCENE_ARCHIVE_OK);
    CHECK(host.titled && host.rekeyed && host.linked_scenes == 3 && host.adds == 6);
    CHECK(mm_scene_init(&scene, 2) && mm_memory_stream_init(&stream, host.records[1], host.lengths[1]));
    CHECK(mm_scene_decode_header(&stream.stream, &scene));
    CHECK(scene.firstFrame == 1 && scene.lastFrame == 1 && scene.transition == 0);
    CHECK(host.lengths[5] == MM_SCENE_BACKGROUND_GROUP_BYTES);
    CHECK(!memcmp(host.records[5] + MM_SCENE_EMPTY_GROUP_BYTES, start_row, sizeof(start_row)));
    CHECK(mm_movie_archive_ensure_empty_rollcall(&editor) && host.rollcall && host.adds == 7);
    CHECK(mm_memory_stream_init(&stream, host.records[6], host.lengths[6]));
    CHECK(mm_collection_header_read(&stream.stream, MM_COLLECTION_STRINGS, 0, &rollcall) == MM_STORAGE_OK);
    CHECK(rollcall.big_endian && rollcall.system == MM_TEXT_UTF16_LE && rollcall.count == 0 &&
          rollcall.width == MM_MOVIE_ACTOR_BYTES + 4);
    CHECK(mm_movie_archive_ensure_empty_rollcall(&editor) && host.adds == 7);
}

int main(void)
{
    titles();
    roots();
    archived_scenes();
    puts("movie title/root passed");
    return 0;
}
