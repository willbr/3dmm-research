#include "mm/help_interaction.h"
#include "mm/wide.h"
#include <string.h>
void mm_help_interaction_init(MMHelpInteraction *p)
{
    memset(p, 0, sizeof(*p));
}
int mm_help_track(MMHelpInteraction *p, const MMHelpInteractionServices *s)
{
    MMHelpMouse m;
    MMU32 flags, topic = 0;
    unsigned group = 0;
    int r;
    s->mouse(s->context, &m);
    r = s->adjust(s->context, m.flags, &flags);
    if (r <= 0)
        return r;
    s->publish_flags(s->context, flags);
    s->mouse(s->context, &m);
    s->check_tracking(s->context, m.down);
    if (m.down)
    {
        r = s->hit(s->context, m.x, m.y, &p->group, &p->topic);
        if (r < 0)
            return r;
        if (!r)
            return 1;
        r = s->track(s->context, 1);
        if (r <= 0)
            return r;
        s->mouse(s->context, &m);
        r = s->cursor(s->context, m.flags);
        if (r <= 0)
            return r;
        s->mouse(s->context, &m);
        p->flags = m.flags;
    }
    else if (!(m.flags & 8))
    {
        r = s->track(s->context, 0);
        if (r <= 0)
            return r;
        s->mouse(s->context, &m);
        r = s->hit(s->context, m.x, m.y, &group, &topic);
        if (r < 0)
            return r;
        if (r && group == p->group && topic == p->topic)
        {
            r = s->do_hit(s->context, group, topic, p->flags, 0);
            if (r < 0)
                return r;
        }
    }
    return 1;
}
int mm_help_bad_key(MMHelpInteraction *p, const MMHelpInteractionServices *s)
{
    MMHelpKey k;
    MMU32 flags;
    MMI32 low = mm_signed_word((MMU32)p->base + 1), high = mm_signed_word((MMU32)p->base + 257);
    int r;
    s->key(s->context, &k);
    if (k.id < low || k.id >= high)
        return 0;
    r = s->adjust(s->context, k.flags, &flags);
    if (r <= 0)
        return r;
    s->publish_flags(s->context, flags);
    s->key(s->context, &k);
    r = s->run(s->context, ((MMU32)k.id - (MMU32)p->base) & 255, k.flags, k.id, k.character, MM_TEXT_NIL, 0);
    return r < 0 ? r : 1;
}
int mm_help_mouse_move(const MMHelpInteractionServices *s)
{
    MMHelpMouse m;
    MMU32 flags;
    int r;
    s->mouse(s->context, &m);
    r = s->adjust(s->context, m.flags, &flags);
    if (r <= 0)
        return r;
    s->mouse(s->context, &m);
    r = s->hit(s->context, m.x, m.y, 0, 0);
    if (r < 0)
        return r;
    if (r)
        flags |= 512;
    return s->cursor(s->context, flags);
}
int mm_help_hit(unsigned group, MMU32 topic, MMU32 flags, MMI32 id, const MMHelpInteractionServices *s)
{
    MMI32 value = 0;
    int r = s->run(s->context, group, flags, id, 0, topic, &value);
    if (r <= 0)
        return r;
    if (topic != MM_TEXT_NIL && !value)
        return s->balloon(s->context, topic);
    return 1;
}
int mm_help_point(MMI32 x, MMI32 y, const MMHelpInteractionServices *s)
{
    int r = s->base_point(s->context, x, y);
    return r <= 0 ? r : s->hit(s->context, x, y, 0, 0);
}
int mm_help_group_point(MMI32 x, MMI32 y, const MMHelpInteractionServices *s)
{
    MMI32 cp;
    int r = s->locate(s->context, x, y, &cp);
    return r <= 0 ? r : s->group_at(s->context, cp);
}
int mm_help_cursor(void *node, MMU32 flags, const MMHelpInteractionServices *s)
{
    do
    {
        node = s->parent(s->context, node);
    } while (node && !s->kid(s->context, node));
    return s->set_cursor(s->context, node, node ? flags | 256 : flags);
}
int mm_help_script(unsigned group, MMU32 flags, MMI32 id, MMU32 character, MMU32 topic, const MMHelpScriptServices *s)
{
    MMI32 args[5];
    MMU32 number;
    void *script = 0, *interpreter = 0;
    int r, alive = 1;
    s->reset_return(s->context);
    r = s->number(s->context, &number);
    if (r <= 0)
        return r;
    if (number == MM_TEXT_NIL)
        return 1;
    r = s->fetch(s->context, number, &script);
    if (r <= 0)
        goto done;
    r = s->create(s->context, &interpreter);
    if (r <= 0)
        goto done;
    s->snapshot(s->context);
    args[0] = mm_signed_word((MMU32)group);
    args[1] = mm_signed_word(flags);
    args[2] = id;
    args[3] = (MMI32)(character & 255);
    args[4] = mm_signed_word(topic);
    r = s->run(s->context, interpreter, script, args);
    if (r < 0)
        goto done;
    if (!r)
        s->reset_return(s->context);
    s->release(s->context, 1, interpreter);
    interpreter = 0;
    alive = s->alive(s->context);
    if (alive < 0)
        r = alive;
done:
    if (interpreter)
        s->release(s->context, 1, interpreter);
    if (script)
        s->release(s->context, 0, script);
    return r < 0 ? r : alive;
}
