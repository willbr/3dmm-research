#include "mm/text_resource.h"
#include "mm/chunk_ops.h"
#include "mm/collections.h"
#include "mm/material_resource.h"
#include "engine_resource_block_fixture.h"
#include "template_writer_fixture.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>
typedef struct Context
{
    MMTextResourceServices services;
    MMTemplateOwnerHost template_host;
    MMMaterialOwnerHost material_host;
    unsigned char wire[24];
    ResourceTestBlock block;
    MMChunkPayloadServices payloads;
    MMResourceCache cache;
    MMTextResourceSource source;
    MMI32 bytes; int fail,step,live,header,child,found,packed,unpacked,appended;
    int file_refs;
    int error,released_error,bad;
} Context;
static MMTextGenerated *generated(void *context, void *owner)
{
    (void)context;
    return mm_template_owner_generated(owner);
}
static MMI32 length(void *context, void *owner)
{
    const MMStringValue name = mm_template_owner_name(owner);
    (void)context;
    return name.unit == 1 ? *(const unsigned char *)name.buffer : *(const unsigned short *)name.buffer;
}
static MMTemplateState *template_state(void *context, void *owner)
{
    (void)context;
    return mm_template_owner_state(owner);
}
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
static void *material_new(void *context)
{
    Context *c = context;
    return mm_material_owner_create(&c->material_host, MM_MATERIAL);
}
static void retain_material(void *context, void *material)
{
    (void)context;
    mm_material_owner_retain(material);
}
static void release_material(void *context, void *material)
{
    (void)context;
    mm_material_owner_release(material);
}
static int read_template(void *context, MMTemplateOwner *owner, MMU32 type, MMU32 number, MMOwnedBlock *block)
{
    Context *c = context;
    const MMStringValue name = mm_template_owner_name(owner);
    assert(block == &c->block.owner);
    (void)type;
    (void)number;
    mm_string_value_set(&name, "loaded", 6);
    return c->header;
}
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
static void *tag_list(void *p) { assert(!((Context *)p)->error); return resize_shape(p, NULL, 0, 1, MM_STORAGE_NORMAL); }
static void release_tag_list(void *p, void *list)
{
    Context *c = p;
    c->released_error = c->error;
    free_shape(p, list, 1);
}
static int append_tag(void *p,void *list,const MMTag *t) { (void)list; assert(!t->cache); ++((Context *)p)->appended; return ((Context *)p)->child!=2; }
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
    g->material_new=material_new; g->retain_material=retain_material; g->release_material=release_material;
    c->template_host.allocator = &g->table_allocator;
    c->template_host.name_unit = 1;
    c->material_host.objects = &g->table_allocator;
    s->release_tag_list=release_tag_list; s->read_template=read_template;
    s->payloads=&c->payloads;
    s->publish_error=publish_error; s->tag_list=tag_list; s->append_tag=append_tag; s->bad_order=bad;
}
static MMTemplateOwner *create_text(Context *c, const char *text, MMI32 shape, const MMTag *font)
{
    unsigned char storage[MM_STRING_MAX + 2];
    MMStringValue name = {0};
    name.buffer = storage;
    name.unit = 1;
    mm_string_value_set(&name, text, (MMI32)strlen(text));
    return mm_text_create(&c->template_host, &name, shape, font, NULL, &c->services.generation);
}

static struct MMFontOwner *test_font(void *context, const MMTag *font)
{
    (void)context;
    (void)font;
    return NULL;
}

