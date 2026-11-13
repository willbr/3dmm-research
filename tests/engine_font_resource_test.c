#include "three_mm/font_resource.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>
typedef struct Test
{
    ThreeMMFontResource font; ThreeMMFontResourceServices services;
    unsigned char input[40],written[1000]; ThreeMMI32 input_size,written_size;
    int phase,fail,live,frees,object_live,invalidations,bad,order_bad,packed,author;
    int models[3],refs[3],children,adoptions,added,missing;
    ThreeMMFontChild kids[4]; ThreeMMFontChunk chunk;
    void *output; ThreeMMI32 size;
} Test;
static Test *t(void *p) { return (Test *)p; }
static int checkpoint(Test *x) { return ++x->phase==x->fail; }
static void put(unsigned char *p,ThreeMMU32 v,int n,int big)
{ int i; for (i=0;i<n;++i) { p[big?n-1-i:i]=(unsigned char)v; v>>=8; } }
static ThreeMMU32 get(const unsigned char *p,int big)
{ ThreeMMU32 v=0; int i; for (i=0;i<4;++i) v=(v<<8)|p[big?i:3-i]; return v; }
static ThreeMMFontResource *state(void *p,void *object) { assert(p==object); return &t(p)->font; }
static void *create(void *p) { if (checkpoint(t(p))) return NULL; t(p)->object_live=1; return p; }
static void release(void *p,void *object)
{
    int i; Test *x=t(p);
    if (object==p) { assert(x->object_live); if (!x->output || x->output!=p) assert(x->invalidations==2); three_mm_font_dispose(&x->font,&x->services); x->object_live=0; return; }
    for (i=0;i<3;++i) if (object==&x->models[i]) break;
    assert(i<3 && x->refs[i]==1); --x->refs[i];
}
static int initialize(void *p,void *object,void *block) { assert(object==p); return three_mm_font_initialize(&t(p)->font,block,&t(p)->services); }
static int huge_initialize(void *p,void *object,void *block) { assert(object==p && block==p); t(p)->font.count=0x0ffffffe; return 1; }
static ThreeMMI32 object_size(void *p) { (void)p; return 80; }
static int packed(void *p,void *block) { assert(block==p); return t(p)->packed; }
static int unpack(void *p,void *block) { assert(block==p); if (checkpoint(t(p))) return 0; t(p)->packed=0; return 1; }
static ThreeMMI32 block_size(void *p,void *block) { assert(block==p && !t(p)->packed); return t(p)->input_size; }
static int read_block(void *p,void *block,void *out,ThreeMMI32 bytes,ThreeMMI32 offset)
{ assert(block==p && bytes>=0 && offset>=0 && offset+bytes<=t(p)->input_size); if (checkpoint(t(p))) return 0; if (bytes) memcpy(out,t(p)->input+offset,bytes); return 1; }
static int write_block(void *p,void *block,const void *data,ThreeMMI32 bytes,ThreeMMI32 offset)
{ assert(block==p && bytes>=0 && offset>=0 && offset+bytes<=t(p)->written_size); if (checkpoint(t(p))) return 0; memcpy(t(p)->written+offset,data,bytes); return 1; }
static int allocate(void *p,ThreeMMI32 bytes,int clear,void **out)
{ assert(bytes>=0); if (checkpoint(t(p))) return 0; *out=malloc(bytes?bytes:1); assert(*out); memset(*out,clear?0:0xcc,bytes?bytes:1); ++t(p)->live; return 1; }
static void free_memory(void *p,void **out)
{
    Test *x=t(p); int i; ++x->frees;
    if (x->author) { assert(!x->invalidations && x->chunk.type==999 && x->chunk.number==888); for (i=0;i<3;++i) assert(!x->refs[i]); }
    if (*out) { assert(x->live>0); --x->live; free(*out); *out=NULL; }
}
static void invalidate(void *p,void *out,size_t n) { ++t(p)->invalidations; memset(out,0xab,n); }
static void bad_font(void *p) { ++t(p)->bad; }
static void bad_order(void *p) { ++t(p)->order_bad; }
static ThreeMMI32 child_count(void *p,void *children) { assert(p==children); return t(p)->children; }
static void child_at(void *p,void *children,ThreeMMI32 i,ThreeMMFontChild *out) { assert(p==children && i>=0 && i<t(p)->children); *out=t(p)->kids[i]; }
static void *file(void *p,void *cache) { assert(p==cache); return p; }
static void *add(void *p,void *f,ThreeMMI32 bytes,ThreeMMU32 type,ThreeMMU32 *number)
{ assert(p==f && type==THREE_MM_FONT_TYPE && bytes<=1000); if (checkpoint(t(p))) return NULL; ++t(p)->added; *number=7; t(p)->written_size=bytes; return p; }
static void *fetch_model(void *p,void *cache,ThreeMMU32 type,ThreeMMU32 number)
{ int i=(int)number-10; assert(p==cache && (type==0x424d444cUL || type==0x1234) && i>=0 && i<3); if (checkpoint(t(p))) return NULL; ++t(p)->refs[i]; return &t(p)->models[i]; }
static int adopt(void *p,void *f,ThreeMMU32 type,ThreeMMU32 number,const ThreeMMFontChild *child)
{ assert(p==f && type==THREE_MM_FONT_TYPE && number==7 && child->id<=105); if (checkpoint(t(p))) return 0; ++t(p)->adoptions; return 1; }
static ThreeMMI32 model_width(void *p,void *model) { return model==&t(p)->models[0]?0:model==&t(p)->models[1]?65536:196608; }
static ThreeMMI32 model_height(void *p,void *model) { return model==&t(p)->models[0]?0:model==&t(p)->models[1]?131072:262144; }
static void binding(void *p,void *object,void **cache,ThreeMMFontChunk *chunk)
{ assert(p==object); *cache=p; chunk->type=THREE_MM_FONT_TYPE; chunk->number=7; }
static int child_id(void *p,void *f,ThreeMMU32 type,ThreeMMU32 number,ThreeMMU32 id,ThreeMMFontChild *child)
{ assert(p==f && type==THREE_MM_FONT_TYPE && number==7); if (id!=65) return 0; child->type=0x1234; child->number=11; child->id=id; return 1; }
static void missing_model(void *p,ThreeMMU32 id) { assert(id==66); ++t(p)->missing; }
static ThreeMMFontResourceServices setup(Test *x,int host,int other)
{
    ThreeMMFontResourceServices s; int i,big=host!=other; memset(x,0,sizeof(*x)); memset(&s,0,sizeof(s));
    put(x->input,1,2,big); put(x->input+2,17,2,big); put(x->input+4,2,4,big); put(x->input+8,655360,4,big);
    for (i=0;i<4;++i) put(x->input+12+i*4,65536+(ThreeMMU32)i*65536,4,big);
    x->input_size=28; x->children=3; x->chunk.type=999; x->chunk.number=888;
    x->kids[0].type=x->kids[1].type=x->kids[2].type=x->kids[3].type=0x424d444cUL;
    x->kids[0].number=10; x->kids[0].id=32; x->kids[1].number=11; x->kids[1].id=105; x->kids[2].number=11; x->kids[2].id=65; x->kids[3].number=12; x->kids[3].id=105;
    s.context=x; s.host_big_endian=host; s.system=17; s.leading=32768;
    s.state=state; s.create=create; s.release=release; s.initialize=initialize; s.object_size=object_size;
    s.packed=packed; s.unpack=unpack; s.block_size=block_size; s.read=read_block; s.write=write_block;
    s.allocate=allocate; s.free_memory=free_memory; s.invalidate=invalidate; s.bad_font=bad_font; s.bad_order=bad_order;
    s.child_count=child_count; s.child_at=child_at; s.file=file; s.add=add; s.fetch_model=fetch_model; s.adopt=adopt;
    s.model_width=model_width; s.model_height=model_height; s.binding=binding; s.child_id=child_id; s.missing_model=missing_model;
    x->services=s; return s;
}
static void empty(Test *x) { assert(!x->font.widths && !x->font.heights && !x->live && !x->object_live); }
int main(void)
{
    Test x; ThreeMMFontResourceServices s; int host,other,fail,ok,i; ThreeMMI32 value; void *model;
    for (host=0;host<2;++host) for (other=0;other<2;++other)
    {
        s=setup(&x,host,other); assert(three_mm_font_initialize(&x.font,&x,&s));
        assert(x.font.count==2 && x.font.maximum_height==655360 && x.live==2);
        assert(!memcmp(x.font.widths,x.input+12,8) && !memcmp(x.font.heights,x.input+20,8));
        memcpy(&value,x.input+16,4); assert(three_mm_font_width(&x.font,1)==value);
        memcpy(&value,x.input+24,4); assert(three_mm_font_height(&x.font,1)==value);
        three_mm_font_dispose(&x.font,&s); empty(&x); three_mm_font_dispose(&x.font,&s); assert(x.frees==4 && x.font.count==2);
    }
    for (fail=0;fail<=6;++fail)
    {
        s=setup(&x,0,0); x.fail=fail; ok=three_mm_font_initialize(&x.font,&x,&s); assert(ok==!fail);
        if (fail>=3) assert(x.font.count==2 && x.font.maximum_height==655360);
        if (fail==4 || fail==5) assert(x.font.widths && !x.font.heights);
        if (fail==6) assert(x.font.widths && x.font.heights);
        three_mm_font_dispose(&x.font,&s); empty(&x);
    }
    for (fail=0;fail<=7;++fail)
    {
        s=setup(&x,0,0); x.fail=fail;
        ok=three_mm_font_fetch(&x,&x.output,&x.size,&s); assert(ok==!fail);
        if (ok) { assert(x.output==&x && x.size==96); release(&x,&x); }
        else assert(x.invalidations==2 && x.size==(ThreeMMI32)-1414812757);
        empty(&x);
    }
    for (i=0;i<2;++i) { s=setup(&x,0,0); x.packed=i; x.fail=1; assert(three_mm_font_fetch(&x,NULL,&x.size,&s) && x.size==(i?2128:28) && !x.phase); }
    s=setup(&x,0,0); x.packed=1; assert(three_mm_font_fetch(&x,&x.output,&x.size,&s) && x.size==96); release(&x,&x); empty(&x);
    s=setup(&x,0,0); s.initialize=huge_initialize; assert(!three_mm_font_fetch(&x,&x.output,&x.size,&s) && x.bad==1 && x.invalidations==2); empty(&x);
    s=setup(&x,0,0); x.input_size=11; assert(!three_mm_font_initialize(&x.font,&x,&s) && x.bad==1 && !x.font.count); empty(&x);
    s=setup(&x,0,0); x.input_size=32; assert(!three_mm_font_initialize(&x.font,&x,&s) && x.bad==1 && x.font.count==2 && !x.font.maximum_height); empty(&x);
    for (i=0;i<2;++i) { s=setup(&x,0,0); put(x.input+4,i?0x10000000UL:0xffffffffUL,4,0); assert(!three_mm_font_initialize(&x.font,&x,&s) && x.bad==1 && !x.live); }
    s=setup(&x,0,0); x.input[0]=3; assert(!three_mm_font_initialize(&x.font,&x,&s) && x.order_bad==1 && x.bad==1); empty(&x);
    s=setup(&x,0,0); put(x.input+4,0,4,0); x.input_size=12; assert(three_mm_font_initialize(&x.font,&x,&s) && x.live==2); three_mm_font_dispose(&x.font,&s); empty(&x);
    for (fail=0;fail<=13;++fail)
    {
        s=setup(&x,0,0); x.author=1; x.fail=fail; ok=three_mm_font_author(&x,&x,&x.chunk,&s); assert(ok==!fail);
        assert(!x.live && x.frees==2 && !x.refs[0] && !x.refs[1]);
        if (ok) { assert(x.chunk.type==THREE_MM_FONT_TYPE && x.chunk.number==7 && x.adoptions==3 && x.written_size==860);
            assert(get(x.written+4,0)==106 && get(x.written+8,0)==163840);
            assert(get(x.written+12+32*4,0)==65536 && get(x.written+12+424+32*4,0)==163840); }
        else { assert(x.invalidations==1); if (fail>=4) assert(x.added==1);
            assert(x.adoptions==(fail<=5?0:fail<=8?1:fail<=10?2:3)); }
    }
    s=setup(&x,0,0); x.author=1; x.children=0; assert(three_mm_font_author(&x,&x,&x.chunk,&s) && x.written_size==20 && get(x.written+4,0)==1 && !get(x.written+8,0) && !get(x.written+12,0) && !get(x.written+16,0));
    s=setup(&x,0,0); x.author=1; x.children=2; x.kids[1]=x.kids[2]; assert(three_mm_font_author(&x,&x,&x.chunk,&s) && !get(x.written+12+32*4,0));
    s=setup(&x,0,0); x.author=1; x.children=4; assert(three_mm_font_author(&x,&x,&x.chunk,&s) && get(x.written+12+32*4,0)==196608 && get(x.written+8,0)==294912);
    s=setup(&x,0,0); x.author=1; x.kids[0].id=0xffffffffUL; assert(!three_mm_font_author(&x,&x,&x.chunk,&s) && !x.phase && !x.live && !x.added);
    s=setup(&x,1,0); x.author=1; x.children=0; assert(three_mm_font_author(&x,&x,NULL,&s) && x.written[0]==0 && x.written[1]==1 && get(x.written+4,1)==1 && !x.invalidations);
    s=setup(&x,0,0); x.author=1; x.fail=5; assert(!three_mm_font_author(&x,&x,NULL,&s) && !x.live && !x.refs[0] && !x.invalidations);
    s=setup(&x,0,0); model=three_mm_font_model(&x,65,&s); assert(model==&x.models[1] && x.refs[1]==1); release(&x,model);
    assert(!three_mm_font_model(&x,66,&s) && x.missing==1);
    return 0;
}
