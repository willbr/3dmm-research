/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "mm/action_definition.h"
#include "mm/movie_session.h"
#include <string.h>
static MMU32 word(const unsigned char *p,int big)
{
    MMU32 value=0; int i;
    for (i=0;i<4;++i) value=(value<<8)|p[big?i:3-i];
    return value;
}
static void swap(unsigned char *p,MMI32 count,int width)
{
    MMI32 i; int j; unsigned char t;
    for (i=0;i<count;++i,p+=width) for (j=0;j<width/2;++j)
    { t=p[j]; p[j]=p[width-1-j]; p[width-1-j]=t; }
}

static void decode_words(const void *bytes, int other, void *out, int count, int width)
{
    if (out != bytes)
        memcpy(out, bytes, (size_t)(count * width));
    if (other)
        swap((unsigned char *)out, count, width);
}

void mm_action_decode_cel(const void *bytes, int other, void *out)
{
    decode_words(bytes, other, out, 2, 4);
}

void mm_action_decode_part(const void *bytes, int other, void *out)
{
    decode_words(bytes, other, out, 2, 2);
}

void mm_action_decode_matrix(const void *bytes, int other, void *out)
{
    decode_words(bytes, other, out, 12, 4);
}

MMU32 mm_action_table_type(int kind)
{
    static const MMU32 types[] = {MM_ACTION_GGCL, MM_ACTION_GLXF, MM_ACTION_GLMS};
    return types[kind];
}

int mm_action_read_cels(MMActionDefinition *action, const MMActionRead *read, int *other)
{
    unsigned char bytes[MM_ACTION_HEADER_BYTES];
    int big, status = read->header(read->context, bytes);
    if (status != MM_MOVIE_OK)
        return status;
    if (bytes[0] == 1 && bytes[1] == 0)
        big = 0;
    else if (bytes[0] == 0 && bytes[1] == 1)
        big = 1;
    else
    {
        if (read->bad_order)
            read->bad_order(read->context);
        big = read->host_big_endian;
    }
    action->flags = word(bytes + 4, big);
    return read->table(read->context, MM_ACTION_CELS, &action->cels, other);
}

