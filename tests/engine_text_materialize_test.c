#include "mm/text_materialize.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static void check(int ok, int line)
{
    if (!ok)
    {
        fprintf(stderr, "text materialization failed at %d\n", line);
        exit(1);
    }
}
#define CHECK(x) check((x), __LINE__)
typedef struct Token
{
    int refs;
    void *rows;
} Token;
typedef struct Host
{
    MMTextMaterializeServices s;
    MMTextFontArgumentServices font;
    MMTextDocumentConfig config;
    Token file, translated, temporary, props, args;
    void *slots[5];
    MMPieceTable pieces;
    MMCollectionStorage property_storage, argument_storage;
    MMArray property_rows;
    MMGroup argument_rows;
    unsigned char header[16], marker[2], properties[16], normalized[16], argument[16];
    MMU32 fixed[3];
    int foreign, big, change, missing, empty, copy, base, defaults, replaced, opened, long_text, unpadded;
    int refs_at_publish, fail_index, fail_result, calls, fail_at, failed;
    int locked, diagnostic, mapped, put, size, decoded, font_system;
    char log[256];
    unsigned used;
} Host;
static void event(Host *h, char c)
{
    CHECK(h->used < 255);
    h->log[h->used++] = c;
    h->log[h->used] = 0;
}
static int step(Host *h, char c)
{
    event(h, c);
    if (++h->calls == h->fail_at)
    {
        h->failed = c;
        return h->fail_result;
    }
    return 1;
}
static void retain_file(void *v, void *p)
{
    Host *h = (Host *)v;
    Token *t = (Token *)p;
    CHECK(t == &h->file && t->refs > 0);
    ++t->refs;
    event(h, '+');
}
static void drop(void *v, MMU32 kind, void *p)
{
    Host *h = (Host *)v;
    Token *t = (Token *)p;
    (void)kind;
    CHECK(t->refs > 0);
    --t->refs;
    event(h, t == &h->file ? 'f' : t == &h->translated ? 't' : t == &h->props ? 'p' : 'a');
}
static MMI32 width(void *v, MMU32 kind, void *p)
{
    Host *h = (Host *)v;
    CHECK(p == (kind == MM_TEXT_PROPERTIES ? (void *)&h->props : (void *)&h->args));
    return kind == MM_TEXT_PROPERTIES ? 8 : 4;
}
static int header_read(void *v, MMU32 off, void *p, size_t n)
{
    Host *h = (Host *)v;
    CHECK(off == 0 && n == 16);
    memcpy(p, h->header, n);
    return 1;
}
static int root(void *v, MMU32 type, MMU32 number, MMReadStream *out)
{
    Host *h = (Host *)v;
    int r = step(h, 'R');
    CHECK(type == MM_TEXT_ROOT && number == 9);
    out->context = h;
    out->length = 16;
    out->read_at = header_read;
    return r;
}
static int font_read(void *v, MMMovieTitle *out)
{
    Host *h = (Host *)v;
    unsigned short name[2] = {'A', 0};
    int r = step(h, 'F');
    if (r > 0)
        CHECK(mm_movie_title_set(out, out->unit == 1 ? (const void *)"A" : (const void *)name, 1));
    return r;
}
static int child(void *v, MMU32 type, MMU32 number, MMU32 kind, MMU32 *out)
{
    Host *h = (Host *)v;
    CHECK(type == MM_TEXT_ROOT && number == 9);
    *out = 3;
    if (kind == MM_TEXT_ARGUMENTS && h->missing)
        return 0;
    return step(h, 'C');
}
static int location(void *v, MMU32 type, MMU32 number, MMTextLocation *out)
{
    Host *h = (Host *)v;
    CHECK(type == MM_TEXT_BYTES && number == 3);
    out->file = &h->file;
    out->offset = 10;
    out->bytes = h->long_text ? 2050 : 6;
    return step(h, 'L');
}
static int read_text(void *v, const MMTextLocation *p, void *out, MMU32 n)
{
    Host *h = (Host *)v;
    CHECK(p->offset == 10 && p->file == &h->file && n == 2);
    memcpy(out, h->marker, 2);
    return step(h, 'E');
}
static int group(void *v, MMU32 kind, MMU32 number, void **out, int *order, int *system)
{
    Host *h = (Host *)v;
    Token *t = kind == MM_TEXT_PROPERTIES ? &h->props : &h->args;
    int r = step(h, 'G');
    CHECK(number == 3 && !t->refs);
    /* Owned output on failure must still be released. */
    *out = t;
    t->refs = 1;
    *order = h->foreign ? 256 : 1;
    *system = 0x202;
    return r;
}
static void set(void *v, int slot, void *p)
{
    Host *h = (Host *)v;
    CHECK(!h->slots[slot]);
    CHECK(slot == MM_TEXT_OWNER_PROPERTIES || slot == MM_TEXT_OWNER_ARGUMENTS);
    h->refs_at_publish = h->file.refs + h->translated.refs + h->temporary.refs;
    CHECK(h->refs_at_publish == 2 + h->long_text); /* archive owner, stored reference, optional file piece */
    h->slots[slot] = p;
    event(h, slot == MM_TEXT_OWNER_PROPERTIES ? 'P' : 'A');
}
static void retain(void *v, int kind, void *p)
{
    CHECK(kind == MM_TEXT_OWNER_FILE);
    retain_file(v, p);
}
static void release(void *v, int kind, void *p)
{
    CHECK(kind == MM_TEXT_OWNER_FILE);
    drop(v, MM_TEXT_BYTES, p);
}
static int base(void *v)
{
    Host *h = (Host *)v;
    int r = step(h, 'I');
    h->base = 1;
    CHECK(mm_piece_table_replace(&h->pieces, 0, 0, "1234567890", 5 * h->config.unit) == MM_DATA_OK);
    return r;
}
static int defaults(void *v, const MMRichTextDefaults *d, const MMMovieTitle *font)
{
    Host *h = (Host *)v;
    CHECK(d->width == 321 && d->height == 19 && d->background == 0x01020304UL);
    CHECK(font->unit == h->config.unit && font->count == 1 && d->font_system == 0x303);
    h->defaults = 1;
    return step(h, 'D');
}
static int translate(void *v, MMTextLocation *p, int system)
{
    Host *h = (Host *)v;
    CHECK(h->defaults && p->file == &h->file && h->file.refs == 3);
    CHECK(system == 0x505 && p->offset == 12 && p->bytes == (h->long_text ? 2048 : 4));
    if (h->change)
    {
        drop(h, MM_TEXT_BYTES, p->file);
        p->file = &h->translated;
        p->offset = 0;
        p->bytes = h->long_text ? 2048 : 8;
        h->translated.refs = 1;
    }
    return step(h, 'T');
}
static MMPieceTable *pieces(void *v)
{
    Host *h = (Host *)v;
    CHECK(h->base && h->defaults);
    return &h->pieces;
}
static int piece_read(void *v, const MMFilePiece *p, void *out)
{
    Host *h = (Host *)v;
    int r = step(h, 'B');
    CHECK(p->file == (h->change ? (void *)&h->translated : (void *)&h->file));
    CHECK(p->offset == (h->change ? 0U : 12U) && p->length == (h->change ? 8U : 4U));
    if (r > 0)
    {
        memset(out, 'x', p->length);
        h->replaced = 1;
    }
    return r > 0;
}
static void piece_retain(void *v, void *p)
{
    Host *h = (Host *)v;
    Token *t = (Token *)p;
    CHECK((t == &h->file || t == &h->translated) && t->refs > 0);
    ++t->refs;
}
static void piece_release(void *v, void *p)
{
    drop(v, MM_TEXT_BYTES, p);
}
static void *piece_temp(void *v)
{
    Host *h = (Host *)v;
    CHECK(h->long_text && h->copy && !h->change && !h->temporary.refs);
    h->temporary.refs = 1;
    return &h->temporary;
}
static int piece_copy(void *v, const MMFilePiece *from, const MMFilePiece *to)
{
    Host *h = (Host *)v;
    CHECK(from->file == &h->file && from->offset == 12 && from->length == 2048);
    CHECK(to->file == &h->temporary && to->offset == 0 && to->length == 2048);
    return 1;
}
static MMArray properties(void *p)
{
    return *(MMArray *)((Token *)p)->rows;
}
static MMGroup arguments(void *p)
{
    return *(MMGroup *)((Token *)p)->rows;
}
static MMU32 fixed(Host *h, MMU32 i)
{
    MMGroupLocation row;
    MMU32 n;
    CHECK(mm_group_read_slot(&h->argument_rows, i, &row) == MM_GROUP_SLOT_PRESENT);
    memcpy(&n, h->argument_storage.buffers[0].data + row.offset, sizeof(n));
    return n;
}
static int open_argument(void *v, MMI32 i, unsigned kind, int order, int system)
{
    Host *h = (Host *)v;
    CHECK(h->slots[MM_TEXT_OWNER_PROPERTIES] == &h->props && h->slots[MM_TEXT_OWNER_ARGUMENTS] == &h->args);
    CHECK(h->empty || !memcmp(h->property_storage.buffers[0].data, h->normalized, 16));
    CHECK(i == (h->opened == 0 ? 2 : 0) && kind == (i == 2 ? 0x80U : 2U));
    CHECK(order == (h->foreign ? 256 : 1) && system == 0x202);
    ++h->opened;
    event(h, i == 2 ? '2' : '0');
    return i == h->fail_index ? h->fail_result : step(h, 'O');
}
static void word(unsigned char *p, MMU32 n, unsigned bytes, int big)
{
    unsigned i;
    for (i = 0; i < bytes; ++i)
        p[big ? bytes - 1 - i : i] = (unsigned char)(n >> (8 * i));
}
static void init(Host *h, unsigned unit, int big, int foreign)
{
    unsigned i;
    MMTextMaterializeServices *s;
    memset(h, 0, sizeof(*h));
    h->config.unit = unit;
    h->config.system = 0x101;
    h->config.big = big;
    h->big = big;
    h->foreign = foreign;
    h->copy = 1;
    h->file.refs = 1;
    h->fail_index = -1;
    h->fail_result = -2;
    word(h->header, 1, 2, big);
    word(h->header + 2, 0x303, 2, big);
    word(h->header + 4, 321, 4, big);
    word(h->header + 8, 19, 4, big);
    word(h->header + 12, 0x01020304UL, 4, big);
    word(h->marker, 0x505, 2, big);
    for (i = 0; i < 16; ++i)
        h->normalized[i] = (unsigned char)(i + 1);
    for (i = 0; i < 16; ++i)
        h->properties[i] = h->normalized[foreign ? (i / 4) * 4 + 3 - i % 4 : i];
    h->fixed[0] = foreign ? 0x01000002UL : 0x02000001UL;
    h->fixed[1] = 0xdeadbeefUL;
    h->fixed[2] = foreign ? 0x01000080UL : 0x80000001UL;
    s = &h->s;
    s->context = h;
    s->read.context = h;
    s->read.objects.context = h;
    s->read.objects.retain_file = retain_file;
    s->read.objects.release = drop;
    s->read.objects.width = width;
    s->read.root = root;
    s->read.font = font_read;
    s->read.child = child;
    s->read.location = location;
    s->read.read_text = read_text;
    s->read.group = group;
    s->base.context = h;
    s->base.objects.context = h;
    s->base.objects.set = set;
    s->base.objects.retain = retain;
    s->base.objects.release = release;
    s->base.translate = translate;
    s->init_base = base;
    s->defaults = defaults;
    s->pieces = pieces;
    s->properties = properties;
    s->arguments = arguments;
    s->open_argument = open_argument;
}
static int run(Host *h)
{
    MMPieceFiles files;
    const unsigned char payload = 0x61;
    unsigned i;
    memset(&files, 0, sizeof(files));
    files.context = h;
    files.retain = piece_retain;
    files.release = piece_release;
    files.read = piece_read;
    files.create_temp = piece_temp;
    files.copy = piece_copy;
    CHECK(mm_piece_table_init(&h->pieces, NULL, &files, 0) == MM_DATA_OK);
    CHECK(mm_storage_init(&h->property_storage, NULL) == MM_STORAGE_OK);
    h->property_rows.storage = &h->property_storage;
    h->property_rows.width = 8;
    h->props.rows = &h->property_rows;
    if (!h->empty)
        CHECK(mm_array_insert(&h->property_rows, 0, h->properties, 2) == MM_STORAGE_OK);
    CHECK(mm_storage_init(&h->argument_storage, NULL) == MM_STORAGE_OK);
    h->argument_rows.storage = &h->argument_storage;
    h->argument_rows.fixed_bytes = 4;
    h->args.rows = &h->argument_rows;
    for (i = 0; i < 3; ++i)
        CHECK(mm_group_add(&h->argument_rows, i == 2 && h->unpadded, &payload, h->fixed + i, NULL) == MM_STORAGE_OK);
    CHECK(mm_group_delete(&h->argument_rows, 1) == MM_STORAGE_OK);
    if (h->unpadded)
        h->argument_rows.bytes -= 3;
    return mm_text_materialize(&h->config, MM_TEXT_ROOT, 9, h->copy, &h->s);
}
static void cleanup(Host *h)
{
    if (h->slots[MM_TEXT_OWNER_PROPERTIES])
        drop(h, MM_TEXT_PROPERTIES, h->slots[MM_TEXT_OWNER_PROPERTIES]);
    if (h->slots[MM_TEXT_OWNER_ARGUMENTS])
        drop(h, MM_TEXT_ARGUMENTS, h->slots[MM_TEXT_OWNER_ARGUMENTS]);
    CHECK(mm_piece_table_dispose(&h->pieces) == MM_DATA_OK);
    CHECK(mm_storage_dispose(&h->property_storage) == MM_STORAGE_OK);
    CHECK(mm_storage_dispose(&h->argument_storage) == MM_STORAGE_OK);
    CHECK(h->file.refs == 1 && !h->translated.refs && !h->temporary.refs && !h->props.refs && !h->args.refs);
}
static void materialization(void)
{
    Host h;
    MMFilePiece piece;
    MMGroupLocation slot;
    char text[14];
    unsigned unit, inserted;
    int big, foreign, change, copy, n, calls, failure, r, which;
    for (unit = 1; unit <= 2; ++unit)
        for (big = 0; big < 2; ++big)
            for (foreign = 0; foreign < 2; ++foreign)
                for (change = 0; change < 2; ++change)
                    for (copy = 0; copy < 2; ++copy)
                    {
                        init(&h, unit, big, foreign);
                        h.change = change;
                        h.copy = copy;
                        CHECK(run(&h) == 1 && h.opened == 2 && h.replaced && h.defaults);
                        CHECK(fixed(&h, 0) == 0x02000001UL && fixed(&h, 2) == 0x80000001UL);
                        CHECK(mm_group_read_slot(&h.argument_rows, 1, &slot) == MM_GROUP_SLOT_FREE);
                        inserted = change ? 8 : 4;
                        /* Preserve the legacy CpMac()-1 byte removal for both unit widths. */
                        CHECK(h.pieces.length == inserted + 5 * unit - 4);
                        CHECK(mm_piece_table_read(&h.pieces, 0, text, h.pieces.length) == MM_DATA_OK);
                        CHECK(!memcmp(text, "xxxxxxxx", inserted) && !memcmp(text + inserted, "567890", 5 * unit - 4));
                        CHECK(strstr(h.log, change ? "tPA2O0Of" : "fPA2O0Of") != 0);
                        calls = h.calls;
                        cleanup(&h);
                        /* Fail every fallible effect, including owned outputs returned on failure.
                         * Font read's ordinary false intentionally falls back to an empty font;
                         * that pre-existing reader policy is tested in engine-text-store. */
                        for (failure = 0; failure >= -2; --failure)
                            for (n = 1; n <= calls; ++n)
                            {
                                init(&h, unit, big, foreign);
                                h.change = change;
                                h.copy = copy;
                                h.fail_at = n;
                                h.fail_result = failure;
                                if (n == 3 && !failure)
                                    continue;
                                r = run(&h);
                                if (!failure && h.failed == 'C' && h.replaced)
                                {
                                    CHECK(r == 1 && !h.slots[MM_TEXT_OWNER_ARGUMENTS] && !h.opened);
                                    cleanup(&h);
                                    continue;
                                }
                                CHECK(r == (h.failed == 'B' ? 0 : failure));
                                CHECK(h.base);
                                if (h.failed == 'D' || h.failed == 'T' || h.failed == 'B' || h.failed == 'O')
                                    CHECK(h.defaults);
                                if (h.failed == 'D' || h.failed == 'T' || h.failed == 'B')
                                {
                                    CHECK(!h.slots[MM_TEXT_OWNER_PROPERTIES] && !h.slots[MM_TEXT_OWNER_ARGUMENTS] &&
                                          !h.opened);
                                }
                                if (h.failed == 'O')
                                    CHECK(h.replaced && h.slots[MM_TEXT_OWNER_ARGUMENTS]);
                                if (h.failed == 'B')
                                    CHECK(h.pieces.length == 5 * unit);
                                cleanup(&h);
                            }
                        /* A virtual hook can reject either entry, leaving only visited fixed
                         * records byte-normalized; the free hole is never touched. */
                        for (which = 0; which < 2; ++which)
                        {
                            init(&h, unit, big, foreign);
                            h.fail_index = which ? 0 : 2;
                            h.fail_result = 0;
                            CHECK(run(&h) == 0 && h.opened == (which ? 2 : 1));
                            CHECK(fixed(&h, 2) == 0x80000001UL);
                            CHECK(mm_group_read_slot(&h.argument_rows, 1, &slot) == MM_GROUP_SLOT_FREE);
                            CHECK(fixed(&h, 0) == (foreign && !which ? 0x01000002UL : 0x02000001UL));
                            cleanup(&h);
                        }
                        init(&h, unit, big, foreign);
                        h.missing = 1;
                        h.empty = 1;
                        CHECK(run(&h) == 1 && !h.opened && !h.slots[MM_TEXT_OWNER_ARGUMENTS]);
                        cleanup(&h);
                    }
    init(&h, 1, 0, 1);
    h.unpadded = 1;
    CHECK(run(&h) == 1 && h.opened == 2 && fixed(&h, 2) == 0x80000001UL);
    CHECK(mm_group_location(&h.argument_rows, 2, &slot) != MM_STORAGE_OK);
    cleanup(&h);
    for (change = 0; change < 2; ++change)
        for (copy = 0; copy < 2; ++copy)
        {
            init(&h, 1, 0, 0);
            h.change = change;
            h.copy = copy;
            h.long_text = 1;
            CHECK(run(&h) == 1 && h.pieces.length == 2049);
            CHECK(mm_piece_table_get(&h.pieces, 0, &piece, NULL) == MM_DATA_OK && piece.length == 2048);
            CHECK(piece.file == (change ? &h.translated : copy ? &h.temporary : &h.file));
            CHECK(piece.offset == (change || copy ? 0U : 12U));
            cleanup(&h);
        }
}
static MMI32 arg_bytes(void *v, MMI32 i)
{
    Host *h = (Host *)v;
    CHECK(i == 7);
    event(h, 'S');
    return h->size;
}
static int arg_system(void *v, MMI32 i)
{
    Host *h = (Host *)v;
    CHECK(i == 7);
    event(h, 'Y');
    return -123;
}
static const void *arg_lock(void *v, MMI32 i)
{
    Host *h = (Host *)v;
    CHECK(i == 7 && !h->locked);
    h->locked = 1;
    event(h, 'L');
    return h->argument;
}
static void arg_unlock(void *v)
{
    Host *h = (Host *)v;
    CHECK(h->locked);
    h->locked = 0;
    event(h, 'U');
}
static int arg_decode(void *v, const void *p, MMI32 n, MMMovieTitle *out)
{
    Host *h = (Host *)v;
    CHECK(h->locked && p == h->argument + 6 && n == h->size - 6);
    CHECK(out->unit == h->config.unit && out->count == 0);
    ++h->decoded;
    return step(h, 'D');
}
static int arg_map(void *v, const MMMovieTitle *p, int system, MMI32 *out)
{
    Host *h = (Host *)v;
    CHECK(!h->locked && system == -123 && p->unit == h->config.unit);
    ++h->mapped;
    *out = -17;
    return step(h, 'M');
}
static void arg_put(void *v, MMI32 i, MMI32 font)
{
    Host *h = (Host *)v;
    CHECK(!h->locked && i == 7 && font == -17);
    ++h->put;
    event(h, 'P');
}
static void arg_diagnostic(void *v)
{
    Host *h = (Host *)v;
    ++h->diagnostic;
    event(h, h->locked ? '!' : '?');
}
static void init_font(Host *h, unsigned unit)
{
    MMTextFontArgumentServices *s;
    init(h, unit, 0, 0);
    s = &h->font;
    h->size = 16;
    s->context = h;
    s->bytes = arg_bytes;
    s->system = arg_system;
    s->lock = arg_lock;
    s->unlock = arg_unlock;
    s->decode = arg_decode;
    s->map = arg_map;
    s->put = arg_put;
    s->diagnostic = arg_diagnostic;
}
static void font_arguments(void)
{
    Host h;
    unsigned unit, kind;
    int n, failure;
    for (unit = 1; unit <= 2; ++unit)
    {
        init_font(&h, unit);
        CHECK(mm_text_open_font_argument(unit, 7, 2, &h.font) == 1);
        CHECK(!strcmp(h.log, "SYLDUMP") && h.put == 1 && !h.locked);
        for (kind = 0; kind < 256; ++kind)
            if (kind != 2)
            {
                init_font(&h, unit);
                CHECK(!mm_text_open_font_argument(unit, 7, kind, &h.font));
                CHECK(!strcmp(h.log, "S") && !h.diagnostic);
            }
        for (n = 0; n < 6; ++n)
        {
            init_font(&h, unit);
            h.size = n;
            CHECK(!mm_text_open_font_argument(unit, 7, 2, &h.font));
            CHECK(!strcmp(h.log, "S?") && h.diagnostic == 1);
        }
        for (n = 1; n <= 2; ++n)
            for (failure = 0; failure >= -2; --failure)
            {
                init_font(&h, unit);
                h.fail_at = n;
                h.fail_result = failure;
                CHECK(mm_text_open_font_argument(unit, 7, 2, &h.font) == failure && !h.locked && !h.put);
                CHECK(!strcmp(h.log, n == 1 ? (failure ? "SYLDU" : "SYLD!U") : "SYLDUM"));
                CHECK(h.diagnostic == (n == 1 && !failure));
            }
        init_font(&h, unit);
        h.size = 6;
        CHECK(mm_text_open_font_argument(unit, 7, 2, &h.font) == 1);
        CHECK(h.decoded == 1 && h.mapped == 1 && h.put == 1);
    }
}
int main(void)
{
    materialization();
    font_arguments();
    puts("Text materialization checks passed");
    return 0;
}
