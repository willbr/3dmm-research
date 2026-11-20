#include "mm/text_store.h"
#include "mm/collections.h"
#include <stdlib.h>
#include "mm/wide.h"
#include <string.h>
static MMU32 get(const unsigned char *p, unsigned n, int big)
{
    MMU32 value = 0;
    unsigned i;
    for (i = 0; i < n; ++i)
        value |= (MMU32)p[big ? n - 1 - i : i] << (8 * i);
    return value;
}
static void put(unsigned char *p, MMU32 value, unsigned n, int big)
{
    unsigned i;
    for (i = 0; i < n; ++i)
        p[big ? n - 1 - i : i] = (unsigned char)(value >> (8 * i));
}
static int order(const unsigned char *p)
{
    return p[0] == 1 && !p[1] ? 0 : !p[0] && p[1] == 1 ? 1 : -1;
}
static int signed_short(MMU32 v)
{
    return v < 32768 ? (int)v : -(int)(65536UL - v);
}
int mm_text_box_decode(const MMReadStream *stream, MMTextBoxState *out)
{
    unsigned char b[36];
    MMTextBoxState s;
    int big;
    if (stream->length != 36 || !mm_read_exact(stream, 0, b, 36) || (big = order(b)) < 0)
        return 0;
    memset(&s, 0, sizeof(s));
    s.first = mm_signed_word(get(b + 4, 4, big));
    s.end = mm_signed_word(get(b + 8, 4, big));
    if (s.first == (-2147483647L - 1) || b[32] > 1)
        return 0;
    s.current = s.first - 1;
    s.left = mm_signed_word(get(b + 12, 4, big));
    s.right = mm_signed_word(get(b + 16, 4, big));
    s.top = mm_signed_word(get(b + 20, 4, big));
    s.bottom = mm_signed_word(get(b + 24, 4, big));
    s.child = get(b + 28, 4, big);
    s.story = b[32];
    *out = s;
    return 1;
}
int mm_text_box_encode(const MMTextBoxState *s, int system, int big, unsigned char b[36])
{
    if (s->first == (-2147483647L - 1))
        return 0;
    memset(b, 0, 36);
    put(b, 1, 2, big);
    put(b + 2, (MMU32)system, 2, big);
    put(b + 4, (MMU32)s->first, 4, big);
    put(b + 8, (MMU32)s->end, 4, big);
    put(b + 12, (MMU32)s->left, 4, big);
    put(b + 16, (MMU32)s->right, 4, big);
    put(b + 20, (MMU32)s->top, 4, big);
    put(b + 24, (MMU32)s->bottom, 4, big);
    put(b + 28, s->child, 4, big);
    b[32] = (unsigned char)(s->story != 0);
    return 1;
}
int mm_text_box_read(MMTextBoxState *state, MMU32 number, MMTextChunk *child, const MMTextBoxServices *s)
{
    MMReadStream stream;
    MMTextBoxState loaded;
    MMTextChunk found;
    int r;
    r = s->read(s->context, number, &stream);
    if (r <= 0)
        return r;
    if (!mm_text_box_decode(&stream, &loaded))
        return 0;
    if (child)
    {
        r = s->child(s->context, number, loaded.child, &found);
        if (r <= 0)
            return r;
    }
    *state = loaded;
    if (child)
        *child = found;
    return 1;
}
int mm_text_box_write(const MMTextBoxState *state, MMU32 number, int system, int big, const MMTextBoxServices *s)
{
    unsigned char b[36];
    if (!mm_text_box_encode(state, system, big, b))
        return 0;
    return s->put(s->context, number, b);
}
static int valid(const MMRichTextDefaults *s)
{
    return s->height >= 4 && s->height < 256 && s->width >= 1 && s->width < MM_TEXT_LIMIT;
}
int mm_rich_text_init(MMRichTextState *s, unsigned unit, int system)
{
    if (!s || (unit != 1 && unit != 2))
        return 0;
    memset(s, 0, sizeof(*s));
    s->defaults.order = 1;
    s->defaults.font_system = system;
    s->defaults.width = 640;
    s->defaults.height = 12;
    s->text_system = s->property_system = s->argument_system = system;
    s->property_order = s->argument_order = 1;
    mm_movie_title_init(&s->font, unit);
    return 1;
}
int mm_rich_text_decode(const MMReadStream *stream, MMRichTextDefaults *out)
{
    unsigned char b[16];
    MMRichTextDefaults s;
    int big;
    if (stream->length < 16 || !mm_read_exact(stream, 0, b, 16) || (big = order(b)) < 0)
        return 0;
    s.order = 1;
    s.font_system = signed_short(get(b + 2, 2, big));
    s.width = mm_signed_word(get(b + 4, 4, big));
    s.height = mm_signed_word(get(b + 8, 4, big));
    s.background = get(b + 12, 4, big);
    if (!valid(&s))
        return 0;
    *out = s;
    return 1;
}
int mm_rich_text_encode(const MMRichTextDefaults *s, int big, unsigned char b[16])
{
    if (!valid(s))
        return 0;
    put(b, 1, 2, big);
    put(b + 2, (MMU32)s->font_system, 2, big);
    put(b + 4, (MMU32)s->width, 4, big);
    put(b + 8, (MMU32)s->height, 4, big);
    put(b + 12, s->background, 4, big);
    return 1;
}
void mm_rich_text_dispose(MMRichTextState *s, const MMRichTextObjects *o)
{
    if (s->text.file)
        o->release(o->context, MM_TEXT_BYTES, s->text.file);
    if (s->properties)
        o->release(o->context, MM_TEXT_PROPERTIES, s->properties);
    if (s->arguments)
        o->release(o->context, MM_TEXT_ARGUMENTS, s->arguments);
    s->text.file = s->properties = s->arguments = 0;
}
int mm_rich_text_read(MMRichTextState *state, MMU32 type, MMU32 number, int system, int big,
                      const MMRichTextReadServices *s)
{
    MMRichTextState loaded;
    MMReadStream stream;
    MMTextLocation text;
    unsigned char marker[2];
    MMU32 child;
    int r, result = 0;
    const MMRichTextObjects *o = &s->objects;
    mm_rich_text_init(&loaded, state->font.unit, system);
    r = s->root(s->context, type, number, &stream);
    if (r <= 0)
    {
        result = r;
        goto done;
    }
    if (!mm_rich_text_decode(&stream, &loaded.defaults))
        goto done;
    r = s->font(s->context, &loaded.font);
    if (r < 0)
    {
        result = r;
        goto done;
    }
    if (!r || !loaded.font.count)
    {
        loaded.defaults.font_system = system;
        mm_movie_title_init(&loaded.font, state->font.unit);
    }
    r = s->child(s->context, type, number, MM_TEXT_BYTES, &child);
    if (r <= 0)
    {
        result = r;
        goto done;
    }
    r = s->location(s->context, MM_TEXT_BYTES, child, &text);
    if (r <= 0)
    {
        result = r;
        goto done;
    }
    loaded.text = text;
    o->retain_file(o->context, text.file);
    if (text.bytes < 2)
        goto done;
    r = s->read_text(s->context, &loaded.text, marker, 2);
    if (r <= 0)
    {
        result = r;
        goto done;
    }
    loaded.text_system = signed_short(get(marker, 2, big));
    loaded.text.offset += 2;
    loaded.text.bytes -= 2;
    r = s->child(s->context, type, number, MM_TEXT_PROPERTIES, &child);
    if (r <= 0)
    {
        result = r;
        goto done;
    }
    r = s->group(s->context, MM_TEXT_PROPERTIES, child, &loaded.properties, &loaded.property_order,
                 &loaded.property_system);
    if (r <= 0)
    {
        result = r;
        goto done;
    }
    if (!loaded.properties || o->width(o->context, MM_TEXT_PROPERTIES, loaded.properties) != 8)
        goto done;
    r = s->child(s->context, type, number, MM_TEXT_ARGUMENTS, &child);
    if (r < 0)
    {
        result = r;
        goto done;
    }
    if (r)
    {
        r = s->group(s->context, MM_TEXT_ARGUMENTS, child, &loaded.arguments, &loaded.argument_order,
                     &loaded.argument_system);
        if (r <= 0)
        {
            result = r;
            goto done;
        }
        if (!loaded.arguments || o->width(o->context, MM_TEXT_ARGUMENTS, loaded.arguments) != 4)
            goto done;
    }
    mm_rich_text_dispose(state, o);
    *state = loaded;
    return 1;
done:
    mm_rich_text_dispose(&loaded, o);
    return result;
}
int mm_rich_text_create(const MMRichTextDefaults *defaults, const MMMovieTitle *font, MMTextChunk *written, int big,
                        const MMRichTextWriteServices *s)
{
    unsigned char b[16];
    int r;
    if (!mm_rich_text_encode(defaults, big, b))
        return 0;
    written->type = MM_TEXT_ROOT;
    r = s->add_root(s->context, 16 + s->font_bytes(s->context, font), &written->number);
    if (r <= 0)
    {
        written->number = MM_TEXT_NIL;
        return r;
    }
    r = s->write(s->context, 0, b, 16);
    if (r > 0)
        r = s->font(s->context, font);
    if (r > 0)
        return 1;
    s->remove(s->context, written->number);
    written->number = MM_TEXT_NIL;
    return r;
}
int mm_rich_text_write_encoded(const MMRichTextState *state, const MMTextEncoded *replacement,
                               MMTextChunk *written, int big, const MMRichTextWriteServices *s)
{
    const MMRichTextObjects *o = &s->objects;
    MMTextLocation destination;
    unsigned char marker[2];
    MMU32 bytes = replacement ? replacement->bytes : (MMU32)state->text.bytes;
    int encoding = replacement ? replacement->encoding : state->text_system;
    int r;
    if (bytes > MM_TEXT_LIMIT - 2 || (replacement && bytes && !replacement->text) || !state->properties ||
        o->width(o->context, MM_TEXT_PROPERTIES, state->properties) != 8 ||
        (state->arguments && o->width(o->context, MM_TEXT_ARGUMENTS, state->arguments) != 4))
        return 0;
    r = mm_rich_text_create(&state->defaults, &state->font, written, big, s);
    if (r <= 0)
        return r;
    r = s->add_child(s->context, written->number, MM_TEXT_BYTES, bytes + 2);
    if (r <= 0)
        goto fail;
    put(marker, (MMU32)encoding, 2, big);
    r = s->write(s->context, 0, marker, 2);
    if (r <= 0)
        goto fail;
    if (bytes && replacement)
    {
        r = s->write(s->context, 2, replacement->text, bytes);
        if (r <= 0)
            goto fail;
    }
    else if (bytes)
    {
        memset(&destination, 0, sizeof(destination));
        r = s->destination(s->context, &destination);
        if (r > 0)
        {
            destination.offset += 2;
            destination.bytes -= 2;
            r = s->copy(s->context, &state->text, &destination);
        }
        if (destination.file)
            o->release(o->context, MM_TEXT_BYTES, destination.file);
        if (r <= 0)
            goto fail;
    }
    r = s->add_child(s->context, written->number, MM_TEXT_PROPERTIES,
                     s->group_bytes(s->context, MM_TEXT_PROPERTIES, state->properties));
    if (r <= 0)
        goto fail;
    r = s->group(s->context, MM_TEXT_PROPERTIES, state->properties, state->property_order, state->property_system);
    if (r <= 0)
        goto fail;
    if (state->arguments && s->count(s->context, state->arguments) > 0)
    {
        r = s->add_child(s->context, written->number, MM_TEXT_ARGUMENTS,
                         s->group_bytes(s->context, MM_TEXT_ARGUMENTS, state->arguments));
        if (r <= 0)
            goto fail;
        r = s->group(s->context, MM_TEXT_ARGUMENTS, state->arguments, state->argument_order, state->argument_system);
        if (r <= 0)
            goto fail;
    }
    return 1;
fail:
    s->remove(s->context, written->number);
    written->number = MM_TEXT_NIL;
    return r;
}

