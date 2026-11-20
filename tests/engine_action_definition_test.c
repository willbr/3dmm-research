#include "mm/action_definition.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>

typedef struct Block { unsigned char bytes[108]; size_t length; } Block;
typedef struct Test Test;
typedef struct TableOwner
{
    MMActionTable table;
    Test *test;
    int kind, live;
} TableOwner;
struct Test
{
    MMActionDefinition state;
    MMActionDefinitionServices services;
    TableOwner tables[4];
    Block header, blocks[3];
    int phase, fail, object_live, invalidations, bad, optional_absent, header_lookups;
    int released[4], releases, clears, publishes;
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
    (void)old_size;
    if (priority == MM_STORAGE_SPEED || checkpoint((Test *)p))
        return NULL;
    return realloc(bytes, size);
}
static void free_bytes(void *p, void *bytes, MMU32 size)
{ (void)p; (void)size; free(bytes); }

static void release_table(void *p)
{
    TableOwner *owner=p;
    Test *x=owner->test;
    assert(owner->live);
    owner->live=0;
    x->released[x->releases++]=owner->kind;
}

static MMActionTable *new_table(void *p, int kind)
{
    Test *x=p;
    TableOwner *owner=&x->tables[kind];
    MMStorageAllocator allocator={NULL,resize,free_bytes};
    if (checkpoint(x))
        return NULL;
    assert(!owner->live && x->state.flags==0x8000000dUL);
    owner->test=x;
    owner->kind=kind;
    owner->live=1;
    allocator.context=x;
    mm_action_table_init(&owner->table,&allocator,owner,release_table);
    return &owner->table;
}

static void *find(void *p, void *file, MMU32 type, MMU32 number)
{
    Test *x=p;
    assert(file==p);
    if (type==0x4143544eUL) ++x->header_lookups;
    if (checkpoint(x)) return NULL;
    if (type==0x4143544eUL) { assert(number==7); return &x->header; }
    assert(number<3 && type==mm_action_table_type((int)number));
    return &x->blocks[number];
}
static int unpack(void *p, void *block) { (void)block; return !checkpoint((Test *)p); }
static MMI32 block_size(void *p, void *block, int packed)
{ assert(block==&((Test *)p)->header); return packed?88:(MMI32)((Block *)block)->length; }
static int block_read(void *p, void *block, void *out, size_t n)
{
    assert(block==&((Test *)p)->header && n==8);
    if (checkpoint((Test *)p)) return 0;
    memcpy(out,((Block *)block)->bytes,n);
    return 1;
}
static int child(void *p, void *file, MMU32 type, MMU32 number, MMU32 ct, MMU32 *out)
{
    Test *x=p;
    assert(file==p && type==0x4143544eUL && number==7);
    if (checkpoint(x)) return 0;
    *out=ct==MM_ACTION_GGCL?0:ct==MM_ACTION_GLXF?1:2;
    return *out!=2 || !x->optional_absent;
}
static int read_bytes(void *p, MMU32 offset, void *out, size_t bytes)
{
    const Block *block=p;
    if (offset>block->length || bytes>block->length-offset)
        return 0;
    memcpy(out,block->bytes+offset,bytes);
    return 1;
}
static MMReadStream stream(void *p, void *block)
{
    MMReadStream s={NULL,0,read_bytes};
    (void)p;
    s.context=block;
    s.length=(MMU32)((Block *)block)->length;
    return s;
}
static void clear_type(void *p, void *out)
{ ++((Test *)p)->clears; ((MMTag *)out)->type=0; }
static void publish(void *p, void *out, const MMTag *tag)
{ ++((Test *)p)->publishes; assert(!((MMTag *)out)->type); *(MMTag *)out=*tag; }
static int initialize(void *p, void *object, void *file, MMU32 type, MMU32 number)
{
    assert(p==object);
    return mm_action_initialize(&((Test *)p)->state,file,type,number,NULL,&((Test *)p)->services);
}
static MMI32 object_size(void *p) { (void)p; return 80; }
static void invalidate(void *p, void *out, size_t n)
{ ++((Test *)p)->invalidations; memset(out,0xab,n); }
static void bad_order(void *p) { ++((Test *)p)->bad; }

static void setup(Test *x, int header_big, int big, MMU32 count)
{
    MMActionDefinitionServices *s=&x->services;
    unsigned char *row;
    MMU32 i,j;
    memset(x,0,sizeof(*x));
    x->header.length=8;
    x->header.bytes[header_big?1:0]=1;
    put(x->header.bytes+4,0x8000000dUL,header_big);

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
    s->context=x; s->host_big_endian=header_big; s->state=state; s->create=create; s->release=release;
    s->find=find; s->unpack=unpack; s->block_size=block_size; s->block_read=block_read;
    s->child=child; s->stream=stream; s->new_table=new_table;
    s->clear_sound_type=clear_type; s->publish_sound=publish;
    s->object_size=object_size; s->invalidate=invalidate; s->bad_order=bad_order;
}

