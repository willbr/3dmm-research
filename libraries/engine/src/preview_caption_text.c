#include "mm/preview_media.h"

static MMI32 translate_name(void *p, const void *data, MMU32 bytes, int system, void *out, MMU32 capacity)
{
    const MMPreviewCaptionText *text = p;
    if (text->font.unit == 1)
        return (MMI32)mm_text_ansi(text->encoding, data, bytes, system, out, capacity);
    return (MMI32)mm_text_utf16(text->encoding, data, bytes, system, out, capacity);
}

static void bad_name(void *p)
{
    const MMPreviewCaptionText *text = p;
    if (text->bad_string)
        text->bad_string(text->context);
}

static int read_name(MMPreviewCaptionText *text, MMMovieTitle *title, const void *wire, MMU32 bytes)
{
    MMChunkTitleIO io = {0};
    io.context = text;
    io.encoding = text->system;
    io.swapped_unicode_encoding = text->swapped_unicode;
    io.convert = translate_name;
    io.bad_string = bad_name;
    return mm_movie_title_read_wire(title, wire, bytes, &io);
}

static int load_name(void *p, const MMReadStream *stream, MMU32 offset)
{
    MMPreviewCaptionText *text = p;
    unsigned char wire[2 + (MM_STRING_MAX + 2) * sizeof(unsigned short)];
    MMU32 bytes = offset <= stream->length ? stream->length - offset : 0;
    if (bytes > sizeof(wire) || !mm_read_exact(stream, offset, wire, bytes))
        bytes = 0;
    read_name(text, &text->font, wire, bytes);
    return 1;
}

static int title_font(void *p, const MMMovieTitle *title, MMCaptionFont *out)
{
    MMPreviewCaptionText *text = p;
    unsigned short units[MM_STRING_MAX];
    size_t bytes = 0;
    int status;
    /* Use the same display encoding for counted font names as Preview. */
    const int system = title->unit == 1 ? MM_TEXT_WINDOWS : MM_TEXT_UTF16_LE;
    const size_t count = mm_text_utf16(text->encoding, mm_movie_title_text(title), title->count * title->unit,
                                       system, units, MM_STRING_MAX);
    status = mm_text_write_utf8(units, count, text->utf8, sizeof(text->utf8), &bytes);
    if (!status && text->malformed_font)
        status = text->malformed_font(text->context, units, count, text->utf8, sizeof(text->utf8), &bytes);
    if (status <= 0 || bytes > sizeof(text->utf8))
        return status < 0 ? status : 0;
    out->name = text->utf8;
    out->length = bytes;
    return 1;
}

static int property_font(void *p, const void *wire, MMU32 bytes, MMCaptionFont *out)
{
    MMPreviewCaptionText *text = p;
    MMMovieTitle property;
    if (!wire)
        return title_font(text, &text->font, out);
    mm_movie_title_init(&property, text->font.unit);
    if (!read_name(text, &property, wire, bytes))
        return 0;
    return title_font(text, &property, out);
}

static int caption_text(void *p, const void *bytes, MMU32 size, int system, unsigned short *out,
                        MMU32 capacity, MMU32 *count)
{
    const MMPreviewCaptionText *text = p;
    *count = (MMU32)mm_text_utf16(text->encoding, bytes, size, system, out, capacity);
    return 1;
}

MMPreviewCaptionCodec mm_preview_caption_text_codec(MMPreviewCaptionText *text, unsigned unit, int system,
                                                    int swapped_unicode, const MMTextHost *encoding)
{
    MMPreviewCaptionCodec codec = {NULL, load_name, property_font, caption_text, title_font};
    text->encoding = encoding;
    text->system = system;
    text->swapped_unicode = swapped_unicode;
    mm_movie_title_init(&text->font, unit);
    codec.context = text;
    return codec;
}
