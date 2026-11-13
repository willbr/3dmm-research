/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "three_mm/text_generation.h"
#include "three_mm/wide.h"
#include <limits.h>
#if SHRT_MAX != 32767
#error Text cel parts require 16-bit shorts
#endif
typedef struct Cel { ThreeMMU32 sound; ThreeMMI32 distance; } Cel;
typedef struct Part { short model,matrix; } Part;
static ThreeMMI32 half(ThreeMMI32 value)
{ return three_mm_signed_word(((ThreeMMU32)value>>1)|((ThreeMMU32)value&0x80000000UL)); }
void *three_mm_text_build_indices(void *object,int sets,const ThreeMMTextGenerationServices *s)
{
    ThreeMMI32 i,n=s->length(s->context,object); short value=(short)(sets?0:-1);
    void *list=s->list_new(s->context,2,n); if (!list) return NULL;
    s->list_count(s->context,list,n); for (i=0;i<n;++i) s->list_put(s->context,list,i,&value); return list;
}
void *three_mm_text_build_costumes(const ThreeMMTextGenerationServices *s)
{
    ThreeMMI32 count=1,id=0; void *group=s->group_new(s->context,4,1,4);
    if (group && !s->group_add(s->context,group,&count,4,&id)) { s->release(s->context,group); group=NULL; }
    return group;
}
int three_mm_text_initialize_lists(void *object,const ThreeMMTextGenerationServices *s)
{
    void *parents=NULL,*sets=NULL,*costumes=NULL,*material=NULL; ThreeMMTemplateState t;
    ThreeMMTextGenerated *a=s->state(s->context,object);
    parents=three_mm_text_build_indices(object,0,s); if (!parents) goto fail;
    sets=three_mm_text_build_indices(object,1,s); if (!sets) goto fail;
    costumes=three_mm_text_build_costumes(s); if (!costumes) goto fail;
    if (!a->material) material=s->material_new(s->context);
    else { material=a->material; s->retain(s->context,material); }
    if (!material) goto fail;
    s->template_read(s->context,object,&t);
    if (t.parents) { s->release(s->context,t.parents); t.parents=NULL; s->template_write(s->context,object,&t); }
    t.parents=parents; s->template_write(s->context,object,&t);
    if (t.sets) { s->release(s->context,t.sets); t.sets=NULL; s->template_write(s->context,object,&t); }
    t.sets=sets; s->template_write(s->context,object,&t);
    if (t.costumes) { s->release(s->context,t.costumes); t.costumes=NULL; s->template_write(s->context,object,&t); }
    t.costumes=costumes; s->template_write(s->context,object,&t);
    if (a->material) { s->release(s->context,a->material); a->material=NULL; }
    a->material=material;
    if (a->action) { s->release(s->context,a->action); a->action=NULL; }
    a->action_id=-1; t.action_count=THREE_MM_TEXT_ACTION_COUNT; t.custom_count=t.set_count=1; t.flags|=2U;
    s->template_write(s->context,object,&t); return 1;
fail:
    if (parents) s->release(s->context,parents);
    if (sets) s->release(s->context,sets);
    if (costumes) s->release(s->context,costumes);
    if (material) s->release(s->context,material); return 0;
}
ThreeMMI32 three_mm_text_cel_count(ThreeMMI32 action)
{
    switch (action)
    {
    case THREE_MM_TEXT_REST: return 1;
    case THREE_MM_TEXT_LETTER_ROT_X: case THREE_MM_TEXT_LETTER_ROT_Y: case THREE_MM_TEXT_LETTER_ROT_Z:
    case THREE_MM_TEXT_SWING_X: case THREE_MM_TEXT_SWING_Y: case THREE_MM_TEXT_SWING_Z:
    case THREE_MM_TEXT_PULSE: case THREE_MM_TEXT_HOP: case THREE_MM_TEXT_STRETCH: return 12;
    case THREE_MM_TEXT_WORD_ROT_X: case THREE_MM_TEXT_WORD_ROT_Y: case THREE_MM_TEXT_WORD_ROT_Z:
    case THREE_MM_TEXT_WAVE: case THREE_MM_TEXT_REVEAL: case THREE_MM_TEXT_WALK: return 24;
    default: return 0;
    }
}
void *three_mm_text_build_cels(void *object,ThreeMMI32 action,const ThreeMMTextGenerationServices *s)
{
    ThreeMMI32 n=s->length(s->context,object),i,j,cels=three_mm_text_cel_count(action);
    void *group=s->group_new(s->context,8,0,0); Part *parts=NULL; Cel cel;
    if (!group) return NULL;
    if (!s->allocate(s->context,n*4,(void **)&parts)) goto fail;
    for (i=0;i<cels;++i)
    {
        cel.sound=0; cel.distance=action==THREE_MM_TEXT_WALK?65536L:327680L;
        for (j=0;j<n;++j) { parts[j].model=(short)j; parts[j].matrix=(short)(i*n+j); }
        if (!s->group_add(s->context,group,&cel,n*4,parts)) goto fail;
    }
    s->free_memory(s->context,(void **)&parts); return group;
fail:
    s->free_memory(s->context,(void **)&parts); s->release(s->context,group); return NULL;
}
void *three_mm_text_build_matrices(void *object,ThreeMMI32 action,const ThreeMMTextGenerationServices *s)
{
    ThreeMMI32 n=s->length(s->context,object),i,j,cels,cw,ch,total=0,height=0,x,y,center,width,phase,theta,stretch;
    void *font=s->font(s->context,object),*list=NULL; ThreeMMActorMatrix matrix;
    if (!font) return NULL;
    cels=three_mm_text_cel_count(action); list=s->list_new(s->context,48,cels*n); if (!list) goto fail;
    s->list_count(s->context,list,cels*n);
    for (j=0;j<n;++j) total+=s->font_width(s->context,font,s->character(s->context,object,j));
    for (j=0;j<n;++j) height+=s->font_height(s->context,font,s->character(s->context,object,j));
    for (i=0;i<cels;++i)
    {
        x=-half(total); y=0;
        for (j=0;j<n;++j)
        {
            cw=s->font_width(s->context,font,s->character(s->context,object,j)); ch=s->font_height(s->context,font,s->character(s->context,object,j));
            center=x+half(cw); width=total; three_mm_matrix_identity(&matrix);
            if (action==THREE_MM_TEXT_STRETCH)
            {
                phase=three_mm_fixed_div(i*65536L,cels*65536L); theta=three_mm_fixed_mul(phase,411774L)+205887L+102943L;
                stretch=98304L+half(three_mm_angle_sin((unsigned short)three_mm_fixed_mul(theta,10430L)));
                center=three_mm_fixed_mul(center,stretch); width=three_mm_fixed_mul(total,stretch);
            }
            three_mm_text_apply_shape(&matrix,s->shape(s->context,object),n,j,center,width,y+ch,s->font_maximum(s->context,font),height);
            three_mm_text_apply_action(&matrix,action,j,cels,i,center,width);
            if (s->shape(s->context,object)==THREE_MM_TEXT_CIRCLE_Z)
            {
                ThreeMMActorMatrix translation,result; three_mm_matrix_identity(&translation);
                translation.m[3][1]=three_mm_fixed_div(total,411774L)+s->font_maximum(s->context,font);
                three_mm_matrix_multiply(&result,&matrix,&translation); matrix=result;
            }
            x+=cw; y+=ch; s->list_put(s->context,list,i*n+j,&matrix);
        }
    }
    s->release(s->context,font); return list;
fail:
    s->release(s->context,font); if (list) s->release(s->context,list); return NULL;
}
void *three_mm_text_build_action(void *object,ThreeMMI32 action,const ThreeMMTextGenerationServices *s)
{
    void *cels=three_mm_text_build_cels(object,action,s),*matrices=NULL,*result=NULL;
    if (!cels) return NULL;
    matrices=three_mm_text_build_matrices(object,action,s);
    if (matrices) result=s->action_new(s->context,cels,matrices,10U);
    s->release(s->context,cels); if (matrices) s->release(s->context,matrices); return result;
}
void *three_mm_text_fetch_action(void *object,ThreeMMI32 action,const ThreeMMTextGenerationServices *s)
{
    ThreeMMTextGenerated *a=s->state(s->context,object);
    if (action!=a->action_id)
    {
        if (a->action) { s->release(s->context,a->action); a->action=NULL; }
        a->action_id=-1; a->action=three_mm_text_build_action(object,action,s);
    }
    if (a->action) { s->retain(s->context,a->action); a->action_id=action; }
    return a->action;
}
void *three_mm_text_fetch_model(void *object,ThreeMMI32 index,const ThreeMMTextGenerationServices *s)
{
    void *font=s->font(s->context,object),*model;
    if (!font) return NULL;
    model=s->glyph(s->context,font,s->character(s->context,object,index)); s->release(s->context,font); return model;
}
int three_mm_text_adjust_body(void *object,void *body,const ThreeMMTextGenerationServices *s)
{ ThreeMMTemplateState t; s->template_read(s->context,object,&t); return s->body_shape(s->context,body,t.parents,t.sets); }
int three_mm_text_default_costume(void *object,void *body,const ThreeMMTextGenerationServices *s)
{ s->body_material(s->context,body,0,s->state(s->context,object)->material); return 1; }
void three_mm_text_dispose(ThreeMMTextGenerated *a,const ThreeMMTextGenerationServices *s)
{
    if (a->material) { s->release(s->context,a->material); a->material=NULL; }
    if (a->action) { s->release(s->context,a->action); a->action=NULL; }
}
