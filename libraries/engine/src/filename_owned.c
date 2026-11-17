/* Filename storage/copy/validation converted from Kauai filename.cpp and String.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "mm/filename_owned.h"
#include <string.h>
typedef union PathBuffer { unsigned char bytes[256]; unsigned short words[256]; } PathBuffer;
static unsigned unit(const MMStringValue *v, size_t i)
{ return v->unit == 1 ? ((const unsigned char *)v->buffer)[i] : ((const unsigned short *)v->buffer)[i]; }
static const void *characters(const MMStringValue *v)
{ return (const unsigned char *)v->buffer + v->unit; }
static void check(const MMFilenameOwnedHost *h, const MMStringValue *v)
{ if (h && h->check_path) h->check_path(h->context,v); }
static void assertion(const MMFilenameOwnedHost *h, int valid, int code, MMI32 observed)
{ if (!valid && h && h->assertion) h->assertion(h->context,code,observed); }
static MMFilenameValue view(MMFilenameState *state, void *path, unsigned size, void *owner)
{
    MMFilenameValue v; memset(&v,0,sizeof(v)); v.state=state; v.path.buffer=path;
    v.path.unit=size; v.owner=owner; return v;
}
MMFilenameValue mm_filename_owned8_view(MMFilenameOwned8 *s, void *owner)
{ return view(&s->state,s->path,1,owner); }
MMFilenameValue mm_filename_owned16_view(MMFilenameOwned16 *s, void *owner)
{ return view(&s->state,s->path,2,owner); }
void mm_filename_owned_init(const MMFilenameValue *v, const MMFilenameOwnedHost *h)
{ mm_string_value_clear(&v->path); v->state->type=MM_FILENAME_NIL; check(h,&v->path); }
void mm_filename_owned_copy(const MMFilenameValue *out, const MMFilenameValue *in, int assignment, const MMFilenameOwnedHost *h)
{
    out->state->type=in->state->type;
    if (assignment) check(h,&out->path);
    check(h,&in->path); mm_string_value_copy(&out->path,in->path.buffer); check(h,&out->path);
}
void mm_filename_owned_set_path(const MMFilenameValue *v, const void *input, MMI32 count, const MMFilenameOwnedHost *h)
{
    check(h,&v->path);
    if (h && h->arguments) h->arguments(h->context,input,count,v->path.unit);
    mm_string_value_set(&v->path,input,count); check(h,&v->path);
}
void mm_filename_owned_export(const MMFilenameValue *v, const MMStringValue *out, const MMFilenameOwnedHost *h)
{ check(h,&v->path); mm_string_value_copy(out,v->path.buffer); }
void mm_filename_owned_check_path(const MMStringValue *v, const MMFilenameOwnedHost *h)
{
    unsigned count; size_t i;
    if (h && h->memory) h->memory(h->context,v->buffer,257*v->unit);
    count=unit(v,0); assertion(h,count<=255,MM_FILENAME_CHECK_COUNT,(MMI32)count);
    if (count>255) return;
    assertion(h,unit(v,count+1)==0,MM_FILENAME_CHECK_TERMINATOR,(MMI32)count);
    for (i=1;i<257 && unit(v,i);++i) { }
    assertion(h,i-1==count,MM_FILENAME_CHECK_NULL,(MMI32)count);
}
void mm_filename_owned_validate(const MMFilenameValue *v, int flags, const MMFilenameOwnedHost *h)
{
    PathBuffer output; void *leaf=NULL; const unsigned char *start=(const unsigned char *)&output;
    size_t count; unsigned last;
    check(h,&v->path);
    if (!flags) flags=MM_FILENAME_FILE|MM_FILENAME_DIR|MM_FILENAME_EMPTY;
    if (v->state->type==MM_FILENAME_NIL) {
        assertion(h,flags&MM_FILENAME_EMPTY,MM_FILENAME_CHECK_EMPTY,flags);
        check(h,&v->path); assertion(h,unit(&v->path,0)==0,MM_FILENAME_CHECK_NAMED_EMPTY,0); return;
    }
    check(h,&v->path); count=h->full_path(h->context,characters(&v->path),&output,255,&leaf);
    if (!count || count>255) { assertion(h,0,MM_FILENAME_CHECK_FULL,0); return; }
    check(h,&v->path);
    if (!h->equal(h->context,characters(&v->path),&output)) { assertion(h,0,MM_FILENAME_CHECK_FULL,0); return; }
    if (v->state->type==MM_FILENAME_DIRECTORY) {
        assertion(h,flags&MM_FILENAME_DIR,MM_FILENAME_CHECK_DIR,flags);
        last=v->path.unit==1 ? output.bytes[count-1] : output.words[count-1];
        assertion(h,h->separator(h->context,last),MM_FILENAME_CHECK_SEPARATOR,0);
        assertion(h,leaf==NULL,MM_FILENAME_CHECK_NO_LEAF,0);
    } else {
        assertion(h,flags&MM_FILENAME_FILE,MM_FILENAME_CHECK_FILE,flags);
        assertion(h,leaf && (const unsigned char *)leaf>=start && (const unsigned char *)leaf<start+count*v->path.unit,
                  MM_FILENAME_CHECK_LEAF,0);
    }
}
