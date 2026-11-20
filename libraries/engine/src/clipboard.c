#include "mm/clipboard.h"

#define CALL(op) s->call(s->context, op, &q)
#define TRY(value) do { int status_ = (value); if (status_ < 0) return status_; } while (0)

size_t mm_clipboard_text_bytes(const void *buffer, size_t bytes, unsigned unit)
{
    const unsigned char *p = buffer;
    size_t i;

    if (unit == 1)
    {
        for (i = 0; i < bytes && p[i]; ++i)
            ;
    }
    else
    {
        bytes &= ~(size_t)1;
        for (i = 0; i < bytes && (p[i] || p[i + 1]); i += 2)
            ;
    }
    return i;
}

size_t mm_clipboard_normalize_paragraphs(unsigned short *text, size_t count)
{
    size_t i, used = 0;
    unsigned short previous = 0, unit;

    for (i = 0; i < count; ++i)
    {
        unit = text[i];
        if (unit != 10 || previous != 13)
            text[used++] = unit == 10 ? 13 : unit;
        previous = unit;
    }
    return used;
}

static int has_views(MMClipboardRequest *q, const MMClipboardServices *s)
{
    return q->document ? s->call(s->context, MM_CLIPBOARD_HAS_VIEWS, q) : 0;
}

static int release_document(MMClipboardRequest *q, const MMClipboardServices *s)
{
    return q->document ? s->call(s->context, MM_CLIPBOARD_RELEASE, q) : 1;
}

int mm_clipboard_set(MMClipboard *clip, void *document, int export_document, const MMClipboardServices *s)
{
    MMClipboardRequest q = {0};
    int result;

    if (clip->exporting || clip->importing)
    {
        q.diagnostic = MM_CLIPBOARD_BUSY_SET;
        return CALL(MM_CLIPBOARD_DIAGNOSTIC);
    }
    if (document == clip->document)
        return 1;

    clip->current = 1;
    clip->delayed = 0;
    clip->import_format = 0;
    q.document = clip->document;
    clip->document = document;
    result = has_views(&q, s);
    TRY(result);
    if (result)
        TRY(CALL(MM_CLIPBOARD_RENAME));
    TRY(release_document(&q, s));
    q.document = clip->document;
    if (q.document)
    {
        TRY(CALL(MM_CLIPBOARD_RETAIN));
        result = has_views(&q, s);
        TRY(result);
        if (result)
        {
            q.document = clip->document;
            TRY(CALL(MM_CLIPBOARD_RENAME));
        }
        if (export_document)
        {
            q.document = clip->document;
            TRY(CALL(MM_CLIPBOARD_EXPORT_DOCUMENT));
            return mm_clipboard_end_export(clip, s);
        }
    }
    return 1;
}

int mm_clipboard_invalidate(MMClipboard *clip, const MMClipboardServices *s)
{
    MMClipboardRequest q = {0};
    int result;

    q.document = clip->document;
    result = has_views(&q, s);
    TRY(result);
    if (result)
        TRY(mm_clipboard_set(clip, 0, 1, s));
    clip->current = 0;
    return 1;
}

static int import_document(MMClipboard *clip, MMClipboardRequest *q, const MMClipboardServices *s)
{
    int result;

    if (q->buffer && q->bytes > 0)
    {
        if (s->wide_format && q->format == s->wide_format)
            q->bytes = (MMI32)mm_clipboard_text_bytes(q->buffer, (size_t)q->bytes, 2);
        else if (s->byte_format && q->format == s->byte_format)
            q->bytes = (MMI32)mm_clipboard_text_bytes(q->buffer, (size_t)q->bytes, 1);
    }
    clip->importing = 1;
    result = s->call(s->context, MM_CLIPBOARD_IMPORT_DOCUMENT, q);
    TRY(result);
    clip->importing = 0;
    return result;
}

static int read_text(MMClipboard *clip, const MMClipboardServices *s)
{
    MMClipboardRequest q = {0};
    int result = CALL(MM_CLIPBOARD_READ_TEXT);

    if (result <= 0)
        return result;
    if (!q.bytes)
        return mm_clipboard_set(clip, 0, 0, s);
    if (q.bytes < 0 || q.bytes > s->max_bytes)
        return 0;
    q.format = s->text_format;
    q.output = 1;
    result = import_document(clip, &q, s);
    TRY(result);
    if (result)
        TRY(mm_clipboard_set(clip, q.document, 0, s));
    return release_document(&q, s);
}

static int read_formats(MMClipboard *clip, const MMClipboardServices *s)
{
    MMClipboardRequest q = {0};
    int result = CALL(MM_CLIPBOARD_OPEN_READ);

    if (result <= 0)
        return result;
    for (;;)
    {
        result = CALL(MM_CLIPBOARD_NEXT_FORMAT);
        TRY(result);
        if (!result)
            break;
        q.document = q.buffer = 0;
        q.bytes = q.output = q.delay = q.allow_delay = 0;
        result = import_document(clip, &q, s);
        TRY(result);
        if (!result)
            continue;
        result = CALL(MM_CLIPBOARD_GET_DATA);
        TRY(result);
        if (!result)
            continue;
        TRY(CALL(MM_CLIPBOARD_LOCK_DATA));
        q.output = q.allow_delay = 1;
        result = import_document(clip, &q, s);
        TRY(result);
        TRY(CALL(MM_CLIPBOARD_UNLOCK_DATA));
        if (result)
        {
            TRY(mm_clipboard_set(clip, q.document, 0, s));
            clip->delayed = q.delay && q.document != 0;
            clip->import_format = q.format;
            TRY(release_document(&q, s));
            break;
        }
    }
    return CALL(MM_CLIPBOARD_CLOSE);
}

