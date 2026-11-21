/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "mm/text_resource.h"
#include "mm/font_resource.h"
#include "mm/text.h"
#include <string.h>

int mm_text_author_name(const char *utf8, char name[MM_TEXT_NAME_BYTES])
{
    unsigned short units[MM_TEXT_NAME_BYTES - 1];
    char ansi[MM_TEXT_NAME_BYTES - 1];
    size_t count, bytes, i;
    int result, visible = 0, single_line = 1;
    if (!utf8 || !*utf8)
        return MM_TEXT_NAME_EMPTY;
    result = mm_text_read_utf8(utf8, strlen(utf8), units, MM_TEXT_NAME_BYTES - 1, &count);
    if (!result)
        return MM_TEXT_NAME_UTF8;
    if (result < 0)
        return MM_TEXT_NAME_LENGTH;
    for (i = 0; i < count; ++i)
    {
        if (units[i] == '\r' || units[i] == '\n' || units[i] == '\t')
            single_line = 0;
        else if (units[i] != ' ')
            visible = 1;
    }
    if (!visible)
        return MM_TEXT_NAME_LENGTH;
    bytes = mm_text_ansi_from_units(NULL, units, count, ansi, sizeof(ansi), 1);
    if (!single_line || !bytes)
        return MM_TEXT_NAME_CHARACTERS;
    memcpy(name, ansi, bytes);
    name[bytes] = '\0';
    return MM_TEXT_NAME_OK;
}

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
static int descriptor(const MMTextResourceSource *source, MMU32 type, MMU32 number,
                      MMTextDescriptor *d, const MMTextResourceServices *s)
{
    const MMChunkPayloadServices *payloads = s->payloads;
    const MMChunkAccess chunks = source->cache ? mm_resource_cache_chunks(source->cache) : source->chunks;
    MMOwnedBlock block;
    MMBlockIO io;
    MMU32 child;
    unsigned char bytes[MM_TEXT_DESCRIPTOR_BYTES];
    int found, ok;
    if (mm_chunk_link_number(&chunks, type, number, 0, MM_TDT, &child) != MM_CHUNK_OK ||
        mm_owned_block_init(&block, payloads->handles, payloads->files) != MM_DATA_OK)
        return 0;
    found = source->cache ? mm_resource_cache_read_block(source->cache, MM_TDT, child, &block) == MM_CHUNK_OK :
                           mm_chunk_payload_bind_block(payloads, source->archive, MM_TDT, child, &block) ==
                               MM_CHUNK_PAYLOAD_OK;
    ok = found && mm_owned_block_code(&block, 0, payloads->decode_context, payloads->decode, NULL) == MM_DATA_OK &&
         mm_owned_block_io(&block, &io) == MM_DATA_OK && mm_block_read(&io, 0, bytes, sizeof(bytes), 0) == MM_DATA_OK;
    if (ok && !mm_text_decode_descriptor(bytes, s->host_big_endian, d) && s->bad_order)
        s->bad_order(s->context);
    mm_owned_block_clear(&block);
    return ok;
}
int mm_text_read(void *object, const MMTextResourceSource *source, MMU32 type, MMU32 number, MMOwnedBlock *block,
                 const MMTextResourceServices *s)
{
    MMTextDescriptor read,*d;
    if (!s->read_template(s->context,object,type,number,block) || !descriptor(source,type,number,&read,s))
        return 0;
    d=s->state(s->context,object); memcpy(&d->font,&read.font,sizeof(d->font)); d->shape=read.shape;
    return mm_text_initialize_lists(object,&s->generation);
}
static int adopt_descriptor(const MMChunkEdit *edit, MMU32 type, MMU32 parent, const MMChunkChild *child)
{
    if (mm_chunk_adopt(edit, type, parent, child, 1) == MM_CHUNK_OK)
        return 1;
    mm_chunk_delete(edit, child->type, child->number);
    return 0;
}

int mm_text_write(const MMTemplateState *state, const MMTextDescriptor *descriptor, const MMChunkNameValue *name,
                  MMU32 type, MMU32 *number, const MMTemplateWriter *writer)
{
    const MMChunkPayloadServices *payloads = writer->payloads;
    const MMChunkEdit *edit = &writer->edit;
    unsigned char bytes[MM_TEXT_DESCRIPTOR_BYTES];
    MMOwnedBlock block;
    MMBlockIO io;
    MMChunkChild child = {MM_TDT, 0, 0};
    int ok = 0;
    if (!mm_template_write_header(state, name, type, number, writer) ||
        mm_owned_block_init(&block, payloads->handles, payloads->files) != MM_DATA_OK)
        return 0;
    encode_descriptor(bytes, writer->big_endian, writer->system, descriptor);
    if (mm_chunk_add_block(edit, child.type, sizeof(bytes), &child.number, &block) == MM_CHUNK_OK &&
        adopt_descriptor(edit, type, *number, &child))
    {
        ok = mm_owned_block_io(&block, &io) == MM_DATA_OK &&
             mm_block_write(&io, 0, bytes, sizeof(bytes), 0) == MM_DATA_OK;
        if (!ok)
            mm_chunk_unlink(edit, type, *number, &child);
    }
    mm_owned_block_clear(&block);
    return ok;
}
int mm_text_write_draft(const MMChunkNameValue *name, const MMTextDescriptor *d, MMU32 *number,
                        const MMTemplateWriter *writer)
{
    MMTemplateState state = {0};
    unsigned char descriptor[MM_TEXT_DESCRIPTOR_BYTES];
    const MMChunkEdit *edit = &writer->edit;
    MMChunkChild child = {MM_TDT, 0, 0};
    MMU32 parent = 0;
    if (d->shape < 0 || d->shape >= MM_TEXT_SHAPE_COUNT || d->font.type != MM_FONT_TYPE)
        return 0;
    state.flags = MM_TEMPLATE_TEXT;
    encode_descriptor(descriptor, writer->big_endian, writer->system, d);
    if (!mm_template_write_header(&state, name, MM_TMPL, &parent, writer))
        return 0;
    if (mm_chunk_add_bytes(edit, writer->payloads->files, child.type, descriptor, sizeof(descriptor),
                           &child.number) != MM_CHUNK_OK || !adopt_descriptor(edit, MM_TMPL, parent, &child))
    {
        mm_chunk_delete(edit, MM_TMPL, parent);
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
void *mm_text_tags(const MMTextResourceSource *source, MMU32 type, MMU32 number, void *error_out,
                   const MMTextResourceServices *s)
{
    void *list; MMTextDescriptor d;
    s->publish_error(s->context,error_out,0); list=s->tag_list(s->context);
    if (list && descriptor(source,type,number,&d,s) && s->append_tag(s->context,list,&d.font)) return list;
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
