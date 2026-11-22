#include "mm/text_store.h"
#include "mm/chunk_owner.h"
#include "mm/text_properties.h"
#include "mm/movie_archive_edit.h"
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
int mm_text_box_read(MMTextBoxState *state, MMU32 number, MMTextChunk *child, const MMMovieArchiveEditor *s)
{
    MMReadStream stream;
    MMTextBoxState loaded;
    MMTextChunk found;
    int r;
    r = s->read(s->context, MM_TEXT_BOX, number, &stream);
    if (r <= 0)
        return r;
    if (!mm_text_box_decode(&stream, &loaded))
        return 0;
    if (child)
    {
        MMChunkAccess chunks = mm_chunk_edit_access(&s->edit);
        found.type = MM_TEXT_ROOT;
        if (mm_chunk_link_number(&chunks, MM_TEXT_BOX, number, loaded.child, found.type, &found.number) != MM_CHUNK_OK)
            return 0;
    }
    *state = loaded;
    if (child)
        *child = found;
    return 1;
}
int mm_text_box_write(const MMTextBoxState *state, MMU32 number, const MMMovieArchiveEditor *s)
{
    unsigned char b[36];
    if (!mm_text_box_encode(state, s->system, s->big_endian, b))
        return 0;
    return s->put(s->context, MM_TEXT_BOX, number, b, sizeof(b));
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

int mm_rich_text_encoding(const unsigned char marker[2], int big)
{
    return signed_short(get(marker, 2, big));
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
    loaded.text_system = mm_rich_text_encoding(marker, big);
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
static int write_text_range(const MMTextEncoded *text, const MMRichTextWriteServices *s)
{
    unsigned char buffer[4096];
    MMU32 offset, count;
    int result;
    if (text->text)
        return s->write(s->context, 2, text->text, text->bytes);
    for (offset = 0; offset < text->bytes; offset += count)
    {
        count = text->bytes - offset;
        if (count > sizeof(buffer))
            count = sizeof(buffer);
        if (!mm_read_exact(text->stream, offset, buffer, count))
            return 0;
        result = s->write(s->context, 2 + offset, buffer, count);
        if (result <= 0)
            return result;
    }
    return 1;
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
    if (bytes > MM_TEXT_LIMIT - 2 ||
        (replacement && bytes && !replacement->text && (!replacement->stream || replacement->stream->length != bytes)) ||
        !state->properties ||
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
        r = write_text_range(replacement, s);
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

int mm_rich_text_file_location(const MMRichTextArchive *archive, MMU32 number, MMTextLocation *out)
{
    MMChunkOwnerRead *owner = archive->owner;
    MMBlockFile extent;
    int packed;
    if (!owner || !archive->files || !owner->payloads.locate(owner, MM_TEXT_BYTES, number, &extent, &packed) ||
        !extent.file ||
        extent.length > 0x7fffffffUL || extent.offset > 0x7fffffffUL - extent.length)
        return 0;
    out->file = extent.file;
    out->offset = extent.offset;
    out->bytes = (MMI32)extent.length;
    return 1;
}

int mm_rich_text_file_read(const MMRichTextArchive *archive, const MMTextLocation *text,
                           MMU32 offset, void *out, size_t bytes)
{
    const MMBlockFileServices *files = archive->files;
    if (text->bytes < 0 || offset > (MMU32)text->bytes || bytes > (MMU32)text->bytes - offset)
        return 0;
    if (!bytes)
        return 1;
    if (!files || !text->file || text->offset > 0x7fffffffUL - (MMU32)text->bytes)
        return 0;
    return files->read(files->context, text->file, text->offset + offset, out, bytes);
}

MMRichTextArchive mm_rich_text_file_archive(MMChunkOwnerRead *owner, const MMBlockFileServices *fallback_files,
                                           const MMMovieArchiveEditor *editor, int swapped_unicode_encoding)
{
    MMRichTextArchive archive = {0};
    archive.editor = editor;
    archive.owner = owner;
    archive.files = owner ? owner->payloads.files : fallback_files;
    archive.swapped_unicode_encoding = swapped_unicode_encoding;
    archive.location = mm_rich_text_file_location;
    archive.read_at = mm_rich_text_file_read;
    return archive;
}

static int archive_text_range(void *p, MMU32 offset, void *out, size_t bytes)
{
    const MMRichTextStream *text = p;
    const MMRichTextArchive *archive = text->archive;
    return archive->read_at(archive, text->location, offset, out, bytes) > 0;
}

void mm_rich_text_archive_stream(MMRichTextStream *out, const MMRichTextArchive *archive, const MMTextLocation *text)
{
    out->archive = archive;
    out->location = text;
    out->stream.context = out;
    out->stream.length = (MMU32)text->bytes;
    out->stream.read_at = archive_text_range;
}

static void archive_retain_file(void *p, void *file)
{
    const MMRichTextArchive *archive = p;
    archive->files->retain(archive->files->context, file);
}

static void archive_release(void *p, MMU32 kind, void *object)
{
    const MMRichTextArchive *archive = p;
    if (kind == MM_TEXT_BYTES)
        archive->files->release(archive->files->context, object);
    else if (archive->collections)
        archive->collections->release(archive->context, kind, object);
    else
        mm_text_collection_dispose(object);
}

static MMI32 archive_collection_width(void *p, MMU32 kind, void *group)
{
    const MMRichTextArchive *archive = p;
    if (archive->collections)
        return archive->collections->width(archive->context, kind, group);
    return mm_text_collection_width(group);
}

static MMRichTextObjects archive_objects(const MMRichTextArchive *archive)
{
    MMRichTextObjects objects = {0};
    objects.context = (void *)archive;
    objects.retain_file = archive_retain_file;
    objects.release = archive_release;
    objects.width = archive_collection_width;
    return objects;
}

void mm_rich_text_archive_dispose(MMRichTextState *state, const MMRichTextArchive *archive)
{
    const MMRichTextObjects objects = archive_objects(archive);
    mm_rich_text_dispose(state, &objects);
}

static MMI32 archive_convert_font(void *p, const void *in, MMU32 count, int encoding, void *out, MMU32 capacity)
{
    const MMMovieArchiveEditor *editor = p;
    return (MMI32)editor->convert_text(editor->context, in, count, encoding, out, capacity);
}

static MMStringWire archive_font_wire(const MMRichTextArchive *archive, const MMMovieTitle *font, void *counted)
{
    const MMMovieArchiveEditor *editor = archive->editor;
    MMStringWire wire;
    wire.buffer = counted;
    wire.unit = font->unit;
    wire.current_encoding = editor->system;
    wire.swapped_unicode_encoding = archive->swapped_unicode_encoding;
    wire.native_big_endian = editor->big_endian;
    wire.context = (void *)editor;
    wire.convert = archive_convert_font;
    return wire;
}

static int archive_rich_root(void *p, MMU32 type, MMU32 number, MMReadStream *out)
{
    MMRichTextArchiveReader *read = p;
    const MMMovieArchiveEditor *editor = read->archive->editor;
    int result = editor->read(editor->context, type, number, &read->root);
    if (result > 0)
        *out = read->root;
    return result;
}

static int archive_rich_read_font(void *p, MMMovieTitle *font)
{
    const MMRichTextArchiveReader *read = p;
    unsigned short counted[MM_STRING_MAX + 2];
    const unsigned char *raw = (const unsigned char *)counted;
    const MMStringWire wire = archive_font_wire(read->archive, font, counted);
    if (!mm_string_wire_read(&wire, &read->root, 16, NULL))
        return 0;
    return mm_movie_title_set(font, raw + font->unit, font->unit == 1 ? raw[0] : counted[0]);
}

static int archive_rich_child(void *p, MMU32 type, MMU32 number, MMU32 kind, MMU32 *out)
{
    const MMMovieArchiveEditor *editor = ((const MMRichTextArchiveReader *)p)->archive->editor;
    const MMChunkAccess chunks = mm_chunk_edit_access(&editor->edit);
    return mm_chunk_link_number(&chunks, type, number, 0, kind, out) == MM_CHUNK_OK;
}

static int archive_rich_location(void *p, MMU32 type, MMU32 number, MMTextLocation *out)
{
    const MMRichTextArchive *archive = ((const MMRichTextArchiveReader *)p)->archive;
    return type == MM_TEXT_BYTES ? archive->location(archive, number, out) : 0;
}

static int archive_rich_read_text(void *p, const MMTextLocation *location, void *out, MMU32 bytes)
{
    const MMRichTextArchive *archive = ((const MMRichTextArchiveReader *)p)->archive;
    return archive->read_at(archive, location, 0, out, bytes);
}

static int archive_rich_read_group(void *p, MMU32 kind, MMU32 number, void **out, int *order, int *system)
{
    const MMRichTextArchive *archive = ((const MMRichTextArchiveReader *)p)->archive;
    const MMMovieArchiveEditor *editor = archive->editor;
    MMReadStream stream;
    MMTextCollection *collection;
    int result = editor->read(editor->context, kind, number, &stream);
    if (result <= 0)
        return result;
    if (archive->collections)
        return archive->collections->read(archive->context, kind, &stream, out, order, system);
    result = mm_text_collection_read(kind, &stream, &collection, order, system);
    if (result > 0)
        *out = collection;
    return result;
}

void mm_rich_text_archive_reader(MMRichTextArchiveReader *read, const MMRichTextArchive *archive,
                                 MMRichTextReadServices *services)
{
    read->archive = archive;
    memset(services, 0, sizeof(*services));
    services->context = read;
    services->objects = archive_objects(archive);
    services->root = archive_rich_root;
    services->font = archive_rich_read_font;
    services->child = archive_rich_child;
    services->location = archive_rich_location;
    services->read_text = archive_rich_read_text;
    services->group = archive_rich_read_group;
}

int mm_rich_text_archive_read(MMRichTextState *state, const MMTextChunk *chunk, const MMRichTextArchive *archive)
{
    MMRichTextArchiveReader read;
    MMRichTextReadServices services;
    const MMMovieArchiveEditor *editor = archive->editor;
    mm_rich_text_archive_reader(&read, archive, &services);
    return mm_rich_text_read(state, chunk->type, chunk->number, editor->system, editor->big_endian, &services);
}

static MMU32 archive_rich_font_bytes(void *p, const MMMovieTitle *font)
{
    (void)p;
    return 2 + (font->count + 2) * font->unit;
}

static int archive_rich_add_root(void *p, MMU32 bytes, MMU32 *out)
{
    MMRichTextArchiveWriter *write = p;
    const MMMovieArchiveEditor *editor = write->archive->editor;
    return editor->add(editor->context, MM_TEXT_ROOT, bytes, out, &write->writer);
}

static int archive_rich_put(void *p, MMU32 offset, const void *in, MMU32 bytes)
{
    const MMStorageWriter *writer = &((const MMRichTextArchiveWriter *)p)->writer;
    return writer->write_at(writer->context, offset, in, bytes);
}

static int archive_rich_write_font(void *p, const MMMovieTitle *font)
{
    const MMRichTextArchiveWriter *write = p;
    unsigned short counted[MM_STRING_MAX + 2];
    unsigned char *raw = (unsigned char *)counted;
    const MMStringWire wire = archive_font_wire(write->archive, font, counted);
    MMStringWriter writer;
    if (font->unit == 1)
        raw[0] = (unsigned char)font->count;
    else
        counted[0] = (unsigned short)font->count;
    memcpy(raw + font->unit, font->text, (font->count + 1) * font->unit);
    writer.context = write->writer.context;
    writer.length = write->writer.length;
    writer.write_at = write->writer.write_at;
    return mm_string_wire_write(&wire, &writer, 16);
}

static void archive_rich_remove(void *p, MMU32 number)
{
    const MMMovieArchiveEditor *editor = ((const MMRichTextArchiveWriter *)p)->archive->editor;
    mm_chunk_delete(&editor->edit, MM_TEXT_ROOT, number);
}

static int archive_rich_add_child(void *p, MMU32 root, MMU32 kind, MMU32 bytes)
{
    MMRichTextArchiveWriter *write = p;
    const MMMovieArchiveEditor *editor = write->archive->editor;
    MMChunkChild child = {0};
    int result = editor->add(editor->context, kind, bytes, &child.number, &write->writer);
    if (result <= 0)
        return result;
    child.type = kind;
    if (mm_chunk_adopt(&editor->edit, MM_TEXT_ROOT, root, &child, 1) == MM_CHUNK_OK)
        return 1;
    mm_chunk_delete(&editor->edit, child.type, child.number);
    return 0;
}

static MMU32 archive_rich_group_bytes(void *p, MMU32 kind, void *collection)
{
    const MMRichTextArchive *archive = ((const MMRichTextArchiveWriter *)p)->archive;
    if (archive->collections)
        return archive->collections->bytes(archive->context, kind, collection);
    return mm_text_collection_bytes(collection);
}

static MMI32 archive_rich_group_count(void *p, void *collection)
{
    const MMRichTextArchive *archive = ((const MMRichTextArchiveWriter *)p)->archive;
    if (archive->collections)
        return archive->collections->count(archive->context, collection);
    return mm_text_collection_count(collection);
}

static int archive_rich_write_group(void *p, MMU32 kind, void *collection, int order, int system)
{
    const MMRichTextArchiveWriter *write = p;
    if (write->archive->collections)
        return write->archive->collections->write(write->archive->context, kind, &write->writer, collection, order, system);
    return mm_text_collection_write(collection, &write->writer, order, system);
}

void mm_rich_text_archive_writer(MMRichTextArchiveWriter *write, const MMRichTextArchive *archive,
                                 MMRichTextWriteServices *services)
{
    write->archive = archive;
    memset(services, 0, sizeof(*services));
    services->context = write;
    services->objects = archive_objects(archive);
    services->font_bytes = archive_rich_font_bytes;
    services->add_root = archive_rich_add_root;
    services->write = archive_rich_put;
    services->font = archive_rich_write_font;
    services->remove = archive_rich_remove;
    services->add_child = archive_rich_add_child;
    services->group_bytes = archive_rich_group_bytes;
    services->count = archive_rich_group_count;
    services->group = archive_rich_write_group;
}

int mm_rich_text_archive_write(const MMRichTextState *state, const MMTextEncoded *replacement, MMTextChunk *out,
                               const MMRichTextArchive *archive)
{
    MMRichTextArchiveWriter write;
    MMRichTextWriteServices services;
    MMRichTextStream source;
    MMTextEncoded stored = {0};
    mm_rich_text_archive_writer(&write, archive, &services);
    if (!replacement)
    {
        mm_rich_text_archive_stream(&source, archive, &state->text);
        stored.stream = &source.stream;
        stored.bytes = source.stream.length;
        stored.encoding = state->text_system;
        replacement = &stored;
    }
    return mm_rich_text_write_encoded(state, replacement, out, archive->editor->big_endian, &services);
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

static unsigned char *property_address(const MMArray *rows, MMU32 row)
{
    if (rows->width != 8 || rows->allocated || !rows->storage || row >= rows->count ||
        row >= rows->storage->buffers[0].capacity / 8)
        return NULL;
    return rows->storage->buffers[0].data + row * 8;
}

int mm_text_property_array_read(const MMArray *rows, MMU32 row, int big, MMU32 words[2])
{
    const unsigned char *p = property_address(rows, row);
    if (!p)
        return 0;
    words[0] = get(p, 4, big);
    words[1] = get(p + 4, 4, big);
    return 1;
}

int mm_text_property_array_write(MMArray *rows, MMU32 row, int big, const MMU32 words[2], int insert)
{
    unsigned char wire[8];
    unsigned char *p;
    if (rows->width != sizeof(wire) || rows->allocated)
        return 0;
    put(wire, words[0], 4, big);
    put(wire + 4, words[1], 4, big);
    if (insert)
        return mm_array_insert(rows, row, wire, 1) == MM_STORAGE_OK;
    p = property_address(rows, row);
    if (!p)
        return 0;
    memcpy(p, wire, sizeof(wire));
    return 1;
}

MMArray *mm_text_collection_property_array(MMTextCollection *c, int *big)
{
    if (!c || c->kind != MM_TEXT_PROPERTIES)
        return NULL;
    if (big)
        *big = c->big;
    return &c->rows.array;
}

int mm_text_collection_property(const MMTextCollection *c, MMU32 row, MMU32 words[2])
{
    return c && c->kind == MM_TEXT_PROPERTIES && mm_text_property_array_read(&c->rows.array, row, c->big, words);
}

int mm_text_collection_put_property(MMTextCollection *c, MMU32 row, const MMU32 words[2], int insert)
{
    return c && c->kind == MM_TEXT_PROPERTIES && mm_text_property_array_write(&c->rows.array, row, c->big, words, insert);
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
