#include "mm/text_store.h"
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
int mm_rich_text_write(const MMRichTextState *state, MMTextChunk *written, int big, const MMRichTextWriteServices *s)
{
    const MMRichTextObjects *o = &s->objects;
    MMTextLocation destination;
    unsigned char marker[2];
    int r;
    if (state->text.bytes < 0 || state->text.bytes > MM_TEXT_LIMIT - 2 || !state->properties ||
        o->width(o->context, MM_TEXT_PROPERTIES, state->properties) != 8 ||
        (state->arguments && o->width(o->context, MM_TEXT_ARGUMENTS, state->arguments) != 4))
        return 0;
    r = mm_rich_text_create(&state->defaults, &state->font, written, big, s);
    if (r <= 0)
        return r;
    r = s->add_child(s->context, written->number, MM_TEXT_BYTES, (MMU32)state->text.bytes + 2);
    if (r <= 0)
        goto fail;
    put(marker, (MMU32)state->text_system, 2, big);
    r = s->write(s->context, 0, marker, 2);
    if (r <= 0)
        goto fail;
    if (state->text.bytes > 0)
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
