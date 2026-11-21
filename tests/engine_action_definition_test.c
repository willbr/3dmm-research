#include "mm/action_definition.h"
#include "engine_resource_block_fixture.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>

enum { ACTION_TYPE = 0x4143544eUL, ACTION_NUMBER = 7 };
enum { HEADER = 3, STORED_PACKED_BYTES = 12, FILE_BASE = 17, FILE_STRIDE = 128 };
typedef struct Block { unsigned char bytes[108]; MMU32 length; } Block;
typedef struct Test Test;
struct Test
{
    MMActionDefinition state;
    MMActionDefinitionServices services;
    Block blocks[4];
    ResourceTestBlock root;
    MMResourceCache source;
    int phase, fail, object_live, invalidations, bad, optional_absent, header_lookups;
    int lookup_phase[4], packed_kind, decodes, file_refs;
    int allocated, released[3], releases, clears, publishes;
};

static int checkpoint(Test *x) { return ++x->phase == x->fail; }
static void put(unsigned char *p, MMU32 n, int big)
{
    int i;
    for (i=0;i<4;++i) { p[big?3-i:i]=(unsigned char)n; n>>=8; }
}
static MMActionDefinition *state(void *p, void *object)
{ assert(p==object); return &((Test *)p)->state; }

static void *create(void *p)
{
    Test *x=p;
    if (checkpoint(x)) return NULL;
    assert(!x->object_live);
    x->object_live=1;
    return p;
}

static void release(void *p, void *object)
{
    Test *x=p;
    assert(p==object && x->object_live && !x->invalidations);
    mm_action_dispose(&x->state);
    x->object_live=0;
}

static void *resize(void *p, void *bytes, MMU32 old_size, MMU32 size, int priority)
{
    Test *x=p;
    void *result;
    assert(x->state.flags==0x8000000dUL);
    if (priority == MM_STORAGE_SPEED || checkpoint(x))
        return NULL;
    result=realloc(bytes,size);
    assert(result);
    if (!old_size)
        ++x->allocated;
    return result;
}
static void free_bytes(void *p, void *bytes, MMU32 size)
{
    Test *x=p;
    assert(bytes && size && x->allocated>0);
    if (bytes==x->state.cels)
        x->released[x->releases++]=MM_ACTION_CELS;
    if (bytes==x->state.matrices)
        x->released[x->releases++]=MM_ACTION_MATRICES;
    if (bytes==x->state.sounds)
        x->released[x->releases++]=MM_ACTION_SOUNDS;
    --x->allocated;
    free(bytes);
}

static int table_blocks(const MMActionTable *table)
{
    return table ? 1+(table->storage.buffers[0].data!=NULL)+(table->storage.buffers[1].data!=NULL) : 0;
}

