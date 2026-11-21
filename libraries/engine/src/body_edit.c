/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "mm/body_edit.h"
#include <stddef.h>
#include <string.h>
void *mm_body_duplicate(void *body,const MMBodyEditServices *s)
{
    const MMBodyCreateServices *c=&s->shape;
    const MMBodyState *source=c->state(c->context,body);
    MMBodyState *target;
    MMBodyShape b,d;
    MMI32 i,j;
    int visible,generic;
    void *copy,*material;
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
            s->retain(c->context,d.bindings[i+2].model_owner);
    d.sets=c->duplicate_sets(c->context,b.sets); target->shape=d;
    if (!d.sets) goto fail;
    if (!c->allocate_materials(c->context,b.set_count,&d.materials))
    { target->shape=d; goto fail; }
    target->shape=d;
    s->copy_materials(c->context,d.materials,b.materials,b.set_count);
    target->geometry.bounds=source->geometry.bounds;
    target->geometry.last_visible=source->geometry.last_visible;
    for (i=0;i<b.set_count;++i)
    {
        material=s->material(c->context,copy,i,&generic);
        if (generic)
        {
            for (j=0;j<d.part_count;++j)
                if (i==c->item(c->context,b.sets,j)) s->retain(c->context,material);
        }
        else s->retain(c->context,material);
    }
    if (visible) s->show(c->context,body);
    return copy;
fail:
    if (visible) s->show(c->context,body);
    if (copy) c->release(c->context,copy);
    return NULL;
}
void mm_body_restore(void *body,void *replacement,const MMBodyEditServices *s)
{
    const MMBodyCreateServices *c=&s->shape;
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
    if (!displaced.shape.hidden) s->show(c->context,body);
    receiver->shape.hidden=displaced.shape.hidden;
}
int mm_body_change_shape(void *body,void *parents,void *sets,const MMBodyEditServices *s)
{
    const MMBodyCreateServices *c=&s->shape;
    MMBodyShape b,d;
    MMI32 hidden,i,n;
    void *copy,*material,*model;
    int generic;
    b=c->state(c->context,body)->shape; hidden=b.hidden;
    copy=mm_body_duplicate(body,s);
    if (!copy) return 0;
    mm_body_shape_dispose(body,c);
    if (!mm_body_shape_initialize(body,parents,sets,c))
    {
        mm_body_restore(body,copy,s);
        if (!hidden) s->show(c->context,body);
        c->release(c->context,copy); return 0;
    }
    if (!hidden) s->show(c->context,body);
    b=c->state(c->context,body)->shape; d=c->state(c->context,copy)->shape;
    n=b.set_count<d.set_count?b.set_count:d.set_count;
    for (i=0;i<n;++i)
    {
        material=s->material(c->context,copy,i,&generic);
        if (generic) s->set_generic(c->context,body,i,material);
        else s->set_custom(c->context,body,material);
    }
    n=b.part_count<d.part_count?b.part_count:d.part_count;
    for (i=0;i<n;++i)
    {
        model=d.bindings[i+2].model_owner;
        if (model) s->set_model(c->context,body,i,model);
        mm_body_shape_matrix(&b,i+2,&d.bindings[i+2].matrix,c);
    }
    mm_body_shape_matrix(&b,0,&d.bindings[0].matrix,c);
    if (s->highlighted(c->context,d.actors)) s->highlight(c->context,body);
    c->release(c->context,copy); return 1;
}
