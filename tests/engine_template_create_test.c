#include "three_mm/template_create.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(c) do { if (!(c)) { fprintf(stderr,"template check %d: %s\n",__LINE__,#c); exit(1); } } while (0)
typedef struct Ref { int refs,kind,set; } Ref;
typedef struct List { Ref ref; int count; unsigned char data[12]; } List;
typedef struct Group { Ref ref; int rows; unsigned char fixed[2][4],data[2][200]; } Group;
typedef struct Block { int kind,length; unsigned char data[16]; } Block;
typedef struct Fixture
{
    ThreeMMTemplateCreateServices s; ThreeMMTemplateState t; Ref object,cache,body,customs[60]; List parents,sets; Group group; Block blocks[4];
    int file,fail,fallback,other,text,gap,custom_count,child_calls,created,initialized,header_calls;
    int fetched,applied,freed,body_created,body_released,default_calls,deleted,diagnostics[5],invalidated;
    unsigned char encoded[16]; ThreeMMU32 written_type,delete_type; int name_calls;
} Fixture;
static void put(unsigned char *p,unsigned n,ThreeMMU32 v,int big)
{ unsigned i; for (i=0;i<n;++i) { p[big?n-i-1:i]=(unsigned char)(v&255U); v>>=8; } }
static ThreeMMU32 word(const unsigned char *p,unsigned n,int big)
{ ThreeMMU32 v=0; unsigned i; for (i=0;i<n;++i) v=(v<<8)|p[big?i:n-i-1]; return v; }
static void read_state(void *p,void *o,ThreeMMTemplateState *t) { Fixture *f=p; CHECK(o==&f->object); *t=f->t; }
static void write_state(void *p,void *o,const ThreeMMTemplateState *t) { Fixture *f=p; CHECK(o==&f->object); f->t=*t; }
static void release(void *p,void *o)
{
    Fixture *f=p; Ref *r=o; CHECK(r && r->refs>0); --r->refs;
    if (o==&f->object) { CHECK(!r->refs); three_mm_template_dispose(o,&f->s); }
    else if (o==&f->parents) CHECK(f->t.parents==o);
    else if (o==&f->sets) CHECK(!f->t.parents && f->t.sets==o);
    else if (o==&f->group) CHECK(!f->t.parents && !f->t.sets && f->t.costumes==o);
    else if (o==&f->body) ++f->body_released;
}
static int child(void *p,void *file,ThreeMMU32 type,ThreeMMU32 number,ThreeMMU32 id,ThreeMMU32 wanted,ThreeMMTemplateChild *out)
{
    Fixture *f=p; int ok=0; CHECK(file==&f->file && type==THREE_MM_TMPL && number==1); ++f->child_calls;
    if (wanted==THREE_MM_GLPI || wanted==THREE_MM_GLBS) ok=!id;
    else if (wanted==THREE_MM_GGCM) ok=!id && !f->fallback;
    else if (wanted==THREE_MM_CMTL) ok=id<(ThreeMMU32)f->custom_count && !(f->gap && id==2);
    else if (wanted==THREE_MM_ACTN) ok=id==0 || id==2;
    else if (wanted==THREE_MM_TDT) ok=f->text && !id;
    if (ok) { out->type=wanted; out->number=10+id; out->id=id; } return ok;
}
static int enumerate(void *p,void *file,ThreeMMU32 type,ThreeMMU32 number,ThreeMMI32 i,ThreeMMTemplateChild *out)
{
    Fixture *f=p; CHECK(file==&f->file && type==THREE_MM_TMPL && number==1);
    if (!i) { out->type=THREE_MM_GLPI; out->number=10; out->id=0; return 1; }
    if (i>f->custom_count) return 0;
    out->type=THREE_MM_CMTL; out->number=10+i-1; out->id=(ThreeMMU32)i-1; return 1;
}
static void *find(void *p,void *file,ThreeMMU32 type,ThreeMMU32 number)
{
    Fixture *f=p; (void)number; CHECK(file==&f->file);
    if (type==THREE_MM_GLPI) return f->fail==1?NULL:&f->blocks[1];
    if (type==THREE_MM_GLBS) return f->fail==3?NULL:&f->blocks[2];
    if (type==THREE_MM_GGCM) return f->fail==5?NULL:&f->blocks[3];
    CHECK(type==THREE_MM_TMPL); return &f->blocks[0];
}
static int unpack(void *p,void *b) { (void)b; return ((Fixture *)p)->fail!=17; }
static ThreeMMI32 block_size(void *p,void *b,int packed) { (void)p; (void)packed; return ((Block *)b)->length; }
static int block_read(void *p,void *b,void *out,size_t n) { Fixture *f=p; CHECK(n==16); if (f->fail==18) return 0; memcpy(out,((Block *)b)->data,n); return 1; }
static int read_name(void *p,void *o,void *file,ThreeMMU32 type,ThreeMMU32 number)
{ Fixture *f=p; CHECK(o==&f->object && file==&f->file && type==THREE_MM_TMPL && number==1); CHECK(f->t.x==0x1234 && f->t.flags==4); return f->fail!=16; }
static int add_chunk(void *p,void *file,ThreeMMU32 type,const void *data,size_t n,ThreeMMU32 *number)
{ Fixture *f=p; CHECK(file==&f->file && n==16); f->written_type=type; memcpy(f->encoded,data,16); if (f->fail==19) return 0; *number=123; return 1; }
static int set_name(void *p,void *o,void *file,ThreeMMU32 type,ThreeMMU32 number)
{ Fixture *f=p; CHECK(o==&f->object && file==&f->file && type==THREE_MM_TMPL && number==123); ++f->name_calls; return f->fail!=20; }
static void delete_chunk(void *p,void *file,ThreeMMU32 type,ThreeMMU32 number)
{ Fixture *f=p; CHECK(file==&f->file && number==123); ++f->deleted; f->delete_type=type; }
static void *create(void *p,int text) { Fixture *f=p; CHECK(text==f->text); ++f->created; if (f->fail==11) return NULL; f->object.refs=1; return &f->object; }
static int initialize(void *p,void *o,void *file,ThreeMMU32 type,ThreeMMU32 number)
{
    Fixture *f=p; ++f->initialized; if (f->fail==12) return 0;
    if (f->text) { f->t.flags=2; return 1; }
    return three_mm_template_initialize(o,file,type,number,&f->s);
}
static ThreeMMI32 object_size(void *p,int text) { (void)p; return text?200:100; }
static ThreeMMI32 table_size(void *p,void *table) { (void)p; CHECK(table); return 10; }
static void invalidate(void *p,void *out,size_t n) { Fixture *f=p; CHECK(!f->object.refs); memset(out,0xa5,n); ++f->invalidated; }
static int header(void *p,void *o,void *file,ThreeMMU32 type,ThreeMMU32 number)
{ Fixture *f=p; ++f->header_calls; return three_mm_template_read_header(o,file,type,number,&f->s); }
static void *read_table(void *p,void *b,int kind,int *other)
{
    Fixture *f=p; Ref *r; CHECK(((Block *)b)->kind==kind); *other=f->other;
    if (f->fail==2+kind*2) return NULL;
    r=kind==0?&f->parents.ref:kind==1?&f->sets.ref:&f->group.ref; ++r->refs; return r;
}
static void validate(void *p,void *table,int kind,ThreeMMI32 n) { Fixture *f=p; (void)table; CHECK(kind!=2 || n==f->t.set_count); }
static ThreeMMI32 count(void *p,void *list) { (void)p; return ((List *)list)->count; }
static ThreeMMI32 item(void *p,void *list,ThreeMMI32 i) { Fixture *f=p; CHECK(i>=0 && i<((List *)list)->count); return (ThreeMMI32)word(((List *)list)->data+i*2,2,f->s.host_big_endian); }
static unsigned char *list_data(void *p,void *list) { (void)p; return ((List *)list)->data; }
static unsigned char *fixed(void *p,void *g,ThreeMMI32 i) { Fixture *f=p; CHECK(g==&f->group && i>=0 && i<2); return f->group.fixed[i]; }
static unsigned char *variable(void *p,void *g,ThreeMMI32 i) { Fixture *f=p; CHECK(g==&f->group && i>=0 && i<2); return f->group.data[i]; }
static void *create_cache(void *p,void *file) { Fixture *f=p; CHECK(file==&f->file); if (f->fail==7) return NULL; ++f->cache.refs; return &f->cache; }
static void *create_costumes(void *p) { Fixture *f=p; if (f->fail==8) return NULL; f->group.rows=0; ++f->group.ref.refs; return &f->group; }
static void *fetch_custom(void *p,void *cache,ThreeMMU32 type,ThreeMMU32 number)
{
    Fixture *f=p; CHECK(cache==&f->cache && f->cache.refs==1 && type==THREE_MM_CMTL && number>=10 && number<70);
    ++f->fetched; if (f->fail==9 && f->fetched==6) return NULL; ++f->customs[number-10].refs; return &f->customs[number-10];
}
static ThreeMMI32 custom_set(void *p,void *c) { (void)p; return ((Ref *)c)->set; }
static int append(void *p,void *group,const ThreeMMI32 *ids,ThreeMMI32 n)
{
    Fixture *f=p; int i,row=f->group.rows; CHECK(group==&f->group && row<2 && n<=50);
    if (f->fail==10 && row==1) return 0;
    put(f->group.fixed[row],4,(ThreeMMU32)n,f->s.host_big_endian);
    for (i=0;i<n;++i) put(f->group.data[row]+i*4,4,(ThreeMMU32)ids[i],f->s.host_big_endian);
    ++f->group.rows; return 1;
}
static void *create_body(void *p,void *parents,void *sets)
{ Fixture *f=p; CHECK(parents==&f->parents && sets==&f->sets); ++f->body_created; if (f->fail==15) return NULL; f->body.refs=1; return &f->body; }
static int default_costume(void *p,void *o,void *body)
{ Fixture *f=p; ++f->default_calls; return three_mm_template_default_costume(o,body,&f->s); }
static ThreeMMI32 costume_id(void *p,void *o,ThreeMMI32 set,ThreeMMI32 index) { Fixture *f=p; CHECK(o==&f->object && !index); return set; }
static void *custom(void *p,void *o,ThreeMMI32 id)
{ Fixture *f=p; CHECK(o==&f->object && id>=0 && id<2); ++f->fetched; if (f->fail==14 && id==1) return NULL; ++f->customs[id].refs; return &f->customs[id]; }
static int allocate(void *p,ThreeMMI32 n,void **out)
{ Fixture *f=p; *out=NULL; CHECK(n==f->t.set_count); if (f->fail==13) return 0; *out=calloc((size_t)n+1,sizeof(void *)); CHECK(*out); return 1; }
static void apply(void *p,void *body,void *c) { Fixture *f=p; CHECK(body==&f->body && f->fetched==f->t.set_count && ((Ref *)c)->refs==1); ++f->applied; }
static void free_memory(void *p,void **data) { Fixture *f=p; int i; void **refs=*data; for (i=0;i<f->t.set_count;++i) CHECK(!refs[i]); free(*data); *data=NULL; ++f->freed; }
static void diagnose(void *p,int n) { CHECK(n>=0 && n<5); ++((Fixture *)p)->diagnostics[n]; }
static void setup(Fixture *f,int other,int host_big)
{
    int i,j; memset(f,0,sizeof(*f)); f->other=other; f->s.host_big_endian=host_big; f->s.system=0x0303; f->custom_count=4;
    f->parents.ref.kind=1; f->sets.ref.kind=2; f->group.ref.kind=3; f->cache.kind=4; f->body.kind=5;
    f->parents.count=f->sets.count=6; f->group.rows=2;
    for (i=0;i<4;++i) { f->blocks[i].kind=i-1; f->blocks[i].length=16; }
    put(f->blocks[0].data,2,1,other); put(f->blocks[0].data+4,2,0x1234,other); put(f->blocks[0].data+6,2,0xabcd,other);
    put(f->blocks[0].data+8,2,0x8000,other); put(f->blocks[0].data+12,4,4,other);
    for (i=0;i<6;++i) { put(f->parents.data+i*2,2,i?(ThreeMMU32)(i-1):65535U,other!=host_big); put(f->sets.data+i*2,2,(ThreeMMU32)i%2,other!=host_big); }
    for (i=0;i<2;++i) { put(f->group.fixed[i],4,2,other!=host_big); for (j=0;j<2;++j) put(f->group.data[i]+j*4,4,(ThreeMMU32)(i+j*2),other!=host_big); }
    for (i=0;i<60;++i) { f->customs[i].kind=6; f->customs[i].set=i%2; }
    f->s.context=f; f->s.read=read_state; f->s.write=write_state; f->s.release=release; f->s.child=child; f->s.enumerate=enumerate;
    f->s.find=find; f->s.unpack=unpack; f->s.block_size=block_size; f->s.block_read=block_read; f->s.read_name=read_name;
    f->s.add_chunk=add_chunk; f->s.set_name=set_name; f->s.delete_chunk=delete_chunk; f->s.create=create; f->s.initialize=initialize;
    f->s.object_size=object_size; f->s.table_size=table_size; f->s.invalidate=invalidate; f->s.header=header; f->s.read_table=read_table;
    f->s.validate_table=validate; f->s.count=count; f->s.item=item; f->s.list_data=list_data; f->s.fixed_data=fixed; f->s.variable_data=variable;
    f->s.create_cache=create_cache; f->s.create_costumes=create_costumes; f->s.fetch_custom=fetch_custom; f->s.custom_set=custom_set;
    f->s.append_costumes=append; f->s.create_body=create_body; f->s.default_costume=default_costume; f->s.costume_id=costume_id;
    f->s.custom=custom; f->s.allocate_refs=allocate; f->s.apply_custom=apply; f->s.free_memory=free_memory; f->s.diagnostic=diagnose;
}
static void disposed(Fixture *f)
{
    int i; three_mm_template_dispose(&f->object,&f->s);
    CHECK(!f->parents.ref.refs && !f->sets.ref.refs && !f->group.ref.refs && !f->cache.refs);
    CHECK(!f->t.parents && !f->t.sets && !f->t.costumes);
    for (i=0;i<60;++i) CHECK(!f->customs[i].refs);
}
int main(void)
{
    Fixture f; int other,host,failure,i,ok; void *out,*body; ThreeMMI32 bytes; ThreeMMU32 number;
    for (other=0;other<2;++other) for (host=0;host<2;++host)
    {
        setup(&f,other,host); CHECK(three_mm_template_fetch(&f.file,THREE_MM_TMPL,1,&f.blocks[0],NULL,&bytes,&f.s)); CHECK(bytes==16 && !f.created && !f.child_calls);
        out=NULL; CHECK(three_mm_template_fetch(&f.file,THREE_MM_TMPL,1,&f.blocks[0],&out,&bytes,&f.s));
        CHECK(out==&f.object && bytes==130 && f.t.custom_count==4 && f.t.action_count==1 && f.t.set_count==2);
        CHECK(word(f.group.fixed[0],4,host)==2 && word(f.group.data[1]+4,4,host)==3);
        body=three_mm_template_create_body(&f.object,&f.s); CHECK(body==&f.body && f.applied==2 && f.freed==1); release(&f,body);
        number=777; CHECK(three_mm_template_write_header(&f.object,&f.file,THREE_MM_TMPL,&number,&f.s));
        CHECK(number==123 && !f.encoded[10] && !f.encoded[11] && word(f.encoded+4,2,host)==0x1234 && word(f.encoded+12,4,host)==4);
        release(&f,&f.object); disposed(&f);
    }
    for (failure=1;failure<=10;++failure)
    {
        setup(&f,0,0); f.fail=failure; f.fallback=failure>=7;
        CHECK(!three_mm_template_initialize(&f.object,&f.file,THREE_MM_TMPL,1,&f.s));
        CHECK(f.t.parents==(failure<=2?NULL:&f.parents)); CHECK(f.t.sets==(failure<=4?NULL:&f.sets));
        if (failure>=9) CHECK(f.t.costumes==&f.group && f.group.rows==1);
        CHECK(!f.cache.refs); disposed(&f);
    }
    for (failure=11;failure<=12;++failure)
    {
        unsigned char expected[sizeof(void *)]; setup(&f,0,0); f.fail=failure; out=&f.file; memset(expected,0xa5,sizeof(expected));
        CHECK(!three_mm_template_fetch(&f.file,THREE_MM_TMPL,1,&f.blocks[0],&out,&bytes,&f.s));
        CHECK(!memcmp(&out,expected,sizeof(out)) && f.invalidated==2 && !f.object.refs); disposed(&f);
    }
    setup(&f,0,0); f.text=1; out=NULL; CHECK(three_mm_template_fetch(&f.file,THREE_MM_TMPL,1,&f.blocks[0],&out,&bytes,&f.s)); CHECK(bytes==200 && !f.header_calls); release(&f,out);
    for (failure=13;failure<=15;++failure)
    {
        setup(&f,0,0); CHECK(three_mm_template_initialize(&f.object,&f.file,THREE_MM_TMPL,1,&f.s)); f.fail=failure;
        CHECK(!three_mm_template_create_body(&f.object,&f.s) && !f.applied);
        CHECK(f.body_released==(failure!=15) && f.default_calls==(failure!=15)); disposed(&f);
    }
    for (failure=16;failure<=18;++failure)
    { setup(&f,0,0); f.fail=failure; CHECK(!three_mm_template_read_header(&f.object,&f.file,THREE_MM_TMPL,1,&f.s)); CHECK(f.t.flags==(failure==16?4U:0U)); disposed(&f); }
    for (failure=19;failure<=20;++failure)
    {
        setup(&f,0,0); f.fail=failure; number=777;
        CHECK(!three_mm_template_write_header(&f.object,&f.file,0x44415441U,&number,&f.s));
        CHECK(number==(failure==19?777U:123U) && f.deleted==(failure==20)); if (f.deleted) CHECK(f.delete_type==0x44415441U);
    }
    setup(&f,0,0); f.fallback=1; f.gap=1;
    CHECK(three_mm_template_initialize(&f.object,&f.file,THREE_MM_TMPL,1,&f.s)); CHECK(f.t.custom_count==2 && f.group.rows==2 && f.fetched==8); disposed(&f);
    for (i=50;i<=51;++i)
    {
        setup(&f,0,0); f.fallback=1; f.custom_count=i; f.sets.count=f.parents.count=1; put(f.sets.data,2,0,0);
        for (failure=0;failure<i;++failure) f.customs[failure].set=0;
        ok=three_mm_template_initialize(&f.object,&f.file,THREE_MM_TMPL,1,&f.s); CHECK(ok==(i==50));
        CHECK(f.diagnostics[THREE_MM_TEMPLATE_COSTUME_LIMIT]==(i==51)); if (ok) CHECK(word(f.group.fixed[0],4,0)==50); disposed(&f);
    }
    puts("C89 template construction, header and default costume checks passed"); return 0;
}
