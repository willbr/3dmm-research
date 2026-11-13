/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "three_mm/body_create.h"
#include <stddef.h>
int three_mm_body_shape_initialize(void *body,void *parents,void *sets,const ThreeMMBodyCreateServices *s)
{
    ThreeMMBodyShape b;
    ThreeMMI32 i,n,set,parent;
    if (s->validate_lists) s->validate_lists(s->context,parents,sets);
    s->read(s->context,body,&b);
    b.sets=s->duplicate_sets(s->context,sets);
    s->write(s->context,body,&b);
    if (!b.sets) return 0;
    b.set_count=-1;
    n=s->count(s->context,b.sets);
    for (i=0;i<n;++i)
    {
        set=s->item(s->context,b.sets,i);
        if (set>b.set_count) b.set_count=set;
    }
    ++b.set_count;
    s->write(s->context,body,&b);
    if (!s->allocate_materials(s->context,b.set_count,&b.materials))
    { s->write(s->context,body,&b); return 0; }
    b.part_count=s->count(s->context,parents);
    s->write(s->context,body,&b);
    if (!s->allocate_actors(s->context,b.part_count+2,&b.actors))
    { s->write(s->context,body,&b); return 0; }
    s->write(s->context,body,&b);
    s->initialize_actor(s->context,body,b.actors,0,THREE_MM_BODY_ROOT);
    s->initialize_actor(s->context,body,b.actors,1,THREE_MM_BODY_HIGHLIGHT);
    s->link(s->context,b.actors,0,1);
    for (i=0;i<b.part_count;++i)
    {
        s->initialize_actor(s->context,body,b.actors,i+2,THREE_MM_BODY_PART);
        parent=s->item(s->context,parents,i);
        s->link(s->context,b.actors,parent==-1?0:parent+2,i+2);
    }
    return 1;
}
int three_mm_body_initialize(void *body,void *parents,void *sets,const ThreeMMBodyCreateServices *s)
{
    ThreeMMBodyShape b;
    s->read(s->context,body,&b); b.hidden=1; s->write(s->context,body,&b);
    return three_mm_body_shape_initialize(body,parents,sets,s);
}
void *three_mm_body_create(void *parents,void *sets,const ThreeMMBodyCreateServices *s)
{
    void *highlight,*body;
    if (!s->highlight(s->context))
    {
        highlight=s->allocate_highlight(s->context);
        if (!highlight) return NULL;
        s->install_highlight(s->context,highlight);
    }
    body=s->create(s->context);
    if (!body) return NULL;
    if (!three_mm_body_initialize(body,parents,sets,s))
    { s->release(s->context,body); return NULL; }
    return body;
}
void three_mm_body_shape_dispose(void *body,const ThreeMMBodyCreateServices *s)
{
    ThreeMMBodyShape b;
    ThreeMMI32 i;
    s->read(s->context,body,&b);
    if (!b.hidden)
    { s->hide(s->context,body); s->read(s->context,body,&b); }
    if (b.actors)
    {
        for (i=0;i<b.part_count;++i) s->release_model(s->context,b.actors,i);
        if (b.materials)
            for (i=0;i<b.set_count;++i) s->remove_material(s->context,body,i);
        s->free_memory(s->context,&b.actors); s->write(s->context,body,&b);
    }
    s->free_memory(s->context,&b.materials); s->write(s->context,body,&b);
    if (b.sets) s->release(s->context,b.sets);
    b.sets=NULL; s->write(s->context,body,&b);
}
