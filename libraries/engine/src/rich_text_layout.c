/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "mm/rich_text_layout.h"
#include "mm/wide.h"
#include <string.h>

#define TRY(expr) do { int status_ = (expr); if (status_ < 0) return status_; } while (0)
static MMI32 minimum(MMI32 a, MMI32 b) { return a < b ? a : b; }
static MMI32 maximum(MMI32 a, MMI32 b) { return a > b ? a : b; }
static MMI32 add(MMI32 a, MMI32 b) { return mm_signed_word((MMU32)a + (MMU32)b); }
static MMI32 sub(MMI32 a, MMI32 b) { return mm_signed_word((MMU32)a - (MMU32)b); }
static MMI32 short_value(MMI32 value)
{
    MMU32 n = (MMU32)value & 65535U;
    return n < 32768U ? (MMI32)n : (MMI32)n - 65536L;
}
/* The only wide multiplication in layout is signed short * signed 32-bit,
 * divided by 256. Split the positive product, then truncate toward zero. */
static MMI32 scale(MMI32 value, MMI32 factor)
{
    MMU32 a = value < 0 ? 0U - (MMU32)value : (MMU32)value;
    MMU32 b = factor < 0 ? 0U - (MMU32)factor : (MMU32)factor;
    MMU32 low = a * (b & 65535U), high = a * (b >> 16);
    MMU32 word = (low >> 8) + (high << 8);
    return mm_signed_word((value < 0) != (factor < 0) ? 0U - word : word);
}
/* C89 permits either rounding direction for negative signed division. */
static MMI32 divide(MMI32 a, MMI32 b)
{
    MMU32 x = a < 0 ? 0U - (MMU32)a : (MMU32)a;
    MMU32 y = b < 0 ? 0U - (MMU32)b : (MMU32)b;
    MMU32 q = x / y;
    return mm_signed_word((a < 0) != (b < 0) ? 0U - q : q);
}
MMU32 mm_rich_text_flags(MMU32 ch)
{
    switch (ch)
    {
    case '\r': return MM_RICH_BREAK | MM_RICH_MAY_BREAK | 16;
    case '\n': return MM_RICH_IGNORE | 16;
    case '\t': return MM_RICH_TAB | MM_RICH_MAY_BREAK | 16;
    case ' ': return MM_RICH_WHITE | MM_RICH_MAY_BREAK;
    case 127: return 16;
    default: return ch < 32 ? 16 : 0;
    }
}
static int call(const MMRichTextProvider *p, MMRichTextOperation op, MMRichTextRequest *q)
{
    return p->call(p->context, op, q);
}
static void *buffer_at(MMRichTextRun *r, MMI32 index)
{
    return (unsigned char *)r->provider.characters + index * r->provider.unitBytes;
}
static MMU32 character(MMRichTextRun *r, MMI32 index)
{
    unsigned short unit;
    if (r->provider.unitBytes == 1)
        return *(unsigned char *)buffer_at(r, index);
    memcpy(&unit, buffer_at(r, index), sizeof(unit));
    return unit;
}
static int flags(MMRichTextRun *r, MMU32 ch, MMU32 *out)
{
    MMRichTextRequest q;
    q.character = ch;
    TRY(call(&r->provider, MM_RICH_FLAGS, &q));
    *out = q.flags;
    return 0;
}
int mm_rich_text_run_init(MMRichTextRun *r, const MMRichTextProvider *p,
                         const MMRichTextParagraph *pap, MMI32 cpMin, MMI32 cpLim,
                         MMI32 xpBase, MMI32 width, MMI32 xpBreak)
{
    MMRichTextRequest q;
    if (!p || !p->call || !p->characters ||
        (p->unitBytes != 1 && (p->unitBytes != 2 || sizeof(unsigned short) != 2)))
        return -1;
    TRY(call(p, MM_RICH_COPY_STYLE, &q));
    r->provider = *p;
    r->paragraph = *pap;
    r->lineBreak = 0;
    r->cpMin = cpMin; r->cpLim = cpLim; r->cpLimFetch = cpMin;
    r->xpMin = xpBase; r->xpBreak = minimum(xpBreak, width);
    if (!xpBase)
    {
        if (pap->indent == 1 || pap->indent == 2)
        {
            q.cp = cpMin;
            TRY(call(p, MM_RICH_PARAGRAPH_START, &q));
            if ((pap->indent == 1 && q.value) || (pap->indent == 2 && !q.value))
                r->xpMin = add(r->xpMin, pap->tab);
        }
        else if (pap->indent == 3)
            r->xpMin = add(r->xpMin, pap->tab);
    }
    if (pap->indent == 3)
        r->xpBreak = minimum(r->xpBreak, sub(width, pap->tab));
    r->data.cpLim = r->data.cpLimDraw = cpMin;
    r->data.xpLim = r->data.xpLimDraw = r->xpMin;
    r->opportunity = r->data;
    TRY(call(p, MM_RICH_SET_FONT, &q));
    TRY(call(p, MM_RICH_WARM_FONT, &q));
    q.characters = NULL; q.count = 0;
    TRY(call(p, MM_RICH_MEASURE, &q));
    { MMI32 top = q.top, bottom = q.bottom;
      TRY(call(p, MM_RICH_OFFSET, &q));
      r->ascent = maximum(0, sub(sub(0, top), q.value));
      TRY(call(p, MM_RICH_OFFSET, &q));
      r->descent = maximum(0, add(bottom, q.value)); }
    return 0;
}
static int fit(MMRichTextRun *r)
{
    MMRichTextRequest q;
    if (r->data.cpLimDraw == r->cpMin)
        r->data.xpLim = r->data.xpLimDraw = r->xpMin;
    else
    {
        q.characters = r->provider.characters;
        q.count = sub(r->data.cpLimDraw, r->cpMin);
        TRY(call(&r->provider, MM_RICH_MEASURE, &q));
        r->data.xpLim = r->data.xpLimDraw = add(r->xpMin, sub(q.right, q.left));
    }
    return r->data.xpLimDraw <= r->xpBreak;
}
static int skip_ignores(MMRichTextRun *r)
{
    MMU32 bits;
    MMRichTextRequest q;
    unsigned short unit = 0;
    while (r->data.cpLim < r->cpLimFetch)
    {
        TRY(flags(r, character(r, sub(r->data.cpLim, r->cpMin)), &bits));
        if (!(bits & MM_RICH_IGNORE)) break;
        r->data.cpLim = add(r->data.cpLim, 1);
    }
    if (r->data.cpLim == r->cpLimFetch)
    {
        MMI32 length;
        TRY(call(&r->provider, MM_RICH_LENGTH, &q)); length = q.value;
        while (r->data.cpLim < length)
        {
            q.cp = r->data.cpLim; q.count = 1; q.characters = &unit;
            TRY(call(&r->provider, MM_RICH_FETCH, &q));
            TRY(flags(r, r->provider.unitBytes == 1 ? *(unsigned char *)&unit : unit, &bits));
            if (!(bits & MM_RICH_IGNORE)) return 0;
            r->data.cpLim = add(r->data.cpLim, 1);
        }
    }
    return 0;
}
static int set_opportunity(MMRichTextRun *r)
{
    MMRichTextRequest q;
    r->lineBreak = 1;
    if (r->opportunity.cpLim <= r->cpMin && r->mustAdvance)
    {
        MMI32 low = 0, high = sub(r->data.cpLimDraw, r->cpMin), middle;
        MMI32 dx = sub(r->xpBreak, r->xpMin);
        while (low < high)
        {
            middle = add(divide(add(low, high), 2), 1);
            q.characters = r->provider.characters; q.count = middle;
            TRY(call(&r->provider, MM_RICH_MEASURE, &q));
            if (sub(q.right, q.left) <= dx) low = middle;
            else high = sub(middle, 1);
        }
        if (!low) low = 1;
        r->data.cpLim = r->data.cpLimDraw = add(r->cpMin, low);
        TRY(fit(r));
    }
    else r->data = r->opportunity;
    return skip_ignores(r);
}
static int tabs(MMRichTextRun *r)
{
    MMU32 bits;
    int fits = fit(r);
    TRY(fits);
    if (!fits) return set_opportunity(r);
    while (r->data.cpLim < r->cpLimFetch)
    {
        TRY(flags(r, character(r, sub(r->data.cpLim, r->cpMin)), &bits));
        if (!(bits & MM_RICH_TAB)) break;
        r->data.cpLim = add(r->data.cpLim, 1);
        if (!r->paragraph.tab) return -1;
        r->data.xpLim = mm_signed_word((MMU32)divide(sub(add(add(r->data.xpLim, 1), r->paragraph.tab), 1),
                                                    r->paragraph.tab) * (MMU32)r->paragraph.tab);
        if (r->data.xpLim > r->xpBreak)
        {
            if (r->data.cpLim == add(r->cpMin, 1) && r->mustAdvance)
            {
                TRY(skip_ignores(r)); r->lineBreak = 1;
            }
            else TRY(set_opportunity(r));
            return 0;
        }
        r->data.xpLimDraw = r->data.xpLim;
        r->opportunity = r->data;
    }
    return skip_ignores(r);
}
int mm_rich_text_run_next(MMRichTextRun *r, int mustAdvance)
{
    MMRichTextRequest q;
    MMU32 bits, ch;
    int fits;
    if (r->data.cpLim >= add(r->cpMin, 1) && r->data.cpLim < r->cpLimFetch)
        memmove(r->provider.characters, buffer_at(r, sub(r->data.cpLim, r->cpMin)),
                sub(r->cpLimFetch, r->data.cpLim) * r->provider.unitBytes);
    r->cpMin = r->data.cpLimDraw = r->data.cpLim;
    r->xpMin = r->data.xpLimDraw = r->data.xpLim;
    r->xpBreak = maximum(r->xpBreak, r->xpMin);
    r->opportunity = r->data;
    if (r->cpMin >= r->cpLim) return 0;
    if (r->cpLimFetch < r->cpLim && r->cpLimFetch < add(r->cpMin, MM_RICH_BUFFER))
    {
        MMI32 cp = maximum(r->cpMin, r->cpLimFetch);
        r->cpLimFetch = add(minimum(MM_RICH_BUFFER, sub(r->cpLim, r->cpMin)), r->cpMin);
        q.cp = cp; q.count = sub(r->cpLimFetch, cp); q.characters = buffer_at(r, sub(cp, r->cpMin));
        TRY(call(&r->provider, MM_RICH_FETCH, &q));
    }
    r->mustAdvance = mustAdvance; r->lineBreak = r->object = 0;
    TRY(call(&r->provider, MM_RICH_SET_FONT, &q));
    for (;;)
    {
        bits = 0; ch = 0;
        if (r->data.cpLim < r->cpLimFetch)
        {
            ch = character(r, sub(r->data.cpLim, r->cpMin));
            TRY(flags(r, ch, &bits));
        }
        if (r->data.cpLim >= r->cpLimFetch || (bits & MM_RICH_IGNORE))
        {
            fits = fit(r); TRY(fits);
            return !fits ? set_opportunity(r) : skip_ignores(r);
        }
        if (bits & MM_RICH_TAB) return tabs(r);
        if (bits & MM_RICH_BREAK)
        {
            fits = fit(r); TRY(fits);
            if (!fits) return set_opportunity(r);
            r->data.cpLim = add(r->data.cpLim, 1);
            TRY(skip_ignores(r));
            r->opportunity = r->data; r->lineBreak = 1;
            return 0;
        }
        if (bits & MM_RICH_MAY_BREAK)
        {
            r->data.cpLimDraw = r->data.cpLim = add(r->data.cpLim, 1);
            fits = fit(r); TRY(fits);
            if (fits) { r->opportunity = r->data; continue; }
            r->lineBreak = 1;
            if (bits & MM_RICH_WHITE)
            {
                MMI32 x = r->data.xpLim;
                r->data.cpLimDraw = sub(r->data.cpLimDraw, 1);
                fits = fit(r); TRY(fits);
                if (fits)
                {
                    r->data.xpLim = x; r->opportunity = r->data;
                    return skip_ignores(r);
                }
            }
            return set_opportunity(r);
        }
        if (ch == 1)
        {
            if (r->data.cpLim > r->cpMin)
            {
                fits = fit(r); TRY(fits);
                if (!fits) return set_opportunity(r);
                r->opportunity = r->data;
                return 0;
            }
            q.cp = r->data.cpLim;
            TRY(call(&r->provider, MM_RICH_OBJECT, &q));
            if (!q.value)
            {
                r->data.cpLimDraw = r->data.cpLim = add(r->data.cpLim, 1);
                continue;
            }
            { MMI32 right = q.right, top = q.top, bottom = q.bottom;
              TRY(call(&r->provider, MM_RICH_OFFSET, &q));
              top = add(top, q.value); bottom = add(bottom, q.value);
              if (mustAdvance || add(r->xpMin, right) <= r->xpBreak)
              {
                  r->object = 1;
                  r->data.cpLimDraw = r->data.cpLim = add(r->data.cpLim, 1);
                  r->data.xpLim = r->data.xpLimDraw = add(r->xpMin, right);
                  r->opportunity = r->data;
                  r->ascent = maximum(r->ascent, sub(0, top));
                  r->descent = maximum(r->descent, bottom);
                  if (add(r->xpMin, right) >= r->xpBreak) r->lineBreak = 1;
                  TRY(skip_ignores(r));
              } }
            return 0;
        }
        r->data.cpLimDraw = r->data.cpLim = add(r->data.cpLim, 1);
    }
}
int mm_rich_text_line(const MMRichTextProvider *p, MMI32 width, MMI32 cpMin, MMI32 y)
{
    typedef struct LineRun { MMI32 cp, x, ascent, descent; } LineRun;
    MMRichTextRequest q;
    MMRichTextParagraph pap;
    MMRichTextRun chr;
    LineRun run, sure, previous;
    MMI32 paragraphLimit, styleLimit = cpMin;
    int paragraphStart;
    memset(&chr, 0, sizeof(chr));
    q.cp = cpMin;
    TRY(call(p, MM_RICH_FETCH_PARAGRAPH, &q));
    pap = q.paragraph; paragraphLimit = q.value;
    run.cp = cpMin; run.x = run.ascent = run.descent = 0; sure = run;
    for (;;)
    {
        if (run.cp >= styleLimit)
        {
            q.cp = run.cp;
            TRY(call(p, MM_RICH_FETCH_STYLE, &q));
            styleLimit = minimum(q.value, paragraphLimit);
            if (styleLimit <= run.cp)
            {
                TRY(call(p, MM_RICH_INVALID_STYLE, &q));
                break;
            }
            TRY(mm_rich_text_run_init(&chr, p, &pap, run.cp, styleLimit, run.x, width, width));
        }
        TRY(mm_rich_text_run_next(&chr, sure.cp == cpMin));
        if (chr.data.cpLim == run.cp) { run = sure; break; }
        previous = run;
        run.cp = chr.data.cpLim; run.x = chr.data.xpLimDraw;
        run.ascent = maximum(run.ascent, chr.ascent); run.descent = maximum(run.descent, chr.descent);
        if (chr.lineBreak)
        {
            if (run.x > chr.xpBreak && previous.cp > cpMin) run = previous;
            break;
        }
        if (chr.opportunity.cpLim > previous.cp)
        {
            sure.cp = chr.opportunity.cpLim; sure.x = chr.opportunity.xpLimDraw;
            sure.ascent = maximum(sure.ascent, chr.ascent); sure.descent = maximum(sure.descent, chr.descent);
        }
    }
    q.line.xpLeft = pap.justification == 1 ? short_value(sub(width, run.x)) :
                    pap.justification == 2 ? short_value(divide(sub(width, run.x), 2)) : 0;
    q.line.ccp = short_value(sub(run.cp, cpMin));
    q.line.dyp = short_value(add(run.ascent, run.descent));
    if (pap.lineScale != 256) q.line.dyp = short_value(scale(q.line.dyp, pap.lineScale));
    q.line.dyp = short_value(add(q.line.dyp, pap.extraLine));
    q.published = 1;
    TRY(call(p, MM_RICH_PUBLISH, &q));
    q.cp = run.cp;
    TRY(call(p, MM_RICH_PARAGRAPH_START, &q)); paragraphStart = q.value;
    if (!paragraphStart)
    {
        TRY(call(p, MM_RICH_LENGTH, &q)); paragraphStart = run.cp == q.value;
    }
    if (paragraphStart)
    {
        if (pap.afterScale != 256) q.line.dyp = short_value(scale(q.line.dyp, pap.afterScale));
        q.line.dyp = short_value(add(q.line.dyp, pap.extraAfter));
    }
    if (q.line.dyp <= 0) q.line.dyp = 1;
    q.line.dypAscent = short_value(run.ascent);
    if (q.line.dypAscent <= 0) q.line.dypAscent = 1;
    q.line.cpMin = cpMin; q.line.dypTot = y;
    q.published = 3;
    return call(p, MM_RICH_PUBLISH, &q);
}
