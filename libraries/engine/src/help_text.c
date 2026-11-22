#include "mm/help_text.h"
#include "mm/chunk_storage.h"
#include "mm/wide.h"
#include <string.h>
static MMU32 get(const unsigned char *p, unsigned n, int big)
{
    MMU32 v = 0;
    unsigned i;
    for (i = 0; i < n; ++i)
        v |= (MMU32)p[big ? n - 1 - i : i] << (i * 8);
    return v;
}
static void put(unsigned char *p, MMU32 v, unsigned n, int big)
{
    unsigned i;
    for (i = 0; i < n; ++i)
        p[big ? n - 1 - i : i] = (unsigned char)(v >> (i * 8));
}
void mm_help_topic_init(MMHelpTopic *p)
{
    memset(p, 0, sizeof(*p));
    p->balloon = p->script = p->sound_number = MM_TEXT_NIL;
}
int mm_help_topic_decode(MMHelpTopic *p, const void *data, MMI32 bytes, int big)
{
    const unsigned char *b = (const unsigned char *)data;
    MMU32 v[8], order;
    unsigned i;
    if (bytes != 28 && bytes != 36)
        return 0;
    order = get(b, 2, big);
    if (order != 1 && order != 256)
        return 0;
    for (i = 0; i < 6; ++i)
        v[i] = get(b + 4 + i * 4, 4, order == 1 ? big : !big);
    v[6] = bytes == 36 ? get(b + 28, 4, big) : 0;
    v[7] = bytes == 36 ? get(b + 32, 4, big) : MM_TEXT_NIL;
    p->balloon = v[0];
    p->hid = mm_signed_word(v[1]);
    p->target = mm_signed_word(v[2]);
    p->script = v[3];
    p->width = mm_signed_word(v[4]);
    p->height = mm_signed_word(v[5]);
    p->sound_type = v[6];
    p->sound_number = v[7];
    return 1;
}
void mm_help_topic_encode(const MMHelpTopic *p, int system, int big, unsigned char b[36])
{
    MMU32 v[8];
    unsigned i;
    v[0] = p->balloon;
    v[1] = (MMU32)p->hid;
    v[2] = (MMU32)p->target;
    v[3] = p->script;
    v[4] = (MMU32)p->width;
    v[5] = (MMU32)p->height;
    v[6] = p->sound_type;
    v[7] = p->sound_number;
    put(b, 1, 2, big);
    put(b + 2, (MMU32)system, 2, big);
    for (i = 0; i < 8; ++i)
        put(b + 4 + i * 4, v[i], 4, big);
}
int mm_help_open_argument(MMI32 index, unsigned kind, int order, int system, int big, const MMHelpArgumentServices *s)
{
    MMGroup arguments;
    unsigned char *data;
    MMI32 bytes;
    MMU32 type, n, i;
    int r = s->base(s->context, index, kind, order, system);
    if (r)
        return r;
    arguments = s->arguments(s->context);
    data = mm_text_argument_payload(&arguments, index, &bytes);
    if (!data)
        return 0;
    if (kind == 64)
    {
        if (bytes < 5)
            return 0;
        if (order == 256)
            put(data + 1, get(data + 1, 4, !big), 4, big);
        return 1;
    }
    if (kind != 192 || bytes < 4)
        return 0;
    type = get(data, 4, order == 256 ? !big : big);
    if (order == 256)
        put(data, type, 4, big);
    n = type == MM_HELP_BITMAP || type == MM_HELP_EDIT ? 1 : type == MM_HELP_BUTTON ? 2 : 0;
    if (!n || (MMU32)bytes - 4 < n * 4)
        return 0;
    if (order == 256)
        for (i = 1; i <= n; ++i)
            put(data + i * 4, get(data + i * 4, 4, !big), 4, big);
    return 1;
}
int mm_help_group(MMI32 first, MMI32 end, unsigned group, MMU32 topic, int big, const MMHelpEditServices *s)
{
    unsigned char bytes[527];
    MMTextPropertyChange change;
    MMI32 swap, count = 0, reference = 0;
    int r, t;
    if (first > end)
    {
        swap = first;
        first = end;
        end = swap;
    }
    if (first == end)
        return 1;
    r = s->undo(s->context, first, end, end - first);
    if (r <= 0)
        return r;
    change.kind = 64;
    change.value = 0;
    change.mask = 0xffffffffUL;
    if (group)
    {
        bytes[0] = (unsigned char)group;
        put(bytes + 1, topic, 4, big);
        r = s->group_name(s->context, bytes + 5, 522, &count);
        if (r < 0)
            return r;
        r = mm_text_argument_ensure(64, bytes, 5 + count, &reference, &s->properties.arguments);
        if (r < 0)
            return r;
        if (!r)
            goto fail;
        change.value = (MMU32)reference;
    }
    r = s->reserve(s->context, 2);
    if (r < 0)
        return r;
    if (!r)
    {
        mm_text_property_ref(64, reference, 0, &s->properties.arguments);
        goto fail;
    }
    r = mm_text_properties_apply_changes(first, end - first, s->characters(s->context), &change, 1, &s->properties);
    if (r < 0)
        return r;
    r = s->commit(s->context);
    if (r <= 0)
        return r;
    s->validate(s->context);
    return s->notify(s->context, first, end - first);
fail:
    t = s->cancel(s->context);
    return t < 0 ? t : 0;
}
int mm_help_grouped(MMI32 cp, int big, int name, const MMHelpEditServices *s)
{
    MMTextPropertyEntry entry;
    MMGroup rows;
    const unsigned char *data;
    MMI32 end, bytes;
    unsigned group = 0;
    int r;
    if (!mm_text_property_find(0x40000000UL | (MMU32)cp, s->characters(s->context), &entry, &end, 0, &s->properties))
    {
        entry.value = 0;
        entry.key = 0;
    }
    s->publish(s->context, MM_HELP_END, (MMU32)end);
    if (entry.value > 0)
    {
        rows = s->arguments(s->context);
        if (mm_storage_lock(rows.storage, 0) != MM_STORAGE_OK)
        {
            s->bad_group(s->context);
            goto none;
        }
        data = mm_text_argument_payload(&rows, entry.value - 1, &bytes);
        if (!data || bytes < 5 || !(group = data[0]))
        {
            s->bad_group(s->context);
            mm_storage_unlock(rows.storage, 0);
            goto none;
        }
        s->publish(s->context, MM_HELP_TARGET, get(data + 1, 4, big));
        r = name ? (bytes > 5 ? s->read_name(s->context, data + 5, bytes - 5) : s->clear_name(s->context)) : 1;
        mm_storage_unlock(rows.storage, 0);
        if (r < 0)
            return r;
    }
    else
    {
    none:
        s->publish(s->context, MM_HELP_TARGET, MM_TEXT_NIL);
        r = name ? s->clear_name(s->context) : 1;
        if (r < 0)
            return r;
    }
    s->publish(s->context, MM_HELP_GROUP, group);
    s->publish(s->context, MM_HELP_FIRST, entry.key & 0xffffffUL);
    return group != 0;
}
int mm_help_insert(int button, MMU32 number, MMU32 topic, const void *extra, MMI32 bytes, MMI32 at, MMI32 deleted,
                   int big, const MMHelpEditServices *s)
{
    unsigned char header[12];
    void *owned = 0;
    const void *data = header;
    MMI32 n = button ? 12 : 8;
    int r;
    put(header, button ? MM_HELP_BUTTON : MM_HELP_BITMAP, 4, big);
    put(header + 4, number, 4, big);
    if (button)
        put(header + 8, topic, 4, big);
    if (bytes > 0)
    {
        r = s->allocate(s->context, n + bytes, &owned);
        if (r <= 0)
        {
            if (owned)
                s->free(s->context, owned);
            return r;
        }
        memcpy(owned, header, (size_t)n);
        memcpy((unsigned char *)owned + n, extra, (size_t)bytes);
        data = owned;
    }
    r = s->insert(s->context, data, n + bytes, at, deleted);
    if (owned)
        s->free(s->context, owned);
    return r;
}
int mm_help_topic_set(MMHelpTopic *out, const MMHelpTopic *in, const MMHelpEditServices *s)
{
    *out = *in;
    return s->dirty(s->context);
}
int mm_help_read(MMHelpTopic *out, void *file, MMU32 type, MMU32 number, MMU32 flags, int strings, unsigned unit,
                 int big, const MMHelpReadServices *s)
{
    void *owned = 0;
    unsigned char bytes[36];
    MMHelpTopic topic;
    MMTextChunk child;
    MMMovieTitle text;
    MMI32 count, cp, limit, first, id;
    MMU32 ch;
    int r;
    r = s->forest(s->context, file, type, number);
    if (r < 0)
        return r;
    if (r)
    {
        r = s->open_forest(s->context, file, type, number, &owned);
        if (r <= 0)
            goto done;
        r = s->first(s->context, owned, type, &number);
        if (r <= 0)
            goto done;
    }
    else
    {
        s->retain(s->context, file);
        owned = file;
    }
    r = s->header(s->context, owned, type, number, bytes, &count);
    if (r <= 0)
        goto done;
    if (!mm_help_topic_decode(&topic, bytes, count, big))
    {
        r = 0;
        goto done;
    }
    r = s->child(s->context, owned, type, number, &child);
    if (r <= 0)
        goto done;
    r = s->rich(s->context, owned, &child, (flags & 1) != 0);
    if (r <= 0)
        goto done;
    if ((flags & 2) && strings)
    {
        r = s->internal(s->context);
        if (r <= 0)
            goto done;
        r = s->suspend(s->context);
        if (r <= 0)
            goto done;
        mm_movie_title_init(&text, unit);
        limit = s->characters(s->context);
        cp = 0;
        while (cp < limit)
        {
            r = s->character(s->context, cp, &ch);
            if (r <= 0)
                goto done;
            if (ch != '~')
            {
                ++cp;
                continue;
            }
            first = cp++;
            id = 0;
            while (cp < limit)
            {
                r = s->character(s->context, cp, &ch);
                if (r <= 0)
                    goto done;
                if (ch < '0' || ch > '9')
                    break;
                id = mm_signed_word((MMU32)id * 10 + ch - '0');
                ++cp;
            }
            r = s->string(s->context, id, &text);
            if (r < 0)
                goto done;
            if (!r)
                s->missing(s->context);
            r = s->replace(s->context, &text, first, cp - first);
            if (r < 0)
                goto done;
            if (r)
            {
                cp = first + (MMI32)text.count;
                limit = s->characters(s->context);
            }
        }
    }
    *out = topic;
    s->validate(s->context);
    r = 1;
done:
    if (owned)
        s->release(s->context, owned);
    return r;
}
int mm_help_save(const MMHelpTopic *topic, MMU32 *type, MMU32 *number, int system, int big,
                 const MMArchiveWriter *archive, const MMHelpSaveServices *s)
{
    unsigned char bytes[36];
    MMTextChunk child;
    MMChunkChild link;
    MMOwnedBlock block;
    MMBlockIO io;
    int r;
    *type = MM_HELP_TOPIC;
    if (!archive->payloads ||
        mm_owned_block_init(&block, archive->payloads->handles, archive->payloads->files) != MM_DATA_OK)
        goto fail;
    mm_help_topic_encode(topic, system, big, bytes);
    if (mm_archive_add_block(archive, *type, sizeof(bytes), number, &block) != MM_CHUNK_OK)
    {
        mm_owned_block_clear(&block);
        goto fail;
    }
    r = mm_owned_block_io(&block, &io) == MM_DATA_OK &&
        mm_block_write(&io, 0, bytes, sizeof(bytes), 0) == MM_DATA_OK;
    mm_owned_block_clear(&block);
    if (!r)
        goto remove_root;
    r = s->rich(s->context, &child);
    if (r < 0)
        return r;
    if (!r)
        goto remove_root;
    link.type = child.type;
    link.number = child.number;
    link.slot = 0;
    if (mm_chunk_adopt(&archive->edit, *type, *number, &link, 1) == MM_CHUNK_OK)
        return 1;
    mm_chunk_delete(&archive->edit, child.type, child.number);
remove_root:
    mm_chunk_delete(&archive->edit, *type, *number);
fail:
    s->error(s->context);
    return 0;
}
