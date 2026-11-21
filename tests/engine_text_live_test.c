#include "mm/text_live.h"
#include "mm/movie_archive_edit.h"
#include "chunk_writer_fixture.h"
#include <stdio.h>

static void check(int ok, int line)
{
    if (!ok)
    {
        fprintf(stderr, "live text failed at %d\n", line);
        exit(1);
    }
}
#define CHECK(x) check((x), __LINE__)

enum { text_offset = 16, long_text = 2048 };
static const unsigned char short_text[] = {'a', 'b', 'c', 'd', 0x5a, 0xa5};

typedef struct Host
{
    MMRichTextLiveServices services;
    MMRichTextState state;
    MMTextLiveOutput output;
    ChunkWriterFixture chunks;
    MMArchiveWriter archive;
    MMPieceTable pieces;
    int calls, fail_at, failure, remaining, removed, errors, args;
    int source_refs, archive_refs;
    char log[32];
    unsigned used;
    MMU32 bytes;
    unsigned char saved[text_offset + 2 + long_text], source[long_text];
} Host;

static void event(Host *h, char c)
{
    CHECK(h->used + 1 < sizeof(h->log));
    h->log[h->used++] = c;
    h->log[h->used] = 0;
}

static int step(Host *h, char c)
{
    event(h, c);
    return ++h->calls == h->fail_at ? h->failure : 1;
}

static MMU32 font_size(void *v, const MMMovieTitle *p)
{
    (void)v;
    CHECK(p->unit == 1 || p->unit == 2);
    return 8;
}

static int root(void *v, MMU32 bytes, MMU32 *number)
{
    Host *h = v;
    const int r = step(h, 'n');
    CHECK(bytes == 24 && h->output.chunk.type == MM_TEXT_ROOT);
    *number = 77;
    if (r > 0)
    {
        fixture_chunk_add(&h->chunks.index, MM_TEXT_ROOT, *number);
        h->remaining = 1;
    }
    return r;
}

static int write_header(void *v, MMU32 offset, const void *p, MMU32 bytes)
{
    (void)p;
    CHECK(offset == 0 && bytes == 16);
    return step(v, 'h');
}

static int font(void *v, const MMMovieTitle *p)
{
    (void)p;
    return step(v, 'f');
}

static void remove_root(void *v, MMU32 number)
{
    Host *h = v;
    CHECK(number == 77 && h->remaining && !h->chunks.file_refs);
    event(h, '!');
    CHECK(mm_chunk_delete(&h->archive.edit, MM_TEXT_ROOT, number) == MM_CHUNK_OK && !h->chunks.index.count);
    h->remaining = 0;
    ++h->removed;
}

static int text_extent(void *v, MMU32 bytes, MMChunkLocation *out)
{
    Host *h = ((ChunkIndexFixture *)v)->observer;
    CHECK(h->remaining && bytes == h->bytes + 2);
    if (step(h, 't') <= 0)
        return 0;
    out->file = &h->chunks;
    out->offset = text_offset;
    out->bytes = bytes;
    out->flags = 0;
    return 1;
}

static int link_text(void *v, MMU32 parent, const MMChunkChild *child)
{
    CHECK(parent == 77 && child->slot == 0);
    if (child->type != MM_TEXT_BYTES)
        return 1;
    CHECK(child->number == 0);
    return step(v, 'l') > 0;
}

static int text_write(void *v, void *file, MMU32 offset, const void *data, size_t bytes)
{
    ChunkWriterFixture *f = v;
    Host *h = f->context;
    CHECK(file == f && f->file_refs == 1 &&
          ((offset == text_offset && bytes == 2) || (offset == text_offset + 2 && bytes == h->bytes)));
    if (step(h, offset == text_offset ? 'm' : 'w') <= 0)
        return 0;
    memcpy(h->saved + offset, data, bytes);
    return 1;
}

static void retain_piece(void *v, void *file)
{
    Host *h = v;
    CHECK(file == &h->chunks || file == h->source);
    ++*(file == &h->chunks ? &h->archive_refs : &h->source_refs);
}

static void release_piece(void *v, void *file)
{
    Host *h = v;
    int *refs = file == &h->chunks ? &h->archive_refs : &h->source_refs;
    CHECK((file == &h->chunks || file == h->source) && *refs > 0);
    --*refs;
}

