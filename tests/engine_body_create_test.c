#include "three_mm/body_create.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(c) do { if (!(c)) { fprintf(stderr,"body check %d: %s\n",__LINE__,#c); exit(1); } } while (0)
typedef struct List { int count; short values[8]; } List;
typedef struct Part { int role,parent,model; } Part;
typedef struct Fixture
{
    ThreeMMBodyShape body;
    ThreeMMBodyCreateServices s;
    List sets,parents,copy;
    int fail,highlight,installed,created,released,duplicated,material_alloc,actor_alloc;
    int initialized,linked,hidden,models,materials,freed,sets_released,disposed;
    int events[64],event_count;
} Fixture;
static void event(Fixture *f,int n) { CHECK(f->event_count<64); f->events[f->event_count++]=n; }
static void read_body(void *p,void *body,ThreeMMBodyShape *b) { Fixture *f=p; CHECK(body==&f->body); *b=f->body; }
static void write_body(void *p,void *body,const ThreeMMBodyShape *b) { Fixture *f=p; CHECK(body==&f->body); f->body=*b; }
static void *highlight(void *p) { Fixture *f=p; return f->highlight?&f->highlight:NULL; }
static void *allocate_highlight(void *p) { Fixture *f=p; event(f,1); return f->fail==1?NULL:&f->highlight; }
static void install_highlight(void *p,void *h) { Fixture *f=p; CHECK(h==&f->highlight); f->highlight=1; ++f->installed; event(f,2); }
static void *create(void *p) { Fixture *f=p; CHECK(f->highlight); ++f->created; event(f,3); return f->fail==2?NULL:&f->body; }
static void release(void *p,void *object)
{
    Fixture *f=p;
    if (object==&f->body) { ++f->released; three_mm_body_shape_dispose(object,&f->s); ++f->disposed; }
    else { CHECK(object==&f->copy && !f->body.actors && !f->body.materials); ++f->sets_released; event(f,12); }
}
static void *duplicate(void *p,void *sets)
{
    Fixture *f=p; CHECK(sets==&f->sets); ++f->duplicated; event(f,4);
    if (f->fail==3) return NULL; f->copy=f->sets; return &f->copy;
}
static ThreeMMI32 count(void *p,void *list) { (void)p; return ((List *)list)->count; }
static ThreeMMI32 item(void *p,void *list,ThreeMMI32 i) { (void)p; CHECK(i>=0 && i<((List *)list)->count); return ((List *)list)->values[i]; }
static int allocate_materials(void *p,ThreeMMI32 n,void **out)
{
    Fixture *f=p; CHECK(f->body.set_count==n && f->body.sets==&f->copy); ++f->material_alloc; event(f,5);
    if (f->fail==4) { *out=NULL; return 0; }
    *out=calloc((size_t)n+1,sizeof(int)); CHECK(*out); return 1;
}
static int allocate_actors(void *p,ThreeMMI32 n,void **out)
{
    Fixture *f=p; CHECK(n==f->body.part_count+2 && f->body.materials); ++f->actor_alloc; event(f,6);
    if (f->fail==5) { *out=NULL; return 0; }
    *out=calloc((size_t)n,sizeof(Part)); CHECK(*out); return 1;
}
static void initialize(void *p,void *body,void *actors,ThreeMMI32 i,int role)
{
    Fixture *f=p; Part *parts=actors; CHECK(body==&f->body && f->body.actors==actors);
    CHECK(i==f->initialized++ && role==(i<2?i:THREE_MM_BODY_PART)); parts[i].role=role; parts[i].parent=-1;
    if (i>=2) parts[i].model=1;
    event(f,7);
}
static void link_actor(void *p,void *actors,ThreeMMI32 parent,ThreeMMI32 child)
{
    Fixture *f=p; Part *parts=actors; CHECK(parent>=0 && parent<child && child==f->linked+1);
    parts[child].parent=parent; ++f->linked; event(f,8);
}
static void hide_body(void *p,void *body)
{
    Fixture *f=p; CHECK(body==&f->body && !f->body.hidden && f->body.actors);
    f->body.hidden=1; ++f->hidden; event(f,9);
}
static void release_model(void *p,void *actors,ThreeMMI32 i)
{
    Fixture *f=p; Part *parts=actors;
    CHECK(f->body.hidden && i==f->models++ && !f->materials && !f->freed);
    CHECK(parts[i+2].model); parts[i+2].model=0; event(f,10);
}
static void remove_material(void *p,void *body,ThreeMMI32 i)
{
    Fixture *f=p; CHECK(body==&f->body && f->models==f->body.part_count && i==f->materials++ && !f->freed); event(f,11);
}
static void free_memory(void *p,void **memory)
{
    Fixture *f=p;
    if (*memory) { CHECK(f->models==(f->body.actors?f->body.part_count:f->models)); ++f->freed; free(*memory); }
    *memory=NULL; event(f,13);
}
static void setup(Fixture *f,int n,int fail)
{
    int i; memset(f,0,sizeof(*f)); f->fail=fail; f->sets.count=f->parents.count=n;
    for (i=0;i<n;++i) { f->sets.values[i]=(short)(i%3*2); f->parents.values[i]=(short)(i?((i-1)/2):-1); }
    f->s.context=f; f->s.read=read_body; f->s.write=write_body; f->s.highlight=highlight;
    f->s.allocate_highlight=allocate_highlight; f->s.install_highlight=install_highlight;
    f->s.create=create; f->s.release=release; f->s.duplicate_sets=duplicate; f->s.count=count; f->s.item=item;
    f->s.allocate_materials=allocate_materials; f->s.allocate_actors=allocate_actors; f->s.initialize_actor=initialize;
    f->s.link=link_actor; f->s.hide=hide_body; f->s.release_model=release_model;
    f->s.remove_material=remove_material; f->s.free_memory=free_memory;
}
int main(void)
{
    Fixture f; int n,fail,warm,mode,i,ok;
    for (n=0;n<=6;n+=6) for (fail=0;fail<=5;++fail) for (warm=0;warm<2;++warm)
    {
        void *body; setup(&f,n,fail); f.highlight=warm;
        body=three_mm_body_create(&f.parents,&f.sets,&f.s);
        ok=!fail || (warm && fail==1); CHECK((body!=NULL)==ok);
        CHECK(f.installed==(!warm && fail!=1));
        if (body)
        {
            CHECK(f.body.hidden==1 && f.body.part_count==n && f.body.set_count==(n?5:0));
            CHECK(f.initialized==n+2 && f.linked==n+1);
            for (i=0;i<n;++i) CHECK(((Part *)f.body.actors)[i+2].parent==(i?f.parents.values[i]+2:0));
            f.body.hidden=0; release(&f,body); CHECK(f.hidden==1 && f.models==n && f.materials==(n?5:0));
        }
        CHECK(!f.body.actors && !f.body.materials && !f.body.sets);
        CHECK(f.released==(ok || fail>=3));
        CHECK(f.created==(!(fail==1 && !warm)));
        CHECK(f.sets_released==(ok || fail>=4));
        CHECK(f.freed==(ok?2:fail==5?1:0));
    }
    /* Direct callers retain partial ownership, and shape-only initialization
     * does not reset visibility. Cleanup is explicit, not a factory rollback. */
    for (mode=0;mode<2;++mode) for (fail=3;fail<=5;++fail)
    {
        setup(&f,6,fail); f.body.hidden=7;
        ok=mode?three_mm_body_initialize(&f.body,&f.parents,&f.sets,&f.s):
            three_mm_body_shape_initialize(&f.body,&f.parents,&f.sets,&f.s);
        CHECK(!ok && !f.released && !f.freed && f.body.hidden==(mode?1:7));
        CHECK((f.body.sets!=NULL)==(fail>3)); CHECK((f.body.materials!=NULL)==(fail>4));
        CHECK(f.body.set_count==(fail>3?5:0) && f.body.part_count==(fail>4?6:0));
        three_mm_body_shape_dispose(&f.body,&f.s);
        CHECK(!f.body.sets && !f.body.materials && !f.body.actors && !f.hidden);
        CHECK(!f.materials && !f.models && f.freed==(fail==5));
    }
    puts("C89 body construction and partial ownership checks passed"); return 0;
}
