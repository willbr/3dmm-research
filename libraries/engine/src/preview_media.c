#include "mm/preview_media.h"
#include <string.h>
typedef struct PropertyKey
{
    MMU32 position, index;
} PropertyKey;
static MMU32 swapped(MMU32 x)
{
    return (x >> 24) | ((x >> 8) & 0xff00U) | ((x << 8) & 0xff0000U) | (x << 24);
}
static void reverse(const MMPreviewCaptionServices *s, MMU32 first, MMU32 end)
{
    while (first < end && first < --end)
        s->swap(s->context, first++, end);
}
static void merge(const MMPreviewCaptionServices *s, MMU32 first, MMU32 middle, MMU32 end)
{
    MMU32 a, b, low, high, probe, pivot, next;
    if (first == middle || middle == end || s->position(s->context, middle - 1) <= s->position(s->context, middle))
        return;
    if (end - first == 2)
    {
        s->swap(s->context, first, middle);
        return;
    }
    if (middle - first > end - middle)
    {
        a = first + (middle - first) / 2;
        pivot = s->position(s->context, a);
        low = middle;
        high = end;
        while (low < high)
        {
            probe = low + (high - low) / 2;
            if (s->position(s->context, probe) < pivot)
                low = probe + 1;
            else
                high = probe;
        }
        b = low;
    }
    else
    {
        b = middle + (end - middle) / 2;
        pivot = s->position(s->context, b);
        low = first;
        high = middle;
        while (low < high)
        {
            probe = low + (high - low) / 2;
            if (s->position(s->context, probe) <= pivot)
                low = probe + 1;
            else
                high = probe;
        }
        a = low;
    }
    next = a + (b - middle);
    reverse(s, a, middle);
    reverse(s, middle, b);
    reverse(s, a, b);
    merge(s, first, a, next);
    merge(s, next, b, end);
}
static void sort(MMU32 count, const MMPreviewCaptionServices *s)
{
    MMCollectionStorage scratch;
    PropertyKey *a, *b, *temp;
    MMU32 i, j, width, first, middle, end, left, right, out, swap;
    if (count < 2)
        return;
    mm_storage_init(&scratch, s->allocator);
    if (count >= MM_STORAGE_LIMIT / sizeof(*a) ||
        mm_storage_ensure(&scratch, count * sizeof(*a), count * sizeof(*a), 0) != MM_STORAGE_OK)
    {
        for (width = 1; width < count;)
        {
            for (first = 0; first < count; first = end)
            {
                middle = width < count - first ? first + width : count;
                end = width < count - middle ? middle + width : count;
                merge(s, first, middle, end);
            }
            if (width > count / 2)
                break;
            width *= 2;
        }
    }
    else
    {
        a = (PropertyKey *)scratch.buffers[0].data;
        b = (PropertyKey *)scratch.buffers[1].data;
        for (i = 0; i < count; ++i)
        {
            a[i].position = s->position(s->context, i);
            a[i].index = i;
        }
        for (width = 1; width < count;)
        {
            for (first = 0; first < count; first = end)
            {
                middle = width < count - first ? first + width : count;
                end = width < count - middle ? middle + width : count;
                left = first;
                right = middle;
                for (out = first; out < end; ++out)
                    b[out] = left < middle && (right == end || a[left].position <= a[right].position) ? a[left++]
                                                                                                      : a[right++];
            }
            temp = a;
            a = b;
            b = temp;
            if (width > count / 2)
                break;
            width *= 2;
        }
        for (i = 0; i < count; ++i)
            b[a[i].index].index = i;
        for (i = 0; i < count; ++i)
            while (b[i].index != i)
            {
                j = b[i].index;
                s->swap(s->context, i, j);
                swap = b[i].index;
                b[i].index = b[j].index;
                b[j].index = swap;
            }
    }
    mm_storage_dispose(&scratch);
}
int mm_preview_caption(MMU32 number, const MMPreviewCaptionServices *s)
{
    MMCollectionStorage scratch;
    MMPreviewCaptionData data;
    MMPreviewTextProperty property;
    MMU32 words[2], count, converted, i, index;
    MMI32 bytes;
    unsigned short *output;
    const void *font;
    MMU32 font_bytes;
    void *owner = 0;
    int r, result = 0, free_entry;
    mm_storage_init(&scratch, s->allocator);
    memset(&data, 0, sizeof(data));
    r = s->create(s->context, &owner);
    if (r <= 0)
    {
        result = r ? r : -1;
        goto done;
    }
    r = s->box(s->context, owner, number, &data);
    if (r <= 0)
    {
        result = r;
        goto done;
    }
    r = s->rich_text(s->context, owner, &data);
    if (r <= 0)
    {
        result = r;
        goto done;
    }
    if (data.bytes < 0 || data.bytes > 1024 * 1024)
        goto done;
    s->publish(s->context, &data);
    r = s->font(s->context, owner);
    if (r <= 0)
    {
        result = r;
        goto done;
    }
    if (mm_storage_resize_exact(&scratch, 0, (MMU32)data.bytes, MM_STORAGE_NORMAL) != MM_STORAGE_OK)
    {
        result = -1;
        goto done;
    }
    if (data.bytes)
    {
        r = s->read_text(s->context, owner, scratch.buffers[0].data, (MMU32)data.bytes);
        if (r <= 0)
        {
            result = r;
            goto done;
        }
    }
    count = 0;
    r = s->utf16(s->context, scratch.buffers[0].data, (MMU32)data.bytes, data.encoding, 0, 0, &count);
    if (r <= 0)
    {
        result = r;
        goto done;
    }
    if (data.bytes && !count)
        goto done;
    r = s->text_buffer(s->context, count, &output);
    if (r <= 0)
    {
        result = r;
        goto done;
    }
    if (count)
    {
        converted = 0;
        r = s->utf16(s->context, scratch.buffers[0].data, (MMU32)data.bytes, data.encoding, output, count, &converted);
        if (r <= 0)
        {
            result = r;
            goto done;
        }
        if (!converted)
            goto done;
    }
    for (i = 0; i < data.properties; ++i)
    {
        s->property(s->context, owner, i, words);
        if (data.property_swap)
        {
            words[0] = swapped(words[0]);
            words[1] = swapped(words[1]);
        }
        property.kind = words[0] >> 24;
        property.position = words[0] & 0xffffff;
        property.value = words[1];
        if (property.position > count)
            goto done;
        font = 0;
        font_bytes = 0;
        if (property.kind == 2 && mm_signed_word(property.value) > 0)
        {
            index = property.value - 1;
            if (!data.has_arguments || index >= data.arguments)
                goto done;
            s->argument_info(s->context, owner, index, &bytes, &free_entry);
            if (free_entry || bytes < 6)
                goto done;
            if (mm_storage_resize_exact(&scratch, 1, (MMU32)bytes, MM_STORAGE_NORMAL) != MM_STORAGE_OK)
            {
                result = -1;
                goto done;
            }
            s->argument(s->context, owner, index, scratch.buffers[1].data);
            font = scratch.buffers[1].data + 6;
            font_bytes = (MMU32)bytes - 6;
        }
        r = s->append(s->context, &property, font, font_bytes);
        if (r <= 0)
        {
            result = r;
            goto done;
        }
    }
    sort(data.properties, s);
    result = 1;
done:
    mm_storage_dispose(&scratch);
    if (owner)
        s->release(s->context, owner);
    return result;
}
static MMU32 word(const unsigned char *p, int big)
{
    return big ? ((MMU32)p[0] << 24) | ((MMU32)p[1] << 16) | ((MMU32)p[2] << 8) | p[3]
               : ((MMU32)p[3] << 24) | ((MMU32)p[2] << 16) | ((MMU32)p[1] << 8) | p[0];
}
int mm_preview_audio(MMU32 type, MMU32 number, const MMPreviewAudioServices *s)
{
    MMCollectionStorage scratch;
    MMMidiParser parser;
    MMMidiEvent event;
    unsigned char header[16], *wave;
    MMI32 bytes;
    MMU32 events = 0, message;
    int r, result = 0, big;
    mm_storage_init(&scratch, s->allocator);
    s->reset(s->context);
    if (type == MM_SOUND_TYPE)
    {
        r = s->find(s->context, type, number);
        if (r <= 0)
        {
            result = r;
            goto done;
        }
        r = s->unpack(s->context);
        if (r <= 0)
        {
            result = r;
            goto done;
        }
        if (s->size(s->context) != 16)
            goto done;
        r = s->read(s->context, header, 16);
        if (r <= 0)
        {
            result = r;
            goto done;
        }
        if (!((header[0] == 1 && !header[1]) || (!header[0] && header[1] == 1)))
            goto done;
        big = header[0] == 0;
        if (word(header + 12, big))
        {
            s->silent(s->context);
            result = 1;
            goto done;
        }
        r = s->child(s->context, number, &type, &number);
        if (r < 0)
        {
            result = r;
            goto done;
        }
        if (!r)
        {
            s->silent(s->context);
            result = 1;
            goto done;
        }
    }
    r = s->find(s->context, type, number);
    if (r <= 0)
    {
        result = r;
        goto done;
    }
    r = s->unpack(s->context);
    if (r <= 0)
    {
        result = r;
        goto done;
    }
    bytes = s->size(s->context);
    if (bytes < 0 || bytes > 64 * 1024 * 1024)
        goto done;
    s->publish(s->context, type, number, type == MM_SOUND_MIDI);
    if (type == MM_SOUND_MIDI)
    {
        if (mm_storage_resize_exact(&scratch, 0, (MMU32)bytes, MM_STORAGE_NORMAL) != MM_STORAGE_OK)
            goto done;
        if (bytes)
        {
            r = s->read(s->context, scratch.buffers[0].data, (MMU32)bytes);
            if (r <= 0)
            {
                result = r;
                goto done;
            }
        }
        mm_midi_parser_init(&parser, scratch.buffers[0].data, (MMU32)bytes, 0, 500000);
        while (mm_midi_next(&parser, &event, 1))
        {
            if (events >= 1000000 || event.timestamp > 24u * 60 * 60 * 1000)
                goto done;
            if (event.count > 0 && event.count <= 3 && event.send[0] < 0xf0)
            {
                message = (MMU32)event.send[0] | ((MMU32)event.send[1] << 8) | ((MMU32)event.send[2] << 16);
                r = s->event(s->context, event.timestamp, message);
                if (r <= 0)
                {
                    result = r;
                    goto done;
                }
                ++events;
            }
        }
    }
    else
    {
        r = s->wave_buffer(s->context, (MMU32)bytes, &wave);
        if (r <= 0)
        {
            result = r;
            goto done;
        }
        if (bytes)
        {
            r = s->read(s->context, wave, (MMU32)bytes);
            if (r <= 0)
            {
                result = r;
                goto done;
            }
        }
    }
    result = 1;
done:
    mm_storage_dispose(&scratch);
    return result;
}
int mm_preview_sound(const MMPreviewCue *cue, int ready, MMU32 scene, MMU32 *resolved, const MMPreviewSoundServices *s)
{
    MMU32 i, count, type, number, child;
    void *file;
    int r;
    s->reset(s->context);
    *resolved = cue->number;
    if (cue->silent)
    {
        s->silent(s->context);
        return 1;
    }
    if (!ready)
        return MM_PREVIEW_SOUND_NOT_READY;
    if (!cue->source)
    {
        if (cue->scene_type == 0x5343454eU)
            scene = cue->scene_number;
        r = s->child(s->context, 0x5343454eU, scene, cue->child, &child);
        if (r < 0)
            return r;
        if (r)
            *resolved = child;
        else
        {
            count = s->chunks(s->context);
            for (i = 0; i < count; ++i)
            {
                s->chunk(s->context, i, &type, &number);
                if (type != 0x4d564945U)
                    continue;
                r = s->child(s->context, type, number, cue->child, &child);
                if (r < 0)
                    return r;
                if (r)
                {
                    *resolved = child;
                    break;
                }
            }
        }
    }
    r = s->resolve(s->context, cue->source, cue->type, *resolved, &file);
    if (r <= 0)
        return r < 0 ? r : MM_PREVIEW_SOUND_MISSING;
    r = s->audio(s->context, file, cue->type, *resolved);
    if (r > 0)
        s->source(s->context, cue->source);
    return r;
}
