#include "mm/body_create.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(c) do { if (!(c)) { fprintf(stderr,"body check %d: %s\n",__LINE__,#c); exit(1); } } while (0)
typedef struct List { int count; short values[8]; } List;
typedef struct Part { int role,parent,model; } Part;
typedef struct ActorStorage { Part actors[8]; MMBodyPartBinding bindings[8]; } ActorStorage;
typedef struct Fixture
{
    MMBodyState body;
    MMBodyCreateServices s;
    List sets,parents,copy;
    int fail,highlight,installed,created,released,duplicated,material_alloc,actor_alloc;
    int initialized,linked,hidden,models,materials,freed,sets_released,disposed;
    int events[64],event_count;
} Fixture;
static void event(Fixture *f,int n) { CHECK(f->event_count<64); f->events[f->event_count++]=n; }
static MMBodyState *body_state(void *p,void *body) { Fixture *f=p; CHECK(body==&f->body); return &f->body; }
static void *highlight(void *p) { Fixture *f=p; return f->highlight?&f->highlight:NULL; }
static void *allocate_highlight(void *p) { Fixture *f=p; event(f,1); return f->fail==1?NULL:&f->highlight; }
static void install_highlight(void *p,void *h) { Fixture *f=p; CHECK(h==&f->highlight); f->highlight=1; ++f->installed; event(f,2); }
static void *create(void *p) { Fixture *f=p; CHECK(f->highlight); ++f->created; event(f,3); return f->fail==2?NULL:&f->body; }
static void release(void *p,void *object)
{
    Fixture *f=p;
    if (object==&f->body) { ++f->released; mm_body_shape_dispose(object,&f->s); ++f->disposed; }
    else if (object==&f->copy)
    { CHECK(!f->body.shape.actors && !f->body.shape.bindings && !f->body.shape.materials); ++f->sets_released; event(f,12); }
    else
    {
        Part *part=object;
        CHECK(f->body.shape.hidden && !f->materials && !f->freed);
        CHECK(part==(Part *)f->body.shape.actors+f->models+2 && part->model);
        ++f->models; part->model=0; event(f,10);
    }
}
static void *duplicate(void *p,void *sets)
{
    Fixture *f=p; CHECK(sets==&f->sets); ++f->duplicated; event(f,4);
    if (f->fail==3) return NULL; f->copy=f->sets; return &f->copy;
}
static MMI32 count(void *p,void *list) { (void)p; return ((List *)list)->count; }
static MMI32 item(void *p,void *list,MMI32 i) { (void)p; CHECK(i>=0 && i<((List *)list)->count); return ((List *)list)->values[i]; }
static int allocate_materials(void *p,MMI32 n,void **out)
{
    Fixture *f=p; CHECK(f->body.shape.set_count==n && f->body.shape.sets==&f->copy); ++f->material_alloc; event(f,5);
    if (f->fail==4) { *out=NULL; return 0; }
    *out=calloc((size_t)n+1,sizeof(int)); CHECK(*out); return 1;
}
static int allocate_actors(void *p,MMI32 n,void **out,MMBodyPartBinding **bindings)
{
    ActorStorage *storage;
    Fixture *f=p; CHECK(n==f->body.shape.part_count+2 && f->body.shape.materials); ++f->actor_alloc; event(f,6);
    if (f->fail==5) { *out=NULL; return 0; }
    storage=calloc(1,sizeof(*storage)); CHECK(storage && n<=8);
    *out=storage; *bindings=storage->bindings; return 1;
}
static void initialize(void *p,void *body,void *actors,MMI32 i,int role)
{
    Fixture *f=p; Part *parts=actors; CHECK(body==&f->body && f->body.shape.actors==actors);
    CHECK(i==f->initialized++ && role==(i<2?i:MM_BODY_PART)); parts[i].role=role; parts[i].parent=-1;
    if (i>=2) { parts[i].model=1; f->body.shape.bindings[i].model_owner=&parts[i]; }
    event(f,7);
}
static void assign_matrix(void *p,void *actors,MMI32 i,const MMActorMatrix *matrix)
{
    Fixture *f=p;
    CHECK(actors==f->body.shape.actors && matrix==&f->body.shape.bindings[i].matrix);
    CHECK(matrix->m[0][0]==65536 && matrix->m[3][0]==0);
}
static void link_actor(void *p,void *actors,MMI32 parent,MMI32 child)
{
    Fixture *f=p; Part *parts=actors; CHECK(parent>=0 && parent<child && child==f->linked+1);
    parts[child].parent=parent; ++f->linked; event(f,8);
}
typedef struct DrawCheck { Fixture *fixture; int submitted, fail; } DrawCheck;
static int draw_part(void *p,MMI32 index,MMI32 parent,const MMBodyPartBinding *binding)
{
    DrawCheck *draw=p;
    Fixture *f=draw->fixture;
    const Part *actors=f->body.shape.actors;
    CHECK(index==(draw->submitted?draw->submitted+1:0));
    CHECK(binding==&f->body.shape.bindings[index] && parent==actors[index].parent);
    return ++draw->submitted!=draw->fail;
}
static void hide_body(void *p,void *body)
{
    Fixture *f=p; CHECK(body==&f->body && !f->body.shape.hidden && f->body.shape.actors);
    f->body.shape.hidden=1; ++f->hidden; event(f,9);
}
static void remove_material(void *p,void *body,MMI32 i)
{
    Fixture *f=p; CHECK(body==&f->body && f->models==f->body.shape.part_count && i==f->materials++ && !f->freed); event(f,11);
}
static void free_memory(void *p,void **memory)
{
    Fixture *f=p;
    if (*memory) { CHECK(f->models==(f->body.shape.actors?f->body.shape.part_count:f->models)); ++f->freed; free(*memory); }
    *memory=NULL; event(f,13);
}
static void setup(Fixture *f,int n,int fail)
{
    int i; memset(f,0,sizeof(*f)); f->fail=fail; f->sets.count=f->parents.count=n;
    for (i=0;i<n;++i) { f->sets.values[i]=(short)(i%3*2); f->parents.values[i]=(short)(i?((i-1)/2):-1); }
    f->s.context=f; f->s.state=body_state; f->s.highlight=highlight;
    f->s.allocate_highlight=allocate_highlight; f->s.install_highlight=install_highlight;
    f->s.create=create; f->s.release=release; f->s.duplicate_sets=duplicate; f->s.release_sets=release;
    f->s.count=count; f->s.item=item;
    f->s.allocate_materials=allocate_materials; f->s.allocate_actors=allocate_actors; f->s.initialize_actor=initialize;
    f->s.assign_matrix=assign_matrix; f->s.link=link_actor; f->s.hide=hide_body;
    f->s.remove_material=remove_material; f->s.free_memory=free_memory;
}
int main(void)
{
    Fixture f; int n,fail,warm,mode,i,ok;
    memset(&f.body,0xa5,sizeof(f.body)); mm_body_state_init(&f.body);
    CHECK(!f.body.shape.sets && !f.body.shape.materials && !f.body.shape.actors);
    CHECK(!f.body.shape.set_count && !f.body.shape.part_count && !f.body.shape.hidden);
    CHECK(!f.body.render.context && !f.body.render.attach && !f.body.render.changed);
    CHECK(!f.body.geometry.bounds.right && !f.body.geometry.last_visible.right && !f.body.geometry.found && !f.body.geometry.set);
    for (n=0;n<=6;n+=6) for (fail=0;fail<=5;++fail) for (warm=0;warm<2;++warm)
    {
        void *body; setup(&f,n,fail); f.highlight=warm;
        body=mm_body_create(&f.parents,&f.sets,&f.s);
        ok=!fail || (warm && fail==1); CHECK((body!=NULL)==ok);
        CHECK(f.installed==(!warm && fail!=1));
        if (body)
        {
            MMBodyDrawView view;
            DrawCheck draw;
            CHECK(f.body.shape.hidden==1 && f.body.shape.part_count==n && f.body.shape.set_count==(n?5:0));
            CHECK(f.initialized==n+2 && f.linked==n+1);
            for (i=0;i<n;++i)
            {
                CHECK(f.body.shape.bindings[i+2].parent==f.parents.values[i]);
                CHECK(((Part *)f.body.shape.actors)[i+2].parent==(i?f.parents.values[i]+2:0));
            }
            view.bindings=f.body.shape.bindings; view.part_count=n;
            draw.fixture=&f; draw.submitted=0; draw.fail=0;
            CHECK(mm_body_draw(&view,&draw,draw_part) && draw.submitted==n+1);
            draw.submitted=0; draw.fail=n?2:1;
            CHECK(!mm_body_draw(&view,&draw,draw_part) && draw.submitted==draw.fail);
            f.body.shape.hidden=0; release(&f,body); CHECK(f.hidden==1 && f.models==n && f.materials==(n?5:0));
        }
        CHECK(!f.body.shape.actors && !f.body.shape.materials && !f.body.shape.sets);
        CHECK(f.released==(ok || fail>=3));
        CHECK(f.created==(!(fail==1 && !warm)));
        CHECK(f.sets_released==(ok || fail>=4));
        CHECK(f.freed==(ok?2:fail==5?1:0));
    }
    /* Direct callers retain partial ownership, and shape-only initialization
     * does not reset visibility. Cleanup is explicit, not a factory rollback. */
    for (mode=0;mode<2;++mode) for (fail=3;fail<=5;++fail)
    {
        setup(&f,6,fail); f.body.shape.hidden=7;
        ok=mode?mm_body_initialize(&f.body,&f.parents,&f.sets,&f.s):
            mm_body_shape_initialize(&f.body,&f.parents,&f.sets,&f.s);
        CHECK(!ok && !f.released && !f.freed && f.body.shape.hidden==(mode?1:7));
        CHECK((f.body.shape.sets!=NULL)==(fail>3)); CHECK((f.body.shape.materials!=NULL)==(fail>4));
        CHECK(f.body.shape.set_count==(fail>3?5:0) && f.body.shape.part_count==(fail>4?6:0));
        mm_body_shape_dispose(&f.body,&f.s);
        CHECK(!f.body.shape.sets && !f.body.shape.materials && !f.body.shape.actors && !f.hidden);
        CHECK(!f.materials && !f.models && f.freed==(fail==5));
    }
    puts("C89 body construction and partial ownership checks passed"); return 0;
}
