/* Scene property editing and playback lifecycle converted from engine/scene.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "three_mm/scene_control.h"
int three_mm_scene_name_edit(const ThreeMMMovieTitle *name, const ThreeMMScenePropertyServices *s)
{
    void *undo = s->new_undo(s->context, THREE_MM_SCENE_CONTROL_NAME);
    if (!undo)
        return 0;
    s->title(s->context, undo)->name = s->data->name;
    if (!s->commit(s->context, undo))
    {
        s->release(s->context, undo);
        return 0;
    }
    s->release(s->context, undo);
    s->data->name = *name;
    return 1;
}
int three_mm_scene_transition_edit(ThreeMMI32 transition, const ThreeMMScenePropertyServices *s)
{
    void *undo = s->new_undo(s->context, THREE_MM_SCENE_CONTROL_TRANSITION);
    if (!undo)
        return 0;
    s->transition(s->context, undo)->transition = s->data->transition;
    if (!s->commit(s->context, undo))
    {
        s->release(s->context, undo);
        return 0;
    }
    s->release(s->context, undo);
    s->data->transition = transition;
    return 1;
}
static int failed(const ThreeMMScenePropertyUndoServices *s)
{
    s->flush(s->context);
    s->clear(s->context);
    return 0;
}
int three_mm_scene_title_swap(ThreeMMSceneTitleUndo *p, const ThreeMMScenePropertyUndoServices *s)
{
    ThreeMMSceneState *data;
    ThreeMMMovieTitle old;
    if (!s->switch_scene(s->context))
        return failed(s);
    data = s->scene(s->context);
    old = data->name;
    data->name = p->name;
    p->name = old;
    s->flush(s->context);
    return 1;
}
int three_mm_scene_transition_swap(ThreeMMSceneTransitionUndo *p, const ThreeMMScenePropertyUndoServices *s)
{
    ThreeMMSceneState *data;
    ThreeMMI32 old;
    if (!s->switch_scene(s->context) || !s->seek(s->context))
        return failed(s);
    data = s->scene(s->context);
    old = data->transition;
    data->transition = p->transition;
    p->transition = old;
    s->flush(s->context);
    return 1;
}
int three_mm_scene_start_playing(const ThreeMMScenePlaybackServices *s)
{
    ThreeMMI32 i, first, last;
    void *object, *view;
    for (i = 0; i < s->count(s->context, 0); ++i)
        if (!s->lifetime(s->context, 0, s->object(s->context, 0, i), &first, &last))
            return 0;
    for (i = 0; i < s->count(s->context, 1); ++i)
    {
        object = s->object(s->context, 1, i);
        if ((s->story(s->context, object) && s->visible(s->context, object)) ||
            (!s->story(s->context, object) && s->lifetime(s->context, 1, object, &first, &last) &&
             first == s->data->currentFrame))
        {
            view = s->view(s->context, object);
            if (view)
                s->scroll(s->context, view);
            else
                s->missing_view(s->context);
        }
    }
    s->prerender(s->context, 1);
    return 1;
}
void three_mm_scene_stop_playing(const ThreeMMScenePlaybackServices *s)
{
    ThreeMMI32 i;
    s->prerender(s->context, 0);
    for (i = 0; i < s->count(s->context, 1); ++i)
        s->clean_text(s->context, s->object(s->context, 1, i));
}
