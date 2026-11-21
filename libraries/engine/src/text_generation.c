/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "mm/text_generation.h"
#include "mm/actor_pose.h"
#include "mm/wide.h"
#include <limits.h>
#include <string.h>
#if SHRT_MAX != 32767
#error Text cel parts require 16-bit shorts
#endif
static MMI32 half(MMI32 value)
{ return mm_signed_word(((MMU32)value>>1)|((MMU32)value&0x80000000UL)); }
static MMPartIndexList *build_indices(MMI32 count, short value, const MMTextGenerationServices *s)
{
    MMPartIndexList *list = mm_part_indices_new(&s->table_allocator);
    if (list && !mm_part_indices_fill(list, count, value))
    {
        mm_part_indices_dispose(list);
        return NULL;
    }
    return list;
}
static MMTemplateCostumeTable *build_costumes(const MMTextGenerationServices *s)
{
    MMI32 id = 0;
    MMTemplateCostumeTable *table = mm_template_costumes_new(&s->table_allocator);
    if (table && (!mm_template_costumes_reserve(table, 1, MM_TEMPLATE_COSTUME_WORD_BYTES) ||
                  !mm_template_costumes_append(table, &id, 1)))
    {
        mm_template_costumes_dispose(table);
        return NULL;
    }
    return table;
}
int mm_text_initialize_lists(void *object,const MMTextGenerationServices *s)
{
    MMPartIndexList *parents=NULL,*sets=NULL;
    void *material=NULL; MMTemplateState t;
    MMI32 count=s->length(s->context,object);
    MMTemplateCostumeTable *costumes = NULL;
    MMTextGenerated *a=s->state(s->context,object);
    parents=build_indices(count,-1,s); if (!parents) goto fail;
    sets=build_indices(count,0,s); if (!sets) goto fail;
    costumes=build_costumes(s); if (!costumes) goto fail;
    if (!a->material) material=s->material_new(s->context);
    else { material=a->material; s->retain(s->context,material); }
    if (!material) goto fail;
    t=*s->template_state(s->context,object);
    if (t.parents) { mm_part_indices_dispose(t.parents); t.parents=NULL; *s->template_state(s->context,object)=t; }
    t.parents=parents; *s->template_state(s->context,object)=t;
    if (t.sets) { mm_part_indices_dispose(t.sets); t.sets=NULL; *s->template_state(s->context,object)=t; }
    t.sets=sets; *s->template_state(s->context,object)=t;
    if (t.costumes) { mm_template_costumes_dispose(t.costumes); t.costumes=NULL; *s->template_state(s->context,object)=t; }
    t.costumes=costumes; *s->template_state(s->context,object)=t;
    if (a->material) { s->release(s->context,a->material); a->material=NULL; }
    a->material=material;
    mm_action_resource_release(a->action);
    a->action=NULL;
    a->action_id=-1; t.action_count=MM_TEXT_ACTION_COUNT; t.custom_count=t.set_count=1; t.flags|=2U;
    *s->template_state(s->context,object)=t; return 1;
fail:
    mm_part_indices_dispose(parents);
    mm_part_indices_dispose(sets);
    mm_template_costumes_dispose(costumes);
    if (material) s->release(s->context,material); return 0;
}
MMI32 mm_text_cel_count(MMI32 action)
{
    switch (action)
    {
    case MM_TEXT_REST: return 1;
    case MM_TEXT_LETTER_ROT_X: case MM_TEXT_LETTER_ROT_Y: case MM_TEXT_LETTER_ROT_Z:
    case MM_TEXT_SWING_X: case MM_TEXT_SWING_Y: case MM_TEXT_SWING_Z:
    case MM_TEXT_PULSE: case MM_TEXT_HOP: case MM_TEXT_STRETCH: return 12;
    case MM_TEXT_WORD_ROT_X: case MM_TEXT_WORD_ROT_Y: case MM_TEXT_WORD_ROT_Z:
    case MM_TEXT_WAVE: case MM_TEXT_REVEAL: case MM_TEXT_WALK: return 24;
    default: return 0;
    }
}
MMI32 mm_text_cel_distance(MMI32 action)
{ return action==MM_TEXT_WALK?65536L:327680L; }
MMU32 mm_text_action_flags(void)
{ return MM_ACTOR_ROTATE_Y | MM_ACTOR_STATIC_ACTION; }
static int text_action_size(MMI32 characters, MMI32 cels)
{
    return characters >= 0 && cels > 0 && characters <= ((MMI32)SHRT_MAX + 1) / cels;
}

MMActionTable *mm_text_build_cels(void *object, MMI32 action, const MMTextGenerationServices *s)
{
    MMI32 n = s->length(s->context, object), cels = mm_text_cel_count(action);
    MMI32 i, j, distance = mm_text_cel_distance(action);
    MMActionTable *table;
    unsigned char *parts;
    short indices[2];
    if (!text_action_size(n, cels))
        return NULL;
    table = mm_action_table_new(&s->table_allocator);
    if (!table)
        return NULL;
    mm_action_cels_prepare(table);
    for (i = 0; i < cels; ++i)
    {
        parts = mm_action_cel_append(table, 0, distance, (MMU32)n);
        if (!parts)
        {
            mm_action_table_dispose(table);
            return NULL;
        }
        for (j = 0; j < n; ++j)
        {
            indices[0] = (short)j;
            indices[1] = (short)(i * n + j);
            memcpy(parts + j * MM_ACTION_PART_BYTES, indices, MM_ACTION_PART_BYTES);
        }
    }
    return table;
}

