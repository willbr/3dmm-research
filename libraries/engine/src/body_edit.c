/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "three_mm/body_edit.h"
#include <stddef.h>
void *three_mm_body_duplicate(void *body,const ThreeMMBodyEditServices *s)
{
    const ThreeMMBodyCreateServices *c=&s->shape;
    ThreeMMBodyShape b,d;
    ThreeMMI32 i,j;
    int visible,generic;
    void *copy,*material;
    c->read(c->context,body,&b); visible=!b.hidden;
    if (visible) { c->hide(c->context,body); c->read(c->context,body,&b); }
    copy=c->create(c->context);
    if (!copy) goto fail;
    c->read(c->context,copy,&d);
    d.hidden=1; d.set_count=b.set_count; d.part_count=b.part_count;
    c->write(c->context,copy,&d); s->copy_bindings(c->context,copy,body);
    if (!c->allocate_actors(c->context,b.part_count+2,&d.actors))
    { c->write(c->context,copy,&d); goto fail; }
    c->write(c->context,copy,&d);
    s->copy_actors(c->context,d.actors,b.actors,b.part_count+2);
    for (i=0;i<b.part_count+2;++i) s->rebase_actor(c->context,copy,d.actors,b.actors,i,i==0);
    for (i=0;i<d.part_count;++i) s->retain_model(c->context,b.actors,i);
    d.sets=c->duplicate_sets(c->context,b.sets); c->write(c->context,copy,&d);
    if (!d.sets) goto fail;
    if (!c->allocate_materials(c->context,b.set_count,&d.materials))
    { c->write(c->context,copy,&d); goto fail; }
    c->write(c->context,copy,&d);
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
void three_mm_body_restore(void *body,void *replacement,const ThreeMMBodyEditServices *s)
{
    const ThreeMMBodyCreateServices *c=&s->shape;
    ThreeMMBodyShape b;
    ThreeMMI32 hidden,i;
    c->read(c->context,body,&b); hidden=b.hidden;
    s->swap_bodies(c->context,body,replacement); c->read(c->context,body,&b);
    for (i=0;i<b.part_count+2;++i) s->identify(c->context,body,b.actors,i);
    if (!hidden) { s->show(c->context,body); c->read(c->context,body,&b); }
    b.hidden=hidden; c->write(c->context,body,&b);
}
int three_mm_body_change_shape(void *body,void *parents,void *sets,const ThreeMMBodyEditServices *s)
{
    const ThreeMMBodyCreateServices *c=&s->shape;
    ThreeMMBodyShape b,d;
    ThreeMMI32 hidden,i,n;
    void *copy,*material,*model;
    int generic;
    c->read(c->context,body,&b); hidden=b.hidden;
    copy=three_mm_body_duplicate(body,s);
    if (!copy) return 0;
    three_mm_body_shape_dispose(body,c);
    if (!three_mm_body_shape_initialize(body,parents,sets,c))
    {
        three_mm_body_restore(body,copy,s);
        if (!hidden) s->show(c->context,body);
        c->release(c->context,copy); return 0;
    }
    if (!hidden) s->show(c->context,body);
    c->read(c->context,body,&b); c->read(c->context,copy,&d);
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
        model=s->model(c->context,d.actors,i);
        if (model) s->set_model(c->context,body,i,model);
        s->copy_matrix(c->context,b.actors,i+2,d.actors,i+2);
    }
    s->copy_matrix(c->context,b.actors,0,d.actors,0);
    if (s->highlighted(c->context,d.actors)) s->highlight(c->context,body);
    c->release(c->context,copy); return 1;
}