int mm_rich_text_write(const MMRichTextState *state, MMTextChunk *written, int big, const MMRichTextWriteServices *s)
{
    return mm_rich_text_write_encoded(state, NULL, written, big, s);
}

struct MMTextCollection
{
    MMCollectionStorage storage;
    MMU32 kind;
    int big;
    union
    {
        MMArray array;
        MMGroup group;
    } rows;
};

static int native_big(void)
{
    const unsigned short one = 1;
    return *(const unsigned char *)&one == 0;
}

MMTextCollection *mm_text_collection_new(MMU32 kind)
{
    MMTextCollection *c;
    if (kind != MM_TEXT_PROPERTIES && kind != MM_TEXT_ARGUMENTS)
        return NULL;
    c = calloc(1, sizeof(*c));
    if (!c)
        return NULL;
    mm_storage_init(&c->storage, NULL);
    c->kind = kind;
    c->big = native_big();
    if (kind == MM_TEXT_PROPERTIES)
    {
        c->rows.array.storage = &c->storage;
        c->rows.array.width = 8;
        c->rows.array.free_count = -1;
    }
    else
    {
        c->rows.group.storage = &c->storage;
        c->rows.group.fixed_bytes = 4;
    }
    return c;
}

void mm_text_collection_dispose(MMTextCollection *c)
{
    if (c)
    {
        mm_storage_dispose(&c->storage);
        free(c);
    }
}