static int ensure_document(MMClipboard *clip, const MMClipboardServices *s)
{
    MMClipboardRequest q = {0};
    int result;

    if (clip->exporting || clip->importing)
    {
        q.diagnostic = MM_CLIPBOARD_BUSY_READ;
        return CALL(MM_CLIPBOARD_DIAGNOSTIC);
    }
    if (clip->current)
        return 1;

    clip->current = 1;
    if (s->transport == MM_CLIPBOARD_TEXT)
    {
        result = read_text(clip, s);
        return result == MM_CLIPBOARD_ALLOCATION_ERROR ? 0 : result;
    }
    if (s->transport == MM_CLIPBOARD_FORMATS)
        return read_formats(clip, s);
    return 1;
}

static int complete_import(MMClipboard *clip, const MMClipboardServices *s)
{
    MMClipboardRequest q = {0};
    int result;

    if (!clip->document || !clip->delayed || !clip->import_format || s->transport != MM_CLIPBOARD_FORMATS)
        return 1;
    result = CALL(MM_CLIPBOARD_OPEN_READ);
    if (result <= 0)
        return result;
    q.format = clip->import_format;
    result = CALL(MM_CLIPBOARD_GET_DATA);
    TRY(result);
    if (result)
    {
        q.document = clip->document;
        if (q.document)
            TRY(CALL(MM_CLIPBOARD_RETAIN));
        TRY(CALL(MM_CLIPBOARD_LOCK_DATA));
        q.output = 1;
        result = import_document(clip, &q, s);
        TRY(result);
        TRY(CALL(MM_CLIPBOARD_UNLOCK_DATA));
        if (result)
            TRY(mm_clipboard_set(clip, q.document, 0, s));
        clip->delayed = 0;
        clip->import_format = 0;
        TRY(release_document(&q, s));
    }
    return CALL(MM_CLIPBOARD_CLOSE);
}

int mm_clipboard_is_document(MMClipboard *clip, void *document, const MMClipboardServices *s)
{
    if (!document)
        TRY(ensure_document(clip, s));
    return clip->document == document;
}

int mm_clipboard_show(MMClipboard *clip, const MMClipboardServices *s)
{
    MMClipboardRequest q = {0};
    int result;

    TRY(ensure_document(clip, s));
    TRY(complete_import(clip, s));
    q.document = clip->document;
    if (!q.document)
        return 1;
    result = has_views(&q, s);
    TRY(result);
    q.document = clip->document;
    return result ? CALL(MM_CLIPBOARD_ACTIVATE) : CALL(MM_CLIPBOARD_SHOW);
}

int mm_clipboard_get(MMClipboard *clip, MMI32 format, int output, const MMClipboardServices *s)
{
    MMClipboardRequest q = {0};
    int result;

    TRY(ensure_document(clip, s));
    if (output)
        TRY(complete_import(clip, s));
    q.document = clip->document;
    if (!q.document)
        return 0;
    q.format = format;
    q.output = output;
    result = CALL(MM_CLIPBOARD_IS_FORMAT);
    TRY(result);
    q.document = clip->document;
    if (!result)
        return CALL(MM_CLIPBOARD_DOCUMENT_FORMAT);
    if (output)
    {
        TRY(CALL(MM_CLIPBOARD_PUBLISH_DOCUMENT));
        TRY(CALL(MM_CLIPBOARD_RETAIN));
    }
    return 1;
}

int mm_clipboard_begin_export(MMClipboard *clip, const MMClipboardServices *s)
{
    MMClipboardRequest q = {0};
    int result;

    if (clip->importing || clip->exporting)
    {
        q.diagnostic = MM_CLIPBOARD_BUSY_WRITE;
        TRY(CALL(MM_CLIPBOARD_DIAGNOSTIC));
        return 0;
    }
    result = CALL(MM_CLIPBOARD_OPEN_WRITE);
    TRY(result);
    clip->exporting = result != 0;
    return result;
}

int mm_clipboard_export_buffer(MMClipboard *clip, MMI32 bytes, MMI32 format, void **buffer, const MMClipboardServices *s)
{
    MMClipboardRequest q = {0};
    MMU32 padded, unit = s->text_unit;
    int result;

    *buffer = 0;
    if (!clip->exporting)
    {
        q.diagnostic = MM_CLIPBOARD_NOT_EXPORTING;
        TRY(CALL(MM_CLIPBOARD_DIAGNOSTIC));
        return 0;
    }
    q.format = clip->export_format;
    TRY(CALL(MM_CLIPBOARD_FLUSH_EXPORT));
    if (bytes <= 0 || bytes > s->max_bytes)
        return 0;
    padded = (MMU32)bytes;
    if (s->transport == MM_CLIPBOARD_TEXT)
    {
        if (format != s->text_format || padded + unit > (MMU32)s->max_bytes)
            return 0;
        padded += unit;
    }
    else if (format == s->text_format)
    {
        padded += unit;
        padded += (unit - padded % unit) % unit;
        if (padded > 0x7fffffffUL)
            return 0;
    }
    q.format = format;
    q.bytes = (MMI32)padded;
    result = CALL(MM_CLIPBOARD_ALLOCATE_EXPORT);
    TRY(result);
    if (result)
    {
        clip->export_format = format;
        *buffer = q.buffer;
    }
    return result;
}

int mm_clipboard_end_export(MMClipboard *clip, const MMClipboardServices *s)
{
    MMClipboardRequest q = {0};

    if (!clip->exporting)
        return 1;
    q.format = clip->export_format;
    TRY(CALL(MM_CLIPBOARD_FLUSH_EXPORT));
    TRY(CALL(MM_CLIPBOARD_CLOSE));
    clip->exporting = 0;
    return 1;
}

#undef TRY
#undef CALL
