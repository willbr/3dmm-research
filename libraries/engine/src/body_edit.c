/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "mm/body_edit.h"
#include <stddef.h>
#include <string.h>
void *mm_body_duplicate(void *body,const MMBodyEditServices *s)
{
    const MMBodyRuntimeServices *runtime=&s->runtime;
    const MMBodyCreateServices *c=&runtime->shape;
    const MMBodyState *source=c->state(c->context,body);
    MMBodyState *target;
    MMBodyShape b,d;
    MMI32 i,j;
    int visible;
    void *copy;
    visible=!source->shape.hidden;
    if (visible) c->hide(c->context,body);
    b=source->shape;
    copy=c->create(c->context);
    if (!copy) goto fail;
    target=c->state(c->context,copy);
    d=target->shape;
    d.hidden=1; d.set_count=b.set_count; d.part_count=b.part_count;
    target->shape=d;
    target->render=source->render;
    target->geometry.found=source->geometry.found;
    target->geometry.set=source->geometry.set;
    if (!c->allocate_actors(c->context,b.part_count+2,&d.actors,&d.bindings))
    { target->shape=d; goto fail; }
    target->shape=d;
    memcpy(d.bindings,b.bindings,(size_t)(b.part_count+2)*sizeof(*d.bindings));
    s->copy_actors(c->context,copy,d.actors,b.actors,b.part_count+2);
    for (i=0;i<d.part_count;++i)
        if (d.bindings[i+2].model_owner)
            runtime->retain_model(c->context,d.bindings[i+2].model_owner);
    if (!c->allocate_materials(c->context,b.set_count,&d.custom_materials))
    { target->shape=d; goto fail; }
    target->shape=d;
    if (b.set_count)
        memcpy(d.custom_materials,b.custom_materials,(size_t)b.set_count*sizeof(*d.custom_materials));
    target->geometry.bounds=source->geometry.bounds;
    target->geometry.last_visible=source->geometry.last_visible;
    for (i=0;i<b.set_count;++i)
    {
        if (d.custom_materials[i])
            runtime->retain(c->context,d.custom_materials[i]);
        else
        {
            for (j=0;j<d.part_count;++j)
                if (d.bindings[j+2].material_owner && i==d.bindings[j+2].set)
                    runtime->retain(c->context,d.bindings[j+2].material_owner);
        }
    }
    if (visible) mm_body_show(body,runtime);
    return copy;
fail:
    if (visible) mm_body_show(body,runtime);
    if (copy) c->release(c->context,copy);
    return NULL;
}
void mm_body_restore(void *body,void *replacement,const MMBodyEditServices *s)
{
    const MMBodyCreateServices *c=&s->runtime.shape;
    MMBodyState *receiver,*donor,displaced;
    if (body==replacement) return;
    receiver=c->state(c->context,body);
    donor=c->state(c->context,replacement);
    displaced=*receiver;
    *receiver=*donor;
    *donor=displaced;
    s->identify(c->context,body,receiver->shape.actors,receiver->shape.part_count+2);
    if (donor->shape.actors)
        s->identify(c->context,replacement,donor->shape.actors,donor->shape.part_count+2);
    if (!displaced.shape.hidden) mm_body_show(body,&s->runtime);
    receiver->shape.hidden=displaced.shape.hidden;
}
int mm_body_change_shape(void *body,const MMPartIndexView *parents,const MMPartIndexView *sets,const MMBodyEditServices *s)
{
    const MMBodyRuntimeServices *runtime=&s->runtime;
    const MMBodyCreateServices *c=&runtime->shape;
    MMBodyShape b,d;
    MMI32 hidden,i,n;
    void *copy,*model;
    int generic;
    b=c->state(c->context,body)->shape; hidden=b.hidden;
    copy=mm_body_duplicate(body,s);
    if (!copy) return 0;
    mm_body_shape_dispose(body,c);
    if (!mm_body_shape_initialize(body,parents,sets,c))
    {
        mm_body_restore(body,copy,s);
        if (!hidden) mm_body_show(body,runtime);
        c->release(c->context,copy); return 0;
    }
    if (!hidden) mm_body_show(body,runtime);
    b=c->state(c->context,body)->shape; d=c->state(c->context,copy)->shape;
    n=b.set_count<d.set_count?b.set_count:d.set_count;
    for (i=0;i<n;++i)
    {
        void *material=NULL,*custom;
        mm_body_get_material(copy,i,&generic,&material,&custom,runtime);
        if (!generic) mm_body_set_custom(body,custom,runtime);
        else if (material) mm_body_set_generic(body,i,material,runtime);
    }
    n=b.part_count<d.part_count?b.part_count:d.part_count;
    for (i=0;i<n;++i)
    {
        model=d.bindings[i+2].model_owner;
        if (model) mm_body_set_model(body,i,model,runtime);
        mm_body_shape_matrix(&b,i+2,&d.bindings[i+2].matrix,c);
    }
    mm_body_shape_matrix(&b,0,&d.bindings[0].matrix,c);
    if (s->highlighted(c->context,d.actors)) mm_body_highlight(body,1,runtime);
    c->release(c->context,copy); return 1;
}
