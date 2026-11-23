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

static int edited_caption_text(void *p, void *owner, void *out, MMU32 bytes)
{
    (void)owner;
    return mm_read_exact((const MMReadStream *)p, 0, out, bytes);
}

static void edited_caption_property(void *p, void *owner, MMU32 row, MMU32 words[2])
{
    const MMRichTextState *body = owner;
    (void)p;
    mm_text_collection_property(body->properties, row, words);
}

static void edited_caption_argument_info(void *p, void *owner, MMU32 row, MMI32 *bytes, int *free_entry)
{
    const MMRichTextState *body = owner;
    (void)p;
    *bytes = 0;
    *free_entry = mm_text_collection_argument(body->arguments, row, bytes) == NULL;
}

static void edited_caption_argument(void *p, void *owner, MMU32 row, void *out)
{
    const MMRichTextState *body = owner;
    MMI32 bytes;
    const void *data = mm_text_collection_argument(body->arguments, row, &bytes);
    (void)p;
    if (data && bytes)
        memcpy(out, data, (size_t)bytes);
}

int mm_preview_caption_body(const MMTextBoxState *box, const MMRichTextState *body, const MMReadStream *text,
                            int encoding, const MMPreviewCaptionServices *s)
{
    MMPreviewCaptionData data = {0};
    MMPreviewCaptionServices services = *s;
    if (!body->properties || text->length > 1024 * 1024)
        return 0;
    data.first = box->first;
    data.end = box->end;
    data.left = box->left;
    data.top = box->top;
    data.right = box->right;
    data.bottom = box->bottom;
    data.story = box->story;
    data.bytes = (MMI32)text->length;
    data.encoding = encoding;
    data.font_size = body->defaults.height;
    data.background = body->defaults.background;
    data.properties = mm_text_collection_count(body->properties);
    data.has_arguments = body->arguments != NULL;
    data.arguments = mm_text_collection_count(body->arguments);
    services.input = (void *)text;
    services.read_text = edited_caption_text;
    services.property = edited_caption_property;
    services.argument_info = edited_caption_argument_info;
    services.argument = edited_caption_argument;
    return convert_caption(&data, (void *)body, &services);
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

static int preview_read_failure(int status)
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
    const unsigned short one = 1;
    unsigned char marker[2];
    int status;
    if (!mm_read_exact(stream, 0, marker, sizeof(marker)))
        return MM_MOVIE_BAD_FILE;
    status = read_group(caption, MM_TEXT_PROPERTIES, 0, &caption->properties);
    if (status == MM_MOVIE_OK)
        status = read_group(caption, MM_TEXT_ARGUMENTS, 1, &caption->arguments);
    if (status != MM_MOVIE_OK)
        return status;
    caption->text = stream;
    caption->encoding = mm_rich_text_encoding(marker, *(const unsigned char *)&one == 0);
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
        return preview_read_failure(status);
    if (mm_session_child(&caption.archive.access, box, caption.box.child, MM_TEXT_ROOT, &entry, &caption.root) !=
        MM_CHUNK_OK)
        return 0;
    status = graph->payload(graph->context, &entry, read_root, &caption);
    if (status != MM_MOVIE_OK)
        return preview_read_failure(status);
    if (caption.result <= 0)
        return caption.result;
    if (mm_session_child(&caption.archive.access, caption.root, 0, MM_TEXT_BYTES, &entry, &position) != MM_CHUNK_OK)
        return 0;
    caption.result = 0;
    status = source->text(graph->context, &entry, read_stored_text, &caption);
    free(caption.properties.payload.decoded);
    free(caption.arguments.payload.decoded);
    return status == MM_MOVIE_OK ? caption.result : preview_read_failure(status);
}

void mm_preview_owned_caption_dispose(MMPreviewOwnedCaption *caption)
{
    size_t i;
    for (i = 0; i < caption->property_count; ++i)
        free((void *)caption->properties[i].font.name);
    free(caption->properties);
    free(caption->text);
    free((void *)caption->font.name);
    memset(caption, 0, sizeof(*caption));
}

typedef struct OwnedCaptionReader
{
    const MMPreviewCaptionCodec *codec;
    MMPreviewOwnedCaption *caption;
    const MMMovieTitle *body_font;
} OwnedCaptionReader;

