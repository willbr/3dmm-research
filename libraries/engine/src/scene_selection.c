/* Scene selection converted from engine/scene.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "three_mm/scene_selection.h"
#include "three_mm/wide.h"
#include <stddef.h>
static void set_selected(const ThreeMMSceneSelectionServices *s, int text, void *object)
{
    if (text)
        s->state->text = object;
    else
        s->state->actor = object;
    s->publish(s->context, text, object);
}
static void notify(const ThreeMMSceneSelectionServices *s)
{
    s->invalidate(s->context);
    s->action_menu(s->context);
}
static void clear_extras(const ThreeMMSceneSelectionServices *s)
{
    ThreeMMI32 i;
    void *actor;
    for (i = 0; i < s->count(s->context, 1); ++i)
    {
        actor = s->at(s->context, 1, i);
        if (actor)
            s->hilite(s->context, actor, 0);
    }
    s->clear_extras(s->context);
}
void three_mm_scene_select_actor(void *actor, const ThreeMMSceneSelectionServices *s)
{
    void *old;
    if (s->view_mode(s->context) == 0)
    {
        old = s->state->actor;
        if (old)
            s->hilite(s->context, old, 0);
        clear_extras(s);
        if (actor)
            s->hilite(s->context, actor, 1);
        old = s->state->text;
        if (old)
            s->select_text(s->context, old, 0);
    }
    s->invalidate(s->context);
    set_selected(s, 0, actor);
    s->action_menu(s->context);
}
void three_mm_scene_select_text(void *text, const ThreeMMSceneSelectionServices *s)
{
    void *old;
    s->invalidate(s->context);
    if (s->view_mode(s->context) == 1)
    {
        old = s->state->actor;
        if (old)
        {
            s->hilite(s->context, old, 0);
            s->action_menu(s->context);
        }
        clear_extras(s);
        old = s->state->text;
        if (text == old && (!text || s->text_selected(s->context, text)))
            return;
        if (old)
            s->select_text(s->context, old, 0);
        if (text)
            s->select_text(s->context, text, 1);
    }
    set_selected(s, 1, text);
    s->text_notice(s->context);
}
ThreeMMI32 three_mm_scene_selection_count(const ThreeMMSceneSelectionServices *s)
{
    return s->state->actor ? 1 + s->count(s->context, 1) : 0;
}
void *three_mm_scene_selection_at(ThreeMMI32 index, const ThreeMMSceneSelectionServices *s)
{
    if (index < 0 || index >= three_mm_scene_selection_count(s))
        return NULL;
    return index == 0 ? s->state->actor : s->at(s->context, 1, index - 1);
}
int three_mm_scene_selection_contains(void *actor, const ThreeMMSceneSelectionServices *s)
{
    ThreeMMI32 i;
    if (!actor)
        return 0;
    if (actor == s->state->actor)
        return 1;
    for (i = 0; i < s->count(s->context, 1); ++i)
        if (actor == s->at(s->context, 1, i))
            return 1;
    return 0;
}
int three_mm_scene_selection_toggle(void *actor, const ThreeMMSceneSelectionServices *s)
{
    ThreeMMI32 i;
    int hilite = s->view_mode(s->context) == 0;
    if (actor == s->state->actor)
    {
        if (hilite)
            s->hilite(s->context, actor, 0);
        if (s->count(s->context, 1) > 0)
        {
            void *promote = s->at(s->context, 1, 0);
            s->erase_extra(s->context, 0);
            set_selected(s, 0, promote);
        }
        else
            set_selected(s, 0, NULL);
        notify(s);
        return 1;
    }
    for (i = 0; i < s->count(s->context, 1); ++i)
        if (actor == s->at(s->context, 1, i))
        {
            if (hilite)
                s->hilite(s->context, actor, 0);
            s->erase_extra(s->context, i);
            notify(s);
            return 1;
        }
    if (!s->state->actor)
    {
        three_mm_scene_select_actor(actor, s);
        return 1;
    }
    if (!s->ensure_extras(s->context) || !s->append_extra(s->context, actor))
        return 0;
    if (hilite)
        s->hilite(s->context, actor, 1);
    notify(s);
    return 1;
}
int three_mm_scene_selection_all(const ThreeMMSceneSelectionServices *s)
{
    ThreeMMI32 i, count = s->count(s->context, 0);
    if (!count)
    {
        three_mm_scene_select_actor(NULL, s);
        return 1;
    }
    three_mm_scene_select_actor(s->at(s->context, 0, 0), s);
    for (i = 1; i < count; ++i)
        if (!three_mm_scene_selection_toggle(s->at(s->context, 0, i), s))
            return 0;
    return 1;
}
static unsigned int unit(const void *text, ThreeMMI32 index, int wide)
{
    return wide ? ((const unsigned short *)text)[index] : ((const unsigned char *)text)[index];
}
static int tag_char(unsigned int ch)
{
    return (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9') || ch == '_';
}
static unsigned int lower(unsigned int ch)
{
    return ch >= 'A' && ch <= 'Z' ? ch + ('a' - 'A') : ch;
}
static int contains_tag(const unsigned short *name, ThreeMMI32 length, const void *tag, ThreeMMI32 tag_length, int wide)
{
    ThreeMMI32 i, j;
    if (tag_length <= 0 || tag_length >= length)
        return 0;
    for (i = 0; i < length - tag_length; ++i)
    {
        if (name[i] != '#')
            continue;
        for (j = 0; j < tag_length; ++j)
            if (lower(name[i + 1 + j]) != lower(unit(tag, j, wide)))
                break;
        if (j == tag_length && (i + 1 + j == length || !tag_char(name[i + 1 + j])))
            return 1;
    }
    return 0;
}
static int shared(const unsigned short *a, ThreeMMI32 na, const unsigned short *b, ThreeMMI32 nb)
{
    ThreeMMI32 i, j, n;
    for (i = 0; i < na; ++i)
    {
        if (a[i] != '#')
            continue;
        j = i + 1;
        while (j < na && tag_char(a[j]))
            ++j;
        n = j - i - 1;
        if (n > 63)
            n = 63;
        if (n && contains_tag(b, nb, a + i + 1, n, 1))
            return 1;
    }
    return 0;
}
int three_mm_scene_selection_tag(const void *tag, ThreeMMI32 length, int wide, const ThreeMMSceneSelectionServices *s)
{
    ThreeMMI32 i, n, count = s->count(s->context, 0);
    unsigned short name[256];
    void *actor;
    int any = 0;
    three_mm_scene_select_actor(NULL, s);
    for (i = 0; i < count; ++i)
    {
        actor = s->at(s->context, 0, i);
        if (!actor)
            continue;
        n = s->name(s->context, actor, name);
        if (!contains_tag(name, n, tag, length, wide))
            continue;
        if (!any)
        {
            three_mm_scene_select_actor(actor, s);
            any = 1;
        }
        else if (!three_mm_scene_selection_toggle(actor, s))
            return 0;
    }
    return 1;
}
int three_mm_scene_selection_shared(void *seed, const ThreeMMSceneSelectionServices *s)
{
    unsigned short a[256], b[256];
    ThreeMMI32 i, na, nb, count;
    void *actor;
    int has_tag = 0;
    na = s->name(s->context, seed, a);
    for (i = 0; i + 1 < na && !has_tag; ++i)
        if (a[i] == '#' && tag_char(a[i + 1]))
            has_tag = 1;
    three_mm_scene_select_actor(seed, s);
    if (!has_tag)
        return 1;
    count = s->count(s->context, 0);
    for (i = 0; i < count; ++i)
    {
        actor = s->at(s->context, 0, i);
        if (!actor || actor == seed)
            continue;
        nb = s->name(s->context, actor, b);
        if (shared(a, na, b, nb) && !three_mm_scene_selection_toggle(actor, s))
            return 0;
    }
    return 1;
}
int three_mm_scene_selection_centroid(ThreeMMActorPoint *out, const ThreeMMSceneSelectionServices *s)
{
    ThreeMMI32 i, live = 0, count = three_mm_scene_selection_count(s), divisor;
    ThreeMMActorPoint sum, point;
    void *actor;
    sum.dxr = sum.dyr = sum.dzr = 0;
    for (i = 0; i < count; ++i)
    {
        actor = three_mm_scene_selection_at(i, s);
        if (!actor || !s->visible_position(s->context, actor, &point))
            continue;
        sum.dxr = three_mm_fixed_add(sum.dxr, point.dxr);
        sum.dyr = three_mm_fixed_add(sum.dyr, point.dyr);
        sum.dzr = three_mm_fixed_add(sum.dzr, point.dzr);
        ++live;
    }
    if (!live)
        return 0;
    /* Integer-to-16.16 conversion wraps just like the native scalar word. */
    divisor = three_mm_signed_word((ThreeMMU32)live << 16);
    out->dxr = three_mm_fixed_div(sum.dxr, divisor);
    out->dyr = three_mm_fixed_div(sum.dyr, divisor);
    out->dzr = three_mm_fixed_div(sum.dzr, divisor);
    return 1;
}
