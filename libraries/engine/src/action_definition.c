/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "three_mm/action_definition.h"
#include <string.h>
static ThreeMMU32 word(const unsigned char *p,int big)
{
    ThreeMMU32 value=0; int i;
    for (i=0;i<4;++i) value=(value<<8)|p[big?i:3-i];
    return value;
}
static void swap(unsigned char *p,ThreeMMI32 count,int width)
{
    ThreeMMI32 i; int j; unsigned char t;
    for (i=0;i<count;++i,p+=width) for (j=0;j<width/2;++j)
    { t=p[j]; p[j]=p[width-1-j]; p[width-1-j]=t; }
}
void *three_mm_action_create(void *cels,void *matrices,ThreeMMU32 flags,const ThreeMMActionDefinitionServices *s)
{
    void *object=s->create(s->context); ThreeMMActionDefinition *a;
    if (!object) return NULL;
    a=s->state(s->context,object); a->flags=flags;
    a->cels=s->duplicate(s->context,cels,THREE_MM_ACTION_CELS);
    if (a->cels)
    {
        a->matrices=s->duplicate(s->context,matrices,THREE_MM_ACTION_MATRICES);
        if (a->matrices) return object;
    }
    s->release(s->context,object); return NULL;
}
int three_mm_action_fetch(void *file,ThreeMMU32 type,ThreeMMU32 number,void *block,void *out,void *size,const ThreeMMActionDefinitionServices *s)
{
    ThreeMMI32 bytes; void *object; ThreeMMActionDefinition *a;
    bytes=s->block_size(s->context,block,1); memcpy(size,&bytes,sizeof(bytes));
    if (!out) return 1;
    if (!s->unpack(s->context,block)) goto fail;
    bytes=s->block_size(s->context,block,0); memcpy(size,&bytes,sizeof(bytes));
    object=s->create(s->context);
    if (!object || !s->initialize(s->context,object,file,type,number))
    { if (object) s->release(s->context,object); goto fail; }
    memcpy(out,&object,sizeof(object)); a=s->state(s->context,object);
    bytes=s->object_size(s->context)+s->table_size(s->context,a->cels)+s->table_size(s->context,a->matrices);
    memcpy(size,&bytes,sizeof(bytes)); return 1;
fail:
    s->invalidate(s->context,out,sizeof(object)); s->invalidate(s->context,size,sizeof(bytes)); return 0;
}
static void *table(void *file,ThreeMMU32 type,ThreeMMU32 number,ThreeMMU32 child_type,int kind,int *other,const ThreeMMActionDefinitionServices *s,int *found)
{
    ThreeMMU32 child; void *block; *found=s->child(s->context,file,type,number,child_type,&child);
    if (!*found) return NULL;
    block=s->find(s->context,file,child_type,child); if (!block) return NULL;
    return s->read_table(s->context,block,kind,other);
}
int three_mm_action_initialize(ThreeMMActionDefinition *a,void *file,ThreeMMU32 type,ThreeMMU32 number,const ThreeMMActionDefinitionServices *s)
{
    unsigned char header[8],*p; void *block,*wire; int big,other,found; ThreeMMI32 i,count; ThreeMMTag tag; ThreeMMU32 sid;
    block=s->find(s->context,file,type,number);
    if (!block || !s->unpack(s->context,block) || s->block_size(s->context,block,0)<8 || !s->block_read(s->context,block,header,8)) return 0;
    if (header[0]==1 && !header[1]) big=0;
    else if (!header[0] && header[1]==1) big=1;
    else { if (s->bad_order) s->bad_order(s->context); big=s->host_big_endian; }
    a->flags=word(header+4,big);
    /* Missing child/find retains the old slot, just as the native early return.
     * A failed reader publishes NULL. Fresh owners are the supported input. */
    if (!s->child(s->context,file,type,number,THREE_MM_ACTION_GGCL,&sid)) return 0;
    block=s->find(s->context,file,THREE_MM_ACTION_GGCL,sid); if (!block) return 0;
    a->cels=s->read_table(s->context,block,THREE_MM_ACTION_CELS,&other); if (!a->cels) return 0;
    if (other) for (i=0;i<s->count(s->context,a->cels);++i)
    {
        swap(s->fixed(s->context,a->cels,i),2,4);
        swap(s->variable(s->context,a->cels,i),s->variable_size(s->context,a->cels,i)/2,2);
    }
    if (!s->child(s->context,file,type,number,THREE_MM_ACTION_GLXF,&sid)) return 0;
    block=s->find(s->context,file,THREE_MM_ACTION_GLXF,sid); if (!block) return 0;
    a->matrices=s->read_table(s->context,block,THREE_MM_ACTION_MATRICES,&other); if (!a->matrices) return 0;
    count=s->count(s->context,a->matrices);
    if (other && count) swap(s->data(s->context,a->matrices,0),count*12,4);
    wire=table(file,type,number,THREE_MM_ACTION_GLMS,THREE_MM_ACTION_WIRE_SOUNDS,&other,s,&found);
    if (!found) return 1;
    if (!wire) return 0;
    s->validate_wire_sounds(s->context,wire); count=s->count(s->context,wire);
    a->sounds=s->create_sounds(s->context,count);
    if (!a->sounds) { s->release(s->context,wire); return 0; }
    big=s->host_big_endian!=other;
    for (i=0;i<count;++i)
    {
        p=s->data(s->context,wire,i); sid=word(p,big); memcpy(&tag.sid,&sid,sizeof(sid));
        tag.cache=NULL; tag.type=word(p+8,big); tag.number=word(p+12,big);
        if (!s->append_sound(s->context,a->sounds,&tag)) { s->release(s->context,wire); return 0; }
    }
    s->release(s->context,wire); return 1;
}
void three_mm_action_dispose(ThreeMMActionDefinition *a,const ThreeMMActionDefinitionServices *s)
{
    if (a->cels) { s->release(s->context,a->cels); a->cels=NULL; }
    if (a->matrices) { s->release(s->context,a->matrices); a->matrices=NULL; }
    if (a->sounds) { s->release(s->context,a->sounds); a->sounds=NULL; }
}
ThreeMMI32 three_mm_action_cel_count(const ThreeMMActionDefinition *a,const ThreeMMActionDefinitionServices *s)
{ return s->count(s->context,a->cels); }
void three_mm_action_get_cel(const ThreeMMActionDefinition *a,ThreeMMI32 i,void *out,const ThreeMMActionDefinitionServices *s)
{ memcpy(out,s->fixed(s->context,a->cels,i),8); }
void three_mm_action_get_part(const ThreeMMActionDefinition *a,ThreeMMI32 cel,ThreeMMI32 part,void *out,const ThreeMMActionDefinitionServices *s)
{ memcpy(out,s->variable(s->context,a->cels,cel)+part*4,4); }
void three_mm_action_get_matrix(const ThreeMMActionDefinition *a,ThreeMMI32 i,void *out,const ThreeMMActionDefinitionServices *s)
{ memcpy(out,s->data(s->context,a->matrices,i),48); }
void three_mm_action_get_sound(const ThreeMMActionDefinition *a,ThreeMMI32 cel,void *out,const ThreeMMActionDefinitionServices *s)
{
    ThreeMMU32 id; ThreeMMTag tag; s->clear_sound_type(s->context,out);
    if (!a->sounds) return;
    memcpy(&id,s->fixed(s->context,a->cels,cel),sizeof(id));
    if (id<0x80000000UL) { s->read_sound(s->context,a->sounds,(ThreeMMI32)id,&tag); s->publish_sound(s->context,out,&tag); }
}
