#include "mm/preview_media.h"
#include "movie_session_internal.h"
#include "mm/text_store.h"
#include <stdlib.h>
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
static int convert_caption(const MMPreviewCaptionData *data, void *owner, const MMPreviewCaptionServices *s)
{
    MMCollectionStorage scratch;
    MMPreviewTextProperty property;
    MMU32 words[2], count, converted, i, index;
    MMI32 bytes;
    unsigned short *output;
    const void *font;
    MMU32 font_bytes;
    int r, result = 0, free_entry;
    mm_storage_init(&scratch, s->allocator);
    if (data->bytes < 0 || data->bytes > 1024 * 1024)
        goto done;
    s->publish(s->context, data);
    r = s->font(s->context, owner);
    if (r <= 0)
    {
        result = r;
        goto done;
    }
    if (mm_storage_resize_exact(&scratch, 0, (MMU32)data->bytes, MM_STORAGE_NORMAL) != MM_STORAGE_OK)
    {
        result = -1;
        goto done;
    }
    if (data->bytes)
    {
        r = s->read_text(s->input, owner, scratch.buffers[0].data, (MMU32)data->bytes);
        if (r <= 0)
        {
            result = r;
            goto done;
        }
    }
    count = 0;
    r = s->utf16(s->context, scratch.buffers[0].data, (MMU32)data->bytes, data->encoding, 0, 0, &count);
    if (r <= 0)
    {
        result = r;
        goto done;
    }
    if (data->bytes && !count)
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
        r = s->utf16(s->context, scratch.buffers[0].data, (MMU32)data->bytes, data->encoding, output, count, &converted);
        if (r <= 0)
        {
            result = r;
            goto done;
        }
        if (!converted)
            goto done;
    }
    for (i = 0; i < data->properties; ++i)
    {
        s->property(s->input, owner, i, words);
        if (data->property_swap)
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
            if (!data->has_arguments || index >= data->arguments)
                goto done;
            s->argument_info(s->input, owner, index, &bytes, &free_entry);
            if (free_entry || bytes < 6)
                goto done;
            if (mm_storage_resize_exact(&scratch, 1, (MMU32)bytes, MM_STORAGE_NORMAL) != MM_STORAGE_OK)
            {
                result = -1;
                goto done;
            }
            s->argument(s->input, owner, index, scratch.buffers[1].data);
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
    sort(data->properties, s);
    result = 1;
done:
    mm_storage_dispose(&scratch);
    return result;
}
int mm_preview_caption(MMU32 number, const MMPreviewCaptionServices *s)
{
    MMPreviewCaptionData data = {0};
    void *owner = NULL;
    int result = s->create(s->input, &owner);
    if (!result)
        result = -1;
    if (result > 0)
        result = s->box(s->input, owner, number, &data);
    if (result > 0)
        result = s->rich_text(s->input, owner, &data);
    if (result > 0)
        result = convert_caption(&data, owner, s);
    if (owner)
        s->release(s->input, owner);
    return result;
}
static MMU32 word(const unsigned char *p, int big)
{
    return big ? ((MMU32)p[0] << 24) | ((MMU32)p[1] << 16) | ((MMU32)p[2] << 8) | p[3]
               : ((MMU32)p[3] << 24) | ((MMU32)p[2] << 16) | ((MMU32)p[1] << 8) | p[0];
}
typedef struct CaptionGroup
{
    MMMoviePayload payload;
    MMCollectionHeader header;
} CaptionGroup;

typedef struct StoredCaption
{
    const MMPreviewCaptionArchive *source;
    MMMovieArchive archive;
    MMPreviewCaptionServices services;
    MMTextBoxState box;
    MMRichTextDefaults defaults;
    CaptionGroup properties, arguments;
    const MMReadStream *text;
    MMU32 root;
    int encoding, result;
} StoredCaption;

static int caption_status(int status)
{
    return status == MM_MOVIE_MEMORY ? -1 : 0;
}

static int read_box(void *context, const MMReadStream *stream)
{
    return mm_text_box_decode(stream, (MMTextBoxState *)context) ? MM_MOVIE_OK : MM_MOVIE_BAD_FILE;
}

static int read_root(void *context, const MMReadStream *stream)
{
    StoredCaption *caption = (StoredCaption *)context;
    if (!mm_rich_text_decode(stream, &caption->defaults))
        return MM_MOVIE_BAD_FILE;
    caption->result = caption->source->load_font(caption->services.context, stream, 16);
    return MM_MOVIE_OK;
}

static int read_group(StoredCaption *caption, MMU32 type, int allocated, CaptionGroup *group)
{
    MMChunkEntry entry;
    MMU32 position;
    int status;
    status = mm_session_child(&caption->archive.access, caption->root, 0, type, &entry, &position);
    if (status == MM_CHUNK_NOT_FOUND && allocated)
        return MM_MOVIE_OK;
    if (status != MM_CHUNK_OK)
        return MM_MOVIE_BAD_FILE;
    status = mm_session_payload(&caption->archive, &entry, 0, &group->payload);
    if (status != MM_MOVIE_OK)
        return status;
    if (mm_collection_header_read(group->payload.stream, allocated ? MM_COLLECTION_GROUP : MM_COLLECTION_ARRAY,
                                  allocated, &group->header) != MM_STORAGE_OK ||
        group->header.width != (allocated ? 4U : 8U))
        return MM_MOVIE_BAD_FILE;
    return MM_MOVIE_OK;
}