static int read_piece(void *v, const MMFilePiece *p, void *bytes)
{
    Host *h = v;
    if (p->file == h->source)
    {
        CHECK(p->offset <= sizeof(h->source) && p->length <= sizeof(h->source) - p->offset);
        memcpy(bytes, h->source + p->offset, p->length);
        return 1;
    }
    CHECK(p->file == &h->chunks && p->offset == text_offset + 2 && p->length == h->bytes);
    if (p->length < long_text && step(h, 'r') <= 0)
        return 0;
    memcpy(bytes, h->saved + p->offset, p->length);
    return 1;
}

static int child(void *v, MMU32 number, MMU32 kind, MMU32 bytes)
{
    Host *h = v;
    MMChunkChild link = {0};
    const int r = step(h, kind == MM_TEXT_PROPERTIES ? 'p' : 'a');
    CHECK(number == 77 && h->remaining && h->chunks.file_refs == 1 && bytes == 20 &&
          (kind == MM_TEXT_PROPERTIES || kind == MM_TEXT_ARGUMENTS));
    if (r > 0)
    {
        link.type = kind;
        fixture_chunk_add(&h->chunks.index, kind, 0);
        CHECK(mm_chunk_adopt(&h->archive.edit, MM_TEXT_ROOT, number, &link, 1) == MM_CHUNK_OK);
    }
    return r;
}

static MMU32 group_size(void *v, MMU32 kind, void *p)
{
    Host *h = v;
    CHECK(p == (kind == MM_TEXT_PROPERTIES ? (void *)&h->state.properties : (void *)&h->args));
    return 20;
}

static MMI32 count(void *v, void *p)
{
    Host *h = v;
    CHECK(p == &h->args);
    return h->args;
}

static int group(void *v, MMU32 kind, void *p, int order, int system)
{
    (void)p;
    CHECK(order == 1 && system == 0x1234);
    return step(v, kind == MM_TEXT_PROPERTIES ? 'g' : 'b');
}

static void error(void *v)
{
    Host *h = v;
    CHECK(h->output.chunk.type == MM_TEXT_ROOT);
    ++h->errors;
    event(h, 'E');
}

static void setup(Host *h, unsigned unit, MMU32 bytes)
{
    MMRichTextLiveServices *s = &h->services;
    MMPieceFiles files = {NULL, retain_piece, release_piece, NULL, read_piece, NULL, NULL};
    MMFilePiece file;
    memset(h, 0, sizeof(*h));
    CHECK(mm_rich_text_init(&h->state, unit, 0x303));
    h->state.text_system = 0x1234;
    h->bytes = bytes;
    h->args = 1;
    h->state.property_order = h->state.argument_order = 256;
    h->state.property_system = h->state.argument_system = 0x202;
    h->state.properties = &h->state.properties;
    h->state.arguments = &h->args;
    h->output.chunk.type = 22;
    h->output.chunk.number = 33;
    h->archive = chunk_writer_fixture(&h->chunks, h, text_extent, text_write);
    h->chunks.index.before_insert = link_text;
    files.context = h;
    CHECK(mm_piece_table_init(&h->pieces, NULL, &files, 0) == MM_DATA_OK);
    if (bytes == long_text)
    {
        memset(h->source, 0x33, sizeof(h->source));
        file.file = h->source;
        file.offset = 0;
        file.length = sizeof(h->source);
        CHECK(mm_piece_table_replace_file(&h->pieces, 0, 0, &file, 0) == MM_DATA_OK);
        CHECK(mm_piece_table_replace(&h->pieces, bytes, 0, short_text + 4, unit) == MM_DATA_OK);
    }
    else
        CHECK(mm_piece_table_replace(&h->pieces, 0, 0, short_text + (bytes ? 0 : 4), bytes + unit) == MM_DATA_OK);
    s->write.context = h;
    s->write.font_bytes = font_size;
    s->write.add_root = root;
    s->write.write = write_header;
    s->write.font = font;
    s->write.remove = remove_root;
    s->write.add_child = child;
    s->write.group_bytes = group_size;
    s->write.count = count;
    s->write.group = group;
    s->text = &h->pieces;
    s->archive = &h->archive;
    s->context = h;
    s->error = error;
}

static void dispose(Host *h)
{
    CHECK(!h->chunks.file_refs);
    CHECK(mm_piece_table_dispose(&h->pieces) == MM_DATA_OK);
    CHECK(!h->source_refs && !h->archive_refs);
}

