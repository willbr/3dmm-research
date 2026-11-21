#include "mm/template_create.h"
#include "engine_resource_block_fixture.h"
#include "template_writer_fixture.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(c) do { if (!(c)) { fprintf(stderr,"template check %d: %s\n",__LINE__,#c); exit(1); } } while (0)
typedef struct Ref { int refs,kind,set; } Ref;
typedef struct List { int count; unsigned char data[12]; } List;
enum { TEMPLATE_HEADER, TEMPLATE_PARENTS, TEMPLATE_SETS, TEMPLATE_COSTUMES };
enum { FILE_BASE = 8, FILE_STRIDE = 64 };
typedef struct Block { MMU32 length; unsigned char data[60]; } Block;
typedef struct Fixture
{
    MMTemplateCreateServices s; MMTemplateState t; Ref object,cache,body,customs[60]; List parents,sets; Block blocks[4];
    void *tables[4]; int table_blocks,table_buffer_failure;
    MMResourceCache source;
    ResourceTestBlock block;
    MMChunkChild children[68];
    MMU32 child_count;
    int file,fail,fallback,other,text,gap,custom_count,child_calls,created,initialized;
    int file_refs, packed, decodes, read_failure;
    int fetched,applied,freed,body_created,body_released,default_calls,diagnostics[5],invalidated;
    MMI32 applied_ids[2];
} Fixture;
static void put(unsigned char *p,unsigned n,MMU32 v,int big)
{ unsigned i; for (i=0;i<n;++i) { p[big?n-i-1:i]=(unsigned char)(v&255U); v>>=8; } }
static MMU32 word(const unsigned char *p,unsigned n,int big)
{ MMU32 v=0; unsigned i; for (i=0;i<n;++i) v=(v<<8)|p[big?i:n-i-1]; return v; }
static MMTemplateState *template_state(void *p,void *o) { Fixture *f=p; CHECK(o==&f->object); return &f->t; }
static void release(void *p,void *o)
{
    Fixture *f=p; Ref *r=o; CHECK(r && r->refs>0); --r->refs;
    if (o==&f->object) { CHECK(!r->refs); mm_template_dispose(o,&f->s); }
    else if (o==&f->body) ++f->body_released;
}
static int graph_entry(void *p, MMU32 position, MMChunkEntry *out)
{
    Fixture *f = p;
    CHECK(position == 0);
    memset(out, 0, sizeof(*out));
    out->type = MM_TMPL;
    out->number = 1;
    out->child_count = f->child_count;
    return 1;
}
static int graph_child(void *p, MMU32 parent, MMU32 position, MMChunkChild *out)
{
    Fixture *f = p;
    CHECK(parent == 0 && position < f->child_count);
    ++f->child_calls;
    *out = f->children[position];
    return 1;
}
static void add_link(Fixture *f, MMU32 slot, MMU32 type, MMU32 number)
{
    MMChunkChild *link = &f->children[f->child_count++];
    link->slot = slot;
    link->type = type;
    link->number = number;
}
static MMChunkAccess live_chunks(void *file)
{
    Fixture *f = file;
    MMChunkAccess chunks;
    MMI32 i;
    f->child_count = 0;
    add_link(f, 0, MM_ACTN, 10);
    add_link(f, 0, MM_CMTL, 10);
    if (!f->fallback)
        add_link(f, 0, MM_GGCM, 10);
    add_link(f, 0, MM_GLBS, 10);
    add_link(f, 0, MM_GLPI, 10);
    if (f->text)
        add_link(f, 0, MM_TDT, 10);
    for (i = 1; i < f->custom_count; ++i)
    {
        if (i == 2)
        {
            add_link(f, 2, MM_ACTN, 12);
            if (f->gap)
                continue;
        }
        add_link(f, (MMU32)i, MM_CMTL, 10U + (MMU32)i);
    }
    if (f->gap)
        add_link(f, (MMU32)f->custom_count, MM_CMTL, 12);
    chunks.context = f;
    chunks.count = 1;
    chunks.entry = graph_entry;
    chunks.child = graph_child;
    return chunks;
}
static void retain_file(void *p, void *file)
{
    Fixture *f = p;
    CHECK(file == p);
    ++f->file_refs;
}
static void release_file(void *p, void *file)
{
    Fixture *f = p;
    CHECK(file == p && f->file_refs > 0);
    --f->file_refs;
}
static int block_read(void *p, void *file, MMU32 offset, void *out, size_t n)
{
    Fixture *f = p;
    MMU32 kind = (offset - FILE_BASE) / FILE_STRIDE;
    MMU32 start = (offset - FILE_BASE) % FILE_STRIDE;
    CHECK(file == p && f->file_refs && offset >= FILE_BASE && kind < 4);
    CHECK(start <= f->blocks[kind].length && n <= f->blocks[kind].length - start);
    if ((int)kind == f->read_failure || (kind == TEMPLATE_HEADER && f->fail == 18))
        return 0;
    memcpy(out, f->blocks[kind].data + start, n);
    return 1;
}
static int block_write(void *p, void *file, MMU32 offset, const void *bytes, size_t n)
{
    (void)p; (void)file; (void)offset; (void)bytes; (void)n;
    return 0;
}
static int decode(void *p, void **handle)
{
    Fixture *f = p;
    CHECK(resource_handle_size(NULL, *handle) == 12);
    ++f->decodes;
    if (f->fail == 17)
        return 0;
    resource_block_decode(handle, f->blocks[TEMPLATE_HEADER].data, MM_TEMPLATE_HEADER_BYTES);
    return 1;
}
static int read_name(void *p,void *o,MMU32 type,MMU32 number)
{ Fixture *f=p; CHECK(o==&f->object && type==MM_TMPL && number==1); CHECK(f->t.x==0x1234 && f->t.flags==4); return f->fail!=16; }
static void *create(void *p,int text) { Fixture *f=p; CHECK(text==f->text); ++f->created; if (f->fail==11) return NULL; f->object.refs=1; return &f->object; }
static int initialize(void *p,void *o,MMU32 type,MMU32 number,MMOwnedBlock *block)
{
    Fixture *f=p; ++f->initialized; if (f->fail==12) return 0;
    if (f->text) { f->t.flags=2; return 1; }
    CHECK(block==&f->block.owner);
    return mm_template_initialize(o,&f->source,type,number,block,&f->s);
}
static MMI32 object_size(void *p,int text) { (void)p; return text?200:100; }
static void invalidate(void *p,void *out,size_t n) { Fixture *f=p; CHECK(!f->object.refs); memset(out,0xa5,n); ++f->invalidated; }
static void *resize_table(void *p,void *data,MMU32 old_bytes,MMU32 bytes,int priority)
{
    Fixture *f=p;
    int kind=!f->t.parents?TEMPLATE_PARENTS:!f->t.sets?TEMPLATE_SETS:TEMPLATE_COSTUMES;
    int record=f->tables[kind]==NULL;
    void *result;
    (void)priority;
    if (record)
    {
        CHECK(!data && !old_bytes);
        CHECK(bytes==(kind==TEMPLATE_COSTUMES?sizeof(MMTemplateCostumeTable):sizeof(MMPartIndexList)));
        if (f->fail==2*kind || (kind==TEMPLATE_COSTUMES && f->fail==8))
            return NULL;
    }
    else if (kind==f->table_buffer_failure)
        return NULL;
    result=realloc(data,bytes);
    CHECK(result);
    if (!old_bytes) ++f->table_blocks;
    if (record) f->tables[kind]=result;
    return result;
}
static void free_table(void *p,void *data,MMU32 bytes)
{
    Fixture *f=p;
    int kind;
    CHECK(data && bytes && f->table_blocks>0);
    for (kind=TEMPLATE_PARENTS;kind<=TEMPLATE_COSTUMES;++kind)
        if (data==f->tables[kind])
        {
            const MMCollectionStorage *storage;
            if (kind==TEMPLATE_COSTUMES)
            {
                storage=&((MMTemplateCostumeTable *)data)->storage;
                CHECK(!f->t.costumes || (f->t.costumes==data && !f->t.parents && !f->t.sets));
            }
            else
            {
                storage=&((MMPartIndexList *)data)->storage;
                if (kind==TEMPLATE_SETS)
                    CHECK(!f->t.sets || (f->t.sets==data && !f->t.parents));
            }
            CHECK(!storage->buffers[0].data && !storage->buffers[1].data);
            f->tables[kind]=NULL;
        }
    --f->table_blocks;
    free(data);
}
static void encode_part_indices(Block *block, const List *source, int big)
{
    block->length = 12 + (MMU32)source->count * 2;
    memset(block->data, 0, block->length);
    put(block->data, 2, 1, big);
    put(block->data + 4, 4, 2, big);
    put(block->data + 8, 4, (MMU32)source->count, big);
    memcpy(block->data + 12, source->data, (size_t)source->count * 2);
}
static void *create_cache(void *p) { Fixture *f=p; if (f->fail==7) return NULL; ++f->cache.refs; return &f->cache; }
static void encode_costumes(Block *block, int big)
{
    unsigned char *wire = block->data;
    int i;
    block->length = 60;
    memset(wire, 0, block->length);
    put(wire, 2, 1, big);
    put(wire + 4, 4, 2, big);
    put(wire + 8, 4, 24, big);
    put(wire + 12, 4, 0xffffffffUL, big);
    put(wire + 16, 4, 4, big);
    for (i = 0; i < 2; ++i)
    {
        put(wire + 20 + i * 12, 4, 2, big);
        put(wire + 24 + i * 12, 4, (MMU32)i, big);
        put(wire + 28 + i * 12, 4, (MMU32)i + 2, big);
        put(wire + 44 + i * 8, 4, (MMU32)i * 12, big);
        put(wire + 48 + i * 8, 4, 12, big);
    }
}
static int locate(void *file, MMU32 type, MMU32 number, MMBlockFile *out, int *packed)
{
    Fixture *f = file;
    int kind, big = f->other != f->s.host_big_endian;
    if (type == MM_TMPL)
    {
        CHECK(number == 1);
        kind = TEMPLATE_HEADER;
    }
    else
    {
        CHECK(number == 10);
        if (type == MM_GLPI && f->fail != 1)
        {
            kind = TEMPLATE_PARENTS;
            encode_part_indices(&f->blocks[kind], &f->parents, big);
        }
        else if (type == MM_GLBS && f->fail != 3)
        {
            kind = TEMPLATE_SETS;
            encode_part_indices(&f->blocks[kind], &f->sets, big);
        }
        else if (type == MM_GGCM && f->fail != 5)
        {
            kind = TEMPLATE_COSTUMES;
            encode_costumes(&f->blocks[kind], big);
        }
        else
            return 0;
    }
    *packed = kind == TEMPLATE_HEADER && (f->packed || f->fail == 17);
    out->file = f;
    out->offset = FILE_BASE + (MMU32)kind * FILE_STRIDE;
    out->length = *packed ? 12 : f->blocks[kind].length;
    return 1;
}
static MMOwnedBlock *borrow_root(Fixture *f)
{
    MMBlockFile extent;
    int packed;
    CHECK(locate(f, MM_TMPL, 1, &extent, &packed));
    CHECK(mm_owned_block_set_file(&f->block.owner, &extent, packed) == MM_DATA_OK);
    return &f->block.owner;
}
static void *fetch_custom(void *p,void *cache,MMU32 type,MMU32 number)
{
    Fixture *f=p; CHECK(cache==&f->cache && f->cache.refs==1 && type==MM_CMTL && number>=10 && number<70);
    ++f->fetched; if (f->fail==9 && f->fetched==6) return NULL; ++f->customs[number-10].refs; return &f->customs[number-10];
}
static MMI32 custom_set(void *p,void *c) { (void)p; return ((Ref *)c)->set; }
static int append(void *p,MMTemplateCostumeTable *group,const MMI32 *ids,MMI32 n)
{
    Fixture *f=p; CHECK(group==f->t.costumes && group->rows<2 && n<=50);
    if (f->fail==10 && group->rows==1) return 0;
    return mm_template_costumes_append(group, ids, n);
}
static void *create_body(void *p,const MMPartIndexView *parents,const MMPartIndexView *sets)
{ Fixture *f=p; CHECK(parents==&f->t.parents->view && sets==&f->t.sets->view); ++f->body_created; if (f->fail==15) return NULL; f->body.refs=1; return &f->body; }
static int default_costume(void *p,void *o,void *body)
{ Fixture *f=p; ++f->default_calls; return mm_template_default_costume(o,body,&f->s); }
static MMI32 costume_id(void *p,void *o,MMI32 set,MMI32 index)
{
    Fixture *f=p;
    MMI32 id;
    CHECK(o==&f->object && !index && mm_template_costumes_id(f->t.costumes,set,index,&id));
    return id;
}
static void *custom(void *p,void *o,MMI32 id)
{ Fixture *f=p; CHECK(o==&f->object && id>=0 && id<4); ++f->fetched; if (f->fail==14 && id==1) return NULL; ++f->customs[id].refs; return &f->customs[id]; }
static int allocate(void *p,MMI32 n,void **out)
{ Fixture *f=p; *out=NULL; CHECK(n==f->t.set_count); if (f->fail==13) return 0; *out=calloc((size_t)n+1,sizeof(void *)); CHECK(*out); return 1; }
static void apply(void *p,void *body,void *c)
{
    Fixture *f=p;
    CHECK(body==&f->body && f->fetched==f->t.set_count && ((Ref *)c)->refs==1 && f->applied<2);
    f->applied_ids[f->applied++]=(MMI32)((Ref *)c-f->customs);
}
static void free_memory(void *p,void **data) { Fixture *f=p; int i; void **refs=*data; for (i=0;i<f->t.set_count;++i) CHECK(!refs[i]); free(*data); *data=NULL; ++f->freed; }
static void diagnose(void *p,int n) { CHECK(n>=0 && n<5); ++((Fixture *)p)->diagnostics[n]; }
static void setup(Fixture *f,int other,int host_big)
{
    int i; memset(f,0,sizeof(*f)); f->other=other; f->s.host_big_endian=host_big; f->custom_count=4;
    f->cache.kind=4; f->body.kind=5;
    f->parents.count=f->sets.count=6;
    f->blocks[TEMPLATE_HEADER].length = MM_TEMPLATE_HEADER_BYTES;
    f->read_failure = f->table_buffer_failure = -1;
    resource_block_init(&f->block, f, block_read, block_write);
    f->block.files.retain = retain_file;
    f->block.files.release = release_file;
    f->source.file = f;
    f->source.services.handles = f->block.owner.handles;
    f->source.services.block_files = &f->block.files;
    f->source.services.locate = locate;
    f->source.services.live_chunks = live_chunks;
    put(f->blocks[0].data,2,1,other); put(f->blocks[0].data+4,2,0x1234,other); put(f->blocks[0].data+6,2,0xabcd,other);
    put(f->blocks[0].data+8,2,0x8000,other); put(f->blocks[0].data+12,4,4,other);
    for (i=0;i<6;++i) { put(f->parents.data+i*2,2,i?(MMU32)(i-1):65535U,other!=host_big); put(f->sets.data+i*2,2,(MMU32)i%2,other!=host_big); }
    for (i=0;i<60;++i) { f->customs[i].kind=6; f->customs[i].set=i%2; }
    f->s.context=f; f->s.state=template_state; f->s.release=release; f->s.release_custom=release; f->s.decode=decode; f->s.read_name=read_name;
    f->s.create=create; f->s.initialize=initialize;
    f->s.object_size=object_size; f->s.invalidate=invalidate;
    f->s.table_allocator.context=f; f->s.table_allocator.resize=resize_table; f->s.table_allocator.release=free_table;
    f->s.create_cache=create_cache; f->s.fetch_custom=fetch_custom; f->s.custom_set=custom_set;
    f->s.append_costumes=append; f->s.create_body=create_body; f->s.release_body=release; f->s.default_costume=default_costume; f->s.costume_id=costume_id;
    f->s.custom=custom; f->s.allocate_refs=allocate; f->s.apply_custom=apply; f->s.free_memory=free_memory; f->s.diagnostic=diagnose;
}
static void disposed(Fixture *f)
{
    int i; mm_template_dispose(&f->object,&f->s);
    CHECK(mm_owned_block_clear(&f->block.owner) == MM_DATA_OK && !f->file_refs);
    CHECK(!f->table_blocks && !f->tables[1] && !f->tables[2] && !f->tables[3] && !f->cache.refs);
    CHECK(!f->t.parents && !f->t.sets && !f->t.costumes);
    for (i=0;i<60;++i) CHECK(!f->customs[i].refs);
}
int main(void)
{
    Fixture f; int other,host,failure,i,ok; void *out,*body; MMI32 bytes, id; MMU32 number;
    TemplateWriterFixture written;
    MMArchiveWriter writer;
    const MMChunkNameValue name = {"Template", 8, 8};
    memset(&f.t,0xa5,sizeof(f.t)); mm_template_state_init(&f.t);
    CHECK(!f.t.parents && !f.t.sets && !f.t.costumes);
    CHECK(!f.t.set_count && !f.t.custom_count && !f.t.action_count);
    CHECK(!f.t.x && !f.t.y && !f.t.z && !f.t.flags);
    for (other=0;other<2;++other) for (host=0;host<2;++host)
    {
        setup(&f,other,host);
        CHECK(mm_template_fetch(&f.source,MM_TMPL,1,borrow_root(&f),NULL,&bytes,&f.s));
        CHECK(bytes==16 && !f.created && !f.child_calls);
        out=NULL; CHECK(mm_template_fetch(&f.source,MM_TMPL,1,&f.block.owner,&out,&bytes,&f.s));
        CHECK(out==&f.object && bytes==208 && f.t.custom_count==4 && f.t.action_count==1 && f.t.set_count==2);
        CHECK(mm_part_index_get(&f.t.parents->view,0)==-1 && mm_part_index_get(&f.t.parents->view,5)==4);
        CHECK(mm_part_index_get(&f.t.sets->view,5)==1);
        CHECK(mm_template_costumes_count(f.t.costumes,0)==2 && mm_template_costumes_id(f.t.costumes,1,1,&id) && id==3);
        put(f.t.costumes->storage.buffers[0].data + 4,4,2,f.t.costumes->big_endian);
        body=mm_template_create_body(&f.object,&f.s); CHECK(body==&f.body && f.applied==2 && f.freed==1); release(&f,body);
        CHECK(f.applied_ids[0]==2 && f.applied_ids[1]==1);
        writer=writer_fixture(&written,host,0x0303);
        number=777; CHECK(mm_template_write_header(&f.t,&name,MM_TMPL,&number,&writer));
        CHECK(number==0 && !written.header[10] && !written.header[11] &&
              word(written.header+4,2,host)==0x1234 && word(written.header+12,4,host)==4);
        CHECK(!memcmp(written.archive.name,"Template",8) && !written.archive.pins);
        release(&f,&f.object); disposed(&f);
    }
    for (failure=1;failure<=10;++failure)
    {
        setup(&f,0,0); f.fail=failure; f.fallback=failure>=7;
        CHECK(!mm_template_initialize(&f.object,&f.source,MM_TMPL,1,NULL,&f.s));
        CHECK((f.t.parents!=NULL)==(failure>2)); CHECK((f.t.sets!=NULL)==(failure>4));
        if (failure>=9) CHECK(f.t.costumes==f.tables[TEMPLATE_COSTUMES] && f.t.costumes->rows==1);
        CHECK(!f.cache.refs); disposed(&f);
    }
    for (failure=11;failure<=12;++failure)
    {
        unsigned char expected[sizeof(void *)]; setup(&f,0,0); f.fail=failure; out=&f.file; memset(expected,0xa5,sizeof(expected));
        CHECK(!mm_template_fetch(&f.source,MM_TMPL,1,borrow_root(&f),&out,&bytes,&f.s));
        CHECK(!memcmp(&out,expected,sizeof(out)) && f.invalidated==2 && !f.object.refs); disposed(&f);
    }
    setup(&f,0,0); f.text=1; out=NULL;
    CHECK(mm_template_fetch(&f.source,MM_TMPL,1,borrow_root(&f),&out,&bytes,&f.s));
    CHECK(bytes==200 && !f.t.parents && !f.t.sets); release(&f,out); disposed(&f);
    for (failure=13;failure<=15;++failure)
    {
        setup(&f,0,0); CHECK(mm_template_initialize(&f.object,&f.source,MM_TMPL,1,NULL,&f.s)); f.fail=failure;
        CHECK(!mm_template_create_body(&f.object,&f.s) && !f.applied);
        CHECK(f.body_released==(failure!=15) && f.default_calls==(failure!=15)); disposed(&f);
    }
    for (failure=16;failure<=18;++failure)
    { setup(&f,0,0); f.fail=failure; CHECK(!mm_template_read_header(&f.object,&f.source,MM_TMPL,1,NULL,&f.s)); CHECK(f.t.flags==(failure==16?4U:0U)); disposed(&f); }
    for (failure=0;failure<=3;++failure)
    {
        setup(&f,0,0); number=777; writer=writer_fixture(&written,0,0x0303);
        fixture_chunk_add(&written.archive.index,MM_TMPL,0)->runtime=123;
        written.fail=failure;
        CHECK(mm_template_write_header(&f.t,&name,0x44415441U,&number,&writer)==!failure);
        CHECK(number==(failure==1 || failure==2 ? 777U : 0U));
        CHECK(written.archive.index.count==(failure?1U:2U));
        if (failure>=2) CHECK(written.deleted==1 && written.last_deleted==0x44415441U);
        CHECK(fixture_chunk_runtime(&written.archive.index,MM_TMPL,0)==(failure?123:0));
        if (!failure) CHECK(!memcmp(written.archive.name,"Template",8));
    }
    setup(&f,0,0); f.fallback=1; f.gap=1;
    CHECK(mm_template_initialize(&f.object,&f.source,MM_TMPL,1,NULL,&f.s)); CHECK(f.t.custom_count==2 && f.t.costumes->rows==2 && f.fetched==8);
    f.fetched=0;
    body=mm_template_create_body(&f.object,&f.s);
    CHECK(body==&f.body && f.applied==2 && f.applied_ids[0]==0 && f.applied_ids[1]==1);
    release(&f,body); disposed(&f);
    for (i=50;i<=51;++i)
    {
        setup(&f,0,0); f.fallback=1; f.custom_count=i; f.sets.count=f.parents.count=1; put(f.sets.data,2,0,0);
        for (failure=0;failure<i;++failure) f.customs[failure].set=0;
        ok=mm_template_initialize(&f.object,&f.source,MM_TMPL,1,NULL,&f.s); CHECK(ok==(i==50));
        CHECK(f.diagnostics[MM_TEMPLATE_COSTUME_LIMIT]==(i==51)); if (ok) CHECK(mm_template_costumes_count(f.t.costumes,0)==50); disposed(&f);
    }
    setup(&f,0,0);
    f.packed = 1;
    CHECK(mm_template_fetch(&f.source, MM_TMPL, 1, borrow_root(&f), NULL, &bytes, &f.s));
    CHECK(bytes == 12 && f.block.owner.view.packed && !f.decodes && !f.child_calls);
    CHECK(mm_template_fetch(&f.source, MM_TMPL, 1, &f.block.owner, &out, &bytes, &f.s));
    CHECK(bytes == 208 && f.decodes == 1 && f.block.owner.view.kind == MM_BLOCK_MEMORY && !f.file_refs);
    release(&f, out);
    disposed(&f);
    for (failure=0;failure<2;++failure) for (i = TEMPLATE_PARENTS; i <= TEMPLATE_COSTUMES; ++i)
    {
        setup(&f,0,0);
        if (failure) f.table_buffer_failure=i;
        else f.read_failure=i;
        CHECK(!mm_template_initialize(&f.object, &f.source, MM_TMPL, 1, NULL, &f.s));
        CHECK((f.t.parents != NULL) == (i > TEMPLATE_PARENTS));
        CHECK((f.t.sets != NULL) == (i > TEMPLATE_SETS));
        CHECK(!f.t.costumes && !f.tables[TEMPLATE_COSTUMES] && !f.file_refs && !f.diagnostics[MM_TEMPLATE_BUILD_COSTUMES]);
        disposed(&f);
    }
    puts("C89 template construction, header and default costume checks passed"); return 0;
}
