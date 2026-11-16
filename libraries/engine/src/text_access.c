#include "mm/text_access.h"
#include <string.h>
static MMI32 min(MMI32 a, MMI32 b)
{
    return a < b ? a : b;
}
static MMI32 max(MMI32 a, MMI32 b)
{
    return a > b ? a : b;
}
static MMU32 unit_at(const void *data, MMI32 i, unsigned unit)
{
    unsigned short value;
    const unsigned char *p = (const unsigned char *)data + i * unit;
    if (unit == 1)
        return *p;
    memcpy(&value, p, 2);
    return value;
}
int mm_text_cache_fill(MMTextCache *c, MMI32 first, MMI32 end, MMI32 characters, const MMTextAccessServices *s)
{
    MMI32 at, new_first, new_end;
    int r;
    unsigned char *bytes = (unsigned char *)c->data;
    if (first >= c->first && end <= c->end)
        return 1;
    if (c->end <= c->first)
        goto fresh;
    if (first < c->first)
    {
        if (c->first >= first + MM_TEXT_CACHE_UNITS)
            goto fresh;
        at = max(0, min(first, c->end - MM_TEXT_CACHE_UNITS));
        c->end = min(at + MM_TEXT_CACHE_UNITS, c->end);
        memmove(bytes + (c->first - at) * c->unit, bytes, (size_t)(c->end - c->first) * c->unit);
        r = s->fetch(s->context, at * c->unit, (c->first - at) * c->unit, bytes);
        if (r <= 0)
            return r;
        c->first = at;
    }
    else
    {
        if (end >= c->end + MM_TEXT_CACHE_UNITS)
        {
        fresh:
            c->first = max(0, min((first + end - MM_TEXT_CACHE_UNITS) / 2, characters - MM_TEXT_CACHE_UNITS));
            c->end = min(c->first + MM_TEXT_CACHE_UNITS, characters);
            return s->fetch(s->context, c->first * c->unit, (c->end - c->first) * c->unit, bytes);
        }
        new_end = min(characters, max(end, c->first + MM_TEXT_CACHE_UNITS));
        new_first = max(new_end - MM_TEXT_CACHE_UNITS, c->first);
        if (new_first != c->first)
            memmove(bytes, bytes + (new_first - c->first) * c->unit, (size_t)(c->end - new_first) * c->unit);
        r = s->fetch(s->context, c->end * c->unit, (new_end - c->end) * c->unit,
                     bytes + (c->end - new_first) * c->unit);
        if (r <= 0)
            return r;
        c->first = new_first;
        c->end = new_end;
    }
    return 1;
}
void mm_text_cache_invalidate(MMTextCache *c, MMI32 cp, MMI32 ins, MMI32 del)
{
    MMI32 front, back;
    if (c->end <= cp || c->end <= c->first || (!ins && !del))
        return;
    if (c->first >= cp + del)
    {
        c->first += ins - del;
        c->end += ins - del;
        return;
    }
    front = cp - c->first;
    back = c->end - cp - del;
    if (front <= 0 && back <= 0)
        c->first = c->end = 0;
    else if (front >= back)
        c->end = cp;
    else
    {
        memmove(c->data, (unsigned char *)c->data + (cp + del - c->first) * c->unit, (size_t)back * c->unit);
        c->first = cp + ins;
        c->end = c->first + back;
    }
}
int mm_text_access_character(MMI32 cp, MMU32 *out, const MMTextAccessServices *s)
{
    MMTextCache c;
    int r;
    s->view(s->context, &c);
    if (cp < c.first || cp >= c.end)
    {
        r = s->cache(s->context, cp, cp + 1);
        if (r <= 0)
            return r;
        s->view(s->context, &c);
    }
    *out = unit_at(c.data, cp - c.first, c.unit);
    return 1;
}
int mm_text_access_fetch(MMI32 cp, MMI32 count, void *output, const MMTextAccessServices *s)
{
    MMTextCache c;
    MMI32 n;
    int r;
    unsigned char *p = (unsigned char *)output;
    while (count > 0)
    {
        n = min(count, MM_TEXT_CACHE_UNITS);
        r = s->cache(s->context, cp, cp + n);
        if (r <= 0)
            return r;
        s->view(s->context, &c);
        memcpy(p, (unsigned char *)c.data + (cp - c.first) * c.unit, (size_t)n * c.unit);
        p += n * c.unit;
        cp += n;
        count -= n;
    }
    return 1;
}
static int flags(MMI32 cp, unsigned *out, const MMTextAccessServices *s)
{
    MMU32 ch;
    int r = s->character(s->context, cp, &ch);
    if (r > 0)
        *out = s->classify(s->context, ch);
    return r;
}
int mm_text_navigate(int kind, MMI32 cp, int word, MMI32 characters, MMI32 *out, const MMTextAccessServices *s)
{
    unsigned f;
    MMI32 original, line = 1;
    int cr = 0, r;
    switch (kind)
    {
    case MM_TEXT_NAV_IS_START:
        if (cp <= 0)
        {
            *out = cp == 0;
            return 1;
        }
        if (cp >= characters)
        {
            *out = 0;
            return 1;
        }
        r = flags(cp, &f, s);
        if (r <= 0)
            return r;
        if (f & MM_TEXT_CHAR_IGNORE)
        {
            *out = 0;
            return 1;
        }
        while (cp-- > 0)
        {
            r = flags(cp, &f, s);
            if (r <= 0)
                return r;
            if (f & MM_TEXT_CHAR_BREAK)
            {
                *out = 1;
                return 1;
            }
            if (!(f & MM_TEXT_CHAR_IGNORE))
            {
                *out = 0;
                return 1;
            }
        }
        s->diagnostic(s->context, MM_TEXT_ISOLATED_LF);
        *out = 0;
        return 1;
    case MM_TEXT_NAV_START:
        if (cp <= 0)
        {
            *out = 0;
            return 1;
        }
        if (cp >= characters)
            cp = characters - 1;
        else
        {
            r = flags(cp, &f, s);
            if (r <= 0)
                return r;
            if (f & MM_TEXT_CHAR_IGNORE)
                ++line;
        }
        original = cp;
        while (cp-- > 0)
        {
            r = flags(cp, &f, s);
            if (r <= 0)
                return r;
            if (f & MM_TEXT_CHAR_BREAK)
            {
                if (cp + line <= original)
                {
                    *out = cp + line;
                    return 1;
                }
                line = 1;
            }
            else if (f & MM_TEXT_CHAR_IGNORE)
                ++line;
            else
                line = 1;
        }
        *out = 0;
        return 1;
    case MM_TEXT_NAV_END:
        if (cp < 0)
        {
            *out = 0;
            return 1;
        }
        if (cp >= characters)
        {
            *out = characters;
            return 1;
        }
        while (cp > 0)
        {
            r = flags(cp, &f, s);
            if (r <= 0)
                return r;
            if (!(f & MM_TEXT_CHAR_IGNORE))
                break;
            --cp;
        }
        for (; cp < characters; ++cp)
        {
            r = flags(cp, &f, s);
            if (r <= 0)
                return r;
            if (f & MM_TEXT_CHAR_BREAK)
            {
                if (cr)
                {
                    *out = cp;
                    return 1;
                }
                cr = 1;
            }
            else if (!(f & MM_TEXT_CHAR_IGNORE) && cr)
            {
                *out = cp;
                return 1;
            }
        }
        if (!cr)
            s->diagnostic(s->context, MM_TEXT_NO_FINAL_CR);
        *out = characters;
        return 1;
    case MM_TEXT_NAV_PREVIOUS:
        if (!word)
        {
            while (cp > 0)
            {
                --cp;
                r = flags(cp, &f, s);
                if (r <= 0)
                    return r;
                if (!(f & MM_TEXT_CHAR_IGNORE))
                    break;
            }
        }
        else
        {
            while (cp > 0)
            {
                r = flags(cp - 1, &f, s);
                if (r <= 0)
                    return r;
                if (!(f & (MM_TEXT_CHAR_IGNORE | MM_TEXT_CHAR_MAY_BREAK)))
                    break;
                --cp;
            }
            while (cp > 0)
            {
                r = flags(cp - 1, &f, s);
                if (r <= 0)
                    return r;
                if (f & (MM_TEXT_CHAR_IGNORE | MM_TEXT_CHAR_MAY_BREAK))
                    break;
                --cp;
            }
        }
        *out = cp;
        return 1;
    default:
        if (cp >= characters)
        {
            *out = characters;
            return 1;
        }
        if (!word)
        {
            while (++cp < characters)
            {
                r = flags(cp, &f, s);
                if (r <= 0)
                    return r;
                if (!(f & MM_TEXT_CHAR_IGNORE))
                    break;
            }
        }
        else
        {
            while (cp < characters)
            {
                r = flags(cp, &f, s);
                if (r <= 0)
                    return r;
                if (f & (MM_TEXT_CHAR_IGNORE | MM_TEXT_CHAR_MAY_BREAK))
                    break;
                ++cp;
            }
            while (cp < characters)
            {
                r = flags(cp, &f, s);
                if (r <= 0)
                    return r;
                if (!(f & (MM_TEXT_CHAR_IGNORE | MM_TEXT_CHAR_MAY_BREAK)))
                    break;
                ++cp;
            }
        }
        *out = cp;
        return 1;
    }
}
int mm_text_find(void *pattern, MMI32 count, unsigned unit, MMI32 first, MMI32 characters, int sensitive,
                 MMI32 *found_first, MMI32 *found_end, const MMTextAccessServices *s)
{
    unsigned char used[32];
    MMI32 i, at, last = count - 1;
    MMU32 ch;
    unsigned low;
    int r;
    if (!sensitive)
    {
        r = s->lower_pattern(s->context, pattern, count);
        if (r <= 0)
            return r;
    }
    memset(used, 0, sizeof(used));
    for (i = 0; i < count; ++i)
    {
        low = (unsigned)unit_at(pattern, i, unit) & 255U;
        used[low / 8] |= (unsigned char)(1U << (low % 8));
    }
    while ((at = first + last) < characters)
    {
        for (i = last;; --i, --at)
        {
            r = s->character(s->context, at, &ch);
            if (r <= 0)
                return r;
            if (!sensitive)
            {
                r = s->lower(s->context, ch, &ch);
                if (r <= 0)
                    return r;
            }
            low = (unsigned)ch & 255U;
            if (!(used[low / 8] & (1U << (low % 8))))
            {
                first = at + 1;
                break;
            }
            if (ch != unit_at(pattern, i, unit))
            {
                ++first;
                break;
            }
            if (at == first)
            {
                *found_first = first;
                *found_end = first + count;
                return 1;
            }
        }
    }
    return 0;
}
