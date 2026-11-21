#include "mm/text_materialize.h"
#include "mm/text_properties.h"
#include <string.h>
static MMU32 swap(MMU32 v)
{
    return (v >> 24) | ((v >> 8) & 0xff00UL) | ((v << 8) & 0xff0000UL) | (v << 24);
}
int mm_text_materialize(const MMTextDocumentConfig *config, MMU32 type, MMU32 number, int copy,
                        const MMTextMaterializeServices *s)
{
    MMRichTextState stored;
    MMTextLocation location;
    MMFilePiece piece;
    MMPieceTable *pieces;
    MMArray properties;
    MMGroup arguments;
    MMGroupLocation entry;
    void *original, *group;
    MMU32 i, words[2], fixed;
    unsigned char *bytes;
    int result, slot;
    result = s->init_base(s->context);
    if (result <= 0)
        return result;
    mm_rich_text_init(&stored, config->unit, config->system);
    result = mm_rich_text_read(&stored, type, number, config->system, config->big, &s->read);
    if (result <= 0)
        goto done;
    result = s->defaults(s->context, &stored.defaults, &stored.font);
    if (result <= 0)
        goto done;
    location = stored.text;
    original = location.file;
    s->base.objects.retain(s->base.objects.context, MM_TEXT_OWNER_FILE, original);
    result = s->base.translate(s->base.context, &location, stored.text_system);
    if (result > 0)
    {
        pieces = s->pieces(s->context);
        piece.file = location.file;
        piece.offset = location.offset;
        piece.length = (MMU32)location.bytes;
        result = mm_piece_table_replace_file(pieces, 0, pieces->length / config->unit - 1, &piece,
                                             copy && original == piece.file) == MM_DATA_OK;
    }
    if (location.file)
        s->base.objects.release(s->base.objects.context, MM_TEXT_OWNER_FILE, location.file);
    if (result <= 0)
        goto done;
    group = stored.properties;
    s->base.objects.set(s->base.objects.context, MM_TEXT_OWNER_PROPERTIES, group);
    stored.properties = 0;
    if (stored.property_order == 256)
    {
        properties = s->properties(group);
        bytes = properties.storage->buffers[0].data;
        for (i = 0; i < properties.count; ++i, bytes += sizeof(words))
        {
            memcpy(words, bytes, sizeof(words));
            words[0] = swap(words[0]);
            words[1] = swap(words[1]);
            memcpy(bytes, words, sizeof(words));
        }
    }
    group = stored.arguments;
    if (group)
    {
        s->base.objects.set(s->base.objects.context, MM_TEXT_OWNER_ARGUMENTS, group);
        stored.arguments = 0;
        arguments = s->arguments(group);
        for (i = arguments.count; i-- > 0; arguments = s->arguments(group))
        {
            slot = mm_group_read_slot(&arguments, i, &entry);
            if (slot == MM_GROUP_SLOT_FREE)
                continue;
            if (slot != MM_GROUP_SLOT_PRESENT)
            {
                result = 0;
                goto done;
            }
            bytes = arguments.storage->buffers[0].data + entry.offset;
            memcpy(&fixed, bytes, sizeof(fixed));
            if (stored.argument_order == 256)
            {
                fixed = swap(fixed);
                memcpy(bytes, &fixed, sizeof(fixed));
            }
            result =
                s->open_argument(s->context, i, (unsigned)(fixed >> 24), stored.argument_order, stored.argument_system);
            if (result <= 0)
                goto done;
        }
    }
    result = 1;
done:
    mm_rich_text_dispose(&stored, &s->read.objects);
    return result;
}
int mm_text_open_font_argument(MMI32 index, unsigned kind, const MMTextFontArgumentServices *s)
{
    MMGroup arguments;
    MMStringWire wire;
    MMMemoryStream memory;
    MMMovieTitle name;
    unsigned short counted[MM_STRING_MAX + 2];
    short system;
    MMU32 count;
    MMI32 bytes, font;
    unsigned char *data;
    int result;
    arguments = s->arguments(s->context);
    data = mm_text_argument_payload(&arguments, index, &bytes);
    if (!data || kind != 2)
        return 0;
    if (bytes < 6)
    {
        s->bad_entry(s->context);
        return 0;
    }
    if (mm_storage_lock(arguments.storage, 0) != MM_STORAGE_OK)
        return 0;
    memcpy(&system, data + 4, 2);
    wire = s->wire;
    wire.buffer = counted;
    result = mm_memory_stream_init(&memory, data + 6, bytes - 6) &&
             mm_string_wire_read(&wire, &memory.stream, 0, NULL);
    if (!result)
    {
        s->bad_string(s->context);
        s->bad_entry(s->context);
    }
    mm_storage_unlock(arguments.storage, 0);
    if (!result)
        return 0;
    mm_movie_title_init(&name, wire.unit);
    count = wire.unit == 1 ? *(const unsigned char *)counted : counted[0];
    mm_movie_title_set(&name, (const unsigned char *)counted + wire.unit, count);
    result = s->map(s->context, &name, system, &font);
    if (result <= 0)
        return result;
    arguments = s->arguments(s->context);
    data = mm_text_argument_payload(&arguments, index, &bytes);
    if (!data || bytes < (MMI32)sizeof(font))
        return 0;
    memcpy(data, &font, sizeof(font));
    return result;
}
