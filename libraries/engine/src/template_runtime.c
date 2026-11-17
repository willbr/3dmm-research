/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "mm/template_runtime.h"
#include <string.h>
static MMU32 resource_type(int kind)
{ return kind==MM_TEMPLATE_ACTION?MM_ACTN:kind==MM_TEMPLATE_MODEL?MM_TEMPLATE_BMDL:MM_CMTL; }
static int child(void *object,MMU32 id,int kind,MMTemplateBinding *b,MMTemplateChild *c,const MMTemplateRuntimeServices *s)
{
    s->binding(s->storage.context,object,b);
    return s->storage.child(s->storage.context,b->file,b->type,b->number,id,resource_type(kind),c);
}
void *mm_template_fetch_child(void *object,MMU32 id,int kind,const MMTemplateRuntimeServices *s)
{
    MMTemplateBinding b; MMTemplateChild c;
    if (!child(object,id,kind,&b,&c,s)) return NULL;
    return s->fetch(s->storage.context,b.cache,c.type,c.number,kind);
}
int mm_template_action_name(void *object,MMI32 id,void *out,const MMTemplateRuntimeServices *s)
{
    MMTemplateBinding b; MMTemplateChild c;
    if (!child(object,(MMU32)id,MM_TEMPLATE_ACTION,&b,&c,s)) return 0;
    return s->name(s->storage.context,b.file,c.type,c.number,out);
}
MMI32 mm_template_wrap_cel(MMI32 cel, MMI32 count)
{
    MMI32 index = cel % count;
    return index < 0 ? index + count : index;
}

int mm_template_bind_cel(MMU32 count, const MMTemplateCelServices *s)
{
    MMU32 i;
    MMI32 model, matrix;
    void *context = s->context;
    for (i = 0; i < count; ++i)
    {
        s->part(context, i, &model, &matrix);
        if (!s->prepare_part(context, i, model, matrix, model != MM_TEMPLATE_KEEP_MODEL))
            return 0;
    }
    if (s->begin_apply)
        s->begin_apply(context);
    for (i = 0; i < count; ++i)
    {
        s->part(context, i, &model, &matrix);
        s->apply_part(context, i, model != MM_TEMPLATE_KEEP_MODEL, matrix);
    }
    return 1;
}

static MMI32 wrapped(void *action, MMI32 cel, const MMTemplateRuntimeServices *s)
{
    return mm_template_wrap_cel(cel, s->cel_count(s->storage.context, action));
}

typedef struct TemplateCel
{
    const MMTemplateRuntimeServices *services;
    void *object, *body, *action, *distance;
    void **models;
    MMI32 index, step;
} TemplateCel;

static void render_cel_part(void *context, MMU32 part, MMI32 *model, MMI32 *matrix)
{
    const TemplateCel *cel = (const TemplateCel *)context;
    const MMTemplateRuntimeServices *s = cel->services;
    s->part(s->storage.context, cel->action, cel->index, (MMI32)part, model, matrix);
}

static int render_cel_prepare(void *context, MMU32 part, MMI32 model, MMI32 matrix, int replace_model)
{
    TemplateCel *cel = (TemplateCel *)context;
    const MMTemplateRuntimeServices *s = cel->services;
    (void)matrix;
    if (!replace_model)
        return 1;
    cel->models[part] = s->model(s->storage.context, cel->object, (MMU32)model);
    return cel->models[part] != NULL;
}

static void render_cel_begin(void *context)
{
    const TemplateCel *cel = (const TemplateCel *)context;
    if (cel->distance)
        memcpy(cel->distance, &cel->step, sizeof(cel->step));
}

static void render_cel_apply(void *context, MMU32 part, int replace_model, MMI32 matrix)
{
    const TemplateCel *cel = (const TemplateCel *)context;
    const MMTemplateRuntimeServices *s = cel->services;
    MMI32 transform[12];
    if (replace_model)
        s->set_model(s->storage.context, cel->body, (MMI32)part, cel->models[part]);
    s->matrix(s->storage.context, cel->action, matrix, transform);
    s->set_matrix(s->storage.context, cel->body, (MMI32)part, transform);
}