static void lifecycle(void)
{
    const MMActionDefinition empty = {0};
    const MMTag replacement = {456, NULL, 0x54444620U, 15};
    const unsigned short wide_old[] = {'o', 'l', 'd'};
    const unsigned short wide_space = ' ';
    Context c;
    MMTag font = {123, NULL, 0x54444620U, 29};
    MMTemplateFontProvider provider = {0};
    MMStringValue value = {0}, name;
    unsigned short storage[MM_STRING_MAX + 2];
    const void *old_text, *space;
    MMTemplateOwner *owner, *copy;
    MMTextGenerated *parts;
    MMTextDescriptor *descriptor;
    int failure, create_steps, replace_steps;
    unsigned unit;
    font.cache = &font;
    provider.context = &c;
    provider.fetch = test_font;
    value.buffer = storage;
    for (unit = 1; unit <= 2; ++unit)
    {
        value.unit = unit;
        old_text = unit == 1 ? (const void *)"old" : (const void *)wide_old;
        space = unit == 1 ? (const void *)" " : (const void *)&wide_space;
        mm_string_value_clear(&value);
        create_steps = 0;
        for (failure = 0; failure <= create_steps; ++failure)
        {
            init(&c);
            c.template_host.name_unit = unit;
            c.fail = failure;
            owner = mm_text_create(&c.template_host, &value, 9, &font, &provider, &c.services.generation);
            assert((owner != NULL) == (failure == 0));
            if (owner)
            {
                create_steps = c.step;
                name = mm_template_owner_name(owner);
                assert(mm_string_value_equal(&name, space, 1));
                mm_template_owner_release(owner);
            }
            assert(!c.live);
        }
        init(&c);
        c.template_host.name_unit = unit;
        mm_string_value_set(&value, old_text, 3);
        owner = mm_text_create(&c.template_host, &value, 9, &font, &provider, &c.services.generation);
        assert(owner);
        name = mm_template_owner_name(owner);
        descriptor = mm_template_owner_descriptor(owner);
        parts = mm_template_owner_generated(owner);
        c.step = 0;
        assert(mm_text_change(owner, NULL, -1, NULL, &c.services.generation));
        replace_steps = c.step;
        parts->action = mm_action_resource_create(&empty, NULL);
        assert(parts->action);
        parts->action_id = 7;
        mm_string_value_clear(&value);
        for (failure = 1; failure <= replace_steps; ++failure)
        {
            const MMPartIndexList *parents = mm_template_owner_state(owner)->parents;
            const MMActionResource *action = parts->action;
            const void *material = parts->material;
            c.step = 0;
            c.fail = failure;
            assert(!mm_text_change(owner, &value, 0, &replacement, &c.services.generation));
            assert(mm_string_value_equal(&name, old_text, 3) && descriptor->shape == 9);
            assert(descriptor->font.cache == font.cache && descriptor->font.sid == font.sid &&
                   descriptor->font.type == font.type && descriptor->font.number == font.number);
            assert(mm_template_owner_state(owner)->parents == parents && parts->action == action &&
                   parts->action_id == 7 && parts->material == material);
        }
        c.fail = 0;
        assert(mm_text_change(owner, &value, 0, NULL, &c.services.generation));
        assert(mm_string_value_equal(&name, space, 1) && !parts->action && parts->action_id == -1);
        copy = mm_text_duplicate(owner, &c.services.generation);
        assert(copy && mm_template_owner_generated(copy)->material != parts->material);
        assert(mm_template_owner_font(copy)->fetch == provider.fetch && mm_template_owner_font(copy)->context == &c);
        name = mm_template_owner_name(copy);
        descriptor = mm_template_owner_descriptor(copy);
        assert(mm_string_value_equal(&name, space, 1) && !descriptor->shape && descriptor->font.cache == font.cache);
        mm_template_owner_release(copy);
        mm_template_owner_release(owner);
        assert(!c.live);
    }
}
static void wire(void)
{
    Context c; MMTemplateOwner *o,*loaded; void *tags; MMTag font={-1,NULL,0xfedcba98U,0x87654321U}; MMU32 number; int endian,failure,error;
    TemplateWriterFixture output;
    MMArchiveWriter writer;
    for (endian=0;endian<2;++endian)
    {
        MMStringValue loaded_name;
        init(&c); c.services.host_big_endian=endian; o=create_text(&c,"saved",9,&font); assert(o);
        writer=writer_fixture(&output,endian,MM_TEXT_WINDOWS);
        assert(mm_text_write_owner(o, MM_TMPL, &number, &writer) && number==0 && !output.archive.file_refs);
        memcpy(c.wire,output.descriptor,sizeof(c.wire));
        assert(c.wire[endian?1:0]==1 && c.wire[2]==3 && c.wire[3]==3);
        assert(!c.wire[12] && !c.wire[13] && !c.wire[14] && !c.wire[15]);
        c.source.cache = &c.cache;
        for (failure=0;failure<2;++failure)
        {
            MMTextDescriptor *descriptor;
            loaded=mm_template_owner_create(&c.template_host,1); c.services.host_big_endian=endian^failure;
            c.packed = failure;
            assert(mm_text_read(loaded,&c.source,MM_TMPL,37,&c.block.owner,&c.services) && !c.file_refs);
            descriptor=mm_template_owner_descriptor(loaded);
            assert(descriptor->shape==9 && descriptor->font.sid==-1 && descriptor->font.type==font.type && descriptor->font.number==font.number && !descriptor->font.cache);
            mm_template_owner_release(loaded);
        }
        c.source.cache = NULL;
        for (failure=0;failure<7;++failure)
        {
            c.step=0; c.fail=failure==1?1:0; c.child=failure==2?0:failure==6?2:1; c.found=failure!=3; c.unpacked=failure!=4; c.bytes=failure==5?23:100;
            c.packed = failure == 4;
            error=-1; c.released_error=-1; tags=mm_text_tags(&c.source,MM_TMPL,37,&error,&c.services);
            assert((tags!=NULL)==!failure && error==(failure!=0) && !c.file_refs);
            if (tags) release_tag_list(&c,tags); else if (failure!=1) assert(c.released_error==1);
        }
        c.fail=0; c.bytes=24; c.child=1; c.found=1;
        c.header=0; c.found=1; loaded=mm_template_owner_create(&c.template_host,1);
        assert(!mm_text_read(loaded,&c.source,MM_TMPL,37,&c.block.owner,&c.services));
        loaded_name=mm_template_owner_name(loaded);
        assert(mm_string_value_equal(&loaded_name,"loaded",6));
        mm_template_owner_release(loaded);
        c.header=1; c.wire[0]=c.wire[1]=0x7f; loaded=mm_template_owner_create(&c.template_host,1);
        assert(mm_text_read(loaded,&c.source,MM_TMPL,37,&c.block.owner,&c.services) && c.bad==1 && !c.file_refs);
        mm_template_owner_release(loaded);
        mm_template_owner_release(o); assert(!c.live);
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
    MMStringValue name;
    MMTextDescriptor draft = {9, {123, NULL, 0x54444620UL, 29}};
    MMTemplateState header;
    MMU32 number;
    MMTemplateOwner *object;
    int big, failure;
    for (big = 0; big < 2; ++big)
    {
        init(&c);
        c.services.host_big_endian = big;
        object = create_text(&c, "saved", draft.shape, &draft.font);
        assert(object);
        name = mm_template_owner_name(object);
        for (failure = 1; failure <= 6; ++failure)
        {
            writer = writer_fixture(&normal, big, MM_TEXT_WINDOWS);
            normal.fail = failure;
            number = 99;
            assert(!mm_text_write_owner(object, MM_TMPL, &number, &writer));
            assert(number == (failure < 3 ? 99U : 0U) && !normal.archive.file_refs);
            assert(normal.archive.index.count == (failure >= 4 ? 1U : 0U));
            if (failure >= 4)
                assert(normal.archive.index.rows[0].entry.type == MM_TMPL && !normal.archive.index.rows[0].entry.child_count);
            if (failure >= 5)
                assert(normal.deleted == 1 && normal.last_deleted == MM_TDT);
            writer = writer_fixture(&saved, big, MM_TEXT_WINDOWS);
            fixture_chunk_add(&saved.archive.index, MM_GLPI, 0);
            saved.fail = failure;
            number = 99;
            assert(!mm_text_write_draft(&name, &draft, &number, &writer));
            assert(number == 99 && saved.archive.index.count == 1 && !saved.archive.file_refs);
            assert(saved.archive.index.rows[0].entry.type == MM_GLPI);
            assert(saved.deleted == (failure == 1 ? 0 : failure >= 5 ? 2 : 1));
        }
        writer = writer_fixture(&normal, big, MM_TEXT_WINDOWS);
        assert(mm_text_write_owner(object, MM_TMPL, &number, &writer));
        writer = writer_fixture(&saved, big, MM_TEXT_WINDOWS);
        assert(mm_text_write_draft(&name, &draft, &number, &writer) && number == 0);
        mm_template_owner_release(object);
        assert(mm_template_decode_header(saved.header, 0, &header));
        assert(!header.x && !header.y && !header.z && header.flags == MM_TEMPLATE_TEXT);
        assert(!memcmp(saved.header, normal.header, sizeof(saved.header)));
        assert(!memcmp(saved.descriptor, normal.descriptor, sizeof(saved.descriptor)));
        assert(!memcmp(saved.archive.name, "\003\003\005saved", 9));
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
static void missing(void *context)
{
    ++*(int *)context;
}

static void name_word(unsigned char *bytes, MMU32 word, int big)
{
    unsigned i;
    for (i = 0; i < 4; ++i)
        bytes[big ? 3 - i : i] = (unsigned char)(word >> (8 * i));
}

static void names(void)
{
    static const unsigned char strings[] = "\005Pulse\004Rest\011duplicate\001\216\003A\000B\000";
    static const MMU32 offsets[] = {0, 6, 11, 21, 23, 27};
    static const MMI32 ids[] = {7, 0, 7, -1, -2, 1};
    unsigned char bytes[20 + sizeof(strings) - 1 + sizeof(ids) / sizeof(ids[0]) * 8];
    unsigned char out[257];
    MMCollectionHeader header = {0};
    MMMemoryStream source;
    MMTextNames *catalogue = NULL;
    MMStringValue value = {0};
    int big, warnings = 0;
    MMU32 i;
    value.buffer = out;
    value.unit = 1;
    out[0] = 5;
    assert(!mm_text_action_name(catalogue, 0, &value, missing, &warnings) && !out[0] && !warnings);
    header.kind = MM_COLLECTION_STRINGS;
    header.system = MM_TEXT_MAC;
    header.width = 8;
    header.count = sizeof(ids) / sizeof(ids[0]);
    header.bytes = sizeof(strings) - 1;
    header.free_count = -1;
    for (big = 0; big <= 1; ++big)
    {
        unsigned char *entries = bytes + 20 + header.bytes;
        header.big_endian = big;
        assert(mm_collection_header_write(&header, bytes, sizeof(bytes)) == MM_STORAGE_OK);
        memcpy(bytes + 20, strings, header.bytes);
        for (i = 0; i < header.count; ++i)
        {
            name_word(entries + i * 8, offsets[i], big);
            name_word(entries + i * 8 + 4, (MMU32)ids[i], big);
        }
        assert(mm_memory_stream_init(&source, bytes, sizeof(bytes)));
        assert(mm_text_names_read(&catalogue, &source.stream, MM_TEXT_WINDOWS, NULL, NULL));
        name_word(entries, header.bytes, big);
        assert(!mm_text_names_read(&catalogue, &source.stream, MM_TEXT_WINDOWS, NULL, NULL));
        assert(mm_memory_stream_init(&source, bytes, sizeof(bytes) - 1));
        assert(!mm_text_names_read(&catalogue, &source.stream, MM_TEXT_WINDOWS, NULL, NULL));
        memset(bytes, 0, sizeof(bytes));
        assert(mm_text_action_name(catalogue, 7, &value, missing, &warnings) && !memcmp(out, "\005Pulse", 7));
        assert(mm_text_action_name(catalogue, 0, &value, missing, &warnings) && !memcmp(out, "\004Rest", 6));
        assert(mm_text_action_name(catalogue, -1, &value, missing, &warnings) && !memcmp(out, "\001\351", 3));
        assert(mm_text_action_name(catalogue, -2, &value, missing, &warnings) && !memcmp(out, "\003A\000B", 5));
        assert(mm_text_action_name(catalogue, 1, &value, missing, &warnings) && !out[0]);
        assert(!mm_text_action_name(catalogue, 15, &value, missing, &warnings) && !out[0] && warnings == big + 1);
    }
    mm_text_names_clear(&catalogue);
    assert(!catalogue && !mm_text_action_name(catalogue, 7, &value, missing, &warnings) && warnings == 2);
}
int main(void) { lifecycle(); wire(); drafts(); author_names(); names(); return 0; }