static int graph_entry(void *p, MMU32 position, MMChunkEntry *out)
{
    Test *x = p;
    assert(position == 0);
    memset(out, 0, sizeof(*out));
    out->type = ACTION_TYPE;
    out->number = ACTION_NUMBER;
    out->child_count = x->optional_absent ? 2 : 3;
    return 1;
}
static int graph_child(void *p, MMU32 parent, MMU32 position, MMChunkChild *out)
{
    static const MMChunkChild links[] = {
        {MM_ACTION_GGCL, MM_ACTION_CELS, 0},
        {MM_ACTION_GLMS, MM_ACTION_WIRE_SOUNDS, 0},
        {MM_ACTION_GLXF, MM_ACTION_MATRICES, 0}
    };
    Test *x = p;
    assert(parent == 0 && position < (x->optional_absent ? 2U : 3U));
    if (position && x->optional_absent)
        ++position;
    *out = links[position];
    return 1;
}
static MMChunkAccess live_chunks(void *file)
{
    MMChunkAccess chunks = {NULL, 1, graph_entry, graph_child};
    chunks.context = file;
    return chunks;
}
static void extent(Test *x, int kind, MMBlockFile *out, int *packed)
{
    out->file = x;
    out->offset = FILE_BASE + (MMU32)kind * FILE_STRIDE;
    *packed = kind == x->packed_kind;
    out->length = *packed ? STORED_PACKED_BYTES : x->blocks[kind].length;
}
static int locate(void *file, MMU32 type, MMU32 number, MMBlockFile *out, int *packed)
{
    Test *x = file;
    int kind;
    if (type == ACTION_TYPE)
    {
        assert(number == ACTION_NUMBER);
        ++x->header_lookups;
        kind = HEADER;
    }
    else
    {
        assert(number < 3 && type == mm_action_table_type((int)number));
        kind = (int)number;
    }
    x->lookup_phase[kind] = x->phase + 1;
    if (checkpoint(x))
        return 0;
    extent(x, kind, out, packed);
    return 1;
}
static MMOwnedBlock *borrow_root(Test *x)
{
    MMBlockFile file;
    int packed;
    extent(x, HEADER, &file, &packed);
    assert(mm_owned_block_set_file(&x->root.owner, &file, packed) == MM_DATA_OK);
    return &x->root.owner;
}
static void retain_file(void *p, void *file)
{
    Test *x = p;
    assert(file == p);
    ++x->file_refs;
}
static void release_file(void *p, void *file)
{
    Test *x = p;
    assert(file == p && x->file_refs);
    --x->file_refs;
}
static int block_read(void *p, void *file, MMU32 offset, void *out, size_t count)
{
    Test *x = p;
    MMU32 kind = (offset - FILE_BASE) / FILE_STRIDE;
    MMU32 start = (offset - FILE_BASE) % FILE_STRIDE;
    assert(file == p && x->file_refs && offset >= FILE_BASE && kind < 4);
    if (checkpoint(x))
        return 0;
    if ((int)kind == x->packed_kind)
    {
        assert(!start && count == STORED_PACKED_BYTES);
        memset(out, 0, count);
        *(unsigned char *)out = (unsigned char)kind;
    }
    else
    {
        assert(start <= x->blocks[kind].length && count <= x->blocks[kind].length - start);
        memcpy(out, x->blocks[kind].bytes + start, count);
    }
    return 1;
}
static int block_write(void *p, void *file, MMU32 offset, const void *bytes, size_t count)
{
    (void)p; (void)file; (void)offset; (void)bytes; (void)count;
    return 0;
}
static int decode(void *p, void **handle)
{
    Test *x = p;
    unsigned kind = *(const unsigned char *)resource_handle_lock(NULL, *handle);
    assert(kind < 4 && (int)kind == x->packed_kind && resource_handle_size(NULL, *handle) == STORED_PACKED_BYTES);
    ++x->decodes;
    if (checkpoint(x))
        return 0;
    resource_block_decode(handle, x->blocks[kind].bytes, x->blocks[kind].length);
    return 1;
}
static void clear_type(void *p, void *out)
{ ++((Test *)p)->clears; ((MMTag *)out)->type=0; }
static void publish(void *p, void *out, const MMTag *tag)
{ ++((Test *)p)->publishes; assert(!((MMTag *)out)->type); *(MMTag *)out=*tag; }
static int initialize(Test *x)
{
    return mm_action_initialize(&x->state, &x->source, ACTION_TYPE, ACTION_NUMBER, NULL, &x->services);
}
static MMI32 object_size(void *p) { (void)p; return 80; }
static void invalidate(void *p, void *out, size_t n)
{ Test *x=p; assert(!x->object_live); ++x->invalidations; memset(out,0xab,n); }
static void bad_order(void *p) { ++((Test *)p)->bad; }

static void setup(Test *x, int header_big, int big, MMU32 count)
{
    MMActionDefinitionServices *s=&x->services;
    unsigned char *row;
    MMU32 i,j;
    memset(x,0,sizeof(*x));
    x->packed_kind = -1;
    x->blocks[HEADER].length=8;
    x->blocks[HEADER].bytes[header_big?1:0]=1;
    put(x->blocks[HEADER].bytes+4,0x8000000dUL,header_big);

    x->blocks[0].length=20+24*count;
    x->blocks[0].bytes[big?1:0]=1;
    put(x->blocks[0].bytes+4,count,big);
    put(x->blocks[0].bytes+8,count*16,big);
    put(x->blocks[0].bytes+12,0xffffffffUL,big);
    put(x->blocks[0].bytes+16,8,big);
    x->blocks[1].length=12+48*count;
    x->blocks[1].bytes[big?1:0]=1;
    put(x->blocks[1].bytes+4,48,big);
    put(x->blocks[1].bytes+8,count,big);
    x->blocks[2].length=12+16*count;
    x->blocks[2].bytes[big?1:0]=1;
    put(x->blocks[2].bytes+4,16,big);
    put(x->blocks[2].bytes+8,count,big);
    for (i=0;i<count;++i)
    {
        row=x->blocks[0].bytes+20+i*16;
        put(row,i,big);
        put(row+4,100+i,big);
        for (j=0;j<8;++j) row[8+j]=(unsigned char)(i*8+j+3);
        row=x->blocks[0].bytes+20+count*16+i*8;
        put(row,i*16,big);
        put(row+4,16,big);
        for (j=0;j<12;++j) put(x->blocks[1].bytes+12+i*48+j*4,100*i+j,big);
        row=x->blocks[2].bytes+12+i*16;
        put(row,i?0:0x80000001UL,big);
        put(row+4,0xfedcba98UL,big);
        put(row+8,0x4d534e44UL,big);
        put(row+12,123+i,big);
    }
    resource_block_init(&x->root, x, block_read, block_write);
    x->root.files.retain = retain_file;
    x->root.files.release = release_file;
    x->source.file = x;
    x->source.services.handles = x->root.owner.handles;
    x->source.services.block_files = &x->root.files;
    x->source.services.locate = locate;
    x->source.services.live_chunks = live_chunks;
    s->context=x; s->host_big_endian=header_big; s->state=state; s->create=create; s->release=release;
    s->decode=decode;
    s->table_allocator.context=x; s->table_allocator.resize=resize; s->table_allocator.release=free_bytes;
    s->clear_sound_type=clear_type; s->publish_sound=publish;
    s->object_size=object_size; s->invalidate=invalidate; s->bad_order=bad_order;
}