int mm_text_collection_read(MMU32 kind, const MMReadStream *stream, MMTextCollection **out, int *order, int *system)
{
    MMCollectionHeader h;
    MMTextCollection *c;
    int group = kind == MM_TEXT_ARGUMENTS, status, swapped;
    status = mm_collection_header_read(stream, group ? MM_COLLECTION_GROUP : MM_COLLECTION_ARRAY, group, &h);
    if (status != MM_STORAGE_OK || h.width != (group ? 4U : 8U))
        return 0;
    c = mm_text_collection_new(kind);
    if (!c)
        return -1;
    status = mm_storage_read(&c->storage, stream, h.header_bytes, h.first_bytes, h.second_bytes);
    if (status != MM_STORAGE_OK)
    {
        mm_text_collection_dispose(c);
        return status == MM_STORAGE_MEMORY ? -1 : 0;
    }
    c->big = h.big_endian;
    swapped = c->big != native_big();
    if (group)
    {
        c->rows.group.count = h.count;
        c->rows.group.bytes = h.bytes;
        c->rows.group.free_count = h.free_count;
        if (swapped)
            mm_group_swap_locations(&c->rows.group);
    }
    else
        c->rows.array.count = h.count;
    *order = swapped ? 0x0100 : 1;
    /* Legacy collections publish the system field as native raw bytes. */
    *system = swapped ? ((h.system & 255) << 8) | (h.system >> 8) : h.system;
    *out = c;
    return 1;
}

