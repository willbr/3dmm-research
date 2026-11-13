/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "three_mm/sound_resource.h"
#include <string.h>
static ThreeMMU32 word(const unsigned char *p,int n,int big)
{ ThreeMMU32 v=0; int i; for (i=0;i<n;++i) v=(v<<8)|p[big?i:n-1-i]; return v; }
static void put(unsigned char *p,int n,ThreeMMU32 v,int big)
{ int i; for (i=0;i<n;++i) { p[big?n-1-i:i]=(unsigned char)(v&255U); v>>=8; } }
static void header(unsigned char *b,ThreeMMI32 style,ThreeMMI32 volume,int invalid,const ThreeMMSoundResourceServices *s)
{
    memset(b,0,16); put(b,2,1U,s->host_big_endian); put(b+2,2,s->system,s->host_big_endian);
    put(b+4,4,(ThreeMMU32)style,s->host_big_endian); put(b+8,4,(ThreeMMU32)volume,s->host_big_endian); b[12]=(unsigned char)(invalid!=0);
}
int three_mm_sound_read(void *object,void *file,ThreeMMU32 type,ThreeMMU32 number,const ThreeMMSoundResourceServices *s)
{
    void *block; unsigned char b[16]; ThreeMMU32 v; int big; ThreeMMSoundOwner o; ThreeMMSoundChild child;
    block=s->find(s->context,file,type,number);
    if (!block || !s->unpack(s->context,block) || s->block_size(s->context,block)!=16 || !s->read(s->context,block,b,16)) return 0;
    big=s->host_big_endian; if (word(b,2,big)!=1U) big=!big;
    if (word(b,2,big)!=1U && s->bad_order) s->bad_order(s->context);
    if (!s->read_name(s->context,file,type,number,object)) return 0;
    s->owner(s->context,object,&o);
    v=word(b+4,4,s->host_big_endian); memcpy(&o.resource->style,&v,4);
    v=word(b+8,4,s->host_big_endian); memcpy(&o.resource->volume,&v,4); *o.invalid=(unsigned char)(b[12]!=0);
    if (*o.invalid) return 1;
    if (s->child(s->context,file,type,number,&child)) { o.resource->number=child.number; o.resource->type=child.type; *o.silent=0; }
    else *o.silent=1;
    return 1;
}
int three_mm_sound_fetch(void *cache,ThreeMMU32 type,ThreeMMU32 number,void *object_out,void *size_out,const ThreeMMSoundResourceServices *s)
{
    void *object; ThreeMMI32 bytes; ThreeMMSoundOwner o;
    bytes=s->object_size(s->context); memcpy(size_out,&bytes,4); if (!object_out) return 1;
    object=s->create(s->context);
    if (!object || !three_mm_sound_read(object,s->file(s->context,cache),type,number,s))
    {
        s->invalidate_output(s->context,object_out,sizeof(object)); s->invalidate_output(s->context,size_out,4);
        if (object) s->release(s->context,object); return 0;
    }
    s->owner(s->context,object,&o); o.resource->cache=cache; memcpy(object_out,&object,sizeof(object)); return 1;
}
int three_mm_sound_info(void *file,ThreeMMU32 type,ThreeMMU32 number,void *invalid_out,void *style_out,void *volume_out,const ThreeMMSoundResourceServices *s)
{
    void *object; ThreeMMSoundOwner o;
    object=s->create(s->context); if (!object) return 0;
    if (!three_mm_sound_read(object,file,type,number,s)) { s->release(s->context,object); return 0; }
    s->owner(s->context,object,&o);
    if (invalid_out) s->publish_invalid(s->context,invalid_out,*o.invalid!=0);
    if (style_out) memcpy(style_out,&o.resource->style,4);
    if (volume_out) memcpy(volume_out,&o.resource->volume,4);
    s->release(s->context,object); return 1;
}
int three_mm_sound_write(void *source,int wave,void *file,ThreeMMI32 style,const void *name,void *number_out,const ThreeMMSoundResourceServices *s)
{
    unsigned char b[16]; ThreeMMU32 number,child; ThreeMMI32 bytes;
    bytes=wave?s->wave_size(s->context,source):0; header(b,wave?style:4,65536L,0,s);
    if (!s->add(s->context,file,b,number_out)) return 0;
    memcpy(&number,number_out,4); if (!wave) bytes=s->midi_size(s->context,source);
    if (!s->add_child(s->context,file,number,wave?THREE_MM_SOUND_WAVE:THREE_MM_SOUND_MIDI,bytes,&child)) goto fail;
    if (wave) { if (!s->find_wave(s->context,file,child) || !s->copy_wave(s->context)) goto fail; }
    else if (!s->write_midi(s->context,source)) goto fail;
    if (!s->set_name(s->context,file,number,name)) goto fail;
    return 1;
fail:
    s->delete_parent(s->context,file,number); return 0;
}
int three_mm_sound_copy_midi(void *source,void *file,const void *name,void *number_out,const ThreeMMSoundResourceServices *s)
{
    void *prepared,*stream; int result;
    prepared=s->prepare_name(s->context,source,name); stream=s->read_midi(s->context);
    if (!stream) { s->bad_sound(s->context); return 0; }
    result=three_mm_sound_write(stream,0,file,4,prepared,number_out,s); s->release(s->context,stream); return result;
}
int three_mm_sound_invalidate(void *object,const ThreeMMSoundResourceServices *s)
{
    void *file; ThreeMMU32 type,number; ThreeMMSoundChild child; ThreeMMSoundOwner o; unsigned char b[16];
    s->binding(s->context,object,&file,&type,&number);
    if (!s->child(s->context,file,THREE_MM_SOUND_TYPE,number,&child)) return 0;
    s->owner(s->context,object,&o); header(b,o.resource->style,o.resource->volume,1,s);
    if (!s->put(s->context,file,type,number,b)) return 0;
    s->delete_child(s->context,file,type,number,&child); *o.invalid=1; return 1;
}
ThreeMMI32 three_mm_sound_class(ThreeMMI32 style,int loop) { return loop?(style==4?2:1):0; }
ThreeMMI32 three_mm_sound_priority(ThreeMMI32 style,int tool)
{ if (style==4) return 1; if (style==3) return tool==0?1:tool==2?5:3; if (style==2) return tool==0?2:tool==2?6:4; return 0; }
void three_mm_sound_play(void *object,ThreeMMSoundLastMidi *last,ThreeMMI32 object_id,int loop,int queue,ThreeMMI32 volume,ThreeMMI32 priority,int actor,ThreeMMU32 start,const ThreeMMSoundResourceServices *s)
{
    ThreeMMSoundOwner o; ThreeMMI32 number,sound_class,instance=0;
    s->owner(s->context,object,&o); if (*o.invalid) return;
    number=s->queue_number(s->context,o.resource->style,object_id,actor); sound_class=three_mm_sound_class(o.resource->style,loop);
    if (o.resource->style==4 && o.resource->type==last->type && o.resource->number==last->number && s->playing(s->context,last->instance)) return;
    if (!queue || *o.silent) s->stop(s->context,number);
    if (!*o.silent) instance=s->play(s->context,o.resource,number,volume,loop?2147483647L:1,start,priority,sound_class);
    if (o.resource->style==4) { last->instance=instance; last->type=o.resource->type; last->number=o.resource->number; }
}