static void owned_caption_publish(void *p, const MMPreviewCaptionData *data)
{
    ((OwnedCaptionReader *)p)->caption->data = *data;
}

static int owned_load_font(void *p, const MMReadStream *stream, MMU32 offset)
{
    const MMPreviewCaptionCodec *codec = ((OwnedCaptionReader *)p)->codec;
    return codec->load_font(codec->context, stream, offset);
}

static int owned_copy_font(const MMPreviewCaptionCodec *codec, const MMMovieTitle *title,
                           const void *wire, MMU32 bytes, MMCaptionFont *out)
{
    MMCaptionFont font;
    char *name;
    int result = title && codec->body_font ? codec->body_font(codec->context, title, &font)
                                         : codec->font(codec->context, wire, bytes, &font);
    if (result <= 0)
        return result;
    name = NULL;
    if (font.length)
    {
        name = (char *)malloc(font.length);
        if (!name)
            return -1;
        memcpy(name, font.name, font.length);
    }
    out->name = name;
    out->length = font.length;
    return 1;
}

static int owned_default_font(void *p, void *input)
{
    OwnedCaptionReader *reader = (OwnedCaptionReader *)p;
    (void)input;
    return owned_copy_font(reader->codec, reader->body_font, NULL, 0, &reader->caption->font);
}

static int owned_utf16(void *p, const void *bytes, MMU32 size, int encoding, unsigned short *out,
                       MMU32 capacity, MMU32 *count)
{
    const MMPreviewCaptionCodec *codec = ((OwnedCaptionReader *)p)->codec;
    return codec->utf16(codec->context, bytes, size, encoding, out, capacity, count);
}

static int owned_text_buffer(void *p, MMU32 length, unsigned short **out)
{
    MMPreviewOwnedCaption *caption = ((OwnedCaptionReader *)p)->caption;
    if (length)
    {
        if ((size_t)length > (size_t)-1 / sizeof(*caption->text))
            return -1;
        caption->text = (unsigned short *)calloc(length, sizeof(*caption->text));
        if (!caption->text)
            return -1;
    }
    caption->length = length;
    *out = caption->text;
    return 1;
}

static int owned_append(void *p, const MMPreviewTextProperty *property, const void *wire, MMU32 bytes)
{
    OwnedCaptionReader *reader = (OwnedCaptionReader *)p;
    MMPreviewOwnedCaption *caption = reader->caption;
    const size_t limit = (size_t)-1 / sizeof(*caption->properties);
    MMCaptionProperty row = {0};
    if (caption->property_count == caption->property_capacity)
    {
        size_t capacity = caption->property_capacity;
        MMCaptionProperty *grown;
        if (capacity == limit)
            return -1;
        capacity = capacity ? (capacity <= limit / 2 ? 2 * capacity : limit) : 8;
        grown = (MMCaptionProperty *)realloc(caption->properties, capacity * sizeof(*grown));
        if (!grown)
            return -1;
        caption->properties = grown;
        caption->property_capacity = capacity;
    }
    if (wire)
    {
        const int result = owned_copy_font(reader->codec, NULL, wire, bytes, &row.font);
        if (result <= 0)
            return result;
    }
    row.position = property->position;
    row.kind = property->kind;
    row.value = property->value;
    caption->properties[caption->property_count++] = row;
    return 1;
}

static MMU32 owned_position(void *p, MMU32 index)
{
    return ((OwnedCaptionReader *)p)->caption->properties[index].position;
}

static void owned_swap(void *p, MMU32 left, MMU32 right)
{
    MMCaptionProperty *rows = ((OwnedCaptionReader *)p)->caption->properties;
    const MMCaptionProperty row = rows[left];
    rows[left] = rows[right];
    rows[right] = row;
}

static MMPreviewCaptionServices owned_caption_services(OwnedCaptionReader *reader)
{
    MMPreviewCaptionServices services = {0};
    services.context = reader;
    services.publish = owned_caption_publish;
    services.font = owned_default_font;
    services.utf16 = owned_utf16;
    services.text_buffer = owned_text_buffer;
    services.append = owned_append;
    services.position = owned_position;
    services.swap = owned_swap;
    return services;
}

