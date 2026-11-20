/* Scene selection converted from engine/scene.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "mm/scene_selection.h"
#include "mm/wide.h"
#include <stddef.h>
#include <string.h>
static void set_selected(const MMSceneSelectionServices *s, int text, void *object)
{
    if (text)
        s->state->text = object;
    else
        s->state->actor = object;
    s->publish(s->context, text, object);
}
static void notify(const MMSceneSelectionServices *s)
{
    s->invalidate(s->context);
    s->action_menu(s->context);
}
static void clear_extras(const MMSceneSelectionServices *s)
{
    MMI32 i;
    void *actor;
    for (i = 0; i < mm_scene_member_count(s->owner, MM_SCENE_SELECTION); ++i)
    {
        actor = mm_scene_member_at(s->owner, MM_SCENE_SELECTION, i);
        if (actor)
            s->hilite(s->context, actor, 0);
    }
    mm_scene_member_clear(s->owner, MM_SCENE_SELECTION);
}
void mm_scene_select_actor(void *actor, const MMSceneSelectionServices *s)
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
void mm_scene_select_text(void *text, const MMSceneSelectionServices *s)
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
MMI32 mm_scene_selection_count(const MMSceneSelectionServices *s)
{
    return s->state->actor ? 1 + mm_scene_member_count(s->owner, MM_SCENE_SELECTION) : 0;
}
void *mm_scene_selection_at(MMI32 index, const MMSceneSelectionServices *s)
{
    if (index < 0 || index >= mm_scene_selection_count(s))
        return NULL;
    return index == 0 ? s->state->actor : mm_scene_member_at(s->owner, MM_SCENE_SELECTION, index - 1);
}
int mm_scene_selection_contains(void *actor, const MMSceneSelectionServices *s)
{
    MMI32 i;
    if (!actor)
        return 0;
    if (actor == s->state->actor)
        return 1;
    for (i = 0; i < mm_scene_member_count(s->owner, MM_SCENE_SELECTION); ++i)
        if (actor == mm_scene_member_at(s->owner, MM_SCENE_SELECTION, i))
            return 1;
    return 0;
}
int mm_scene_selection_toggle(void *actor, const MMSceneSelectionServices *s)
{
    MMI32 i;
    int hilite = s->view_mode(s->context) == 0;
    if (actor == s->state->actor)
    {
        if (hilite)
            s->hilite(s->context, actor, 0);
        if (mm_scene_member_count(s->owner, MM_SCENE_SELECTION) > 0)
        {
            void *promote = mm_scene_member_at(s->owner, MM_SCENE_SELECTION, 0);
            mm_scene_member_remove(s->owner, MM_SCENE_SELECTION, 0);
            set_selected(s, 0, promote);
        }
        else
            set_selected(s, 0, NULL);
        notify(s);
        return 1;
    }
    for (i = 0; i < mm_scene_member_count(s->owner, MM_SCENE_SELECTION); ++i)
        if (actor == mm_scene_member_at(s->owner, MM_SCENE_SELECTION, i))
        {
            if (hilite)
                s->hilite(s->context, actor, 0);
            mm_scene_member_remove(s->owner, MM_SCENE_SELECTION, i);
            notify(s);
            return 1;
        }
    if (!s->state->actor)
    {
        mm_scene_select_actor(actor, s);
        return 1;
    }
    if (!mm_scene_member_append(s->owner, MM_SCENE_SELECTION, actor))
        return 0;
    if (hilite)
        s->hilite(s->context, actor, 1);
    notify(s);
    return 1;
}
int mm_scene_selection_all(const MMSceneSelectionServices *s)
{
    MMI32 i, count = mm_scene_member_count(s->owner, MM_SCENE_ACTORS);
    if (!count)
    {
        mm_scene_select_actor(NULL, s);
        return 1;
    }
    mm_scene_select_actor(mm_scene_member_at(s->owner, MM_SCENE_ACTORS, 0), s);
    for (i = 1; i < count; ++i)
        if (!mm_scene_selection_toggle(mm_scene_member_at(s->owner, MM_SCENE_ACTORS, i), s))
            return 0;
    return 1;
}
static unsigned int unit(const void *text, MMI32 index, int wide)
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
static unsigned int name_unit(const MMMovieTitle *name, MMU32 index)
{
    return unit(mm_movie_title_text(name), index, name->unit == 2);
}
static int contains_tag(const MMMovieTitle *name, const void *tag, MMI32 tag_length, int wide)
{
    MMI32 i, j, length = name->count;
    if (tag_length <= 0 || tag_length >= length)
        return 0;
    for (i = 0; i < length - tag_length; ++i)
    {
        if (name_unit(name, i) != '#')
            continue;
        for (j = 0; j < tag_length; ++j)
            if (lower(name_unit(name, i + 1 + j)) != lower(unit(tag, j, wide)))
                break;
        if (j == tag_length && (i + 1 + j == length || !tag_char(name_unit(name, i + 1 + j))))
            return 1;
    }
    return 0;
}
static int shared(const MMMovieTitle *a, const MMMovieTitle *b)
{
    MMU32 i, j, n;
    const unsigned char *text = (const unsigned char *)mm_movie_title_text(a);
    for (i = 0; i < a->count; ++i)
    {
        if (name_unit(a, i) != '#')
            continue;
        j = i + 1;
        while (j < a->count && tag_char(name_unit(a, j)))
            ++j;
        n = j - i - 1;
        if (n > 63)
            n = 63;
        if (n && contains_tag(b, text + (i + 1) * a->unit, n, a->unit == 2))
            return 1;
    }
    return 0;
}
int mm_scene_selection_tag(const void *tag, MMI32 length, int wide, const MMSceneSelectionServices *s)
{
    MMI32 i, count = mm_scene_member_count(s->owner, MM_SCENE_ACTORS);
    MMMovieTitle name;
    void *actor;
    int any = 0;
    mm_scene_select_actor(NULL, s);
    for (i = 0; i < count; ++i)
    {
        actor = mm_scene_member_at(s->owner, MM_SCENE_ACTORS, i);
        if (!actor)
            continue;
        s->name(s->context, actor, &name);
        if (!contains_tag(&name, tag, length, wide))
            continue;
        if (!any)
        {
            mm_scene_select_actor(actor, s);
            any = 1;
        }
        else if (!mm_scene_selection_toggle(actor, s))
            return 0;
    }
    return 1;
}
int mm_scene_selection_shared(void *seed, const MMSceneSelectionServices *s)
{
    MMMovieTitle a, b;
    MMU32 j;
    MMI32 i, count;
    void *actor;
    int has_tag = 0;
    s->name(s->context, seed, &a);
    for (j = 0; j + 1 < a.count && !has_tag; ++j)
        if (name_unit(&a, j) == '#' && tag_char(name_unit(&a, j + 1)))
            has_tag = 1;
    mm_scene_select_actor(seed, s);
    if (!has_tag)
        return 1;
    count = mm_scene_member_count(s->owner, MM_SCENE_ACTORS);
    for (i = 0; i < count; ++i)
    {
        actor = mm_scene_member_at(s->owner, MM_SCENE_ACTORS, i);
        if (!actor || actor == seed)
            continue;
        s->name(s->context, actor, &b);
        if (shared(&a, &b) && !mm_scene_selection_toggle(actor, s))
            return 0;
    }
    return 1;
}

typedef struct NumericTag
{
    unsigned char digits[MM_STRING_MAX + 1];
    MMU32 count;
} NumericTag;

static int digit(unsigned int ch)
{
    return ch >= '0' && ch <= '9';
}

static int greater_tag(const MMMovieTitle *name, MMU32 start, MMU32 count, const NumericTag *max)
{
    MMU32 i;
    if (count != max->count)
        return count > max->count;
    for (i = 0; i < count; ++i)
        if (name_unit(name, start + i) != max->digits[i])
            return name_unit(name, start + i) > max->digits[i];
    return 0;
}

static void scan_tags(const MMMovieTitle *name, NumericTag *max)
{
    MMU32 i, start, end, offset, count;
    for (i = 0; i + 1 < name->count; ++i)
    {
        if (name_unit(name, i) != '#')
            continue;
        start = end = i + 1;
        while (end < name->count && digit(name_unit(name, end)))
            ++end;
        if (end == start || (end < name->count && tag_char(name_unit(name, end))))
            continue;
        while (start < end && name_unit(name, start) == '0')
            ++start;
        count = end - start;
        if (greater_tag(name, start, count, max))
        {
            for (offset = 0; offset < count; ++offset)
                max->digits[offset] = (unsigned char)name_unit(name, start + offset);
            max->count = count;
        }
    }
}

static void next_scene_tag(const MMSceneSelectionServices *s, NumericTag *tag)
{
    MMMovieTitle name;
    MMI32 i, count = mm_scene_member_count(s->owner, MM_SCENE_ACTORS);
    MMU32 place;
    void *actor;
    tag->count = 0;
    for (i = 0; i < count; ++i)
    {
        actor = mm_scene_member_at(s->owner, MM_SCENE_ACTORS, i);
        if (!actor)
            continue;
        s->name(s->context, actor, &name);
        scan_tags(&name, tag);
    }
    for (place = tag->count; place > 0; --place)
    {
        if (tag->digits[place - 1] != '9')
        {
            ++tag->digits[place - 1];
            return;
        }
        tag->digits[place - 1] = '0';
    }
    memmove(tag->digits + 1, tag->digits, tag->count);
    tag->digits[0] = '1';
    ++tag->count;
}

static MMU32 strip_tag(const MMMovieTitle *name)
{
    MMU32 start = name->count;
    while (start > 0 && tag_char(name_unit(name, start - 1)))
        --start;
    if (start == name->count || !start || name_unit(name, start - 1) != '#')
        return name->count;
    --start;
    if (start > 0 && name_unit(name, start - 1) == ' ')
        --start;
    return start;
}

static void append_tag(const MMMovieTitle *name, const NumericTag *tag, unsigned short *out)
{
    MMU32 i;
    unsigned char *bytes = (unsigned char *)out;
    memcpy(out, mm_movie_title_text(name), name->count * name->unit);
    if (name->unit == 1)
    {
        bytes[name->count] = ' ';
        bytes[name->count + 1] = '#';
        memcpy(bytes + name->count + 2, tag->digits, tag->count);
    }
    else
    {
        out[name->count] = ' ';
        out[name->count + 1] = '#';
        for (i = 0; i < tag->count; ++i)
            out[name->count + 2 + i] = tag->digits[i];
    }
}

static int edit_tags(int strip, const MMSceneSelectionServices *s, MMActorRenameGroup *group,
                     const MMSceneTagEditServices *edit)
{
    MMI32 i, id, count = mm_scene_selection_count(s);
    NumericTag tag;
    MMMovieTitle name;
    unsigned short appended[MM_STRING_MAX];
    void *actor;
    const void *text;
    MMU32 units;
    if (!strip)
        next_scene_tag(s, &tag);
    for (i = 0; i < count; ++i)
    {
        actor = mm_scene_selection_at(i, s);
        if (!actor)
            continue;
        s->name(s->context, actor, &name);
        if (strip)
        {
            units = strip_tag(&name);
            if (units == name.count)
                continue;
            text = mm_movie_title_text(&name);
        }
        else
        {
            units = name.count + 2 + tag.count;
            if (units > MM_STRING_MAX)
                return 0;
            append_tag(&name, &tag, appended);
            text = appended;
        }
        id = edit->actor_id(edit->context, actor);
        if (!mm_actor_rename_group_add(group, id, mm_movie_title_text(&name), name.count) ||
            !edit->rename(edit->context, id, text, units))
            return 0;
    }
    return 1;
}

void mm_scene_selection_edit_tags(int strip, const MMSceneSelectionServices *s,
                                 const MMSceneTagEditServices *edit)
{
    MMActorRenameGroup *group;
    void *undo;
    if (!mm_scene_selection_count(s))
        return;
    group = edit->create_undo(edit->context, &undo);
    if (!group)
    {
        edit->not_undoable(edit->context);
        return;
    }
    if (!edit_tags(strip, s, group, edit))
    {
        edit->release_undo(edit->context, undo);
        edit->clear_undo(edit->context);
        edit->not_undoable(edit->context);
        return;
    }
    if (!group->names.count)
    {
        edit->release_undo(edit->context, undo);
        return;
    }
    if (!edit->commit_undo(edit->context, undo))
    {
        edit->not_undoable(edit->context);
        edit->clear_undo(edit->context);
    }
    edit->release_undo(edit->context, undo);
    edit->dirty(edit->context);
}
int mm_scene_selection_centroid(MMActorPoint *out, const MMSceneSelectionServices *s)
{
    MMI32 i, live = 0, count = mm_scene_selection_count(s), divisor;
    MMActorPoint sum, point;
    void *actor;
    sum.dxr = sum.dyr = sum.dzr = 0;
    for (i = 0; i < count; ++i)
    {
        actor = mm_scene_selection_at(i, s);
        if (!actor || !s->visible_position(s->context, actor, &point))
            continue;
        sum.dxr = mm_fixed_add(sum.dxr, point.dxr);
        sum.dyr = mm_fixed_add(sum.dyr, point.dyr);
        sum.dzr = mm_fixed_add(sum.dzr, point.dzr);
        ++live;
    }
    if (!live)
        return 0;
    /* Integer-to-16.16 conversion wraps just like the native scalar word. */
    divisor = mm_signed_word((MMU32)live << 16);
    out->dxr = mm_fixed_div(sum.dxr, divisor);
    out->dyr = mm_fixed_div(sum.dyr, divisor);
    out->dzr = mm_fixed_div(sum.dzr, divisor);
    return 1;
}
