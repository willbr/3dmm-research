#include "mm/font_resource.h"
#include "engine_resource_block_fixture.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>
typedef struct Test Test;
typedef struct Model
{
    MMResourceObject binding;
    MMReference refs;
    Test *test;
} Model;
enum { INPUT_OFFSET = 19, OUTPUT_OFFSET = 4096, STORED_PACKED_BYTES = 12 };
enum { MODEL_TYPE = 0x424d444cUL };
struct Test
{
    MMFontResource font; MMFontResourceServices services;
    ResourceTestBlock block;
    MMResourceCache cache;
    MMReference cache_refs;
    MMResourceObject font_binding;
    unsigned char input[40],written[1000]; MMI32 input_size,written_size;
    int phase,fail,live,frees,object_live,invalidations,bad,order_bad,packed,author;
    Model models[3];
    int file_refs,decodes,children,adoptions,added,missing;
    MMFontChild kids[4]; MMFontChunk chunk;
    void *output; MMI32 size;
};
static Test *t(void *p) { return (Test *)p; }
static int checkpoint(Test *x) { return ++x->phase==x->fail; }
static void put(unsigned char *p,MMU32 v,int n,int big)
{ int i; for (i=0;i<n;++i) { p[big?n-1-i:i]=(unsigned char)v; v>>=8; } }
static MMU32 get(const unsigned char *p,int big)
{ MMU32 v=0; int i; for (i=0;i<4;++i) v=(v<<8)|p[big?i:3-i]; return v; }
static MMFontResource *state(void *p,void *object) { assert(p==object); return &t(p)->font; }
static void *create(void *p) { if (checkpoint(t(p))) return NULL; t(p)->object_live=1; return p; }
static void release(void *p,void *object)
{
    int i; Test *x=t(p);
    if (object==p) { assert(x->object_live); if (!x->output || x->output!=p) assert(x->invalidations==2); mm_font_dispose(&x->font,&x->services); x->object_live=0; return; }
    for (i=0;i<3;++i) if (object==&x->models[i]) break;
    assert(i<3 && x->models[i].refs.count==1);
    mm_resource_object_release(object, &x->cache.services.objects);
}
static int decode(void *p, void **handle)
{
    Test *x = t(p);
    assert(x->packed && resource_handle_size(NULL, *handle) == STORED_PACKED_BYTES);
    ++x->decodes;
    if (checkpoint(x))
        return 0;
    resource_block_decode(handle, x->input, (MMU32)x->input_size);
    return 1;
}
static int read_block(void *p, void *file, MMU32 offset, void *out, size_t bytes)
{
    Test *x = t(p);
    assert(file == p && x->file_refs && offset >= INPUT_OFFSET && offset - INPUT_OFFSET + bytes <= (size_t)x->input_size);
    if (checkpoint(x))
        return 0;
    memcpy(out, x->input + (offset - INPUT_OFFSET), bytes);
    return 1;
}
static int write_block(void *p, void *file, MMU32 offset, const void *data, size_t bytes)
{
    Test *x = t(p);
    assert(file == p && x->file_refs && offset >= OUTPUT_OFFSET && offset - OUTPUT_OFFSET + bytes <= (size_t)x->written_size);
    if (checkpoint(x))
        return 0;
    memcpy(x->written + (offset - OUTPUT_OFFSET), data, bytes);
    return 1;
}
static MMOwnedBlock *borrow_font(Test *x)
{
    MMBlockFile extent;
    extent.file = x;
    extent.offset = INPUT_OFFSET;
    extent.length = x->packed ? STORED_PACKED_BYTES : (MMU32)x->input_size;
    assert(mm_owned_block_set_file(&x->block.owner, &extent, x->packed) == MM_DATA_OK);
    return &x->block.owner;
}
static int allocate(void *p,MMI32 bytes,int clear,void **out)
{ assert(bytes>=0); if (checkpoint(t(p))) return 0; *out=malloc(bytes?bytes:1); assert(*out); memset(*out,clear?0:0xcc,bytes?bytes:1); ++t(p)->live; return 1; }
static void free_memory(void *p,void **out)
{
    Test *x=t(p); int i; ++x->frees;
    if (x->author) { assert(!x->invalidations && x->chunk.type==999 && x->chunk.number==888); for (i=0;i<3;++i) assert(!x->models[i].refs.count); }
    if (*out) { assert(x->live>0); --x->live; free(*out); *out=NULL; }
}
static void invalidate(void *p,void *out,size_t n) { ++t(p)->invalidations; memset(out,0xab,n); }
static void bad_font(void *p) { ++t(p)->bad; }
static void bad_order(void *p) { ++t(p)->order_bad; }
static MMI32 child_count(void *p,void *children) { assert(p==children); return t(p)->children; }
static void child_at(void *p,void *children,MMI32 i,MMFontChild *out) { assert(p==children && i>=0 && i<t(p)->children); *out=t(p)->kids[i]; }
static int add(void *p, void *file, MMI32 bytes, MMU32 type, MMU32 *number, MMOwnedBlock *block)
{
    Test *x = t(p);
    MMBlockFile extent;
    assert(p == file && type == MM_FONT_TYPE && bytes <= 1000);
    if (checkpoint(x))
        return 0;
    ++x->added;
    *number = 7;
    x->written_size = bytes;
    extent.file = file;
    extent.offset = OUTPUT_OFFSET;
    extent.length = (MMU32)bytes;
    return mm_owned_block_set_file(block, &extent, 0) == MM_DATA_OK;
}
static int adopt(void *p,void *f,MMU32 type,MMU32 number,const MMFontChild *child)
{ assert(p==f && type==MM_FONT_TYPE && number==7 && child->id<=105); if (checkpoint(t(p))) return 0; ++t(p)->adoptions; return 1; }
static MMI32 model_width(void *p,void *model) { return model==&t(p)->models[0]?0:model==&t(p)->models[1]?65536:196608; }
static MMI32 model_height(void *p,void *model) { return model==&t(p)->models[0]?0:model==&t(p)->models[1]?131072:262144; }
static int graph_entry(void *p, MMU32 position, MMChunkEntry *out)
{
    (void)p;
    assert(position == 0);
    memset(out, 0, sizeof(*out));
    out->type = MM_FONT_TYPE;
    out->number = 7;
    out->child_count = 2;
    return 1;
}
static int graph_child(void *p, MMU32 parent, MMU32 position, MMChunkChild *out)
{
    (void)p;
    assert(parent == 0 && position < 2);
    out->type = position ? MODEL_TYPE : 0x1234;
    out->number = position ? 12 : 11;
    out->slot = 65;
    return 1;
}
static MMChunkAccess chunks(void *file)
{
    MMChunkAccess access = {NULL, 1, graph_entry, graph_child};
    access.context = file;
    return access;
}
static void model_reader(void) {}
static int compare_reader(MMResourceReader a, MMResourceReader b) { assert(a == b); return 0; }
static MMResourceObject *resource_state(void *p) { return &((Model *)p)->binding; }
static MMReference *resource_refs(void *p) { return &((Model *)p)->refs; }
static void destroy_model(void *p)
{
    Model *m = p;
    assert(!m->refs.count);
    mm_resource_object_dispose(m, &m->test->cache.services.objects);
}
static void destroy_cache(void *p) { assert(mm_resource_cache_dispose(&t(p)->cache) == MM_STORAGE_OK); }
static void retain_file(void *p) { ++t(p)->file_refs; }
static void release_file(void *p) { assert(t(p)->file_refs > 0); --t(p)->file_refs; }
static void retain_block(void *p, void *file) { assert(p == file); retain_file(file); }
static void release_block(void *p, void *file) { assert(p == file); release_file(file); }
static int contains(void *p, MMU32 type, MMU32 number)
{
    (void)p;
    return (type == MODEL_TYPE || type == 0x1234) && number >= 10 && number < 13;
}
static int locate(void *p, MMU32 type, MMU32 number, MMBlockFile *out, int *packed)
{
    if (!contains(p, type, number))
        return 0;
    out->file = p;
    out->offset = 0;
    out->length = 0;
    *packed = 0;
    return 1;
}
static int read_model(void *p, MMResourceReader reader, MMU32 type, MMU32 number, MMOwnedBlock *block,
                      void **out, MMI32 *bytes)
{
    Test *x = t(p);
    Model *m;
    assert(reader == model_reader && contains(p, type, number) && block->file == p && out);
    if (checkpoint(x))
        return 0;
    m = &x->models[number - 10];
    assert(!m->binding.cache && !m->refs.count);
    m->test = x;
    mm_reference_init(&m->refs);
    *out = m;
    *bytes = 4;
    return 1;
}
static void missing_model(void *p,MMU32 id) { assert(id==66); ++t(p)->missing; }
static MMFontResourceServices setup(Test *x,int host,int other)
{
    MMFontResourceServices s;
    MMResourceServices resources = {0};
    MMResourceSource source = {0};
    int i,big=host!=other;
    memset(x,0,sizeof(*x)); memset(&s,0,sizeof(s));
    put(x->input,1,2,big); put(x->input+2,17,2,big); put(x->input+4,2,4,big); put(x->input+8,655360,4,big);
    for (i=0;i<4;++i) put(x->input+12+i*4,65536+(MMU32)i*65536,4,big);
    x->input_size=28; x->children=3; x->chunk.type=999; x->chunk.number=888;
    x->kids[0].type=x->kids[1].type=x->kids[2].type=x->kids[3].type=MODEL_TYPE;
    x->kids[0].number=10; x->kids[0].id=32; x->kids[1].number=11; x->kids[1].id=105; x->kids[2].number=11; x->kids[2].id=65; x->kids[3].number=12; x->kids[3].id=105;
    resource_block_init(&x->block, x, read_block, write_block);
    x->block.files.retain = retain_block;
    x->block.files.release = release_block;
    resources.context = x;
    resources.objects.state = resource_state;
    resources.objects.references = resource_refs;
    resources.objects.destroy = destroy_model;
    resources.compare_reader = compare_reader;
    resources.contains = contains;
    resources.live_chunks = chunks;
    resources.handles = x->block.owner.handles;
    resources.block_files = &x->block.files;
    resources.locate = locate;
    resources.read_legacy = read_model;
    resources.retain_file = retain_file;
    resources.release_file = release_file;
    resources.destroy_owner = destroy_cache;
    source.live = x;
    mm_reference_init(&x->cache_refs);
    assert(mm_resource_cache_init(&x->cache, x, &x->cache_refs, &source, 0, &resources, NULL, 1) == MM_STORAGE_OK);
    x->font_binding.cache = &x->cache;
    x->font_binding.type = MM_FONT_TYPE;
    x->font_binding.number = 7;
    s.context=x; s.host_big_endian=host; s.system=17; s.leading=32768; s.object_bytes=80;
    s.state=state; s.create=create; s.release=release; s.decode=decode;
    s.allocate=allocate; s.free_memory=free_memory; s.invalidate=invalidate; s.bad_font=bad_font; s.bad_order=bad_order;
    s.child_count=child_count; s.child_at=child_at; s.add=add; s.model_reader.key=model_reader; s.adopt=adopt;
    s.model_width=model_width; s.model_height=model_height; s.missing_model=missing_model;
    x->services=s; return s;
}
static void empty(Test *x)
{
    assert(!x->font.widths && !x->font.heights && !x->live && !x->object_live && !x->cache.count);
    assert(mm_owned_block_clear(&x->block.owner) == MM_DATA_OK && x->file_refs == 1);
    mm_resource_cache_release(&x->cache);
    assert(!x->file_refs);
}
int main(void)
{
    Test x; MMFontResourceServices s; int host,other,fail,ok,i; MMI32 value; void *model;
    for (host=0;host<2;++host) for (other=0;other<2;++other)
    {
        s=setup(&x,host,other); assert(mm_font_initialize(&x.font,borrow_font(&x),&s));
        assert(x.font.count==2 && x.font.maximum_height==655360 && x.live==2);
        assert(!memcmp(x.font.widths,x.input+12,8) && !memcmp(x.font.heights,x.input+20,8));
        memcpy(&value,x.input+16,4); assert(mm_font_width(&x.font,1)==value);
        memcpy(&value,x.input+24,4); assert(mm_font_height(&x.font,1)==value);
        mm_font_dispose(&x.font,&s); empty(&x); mm_font_dispose(&x.font,&s); assert(x.frees==4 && x.font.count==2);
    }
    for (fail=0;fail<=5;++fail)
    {
        s=setup(&x,0,0); x.fail=fail; ok=mm_font_initialize(&x.font,borrow_font(&x),&s); assert(ok==!fail);
        if (fail>=2) assert(x.font.count==2 && x.font.maximum_height==655360);
        if (fail==3 || fail==4) assert(x.font.widths && !x.font.heights);
        if (fail==5) assert(x.font.widths && x.font.heights);
        mm_font_dispose(&x.font,&s); empty(&x);
    }
    for (fail=0;fail<=6;++fail)
    {
        s=setup(&x,0,0); x.fail=fail;
        ok=mm_font_fetch(borrow_font(&x),&x.output,&x.size,&s); assert(ok==!fail);
        if (ok) { assert(x.output==&x && x.size==96); release(&x,&x); }
        else assert(x.invalidations==2 && x.size==(MMI32)-1414812757);
        empty(&x);
    }
    for (i=0;i<2;++i)
    {
        s=setup(&x,0,0); x.packed=i; x.fail=1;
        assert(mm_font_fetch(borrow_font(&x),NULL,&x.size,&s) && x.size==(i?2128:28) && !x.phase);
        assert(x.block.owner.view.packed==i && !x.decodes); empty(&x);
    }
    s=setup(&x,0,0); x.packed=1;
    assert(mm_font_fetch(borrow_font(&x),&x.output,&x.size,&s) && x.size==96 && x.decodes==1);
    assert(x.block.owner.view.kind==MM_BLOCK_MEMORY && x.file_refs==1); release(&x,&x); empty(&x);
    s=setup(&x,0,0); x.packed=1; x.fail=3;
    assert(!mm_font_fetch(borrow_font(&x),&x.output,&x.size,&s) && x.decodes==1 && x.invalidations==2);
    assert(x.block.owner.view.packed && x.file_refs==2); empty(&x);
    s=setup(&x,0,0); s.object_bytes=0x7fffffffL;
    assert(!mm_font_fetch(borrow_font(&x),&x.output,&x.size,&s) && x.bad==1 && x.invalidations==2); empty(&x);
    s=setup(&x,0,0); x.input_size=11;
    assert(!mm_font_initialize(&x.font,borrow_font(&x),&s) && x.bad==1 && !x.font.count); empty(&x);
    s=setup(&x,0,0); x.input_size=32;
    assert(!mm_font_initialize(&x.font,borrow_font(&x),&s) && x.bad==1 && x.font.count==2 && !x.font.maximum_height); empty(&x);
    for (i=0;i<2;++i)
    {
        s=setup(&x,0,0); put(x.input+4,i?0x10000000UL:0xffffffffUL,4,0);
        assert(!mm_font_initialize(&x.font,borrow_font(&x),&s) && x.bad==1 && !x.live); empty(&x);
    }
    s=setup(&x,0,0); x.input[0]=3;
    assert(!mm_font_initialize(&x.font,borrow_font(&x),&s) && x.order_bad==1 && x.bad==1); empty(&x);
    s=setup(&x,0,0); put(x.input+4,0,4,0); x.input_size=12;
    assert(mm_font_initialize(&x.font,borrow_font(&x),&s) && x.live==2); mm_font_dispose(&x.font,&s); empty(&x);
    for (fail=0;fail<=13;++fail)
    {
        s=setup(&x,0,0); x.author=1; x.fail=fail; ok=mm_font_author(&x.cache,&x,&x.chunk,&s); assert(ok==!fail);
        assert(!x.live && x.frees==2 && !x.models[0].refs.count && !x.models[1].refs.count);
        if (ok) { assert(x.chunk.type==MM_FONT_TYPE && x.chunk.number==7 && x.adoptions==3 && x.written_size==860);
            assert(get(x.written+4,0)==106 && get(x.written+8,0)==163840);
            assert(get(x.written+12+32*4,0)==65536 && get(x.written+12+424+32*4,0)==163840); }
        else { assert(x.invalidations==1); if (fail>=4) assert(x.added==1);
            assert(x.adoptions==(fail<=5?0:fail<=8?1:fail<=10?2:3)); }
        if (fail>=11) assert(get(x.written+4,0)==(fail==11?0U:106U));
        if (fail>=12) assert(get(x.written+12+32*4,0)==(fail==12?0U:65536U) && !get(x.written+12+424+32*4,0));
        empty(&x);
    }
    s=setup(&x,0,0); x.author=1; x.children=0;
    assert(mm_font_author(&x.cache,&x,&x.chunk,&s) && x.written_size==20 && get(x.written+4,0)==1 &&
           !get(x.written+8,0) && !get(x.written+12,0) && !get(x.written+16,0)); empty(&x);
    s=setup(&x,0,0); x.author=1; x.children=2; x.kids[1]=x.kids[2];
    assert(mm_font_author(&x.cache,&x,&x.chunk,&s) && !get(x.written+12+32*4,0)); empty(&x);
    s=setup(&x,0,0); x.author=1; x.children=4;
    assert(mm_font_author(&x.cache,&x,&x.chunk,&s) && get(x.written+12+32*4,0)==196608 && get(x.written+8,0)==294912); empty(&x);
    s=setup(&x,0,0); x.author=1; x.kids[0].id=0xffffffffUL;
    assert(!mm_font_author(&x.cache,&x,&x.chunk,&s) && !x.phase && !x.live && !x.added); empty(&x);
    s=setup(&x,1,0); x.author=1; x.children=0;
    assert(mm_font_author(&x.cache,&x,NULL,&s) && x.written[0]==0 && x.written[1]==1 && get(x.written+4,1)==1 && !x.invalidations); empty(&x);
    s=setup(&x,0,0); x.author=1; x.fail=5;
    assert(!mm_font_author(&x.cache,&x,NULL,&s) && !x.live && !x.models[0].refs.count && !x.invalidations); empty(&x);
    s=setup(&x,0,0); x.fail=1;
    assert(!mm_font_model(&x.font_binding,65,&s) && !x.missing);
    model=mm_font_model(&x.font_binding,65,&s);
    assert(model==&x.models[1] && x.models[1].refs.count==1); release(&x,model);
    assert(!mm_font_model(&x.font_binding,66,&s) && x.missing==1); empty(&x);
    return 0;
}
