/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "mm/body_runtime.h"
#include <stddef.h>
static void changed(void *body,const MMBodyRuntimeServices *s)
{
    const MMBodyState *state=s->shape.state(s->shape.context,body);
    if (!state->shape.hidden && state->render.changed)
        state->render.changed(state->render.context);
}
static void diagnose(const MMBodyRuntimeServices *s,int code)
{ if (s->diagnostic) s->diagnostic(s->shape.context,code); }
void mm_body_show(void *body,const MMBodyRuntimeServices *s)
{
    MMBodyState *state=s->shape.state(s->shape.context,body);
    if (--state->shape.hidden==0)
    {
        if (state->render.attach) state->render.attach(state->render.context,body);
        if (state->render.changed) state->render.changed(state->render.context);
    }
}
void mm_body_hide(void *body,const MMBodyRuntimeServices *s)
{
    void *p=s->shape.context;
    MMBodyState *state=s->shape.state(p,body);
    if (state->shape.hidden++==0)
    {
        mm_body_clear_bounds(&state->geometry);
        if (s->attached(p,state->shape.actors)) s->detach(p,state->shape.actors);
        if (state->render.changed) state->render.changed(state->render.context);
    }
}
void mm_body_highlight(void *body,int enabled,const MMBodyRuntimeServices *s)
{
    void *p=s->shape.context;
    s->highlight(p,s->shape.state(p,body)->shape.actors,enabled);
    changed(body,s);
}
void mm_body_locate(void *body,const MMActorBodyPose *pose,const MMBodyRuntimeServices *s)
{
    MMActorMatrix root;
    mm_actor_pose_root(pose, &root);
    mm_body_shape_matrix(&s->shape.state(s->shape.context,body)->shape,0,&root,&s->shape);
    changed(body, s);
}
void mm_body_set_matrix(void *body,MMI32 part,const MMActorMatrix *matrix,const MMBodyRuntimeServices *s)
{
    mm_body_shape_matrix(&s->shape.state(s->shape.context,body)->shape,part+2,matrix,&s->shape);
    changed(body,s);
}
void mm_body_set_model(void *body,MMI32 part,void *model,const MMBodyRuntimeServices *s)
{
    void *p = s->shape.context;
    MMBodyShape *shape = &s->shape.state(p,body)->shape;
    MMI32 index = part + 2;
    MMBodyPartBinding *binding = &shape->bindings[index];
    void *old = binding->model_owner;
    if (old == model)
        return;
    if (old)
        s->shape.release_model(p,old);
    binding->model_owner = model;
    binding->model_handle = s->model_handle(p,model);
    s->assign_model(p,shape->actors,index,binding->model_handle);
    s->retain_model(p,model);
    changed(body,s);
}
void mm_body_remove_material(void *body,MMI32 set,const MMBodyRuntimeServices *s)
{
    mm_body_shape_remove_material(&s->shape.state(s->shape.context,body)->shape,set,&s->shape);
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
    MMBodyShape *shape;
    void *material;
} BodySetBinding;

static MMI32 render_part_set(void *context, void *body, MMI32 part)
{
    const BodySetBinding *binding = (const BodySetBinding *)context;
    (void)body;
    return binding->shape->bindings[part+2].set;
}

static int render_generic(void *context, void *body, MMI32 part, MMI32 slot)
{
    const BodySetBinding *binding = (const BodySetBinding *)context;
    const MMBodyRuntimeServices *s = binding->services;
    void *p = s->shape.context;
    void *material = binding->material;
    (void)body;
    (void)slot;
    mm_body_shape_material(binding->shape, part, material, s->material_handle(p, material), &s->shape);
    s->retain(p, material);
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
    mm_body_shape_material(binding->shape, part, NULL, s->custom_material(p, binding->material, slot), &s->shape);
    return 1;
}

void mm_body_set_generic(void *body,MMI32 set,void *material,const MMBodyRuntimeServices *s)
{
    MMBodyShape *shape = &s->shape.state(s->shape.context,body)->shape;
    BodySetBinding binding;
    MMBodySetServices parts;
    mm_body_shape_remove_material(shape, set, &s->shape);
    binding.services = s;
    binding.shape = shape;
    binding.material = material;
    parts.context = &binding;
    parts.part_set = render_part_set;
    parts.apply_part = render_generic;
    mm_body_apply_set(body, shape->part_count, set, &parts);
    changed(body, s);
}
void mm_body_set_custom(void *body,void *custom,const MMBodyRuntimeServices *s)
{
    void *p = s->shape.context;
    MMBodyShape *shape = &s->shape.state(p,body)->shape;
    BodySetBinding binding;
    MMBodySetServices parts;
    MMI32 count, set = s->custom_set(p, custom);
    s->retain(p, custom);
    mm_body_shape_remove_material(shape, set, &s->shape);
    shape->custom_materials[set] = custom;
    binding.services = s;
    binding.shape = shape;
    binding.material = custom;
    parts.context = &binding;
    parts.part_set = render_part_set;
    parts.apply_part = render_custom;
    count = mm_body_apply_set(body, shape->part_count, set, &parts);
    if (count > s->custom_count(p, custom))
        diagnose(s, MM_BODY_SHORT_COSTUME);
    changed(body, s);
}
void mm_body_get_material(void *body,MMI32 set,int *generic,void **material,void **custom,const MMBodyRuntimeServices *s)
{
    void *p=s->shape.context;
    const MMBodyShape *shape=&s->shape.state(p,body)->shape;
    MMI32 i;
    *custom=shape->custom_materials[set];
    if (*custom) { *generic=0; if (s->invalidate_output) s->invalidate_output(p,material); return; }
    *generic=1; if (s->invalidate_output) s->invalidate_output(p,custom);
    for (i=0;i<shape->part_count;++i) if (set==shape->bindings[i+2].set)
    {
        *material=shape->bindings[i+2].material_owner;
        if (!*material) diagnose(s,MM_BODY_MISSING_MATERIAL);
        return;
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
    MMBodyShape b; MMI32 i; int generic; void *p=s->shape.context,*material,*custom;
    mm_body_costume_clear(c,s); b=s->shape.state(p,body)->shape;
    if (!s->shape.allocate_materials(p,b.set_count,&c->entries)) return 0;
    c->count=b.set_count;
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
    b=s->shape.state(p,body)->shape; n=c->count;
    if (allow_different) { if (b.set_count<n) n=b.set_count; }
    else if (n!=b.set_count) diagnose(s,MM_BODY_COSTUME_SHAPE);
    for (i=0;i<n;++i)
    { if (s->is_generic(p,c->entries[i])) mm_body_set_generic(body,i,c->entries[i],s); else mm_body_set_custom(body,c->entries[i],s); }
}