static void saves(void)
{
    Host h;
    char effects[20];
    unsigned char bytes[6];
    int n, i, f, r, big, redirecting;
    unsigned unit;
    for (unit = 1; unit <= 2; ++unit)
        for (big = 0; big < 2; ++big)
            for (redirecting = 0; redirecting < 2; ++redirecting)
            {
                setup(&h, unit, 4);
                CHECK(mm_rich_text_save(&h.state, &h.output, big, redirecting, &h.services) == 1 && h.remaining &&
                      h.chunks.index.count == 4 && !h.errors && h.output.number_changed &&
                      h.output.chunk.number == 77);
                CHECK(h.saved[text_offset + (big ? 1 : 0)] == 0x34 &&
                      h.saved[text_offset + (big ? 0 : 1)] == 0x12);
                CHECK(!memcmp(h.saved + text_offset + 2, short_text, 4));
                CHECK(h.pieces.length == 4 + unit && mm_piece_table_read(&h.pieces, 0, bytes, 4 + unit) == MM_DATA_OK &&
                      !memcmp(bytes, short_text, 4 + unit));
                CHECK(!strcmp(h.log, redirecting ? "nhftlmwpgabr" : "nhftlmwpgab"));
                n = h.calls;
                memcpy(effects, h.log, (size_t)n + 1);
                dispose(&h);
                for (f = -2; f <= 0; ++f)
                    for (i = 1; i <= n; ++i)
                    {
                        if (f && strchr("tlmwr", effects[i - 1]))
                            continue;
                        setup(&h, unit, 4);
                        h.fail_at = i;
                        h.failure = f;
                        r = mm_rich_text_save(&h.state, &h.output, big, redirecting, &h.services);
                        CHECK(h.output.number_changed && h.output.chunk.type == MM_TEXT_ROOT);
                        if (effects[i - 1] == 'r')
                        {
                            CHECK(r == 1 && h.chunks.index.count == 4 && !h.errors && !h.removed);
                            CHECK(mm_piece_table_read(&h.pieces, 0, bytes, 4 + unit) == MM_DATA_OK &&
                                  !memcmp(bytes, short_text, 4 + unit));
                        }
                        else
                        {
                            CHECK(r == f && !h.remaining && !h.chunks.index.count &&
                                  h.errors == (!f) && h.removed == (i != 1));
                            CHECK(h.output.chunk.number == (i <= 3 ? MM_TEXT_NIL : 77U));
                            if (h.removed && !f)
                                CHECK(strchr(h.log, '!') < strchr(h.log, 'E'));
                        }
                        dispose(&h);
                    }
            }
    setup(&h, 1, 4);
    h.state.defaults.height = 3;
    CHECK(!mm_rich_text_save(&h.state, &h.output, 0, 0, &h.services) && !h.calls && h.errors == 1 &&
          h.output.chunk.type == MM_TEXT_ROOT && h.output.chunk.number == 33 && !h.output.number_changed);
    dispose(&h);
    setup(&h, 2, 0);
    h.args = 0;
    CHECK(mm_rich_text_save(&h.state, &h.output, 0, 1, &h.services) == 1 && h.chunks.index.count == 3 &&
          h.pieces.length == 2 && !strcmp(h.log, "nhftlmpg"));
    CHECK(mm_piece_table_read(&h.pieces, 0, bytes, 2) == MM_DATA_OK && !memcmp(bytes, short_text + 4, 2));
    dispose(&h);
    setup(&h, 1, 4);
    h.state.arguments = NULL;
    CHECK(mm_rich_text_save(&h.state, &h.output, 0, 0, &h.services) == 1 && h.chunks.index.count == 3);
    dispose(&h);
    for (redirecting = 0; redirecting < 2; ++redirecting)
    {
        MMFilePiece piece;
        setup(&h, 1, long_text);
        CHECK(mm_rich_text_save(&h.state, &h.output, 0, redirecting, &h.services) == 1 && h.chunks.index.count == 4 &&
              h.pieces.length == long_text + 1 && h.archive_refs == redirecting && h.source_refs == !redirecting);
        CHECK(!memcmp(h.saved + text_offset + 2, h.source, long_text));
        CHECK(mm_piece_table_get(&h.pieces, 0, &piece, NULL) == MM_DATA_OK &&
              piece.file == (redirecting ? (void *)&h.chunks : (void *)h.source) &&
              piece.offset == (redirecting ? text_offset + 2 : 0));
        CHECK(mm_piece_table_read(&h.pieces, long_text, bytes, 1) == MM_DATA_OK && bytes[0] == short_text[4]);
        dispose(&h);
    }
}