static int text_collection_layout(const MMTextCollection *c, MMCollectionHeader *h)
{
    memset(h, 0, sizeof(*h));
    if (c->kind == MM_TEXT_PROPERTIES)
    {
        h->kind = MM_COLLECTION_ARRAY;
        h->width = c->rows.array.width;
        h->count = c->rows.array.count;
        h->free_count = -1;
    }
    else
    {
        h->kind = MM_COLLECTION_GROUP;
        h->width = c->rows.group.fixed_bytes;
        h->count = c->rows.group.count;
        h->bytes = c->rows.group.bytes;
        h->free_count = c->rows.group.free_count;
    }
    return mm_collection_layout(h) == MM_STORAGE_OK;
}

MMU32 mm_text_collection_bytes(const MMTextCollection *c)
{
    MMCollectionHeader h;
    return text_collection_layout(c, &h) ? h.header_bytes + h.first_bytes + h.second_bytes : 0;
}

int mm_text_collection_write(MMTextCollection *c, const MMStorageWriter *writer, int order, int system)
{
    MMCollectionHeader h;
    unsigned char header[20];
    int swapped = order == 0x0100, status;
    if (!text_collection_layout(c, &h))
        return 0;
    h.big_endian = native_big() != swapped;
    h.system = system & 65535;
    if (c->big != h.big_endian || mm_collection_header_write(&h, header, sizeof(header)) != MM_STORAGE_OK)
        return 0;
    if (swapped && c->kind == MM_TEXT_ARGUMENTS)
        mm_group_swap_locations(&c->rows.group);
    status = mm_storage_write(&c->storage, writer, header, h.header_bytes, h.first_bytes, h.second_bytes);
    if (swapped && c->kind == MM_TEXT_ARGUMENTS)
        mm_group_swap_locations(&c->rows.group);
    return status == MM_STORAGE_OK;
}