int mm_preview_caption_body_owned(const MMTextBoxState *box, const MMRichTextState *body, const MMReadStream *text,
                                  int encoding, const MMPreviewCaptionCodec *codec, MMPreviewOwnedCaption *out)
{
    OwnedCaptionReader reader;
    MMPreviewCaptionServices services;
    reader.codec = codec;
    reader.caption = out;
    reader.body_font = &body->font;
    services = owned_caption_services(&reader);
    return mm_preview_caption_body(box, body, text, encoding, &services);
}

typedef struct OwnedPresentation
{
    MMPreviewCaptionArchive source;
    const MMPreviewCaptionCodec *codec;
    const MMPreviewOwnedPresentationOutput *output;
} OwnedPresentation;

static void owned_reset(void *p)
{
    const MMPreviewOwnedPresentationOutput *out = ((OwnedPresentation *)p)->output;
    out->reset(out->context);
}

static void owned_transition(void *p, MMI32 kind)
{
    const MMPreviewOwnedPresentationOutput *out = ((OwnedPresentation *)p)->output;
    out->transition(out->context, kind);
}

static int owned_caption(void *p, MMU32 chunk)
{
    const OwnedPresentation *presentation = (const OwnedPresentation *)p;
    OwnedCaptionReader reader;
    MMPreviewOwnedCaption caption = {0};
    MMPreviewCaptionServices services;
    int result;
    caption.chunk = chunk;
    reader.codec = presentation->codec;
    reader.caption = &caption;
    reader.body_font = NULL;
    services = owned_caption_services(&reader);
    result = mm_preview_caption_graph(chunk, &presentation->source, &services);
    if (result > 0)
        result = presentation->output->caption(presentation->output->context, &caption);
    mm_preview_owned_caption_dispose(&caption);
    return result;
}

static int owned_pause(void *p, MMI32 frame, MMI32 kind, MMI32 duration)
{
    const MMPreviewOwnedPresentationOutput *out = ((OwnedPresentation *)p)->output;
    return out->pause(out->context, frame, kind, duration);
}

static int owned_cue(void *p, const MMPreviewCue *cue)
{
    const MMPreviewOwnedPresentationOutput *out = ((OwnedPresentation *)p)->output;
    return out->cue(out->context, cue);
}

int mm_preview_presentation_owned(const MMMovieSessionGraph *graph, MMMovieGraphBytes stored, MMU32 scene,
                                  int sound_sheet, int include_captions, const MMPreviewCaptionCodec *codec,
                                  const MMPreviewOwnedPresentationOutput *out)
{
    OwnedPresentation presentation;
    MMPreviewPresentationServices services = {NULL, owned_reset, owned_transition, owned_caption, owned_pause, owned_cue};
    presentation.source.graph = graph;
    presentation.source.text = stored;
    presentation.source.load_font = owned_load_font;
    presentation.codec = codec;
    presentation.output = out;
    services.context = &presentation;
    return mm_preview_presentation(graph, scene, sound_sheet, include_captions, &services);
}

typedef struct AudioPayload
{
    const MMPreviewAudioServices *services;
    MMU32 type, number;
    int result;
} AudioPayload;

static int read_midi(const MMReadStream *stream, const MMPreviewAudioServices *s)
{
    MMCollectionStorage scratch;
    MMMidiParser parser;
    MMMidiEvent event;
    MMU32 events = 0, message;
    int r, result = 0;
    mm_storage_init(&scratch, s->allocator);
    if (mm_storage_resize_exact(&scratch, 0, stream->length, MM_STORAGE_NORMAL) != MM_STORAGE_OK ||
        !mm_read_exact(stream, 0, scratch.buffers[0].data, stream->length))
        goto done;
    mm_midi_parser_init(&parser, scratch.buffers[0].data, stream->length, 0, 500000);
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
    result = 1;
done:
    mm_storage_dispose(&scratch);
    return result;
}

static int read_audio_payload(void *context, const MMReadStream *stream)
{
    AudioPayload *payload = (AudioPayload *)context;
    const MMPreviewAudioServices *s = payload->services;
    unsigned char *wave;
    int midi = payload->type == MM_SOUND_MIDI;
    if (stream->length > 64 * 1024 * 1024)
        return MM_MOVIE_BAD_FILE;
    s->publish(s->context, payload->type, payload->number, midi);
    if (midi)
        payload->result = read_midi(stream, s);
    else
    {
        payload->result = s->wave_buffer(s->context, stream->length, &wave);
        if (payload->result > 0)
            payload->result = mm_read_exact(stream, 0, wave, stream->length);
    }
    return MM_MOVIE_OK;
}