int mm_template_set_cel(void *object, void *body, MMI32 id, MMI32 cel, void *distance,
                        const MMTemplateRuntimeServices *s)
{
    const MMTemplateCreateServices *a = &s->storage;
    TemplateCel request;
    MMTemplateCelServices bindings;
    MMTemplateState state;
    void *storage = NULL;
    MMI32 count, i;
    int result = 0;
    state = *a->state(a->context, object);
    count = a->count(a->context, state.parents);
    request.action = s->action(a->context, object, id);
    if (!request.action)
        return 0;
    request.services = s;
    request.object = object;
    request.body = body;
    request.distance = distance;
    request.index = wrapped(request.action, cel, s);
    request.step = s->distance(a->context, request.action, request.index);
    if (!a->allocate_refs(a->context, count, &storage))
        goto done;
    request.models = storage;
    bindings.context = &request;
    bindings.part = render_cel_part;
    bindings.prepare_part = render_cel_prepare;
    bindings.begin_apply = render_cel_begin;
    bindings.apply_part = render_cel_apply;
    result = mm_template_bind_cel((MMU32)count, &bindings);
done:
    if (storage)
    {
        void **models = storage;
        for (i = 0; i < count; ++i)
            if (models[i])
            {
                a->release(a->context, models[i]);
                models[i] = NULL;
            }
        a->free_memory(a->context, &storage);
    }
    a->release(a->context, request.action);
    return result;
}
int mm_template_distance(void *object,MMI32 id,MMI32 cel,MMI32 *out,const MMTemplateRuntimeServices *s)
{
    void *a=s->action(s->storage.context,object,id); MMI32 d;
    if (!a) return 0;
    d=s->distance(s->storage.context,a,wrapped(a,cel,s)); s->storage.release(s->storage.context,a); *out=d; return 1;
}
int mm_template_cel_count(void *object,MMI32 id,void *out,const MMTemplateRuntimeServices *s)
{
    void *a=s->action(s->storage.context,object,id); MMI32 count; if (!a) return 0;
    count=s->cel_count(s->storage.context,a); memcpy(out,&count,sizeof(count)); s->storage.release(s->storage.context,a); return 1;
}
int mm_template_action_flags(void *object,MMI32 id,void *out,const MMTemplateRuntimeServices *s)
{
    void *a=s->action(s->storage.context,object,id); MMU32 flags; if (!a) return 0;
    flags=s->flags(s->storage.context,a); memcpy(out,&flags,sizeof(flags)); s->storage.release(s->storage.context,a); return 1;
}
int mm_template_sound(void *object,MMI32 id,MMI32 cel,void *exists,void *tag,const MMTemplateRuntimeServices *s)
{
    void *a; s->sound_exists(s->storage.context,exists,0); a=s->action(s->storage.context,object,id); if (!a) return 0;
    if (s->sound(s->storage.context,a,wrapped(a,cel,s),tag)) s->sound_exists(s->storage.context,exists,1);
    s->storage.release(s->storage.context,a); return 1;
}
MMI32 mm_template_costume_count(void *object,MMI32 set,const MMTemplateRuntimeServices *s)
{
    MMTemplateState t; MMI32 value; t=*s->storage.state(s->storage.context,object);
    memcpy(&value,s->storage.fixed_data(s->storage.context,t.costumes,set),sizeof(value)); return value;
}
MMI32 mm_template_costume_id(void *object,MMI32 set,MMI32 index,const MMTemplateRuntimeServices *s)
{
    MMTemplateState t; MMI32 value; t=*s->storage.state(s->storage.context,object);
    memcpy(&value,s->storage.variable_data(s->storage.context,t.costumes,set)+index*sizeof(value),sizeof(value)); return value;
}
int mm_template_has_models(void *file,MMU32 type,MMU32 number,const MMTemplateCreateServices *s)
{ MMTemplateChild c; return s->child(s->context,file,type,number,0,MM_TEMPLATE_BMDL,&c); }
int mm_template_equal_models(void *file,MMU32 first,MMU32 second,const MMTemplateCreateServices *s)
{
    MMU32 id=0; MMTemplateChild a,b;
    while (s->child(s->context,file,MM_CMTL,first,id,MM_TEMPLATE_BMDL,&a))
    {
        if (!s->child(s->context,file,MM_CMTL,second,id,MM_TEMPLATE_BMDL,&b) || a.number!=b.number) return 0;
        ++id;
    }
    return !s->child(s->context,file,MM_CMTL,second,id,MM_TEMPLATE_BMDL,&b);
}
int mm_template_is_accessory(void *object,MMI32 set,const MMTemplateRuntimeServices *s)
{
    MMTemplateBinding b; MMTemplateChild c; MMI32 id;
    s->binding(s->storage.context,object,&b); if (!b.cache) return 0;
    id=mm_template_costume_id(object,set,0,s);
    if (!s->storage.child(s->storage.context,b.file,b.type,b.number,(MMU32)id,MM_CMTL,&c)) return 0;
    return mm_template_has_models(b.file,c.type,c.number,&s->storage);
}
int mm_template_accessory_set(void *object,MMI32 set,void *out,const MMTemplateRuntimeServices *s)
{
    MMTemplateState t; MMI32 candidate,part,parent; t=*s->storage.state(s->storage.context,object);
    if (mm_template_is_accessory(object,set,s)) { memcpy(out,&set,sizeof(set)); return 1; }
    for (candidate=0;candidate<t.set_count;++candidate) if (mm_template_is_accessory(object,candidate,s))
        for (part=0;part<s->storage.count(s->storage.context,t.parents);++part)
            if (s->storage.item(s->storage.context,t.sets,part)==candidate)
            {
                parent=s->storage.item(s->storage.context,t.parents,part);
                if (s->storage.item(s->storage.context,t.sets,parent)==set) { memcpy(out,&candidate,sizeof(candidate)); return 1; }
            }
    return 0;
}
int mm_template_same_accessory(void *object,MMI32 first,MMI32 second,const MMTemplateRuntimeServices *s)
{
    MMTemplateBinding b; MMTemplateChild a,c;
    if (!child(object,(MMU32)first,MM_TEMPLATE_CUSTOM,&b,&a,s) ||
        !child(object,(MMU32)second,MM_TEMPLATE_CUSTOM,&b,&c,s)) return 0;
    return mm_template_equal_models(b.file,a.number,c.number,&s->storage);
}