MMI32 mm_text_collection_width(const MMTextCollection *c)
{
    return c->kind == MM_TEXT_PROPERTIES ? (MMI32)c->rows.array.width : (MMI32)c->rows.group.fixed_bytes;
}

MMU32 mm_text_collection_count(const MMTextCollection *c)
{
    if (!c)
        return 0;
    return c->kind == MM_TEXT_PROPERTIES ? c->rows.array.count : c->rows.group.count;
}

int mm_text_collection_property(const MMTextCollection *c, MMU32 row, MMU32 words[2])
{
    const unsigned char *p;
    if (!c || c->kind != MM_TEXT_PROPERTIES || row >= c->rows.array.count)
        return 0;
    p = c->storage.buffers[0].data + row * 8;
    words[0] = get(p, 4, c->big);
    words[1] = get(p + 4, 4, c->big);
    return 1;
}

int mm_text_collection_put_property(MMTextCollection *c, MMU32 row, const MMU32 words[2], int insert)
{
    unsigned char wire[8];
    if (!c || c->kind != MM_TEXT_PROPERTIES)
        return 0;
    put(wire, words[0], 4, c->big);
    put(wire + 4, words[1], 4, c->big);
    if (insert)
        return mm_array_insert(&c->rows.array, row, wire, 1) == MM_STORAGE_OK;
    if (row >= c->rows.array.count)
        return 0;
    memcpy(c->storage.buffers[0].data + row * 8, wire, sizeof(wire));
    return 1;
}

static unsigned char *argument_location(const MMTextCollection *c, MMU32 row, MMI32 *bytes)
{
    MMGroupLocation location;
    if (!c || c->kind != MM_TEXT_ARGUMENTS || mm_group_location(&c->rows.group, row, &location) != MM_STORAGE_OK)
        return NULL;
    *bytes = location.bytes - 4;
    return c->storage.buffers[0].data + (MMU32)location.offset;
}

const void *mm_text_collection_argument(const MMTextCollection *c, MMU32 row, MMI32 *bytes)
{
    unsigned char *p = argument_location(c, row, bytes);
    return p ? p + 4 : NULL;
}

MMU32 mm_text_collection_argument_ref(const MMTextCollection *c, MMU32 row)
{
    MMI32 bytes;
    const unsigned char *p = argument_location(c, row, &bytes);
    return p ? get(p, 4, c->big) : 0;
}

int mm_text_collection_put_argument_ref(MMTextCollection *c, MMU32 row, MMU32 ref)
{
    MMI32 bytes;
    unsigned char *p = argument_location(c, row, &bytes);
    if (!p)
        return 0;
    put(p, ref, 4, c->big);
    return 1;
}

int mm_text_collection_delete(MMTextCollection *c, MMU32 row)
{
    if (!c)
        return 0;
    if (c->kind == MM_TEXT_PROPERTIES)
        return mm_array_delete(&c->rows.array, row, 1) == MM_STORAGE_OK;
    return mm_group_delete(&c->rows.group, row) == MM_STORAGE_OK;
}
