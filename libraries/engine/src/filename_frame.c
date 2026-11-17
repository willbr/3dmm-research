/* Plain filename enumeration frames converted from Kauai filename.cpp/fni.h.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "mm/filename_frame.h"
#include <string.h>
static void initialize(const MMFilenameEnumFrame *v, MMFilenameFrameMetadata *metadata)
{
    mm_filename_owned_init(&v->name,NULL);
    mm_reference_init(&metadata->references);metadata->debug.kind=MM_OBJECT_DEBUG_LOCAL;
    memset(v->cursor,0,sizeof(*v->cursor));memset(v->entry,0,1024*v->name.path.unit);
}
void mm_filename_frame8_init(MMFilenameFrame8 *s)
{ MMFilenameEnumFrame v=mm_filename_frame8_view(s);initialize(&v,&s->metadata); }
void mm_filename_frame16_init(MMFilenameFrame16 *s)
{ MMFilenameEnumFrame v=mm_filename_frame16_view(s);initialize(&v,&s->metadata); }
MMFilenameEnumFrame mm_filename_frame8_view(MMFilenameFrame8 *s)
{
    MMFilenameEnumFrame v;v.record=s;v.cursor=&s->cursor;v.name=mm_filename_owned8_view(&s->name,NULL);v.entry=s->entry;return v;
}
MMFilenameEnumFrame mm_filename_frame16_view(MMFilenameFrame16 *s)
{
    MMFilenameEnumFrame v;v.record=s;v.cursor=&s->cursor;v.name=mm_filename_owned16_view(&s->name,NULL);v.entry=s->entry;return v;
}
MMFilenameMetadataView mm_filename_frame_metadata(MMFilenameFrameMetadata *s)
{ MMFilenameMetadataView v;v.references=&s->references;v.debug=&s->debug;return v; }
void mm_filename_frame_copy_name(const MMFilenameValue *out, const MMFilenameMetadataView *om,
                                 const MMFilenameValue *in, const MMFilenameMetadataView *im,
                                 const MMFilenameOwnedHost *h)
{
    if(om && im) {
        if(om->references && im->references)*om->references=*im->references;
        if(om->debug && im->debug)*om->debug=*im->debug;
    }
    mm_filename_owned_copy(out,in,1,h);
}
