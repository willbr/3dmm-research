/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "mm/text_resource.h"
#include "mm/font_resource.h"
#include <string.h>
static void release(const MMTextResourceServices *s,void *object)
{ s->generation.release(s->generation.context,object); }
static void normalize(void *object,const MMTextResourceServices *s)
{ if (!s->generation.length(s->generation.context,object)) s->space(s->context,s->text(s->context,object)); }
void *mm_text_create(const void *text,MMI32 shape,const MMTag *font,const MMTextResourceServices *s)
{
    void *object; MMTextDescriptor *d;
    object=s->create(s->context); if (!object) return NULL;
    s->assign(s->context,s->text(s->context,object),text); normalize(object,s);
    d=s->state(s->context,object); d->shape=shape; memcpy(&d->font,font,sizeof(*font));
    if (!mm_text_initialize_lists(object,&s->generation)) { release(s,object); return NULL; }
    return object;
}
void *mm_text_duplicate(void *object,const MMTextResourceServices *s)
{
    MMTextDescriptor *d; void *copy;
    d=s->state(s->context,object);
    copy=mm_text_create(s->text(s->context,object),d->shape,&d->font,s);
    if (copy) s->provider_copy(s->context,copy,object);
    return copy;
}
int mm_text_change(void *object,const void *text,MMI32 shape,const MMTag *font,const MMTextResourceServices *s)
{
    MMTextDescriptor saved,*d; void *old_text,*current;
    current=s->text(s->context,object); old_text=s->saved_text(s->context);
    s->assign(s->context,old_text,current); d=s->state(s->context,object); memcpy(&saved,d,sizeof(saved));
    if (text) s->assign(s->context,current,text);
    if (shape!=-1) d->shape=shape;
    if (font) memcpy(&d->font,font,sizeof(*font));
    normalize(object,s);
    if (mm_text_initialize_lists(object,&s->generation)) return 1;
    s->assign(s->context,current,old_text); d->shape=saved.shape; memcpy(&d->font,&saved.font,sizeof(d->font));
    return 0;
}
void mm_text_info(void *object,void *text,void *shape,void *font,const MMTextResourceServices *s)
{
    MMTextDescriptor *d;
    d=s->state(s->context,object);
    if (text) s->assign(s->context,text,s->text(s->context,object));
    if (shape) memcpy(shape,&d->shape,sizeof(d->shape));
    if (font) s->publish_tag(s->context,font,&d->font);
}
void *mm_text_font(void *object,const MMTextResourceServices *s)
{
    MMTextDescriptor *d;
    d=s->state(s->context,object);
    if (s->has_provider(s->context,object)) return s->provider_font(s->context,object,&d->font);
    return s->default_font(s->context,&d->font);
}
static MMU32 word(const unsigned char *p,int n,int big)
{ MMU32 v=0; int i; for (i=0;i<n;++i) v=(v<<8)|p[big?i:n-1-i]; return v; }
static void put(unsigned char *p,int n,MMU32 v,int big)
{ int i; for (i=0;i<n;++i) { p[big?n-1-i:i]=(unsigned char)(v&255U); v>>=8; } }
static void encode_descriptor(unsigned char *b, int big, MMU32 system, const MMTextDescriptor *d)
{
    put(b,2,1U,big); put(b+2,2,system,big); put(b+4,4,(MMU32)d->shape,big);
    put(b+8,4,(MMU32)d->font.sid,big); put(b+12,4,0U,big);
    put(b+16,4,d->font.type,big); put(b+20,4,d->font.number,big);
}
int mm_text_decode_descriptor(const unsigned char b[24], int big, MMTextDescriptor *d)
{
    MMU32 v;
    if (word(b,2,big)!=1U) big=!big;
    v=word(b+4,4,big); memcpy(&d->shape,&v,4);
    v=word(b+8,4,big); memcpy(&d->font.sid,&v,4); d->font.cache=NULL;
    d->font.type=word(b+16,4,big); d->font.number=word(b+20,4,big);
    return word(b,2,big)==1U;
}
static int descriptor(void *file,MMU32 type,MMU32 number,MMTextDescriptor *d,const MMTextResourceServices *s)
{
    MMU32 child; void *block; unsigned char b[24];
    if (!s->child(s->context,file,type,number,&child)) return 0;
    block=s->find(s->context,file,child);
    if (!block || !s->unpack(s->context,block) || s->block_size(s->context,block)<24 || !s->read(s->context,block,b,24)) return 0;
    if (!mm_text_decode_descriptor(b,s->host_big_endian,d) && s->bad_order) s->bad_order(s->context);
    return 1;
}
int mm_text_read(void *object,void *source,MMU32 type,MMU32 number,void *block,const MMTextResourceServices *s)
{
    MMTextDescriptor read,*d;
    if (!s->read_template(s->context,object,source,type,number,block) || !descriptor(source,type,number,&read,s))
        return 0;
    d=s->state(s->context,object); memcpy(&d->font,&read.font,sizeof(d->font)); d->shape=read.shape;
    return mm_text_initialize_lists(object,&s->generation);
}
int mm_text_write(void *object,void *file,MMU32 type,void *number_out,const MMTextResourceServices *s)
{
    unsigned char b[MM_TEXT_DESCRIPTOR_BYTES]; MMU32 number,child; void *block;
    if (!s->write_template(s->context,object,file,type,number_out)) return 0;
    memcpy(&number,number_out,4);
    encode_descriptor(b,s->host_big_endian,s->system,s->state(s->context,object));
    block=s->add(s->context,file,type,number,&child); if (!block) return 0;
    if (s->write(s->context,block,b,sizeof(b))) return 1;
    s->delete_child(s->context,file,type,number,child); return 0;
}
int mm_text_write_draft(const void *name, const MMTextDescriptor *d, MMU32 *number, const MMTextDraftServices *s)
{
    MMTemplateState state = {0};
    unsigned char header[MM_TEMPLATE_HEADER_BYTES], descriptor[MM_TEXT_DESCRIPTOR_BYTES];
    MMU32 parent = 0;
    if (d->shape < 0 || d->shape >= MM_TEXT_SHAPE_COUNT || d->font.type != MM_FONT_TYPE)
        return 0;
    state.flags = MM_TEMPLATE_TEXT;
    mm_template_encode_header(header, s->big_endian, s->system, &state);
    encode_descriptor(descriptor, s->big_endian, s->system, d);
    if (!s->add_chunk(s->context, MM_TMPL, header, sizeof(header), &parent))
        return 0;
    if (!s->set_name(s->context, MM_TMPL, parent, name) ||
        !s->add_child(s->context, MM_TMPL, parent, 0, MM_TDT, descriptor, sizeof(descriptor)))
    {
        s->delete_chunk(s->context, MM_TMPL, parent);
        return 0;
    }
    *number = parent;
    return 1;
}
int mm_text_bind_preview_font(const MMChunkAccess *chunks, MMU32 tmpl, MMU32 font, const MMTextPreviewFontServices *s)
{
    unsigned char bytes[MM_TEXT_DESCRIPTOR_BYTES];
    MMU32 descriptor, copied;
    int big;
    if (mm_chunk_link_number(chunks, MM_TMPL, tmpl, 0, MM_TDT, &descriptor) != MM_CHUNK_OK ||
        !s->read(s->context, descriptor, bytes))
        return 0;
    big = bytes[0] == 0;
    if (word(bytes, 2, big) != 1 || word(bytes + 16, 4, big) != MM_FONT_TYPE ||
        !s->copy_font(s->context, font, &copied))
        return 0;
    put(bytes + 8, 4, 0, big);
    put(bytes + 20, 4, copied, big);
    return s->write(s->context, descriptor, bytes);
}
void *mm_text_tags(void *file,MMU32 type,MMU32 number,void *error_out,const MMTextResourceServices *s)
{
    void *list; MMTextDescriptor d;
    s->publish_error(s->context,error_out,0); list=s->tag_list(s->context);
    if (list && descriptor(file,type,number,&d,s) && s->append_tag(s->context,list,&d.font)) return list;
    s->publish_error(s->context,error_out,1); if (list) release(s,list); return NULL;
}
int mm_text_set_names(MMTextNames *names,void *table,const MMTextResourceServices *s)
{
    names->table=table; s->generation.retain(s->generation.context,table); return 1;
}
int mm_text_action_name(const MMTextNames *names,MMI32 id,void *text,const MMTextResourceServices *s)
{
    MMI32 i;
    if (names->table)
    {
        for (i=0;i<s->name_count(s->context,names->table);++i)
            if (s->name_id(s->context,names->table,i)==id) { s->name_text(s->context,names->table,i,text); return 1; }
        if (s->missing_name) s->missing_name(s->context);
    }
    s->clear(s->context,text); return 0;
}
