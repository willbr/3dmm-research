/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "mm/body_create.h"
#include <stddef.h>
#include <string.h>
void mm_body_state_init(MMBodyState *state)
{ memset(state, 0, sizeof(*state)); }
void mm_body_shape_matrix(MMBodyShape *shape,MMI32 index,const MMActorMatrix *matrix,const MMBodyCreateServices *s)
{
    shape->bindings[index].matrix = *matrix;
    s->assign_matrix(s->context,shape->actors,index,&shape->bindings[index].matrix);
}
void mm_body_shape_material(MMBodyShape *shape,MMI32 part,void *owner,void *handle,const MMBodyCreateServices *s)
{
    MMI32 index=part+2;
    shape->bindings[index].material_owner=owner;
    shape->bindings[index].material=handle;
    s->assign_material(s->context,shape->actors,index,handle);
}
void mm_body_shape_remove_material(MMBodyShape *shape,MMI32 set,const MMBodyCreateServices *s)
{
    void *custom=shape->custom_materials[set];
    int had_custom=custom!=NULL;
    MMI32 part;
    if (custom)
    {
        s->release_material(s->context,custom);
        shape->custom_materials[set]=NULL;
    }
    for (part=0;part<shape->part_count;++part)
    {
        MMBodyPartBinding *binding=&shape->bindings[part+2];
        if (binding->set!=set)
            continue;
        if (binding->material_owner)
            s->release_material(s->context,binding->material_owner);
        else if (!had_custom && !binding->material)
            continue;
        mm_body_shape_material(shape,part,NULL,NULL,s);
    }
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
int mm_body_shape_initialize(void *body,const MMPartIndexView *parents,const MMPartIndexView *sets,const MMBodyCreateServices *s)
{
    MMBodyShape *shape = &s->state(s->context,body)->shape;
    MMBodyShape b=*shape;
    MMActorMatrix identity;
    MMI32 i,n,set,parent,set_count=0;
    if (!parents || !sets || parents->count!=sets->count ||
        (MMU32)parents->count>=MM_STORAGE_LIMIT/MM_PART_INDEX_BYTES)
        return 0;
    n=parents->count;
    if (n && (!parents->bytes || !sets->bytes))
        return 0;
    for (i=0;i<n;++i)
    {
        parent=mm_part_index_get(parents,i);
        set=mm_part_index_get(sets,i);
        if (parent < -1 || parent>=i || set<0 || set>=n)
            return 0;
        if (set>=set_count) set_count=set+1;
    }
    b.set_count=set_count;
    *shape=b;
    if (!s->allocate_materials(s->context,b.set_count,&b.custom_materials))
    { *shape=b; return 0; }
    b.part_count=n;
    *shape=b;
    if (!s->allocate_actors(s->context,b.part_count+2,&b.actors,&b.bindings))
    { *shape=b; return 0; }
    *shape=b;
    mm_matrix_identity(&identity);
    b.bindings[0].parent=-1;
    b.bindings[0].set=b.bindings[1].set=-1;
    s->initialize_actor(s->context,body,b.actors,0,MM_BODY_ROOT);
    mm_body_shape_matrix(&b,0,&identity,s);
    s->initialize_actor(s->context,body,b.actors,1,MM_BODY_HIGHLIGHT);
    s->link(s->context,b.actors,0,1);
    for (i=0;i<b.part_count;++i)
    {
        parent=mm_part_index_get(parents,i);
        b.bindings[i+2].parent=parent;
        b.bindings[i+2].set=mm_part_index_get(sets,i);
        s->initialize_actor(s->context,body,b.actors,i+2,MM_BODY_PART);
        mm_body_shape_matrix(&b,i+2,&identity,s);
        s->link(s->context,b.actors,parent==-1?0:parent+2,i+2);
    }
    return 1;
}
int mm_body_initialize(void *body,const MMPartIndexView *parents,const MMPartIndexView *sets,const MMBodyCreateServices *s)
{
    s->state(s->context,body)->shape.hidden=1;
    return mm_body_shape_initialize(body,parents,sets,s);
}
void *mm_body_create(const MMPartIndexView *parents,const MMPartIndexView *sets,const MMBodyCreateServices *s)
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
    MMBodyShape *shape = &s->state(s->context,body)->shape;
    MMBodyShape b;
    void *materials;
    MMI32 i;
    b=*shape;
    if (!b.hidden)
    { s->hide(s->context,body); b=*shape; }
    if (b.actors)
    {
        for (i=0;i<b.part_count;++i)
            if (b.bindings[i+2].model_owner)
            {
                s->release_model(s->context,b.bindings[i+2].model_owner);
                b.bindings[i+2].model_owner = NULL;
                b.bindings[i+2].model_handle = NULL;
            }
        if (b.custom_materials)
            for (i=0;i<b.set_count;++i) mm_body_shape_remove_material(shape,i,s);
        s->free_memory(s->context,&b.actors);
        b.bindings=NULL;
        *shape=b;
    }
    materials=b.custom_materials;
    s->free_memory(s->context,&materials); b.custom_materials=materials; *shape=b;
}