static void empty(Test *x)
{
    int i;
    assert(!x->state.cels && !x->state.matrices && !x->state.sounds && !x->object_live);
    for (i=0;i<4;++i) assert(!x->tables[i].live);
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
    MMReadStream input;
    void *out;
    MMI32 size;
    int host,big,fail,ok,i;
    for (host=0;host<2;++host) for (big=0;big<2;++big)
    {
        setup(&x,host,big,2);
        assert(initialize(&x,&x,&x,0x4143544eUL,7) && x.state.flags==0x8000000dUL);
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
        assert(x.publishes==2 && x.clears==4);
        mm_action_dispose(&x.state); empty(&x);
        assert(x.releases==4 && x.released[0]==2 && x.released[1]==0 && x.released[2]==1 && x.released[3]==3);
        mm_action_dispose(&x.state); assert(x.releases==4 && x.state.flags==0x8000000dUL);
    }
    for (fail=0;fail<=21;++fail)
    {
        setup(&x,0,0,2); x.fail=fail;
        ok=initialize(&x,&x,&x,0x4143544eUL,7);
        assert(ok==(fail==0 || fail==15));
        if (fail>=4) assert(x.state.flags==0x8000000dUL);
        if (fail>=10) assert(x.state.cels);
        if (fail>=15) assert(x.state.matrices);
        assert(!x.tables[2].live);
        mm_action_dispose(&x.state); empty(&x);
    }
    for (fail=0;fail<=21;++fail)
    {
        setup(&x,0,0,2); x.fail=fail; out=NULL; size=-77;
        ok=mm_action_fetch(&x,0x4143544eUL,7,&x.header,&out,&size,&x.services);
        assert(!x.header_lookups && ok==(fail==0 || fail==15));
        if (ok) { assert(out==&x && size==256); release(&x,&x); }
        else assert(x.invalidations==2 && size==(MMI32)-1414812757);
        empty(&x);
    }
    for (fail=0;fail<=1;++fail)
    {
        setup(&x,0,native_big,2);
        mm_action_table_init(&cels,NULL,NULL,NULL);
        mm_action_table_init(&matrices,NULL,NULL,NULL);
        input=stream(&x,&x.blocks[0]); assert(mm_action_table_read(&cels,&input,MM_ACTION_CELS)==MM_MOVIE_OK);
        input=stream(&x,&x.blocks[1]); assert(mm_action_table_read(&matrices,&input,MM_ACTION_MATRICES)==MM_MOVIE_OK);
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

    setup(&x,0,0,2); x.fail=1;
    assert(mm_action_fetch(&x,0x4143544eUL,7,&x.header,NULL,&size,&x.services) && size==88 && !x.phase);
    setup(&x,0,0,2); x.header.length=7;
    assert(!initialize(&x,&x,&x,0x4143544eUL,7) && !x.state.flags); empty(&x);
    setup(&x,0,0,2); --x.blocks[1].length;
    assert(!initialize(&x,&x,&x,0x4143544eUL,7) && x.state.cels && !x.state.matrices);
    mm_action_dispose(&x.state); empty(&x);
    setup(&x,0,1,2); put(x.blocks[0].bytes+52,32,1);
    assert(initialize(&x,&x,&x,0x4143544eUL,7));
    words[0]=99;
    assert(!mm_action_get_cel(&x.state,0,words) && words[0]==99 && mm_action_part_count(&x.state,0)==-1);
    mm_action_dispose(&x.state); empty(&x);
    setup(&x,0,1,0); x.header.bytes[0]=3;
    assert(initialize(&x,&x,&x,0x4143544eUL,7) && x.bad==1 && x.state.flags==0x8000000dUL);
    assert(x.state.sounds && !x.state.sounds->header.count);
    mm_action_dispose(&x.state); empty(&x);
    setup(&x,0,0,2); x.optional_absent=1;
    assert(initialize(&x,&x,&x,0x4143544eUL,7) && !x.state.sounds);
    tag.sid=77; tag.cache=&x; tag.type=99; tag.number=88; mm_action_get_sound(&x.state,0,&tag,&x.services);
    assert(tag.sid==77 && tag.cache==&x && !tag.type && tag.number==88 && !x.publishes);
    mm_action_dispose(&x.state); empty(&x);
    return 0;
}