static int read_audio(const MMMovieSessionGraph *graph, MMU32 type, MMU32 number, const MMPreviewAudioServices *s)
{
    const unsigned short one = 1;
    MMChunkAccess chunks;
    MMChunkEntry entry;
    MMChunkChild child;
    MMSoundHeader header;
    AudioPayload payload = {0};
    MMU32 position;
    int status;
    graph->access(graph->context, &chunks);
    if (mm_chunk_find(&chunks, type, number, &position) != MM_CHUNK_OK ||
        mm_chunk_at(&chunks, position, &entry) != MM_CHUNK_OK)
        return 0;
    if (type == MM_SOUND_TYPE)
    {
        status = mm_sound_read_header(graph, &entry, *(const unsigned char *)&one == 0, &header);
        if (status != MM_MOVIE_OK)
            return preview_read_failure(status);
        if (!header.valid_order)
            return 0;
        status = header.invalid || header.reserved_nonzero ? MM_CHUNK_NOT_FOUND :
                     mm_chunk_child_type(&chunks, position, 0, 0, &child);
        if (status == MM_CHUNK_NOT_FOUND)
        {
            s->silent(s->context);
            return 1;
        }
        if (status != MM_CHUNK_OK || mm_chunk_find(&chunks, child.type, child.number, &position) != MM_CHUNK_OK ||
            mm_chunk_at(&chunks, position, &entry) != MM_CHUNK_OK)
            return 0;
    }
    payload.services = s;
    payload.type = entry.type;
    payload.number = entry.number;
    status = graph->payload(graph->context, &entry, read_audio_payload, &payload);
    return status == MM_MOVIE_OK ? payload.result : preview_read_failure(status);
}

int mm_preview_audio(const MMMovieSessionGraph *graph, MMU32 type, MMU32 number, const MMPreviewAudioServices *s)
{
    s->reset(s->context);
    return read_audio(graph, type, number, s);
}

static int remap_sound(const MMChunkAccess *movie, MMU32 scene, MMU32 slot, MMU32 *resolved)
{
    const MMU32 scene_type = 0x5343454eUL, movie_type = 0x4d564945UL;
    MMChunkEntry entry;
    MMChunkChild child;
    MMU32 ordinal, position;
    int status = mm_chunk_link_number(movie, scene_type, scene, slot, MM_SOUND_TYPE, resolved);
    if (status != MM_CHUNK_NOT_FOUND)
        return status == MM_CHUNK_OK;
    for (ordinal = 0; ; ++ordinal)
    {
        status = mm_chunk_type_at(movie, movie_type, ordinal, &entry, &position);
        if (status != MM_CHUNK_OK)
            return status == MM_CHUNK_NOT_FOUND;
        status = mm_chunk_child_exact(movie, position, slot, MM_SOUND_TYPE, &child);
        if (status != MM_CHUNK_NOT_FOUND)
        {
            if (status == MM_CHUNK_OK)
                *resolved = child.number;
            return status == MM_CHUNK_OK;
        }
    }
}

int mm_preview_sound(const MMPreviewCue *cue, int ready, MMU32 scene, const MMChunkAccess *movie,
                     MMU32 *resolved, const MMPreviewSoundServices *s, const MMPreviewAudioServices *audio)
{
    MMMovieSessionGraph graph;
    int r;
    audio->reset(audio->context);
    *resolved = cue->number;
    if (cue->silent)
    {
        audio->silent(audio->context);
        return 1;
    }
    if (!ready)
        return MM_PREVIEW_SOUND_NOT_READY;
    if (!cue->source && movie)
    {
        if (cue->scene_type == 0x5343454eUL)
            scene = cue->scene_number;
        if (!remap_sound(movie, scene, cue->child, resolved))
            return 0;
    }
    r = s->resolve(s->context, cue->source, cue->type, *resolved, &graph);
    if (r <= 0)
        return r < 0 ? r : MM_PREVIEW_SOUND_MISSING;
    r = read_audio(&graph, cue->type, *resolved, audio);
    if (r > 0)
        s->source(s->context, cue->source);
    return r;
}