static void write_matrices(void *object, void *font, MMI32 action, MMI32 n, MMI32 first, MMI32 end,
                           MMTextMatrixWrite write, void *target, const MMTextGenerationServices *s)
{
    MMI32 i,j,cels=mm_text_cel_count(action),cw,ch,total=0,height=0,x,y,center,width,phase,theta,stretch;
    MMI32 shape=s->shape(s->context,object);
    MMActorMatrix matrix;
    for (j=0;j<n;++j) total+=s->font_width(s->context,font,s->character(s->context,object,j));
    for (j=0;j<n;++j) height+=s->font_height(s->context,font,s->character(s->context,object,j));
    for (i=first;i<end;++i)
    {
        x=-half(total); y=0;
        for (j=0;j<n;++j)
        {
            cw=s->font_width(s->context,font,s->character(s->context,object,j)); ch=s->font_height(s->context,font,s->character(s->context,object,j));
            center=x+half(cw); width=total; mm_matrix_identity(&matrix);
            if (action==MM_TEXT_STRETCH)
            {
                phase=mm_fixed_div(i*65536L,cels*65536L); theta=mm_fixed_mul(phase,411774L)+205887L+102943L;
                stretch=98304L+half(mm_angle_sin((unsigned short)mm_fixed_mul(theta,10430L)));
                center=mm_fixed_mul(center,stretch); width=mm_fixed_mul(total,stretch);
            }
            mm_text_apply_shape(&matrix,shape,n,j,center,width,y+ch,s->font_maximum(s->context,font),height);
            mm_text_apply_action(&matrix,action,j,cels,i,center,width);
            if (shape==MM_TEXT_CIRCLE_Z)
            {
                MMActorMatrix translation,result; mm_matrix_identity(&translation);
                translation.m[3][1]=mm_fixed_div(total,411774L)+s->font_maximum(s->context,font);
                mm_matrix_multiply(&result,&matrix,&translation); matrix=result;
            }
            x+=cw; y+=ch; write(target,(i-first)*n+j,&matrix);
        }
    }
}
static void store_matrix(void *table, MMI32 index, const MMActorMatrix *matrix)
{
    mm_action_matrix_put((MMActionTable *)table, (MMU32)index, matrix);
}

MMActionTable *mm_text_build_matrices(void *object, MMI32 action, const MMTextGenerationServices *s)
{
    MMI32 n = s->length(s->context, object), cels = mm_text_cel_count(action);
    MMActionTable *table;
    void *font;
    if (!text_action_size(n, cels))
        return NULL;
    font = s->font(s->context, object);
    if (!font)
        return NULL;
    table = mm_action_table_new(&s->table_allocator);
    if (table && !mm_action_matrices_prepare(table, (MMU32)(cels * n)))
    {
        mm_action_table_dispose(table);
        table = NULL;
    }
    if (table)
        write_matrices(object, font, action, n, 0, cels, store_matrix, table, s);
    s->release_font(s->context, font);
    return table;
}

int mm_text_evaluate_cel(void *object, MMI32 action, MMI32 cel, MMTextMatrixWrite write, void *target,
                         const MMTextGenerationServices *s)
{
    MMI32 n = s->length(s->context, object), cels = mm_text_cel_count(action);
    void *font;
    if (!text_action_size(n, cels) || cel < 0 || cel >= cels)
        return 0;
    font = s->font(s->context, object);
    if (!font)
        return 0;
    write_matrices(object, font, action, n, cel, cel + 1, write, target, s);
    s->release_font(s->context, font);
    return 1;
}

static MMActionResource *build_action(void *object, MMI32 action, const MMTextGenerationServices *s)
{
    MMActionDefinition definition = {0};
    MMActionResource *result = NULL;
    definition.cels = mm_text_build_cels(object, action, s);
    if (!definition.cels)
        return NULL;
    definition.matrices = mm_text_build_matrices(object, action, s);
    definition.flags = mm_text_action_flags();
    if (definition.matrices)
        result = mm_action_resource_create(&definition, s->action_host);
    if (!result)
        mm_action_dispose(&definition);
    return result;
}
MMActionResource *mm_text_fetch_action(void *object,MMI32 action,const MMTextGenerationServices *s)
{
    MMTextGenerated *a=s->state(s->context,object);
    if (action!=a->action_id)
    {
        mm_action_resource_release(a->action);
        a->action=NULL;
        a->action_id=-1; a->action=build_action(object,action,s);
    }
    if (a->action) { mm_action_resource_retain(a->action); a->action_id=action; }
    return a->action;
}
void *mm_text_fetch_model(void *object,MMI32 index,const MMTextGenerationServices *s)
{
    void *font=s->font(s->context,object),*model;
    if (!font) return NULL;
    model=s->glyph(s->context,font,s->character(s->context,object,index)); s->release_font(s->context,font); return model;
}
int mm_text_adjust_body(void *object,void *body,const MMTextGenerationServices *s)
{ MMTemplateState t; t=*s->template_state(s->context,object); return s->body_shape(s->context,body,&t.parents->view,&t.sets->view); }
int mm_text_default_costume(void *object,void *body,const MMTextGenerationServices *s)
{ s->body_material(s->context,body,0,s->state(s->context,object)->material); return 1; }
void mm_text_dispose(MMTextGenerated *a,const MMTextGenerationServices *s)
{
    if (a->material) { s->release(s->context,a->material); a->material=NULL; }
    mm_action_resource_release(a->action);
    a->action=NULL;
}
