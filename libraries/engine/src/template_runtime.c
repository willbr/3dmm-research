/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "three_mm/template_runtime.h"
#include <string.h>
static ThreeMMU32 resource_type(int kind)
{ return kind==THREE_MM_TEMPLATE_ACTION?THREE_MM_ACTN:kind==THREE_MM_TEMPLATE_MODEL?THREE_MM_TEMPLATE_BMDL:THREE_MM_CMTL; }
static int child(void *object,ThreeMMU32 id,int kind,ThreeMMTemplateBinding *b,ThreeMMTemplateChild *c,const ThreeMMTemplateRuntimeServices *s)
{
    s->binding(s->storage.context,object,b);
    return s->storage.child(s->storage.context,b->file,b->type,b->number,id,resource_type(kind),c);
}
void *three_mm_template_fetch_child(void *object,ThreeMMU32 id,int kind,const ThreeMMTemplateRuntimeServices *s)
{
    ThreeMMTemplateBinding b; ThreeMMTemplateChild c;
    if (!child(object,id,kind,&b,&c,s)) return NULL;
    return s->fetch(s->storage.context,b.cache,c.type,c.number,kind);
}
int three_mm_template_action_name(void *object,ThreeMMI32 id,void *out,const ThreeMMTemplateRuntimeServices *s)
{
    ThreeMMTemplateBinding b; ThreeMMTemplateChild c;
    if (!child(object,(ThreeMMU32)id,THREE_MM_TEMPLATE_ACTION,&b,&c,s)) return 0;
    return s->name(s->storage.context,b.file,c.type,c.number,out);
}
static ThreeMMI32 wrapped(void *action,ThreeMMI32 cel,const ThreeMMTemplateRuntimeServices *s)
{
    ThreeMMI32 index=cel%s->cel_count(s->storage.context,action);
    if (index<0) index+=s->cel_count(s->storage.context,action);
    return index;
}
int three_mm_template_set_cel(void *object,void *body,ThreeMMI32 id,ThreeMMI32 cel,void *distance,const ThreeMMTemplateRuntimeServices *s)
{
    const ThreeMMTemplateCreateServices *a=&s->storage; void *action; void **models=NULL;
    ThreeMMTemplateState state; ThreeMMI32 count,i,index,d,model,matrix,transform[12]; int result=0;
    a->read(a->context,object,&state); count=a->count(a->context,state.parents);
    action=s->action(a->context,object,id); if (!action) return 0;
    index=wrapped(action,cel,s); d=s->distance(a->context,action,index);
    if (!a->allocate_refs(a->context,count,(void **)&models)) goto done;
    for (i=0;i<count;++i)
    {
        s->part(a->context,action,index,i,&model,&matrix);
        if (model!=-1)
        {
            models[i]=s->model(a->context,object,(ThreeMMU32)model);
            if (!models[i]) goto done;
        }
    }
    if (distance) memcpy(distance,&d,sizeof(d));
    for (i=0;i<count;++i)
    {
        if (models[i]) s->set_model(a->context,body,i,models[i]);
        s->part(a->context,action,index,i,&model,&matrix);
        s->matrix(a->context,action,matrix,transform); s->set_matrix(a->context,body,i,transform);
    }
    result=1;
done:
    if (models)
    {
        for (i=0;i<count;++i) if (models[i]) { a->release(a->context,models[i]); models[i]=NULL; }
        a->free_memory(a->context,(void **)&models);
    }
    a->release(a->context,action); return result;
}
int three_mm_template_distance(void *object,ThreeMMI32 id,ThreeMMI32 cel,ThreeMMI32 *out,const ThreeMMTemplateRuntimeServices *s)
{
    void *a=s->action(s->storage.context,object,id); ThreeMMI32 d;
    if (!a) return 0;
    d=s->distance(s->storage.context,a,wrapped(a,cel,s)); s->storage.release(s->storage.context,a); *out=d; return 1;
}
int three_mm_template_cel_count(void *object,ThreeMMI32 id,void *out,const ThreeMMTemplateRuntimeServices *s)
{
    void *a=s->action(s->storage.context,object,id); ThreeMMI32 count; if (!a) return 0;
    count=s->cel_count(s->storage.context,a); memcpy(out,&count,sizeof(count)); s->storage.release(s->storage.context,a); return 1;
}
int three_mm_template_action_flags(void *object,ThreeMMI32 id,void *out,const ThreeMMTemplateRuntimeServices *s)
{
    void *a=s->action(s->storage.context,object,id); ThreeMMU32 flags; if (!a) return 0;
    flags=s->flags(s->storage.context,a); memcpy(out,&flags,sizeof(flags)); s->storage.release(s->storage.context,a); return 1;
}
int three_mm_template_sound(void *object,ThreeMMI32 id,ThreeMMI32 cel,void *exists,void *tag,const ThreeMMTemplateRuntimeServices *s)
{
    void *a; s->sound_exists(s->storage.context,exists,0); a=s->action(s->storage.context,object,id); if (!a) return 0;
    if (s->sound(s->storage.context,a,wrapped(a,cel,s),tag)) s->sound_exists(s->storage.context,exists,1);
    s->storage.release(s->storage.context,a); return 1;
}
ThreeMMI32 three_mm_template_costume_count(void *object,ThreeMMI32 set,const ThreeMMTemplateRuntimeServices *s)
{
    ThreeMMTemplateState t; ThreeMMI32 value; s->storage.read(s->storage.context,object,&t);
    memcpy(&value,s->storage.fixed_data(s->storage.context,t.costumes,set),sizeof(value)); return value;
}
ThreeMMI32 three_mm_template_costume_id(void *object,ThreeMMI32 set,ThreeMMI32 index,const ThreeMMTemplateRuntimeServices *s)
{
    ThreeMMTemplateState t; ThreeMMI32 value; s->storage.read(s->storage.context,object,&t);
    memcpy(&value,s->storage.variable_data(s->storage.context,t.costumes,set)+index*sizeof(value),sizeof(value)); return value;
}
int three_mm_template_has_models(void *file,ThreeMMU32 type,ThreeMMU32 number,const ThreeMMTemplateCreateServices *s)
{ ThreeMMTemplateChild c; return s->child(s->context,file,type,number,0,THREE_MM_TEMPLATE_BMDL,&c); }
int three_mm_template_equal_models(void *file,ThreeMMU32 first,ThreeMMU32 second,const ThreeMMTemplateCreateServices *s)
{
    ThreeMMU32 id=0; ThreeMMTemplateChild a,b;
    while (s->child(s->context,file,THREE_MM_CMTL,first,id,THREE_MM_TEMPLATE_BMDL,&a))
    {
        if (!s->child(s->context,file,THREE_MM_CMTL,second,id,THREE_MM_TEMPLATE_BMDL,&b) || a.number!=b.number) return 0;
        ++id;
    }
    return !s->child(s->context,file,THREE_MM_CMTL,second,id,THREE_MM_TEMPLATE_BMDL,&b);
}
int three_mm_template_is_accessory(void *object,ThreeMMI32 set,const ThreeMMTemplateRuntimeServices *s)
{
    ThreeMMTemplateBinding b; ThreeMMTemplateChild c; ThreeMMI32 id;
    s->binding(s->storage.context,object,&b); if (!b.cache) return 0;
    id=three_mm_template_costume_id(object,set,0,s);
    if (!s->storage.child(s->storage.context,b.file,b.type,b.number,(ThreeMMU32)id,THREE_MM_CMTL,&c)) return 0;
    return three_mm_template_has_models(b.file,c.type,c.number,&s->storage);
}
int three_mm_template_accessory_set(void *object,ThreeMMI32 set,void *out,const ThreeMMTemplateRuntimeServices *s)
{
    ThreeMMTemplateState t; ThreeMMI32 candidate,part,parent; s->storage.read(s->storage.context,object,&t);
    if (three_mm_template_is_accessory(object,set,s)) { memcpy(out,&set,sizeof(set)); return 1; }
    for (candidate=0;candidate<t.set_count;++candidate) if (three_mm_template_is_accessory(object,candidate,s))
        for (part=0;part<s->storage.count(s->storage.context,t.parents);++part)
            if (s->storage.item(s->storage.context,t.sets,part)==candidate)
            {
                parent=s->storage.item(s->storage.context,t.parents,part);
                if (s->storage.item(s->storage.context,t.sets,parent)==set) { memcpy(out,&candidate,sizeof(candidate)); return 1; }
            }
    return 0;
}
int three_mm_template_same_accessory(void *object,ThreeMMI32 first,ThreeMMI32 second,const ThreeMMTemplateRuntimeServices *s)
{
    ThreeMMTemplateBinding b; ThreeMMTemplateChild a,c;
    if (!child(object,(ThreeMMU32)first,THREE_MM_TEMPLATE_CUSTOM,&b,&a,s) ||
        !child(object,(ThreeMMU32)second,THREE_MM_TEMPLATE_CUSTOM,&b,&c,s)) return 0;
    return three_mm_template_equal_models(b.file,a.number,c.number,&s->storage);
}