int mm_action_read_matrices(MMActionDefinition *action, const MMActionRead *read, int *other)
{
    return read->table(read->context, MM_ACTION_MATRICES, &action->matrices, other);
}
void *mm_action_create(void *cels,void *matrices,MMU32 flags,const MMActionDefinitionServices *s)
{
    void *object=s->create(s->context); MMActionDefinition *a;
    if (!object) return NULL;
    a=s->state(s->context,object); a->flags=flags;
    a->cels=s->duplicate(s->context,cels,MM_ACTION_CELS);
    if (a->cels)
    {
        a->matrices=s->duplicate(s->context,matrices,MM_ACTION_MATRICES);
        if (a->matrices) return object;
    }
    s->release(s->context,object); return NULL;
}
int mm_action_fetch(void *file,MMU32 type,MMU32 number,void *block,void *out,void *size,const MMActionDefinitionServices *s)
{
    MMI32 bytes; void *object; MMActionDefinition *a;
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
typedef struct NativeActionRead
{
    const MMActionDefinitionServices *services;
    void *file;
    MMU32 type, number;
} NativeActionRead;

static int native_header(void *context, unsigned char *bytes)
{
    NativeActionRead *read = (NativeActionRead *)context;
    const MMActionDefinitionServices *s = read->services;
    void *block = s->find(s->context, read->file, read->type, read->number);
    if (!block || !s->unpack(s->context, block) || s->block_size(s->context, block, 0) < MM_ACTION_HEADER_BYTES ||
        !s->block_read(s->context, block, bytes, MM_ACTION_HEADER_BYTES))
        return MM_MOVIE_BAD_FILE;
    return MM_MOVIE_OK;
}

static int native_table(void *context, int kind, void **out, int *other)
{
    NativeActionRead *read = (NativeActionRead *)context;
    const MMActionDefinitionServices *s = read->services;
    MMU32 child, type = mm_action_table_type(kind);
    void *block;
    if (!s->child(s->context, read->file, read->type, read->number, type, &child))
        return MM_MOVIE_ASSET_NOT_FOUND;
    block = s->find(s->context, read->file, type, child);
    if (!block)
        return MM_MOVIE_BAD_FILE;
    *out = s->read_table(s->context, block, kind, other);
    return *out ? MM_MOVIE_OK : MM_MOVIE_BAD_FILE;
}

static void native_bad_order(void *context)
{
    const MMActionDefinitionServices *s = ((NativeActionRead *)context)->services;
    if (s->bad_order)
        s->bad_order(s->context);
}

int mm_action_initialize(MMActionDefinition *a,void *file,MMU32 type,MMU32 number,const MMActionDefinitionServices *s)
{
    NativeActionRead native;
    MMActionRead read = {NULL, 0, native_header, native_table, native_bad_order};
    unsigned char *p; void *wire; int big,other,status; MMI32 i,count; MMTag tag; MMU32 sid;
    native.services = s;
    native.file = file;
    native.type = type;
    native.number = number;
    read.context = &native;
    read.host_big_endian = s->host_big_endian;
    /* Missing child/find retains the old slot, just as the native early return.
     * A failed reader publishes NULL. Fresh owners are the supported input. */
    if (mm_action_read_cels(a, &read, &other) != MM_MOVIE_OK) return 0;
    if (other) for (i=0;i<s->count(s->context,a->cels);++i)
    {
        p = s->fixed(s->context, a->cels, i);
        mm_action_decode_cel(p, 1, p);
        swap(s->variable(s->context,a->cels,i),s->variable_size(s->context,a->cels,i)/2,2);
    }
    if (mm_action_read_matrices(a, &read, &other) != MM_MOVIE_OK) return 0;
    count=s->count(s->context,a->matrices);
    if (other && count) swap(s->data(s->context,a->matrices,0),count*12,4);
    status = native_table(&native, MM_ACTION_WIRE_SOUNDS, &wire, &other);
    if (status == MM_MOVIE_ASSET_NOT_FOUND) return 1;
    if (status != MM_MOVIE_OK) return 0;
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
void mm_action_dispose(MMActionDefinition *a,const MMActionDefinitionServices *s)
{
    if (a->cels) { s->release(s->context,a->cels); a->cels=NULL; }
    if (a->matrices) { s->release(s->context,a->matrices); a->matrices=NULL; }
    if (a->sounds) { s->release(s->context,a->sounds); a->sounds=NULL; }
}
MMI32 mm_action_cel_count(const MMActionDefinition *a,const MMActionDefinitionServices *s)
{ return s->count(s->context,a->cels); }
void mm_action_get_cel(const MMActionDefinition *a,MMI32 i,void *out,const MMActionDefinitionServices *s)
{ mm_action_decode_cel(s->fixed(s->context,a->cels,i),0,out); }
void mm_action_get_part(const MMActionDefinition *a,MMI32 cel,MMI32 part,void *out,const MMActionDefinitionServices *s)
{ mm_action_decode_part(s->variable(s->context,a->cels,cel)+part*MM_ACTION_PART_BYTES,0,out); }
void mm_action_get_matrix(const MMActionDefinition *a,MMI32 i,void *out,const MMActionDefinitionServices *s)
{ mm_action_decode_matrix(s->data(s->context,a->matrices,i),0,out); }
void mm_action_get_sound(const MMActionDefinition *a,MMI32 cel,void *out,const MMActionDefinitionServices *s)
{
    MMU32 id; MMTag tag; s->clear_sound_type(s->context,out);
    if (!a->sounds) return;
    memcpy(&id,s->fixed(s->context,a->cels,cel),sizeof(id));
    if (id<0x80000000UL) { s->read_sound(s->context,a->sounds,(MMI32)id,&tag); s->publish_sound(s->context,out,&tag); }
}
