#include "mm/text_resource.h"
#include "mm/chunk_ops.h"
#include "engine_resource_block_fixture.h"
#include "template_writer_fixture.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>
typedef struct Root { int refs,object; } Root;
typedef struct Object
{
    Root root; MMTextDescriptor descriptor; MMTextGenerated generated;
    MMTemplateState template_state; char text[256]; int provider;
} Object;
typedef struct Context
{
    MMTextResourceServices services; char saved[256]; unsigned char wire[24];
    ResourceTestBlock block;
    MMChunkPayloadServices payloads;
    MMResourceCache cache;
    MMTextResourceSource source;
    MMI32 bytes; int fail,step,live,header,child,found,packed,unpacked,appended;
    int file_refs;
    int error,released_error,bad,warnings,custom,defaults,names_published;
    MMTextNames *names;
} Context;
static void copy_string(char *to,const char *from) { memcpy(to,from,strlen(from)+1); }
static void *allocate(Context *c,size_t n,int object)
{ Root *r; if (++c->step==c->fail) return NULL; r=(Root *)calloc(1,n); assert(r); r->refs=1; r->object=object; ++c->live; return r; }
static void retain(void *p,void *v)
{ Context *c=(Context *)p; if (c->names && c->names->table==v) c->names_published=1; ++((Root *)v)->refs; }
static void release(void *p,void *v)
{
    Context *c=(Context *)p; Root *r=(Root *)v; Object *o;
    if (--r->refs) return;
    c->released_error=c->error;
    if (r->object)
    {
        o=(Object *)v; mm_text_dispose(&o->generated,&c->services.generation);
        mm_part_indices_dispose(o->template_state.parents);
        mm_part_indices_dispose(o->template_state.sets);
        mm_template_costumes_dispose(o->template_state.costumes);
    }
    --c->live; free(v);
}
static void *create(void *p) { return allocate((Context *)p,sizeof(Object),1); }
static MMTextDescriptor *state(void *p,void *o) { (void)p; return &((Object *)o)->descriptor; }
static MMTextGenerated *generated(void *p,void *o) { (void)p; return &((Object *)o)->generated; }
static void *text(void *p,void *o) { (void)p; return ((Object *)o)->text; }
static MMI32 length(void *p,void *o) { (void)p; return (MMI32)strlen(((Object *)o)->text); }
static void assign(void *p,void *to,const void *from) { (void)p; copy_string((char *)to,(const char *)from); }
static void space(void *p,void *t) { (void)p; copy_string((char *)t," "); }
static void clear(void *p,void *t) { (void)p; *(char *)t=0; }
static void *saved(void *p) { return ((Context *)p)->saved; }
static void provider_copy(void *p,void *to,void *from) { (void)p; ((Object *)to)->provider=((Object *)from)->provider; }
static int has_provider(void *p,void *o) { (void)p; return ((Object *)o)->provider; }
static void *provider(void *p,void *o,const MMTag *t) { (void)o; (void)t; ++((Context *)p)->custom; return NULL; }
static void *default_font(void *p,const MMTag *t) { (void)t; ++((Context *)p)->defaults; return p; }
static void publish_tag(void *p,void *out,const MMTag *t) { (void)p; memcpy(out,t,sizeof(*t)); }
static MMTemplateState *template_state(void *p,void *o) { (void)p; return &((Object *)o)->template_state; }
static void *list_new(void *p,MMI32 bytes,MMI32 count) { (void)bytes; (void)count; return allocate((Context *)p,sizeof(Root),0); }
static void *resize_shape(void *p,void *data,MMU32 old_bytes,MMU32 bytes,int priority)
{
    Context *c=p;
    void *result;
    (void)priority;
    ++c->step;
    if (c->fail && c->step>=c->fail)
        return NULL;
    result=realloc(data,bytes);
    assert(result);
    if (!old_bytes) ++c->live;
    return result;
}
static void free_shape(void *p,void *data,MMU32 bytes)
{
    Context *c=p;
    assert(data && bytes && c->live>0);
    --c->live;
    free(data);
}
static void *material_new(void *p) { return list_new(p,0,0); }
static int read_template(void *p,void *o,MMU32 type,MMU32 n,MMOwnedBlock *block)
{ Context *c=(Context *)p; assert(block==&c->block.owner); (void)type; (void)n; copy_string(((Object *)o)->text,"loaded"); return c->header; }
static int graph_entry(void *p, MMU32 pos, MMChunkEntry *out)
{
    Context *c = (Context *)p;
    assert(pos == 0);
    memset(out, 0, sizeof(*out));
    out->type = MM_TMPL;
    out->number = 37;
    out->child_count = c->child != 0;
    return 1;
}
static int graph_child(void *p, MMU32 parent, MMU32 pos, MMChunkChild *out)
{
    assert(parent == 0 && pos == 0 && ((Context *)p)->child);
    out->type = MM_TDT;
    out->number = 19;
    out->slot = 0;
    return 1;
}
static MMChunkAccess live_chunks(void *file)
{
    return ((Context *)file)->source.chunks;
}
static int locate(void *file, MMU32 type, MMU32 number, MMBlockFile *out, int *packed)
{
    Context *c = (Context *)file;
    assert(type == MM_TDT && number == 19);
    if (!c->found)
        return 0;
    out->file = file;
    out->offset = 5;
    out->length = c->packed ? 12 : (MMU32)c->bytes;
    *packed = c->packed;
    return 1;
}
static void retain_file(void *p, void *file)
{
    Context *c = (Context *)p;
    assert(file == p && !c->file_refs);
    ++c->file_refs;
}
static void release_file(void *p, void *file)
{
    Context *c = (Context *)p;
    assert(file == p && c->file_refs == 1);
    --c->file_refs;
}
static int read_block(void *p, void *file, MMU32 offset, void *out, size_t count)
{
    Context *c = (Context *)p;
    assert(file == p && c->file_refs == 1 && offset == 5 && count <= sizeof(c->wire));
    memcpy(out, c->wire, count);
    return 1;
}
static int decode(void *p, void **handle)
{
    Context *c = (Context *)p;
    assert(resource_handle_size(NULL, *handle) == 12);
    if (!c->unpacked)
        return 0;
    resource_block_decode(handle, c->wire, sizeof(c->wire));
    return 1;
}
static int write_block(void *p, void *file, MMU32 offset, const void *bytes, size_t count)
{ (void)p; (void)file; (void)offset; (void)bytes; (void)count; return 0; }
static void publish_error(void *p,void *out,int v) { ((Context *)p)->error=v; *(int *)out=v; }
static void *tag_list(void *p) { assert(!((Context *)p)->error); return list_new(p,0,0); }
static int append_tag(void *p,void *list,const MMTag *t) { (void)list; assert(!t->cache); ++((Context *)p)->appended; return ((Context *)p)->child!=2; }
static MMI32 name_count(void *p,void *t) { (void)p; (void)t; return 3; }
static MMI32 name_id(void *p,void *t,MMI32 i) { (void)p; (void)t; return i==1?0:7; }
static void name_text(void *p,void *t,MMI32 i,void *out) { (void)p; (void)t; copy_string((char *)out,i==0?"Pulse":i==1?"Rest":"duplicate"); }
static void missing(void *p) { ++((Context *)p)->warnings; }
static void bad(void *p) { ++((Context *)p)->bad; }
static void init(Context *c)
{
    MMTextResourceServices *s; MMTextGenerationServices *g;
    memset(c,0,sizeof(*c)); c->bytes=24; c->header=c->child=c->found=c->unpacked=1; s=&c->services; g=&s->generation;
    resource_block_init(&c->block, c, read_block, write_block);
    c->block.files.retain = retain_file;
    c->block.files.release = release_file;
    c->payloads.handles = c->block.owner.handles;
    c->payloads.files = &c->block.files;
    c->payloads.locate = locate;
    c->payloads.decode = decode;
    c->payloads.decode_context = c;
    c->source.archive = c;
    c->source.chunks.context = c;
    c->source.chunks.count = 1;
    c->source.chunks.entry = graph_entry;
    c->source.chunks.child = graph_child;
    c->cache.file = c;
    c->cache.services.block_files = c->payloads.files;
    c->cache.services.handles = c->payloads.handles;
    c->cache.services.live_chunks = live_chunks;
    c->cache.services.locate = locate;
    s->context=c; g->context=c; g->state=generated; g->length=length; g->template_state=template_state;
    g->table_allocator.context=c; g->table_allocator.resize=resize_shape; g->table_allocator.release=free_shape;
    g->material_new=material_new; g->retain_material=retain; g->release_material=release;
    s->state=state; s->create=create; s->retain_object=retain; s->release_object=release; s->release_tag_list=release; s->text=text; s->assign=assign; s->space=space; s->clear=clear; s->saved_text=saved; s->provider_copy=provider_copy;
    s->has_provider=has_provider; s->provider_font=provider; s->default_font=default_font; s->publish_tag=publish_tag; s->read_template=read_template;
    s->payloads=&c->payloads;
    s->publish_error=publish_error; s->tag_list=tag_list; s->append_tag=append_tag; s->name_count=name_count; s->name_id=name_id; s->name_text=name_text; s->missing_name=missing; s->bad_order=bad;
}
static void lifecycle(void)
{
    Context c; Object *o,*copy; MMTag font={123,NULL,0x54444620U,29},out; MMI32 shape; char value[256]; int failure,create_steps=0,replace_steps;
    const MMActionDefinition empty={0};
    font.cache=&font;
    for (failure=0;failure<=create_steps;++failure)
    {
        init(&c); c.fail=failure; o=(Object *)mm_text_create("",9,&font,&c.services);
        assert((o!=NULL)==(failure==0)); if (o) { create_steps=c.step; assert(!strcmp(o->text," ")); release(&c,o); } assert(!c.live);
    }
    init(&c); o=(Object *)mm_text_create("old",9,&font,&c.services); assert(o); o->provider=1;
    assert(!mm_text_font(o,&c.services) && c.custom==1 && !c.defaults);
    o->provider=0; assert(mm_text_font(o,&c.services)==&c && c.defaults==1); o->provider=1;
    c.step=0;
    assert(mm_text_change(o,NULL,-1,NULL,&c.services));
    replace_steps=c.step;
    for (failure=1;failure<=replace_steps;++failure)
    {
        void *parents=o->template_state.parents; c.step=0; c.fail=failure;
        assert(!mm_text_change(o,"",0,NULL,&c.services));
        assert(!strcmp(o->text,"old") && o->descriptor.shape==9 && o->descriptor.font.cache==font.cache && o->template_state.parents==parents);
    }
    c.fail=0; assert(mm_text_change(o,"",0,NULL,&c.services) && !strcmp(o->text," "));
    o->generated.action=mm_action_resource_create(&empty,NULL); assert(o->generated.action); o->generated.action_id=7;
    assert(mm_text_change(o,NULL,-1,NULL,&c.services) && !o->generated.action && o->generated.action_id==-1);
    copy=(Object *)mm_text_duplicate(o,&c.services); assert(copy && copy->provider && copy->generated.material!=o->generated.material);
    mm_text_info(copy,value,&shape,&out,&c.services); assert(!strcmp(value," ") && !shape && out.cache==font.cache && out.sid==123);
    mm_text_info(copy,NULL,NULL,NULL,&c.services); release(&c,copy); release(&c,o); assert(!c.live);
}
static void wire(void)
{
    Context c; Object *o,*loaded; void *tags; MMTag font={-1,NULL,0xfedcba98U,0x87654321U}; MMU32 number; int endian,failure,error;
    TemplateWriterFixture output;
    MMArchiveWriter writer;
    const MMChunkNameValue name = {"saved",5,5};
    for (endian=0;endian<2;++endian)
    {
        init(&c); c.services.host_big_endian=endian; o=(Object *)mm_text_create("saved",9,&font,&c.services); assert(o);
        writer=writer_fixture(&output,endian,0x1234);
        assert(mm_text_write(&o->template_state,&o->descriptor,&name,MM_TMPL,&number,&writer) && number==0 && !output.archive.file_refs);
        memcpy(c.wire,output.descriptor,sizeof(c.wire));
        assert(c.wire[endian?1:0]==1 && c.wire[2]==(endian?0x12:0x34) && c.wire[3]==(endian?0x34:0x12));
        assert(!c.wire[12] && !c.wire[13] && !c.wire[14] && !c.wire[15]);
        c.source.cache = &c.cache;
        for (failure=0;failure<2;++failure)
        {
            loaded=(Object *)create(&c); c.services.host_big_endian=endian^failure;
            c.packed = failure;
            assert(mm_text_read(loaded,&c.source,MM_TMPL,37,&c.block.owner,&c.services) && !c.file_refs);
            assert(loaded->descriptor.shape==9 && loaded->descriptor.font.sid==-1 && loaded->descriptor.font.type==font.type && loaded->descriptor.font.number==font.number && !loaded->descriptor.font.cache);
            release(&c,loaded);
        }
        c.source.cache = NULL;
        for (failure=0;failure<7;++failure)
        {
            c.step=0; c.fail=failure==1?1:0; c.child=failure==2?0:failure==6?2:1; c.found=failure!=3; c.unpacked=failure!=4; c.bytes=failure==5?23:100;
            c.packed = failure == 4;
            error=-1; c.released_error=-1; tags=mm_text_tags(&c.source,MM_TMPL,37,&error,&c.services);
            assert((tags!=NULL)==!failure && error==(failure!=0) && !c.file_refs);
            if (tags) release(&c,tags); else if (failure!=1) assert(c.released_error==1);
        }
        c.fail=0; c.bytes=24; c.child=1; c.found=1;
        c.header=0; c.found=1; loaded=(Object *)create(&c); assert(!mm_text_read(loaded,&c.source,MM_TMPL,37,&c.block.owner,&c.services) && !strcmp(loaded->text,"loaded")); release(&c,loaded);
        c.header=1; c.wire[0]=c.wire[1]=0x7f; loaded=(Object *)create(&c);
        assert(mm_text_read(loaded,&c.source,MM_TMPL,37,&c.block.owner,&c.services) && c.bad==1 && !c.file_refs); release(&c,loaded);
        release(&c,o); assert(!c.live);
    }
}
typedef struct DraftOutput
{
    ResourceTestBlock block;
    unsigned char bytes[64];
    int reject_patch;
} DraftOutput;
static int preview_read(void *p, void *file, MMU32 offset, void *bytes, size_t count)
{
    DraftOutput *o = p;
    assert(file == o && offset + count <= sizeof(o->bytes));
    memcpy(bytes, o->bytes + offset, count);
    return 1;
}
static int preview_write(void *p, void *file, MMU32 offset, const void *bytes, size_t count)
{
    DraftOutput *o = p;
    assert(file == o && offset + count <= sizeof(o->bytes));
    if (!offset && o->reject_patch)
        return 0;
    memcpy(o->bytes + offset, bytes, count);
    return 1;
}
static int preview_bind(void *p, MMU32 position, const MMChunkEntry *entry, MMOwnedBlock *block)
{
    MMBlockFile extent;
    (void)position;
    extent.file = p;
    extent.offset = entry->offset;
    extent.length = entry->bytes;
    return mm_owned_block_set_file(block, &extent, entry->flags & MM_CHUNK_PACKED) == MM_DATA_OK;
}
static int preview_allocate(void *p, MMU32 bytes, MMChunkLocation *location)
{
    location->file = ((ChunkIndexFixture *)p)->observer;
    location->offset = 32;
    location->bytes = bytes;
    location->flags = 0;
    return 1;
}
static const void *preview_name(void *p, MMU32 position)
{
    (void)p; (void)position;
    return NULL;
}
static int preview_set_name(void *p, MMU32 position, const void *name)
{
    (void)p; (void)position; (void)name;
    return 0;
}
static MMChunkEndpoint preview_endpoint(DraftOutput *output)
{
    MMChunkEndpoint endpoint = {0};
    memset(output, 0, sizeof(*output));
    resource_block_init(&output->block, output, preview_read, preview_write);
    endpoint.handles = output->block.owner.handles;
    endpoint.files = &output->block.files;
    endpoint.identity = endpoint.context = output;
    endpoint.bind_payload = preview_bind;
    endpoint.read_name = preview_name;
    endpoint.write_name = preview_set_name;
    return endpoint;
}
static void localize_font(TemplateWriterFixture *saved, MMU32 type)
{
    DraftOutput font, output;
    ChunkIndexFixture font_index = {0};
    MMChunkEndpoint source = preview_endpoint(&font), target = preview_endpoint(&output);
    MMChunkAccess graph, chunks;
    MMChunkEntry copied;
    MMChunkEntry *entry;
    MMTextDescriptor localized;
    MMU32 position, count;
    const unsigned char payload[] = {43, 90, 11, 20};
    const unsigned char *original = saved->descriptor;

    entry = &fixture_chunk_add(&font_index, type, 29)->entry;
    entry->offset = 32;
    entry->bytes = sizeof(payload);
    entry->flags |= MM_CHUNK_PACKED;
    memcpy(font.bytes + 32, payload, sizeof(payload));
    graph = fixture_chunk_access(&font_index);
    source.stored = &graph;

    target.edit = fixture_chunk_edit(&saved->archive.index);
    target.edit.allocate = preview_allocate;
    saved->archive.index.observer = &output;
    fixture_chunk_add(&saved->archive.index, type, 0);
    fixture_chunk_add(&saved->archive.index, type, 29);
    count = saved->archive.index.count;
    chunks = mm_chunk_edit_access(&target.edit);
    assert(mm_chunk_find(&chunks, MM_TDT, 0, &position) == MM_CHUNK_OK);
    entry = &saved->archive.index.rows[position].entry;
    entry->offset = 0;
    entry->bytes = MM_TEXT_DESCRIPTOR_BYTES + 1;
    memcpy(output.bytes, original, MM_TEXT_DESCRIPTOR_BYTES);
    output.bytes[MM_TEXT_DESCRIPTOR_BYTES] = 91;
    output.bytes[0] = 127;
    assert(mm_text_bind_preview_font(&source, &target, 0, 29) == MM_CHUNK_BAD_FILE);
    output.bytes[0] = original[0];
    entry->flags |= MM_CHUNK_PACKED;
    assert(mm_text_bind_preview_font(&source, &target, 0, 29) == MM_CHUNK_BAD_FILE);
    entry->flags &= ~MM_CHUNK_PACKED;
    assert(mm_text_bind_preview_font(&source, &target, 0, 99) == MM_CHUNK_NOT_FOUND);
    assert(saved->archive.index.count == count && !memcmp(output.bytes, original, MM_TEXT_DESCRIPTOR_BYTES));

    output.bytes[12] = 37;
    assert(mm_text_bind_preview_font(&source, &target, 0, 29) == MM_CHUNK_OK);
    assert(saved->archive.index.count == count + 1 && !memcmp(output.bytes + 32, payload, sizeof(payload)));
    chunks = mm_chunk_edit_access(&target.edit);
    assert(mm_chunk_find(&chunks, type, 1, &position) == MM_CHUNK_OK &&
           mm_chunk_at(&chunks, position, &copied) == MM_CHUNK_OK && (copied.flags & MM_CHUNK_PACKED));
    assert(mm_text_decode_descriptor(output.bytes, 0, &localized));
    assert(localized.shape == 9 && localized.font.sid == 0 && localized.font.type == type &&
           localized.font.number == 1 && output.bytes[12] == 37 && output.bytes[MM_TEXT_DESCRIPTOR_BYTES] == 91);
    assert(!memcmp(output.bytes, original, 8) && !memcmp(output.bytes + 16, original + 16, 4));
    output.reject_patch = 1;
    assert(mm_text_bind_preview_font(&source, &target, 0, 29) == MM_CHUNK_IO);
    assert(mm_text_decode_descriptor(output.bytes, 0, &localized) && localized.font.number == 1);
}
static void drafts(void)
{
    Context c;
    TemplateWriterFixture normal, saved;
    MMArchiveWriter writer;
    const MMChunkNameValue name = {"saved", 5, 5};
    MMTextDescriptor draft = {9, {123, NULL, 0x54444620UL, 29}};
    MMTemplateState header;
    MMU32 number;
    Object *object;
    int big, failure;
    for (big = 0; big < 2; ++big)
    {
        init(&c);
        c.services.host_big_endian = big;
        object = mm_text_create("saved", draft.shape, &draft.font, &c.services);
        assert(object);
        for (failure = 1; failure <= 6; ++failure)
        {
            writer = writer_fixture(&normal, big, 0x1234);
            normal.fail = failure;
            number = 99;
            assert(!mm_text_write(&object->template_state, &object->descriptor, &name, MM_TMPL, &number, &writer));
            assert(number == (failure < 3 ? 99U : 0U) && !normal.archive.file_refs);
            assert(normal.archive.index.count == (failure >= 4 ? 1U : 0U));
            if (failure >= 4)
                assert(normal.archive.index.rows[0].entry.type == MM_TMPL && !normal.archive.index.rows[0].entry.child_count);
            if (failure >= 5)
                assert(normal.deleted == 1 && normal.last_deleted == MM_TDT);
            writer = writer_fixture(&saved, big, 0x1234);
            fixture_chunk_add(&saved.archive.index, MM_GLPI, 0);
            saved.fail = failure;
            number = 99;
            assert(!mm_text_write_draft(&name, &draft, &number, &writer));
            assert(number == 99 && saved.archive.index.count == 1 && !saved.archive.file_refs);
            assert(saved.archive.index.rows[0].entry.type == MM_GLPI);
            assert(saved.deleted == (failure == 1 ? 0 : failure >= 5 ? 2 : 1));
        }
        writer = writer_fixture(&normal, big, 0x1234);
        assert(mm_text_write(&object->template_state, &object->descriptor, &name, MM_TMPL, &number, &writer));
        release(&c, object);
        writer = writer_fixture(&saved, big, 0x1234);
        assert(mm_text_write_draft(&name, &draft, &number, &writer) && number == 0);
        assert(mm_template_decode_header(saved.header, 0, &header));
        assert(!header.x && !header.y && !header.z && header.flags == MM_TEMPLATE_TEXT);
        assert(!memcmp(saved.header, normal.header, sizeof(saved.header)));
        assert(!memcmp(saved.descriptor, normal.descriptor, sizeof(saved.descriptor)));
        assert(!memcmp(saved.archive.name, normal.archive.name, sizeof(saved.archive.name)) && !saved.archive.file_refs && !normal.archive.file_refs);
        localize_font(&saved, draft.font.type);
    }
}
static void author_names(void)
{
    char name[MM_TEXT_NAME_BYTES] = "old";
    char long_name[MM_TEXT_NAME_BYTES + 1];
    char max_utf8[MM_TEXT_NAME_UTF8_BYTES];
    MMTextAuthorName authored;
    int i;
    assert(mm_text_author_name(NULL, name) == MM_TEXT_NAME_EMPTY);
    assert(mm_text_author_name("", name) == MM_TEXT_NAME_EMPTY);
    assert(mm_text_author_name(" \t\r\n", name) == MM_TEXT_NAME_LENGTH);
    assert(mm_text_author_name("A\xc3", name) == MM_TEXT_NAME_UTF8);
    assert(mm_text_author_name("A\xed\xa0\x80", name) == MM_TEXT_NAME_UTF8);
    assert(mm_text_author_name("\xf0\x9f\x98\x80", name) == MM_TEXT_NAME_CHARACTERS);
    assert(mm_text_author_name("A\tB", name) == MM_TEXT_NAME_CHARACTERS);
    assert(!strcmp(name, "old"));
    assert(mm_text_author_name("\xc3\xa9 \xe2\x82\xac ?", name) == MM_TEXT_NAME_OK);
    assert(!strcmp(name, "\xe9 \x80 ?"));
    memset(long_name, 'X', sizeof(long_name) - 1);
    long_name[sizeof(long_name) - 1] = '\0';
    assert(mm_text_author_name(long_name, name) == MM_TEXT_NAME_LENGTH);
    long_name[MM_TEXT_NAME_BYTES - 1] = '\0';
    assert(mm_text_author_name(long_name, name) == MM_TEXT_NAME_OK && !strcmp(name, long_name));
    for (i = 0; i < MM_TEXT_NAME_BYTES - 1; ++i)
        memcpy(max_utf8 + 3 * i, "\xe2\x82\xac", 3);
    max_utf8[sizeof(max_utf8) - 1] = '\0';
    assert(mm_text_author_prepare_name(max_utf8, &authored) == MM_TEXT_NAME_OK);
    assert(!strcmp(max_utf8, authored.utf8) && strlen(authored.legacy) == MM_TEXT_NAME_BYTES - 1);
    assert(mm_text_author_prepare_name("", &authored) == MM_TEXT_NAME_EMPTY && !strcmp(max_utf8, authored.utf8));
}
static void names(void)
{
    Context c; MMTextNames n={NULL}; void *table; char out[256];
    init(&c); copy_string(out,"dirty"); assert(!mm_text_action_name(&n,0,out,&c.services) && !out[0] && !c.warnings);
    table=list_new(&c,0,0); c.names=&n; assert(mm_text_set_names(&n,table,&c.services) && c.names_published && ((Root *)table)->refs==2);
    assert(mm_text_action_name(&n,7,out,&c.services) && !strcmp(out,"Pulse"));
    assert(mm_text_action_name(&n,0,out,&c.services) && !strcmp(out,"Rest"));
    assert(!mm_text_action_name(&n,15,out,&c.services) && !out[0] && c.warnings==1);
    release(&c,table); release(&c,table); assert(!c.live);
}
int main(void) { lifecycle(); wire(); drafts(); author_names(); names(); return 0; }