static void empty(Test *x)
{
    assert(!x->state.cels && !x->state.matrices && !x->state.sounds && !x->object_live);
    assert(!x->allocated);
    assert(mm_owned_block_clear(&x->root.owner) == MM_DATA_OK && !x->file_refs);
}

int main(void)
{
    const unsigned short one=1;
    const int native_big=*(const unsigned char *)&one==0;
    Test x;
    MMTag tag;
    MMU32 words[12];
    unsigned short part[2];
    unsigned char *first;
    MMActionTable cels,matrices;
    MMMemoryStream input;
    void *out;
    MMI32 size;
    int host,big,fail,i,steps,flags_at,cels_at,matrices_at;
    for (host=0;host<2;++host) for (big=0;big<2;++big)
    {
        setup(&x,host,big,2);
        assert(initialize(&x) && x.state.flags==0x8000000dUL);
        assert(mm_action_cel_count(&x.state)==2 && mm_action_part_count(&x.state,1)==2);
        assert(mm_action_get_cel(&x.state,1,words) && words[0]==1 && words[1]==101);
        assert(mm_action_get_part(&x.state,1,1,part));
        assert(part[0]==(big?0x0f10:0x100f) && part[1]==(big?0x1112:0x1211));
        assert(mm_action_get_matrix(&x.state,1,words) && words[0]==100 && words[11]==111);
        assert(!mm_action_get_part(&x.state,0,2,part) && !mm_action_get_matrix(&x.state,2,words));
        for (i=0;i<2;++i)
        {
            assert(mm_action_sound_tag(x.state.sounds,(MMU32)i,&tag));
            assert(tag.sid==(i?0:(MMI32)-2147483647) && !tag.cache && tag.type==0x4d534e44UL && tag.number==123+(MMU32)i);
        }
        for (i=0;i<4;++i)
        {
            first=x.state.cels->storage.buffers[0].data;
            put(first,i==0?0:i==1?1:i==2?0xffffffffUL:0x80000000UL,big);
            tag.sid=77; tag.cache=&x; tag.type=99; tag.number=88;
            mm_action_get_sound(&x.state,0,&tag,&x.services);
            if (i<2) assert(!tag.cache && tag.type==0x4d534e44UL && tag.number==123+(MMU32)i);
            else assert(tag.sid==77 && tag.cache==&x && !tag.type && tag.number==88);
        }
        assert(x.publishes==2 && x.clears==4 && x.allocated==7);
        mm_action_dispose(&x.state); empty(&x);
        assert(x.releases==3 && x.released[0]==MM_ACTION_CELS && x.released[1]==MM_ACTION_MATRICES && x.released[2]==MM_ACTION_SOUNDS);
        mm_action_dispose(&x.state); assert(x.releases==3 && x.state.flags==0x8000000dUL);
    }
    setup(&x,0,0,2);
    assert(initialize(&x));
    steps = x.phase;
    flags_at = x.lookup_phase[MM_ACTION_CELS];
    cels_at = x.lookup_phase[MM_ACTION_MATRICES];
    matrices_at = x.lookup_phase[MM_ACTION_WIRE_SOUNDS];
    mm_action_dispose(&x.state); empty(&x);
    for (fail=1;fail<=steps;++fail)
    {
        setup(&x,0,0,2); x.fail=fail;
        assert(!initialize(&x));
        assert(x.state.flags==(fail>=flags_at?0x8000000dUL:0));
        assert((x.state.cels!=NULL)==(fail>=cels_at));
        assert((x.state.matrices!=NULL)==(fail>=matrices_at));
        assert(x.allocated==table_blocks(x.state.cels)+table_blocks(x.state.matrices)+table_blocks(x.state.sounds));
        mm_action_dispose(&x.state); empty(&x);
    }
    setup(&x,0,0,2);
    assert(mm_action_fetch(&x.source,ACTION_TYPE,ACTION_NUMBER,borrow_root(&x),&out,&size,&x.services));
    assert(out==&x && size==256 && !x.header_lookups);
    steps = x.phase;
    release(&x,&x); empty(&x);
    for (fail=1;fail<=steps;++fail)
    {
        setup(&x,0,0,2); x.fail=fail; out=NULL; size=-77;
        assert(!mm_action_fetch(&x.source,ACTION_TYPE,ACTION_NUMBER,borrow_root(&x),&out,&size,&x.services));
        assert(!x.header_lookups && x.invalidations==2 && size==(MMI32)-1414812757);
        empty(&x);
    }
    for (fail=0;fail<=1;++fail)
    {
        setup(&x,0,native_big,2);
        assert(mm_action_table_init(&cels,NULL));
        assert(mm_action_table_init(&matrices,NULL));
        assert(mm_memory_stream_init(&input,x.blocks[0].bytes,x.blocks[0].length));
        assert(mm_action_table_read(&cels,&input.stream,MM_ACTION_CELS)==MM_MOVIE_OK);
        assert(mm_memory_stream_init(&input,x.blocks[1].bytes,x.blocks[1].length));
        assert(mm_action_table_read(&matrices,&input.stream,MM_ACTION_MATRICES)==MM_MOVIE_OK);
        x.fail=fail;
        out=mm_action_create(&cels,&matrices,0x8000000dUL,&x.services);
        assert((out!=NULL)==!fail);
        if (out)
        {
            assert(x.state.cels==&cels && x.state.matrices==&matrices);
            assert(mm_action_get_matrix(&x.state,1,words) && words[0]==100);
            release(&x,out);
        }
        else
        {
            assert(cels.storage.buffers[0].data && matrices.storage.buffers[0].data);
            mm_action_table_dispose(&cels);
            mm_action_table_dispose(&matrices);
        }
        empty(&x);
    }

    setup(&x,0,0,2);
    x.packed_kind = HEADER;
    assert(mm_action_fetch(&x.source,ACTION_TYPE,ACTION_NUMBER,borrow_root(&x),NULL,&size,&x.services));
    assert(size==STORED_PACKED_BYTES && !x.phase && !x.decodes && x.root.owner.view.packed);
    assert(mm_action_fetch(&x.source,ACTION_TYPE,ACTION_NUMBER,&x.root.owner,&out,&size,&x.services));
    assert(out==&x && size==256 && x.decodes==1 && x.root.owner.view.kind==MM_BLOCK_MEMORY && !x.file_refs);
    release(&x,&x); empty(&x);
    setup(&x,0,0,2);
    x.packed_kind = HEADER; x.fail = 2;
    assert(!mm_action_fetch(&x.source,ACTION_TYPE,ACTION_NUMBER,borrow_root(&x),&out,&size,&x.services));
    assert(x.decodes==1 && x.invalidations==2 && x.root.owner.view.packed && x.file_refs==1); empty(&x);
    setup(&x,0,0,2);
    x.packed_kind = MM_ACTION_MATRICES;
    assert(initialize(&x) && x.decodes==1 && x.state.matrices && x.state.sounds);
    mm_action_dispose(&x.state); empty(&x);
    setup(&x,0,0,2); x.blocks[HEADER].length=7;
    assert(!initialize(&x) && !x.state.flags); empty(&x);
    setup(&x,0,0,2); --x.blocks[1].length;
    assert(!initialize(&x) && x.state.cels && !x.state.matrices);
    mm_action_dispose(&x.state); empty(&x);
    setup(&x,0,1,2); put(x.blocks[0].bytes+52,32,1);
    assert(initialize(&x));
    words[0]=99;
    assert(!mm_action_get_cel(&x.state,0,words) && words[0]==99 && mm_action_part_count(&x.state,0)==-1);
    mm_action_dispose(&x.state); empty(&x);
    setup(&x,0,1,0); x.blocks[HEADER].bytes[0]=3;
    assert(initialize(&x) && x.bad==1 && x.state.flags==0x8000000dUL);
    assert(x.state.sounds && !x.state.sounds->header.count);
    mm_action_dispose(&x.state); empty(&x);
    setup(&x,0,0,2); x.optional_absent=1;
    assert(initialize(&x) && !x.state.sounds);
    tag.sid=77; tag.cache=&x; tag.type=99; tag.number=88; mm_action_get_sound(&x.state,0,&tag,&x.services);
    assert(tag.sid==77 && tag.cache==&x && !tag.type && tag.number==88 && !x.publishes);
    mm_action_dispose(&x.state); empty(&x);
    return 0;
}
