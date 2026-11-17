/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "mm/body_runtime.h"
#include <stddef.h>
static void changed(void *body,const MMBodyRuntimeServices *s)
{ MMBodyShape b; b=*s->shape.state(s->shape.context,body); if (!b.hidden) s->changed(s->shape.context,body); }
static void diagnose(const MMBodyRuntimeServices *s,int code)
{ if (s->diagnostic) s->diagnostic(s->shape.context,code); }
void mm_body_show(void *body,const MMBodyRuntimeServices *s)
{
    MMBodyShape b; void *p=s->shape.context;
    b=*s->shape.state(p,body); --b.hidden; *s->shape.state(p,body)=b;
    if (!b.hidden) { s->attach(p,body); s->changed(p,body); }
}
void mm_body_hide(void *body,const MMBodyRuntimeServices *s)
{
    MMBodyShape b; int visible; void *p=s->shape.context;
    b=*s->shape.state(p,body); visible=!b.hidden; ++b.hidden; *s->shape.state(p,body)=b;
    if (visible) { s->clear_bounds(p,body); if (s->attached(p,body)) s->detach(p,body); s->changed(p,body); }
}
void mm_body_highlight(void *body,int enabled,const MMBodyRuntimeServices *s)
{ s->highlight(s->shape.context,body,enabled); changed(body,s); }
void mm_body_locate(void *body,MMI32 x,MMI32 y,MMI32 z,const void *matrix,const MMBodyRuntimeServices *s)
{ s->matrix(s->shape.context,body,0,matrix); s->translate(s->shape.context,body,x,y,z); changed(body,s); }
void mm_body_set_matrix(void *body,MMI32 part,const void *matrix,const MMBodyRuntimeServices *s)
{ s->matrix(s->shape.context,body,part+2,matrix); changed(body,s); }
void mm_body_set_model(void *body,MMI32 part,void *model,const MMBodyRuntimeServices *s)
{
    void *p=s->shape.context,*old=s->model(p,body,part);
    if (old) { if (old==model) return; s->shape.release(p,old); }
    s->assign_model(p,body,part,model); s->retain(p,model); changed(body,s);
}
void mm_body_remove_material(void *body,MMI32 set,const MMBodyRuntimeServices *s)
{
    MMBodyShape b; MMI32 i; void *p=s->shape.context,*custom=s->custom(p,body,set),*raw;
    int had_custom=custom!=NULL;
    if (custom) { s->shape.release(p,custom); s->assign_custom(p,body,set,NULL); }
    b=*s->shape.state(p,body);
    for (i=0;i<b.part_count;++i) if (set==s->shape.item(p,b.sets,i))
    {
        raw=s->part_material(p,body,i);
        if (raw) { if (!had_custom) s->shape.release(p,s->material_object(p,raw)); s->assign_material(p,body,i,NULL); }
    }
}

MMI32 mm_body_apply_set(void *body, MMI32 part_count, MMI32 set, const MMBodySetServices *s)
{
    MMI32 part, slot = 0;
    for (part = 0; part < part_count; ++part)
        if (s->part_set(s->context, body, part) == set)
        {
            if (!s->apply_part(s->context, body, part, slot))
                return -1;
            ++slot;
        }
    return slot;
}

typedef struct BodySetBinding
{
    const MMBodyRuntimeServices *services;
    void *sets, *material;
} BodySetBinding;

static MMI32 render_part_set(void *context, void *body, MMI32 part)
{
    const BodySetBinding *binding = (const BodySetBinding *)context;
    const MMBodyCreateServices *s = &binding->services->shape;
    (void)body;
    return s->item(s->context, binding->sets, part);
}

static int render_generic(void *context, void *body, MMI32 part, MMI32 slot)
{
    const BodySetBinding *binding = (const BodySetBinding *)context;
    const MMBodyRuntimeServices *s = binding->services;
    void *p = s->shape.context;
    (void)slot;
    s->assign_material(p, body, part, s->material_handle(p, binding->material));
    s->retain(p, binding->material);
    return 1;
}

static int render_custom(void *context, void *body, MMI32 part, MMI32 slot)
{
    const BodySetBinding *binding = (const BodySetBinding *)context;
    const MMBodyRuntimeServices *s = binding->services;
    void *p = s->shape.context;
    void *model = s->custom_model(p, binding->material, slot);
    if (model)
        mm_body_set_model(body, part, model, s);
    s->assign_material(p, body, part, s->custom_material(p, binding->material, slot));
    return 1;
}

