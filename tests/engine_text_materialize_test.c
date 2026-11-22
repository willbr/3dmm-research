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
} Token;
typedef struct Host
{
    MMTextMaterializeServices s;
    MMTextDocumentConfig config;
    Token file, translated, temporary;
    MMTextCollection *props, *args;
    int returned_props, returned_args;
    void *slots[5];
    MMPieceTable pieces;
    MMArray *property_rows;
    MMGroup *argument_rows;
    unsigned char header[16], marker[2], properties[16], normalized[16];
    MMU32 fixed[3];
    int foreign, big, change, missing, empty, copy, base, defaults, replaced, opened, long_text, unpadded, relocate;
    int refs_at_publish, fail_index, fail_result, calls, fail_at, failed;
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
    if (kind == MM_TEXT_BYTES)
    {
        Token *t = p;
        CHECK(t->refs > 0);
        --t->refs;
        event(h, t == &h->file ? 'f' : 't');
    }
    else
    {
        MMTextCollection **owner = kind == MM_TEXT_PROPERTIES ? &h->props : &h->args;
        CHECK(*owner == p);
        mm_text_collection_dispose(*owner);
        *owner = NULL;
        event(h, kind == MM_TEXT_PROPERTIES ? 'p' : 'a');
    }
}
static MMI32 width(void *v, MMU32 kind, void *p)
{
    Host *h = (Host *)v;
    CHECK(p == (kind == MM_TEXT_PROPERTIES ? h->props : h->args));
    return mm_text_collection_width(p);
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
    int *returned = kind == MM_TEXT_PROPERTIES ? &h->returned_props : &h->returned_args;
    int r = step(h, 'G');
    CHECK(number == 3 && !*returned);
    /* Owned output on failure must still be released. */
    *out = kind == MM_TEXT_PROPERTIES ? h->props : h->args;
    *returned = 1;
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
static void *get(void *v, int slot)
{
    Host *h = (Host *)v;
    CHECK(slot == MM_TEXT_OWNER_STREAM && h->base && h->defaults);
    return &h->pieces;
}
static MMPieceTable *pieces(void *p)
{
    return (MMPieceTable *)p;
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
static MMU32 fixed(Host *h, MMU32 i)
{
    MMGroupLocation row;
    MMU32 n;
    CHECK(mm_group_read_slot(h->argument_rows, i, &row) == MM_GROUP_SLOT_PRESENT);
    memcpy(&n, h->argument_rows->storage->buffers[0].data + row.offset, sizeof(n));
    return n;
}
static void replace_payload_buffer(MMCollectionStorage *storage, MMCollectionStorage *replacement, MMU32 bytes)
{
    MMStorageBuffer original = storage->buffers[0];
    CHECK(mm_storage_copy(replacement, storage, bytes, 0) == MM_STORAGE_OK);
    storage->buffers[0] = replacement->buffers[0];
    replacement->buffers[0] = original;
}
static int open_argument(void *v, MMI32 i, unsigned kind, int order, int system)
{
    Host *h = (Host *)v;
    CHECK(h->slots[MM_TEXT_OWNER_PROPERTIES] == h->props && h->slots[MM_TEXT_OWNER_ARGUMENTS] == h->args);
    CHECK(h->empty || !memcmp(h->property_rows->storage->buffers[0].data, h->normalized, 16));
    CHECK(h->property_rows->storage->min_grow[0] == 128 && h->property_rows->storage->min_grow[1] == 16);
    CHECK(h->argument_rows->storage->min_grow[1] == 16 * sizeof(MMGroupLocation));
    CHECK(i == (h->opened == 0 ? 2 : 0) && kind == (i == 2 ? 0x80U : 2U));
    CHECK(order == (h->foreign ? 256 : 1) && system == 0x202);
    ++h->opened;
    event(h, i == 2 ? '2' : '0');
    if (h->relocate && i == 2)
    {
        MMCollectionStorage replacement;
        CHECK(mm_storage_init(&replacement, NULL) == MM_STORAGE_OK);
        replace_payload_buffer(h->argument_rows->storage, &replacement, h->argument_rows->bytes);
        CHECK(mm_storage_dispose(&replacement) == MM_STORAGE_OK);
    }
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
    s->base.objects.get = get;
    s->base.objects.set = set;
    s->base.objects.retain = retain;
    s->base.objects.release = release;
    s->base.translate = translate;
    s->base.pieces = pieces;
    s->init_base = base;
    s->defaults = defaults;
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
    h->props = mm_text_collection_new(MM_TEXT_PROPERTIES, NULL);
    h->args = mm_text_collection_new(MM_TEXT_ARGUMENTS, NULL);
    CHECK(h->props && h->args);
    h->property_rows = mm_text_collection_property_array(h->props, NULL);
    h->argument_rows = mm_text_collection_argument_group(h->args);
    if (!h->empty)
        CHECK(mm_array_insert(h->property_rows, 0, h->properties, 2) == MM_STORAGE_OK);
    for (i = 0; i < 3; ++i)
        CHECK(mm_group_add(h->argument_rows, i == 2 && h->unpadded, &payload, h->fixed + i, NULL) == MM_STORAGE_OK);
    CHECK(mm_group_delete(h->argument_rows, 1) == MM_STORAGE_OK);
    if (h->unpadded)
        h->argument_rows->bytes -= 3;
    return mm_text_materialize(&h->config, MM_TEXT_ROOT, 9, h->copy, &h->s);
}
static void cleanup(Host *h)
{
    if (h->slots[MM_TEXT_OWNER_PROPERTIES])
        drop(h, MM_TEXT_PROPERTIES, h->slots[MM_TEXT_OWNER_PROPERTIES]);
    if (h->slots[MM_TEXT_OWNER_ARGUMENTS])
        drop(h, MM_TEXT_ARGUMENTS, h->slots[MM_TEXT_OWNER_ARGUMENTS]);
    CHECK(mm_piece_table_dispose(&h->pieces) == MM_DATA_OK);
    CHECK(!h->props || !h->returned_props);
    CHECK(!h->args || !h->returned_args);
    mm_text_collection_dispose(h->props);
    mm_text_collection_dispose(h->args);
    CHECK(h->file.refs == 1 && !h->translated.refs && !h->temporary.refs);
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
                        CHECK(mm_group_read_slot(h.argument_rows, 1, &slot) == MM_GROUP_SLOT_FREE);
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
                            CHECK(mm_group_read_slot(h.argument_rows, 1, &slot) == MM_GROUP_SLOT_FREE);
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
    CHECK(mm_group_location(h.argument_rows, 2, &slot) != MM_STORAGE_OK);
    cleanup(&h);
    init(&h, 1, 0, 1);
    h.relocate = 1;
    CHECK(run(&h) == 1 && h.opened == 2 && fixed(&h, 0) == 0x02000001UL);
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
typedef struct FontHost
{
    MMTextFontArgumentServices services;
    MMTextCollection *arguments;
    MMCollectionStorage *storage, replacement;
    MMGroup *rows;
    unsigned conversions, mappings, strings, entries, count;
    int converted, mapped, relocate;
} FontHost;
static MMI32 arg_convert(void *v, const void *p, MMU32 n, int encoding, void *out, MMU32 capacity)
{
    FontHost *h = (FontHost *)v;
    const unsigned short wide = 'Z';
    CHECK(h->storage->buffers[0].locks == 1 && n == 1 && *(const unsigned char *)p == 'A');
    CHECK(encoding == MM_TEXT_MAC && capacity == MM_STRING_MAX);
    ++h->conversions;
    if (h->services.wire.unit == 1)
        *(unsigned char *)out = 'Z';
    else
        memcpy(out, &wide, sizeof(wide));
    return h->converted;
}
static int arg_map(void *v, const MMMovieTitle *p, int system, MMI32 *out)
{
    FontHost *h = (FontHost *)v;
    unsigned expected = h->conversions ? 'Z' : 'A';
    CHECK(!h->storage->buffers[0].locks && system == -123 && p->unit == h->services.wire.unit);
    CHECK(p->count == h->count);
    if (p->count)
        CHECK((p->unit == 1 ? *(const unsigned char *)p->text : p->text[0]) == expected);
    if (h->relocate)
        replace_payload_buffer(h->storage, &h->replacement, h->rows->bytes);
    ++h->mappings;
    *out = -17;
    return h->mapped;
}
static void arg_bad_string(void *v)
{
    FontHost *h = (FontHost *)v;
    CHECK(h->storage->buffers[0].locks == 1 && !h->entries);
    ++h->strings;
}
static void arg_bad_entry(void *v)
{
    FontHost *h = (FontHost *)v;
    CHECK(h->storage->buffers[0].locks == h->strings);
    ++h->entries;
}
static void init_font(FontHost *h, unsigned unit, int foreign, unsigned count, int size_adjustment)
{
    MMTextFontArgumentServices *s = &h->services;
    MMStringWire source;
    unsigned char data[24];
    unsigned short counted[3];
    const short one = 1, system = -123;
    const MMI32 original = 23, fixed_word = 0x02000001;
    MMI32 payload_bytes;
    memset(h, 0, sizeof(*h));
    memset(data, 0, sizeof(data));
    h->arguments = mm_text_collection_new(MM_TEXT_ARGUMENTS, NULL);
    CHECK(h->arguments != NULL && mm_storage_init(&h->replacement, NULL) == MM_STORAGE_OK);
    h->rows = mm_text_collection_argument_group(h->arguments);
    h->storage = h->rows->storage;
    h->mapped = h->converted = 1;
    h->count = count;
    s->context = h;
    s->wire.unit = unit;
    s->wire.native_big_endian = *(const unsigned char *)&one == 0;
    s->wire.current_encoding = unit == 1 ? MM_TEXT_WINDOWS :
                                s->wire.native_big_endian ? MM_TEXT_UTF16_BE : MM_TEXT_UTF16_LE;
    s->wire.swapped_unicode_encoding = s->wire.native_big_endian ? MM_TEXT_UTF16_LE : MM_TEXT_UTF16_BE;
    s->wire.context = h;
    s->wire.convert = arg_convert;
    s->map = arg_map;
    s->bad_string = arg_bad_string;
    s->bad_entry = arg_bad_entry;
    source = s->wire;
    source.buffer = counted;
    if (foreign)
    {
        source.unit = 1;
        source.current_encoding = MM_TEXT_MAC;
    }
    memset(counted, 0, sizeof(counted));
    if (source.unit == 1)
    {
        ((unsigned char *)counted)[0] = (unsigned char)count;
        ((unsigned char *)counted)[1] = count ? 'A' : 0;
    }
    else
    {
        counted[0] = (unsigned short)count;
        counted[1] = count ? 'A' : 0;
    }
    memcpy(data, &original, 4);
    memcpy(data + 4, &system, 2);
    CHECK(mm_string_wire_get(&source, data + 6, sizeof(data) - 6));
    payload_bytes = 6 + (MMI32)mm_string_wire_size(&source) + size_adjustment;
    CHECK(payload_bytes >= 0 && payload_bytes <= (MMI32)sizeof(data));
    CHECK(mm_group_add(h->rows, (MMU32)payload_bytes, data, &fixed_word, NULL) == MM_STORAGE_OK);
}
static MMI32 font_id(const MMCollectionStorage *storage)
{
    MMI32 id;
    memcpy(&id, storage->buffers[0].data + sizeof(MMI32), sizeof(id));
    return id;
}
static void dispose_font(FontHost *h)
{
    CHECK(!h->storage->buffers[0].locks && !h->replacement.buffers[0].locks);
    mm_text_collection_dispose(h->arguments);
    CHECK(mm_storage_dispose(&h->replacement) == MM_STORAGE_OK);
}
static void font_arguments(void)
{
    FontHost h;
    unsigned unit, kind;
    int n, failure, foreign;
    for (unit = 1; unit <= 2; ++unit)
    {
        for (foreign = 0; foreign <= 1; ++foreign)
        {
            init_font(&h, unit, foreign, 1, 0);
            h.relocate = foreign;
            CHECK(mm_text_open_font_argument(h.arguments, 0, 2, &h.services) == 1);
            CHECK(h.mappings == 1 && h.conversions == (unsigned)foreign);
            CHECK(font_id(h.storage) == -17 && !h.strings && !h.entries);
            if (foreign)
                CHECK(font_id(&h.replacement) == 23);
            dispose_font(&h);
        }
        for (kind = 0; kind < 256; ++kind)
            if (kind != 2)
            {
                init_font(&h, unit, 0, 1, 0);
                CHECK(!mm_text_open_font_argument(h.arguments, 0, kind, &h.services));
                CHECK(!h.entries && !h.mappings && font_id(h.storage) == 23);
                dispose_font(&h);
            }
        for (n = 0; n < 6 + 2 + 3 * (int)unit; ++n)
        {
            init_font(&h, unit, 0, 1, n - 8 - 3 * (int)unit);
            CHECK(!mm_text_open_font_argument(h.arguments, 0, 2, &h.services));
            CHECK(h.entries == 1 && h.strings == (unsigned)(n >= 6) && !h.mappings);
            dispose_font(&h);
        }
        for (failure = 0; failure >= -2; --failure)
        {
            init_font(&h, unit, 0, 1, 0);
            h.mapped = failure;
            CHECK(mm_text_open_font_argument(h.arguments, 0, 2, &h.services) == failure);
            CHECK(h.mappings == 1 && !h.entries && font_id(h.storage) == 23);
            dispose_font(&h);
        }
        init_font(&h, unit, 1, 1, 0);
        h.converted = 0;
        CHECK(!mm_text_open_font_argument(h.arguments, 0, 2, &h.services));
        CHECK(h.conversions == 1 && h.strings == 1 && h.entries == 1 && !h.mappings);
        dispose_font(&h);
        init_font(&h, unit, 0, 1, 1);
        CHECK(!mm_text_open_font_argument(h.arguments, 0, 2, &h.services) && h.strings == 1 && !h.mappings);
        dispose_font(&h);
        init_font(&h, unit, 0, 0, 0);
        CHECK(mm_text_open_font_argument(h.arguments, 0, 2, &h.services) == 1 && font_id(h.storage) == -17);
        dispose_font(&h);
    }
}
int main(void)
{
    materialization();
    font_arguments();
    puts("Text materialization checks passed");
    return 0;
}
