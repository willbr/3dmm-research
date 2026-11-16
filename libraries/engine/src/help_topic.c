#include "mm/help_topic.h"
#include "mm/wide.h"
static MMI32 add(MMI32 a, MMI32 b)
{
    return mm_signed_word((MMU32)a + (MMU32)b);
}
static MMU32 word(const unsigned char *p, int big)
{
    MMU32 value = 0;
    unsigned i;
    for (i = 0; i < 4; ++i)
        value |= (MMU32)p[big ? 3 - i : i] << (i * 8);
    return value;
}
static int control_id(MMHelpInteraction *p, unsigned group, MMI32 *id, const MMHelpTopicServices *s)
{
    int r;
    if (group)
    {
        *id = add(p->base, (MMI32)group);
        r = s->exists(s->context, *id);
        if (r < 0)
            return r;
        if (!r)
            return 1;
    }
    return s->unique(s->context, 1, id);
}
int mm_help_topic_build(MMHelpInteraction *p, int big, const MMHelpTopicServices *s)
{
    MMTextCharacter character;
    MMMaskedRect rect;
    void *owned;
    const unsigned char *data;
    MMU32 type, resource, topic;
    MMI32 cp, end, bytes, maximum = 0, id, x, baseline, width;
    unsigned group;
    int r = s->base(s->context);
    if (r <= 0)
        return r;
    for (cp = 0; cp < s->characters(s->context); cp = end)
    {
        group = 0;
        r = s->scan(s->context, cp, &end, &group);
        if (r < 0)
            return r;
        if ((MMI32)group > maximum)
            maximum = (MMI32)group;
    }
    p->base = 0;
    if (maximum > 0)
    {
        r = s->unique(s->context, maximum, &id);
        if (r <= 0)
            return r;
        p->base = add(id, -1);
        r = s->assign_base(s->context, p->base);
        if (r <= 0)
            return r;
    }
    s->cache(s->context);
    for (cp = 0;; cp = add(cp, 1))
    {
        owned = 0;
        r = s->fetch(s->context, cp, &cp, &owned, &bytes);
        if (r <= 0)
        {
            if (owned)
                s->free(s->context, owned);
            if (r < 0)
                return r;
            break;
        }
        if (!owned)
            continue;
        data = (const unsigned char *)owned;
        type = bytes >= 4 ? word(data, big) : 0;
        if ((type != MM_HELP_EDIT || bytes < 8) && (type != MM_HELP_BUTTON || bytes < 12))
        {
            s->free(s->context, owned);
            continue;
        }
        resource = word(data + 4, big);
        topic = type == MM_HELP_BUTTON ? word(data + 8, big) : MM_TEXT_NIL;
        s->free(s->context, owned);
        r = s->position(s->context, cp, &x, &baseline);
        if (r <= 0)
            return r;
        r = s->character(s->context, cp, &character);
        if (r <= 0)
            return r;
        if (type == MM_HELP_EDIT)
        {
            r = s->font(s->context, &character);
            if (r <= 0)
                return r;
            r = s->measure(s->context, &rect);
            if (r <= 0)
                return r;
            baseline = add(baseline, character.offset);
            rect.top = add(rect.top, baseline);
            rect.bottom = add(rect.bottom, baseline);
            width = mm_signed_word(resource);
            rect.left = add(x, 1);
            rect.right = add(add(x, width), -1);
        }
        group = 0;
        r = s->group(s->context, cp, &group, type == MM_HELP_BUTTON && topic == MM_TEXT_NIL ? &topic : 0);
        if (r < 0)
            return r;
        r = control_id(p, group, &id, s);
        if (r <= 0)
            return r;
        if (type == MM_HELP_EDIT)
        {
            if (character.background == 0xffffffffUL)
                character.background = 0x01ffffffUL;
            r = s->edit(s->context, id, &rect, &character);
        }
        else
            r = s->button(s->context, id, resource, group, topic, x, add(baseline, character.offset));
        if (r <= 0)
            return r;
    }
    s->validate(s->context);
    return 1;
}
