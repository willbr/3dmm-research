#include "mm/help_display.h"
#include "mm/wide.h"
static MMU32 word(const unsigned char *p, int big)
{
    MMU32 v = 0;
    unsigned i;
    for (i = 0; i < 4; ++i)
        v |= (MMU32)p[big ? 3 - i : i] << (i * 8);
    return v;
}
static MMI32 add(MMI32 a, MMI32 b)
{
    return mm_signed_word((MMU32)a + (MMU32)b);
}
static MMI32 sub(MMI32 a, MMI32 b)
{
    return mm_signed_word((MMU32)a - (MMU32)b);
}
static void offset(MMMaskedRect *p, MMI32 x, MMI32 y)
{
    p->left = add(p->left, x);
    p->right = add(p->right, x);
    p->top = add(p->top, y);
    p->bottom = add(p->bottom, y);
}
static int header(MMI32 index, unsigned kind, int big, MMTextChunk *p, const MMHelpDisplayServices *s)
{
    MMGroup rows;
    const unsigned char *data;
    MMI32 bytes;
    if (kind != 192)
        return 0;
    rows = s->arguments(s->context);
    data = mm_text_argument_payload(&rows, index, &bytes);
    if (!data || bytes < 8)
        return 0;
    p->type = word(data, big);
    p->number = word(data + 4, big);
    return 1;
}
static int fetch(const MMTextChunk *p, void **owned, const MMHelpDisplayServices *s)
{
    MMTextChunk child;
    void *cache = 0;
    int r;
    if (p->type == MM_HELP_BUTTON)
    {
        r = s->find(s->context, p, &cache);
        if (r <= 0)
            return r;
        r = s->child(s->context, cache, p, 0x10000UL, &child);
        if (r <= 0)
            return r;
        p = &child;
    }
    return s->fetch(s->context, cache, p, owned);
}
void mm_help_display_init(MMHelpDisplay *p)
{
    p->hide_buttons = 0;
}
void mm_help_display_hide(MMHelpDisplay *p, int hidden)
{
    p->hide_buttons = hidden != 0;
}
int mm_help_object_bounds(MMI32 index, unsigned kind, int big, MMMaskedRect *out, const MMHelpDisplayServices *s)
{
    MMTextChunk p;
    void *bitmap = 0;
    int r;
    if (!header(index, kind, big, &p, s))
        return 0;
    if (p.type == MM_HELP_EDIT)
    {
        r = s->font(s->context);
        if (r <= 0)
            return r;
        r = s->measure(s->context, 0, out);
        if (r <= 0)
            return r;
        out->top = sub(out->top, 1);
        out->bottom = add(out->bottom, 1);
        out->left = 0;
        out->right = mm_signed_word(p.number);
        return 1;
    }
    if (p.type != MM_HELP_BITMAP && p.type != MM_HELP_BUTTON)
        return 0;
    r = fetch(&p, &bitmap, s);
    if (r > 0)
        r = s->rectangle(s->context, bitmap, out);
    if (bitmap)
        s->release(s->context, bitmap);
    if (r <= 0)
        return r;
    offset(out, sub(0, out->left), sub(0, out->bottom));
    return 1;
}
int mm_help_object_draw(const MMHelpDisplay *display, MMI32 index, unsigned kind, MMI32 baseline, int big,
                        const MMHelpDisplayServices *s)
{
    MMTextChunk p;
    MMMaskedRect rc;
    void *bitmap = 0;
    int r, visible;
    if (!header(index, kind, big, &p, s))
        return 0;
    if (p.type == MM_HELP_EDIT)
    {
        r = s->font(s->context);
        if (r <= 0)
            return r;
        r = s->measure(s->context, baseline, &rc);
        if (r <= 0)
            return r;
        rc.top = sub(rc.top, 1);
        rc.bottom = add(rc.bottom, 1);
        rc.left = s->x(s->context);
        rc.right = add(rc.left, mm_signed_word(p.number));
        s->publish_x(s->context, rc.right);
        r = s->pen(s->context);
        if (r <= 0)
            return r;
        r = s->frame(s->context, &rc, 0, 0x01000000UL);
        if (r <= 0)
            return r;
        rc.left = add(rc.left, 1);
        rc.right = sub(rc.right, 1);
        rc.top = add(rc.top, 1);
        rc.bottom = sub(rc.bottom, 1);
        return s->fill(s->context, &rc, s->color(s->context, 0));
    }
    if (p.type != MM_HELP_BITMAP && p.type != MM_HELP_BUTTON)
        return 0;
    visible = p.type != MM_HELP_BUTTON || !display->hide_buttons;
    r = fetch(&p, &bitmap, s);
    if (r <= 0)
        goto done;
    r = s->rectangle(s->context, bitmap, &rc);
    if (r <= 0)
        goto done;
    offset(&rc, sub(s->x(s->context), rc.left), sub(baseline, rc.bottom));
    if (s->color(s->context, 0) != 0xffffffffUL)
    {
        r = s->fill(s->context, &rc, s->color(s->context, 0));
        if (r <= 0)
            goto done;
    }
    if (visible)
    {
        r = s->draw(s->context, bitmap, &rc);
        if (r <= 0)
            goto done;
    }
    s->release(s->context, bitmap);
    bitmap = 0;
    if (s->style(s->context) & 8)
    {
        r = s->pen(s->context);
        if (r <= 0)
            return r;
        r = s->frame(s->context, &rc, 1, s->color(s->context, 1));
        if (r <= 0)
            return r;
    }
    s->publish_x(s->context, add(s->x(s->context), sub(rc.right, rc.left)));
    return 1;
done:
    if (bitmap)
        s->release(s->context, bitmap);
    return r;
}
