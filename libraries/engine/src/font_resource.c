/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "three_mm/font_resource.h"
#include <string.h>
static ThreeMMU32 word(const unsigned char *p,int big)
{ ThreeMMU32 value=0; int i; for (i=0;i<4;++i) value=(value<<8)|p[big?i:3-i]; return value; }
static ThreeMMI32 signed_word(const unsigned char *p,int big)
{ ThreeMMU32 bits=word(p,big); ThreeMMI32 value; memcpy(&value,&bits,4); return value; }
static void put(unsigned char *p,int bytes,ThreeMMU32 value,int big)
{ int i; for (i=0;i<bytes;++i) { p[big?bytes-1-i:i]=(unsigned char)value; value>>=8; } }
int three_mm_font_fetch(void *block,void *out,void *size,const ThreeMMFontResourceServices *s)
{
    ThreeMMI32 bytes,base; void *object; ThreeMMFontResource *font;
    bytes=s->packed(s->context,block)?s->object_size(s->context)+2048:s->block_size(s->context,block);
    memcpy(size,&bytes,4); if (!out) return 1;
    object=s->create(s->context);
    if (!object || !s->initialize(s->context,object,block)) goto fail;
    font=s->state(s->context,object); base=s->object_size(s->context);
    if (base<0 || font->count>(0x7fffffffL-base)/8) { s->bad_font(s->context); goto fail; }
    bytes=base+font->count*8;
    memcpy(size,&bytes,4); memcpy(out,&object,sizeof(object)); return 1;
fail:
    s->invalidate(s->context,out,sizeof(object)); s->invalidate(s->context,size,4);
    if (object) s->release(s->context,object); return 0;
}
int three_mm_font_initialize(ThreeMMFontResource *font,void *block,const ThreeMMFontResourceServices *s)
{
    unsigned char header[12]; ThreeMMI32 bytes; int big,current;
    if (!s->unpack(s->context,block)) return 0;
    if (s->block_size(s->context,block)<12) { s->bad_font(s->context); return 0; }
    if (!s->read(s->context,block,header,12,0)) return 0;
    current=s->host_big_endian?(!header[0] && header[1]==1):(header[0]==1 && !header[1]);
    big=current?s->host_big_endian:!s->host_big_endian;
    if ((!big && (header[0]!=1 || header[1])) || (big && (header[0] || header[1]!=1)))
        if (s->bad_order) s->bad_order(s->context);
    font->count=signed_word(header+4,big);
    if (font->count<0 || font->count>0x0ffffffeL) { s->bad_font(s->context); return 0; }
    bytes=font->count*4;
    if (s->block_size(s->context,block)!=12+bytes+bytes) { s->bad_font(s->context); return 0; }
    font->maximum_height=signed_word(header+8,big);
    if (!s->allocate(s->context,bytes,0,&font->widths) || !s->read(s->context,block,font->widths,bytes,12)) return 0;
    /* The old code tested the already normalized bo here, so foreign arrays
     * were NOT swapped. Preserve that defined legacy result explicitly. */
    if (!s->allocate(s->context,bytes,0,&font->heights) || !s->read(s->context,block,font->heights,bytes,12+bytes)) return 0;
    return 1;
}
void three_mm_font_dispose(ThreeMMFontResource *font,const ThreeMMFontResourceServices *s)
{ s->free_memory(s->context,&font->widths); s->free_memory(s->context,&font->heights); }
ThreeMMI32 three_mm_font_width(const ThreeMMFontResource *font,ThreeMMI32 i)
{ ThreeMMI32 value; memcpy(&value,(const unsigned char *)font->widths+i*4,4); return value; }
ThreeMMI32 three_mm_font_height(const ThreeMMFontResource *font,ThreeMMI32 i)
{ ThreeMMI32 value; memcpy(&value,(const unsigned char *)font->heights+i*4,4); return value; }
int three_mm_font_author(void *cache,void *children,void *out,const ThreeMMFontResourceServices *s)
{
    ThreeMMI32 count,i,letter=-1,bytes,n,width,height,maximum=0; ThreeMMU32 largest=0,number=0;
    void *widths=NULL,*heights=NULL,*model=NULL,*block,*file; ThreeMMFontChild child,replacement; ThreeMMFontChunk chunk;
    unsigned char header[12]; int big=s->host_big_endian;
    count=s->child_count(s->context,children);
    for (i=0;i<count;++i)
    {
        s->child_at(s->context,children,i,&child); if (child.id>largest) largest=child.id;
        if (child.id==105) letter=i;
    }
    if (largest>=0x0ffffffeUL) goto fail;
    n=(ThreeMMI32)largest+1; bytes=n*4;
    if (!s->allocate(s->context,bytes,1,&widths) || !s->allocate(s->context,bytes,1,&heights)) goto fail;
    file=s->file(s->context,cache); block=s->add(s->context,file,12+bytes+bytes,THREE_MM_FONT_TYPE,&number);
    if (!block) goto fail;
    for (i=0;i<count;++i)
    {
        s->child_at(s->context,children,i,&child); model=s->fetch_model(s->context,cache,child.type,child.number);
        if (!model) goto fail;
        if (!s->adopt(s->context,file,THREE_MM_FONT_TYPE,number,&child)) goto fail;
        if (!s->model_width(s->context,model) && child.id==32 && letter!=-1)
        {
            s->release(s->context,model); model=NULL;
            s->child_at(s->context,children,letter,&replacement);
            model=s->fetch_model(s->context,cache,replacement.type,replacement.number); if (!model) goto fail;
        }
        width=s->model_width(s->context,model)+s->spacing; height=s->model_height(s->context,model)+s->leading;
        memcpy((unsigned char *)widths+child.id*4,&width,4); memcpy((unsigned char *)heights+child.id*4,&height,4);
        if (height>maximum) maximum=height;
        s->release(s->context,model); model=NULL;
    }
    put(header,2,1,big); put(header+2,2,s->system,big); put(header+4,4,(ThreeMMU32)n,big); put(header+8,4,(ThreeMMU32)maximum,big);
    if (!s->write(s->context,block,header,12,0) || !s->write(s->context,block,widths,bytes,12) || !s->write(s->context,block,heights,bytes,12+bytes)) goto fail;
    s->free_memory(s->context,&widths); s->free_memory(s->context,&heights);
    chunk.type=THREE_MM_FONT_TYPE; chunk.number=number; if (out) memcpy(out,&chunk,8); return 1;
fail:
    if (model) s->release(s->context,model);
    s->free_memory(s->context,&widths); s->free_memory(s->context,&heights);
    if (out) s->invalidate(s->context,out,8); return 0;
}
void *three_mm_font_model(void *object,ThreeMMU32 id,const ThreeMMFontResourceServices *s)
{
    void *cache,*file; ThreeMMFontChunk chunk; ThreeMMFontChild child;
    s->binding(s->context,object,&cache,&chunk); file=s->file(s->context,cache);
    if (!s->child_id(s->context,file,chunk.type,chunk.number,id,&child)) { s->missing_model(s->context,id); return NULL; }
    return s->fetch_model(s->context,cache,child.type,child.number);
}
