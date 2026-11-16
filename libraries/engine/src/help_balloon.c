#include "mm/help_balloon.h"
#include "mm/wide.h"
static MMI32 add(MMI32 a, MMI32 b)
{
    return mm_signed_word((MMU32)a + (MMU32)b);
}
static MMI32 sub(MMI32 a, MMI32 b)
{
    return mm_signed_word((MMU32)a - (MMU32)b);
}
static MMI32 half(MMI32 a)
{
    return a >= 0 ? a / 2 : -(MMI32)((0 - (MMU32)a) / 2);
}
static MMI32 min(MMI32 a, MMI32 b)
{
    return a < b ? a : b;
}
static MMI32 max(MMI32 a, MMI32 b)
{
    return a > b ? a : b;
}
static void offset(MMMaskedRect *r, MMI32 x, MMI32 y)
{
    r->left = add(r->left, x);
    r->right = add(r->right, x);
    r->top = add(r->top, y);
    r->bottom = add(r->bottom, y);
}
void mm_help_balloon_init(MMHelpBalloon *p)
{
    p->topic = 0;
}
void mm_help_button_init(MMHelpButton *p)
{
    p->group = 0;
    p->topic = 0;
}
void mm_help_balloon_merge(MMHelpTopic *p, const MMHelpTopic *o)
{
    if (!o)
        return;
    if (o->balloon != MM_TEXT_NIL)
        p->balloon = o->balloon;
    if (o->hid)
        p->hid = o->hid;
    if (o->target)
        p->target = o->target;
    if (o->script != MM_TEXT_NIL)
        p->script = o->script;
    p->width = add(p->width, o->width);
    p->height = add(p->height, o->height);
    if (o->sound_number != MM_TEXT_NIL && o->sound_type)
    {
        p->sound_type = o->sound_type;
        p->sound_number = o->sound_number;
    }
}
int mm_help_balloon_new(void **out, const MMHelpTopic *overrides, const MMHelpBalloonFactoryServices *s)
{
    MMHelpTopic topic;
    void *node = 0;
    int r;
    *out = 0;
    r = s->source(s->context, &topic);
    if (r <= 0)
        return r;
    mm_help_balloon_merge(&topic, overrides);
    if (!topic.hid)
    {
        r = s->unique(s->context, &topic.hid);
        if (r <= 0)
            return r;
    }
    else
    {
        r = s->find(s->context, topic.hid, &node);
        if (r < 0)
            return r;
        if (r)
        {
            if (!s->is_balloon(s->context, node))
            {
                s->diagnostic(s->context, MM_HELP_DUPLICATE);
                return 0;
            }
            if (s->review_reuse && s->same_source(s->context, node, topic.balloon))
            {
                r = s->replace(s->context, node, &topic);
                if (r > 0)
                    *out = node;
                return r;
            }
            s->release(s->context, node);
        }
    }
    node = 0;
    r = s->create(s->context, topic.hid, &node);
    if (r <= 0)
        goto fail;
    s->snapshot(s->context, node);
    r = s->initialize(s->context, node, &topic);
    if (r <= 0)
        goto fail;
    r = s->enter(s->context, node);
    if (r <= 0)
    {
        if (!r)
            s->diagnostic(s->context, MM_HELP_ENTER_DEAD);
        return r;
    }
    r = s->hit(s->context, node);
    if (r <= 0)
        return r;
    r = s->alive(s->context, node);
    if (r <= 0)
    {
        if (!r)
            s->diagnostic(s->context, MM_HELP_HIT_DEAD);
        return r;
    }
    s->validate(s->context, node);
    *out = node;
    return 1;
fail:
    if (node)
        s->release(s->context, node);
    return r;
}
int mm_help_balloon_read(void **out, MMU32 number, const MMHelpBalloonReadServices *s)
{
    void *source = 0, *document = 0;
    int r;
    *out = 0;
    r = s->find(s->context, number, &source);
    if (r <= 0)
        return r;
    r = s->read(s->context, source, number, &document);
    if (r <= 0)
        goto done;
    r = s->hide(s->context, document);
    if (r <= 0)
        goto done;
    r = s->build(s->context, document, out);
done:
    if (document)
        s->release(s->context, document);
    return r;
}
int mm_help_balloon_topic(MMHelpBalloon *p, const MMHelpBalloonTopicServices *s)
{
    void *previous = p->topic, *target = 0;
    MMHelpTopic info;
    MMHelpPoint point, reg;
    MMMaskedRect rc;
    int r;
    r = s->create(s->context, &p->topic);
    if (r < 0)
        return r;
    if (!r)
        goto fail;
    s->info(s->context, &info);
    r = s->assign(s->context, p->topic, 0, info.sound_type);
    if (r < 0)
        return r;
    if (!r)
        goto fail;
    s->info(s->context, &info);
    r = s->assign(s->context, p->topic, 1, info.sound_number);
    if (r < 0)
        return r;
    if (!r)
        goto fail;
    if (previous)
        s->release(s->context, previous);
    r = s->natural(s->context, p->topic);
    if (r <= 0)
        return r;
    s->info(s->context, &info);
    if (info.target)
    {
        r = s->find(s->context, info.target, &target);
        if (r < 0)
            return r;
        if (!r)
            target = 0;
    }
    if (!target)
        target = s->parent(s->context);
    if (s->kid(s->context, target))
        r = s->registration(s->context, target, &point);
    else
    {
        r = s->bounds(s->context, target, &rc);
        if (r > 0)
        {
            point.x = half(add(rc.left, rc.right));
            point.y = half(add(rc.top, rc.bottom));
        }
    }
    if (r <= 0)
        return r;
    r = s->map_target(s->context, target, &point);
    if (r <= 0)
        return r;
    r = s->map_parent(s->context, &point);
    if (r <= 0)
        return r;
    r = s->own_registration(s->context, &reg);
    if (r <= 0)
        return r;
    s->info(s->context, &info);
    return s->representation(s->context, add(sub(point.x, reg.x), info.width), add(sub(point.y, reg.y), info.height));
fail:
    if (p->topic)
    {
        s->release(s->context, p->topic);
        p->topic = 0;
    }
    p->topic = previous;
    return 0;
}
int mm_help_balloon_apply(int initialize, const MMHelpBalloonStages *s)
{
    int r;
    if (initialize)
    {
        r = s->base(s->context);
        if (r <= 0)
            return r;
    }
    r = s->set(s->context);
    return r <= 0 || initialize ? r : s->enter(s->context);
}
int mm_help_balloon_layout(MMI32 x, MMI32 y, const MMHelpBalloonLayoutServices *s)
{
    MMMaskedRect own, parent, intersection, content, child;
    MMI32 dx, dy, width, height;
    int r;
    r = s->base(s->context, x, y);
    if (r <= 0)
        return r;
    r = s->bounds(s->context, 0, &own);
    if (r <= 0)
        return r;
    r = s->bounds(s->context, 1, &parent);
    if (r <= 0)
        return r;
    intersection.left = max(own.left, parent.left);
    intersection.right = min(own.right, parent.right);
    intersection.top = max(own.top, parent.top);
    intersection.bottom = min(own.bottom, parent.bottom);
    if (intersection.left >= intersection.right || intersection.top >= intersection.bottom)
        intersection.left = intersection.top = intersection.right = intersection.bottom = 0;
    if (intersection.left != own.left || intersection.top != own.top || intersection.right != own.right ||
        intersection.bottom != own.bottom)
    {
        dx = max(min(0, sub(parent.right, own.right)), sub(parent.left, own.left));
        dy = max(min(0, sub(parent.bottom, own.bottom)), sub(parent.top, own.top));
        offset(&own, dx, dy);
        r = s->position(s->context, 0, &own);
        if (r <= 0)
            return r;
    }
    r = s->bounds(s->context, 2, &content);
    if (r <= 0)
        return r;
    s->preferred(s->context, &width, &height);
    child.left = half(sub(add(content.left, content.right), width));
    child.right = add(child.left, width);
    child.top = half(sub(add(content.top, content.bottom), height));
    child.bottom = add(child.top, height);
    return s->position(s->context, 1, &child);
}
int mm_help_button_new(void **out, MMI32 id, unsigned group, MMU32 topic, MMI32 x, MMI32 bottom,
                       const MMHelpButtonFactoryServices *s)
{
    void *node = 0;
    MMHelpButton *state;
    MMMaskedRect rc;
    int r;
    *out = 0;
    r = s->find(s->context, id);
    if (r < 0)
        return r;
    if (r)
    {
        s->diagnostic(s->context, MM_HELP_DUPLICATE);
        return 0;
    }
    r = s->create(s->context, id, &node);
    if (r <= 0)
        goto fail;
    state = s->state(s->context, node);
    state->group = group;
    state->topic = topic;
    r = s->initialize(s->context, node);
    if (r <= 0)
        goto fail;
    r = s->enter(s->context, node);
    if (r <= 0)
    {
        if (!r)
            s->diagnostic(s->context, MM_HELP_BUTTON_DEAD);
        return r;
    }
    r = s->bounds(s->context, node, &rc);
    if (r <= 0)
        return r;
    offset(&rc, sub(x, rc.left), sub(bottom, rc.bottom));
    r = s->position(s->context, node, &rc);
    if (r <= 0)
        return r;
    s->validate(s->context, node);
    *out = node;
    return 1;
fail:
    if (node)
        s->release(s->context, node);
    return r;
}
int mm_help_button_point(const MMHelpButton *p, MMI32 x, MMI32 y, const MMHelpButtonServices *s)
{
    void *parent = 0;
    MMHelpPoint point;
    unsigned group = 0;
    MMU32 topic = 0;
    int r = s->base_point(s->context, x, y);
    if (r)
        return r;
    if (!p->group)
        return 0;
    r = s->parent_topic(s->context, &parent);
    if (r <= 0)
        return r;
    point.x = x;
    point.y = y;
    r = s->map(s->context, &point);
    if (r <= 0)
        return r;
    r = s->group(s->context, parent, &point, &group, &topic);
    return r <= 0 ? r : group == p->group && topic == p->topic;
}
int mm_help_button_click(const MMHelpButton *p, const MMHelpButtonServices *s)
{
    void *parent = 0;
    MMI32 id = s->id(s->context);
    int r = s->parent_topic(s->context, &parent);
    if (r < 0)
        return r;
    if (!r)
    {
        s->diagnostic(s->context, MM_HELP_BAD_PARENT);
        return 1;
    }
    r = s->hit(s->context, parent, p->group, p->topic, s->flags(s->context), id);
    return r < 0 ? r : 1;
}
