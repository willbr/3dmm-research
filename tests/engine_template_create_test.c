#include "mm/template_create.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(c) do { if (!(c)) { fprintf(stderr,"template check %d: %s\n",__LINE__,#c); exit(1); } } while (0)
typedef struct Ref { int refs,kind,set; } Ref;
typedef struct List { MMPartIndexList list; int refs,count; unsigned char data[12]; } List;
typedef struct Block { int kind,length; unsigned char data[16]; } Block;
typedef struct Fixture
{
    MMTemplateCreateServices s; MMTemplateState t; Ref object,cache,body,customs[60]; List parents,sets; MMTemplateCostumeTable group; int group_refs; Block blocks[4];
    int file,fail,fallback,other,text,gap,custom_count,child_calls,created,initialized,header_calls;
    int fetched,applied,freed,body_created,body_released,default_calls,deleted,diagnostics[5],invalidated;
    MMI32 applied_ids[2];
    unsigned char encoded[16]; MMU32 written_type,delete_type; int name_calls;
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
static int child(void *p,void *file,MMU32 type,MMU32 number,MMU32 id,MMU32 wanted,MMTemplateChild *out)
{
    Fixture *f=p; int ok=0; CHECK(file==&f->file && type==MM_TMPL && number==1); ++f->child_calls;
    if (wanted==MM_GLPI || wanted==MM_GLBS) ok=!id;
    else if (wanted==MM_GGCM) ok=!id && !f->fallback;
    else if (wanted==MM_CMTL) ok=id<(MMU32)f->custom_count && !(f->gap && id==2);
    else if (wanted==MM_ACTN) ok=id==0 || id==2;
    else if (wanted==MM_TDT) ok=f->text && !id;
    if (ok) { out->type=wanted; out->number=10+id; out->id=id; } return ok;
}
static int enumerate(void *p,void *file,MMU32 type,MMU32 number,MMI32 i,MMTemplateChild *out)
{
    Fixture *f=p; CHECK(file==&f->file && type==MM_TMPL && number==1);
    if (!i) { out->type=MM_GLPI; out->number=10; out->id=0; return 1; }
    if (i>f->custom_count) return 0;
    out->type=MM_CMTL; out->number=10+i-1; out->id=(MMU32)i-1; return 1;
}
static void *find(void *p,void *file,MMU32 type,MMU32 number)
{
    Fixture *f=p; (void)number; CHECK(file==&f->file);
    if (type==MM_GLPI) return f->fail==1?NULL:&f->blocks[1];
    if (type==MM_GLBS) return f->fail==3?NULL:&f->blocks[2];
    if (type==MM_GGCM) return f->fail==5?NULL:&f->blocks[3];
    CHECK(type==MM_TMPL); return &f->blocks[0];
}
static int unpack(void *p,void *b) { (void)b; return ((Fixture *)p)->fail!=17; }
static MMI32 block_size(void *p,void *b,int packed) { (void)p; (void)packed; return ((Block *)b)->length; }
static int block_read(void *p,void *b,void *out,size_t n) { Fixture *f=p; CHECK(n==16); if (f->fail==18) return 0; memcpy(out,((Block *)b)->data,n); return 1; }
static int read_name(void *p,void *o,void *file,MMU32 type,MMU32 number)
{ Fixture *f=p; CHECK(o==&f->object && file==&f->file && type==MM_TMPL && number==1); CHECK(f->t.x==0x1234 && f->t.flags==4); return f->fail!=16; }
static int add_chunk(void *p,void *file,MMU32 type,const void *data,size_t n,MMU32 *number)
{ Fixture *f=p; CHECK(file==&f->file && n==16); f->written_type=type; memcpy(f->encoded,data,16); if (f->fail==19) return 0; *number=123; return 1; }
static int set_name(void *p,void *o,void *file,MMU32 type,MMU32 number)
{ Fixture *f=p; CHECK(o==&f->object && file==&f->file && type==MM_TMPL && number==123); ++f->name_calls; return f->fail!=20; }
static void delete_chunk(void *p,void *file,MMU32 type,MMU32 number)
{ Fixture *f=p; CHECK(file==&f->file && number==123); ++f->deleted; f->delete_type=type; }
static void *create(void *p,int text) { Fixture *f=p; CHECK(text==f->text); ++f->created; if (f->fail==11) return NULL; f->object.refs=1; return &f->object; }
static int initialize(void *p,void *o,void *file,MMU32 type,MMU32 number)
{
    Fixture *f=p; ++f->initialized; if (f->fail==12) return 0;
    if (f->text) { f->t.flags=2; return 1; }
    return mm_template_initialize(o,file,type,number,&f->s);
}
static MMI32 object_size(void *p,int text) { (void)p; return text?200:100; }
static void invalidate(void *p,void *out,size_t n) { Fixture *f=p; CHECK(!f->object.refs); memset(out,0xa5,n); ++f->invalidated; }
static int header(void *p,void *o,void *file,MMU32 type,MMU32 number)
{ Fixture *f=p; ++f->header_calls; return mm_template_read_header(o,file,type,number,&f->s); }
static void release_parents(void *p)
{
    Fixture *f=p;
    CHECK(f->t.parents==&f->parents.list && f->parents.refs==1);
    --f->parents.refs;
}
static void release_sets(void *p)
{
    Fixture *f=p;
    CHECK(!f->t.parents && f->t.sets==&f->sets.list && f->sets.refs==1);
    --f->sets.refs;
}
static int read_part_indices(void *p,void *b,MMPartIndexList **out)
{
    Fixture *f=p;
    int kind=((Block *)b)->kind, big=f->other!=f->s.host_big_endian;
    List *source=kind==0?&f->parents:&f->sets;
    unsigned char wire[24]={0};
    MMMemoryStream stream;
    CHECK(kind==0 || kind==1);
    if (f->fail==2+kind*2)
        return MM_MOVIE_MEMORY;
    put(wire,2,1,big);
    put(wire+4,4,2,big);
    put(wire+8,4,(MMU32)source->count,big);
    memcpy(wire+12,source->data,source->count*2);
    CHECK(mm_memory_stream_init(&stream,wire,12+source->count*2));
    mm_part_indices_init(&source->list,NULL,f,kind==0?release_parents:release_sets);
    CHECK(mm_part_indices_read(&source->list,&stream.stream)==MM_MOVIE_OK);
    ++source->refs;
    *out=&source->list;
    return MM_MOVIE_OK;
}
static void *create_cache(void *p,void *file) { Fixture *f=p; CHECK(file==&f->file); if (f->fail==7) return NULL; ++f->cache.refs; return &f->cache; }
static void release_costumes(void *p)
{
    Fixture *f = (Fixture *)p;
    CHECK(!f->t.parents && !f->t.sets && f->t.costumes == &f->group);
    --f->group_refs;
}
static MMTemplateCostumeTable *create_costumes(void *p)
{
    Fixture *f = (Fixture *)p;
    if (f->fail == 8)
        return NULL;
    mm_template_costumes_init(&f->group, NULL, f, release_costumes);
    ++f->group_refs;
    return &f->group;
}
static int read_costumes(void *p, void *block, MMI32 expected, MMTemplateCostumeTable **out)
{
    Fixture *f = (Fixture *)p;
    MMMemoryStream stream;
    unsigned char wire[60];
    int i, big = f->other != f->s.host_big_endian;
    CHECK(block == &f->blocks[3] && expected == 2);
    if (f->fail == 6)
        return MM_MOVIE_MEMORY;
    memset(wire, 0, sizeof(wire));
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
    CHECK(mm_memory_stream_init(&stream, wire, sizeof(wire)));
    *out = create_costumes(p);
    CHECK(*out && mm_template_costumes_read(*out, &stream.stream, expected) == MM_MOVIE_OK);
    return MM_MOVIE_OK;
}
static void *fetch_custom(void *p,void *cache,MMU32 type,MMU32 number)
{
    Fixture *f=p; CHECK(cache==&f->cache && f->cache.refs==1 && type==MM_CMTL && number>=10 && number<70);
    ++f->fetched; if (f->fail==9 && f->fetched==6) return NULL; ++f->customs[number-10].refs; return &f->customs[number-10];
}
static MMI32 custom_set(void *p,void *c) { (void)p; return ((Ref *)c)->set; }
static int append(void *p,MMTemplateCostumeTable *group,const MMI32 *ids,MMI32 n)
{
    Fixture *f=p; CHECK(group==&f->group && group->rows<2 && n<=50);
    if (f->fail==10 && group->rows==1) return 0;
    return mm_template_costumes_append(group, ids, n);
}
static void *create_body(void *p,const MMPartIndexView *parents,const MMPartIndexView *sets)
{ Fixture *f=p; CHECK(parents==&f->parents.list.view && sets==&f->sets.list.view); ++f->body_created; if (f->fail==15) return NULL; f->body.refs=1; return &f->body; }
static int default_costume(void *p,void *o,void *body)
{ Fixture *f=p; ++f->default_calls; return mm_template_default_costume(o,body,&f->s); }
static MMI32 costume_id(void *p,void *o,MMI32 set,MMI32 index)
{
    Fixture *f=p;
    MMI32 id;
    CHECK(o==&f->object && !index && mm_template_costumes_id(&f->group,set,index,&id));
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
    int i; memset(f,0,sizeof(*f)); f->other=other; f->s.host_big_endian=host_big; f->s.system=0x0303; f->custom_count=4;
    f->cache.kind=4; f->body.kind=5;
    f->parents.count=f->sets.count=6;
    for (i=0;i<4;++i) { f->blocks[i].kind=i-1; f->blocks[i].length=16; }
    put(f->blocks[0].data,2,1,other); put(f->blocks[0].data+4,2,0x1234,other); put(f->blocks[0].data+6,2,0xabcd,other);
    put(f->blocks[0].data+8,2,0x8000,other); put(f->blocks[0].data+12,4,4,other);
    for (i=0;i<6;++i) { put(f->parents.data+i*2,2,i?(MMU32)(i-1):65535U,other!=host_big); put(f->sets.data+i*2,2,(MMU32)i%2,other!=host_big); }
    for (i=0;i<60;++i) { f->customs[i].kind=6; f->customs[i].set=i%2; }
    f->s.context=f; f->s.state=template_state; f->s.release=release; f->s.child=child; f->s.enumerate=enumerate;
    f->s.find=find; f->s.unpack=unpack; f->s.block_size=block_size; f->s.block_read=block_read; f->s.read_name=read_name;
    f->s.add_chunk=add_chunk; f->s.set_name=set_name; f->s.delete_chunk=delete_chunk; f->s.create=create; f->s.initialize=initialize;
    f->s.object_size=object_size; f->s.invalidate=invalidate; f->s.header=header;
    f->s.read_part_indices=read_part_indices; f->s.read_costumes=read_costumes;
    f->s.create_cache=create_cache; f->s.create_costumes=create_costumes; f->s.fetch_custom=fetch_custom; f->s.custom_set=custom_set;
    f->s.append_costumes=append; f->s.create_body=create_body; f->s.default_costume=default_costume; f->s.costume_id=costume_id;
    f->s.custom=custom; f->s.allocate_refs=allocate; f->s.apply_custom=apply; f->s.free_memory=free_memory; f->s.diagnostic=diagnose;
}
static void disposed(Fixture *f)
{
    int i; mm_template_dispose(&f->object,&f->s);
    CHECK(!f->parents.refs && !f->sets.refs && !f->group_refs && !f->cache.refs);
    CHECK(!f->t.parents && !f->t.sets && !f->t.costumes);
    for (i=0;i<60;++i) CHECK(!f->customs[i].refs);
}
int main(void)
{
    Fixture f; int other,host,failure,i,ok; void *out,*body; MMI32 bytes, id; MMU32 number;
    memset(&f.t,0xa5,sizeof(f.t)); mm_template_state_init(&f.t);
    CHECK(!f.t.parents && !f.t.sets && !f.t.costumes);
    CHECK(!f.t.set_count && !f.t.custom_count && !f.t.action_count);
    CHECK(!f.t.x && !f.t.y && !f.t.z && !f.t.flags);
    for (other=0;other<2;++other) for (host=0;host<2;++host)
    {
        setup(&f,other,host); CHECK(mm_template_fetch(&f.file,MM_TMPL,1,&f.blocks[0],NULL,&bytes,&f.s)); CHECK(bytes==16 && !f.created && !f.child_calls);
        out=NULL; CHECK(mm_template_fetch(&f.file,MM_TMPL,1,&f.blocks[0],&out,&bytes,&f.s));
        CHECK(out==&f.object && bytes==208 && f.t.custom_count==4 && f.t.action_count==1 && f.t.set_count==2);
        CHECK(mm_part_index_get(&f.t.parents->view,0)==-1 && mm_part_index_get(&f.t.parents->view,5)==4);
        CHECK(mm_part_index_get(&f.t.sets->view,5)==1);
        CHECK(mm_template_costumes_count(&f.group,0)==2 && mm_template_costumes_id(&f.group,1,1,&id) && id==3);
        put(f.group.storage.buffers[0].data + 4,4,2,f.group.big_endian);
        body=mm_template_create_body(&f.object,&f.s); CHECK(body==&f.body && f.applied==2 && f.freed==1); release(&f,body);
        CHECK(f.applied_ids[0]==2 && f.applied_ids[1]==1);
        number=777; CHECK(mm_template_write_header(&f.object,&f.file,MM_TMPL,&number,&f.s));
        CHECK(number==123 && !f.encoded[10] && !f.encoded[11] && word(f.encoded+4,2,host)==0x1234 && word(f.encoded+12,4,host)==4);
        release(&f,&f.object); disposed(&f);
    }
    for (failure=1;failure<=10;++failure)
    {
        setup(&f,0,0); f.fail=failure; f.fallback=failure>=7;
        CHECK(!mm_template_initialize(&f.object,&f.file,MM_TMPL,1,&f.s));
        CHECK(f.t.parents==(failure<=2?NULL:&f.parents.list)); CHECK(f.t.sets==(failure<=4?NULL:&f.sets.list));
        if (failure>=9) CHECK(f.t.costumes==&f.group && f.group.rows==1);
        CHECK(!f.cache.refs); disposed(&f);
    }
    for (failure=11;failure<=12;++failure)
    {
        unsigned char expected[sizeof(void *)]; setup(&f,0,0); f.fail=failure; out=&f.file; memset(expected,0xa5,sizeof(expected));
        CHECK(!mm_template_fetch(&f.file,MM_TMPL,1,&f.blocks[0],&out,&bytes,&f.s));
        CHECK(!memcmp(&out,expected,sizeof(out)) && f.invalidated==2 && !f.object.refs); disposed(&f);
    }
    setup(&f,0,0); f.text=1; out=NULL; CHECK(mm_template_fetch(&f.file,MM_TMPL,1,&f.blocks[0],&out,&bytes,&f.s)); CHECK(bytes==200 && !f.header_calls); release(&f,out);
    for (failure=13;failure<=15;++failure)
    {
        setup(&f,0,0); CHECK(mm_template_initialize(&f.object,&f.file,MM_TMPL,1,&f.s)); f.fail=failure;
        CHECK(!mm_template_create_body(&f.object,&f.s) && !f.applied);
        CHECK(f.body_released==(failure!=15) && f.default_calls==(failure!=15)); disposed(&f);
    }
    for (failure=16;failure<=18;++failure)
    { setup(&f,0,0); f.fail=failure; CHECK(!mm_template_read_header(&f.object,&f.file,MM_TMPL,1,&f.s)); CHECK(f.t.flags==(failure==16?4U:0U)); disposed(&f); }
    for (failure=19;failure<=20;++failure)
    {
        setup(&f,0,0); f.fail=failure; number=777;
        CHECK(!mm_template_write_header(&f.object,&f.file,0x44415441U,&number,&f.s));
        CHECK(number==(failure==19?777U:123U) && f.deleted==(failure==20)); if (f.deleted) CHECK(f.delete_type==0x44415441U);
    }
    setup(&f,0,0); f.fallback=1; f.gap=1;
    CHECK(mm_template_initialize(&f.object,&f.file,MM_TMPL,1,&f.s)); CHECK(f.t.custom_count==2 && f.group.rows==2 && f.fetched==8);
    f.fetched=0;
    body=mm_template_create_body(&f.object,&f.s);
    CHECK(body==&f.body && f.applied==2 && f.applied_ids[0]==0 && f.applied_ids[1]==1);
    release(&f,body); disposed(&f);
    for (i=50;i<=51;++i)
    {
        setup(&f,0,0); f.fallback=1; f.custom_count=i; f.sets.count=f.parents.count=1; put(f.sets.data,2,0,0);
        for (failure=0;failure<i;++failure) f.customs[failure].set=0;
        ok=mm_template_initialize(&f.object,&f.file,MM_TMPL,1,&f.s); CHECK(ok==(i==50));
        CHECK(f.diagnostics[MM_TEMPLATE_COSTUME_LIMIT]==(i==51)); if (ok) CHECK(mm_template_costumes_count(&f.group,0)==50); disposed(&f);
    }
    puts("C89 template construction, header and default costume checks passed"); return 0;
}