static void describe_caption(const StoredCaption *caption, MMPreviewCaptionData *out)
{
    const MMTextBoxState *box = &caption->box;
    const unsigned short one = 1;
    out->first = box->first;
    out->end = box->end;
    out->left = box->left;
    out->top = box->top;
    out->right = box->right;
    out->bottom = box->bottom;
    out->story = box->story;
    out->bytes = mm_signed_word(caption->text->length - 2);
    out->encoding = caption->encoding;
    out->font_size = caption->defaults.height;
    out->background = caption->defaults.background;
    out->properties = caption->properties.header.count;
    out->property_swap = caption->properties.header.big_endian != (*(const unsigned char *)&one == 0);
    out->has_arguments = caption->arguments.payload.stream != NULL;
    out->arguments = caption->arguments.header.count;
}
static int stored_read_text(void *context, void *owner, void *out, MMU32 count)
{
    (void)context;
    return mm_read_exact(((const StoredCaption *)owner)->text, 2, out, count);
}
static void stored_property(void *context, void *owner, MMU32 i, MMU32 out[2])
{
    const CaptionGroup *group = &((const StoredCaption *)owner)->properties;
    (void)context;
    memcpy(out, group->payload.decoded + group->header.header_bytes + i * 8, 8);
}
static const unsigned char *stored_argument_row(const StoredCaption *caption, MMU32 i, MMI32 *bytes)
{
    const CaptionGroup *group = &caption->arguments;
    const unsigned char *records = group->payload.decoded + group->header.header_bytes;
    MMU32 offset;
    if (!mm_collection_group_row(&group->header, records, i, &offset, bytes))
        return NULL;
    return records + offset + group->header.width;
}
static void stored_argument_info(void *context, void *owner, MMU32 i, MMI32 *bytes, int *free_entry)
{
    (void)context;
    *bytes = 0;
    *free_entry = stored_argument_row((const StoredCaption *)owner, i, bytes) == NULL;
}
static void stored_argument(void *context, void *owner, MMU32 i, void *out)
{
    MMI32 bytes;
    const unsigned char *row = stored_argument_row((const StoredCaption *)owner, i, &bytes);
    (void)context;
    if (row)
        memcpy(out, row, (size_t)bytes);
}

static int read_stored_text(void *context, const MMReadStream *stream)
{
    StoredCaption *caption = (StoredCaption *)context;
    MMPreviewCaptionData data = {0};
    unsigned short marker;
    int status;
    if (!mm_read_exact(stream, 0, &marker, sizeof(marker)))
        return MM_MOVIE_BAD_FILE;
    status = read_group(caption, MM_TEXT_PROPERTIES, 0, &caption->properties);
    if (status == MM_MOVIE_OK)
        status = read_group(caption, MM_TEXT_ARGUMENTS, 1, &caption->arguments);
    if (status != MM_MOVIE_OK)
        return status;
    caption->text = stream;
    caption->encoding = marker < 32768U ? marker : -1 - (int)(65535U - marker);
    describe_caption(caption, &data);
    caption->result = convert_caption(&data, caption, &caption->services);
    return MM_MOVIE_OK;
}

int mm_preview_caption_graph(MMU32 number, const MMPreviewCaptionArchive *source, const MMPreviewCaptionServices *s)
{
    StoredCaption caption = {0};
    const MMMovieSessionGraph *graph = source->graph;
    MMChunkEntry entry;
    MMU32 box, position;
    int status;
    caption.source = source;
    caption.services = *s;
    caption.services.input = &caption;
    caption.services.read_text = stored_read_text;
    caption.services.property = stored_property;
    caption.services.argument_info = stored_argument_info;
    caption.services.argument = stored_argument;
    caption.archive.graph = *graph;
    graph->access(graph->context, &caption.archive.access);
    if (mm_chunk_find(&caption.archive.access, MM_TEXT_BOX, number, &box) != MM_CHUNK_OK ||
        mm_chunk_at(&caption.archive.access, box, &entry) != MM_CHUNK_OK)
        return 0;
    status = graph->payload(graph->context, &entry, read_box, &caption.box);
    if (status != MM_MOVIE_OK)
        return caption_status(status);
    if (mm_session_child(&caption.archive.access, box, caption.box.child, MM_TEXT_ROOT, &entry, &caption.root) !=
        MM_CHUNK_OK)
        return 0;
    status = graph->payload(graph->context, &entry, read_root, &caption);
    if (status != MM_MOVIE_OK)
        return caption_status(status);
    if (caption.result <= 0)
        return caption.result;
    if (mm_session_child(&caption.archive.access, caption.root, 0, MM_TEXT_BYTES, &entry, &position) != MM_CHUNK_OK)
        return 0;
    caption.result = 0;
    status = source->text(graph->context, &entry, read_stored_text, &caption);
    free(caption.properties.payload.decoded);
    free(caption.arguments.payload.decoded);
    return status == MM_MOVIE_OK ? caption.result : caption_status(status);
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
