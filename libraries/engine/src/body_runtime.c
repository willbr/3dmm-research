/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "three_mm/body_runtime.h"
#include <stddef.h>
static void changed(void *body,const ThreeMMBodyRuntimeServices *s)
{ ThreeMMBodyShape b; s->shape.read(s->shape.context,body,&b); if (!b.hidden) s->changed(s->shape.context,body); }
static void diagnose(const ThreeMMBodyRuntimeServices *s,int code)
{ if (s->diagnostic) s->diagnostic(s->shape.context,code); }
void three_mm_body_show(void *body,const ThreeMMBodyRuntimeServices *s)
{
    ThreeMMBodyShape b; void *p=s->shape.context;
    s->shape.read(p,body,&b); --b.hidden; s->shape.write(p,body,&b);
    if (!b.hidden) { s->attach(p,body); s->changed(p,body); }
}
void three_mm_body_hide(void *body,const ThreeMMBodyRuntimeServices *s)
{
    ThreeMMBodyShape b; int visible; void *p=s->shape.context;
    s->shape.read(p,body,&b); visible=!b.hidden; ++b.hidden; s->shape.write(p,body,&b);
    if (visible) { s->clear_bounds(p,body); if (s->attached(p,body)) s->detach(p,body); s->changed(p,body); }
}
void three_mm_body_highlight(void *body,int enabled,const ThreeMMBodyRuntimeServices *s)
{ s->highlight(s->shape.context,body,enabled); changed(body,s); }
void three_mm_body_locate(void *body,ThreeMMI32 x,ThreeMMI32 y,ThreeMMI32 z,const void *matrix,const ThreeMMBodyRuntimeServices *s)
{ s->matrix(s->shape.context,body,0,matrix); s->translate(s->shape.context,body,x,y,z); changed(body,s); }
void three_mm_body_set_matrix(void *body,ThreeMMI32 part,const void *matrix,const ThreeMMBodyRuntimeServices *s)
{ s->matrix(s->shape.context,body,part+2,matrix); changed(body,s); }
void three_mm_body_set_model(void *body,ThreeMMI32 part,void *model,const ThreeMMBodyRuntimeServices *s)
{
    void *p=s->shape.context,*old=s->model(p,body,part);
    if (old) { if (old==model) return; s->shape.release(p,old); }
    s->assign_model(p,body,part,model); s->retain(p,model); changed(body,s);
}
void three_mm_body_remove_material(void *body,ThreeMMI32 set,const ThreeMMBodyRuntimeServices *s)
{
    ThreeMMBodyShape b; ThreeMMI32 i; void *p=s->shape.context,*custom=s->custom(p,body,set),*raw;
    int had_custom=custom!=NULL;
    if (custom) { s->shape.release(p,custom); s->assign_custom(p,body,set,NULL); }
    s->shape.read(p,body,&b);
    for (i=0;i<b.part_count;++i) if (set==s->shape.item(p,b.sets,i))
    {
        raw=s->part_material(p,body,i);
        if (raw) { if (!had_custom) s->shape.release(p,s->material_object(p,raw)); s->assign_material(p,body,i,NULL); }
    }
}
void three_mm_body_set_generic(void *body,ThreeMMI32 set,void *material,const ThreeMMBodyRuntimeServices *s)
{
    ThreeMMBodyShape b; ThreeMMI32 i; void *p=s->shape.context;
    three_mm_body_remove_material(body,set,s); s->shape.read(p,body,&b);
    for (i=0;i<b.part_count;++i) if (set==s->shape.item(p,b.sets,i))
    { s->assign_material(p,body,i,s->material_handle(p,material)); s->retain(p,material); }
    changed(body,s);
}
void three_mm_body_set_custom(void *body,void *custom,const ThreeMMBodyRuntimeServices *s)
{
    ThreeMMBodyShape b; ThreeMMI32 i,index=0,set; void *p=s->shape.context,*model;
    set=s->custom_set(p,custom); s->retain(p,custom); three_mm_body_remove_material(body,set,s);
    s->assign_custom(p,body,set,custom); s->shape.read(p,body,&b);
    for (i=0;i<b.part_count;++i) if (set==s->shape.item(p,b.sets,i))
    {
        model=s->custom_model(p,custom,index); if (model) three_mm_body_set_model(body,i,model,s);
        s->assign_material(p,body,i,s->custom_material(p,custom,index)); ++index;
    }
    if (index>s->custom_count(p,custom)) diagnose(s,THREE_MM_BODY_SHORT_COSTUME);
    changed(body,s);
}
void three_mm_body_get_material(void *body,ThreeMMI32 set,int *generic,void **material,void **custom,const ThreeMMBodyRuntimeServices *s)
{
    ThreeMMBodyShape b; ThreeMMI32 i; void *p=s->shape.context,*raw;
    *custom=s->custom(p,body,set);
    if (*custom) { *generic=0; if (s->invalidate_output) s->invalidate_output(p,material); return; }
    *generic=1; if (s->invalidate_output) s->invalidate_output(p,custom);
    s->shape.read(p,body,&b);
    for (i=0;i<b.part_count;++i) if (set==s->shape.item(p,b.sets,i))
    {
        raw=s->part_material(p,body,i); if (!raw) diagnose(s,THREE_MM_BODY_MISSING_MATERIAL);
        *material=s->material_object(p,raw); return;
    }
    diagnose(s,THREE_MM_BODY_EMPTY_SET);
}
void three_mm_body_costume_init(ThreeMMBodyCostume *c) { c->entries=NULL; c->count=0; }
void three_mm_body_costume_clear(ThreeMMBodyCostume *c,const ThreeMMBodyRuntimeServices *s)
{
    ThreeMMI32 i; void *storage;
    if (c->entries)
    {
        for (i=0;i<c->count;++i) if (c->entries[i]) { s->shape.release(s->shape.context,c->entries[i]); c->entries[i]=NULL; }
        storage=c->entries; s->shape.free_memory(s->shape.context,&storage); c->entries=storage;
    }
    c->count=0;
}
int three_mm_body_costume_capture(ThreeMMBodyCostume *c,void *body,const ThreeMMBodyRuntimeServices *s)
{
    ThreeMMBodyShape b; ThreeMMI32 i; int generic; void *p=s->shape.context,*storage=NULL,*material,*custom;
    three_mm_body_costume_clear(c,s); s->shape.read(p,body,&b);
    if (!s->shape.allocate_materials(p,b.set_count,&storage)) { c->entries=storage; return 0; }
    c->entries=storage; s->shape.read(p,body,&b); c->count=b.set_count;
    for (i=0;i<c->count;++i)
    {
        three_mm_body_get_material(body,i,&generic,&material,&custom,s);
        c->entries[i]=generic?material:custom; s->retain(p,c->entries[i]);
    }
    return 1;
}
void three_mm_body_costume_apply(const ThreeMMBodyCostume *c,void *body,int allow_different,const ThreeMMBodyRuntimeServices *s)
{
    ThreeMMBodyShape b; ThreeMMI32 i,n; void *p=s->shape.context;
    s->shape.read(p,body,&b); n=c->count;
    if (allow_different) { if (b.set_count<n) n=b.set_count; }
    else if (n!=b.set_count) diagnose(s,THREE_MM_BODY_COSTUME_SHAPE);
    for (i=0;i<n;++i)
    { if (s->is_generic(p,c->entries[i])) three_mm_body_set_generic(body,i,c->entries[i],s); else three_mm_body_set_custom(body,c->entries[i],s); }
}
