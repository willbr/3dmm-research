/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "mm/body_create.h"
#include <stddef.h>
#include <string.h>
void mm_body_shape_init(MMBodyShape *state)
{ memset(state, 0, sizeof(*state)); }
void mm_body_shape_matrix(MMBodyShape *shape,MMI32 index,const MMActorMatrix *matrix,const MMBodyCreateServices *s)
{
    shape->bindings[index].matrix = *matrix;
    s->assign_matrix(s->context,shape->actors,index,&shape->bindings[index].matrix);
}
void mm_body_shape_part_world(const MMBodyShape *shape,MMI32 part,MMActorMatrix *out)
{
    const MMBodyPartBinding *bindings=shape->bindings;
    MMActorMatrix matrix=bindings[part+2].matrix, composed;
    for (part=bindings[part+2].parent; part>=0; part=bindings[part+2].parent)
    {
        mm_matrix_multiply(&composed,&matrix,&bindings[part+2].matrix);
        matrix=composed;
    }
    mm_matrix_multiply(out,&matrix,&bindings[0].matrix);
}
int mm_body_draw(const MMBodyDrawView *view,void *context,MMBodyDrawPart submit)
{
    const MMBodyPartBinding *binding;
    MMI32 part;
    if (!view->bindings || view->part_count<0 || view->part_count>2147483645L)
        return 0;
    if (!submit(context,0,-1,&view->bindings[0]))
        return 0;
    for (part=0;part<view->part_count;++part)
    {
        binding=&view->bindings[part+2];
        if (binding->parent < -1 || binding->parent>=part)
            return 0;
        if (!submit(context,part+2,binding->parent<0?0:binding->parent+2,binding))
            return 0;
    }
    return 1;
}
int mm_body_shape_initialize(void *body,void *parents,void *sets,const MMBodyCreateServices *s)
{
    MMBodyShape b;
    MMActorMatrix identity;
    MMI32 i,n,set,parent;
    if (s->validate_lists) s->validate_lists(s->context,parents,sets);
    b=*s->state(s->context,body);
    b.sets=s->duplicate_sets(s->context,sets);
    *s->state(s->context,body)=b;
    if (!b.sets) return 0;
    b.set_count=-1;
    n=s->count(s->context,b.sets);
    for (i=0;i<n;++i)
    {
        set=s->item(s->context,b.sets,i);
        if (set>b.set_count) b.set_count=set;
    }
    ++b.set_count;
    *s->state(s->context,body)=b;
    if (!s->allocate_materials(s->context,b.set_count,&b.materials))
    { *s->state(s->context,body)=b; return 0; }
    b.part_count=s->count(s->context,parents);
    *s->state(s->context,body)=b;
    if (!s->allocate_actors(s->context,b.part_count+2,&b.actors,&b.bindings))
    { *s->state(s->context,body)=b; return 0; }
    *s->state(s->context,body)=b;
    mm_matrix_identity(&identity);
    b.bindings[0].parent=-1;
    s->initialize_actor(s->context,body,b.actors,0,MM_BODY_ROOT);
    mm_body_shape_matrix(&b,0,&identity,s);
    s->initialize_actor(s->context,body,b.actors,1,MM_BODY_HIGHLIGHT);
    s->link(s->context,b.actors,0,1);
    for (i=0;i<b.part_count;++i)
    {
        parent=s->item(s->context,parents,i);
        b.bindings[i+2].parent=parent;
        s->initialize_actor(s->context,body,b.actors,i+2,MM_BODY_PART);
        mm_body_shape_matrix(&b,i+2,&identity,s);
        s->link(s->context,b.actors,parent==-1?0:parent+2,i+2);
    }
    return 1;
}
int mm_body_initialize(void *body,void *parents,void *sets,const MMBodyCreateServices *s)
{
    MMBodyShape b;
    b=*s->state(s->context,body); b.hidden=1; *s->state(s->context,body)=b;
    return mm_body_shape_initialize(body,parents,sets,s);
}
void *mm_body_create(void *parents,void *sets,const MMBodyCreateServices *s)
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
    if (!mm_body_initialize(body,parents,sets,s))
    { s->release(s->context,body); return NULL; }
    return body;
}
void mm_body_shape_dispose(void *body,const MMBodyCreateServices *s)
{
    MMBodyShape b;
    MMI32 i;
    b=*s->state(s->context,body);
    if (!b.hidden)
    { s->hide(s->context,body); b=*s->state(s->context,body); }
    if (b.actors)
    {
        for (i=0;i<b.part_count;++i)
            if (b.bindings[i+2].model)
            {
                s->release(s->context,b.bindings[i+2].model);
                b.bindings[i+2].model=NULL;
            }
        if (b.materials)
            for (i=0;i<b.set_count;++i) s->remove_material(s->context,body,i);
        s->free_memory(s->context,&b.actors);
        b.bindings=NULL;
        *s->state(s->context,body)=b;
    }
    s->free_memory(s->context,&b.materials); *s->state(s->context,body)=b;
    if (b.sets) s->release(s->context,b.sets);
    b.sets=NULL; *s->state(s->context,body)=b;
}
