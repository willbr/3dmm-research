/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "three_mm/text_resource.h"
#include <string.h>
static void release(const ThreeMMTextResourceServices *s,void *object)
{ s->generation.release(s->generation.context,object); }
static void normalize(void *object,const ThreeMMTextResourceServices *s)
{ if (!s->generation.length(s->generation.context,object)) s->space(s->context,s->text(s->context,object)); }
void *three_mm_text_create(const void *text,ThreeMMI32 shape,const ThreeMMTag *font,const ThreeMMTextResourceServices *s)
{
    void *object; ThreeMMTextDescriptor *d;
    object=s->create(s->context); if (!object) return NULL;
    s->assign(s->context,s->text(s->context,object),text); normalize(object,s);
    d=s->state(s->context,object); d->shape=shape; memcpy(&d->font,font,sizeof(*font));
    if (!three_mm_text_initialize_lists(object,&s->generation)) { release(s,object); return NULL; }
    return object;
}
void *three_mm_text_duplicate(void *object,const ThreeMMTextResourceServices *s)
{
    ThreeMMTextDescriptor *d; void *copy;
    d=s->state(s->context,object);
    copy=three_mm_text_create(s->text(s->context,object),d->shape,&d->font,s);
    if (copy) s->provider_copy(s->context,copy,object);
    return copy;
}
int three_mm_text_change(void *object,const void *text,ThreeMMI32 shape,const ThreeMMTag *font,const ThreeMMTextResourceServices *s)
{
    ThreeMMTextDescriptor saved,*d; void *old_text,*current;
    current=s->text(s->context,object); old_text=s->saved_text(s->context);
    s->assign(s->context,old_text,current); d=s->state(s->context,object); memcpy(&saved,d,sizeof(saved));
    if (text) s->assign(s->context,current,text);
    if (shape!=-1) d->shape=shape;
    if (font) memcpy(&d->font,font,sizeof(*font));
    normalize(object,s);
    if (three_mm_text_initialize_lists(object,&s->generation)) return 1;
    s->assign(s->context,current,old_text); d->shape=saved.shape; memcpy(&d->font,&saved.font,sizeof(d->font));
    return 0;
}
void three_mm_text_info(void *object,void *text,void *shape,void *font,const ThreeMMTextResourceServices *s)
{
    ThreeMMTextDescriptor *d;
    d=s->state(s->context,object);
    if (text) s->assign(s->context,text,s->text(s->context,object));
    if (shape) memcpy(shape,&d->shape,sizeof(d->shape));
    if (font) s->publish_tag(s->context,font,&d->font);
}
void *three_mm_text_font(void *object,const ThreeMMTextResourceServices *s)
{
    ThreeMMTextDescriptor *d;
    d=s->state(s->context,object);
    if (s->has_provider(s->context,object)) return s->provider_font(s->context,object,&d->font);
    return s->default_font(s->context,&d->font);
}
static ThreeMMU32 word(const unsigned char *p,int n,int big)
{ ThreeMMU32 v=0; int i; for (i=0;i<n;++i) v=(v<<8)|p[big?i:n-1-i]; return v; }
static void put(unsigned char *p,int n,ThreeMMU32 v,int big)
{ int i; for (i=0;i<n;++i) { p[big?n-1-i:i]=(unsigned char)(v&255U); v>>=8; } }
static int descriptor(void *file,ThreeMMU32 type,ThreeMMU32 number,ThreeMMTextDescriptor *d,const ThreeMMTextResourceServices *s)
{
    ThreeMMU32 child,v; void *block; unsigned char b[24]; int big;
    if (!s->child(s->context,file,type,number,&child)) return 0;
    block=s->find(s->context,file,child);
    if (!block || !s->unpack(s->context,block) || s->block_size(s->context,block)<24 || !s->read(s->context,block,b,24)) return 0;
    big=s->host_big_endian; if (word(b,2,big)!=1U) big=!big;
    if (word(b,2,big)!=1U && s->bad_order) s->bad_order(s->context);
    v=word(b+4,4,big); memcpy(&d->shape,&v,4);
    v=word(b+8,4,big); memcpy(&d->font.sid,&v,4); d->font.cache=NULL;
    d->font.type=word(b+16,4,big); d->font.number=word(b+20,4,big);
    return 1;
}
int three_mm_text_read(void *object,void *file,ThreeMMU32 type,ThreeMMU32 number,const ThreeMMTextResourceServices *s)
{
    ThreeMMTextDescriptor read,*d;
    if (!s->read_template(s->context,object,file,type,number) || !descriptor(file,type,number,&read,s)) return 0;
    d=s->state(s->context,object); memcpy(&d->font,&read.font,sizeof(d->font)); d->shape=read.shape;
    return three_mm_text_initialize_lists(object,&s->generation);
}
int three_mm_text_write(void *object,void *file,ThreeMMU32 type,void *number_out,const ThreeMMTextResourceServices *s)
{
    ThreeMMTextDescriptor *d; unsigned char b[24]; ThreeMMU32 number,child; void *block; int big;
    if (!s->write_template(s->context,object,file,type,number_out)) return 0;
    memcpy(&number,number_out,4); d=s->state(s->context,object); big=s->host_big_endian;
    put(b,2,1U,big); put(b+2,2,s->system,big); put(b+4,4,(ThreeMMU32)d->shape,big);
    put(b+8,4,(ThreeMMU32)d->font.sid,big); put(b+12,4,0U,big); put(b+16,4,d->font.type,big); put(b+20,4,d->font.number,big);
    block=s->add(s->context,file,type,number,&child); if (!block) return 0;
    if (s->write(s->context,block,b,24)) return 1;
    s->delete_child(s->context,file,type,number,child); return 0;
}
void *three_mm_text_tags(void *file,ThreeMMU32 type,ThreeMMU32 number,void *error_out,const ThreeMMTextResourceServices *s)
{
    void *list; ThreeMMTextDescriptor d;
    s->publish_error(s->context,error_out,0); list=s->tag_list(s->context);
    if (list && descriptor(file,type,number,&d,s) && s->append_tag(s->context,list,&d.font)) return list;
    s->publish_error(s->context,error_out,1); if (list) release(s,list); return NULL;
}
int three_mm_text_set_names(ThreeMMTextNames *names,void *table,const ThreeMMTextResourceServices *s)
{
    names->table=table; s->generation.retain(s->generation.context,table); return 1;
}
int three_mm_text_action_name(const ThreeMMTextNames *names,ThreeMMI32 id,void *text,const ThreeMMTextResourceServices *s)
{
    ThreeMMI32 i;
    if (names->table)
    {
        for (i=0;i<s->name_count(s->context,names->table);++i)
            if (s->name_id(s->context,names->table,i)==id) { s->name_text(s->context,names->table,i,text); return 1; }
        if (s->missing_name) s->missing_name(s->context);
    }
    s->clear(s->context,text); return 0;
}
