/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "mm/body_edit.h"
#include <stddef.h>
#include <string.h>
void *mm_body_duplicate(void *body,const MMBodyEditServices *s)
{
    const MMBodyCreateServices *c=&s->shape;
    MMBodyShape b,d;
    MMI32 i,j;
    int visible,generic;
    void *copy,*material;
    b=*c->state(c->context,body); visible=!b.hidden;
    if (visible) { c->hide(c->context,body); b=*c->state(c->context,body); }
    copy=c->create(c->context);
    if (!copy) goto fail;
    d=*c->state(c->context,copy);
    d.hidden=1; d.set_count=b.set_count; d.part_count=b.part_count;
    *c->state(c->context,copy)=d; s->copy_bindings(c->context,copy,body);
    if (!c->allocate_actors(c->context,b.part_count+2,&d.actors,&d.bindings))
    { *c->state(c->context,copy)=d; goto fail; }
    *c->state(c->context,copy)=d;
    memcpy(d.bindings,b.bindings,(size_t)(b.part_count+2)*sizeof(*d.bindings));
    s->copy_actors(c->context,copy,d.actors,b.actors,b.part_count+2);
    for (i=0;i<d.part_count;++i)
        if (d.bindings[i+2].model_owner)
            s->retain(c->context,d.bindings[i+2].model_owner);
    d.sets=c->duplicate_sets(c->context,b.sets); *c->state(c->context,copy)=d;
    if (!d.sets) goto fail;
    if (!c->allocate_materials(c->context,b.set_count,&d.materials))
    { *c->state(c->context,copy)=d; goto fail; }
    *c->state(c->context,copy)=d;
    s->copy_materials(c->context,d.materials,b.materials,b.set_count);
    s->copy_bounds(c->context,copy,body);
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
    MMBodyShape b;
    MMI32 hidden;
    b=*c->state(c->context,body); hidden=b.hidden;
    s->swap_bodies(c->context,body,replacement); b=*c->state(c->context,body);
    s->identify(c->context,body,b.actors,b.part_count+2);
    if (!hidden) { s->show(c->context,body); b=*c->state(c->context,body); }
    b.hidden=hidden; *c->state(c->context,body)=b;
}
int mm_body_change_shape(void *body,void *parents,void *sets,const MMBodyEditServices *s)
{
    const MMBodyCreateServices *c=&s->shape;
    MMBodyShape b,d;
    MMI32 hidden,i,n;
    void *copy,*material,*model;
    int generic;
    b=*c->state(c->context,body); hidden=b.hidden;
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
    b=*c->state(c->context,body); d=*c->state(c->context,copy);
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
