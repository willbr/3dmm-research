#include "mm/text_materialize.h"
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
int mm_text_open_font_argument(unsigned unit, MMI32 index, unsigned kind, const MMTextFontArgumentServices *s)
{
    MMI32 bytes, font;
    MMMovieTitle name;
    const unsigned char *data;
    int result, system;
    bytes = s->bytes(s->context, index);
    if (kind != 2)
        return 0;
    if (bytes < 6)
    {
        s->diagnostic(s->context);
        return 0;
    }
    system = s->system(s->context, index);
    data = (const unsigned char *)s->lock(s->context, index);
    mm_movie_title_init(&name, unit);
    result = s->decode(s->context, data + 6, bytes - 6, &name);
    if (!result)
        s->diagnostic(s->context);
    s->unlock(s->context);
    if (result <= 0)
        return result;
    result = s->map(s->context, &name, system, &font);
    if (result > 0)
        s->put(s->context, index, font);
    return result;
}