void mm_body_set_generic(void *body,MMI32 set,void *material,const MMBodyRuntimeServices *s)
{
    MMBodyShape b;
    BodySetBinding binding;
    MMBodySetServices parts;
    mm_body_remove_material(body, set, s);
    b = *s->shape.state(s->shape.context, body);
    binding.services = s;
    binding.sets = b.sets;
    binding.material = material;
    parts.context = &binding;
    parts.part_set = render_part_set;
    parts.apply_part = render_generic;
    mm_body_apply_set(body, b.part_count, set, &parts);
    changed(body, s);
}
void mm_body_set_custom(void *body,void *custom,const MMBodyRuntimeServices *s)
{
    void *p = s->shape.context;
    MMBodyShape b;
    BodySetBinding binding;
    MMBodySetServices parts;
    MMI32 count, set = s->custom_set(p, custom);
    s->retain(p, custom);
    mm_body_remove_material(body, set, s);
    s->assign_custom(p, body, set, custom);
    b = *s->shape.state(p, body);
    binding.services = s;
    binding.sets = b.sets;
    binding.material = custom;
    parts.context = &binding;
    parts.part_set = render_part_set;
    parts.apply_part = render_custom;
    count = mm_body_apply_set(body, b.part_count, set, &parts);
    if (count > s->custom_count(p, custom))
        diagnose(s, MM_BODY_SHORT_COSTUME);
    changed(body, s);
}
void mm_body_get_material(void *body,MMI32 set,int *generic,void **material,void **custom,const MMBodyRuntimeServices *s)
{
    MMBodyShape b; MMI32 i; void *p=s->shape.context,*raw;
    *custom=s->custom(p,body,set);
    if (*custom) { *generic=0; if (s->invalidate_output) s->invalidate_output(p,material); return; }
    *generic=1; if (s->invalidate_output) s->invalidate_output(p,custom);
    b=*s->shape.state(p,body);
    for (i=0;i<b.part_count;++i) if (set==s->shape.item(p,b.sets,i))
    {
        raw=s->part_material(p,body,i); if (!raw) diagnose(s,MM_BODY_MISSING_MATERIAL);
        *material=s->material_object(p,raw); return;
    }
    diagnose(s,MM_BODY_EMPTY_SET);
}
void mm_body_costume_init(MMBodyCostume *c) { c->entries=NULL; c->count=0; }
void mm_body_costume_clear(MMBodyCostume *c,const MMBodyRuntimeServices *s)
{
    MMI32 i; void *storage;
    if (c->entries)
    {
        for (i=0;i<c->count;++i) if (c->entries[i]) { s->shape.release(s->shape.context,c->entries[i]); c->entries[i]=NULL; }
        storage=c->entries; s->shape.free_memory(s->shape.context,&storage); c->entries=storage;
    }
    c->count=0;
}
int mm_body_costume_capture(MMBodyCostume *c,void *body,const MMBodyRuntimeServices *s)
{
    MMBodyShape b; MMI32 i; int generic; void *p=s->shape.context,*storage=NULL,*material,*custom;
    mm_body_costume_clear(c,s); b=*s->shape.state(p,body);
    if (!s->shape.allocate_materials(p,b.set_count,&storage)) { c->entries=storage; return 0; }
    c->entries=storage; b=*s->shape.state(p,body); c->count=b.set_count;
    for (i=0;i<c->count;++i)
    {
        mm_body_get_material(body,i,&generic,&material,&custom,s);
        c->entries[i]=generic?material:custom; s->retain(p,c->entries[i]);
    }
    return 1;
}
void mm_body_costume_apply(const MMBodyCostume *c,void *body,int allow_different,const MMBodyRuntimeServices *s)
{
    MMBodyShape b; MMI32 i,n; void *p=s->shape.context;
    b=*s->shape.state(p,body); n=c->count;
    if (allow_different) { if (b.set_count<n) n=b.set_count; }
    else if (n!=b.set_count) diagnose(s,MM_BODY_COSTUME_SHAPE);
    for (i=0;i<n;++i)
    { if (s->is_generic(p,c->entries[i])) mm_body_set_generic(body,i,c->entries[i],s); else mm_body_set_custom(body,c->entries[i],s); }
}
