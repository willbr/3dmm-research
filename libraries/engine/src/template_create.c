/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "three_mm/template_create.h"
static void diagnostic(const ThreeMMTemplateCreateServices *s,int n) { if (s->diagnostic) s->diagnostic(s->context,n); }
static ThreeMMU32 word(const unsigned char *p,int n,int big)
{ ThreeMMU32 v=0; int i; for (i=0;i<n;++i) v=(v<<8)|p[big?i:n-1-i]; return v; }
static void put(unsigned char *p,int n,ThreeMMU32 value,int big)
{ int i; for (i=0;i<n;++i) { p[big?n-1-i:i]=(unsigned char)(value&255U); value>>=8; } }
static void swap(unsigned char *p,ThreeMMI32 count,int stride)
{ ThreeMMI32 i; int j; unsigned char t; for (i=0;i<count;++i,p+=stride) for (j=0;j<stride/2;++j) { t=p[j]; p[j]=p[stride-1-j]; p[stride-1-j]=t; } }
int three_mm_template_read_header(void *object,void *file,ThreeMMU32 type,ThreeMMU32 number,const ThreeMMTemplateCreateServices *s)
{
    unsigned char bytes[16]; int big; ThreeMMTemplateState t; void *block;
    block=s->find(s->context,file,type,number);
    if (!block || !s->unpack(s->context,block) || s->block_size(s->context,block,0)<16 ||
        !s->block_read(s->context,block,bytes,sizeof(bytes))) return 0;
    if (bytes[0]==1 && !bytes[1]) big=0;
    else if (!bytes[0] && bytes[1]==1) big=1;
    else { diagnostic(s,THREE_MM_TEMPLATE_BAD_ORDER); big=s->host_big_endian; }
    s->read(s->context,object,&t); t.x=word(bytes+4,2,big); t.y=word(bytes+6,2,big);
    t.z=word(bytes+8,2,big); t.flags=word(bytes+12,4,big); s->write(s->context,object,&t);
    return s->read_name(s->context,object,file,type,number);
}
int three_mm_template_write_header(void *object,void *file,ThreeMMU32 type,ThreeMMU32 *number,const ThreeMMTemplateCreateServices *s)
{
    ThreeMMTemplateState t; unsigned char bytes[16]; int big=s->host_big_endian;
    s->read(s->context,object,&t); put(bytes,2,1,big); put(bytes+2,2,s->system,big);
    put(bytes+4,2,t.x,big); put(bytes+6,2,t.y,big); put(bytes+8,2,t.z,big);
    bytes[10]=bytes[11]=0; put(bytes+12,4,t.flags,big);
    if (!s->add_chunk(s->context,file,type,bytes,sizeof(bytes),number)) return 0;
    if (!s->set_name(s->context,object,file,THREE_MM_TMPL,*number))
    { s->delete_chunk(s->context,file,type,*number); return 0; }
    return 1;
}
int three_mm_template_fetch(void *file,ThreeMMU32 type,ThreeMMU32 number,void *block,void **out,ThreeMMI32 *bytes,const ThreeMMTemplateCreateServices *s)
{
    void *object; ThreeMMTemplateChild child; ThreeMMTemplateState t; int text;
    *bytes=s->block_size(s->context,block,1); if (!out) return 1;
    text=s->child(s->context,file,type,number,0,THREE_MM_TDT,&child);
    object=s->create(s->context,text);
    if (!object || !s->initialize(s->context,object,file,type,number))
    {
        if (object) s->release(s->context,object);
        s->invalidate(s->context,out,sizeof(*out)); s->invalidate(s->context,bytes,sizeof(*bytes)); return 0;
    }
    *out=object; s->read(s->context,object,&t);
    if (t.flags&2U) *bytes=s->object_size(s->context,1);
    else *bytes=s->object_size(s->context,0)+s->table_size(s->context,t.parents)+
        s->table_size(s->context,t.sets)+s->table_size(s->context,t.costumes);
    return 1;
}
int three_mm_template_initialize(void *object,void *file,ThreeMMU32 type,ThreeMMU32 number,const ThreeMMTemplateCreateServices *s)
{
    ThreeMMTemplateState t; ThreeMMTemplateChild child; ThreeMMI32 i,j,k,count,set,ids[50];
    void *block,*cache,*custom; int other;
    if (!s->header(s->context,object,file,type,number)) return 0;
    s->read(s->context,object,&t);
    if (!s->child(s->context,file,type,number,0,THREE_MM_GLPI,&child) ||
        !(block=s->find(s->context,file,child.type,child.number))) return 0;
    t.parents=s->read_table(s->context,block,THREE_MM_TEMPLATE_PARENTS,&other); s->write(s->context,object,&t);
    if (!t.parents) return 0;
    s->validate_table(s->context,t.parents,THREE_MM_TEMPLATE_PARENTS,0);
    if (other) swap(s->list_data(s->context,t.parents),s->count(s->context,t.parents),2);
    if (!s->child(s->context,file,type,number,0,THREE_MM_GLBS,&child) ||
        !(block=s->find(s->context,file,child.type,child.number))) return 0;
    t.sets=s->read_table(s->context,block,THREE_MM_TEMPLATE_SETS,&other); s->write(s->context,object,&t);
    if (!t.sets) return 0;
    s->validate_table(s->context,t.sets,THREE_MM_TEMPLATE_SETS,0);
    if (other) swap(s->list_data(s->context,t.sets),s->count(s->context,t.sets),2);
    if (s->debug_checks && s->child(s->context,file,type,number,0,THREE_MM_GLDC,&child)) diagnostic(s,THREE_MM_TEMPLATE_OBSOLETE);
    t.set_count=-1; s->write(s->context,object,&t);
    for (i=0;i<s->count(s->context,t.sets);++i)
    { set=s->item(s->context,t.sets,i); if (set>t.set_count) { t.set_count=set; s->write(s->context,object,&t); } }
    ++t.set_count; t.custom_count=0; s->write(s->context,object,&t);
    while (s->child(s->context,file,type,number,(ThreeMMU32)t.custom_count,THREE_MM_CMTL,&child))
    { ++t.custom_count; s->write(s->context,object,&t); }
    t.action_count=0; s->write(s->context,object,&t);
    while (s->child(s->context,file,type,number,(ThreeMMU32)t.action_count,THREE_MM_ACTN,&child))
    { ++t.action_count; s->write(s->context,object,&t); }
    if (s->child(s->context,file,type,number,0,THREE_MM_GGCM,&child))
    {
        block=s->find(s->context,file,child.type,child.number); if (!block) return 0;
        t.costumes=s->read_table(s->context,block,THREE_MM_TEMPLATE_COSTUMES,&other); s->write(s->context,object,&t);
        if (!t.costumes) return 0;
        s->validate_table(s->context,t.costumes,THREE_MM_TEMPLATE_COSTUMES,t.set_count);
        if (other) for (i=0;i<t.set_count;++i)
        {
            swap(s->fixed_data(s->context,t.costumes,i),1,4);
            count=(ThreeMMI32)word(s->fixed_data(s->context,t.costumes,i),4,s->host_big_endian);
            swap(s->variable_data(s->context,t.costumes,i),count,4);
        }
        return 1;
    }
    diagnostic(s,THREE_MM_TEMPLATE_BUILD_COSTUMES);
    cache=s->create_cache(s->context,file); if (!cache) return 0;
    t.costumes=s->create_costumes(s->context); s->write(s->context,object,&t);
    if (!t.costumes) { s->release(s->context,cache); return 0; }
    for (i=0;i<t.set_count;++i)
    {
        count=0; k=0;
        while (s->enumerate(s->context,file,type,number,k++,&child))
        {
            if (child.type!=THREE_MM_CMTL) continue;
            custom=s->fetch_custom(s->context,cache,child.type,child.number);
            if (!custom) { s->release(s->context,cache); return 0; }
            j=s->custom_set(s->context,custom);
            if (j==i)
            {
                if (count==50)
                { s->release(s->context,custom); diagnostic(s,THREE_MM_TEMPLATE_COSTUME_LIMIT); s->release(s->context,cache); return 0; }
                ids[count++]=(ThreeMMI32)child.id;
            }
            s->release(s->context,custom);
        }
        if (!s->append_costumes(s->context,t.costumes,ids,count)) { s->release(s->context,cache); return 0; }
    }
    s->release(s->context,cache); return 1;
}
void three_mm_template_dispose(void *object,const ThreeMMTemplateCreateServices *s)
{
    ThreeMMTemplateState t; s->read(s->context,object,&t);
    if (t.parents) { s->release(s->context,t.parents); } t.parents=NULL; s->write(s->context,object,&t);
    if (t.sets) { s->release(s->context,t.sets); } t.sets=NULL; s->write(s->context,object,&t);
    if (t.costumes) { s->release(s->context,t.costumes); } t.costumes=NULL; s->write(s->context,object,&t);
}
void *three_mm_template_create_body(void *object,const ThreeMMTemplateCreateServices *s)
{
    ThreeMMTemplateState t; void *body; s->read(s->context,object,&t);
    body=s->create_body(s->context,t.parents,t.sets);
    if (!body || !s->default_costume(s->context,object,body)) { if (body) s->release(s->context,body); return NULL; }
    return body;
}
int three_mm_template_default_costume(void *object,void *body,const ThreeMMTemplateCreateServices *s)
{
    ThreeMMTemplateState t; ThreeMMI32 i,id; void *storage=NULL,**refs; int ok=0;
    s->read(s->context,object,&t);
    if (!s->allocate_refs(s->context,t.set_count,&storage)) return 0;
    refs=storage;
    for (i=0;i<t.set_count;++i)
    {
        id=s->costume_id(s->context,object,i,0); refs[i]=s->custom(s->context,object,id); if (!refs[i]) goto end;
        if (s->custom_set(s->context,refs[i])!=i) diagnostic(s,THREE_MM_TEMPLATE_WRONG_SET);
    }
    for (i=0;i<t.set_count;++i) { s->apply_custom(s->context,body,refs[i]); } ok=1;
end:
    for (i=0;i<t.set_count;++i) if (refs[i]) { s->release(s->context,refs[i]); refs[i]=NULL; }
    s->free_memory(s->context,&storage); return ok;
}
