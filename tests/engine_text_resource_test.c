#include "three_mm/text_resource.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>
typedef struct Root { int refs,object; } Root;
typedef struct Object
{
    Root root; ThreeMMTextDescriptor descriptor; ThreeMMTextGenerated generated;
    ThreeMMTemplateState template_state; char text[256]; int provider;
} Object;
typedef struct Context
{
    ThreeMMTextResourceServices services; char saved[256]; unsigned char wire[24];
    ThreeMMI32 bytes; int fail,step,live,header,child,found,unpacked,appended,deleted;
    int error,released_error,bad,warnings,custom,defaults,names_published;
    ThreeMMTextNames *names;
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
        o=(Object *)v; three_mm_text_dispose(&o->generated,&c->services.generation);
        if (o->template_state.parents) release(c,o->template_state.parents);
        if (o->template_state.sets) release(c,o->template_state.sets);
        if (o->template_state.costumes) release(c,o->template_state.costumes);
    }
    --c->live; free(v);
}
static void *create(void *p) { return allocate((Context *)p,sizeof(Object),1); }
static ThreeMMTextDescriptor *state(void *p,void *o) { (void)p; return &((Object *)o)->descriptor; }
static ThreeMMTextGenerated *generated(void *p,void *o) { (void)p; return &((Object *)o)->generated; }
static void *text(void *p,void *o) { (void)p; return ((Object *)o)->text; }
static ThreeMMI32 length(void *p,void *o) { (void)p; return (ThreeMMI32)strlen(((Object *)o)->text); }
static void assign(void *p,void *to,const void *from) { (void)p; copy_string((char *)to,(const char *)from); }
static void space(void *p,void *t) { (void)p; copy_string((char *)t," "); }
static void clear(void *p,void *t) { (void)p; *(char *)t=0; }
static void *saved(void *p) { return ((Context *)p)->saved; }
static void provider_copy(void *p,void *to,void *from) { (void)p; ((Object *)to)->provider=((Object *)from)->provider; }
static int has_provider(void *p,void *o) { (void)p; return ((Object *)o)->provider; }
static void *provider(void *p,void *o,const ThreeMMTag *t) { (void)o; (void)t; ++((Context *)p)->custom; return NULL; }
static void *default_font(void *p,const ThreeMMTag *t) { (void)t; ++((Context *)p)->defaults; return p; }
static void publish_tag(void *p,void *out,const ThreeMMTag *t) { (void)p; memcpy(out,t,sizeof(*t)); }
static void template_read(void *p,void *o,ThreeMMTemplateState *t) { (void)p; *t=((Object *)o)->template_state; }
static void template_write(void *p,void *o,const ThreeMMTemplateState *t) { (void)p; ((Object *)o)->template_state=*t; }
static void *list_new(void *p,ThreeMMI32 bytes,ThreeMMI32 count) { (void)bytes; (void)count; return allocate((Context *)p,sizeof(Root),0); }
static void list_count(void *p,void *list,ThreeMMI32 n) { (void)p; (void)list; (void)n; }
static void list_put(void *p,void *list,ThreeMMI32 i,const void *v) { (void)p; (void)list; (void)i; (void)v; }
static void *group_new(void *p,ThreeMMI32 fixed,ThreeMMI32 rows,ThreeMMI32 bytes) { (void)fixed; (void)rows; (void)bytes; return list_new(p,0,0); }
static int group_add(void *p,void *g,const void *fixed,ThreeMMI32 bytes,const void *v) { (void)g; (void)fixed; (void)bytes; (void)v; return ++((Context *)p)->step!=((Context *)p)->fail; }
static void *material_new(void *p) { return list_new(p,0,0); }
static int read_template(void *p,void *o,void *file,ThreeMMU32 type,ThreeMMU32 n)
{ Context *c=(Context *)p; (void)file; (void)type; (void)n; copy_string(((Object *)o)->text,"loaded"); return c->header; }
static int write_template(void *p,void *o,void *file,ThreeMMU32 type,void *out)
{ ThreeMMU32 n=37; (void)o; (void)file; (void)type; memcpy(out,&n,4); return ((Context *)p)->header; }
static int child(void *p,void *file,ThreeMMU32 type,ThreeMMU32 n,ThreeMMU32 *out)
{ (void)file; (void)type; assert(n==37); *out=19; return ((Context *)p)->child; }
static void *find(void *p,void *file,ThreeMMU32 n) { (void)file; assert(n==19); return ((Context *)p)->found?p:NULL; }
static int unpack(void *p,void *b) { assert(p==b); return ((Context *)p)->unpacked; }
static ThreeMMI32 block_size(void *p,void *b) { (void)b; return ((Context *)p)->bytes; }
static int read_block(void *p,void *b,void *out,ThreeMMI32 n) { (void)b; assert(n==24); memcpy(out,((Context *)p)->wire,24); return 1; }
static void *add(void *p,void *file,ThreeMMU32 type,ThreeMMU32 n,ThreeMMU32 *out) { (void)file; (void)type; assert(n==37); *out=19; return ((Context *)p)->child?p:NULL; }
static int write_block(void *p,void *b,const void *in,ThreeMMI32 n) { (void)b; assert(n==24); memcpy(((Context *)p)->wire,in,24); return ((Context *)p)->found; }
static void delete_child(void *p,void *file,ThreeMMU32 type,ThreeMMU32 n,ThreeMMU32 kid) { (void)file; (void)type; assert(n==37 && kid==19); ++((Context *)p)->deleted; }
static void publish_error(void *p,void *out,int v) { ((Context *)p)->error=v; *(int *)out=v; }
static void *tag_list(void *p) { assert(!((Context *)p)->error); return list_new(p,0,0); }
static int append_tag(void *p,void *list,const ThreeMMTag *t) { (void)list; assert(!t->cache); ++((Context *)p)->appended; return ((Context *)p)->child!=2; }
static ThreeMMI32 name_count(void *p,void *t) { (void)p; (void)t; return 3; }
static ThreeMMI32 name_id(void *p,void *t,ThreeMMI32 i) { (void)p; (void)t; return i==1?0:7; }
static void name_text(void *p,void *t,ThreeMMI32 i,void *out) { (void)p; (void)t; copy_string((char *)out,i==0?"Pulse":i==1?"Rest":"duplicate"); }
static void missing(void *p) { ++((Context *)p)->warnings; }
static void bad(void *p) { ++((Context *)p)->bad; }
static void init(Context *c)
{
    ThreeMMTextResourceServices *s; ThreeMMTextGenerationServices *g;
    memset(c,0,sizeof(*c)); c->bytes=24; c->header=c->child=c->found=c->unpacked=1; s=&c->services; g=&s->generation;
    s->context=c; g->context=c; g->state=generated; g->length=length; g->template_read=template_read; g->template_write=template_write;
    g->list_new=list_new; g->list_count=list_count; g->list_put=list_put; g->group_new=group_new; g->group_add=group_add; g->material_new=material_new; g->retain=retain; g->release=release;
    s->state=state; s->create=create; s->text=text; s->assign=assign; s->space=space; s->clear=clear; s->saved_text=saved; s->provider_copy=provider_copy;
    s->has_provider=has_provider; s->provider_font=provider; s->default_font=default_font; s->publish_tag=publish_tag; s->read_template=read_template; s->write_template=write_template;
    s->child=child; s->find=find; s->unpack=unpack; s->block_size=block_size; s->read=read_block; s->add=add; s->write=write_block; s->delete_child=delete_child;
    s->publish_error=publish_error; s->tag_list=tag_list; s->append_tag=append_tag; s->name_count=name_count; s->name_id=name_id; s->name_text=name_text; s->missing_name=missing; s->bad_order=bad;
}
static void lifecycle(void)
{
    Context c; Object *o,*copy; ThreeMMTag font={123,NULL,0x54444620U,29},out; ThreeMMI32 shape; char value[256]; int failure;
    font.cache=&font;
    for (failure=0;failure<=6;++failure)
    {
        init(&c); c.fail=failure; o=(Object *)three_mm_text_create("",9,&font,&c.services);
        assert((o!=NULL)==(failure==0)); if (o) { assert(!strcmp(o->text," ")); release(&c,o); } assert(!c.live);
    }
    init(&c); o=(Object *)three_mm_text_create("old",9,&font,&c.services); assert(o); o->provider=1;
    assert(!three_mm_text_font(o,&c.services) && c.custom==1 && !c.defaults);
    o->provider=0; assert(three_mm_text_font(o,&c.services)==&c && c.defaults==1); o->provider=1;
    for (failure=1;failure<=4;++failure)
    {
        void *parents=o->template_state.parents; c.step=0; c.fail=failure;
        assert(!three_mm_text_change(o,"",0,NULL,&c.services));
        assert(!strcmp(o->text,"old") && o->descriptor.shape==9 && o->descriptor.font.cache==font.cache && o->template_state.parents==parents);
    }
    c.fail=0; assert(three_mm_text_change(o,"",0,NULL,&c.services) && !strcmp(o->text," "));
    o->generated.action=list_new(&c,0,0); o->generated.action_id=7;
    assert(three_mm_text_change(o,NULL,-1,NULL,&c.services) && !o->generated.action && o->generated.action_id==-1);
    copy=(Object *)three_mm_text_duplicate(o,&c.services); assert(copy && copy->provider && copy->generated.material!=o->generated.material);
    three_mm_text_info(copy,value,&shape,&out,&c.services); assert(!strcmp(value," ") && !shape && out.cache==font.cache && out.sid==123);
    three_mm_text_info(copy,NULL,NULL,NULL,&c.services); release(&c,copy); release(&c,o); assert(!c.live);
}
static void wire(void)
{
    Context c; Object *o,*loaded; void *tags; ThreeMMTag font={-1,NULL,0xfedcba98U,0x87654321U}; ThreeMMU32 number; int endian,failure,error;
    for (endian=0;endian<2;++endian)
    {
        init(&c); c.services.host_big_endian=endian; c.services.system=0x1234; o=(Object *)three_mm_text_create("saved",9,&font,&c.services); assert(o);
        assert(three_mm_text_write(o,&c,1,&number,&c.services) && number==37);
        assert(c.wire[endian?1:0]==1 && c.wire[2]==(endian?0x12:0x34) && c.wire[3]==(endian?0x34:0x12));
        assert(!c.wire[12] && !c.wire[13] && !c.wire[14] && !c.wire[15]);
        for (failure=0;failure<2;++failure)
        {
            loaded=(Object *)create(&c); c.services.host_big_endian=endian^failure;
            assert(three_mm_text_read(loaded,&c,1,37,&c.services));
            assert(loaded->descriptor.shape==9 && loaded->descriptor.font.sid==-1 && loaded->descriptor.font.type==font.type && loaded->descriptor.font.number==font.number && !loaded->descriptor.font.cache);
            release(&c,loaded);
        }
        for (failure=0;failure<7;++failure)
        {
            c.step=0; c.fail=failure==1?1:0; c.child=failure==2?0:failure==6?2:1; c.found=failure!=3; c.unpacked=failure!=4; c.bytes=failure==5?23:100;
            error=-1; c.released_error=-1; tags=three_mm_text_tags(&c,1,37,&error,&c.services);
            assert((tags!=NULL)==!failure && error==(failure!=0));
            if (tags) release(&c,tags); else if (failure!=1) assert(c.released_error==1);
        }
        c.fail=0; c.bytes=24; c.child=0; c.found=1;
        assert(!three_mm_text_write(o,&c,1,&number,&c.services) && number==37 && !c.deleted);
        c.child=1; c.found=0; assert(!three_mm_text_write(o,&c,1,&number,&c.services) && c.deleted==1);
        c.header=0; c.found=1; loaded=(Object *)create(&c); assert(!three_mm_text_read(loaded,&c,1,37,&c.services) && !strcmp(loaded->text,"loaded")); release(&c,loaded);
        c.header=1; c.wire[0]=c.wire[1]=0x7f; loaded=(Object *)create(&c);
        assert(three_mm_text_read(loaded,&c,1,37,&c.services) && c.bad==1); release(&c,loaded);
        release(&c,o); assert(!c.live);
    }
}
static void names(void)
{
    Context c; ThreeMMTextNames n={NULL}; void *table; char out[256];
    init(&c); copy_string(out,"dirty"); assert(!three_mm_text_action_name(&n,0,out,&c.services) && !out[0] && !c.warnings);
    table=list_new(&c,0,0); c.names=&n; assert(three_mm_text_set_names(&n,table,&c.services) && c.names_published && ((Root *)table)->refs==2);
    assert(three_mm_text_action_name(&n,7,out,&c.services) && !strcmp(out,"Pulse"));
    assert(three_mm_text_action_name(&n,0,out,&c.services) && !strcmp(out,"Rest"));
    assert(!three_mm_text_action_name(&n,15,out,&c.services) && !out[0] && c.warnings==1);
    release(&c,table); release(&c,table); assert(!c.live);
}
int main(void) { lifecycle(); wire(); names(); return 0; }