typedef struct Box
{
    MMTextBoxStoreServices services;
    MMTextBoxState state;
    MMMovieArchiveEditor archive;
    ChunkIndexFixture chunks;
    char log[16];
    unsigned used;
    int calls, fail_at, failure, header, asserted, other;
} Box;

static int box_step(Box *h, char c)
{
    CHECK(h->used + 1 < sizeof(h->log));
    h->log[h->used++] = c;
    h->log[h->used] = 0;
    return ++h->calls == h->fail_at ? h->failure : 1;
}

static int save(void *v, MMTextChunk *out)
{
    Box *h = v;
    CHECK(h->state.child == 0);
    out->type = h->other ? 22 : MM_TEXT_ROOT;
    out->number = 99;
    fixture_chunk_add(&h->chunks, out->type, out->number);
    return box_step(h, 'S');
}

static void assert_type(void *v)
{
    Box *h = v;
    ++h->asserted;
    h->log[h->used++] = 'T';
    h->log[h->used] = 0;
}

static int adopt(void *v, MMU32 box, const MMChunkChild *link)
{
    CHECK(box == 7 && link->number == 99 && !link->slot);
    return box_step(v, 'A') > 0;
}

static int metadata(void *v, MMU32 type, MMU32 number, const void *p, MMU32 bytes)
{
    Box *h = v;
    MMMemoryStream stream;
    MMTextBoxState decoded;
    MMChunkAccess chunks = mm_chunk_edit_access(&h->archive.edit);
    CHECK(type == MM_TEXT_BOX && number == 7 && bytes == 36);
    CHECK(mm_chunk_link_number(&chunks, type, number, 0, h->other ? 22 : MM_TEXT_ROOT, &number) == MM_CHUNK_OK &&
          number == 99);
    CHECK(mm_memory_stream_init(&stream, p, bytes) && mm_text_box_decode(&stream.stream, &decoded) &&
          !decoded.child && decoded.first == h->state.first && decoded.end == h->state.end);
    h->header = 1;
    return box_step(h, 'M');
}

static void box_setup(Box *h)
{
    memset(h, 0, sizeof(*h));
    h->state.first = 2;
    h->state.end = 4;
    h->state.child = 77;
    fixture_chunk_add(&h->chunks, MM_TEXT_BOX, 7);
    h->chunks.observer = h;
    h->chunks.before_insert = adopt;
    h->archive.edit = fixture_chunk_edit(&h->chunks);
    h->archive.context = h;
    h->archive.put = metadata;
    h->services.context = h;
    h->services.state = &h->state;
    h->services.archive = &h->archive;
    h->services.save = save;
    h->services.assert_type = assert_type;
}

static void boxes(void)
{
    Box h;
    int f, i;
    box_setup(&h);
    CHECK(mm_text_box_store(7, &h.services) == 1 && !strcmp(h.log, "SAM") && !h.state.child && h.chunks.count == 2 &&
          h.chunks.rows[1].links_used == 1 && h.header);
    for (f = -2; f <= 0; ++f)
        for (i = 1; i <= 3; ++i)
        {
            if (f && i == 2)
                continue;
            box_setup(&h);
            h.fail_at = i;
            h.failure = f;
            CHECK(mm_text_box_store(7, &h.services) == f && !h.state.child);
            CHECK(!strcmp(h.log, i == 1 ? "S" : i == 2 ? "SA" : "SAM"));
            CHECK(h.chunks.count == (i == 1 ? 2U : 1U) && h.header == (i == 3));
            CHECK(h.chunks.rows[h.chunks.count - 1].entry.type == MM_TEXT_BOX &&
                  !h.chunks.rows[h.chunks.count - 1].links_used);
        }
    box_setup(&h);
    h.other = 1;
    CHECK(mm_text_box_store(7, &h.services) == 1 && h.asserted == 1 && !strcmp(h.log, "STAM"));
}

int main(void)
{
    saves();
    boxes();
    puts("Live text: TEXT payloads, sentinel exclusion, redirect ownership, and text-box rollback passed.");
    return 0;
}
