#include "mm/body_edit.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(c) do { if (!(c)) { fprintf(stderr,"body edit check %d: %s\n",__LINE__,#c); exit(1); } } while (0)
typedef struct Ref { int count, handle; } Ref;
typedef struct List { int kind,count; short values[8]; } List;
typedef struct Part { void *owner,*model; Ref *material; int role,parent,matrix; } Part;
typedef struct ActorStorage { Part actors[10]; MMBodyPartBinding bindings[10]; } ActorStorage;
typedef struct Body { int kind; MMBodyState state; } Body;
typedef struct Fixture
{
    MMBodyEditServices s; Ref model,generic,custom;
    List parents,sets,new_parents,new_sets; Body *original;
    int allocation,fail,created,released,hide,show,rebased,identified;
    int events[256],event_count;
} Fixture;
static void event(Fixture *f,int e) { CHECK(f->event_count<256); f->events[f->event_count++]=e; }
static int fail(Fixture *f) { return ++f->allocation==f->fail; }
static MMBodyState *body_state(void *p,void *body) { (void)p; return &((Body *)body)->state; }
static void *highlight_material(void *p) { return p; }
static void *create(void *p)
{
    Fixture *f=p; Body *b; if (fail(f)) return NULL;
    b=calloc(1,sizeof(*b)); CHECK(b); b->kind=1; ++f->created; event(f,1); return b;
}
static void drop(Ref *r) { CHECK(r && r->count>1); --r->count; }
static void release(void *p,void *object)
{
    Fixture *f=p; CHECK(object);
    if (object==&f->model) { drop(object); return; }
    if (*(int *)object==1) { event(f,object==f->original?90:80); mm_body_shape_dispose(object,&f->s.shape); ++f->released; }
    else CHECK(*(int *)object==2);
    free(object);
}
static void *duplicate_sets(void *p,void *list)
{
    Fixture *f=p; List *copy; if (fail(f)) return NULL;
    copy=malloc(sizeof(*copy)); CHECK(copy); *copy=*(List *)list; return copy;
}
static MMI32 count(void *p,void *list) { (void)p; return ((List *)list)->count; }
static MMI32 item(void *p,void *list,MMI32 i) { (void)p; CHECK(i>=0 && i<((List *)list)->count); return ((List *)list)->values[i]; }
static int allocate_materials(void *p,MMI32 n,void **out)
{ Fixture *f=p; *out=NULL; if (fail(f)) return 0; *out=calloc((size_t)n+1,sizeof(Ref *)); CHECK(*out); return 1; }
static int allocate_actors(void *p,MMI32 n,void **out,MMBodyPartBinding **bindings)
{
    Fixture *f=p;
    ActorStorage *storage;
    *out=NULL; if (fail(f)) return 0;
    storage=calloc(1,sizeof(*storage)); CHECK(storage && n<=10);
    *out=storage; *bindings=storage->bindings; return 1;
}
static void initialize(void *p,void *b,void *actors,MMI32 i,int role)
{ Part *a=actors; (void)p; CHECK(((Body *)b)->state.shape.actors==actors); a[i].owner=b; a[i].role=role; a[i].parent=-1; }
static void assign_matrix(void *p,void *actors,MMI32 i,const MMActorMatrix *matrix)
{ (void)p; ((Part *)actors)[i].matrix=matrix->m[3][0]; }
static void link_actor(void *p,void *actors,MMI32 parent,MMI32 child)
{ (void)p; CHECK(parent>=0 && parent<child); ((Part *)actors)[child].parent=parent; }
static void hide(void *p,void *object)
{
    Fixture *f=p; Body *b=object; ++f->hide; event(f,object==f->original?20:21);
    if (!b->state.shape.hidden++) mm_body_clear_bounds(&b->state.geometry);
}
static void show(void *p,void *object)
{
    Fixture *f=p; Body *b=object; CHECK(b->state.shape.hidden>0); --b->state.shape.hidden; ++f->show; event(f,object==f->original?30:31);
}
static void remove_material(void *p,void *object,MMI32 set)
{
    Body *b=object; Ref **custom=b->state.shape.materials; Part *actors=b->state.shape.actors;
    int i,is_custom=custom[set]!=NULL; (void)p;
    if (is_custom) { drop(custom[set]); custom[set]=NULL; }
    for (i=0;i<b->state.shape.part_count;++i) if (((List *)b->state.shape.sets)->values[i]==set)
    {
        void *raw=b->state.shape.bindings[i+2].material;
        if (raw && !is_custom) drop(raw);
        b->state.shape.bindings[i+2].material=NULL; actors[i+2].material=NULL;
    }
}
static void free_memory(void *p,void **memory) { (void)p; free(*memory); *memory=NULL; }
static void copy_actors(void *p,void *body,void *to,const void *from,MMI32 n)
{
    Fixture *f=p;
    Part *actors=to;
    MMI32 i;
    memcpy(to,from,(size_t)n*sizeof(*actors));
    actors[0].parent=-1;
    for (i=0;i<n;++i) actors[i].owner=body;
    f->rebased+=n;
}
static void copy_materials(void *p,void *to,void *from,MMI32 n) { (void)p; memcpy(to,from,(size_t)n*sizeof(Ref *)); }
static void *material(void *p,void *object,MMI32 set,int *generic)
{
    Body *b=object; Ref **custom=b->state.shape.materials; int i; (void)p;
    if (custom[set]) { *generic=0; return custom[set]; }
    *generic=1;
    for (i=0;i<b->state.shape.part_count;++i) if (((List *)b->state.shape.sets)->values[i]==set)
    { void *r=b->state.shape.bindings[i+2].material; CHECK(r); return r; }
    CHECK(0); return NULL;
}
static void retain(void *p,void *object) { (void)p; CHECK(object); ++((Ref *)object)->count; }
static void identify(void *p,void *b,void *actors,MMI32 n)
{
    MMI32 i;
    for (i=0;i<n;++i) ((Part *)actors)[i].owner=b;
    ((Fixture *)p)->identified+=n;
}
static void set_generic(void *p,void *object,MMI32 set,void *m)
{
    Body *b=object; int i; remove_material(p,b,set);
    for (i=0;i<b->state.shape.part_count;++i) if (((List *)b->state.shape.sets)->values[i]==set)
    { b->state.shape.bindings[i+2].material=m; ((Part *)b->state.shape.actors)[i+2].material=m; retain(p,m); }
}
static void set_custom(void *p,void *object,void *m)
{
    Body *b=object; int i; remove_material(p,b,1); ((Ref **)b->state.shape.materials)[1]=m; retain(p,m);
    for (i=0;i<b->state.shape.part_count;++i) if (((List *)b->state.shape.sets)->values[i]==1)
    { b->state.shape.bindings[i+2].material=m; ((Part *)b->state.shape.actors)[i+2].material=m; }
}
static void set_model(void *p,void *object,MMI32 i,void *m)
{
    MMBodyShape *shape=&((Body *)object)->state.shape;
    MMBodyPartBinding *binding=&shape->bindings[i+2];
    if (binding->model_owner) drop(binding->model_owner);
    binding->model_owner=m; binding->model_handle=&((Ref *)m)->handle;
    ((Part *)shape->actors)[i+2].model=binding->model_handle; retain(p,m);
}
static int highlighted(void *p,void *actors) { (void)p; return ((Part *)actors)[1].role==MM_BODY_PART; }
static void highlight(void *p,void *object) { (void)p; ((Part *)((Body *)object)->state.shape.actors)[1].role=MM_BODY_PART; }
static void list(List *l,int n) { int i; l->kind=2; l->count=n; for (i=0;i<n;++i) l->values[i]=(short)(i%2); }
static void setup(Fixture *f,int visible,int n,int hilite)
{
    int i; MMBodyCreateServices *c; MMActorMatrix matrix;
    memset(f,0,sizeof(*f)); f->model.count=f->generic.count=f->custom.count=1;
    list(&f->sets,6); list(&f->parents,6); list(&f->new_sets,n); list(&f->new_parents,n);
    for (i=0;i<6;++i) f->parents.values[i]=(short)(i?((i-1)/2):-1);
    for (i=0;i<n;++i) f->new_parents.values[i]=(short)(i?i-1:-1);
    c=&f->s.shape; c->context=f; c->state=body_state; c->highlight=highlight_material;
    c->create=create; c->release=release; c->duplicate_sets=duplicate_sets; c->release_sets=release;
    c->count=count; c->item=item;
    c->allocate_materials=allocate_materials; c->allocate_actors=allocate_actors; c->initialize_actor=initialize;
    c->assign_matrix=assign_matrix; c->link=link_actor; c->hide=hide; c->remove_material=remove_material; c->free_memory=free_memory;
    f->s.copy_actors=copy_actors;
    f->s.copy_materials=copy_materials; f->s.material=material; f->s.retain=retain;
    f->s.show=show; f->s.identify=identify; f->s.set_generic=set_generic; f->s.set_custom=set_custom;
    f->s.set_model=set_model; f->s.highlighted=highlighted; f->s.highlight=highlight;
    f->original=mm_body_create(&f->parents,&f->sets,c); CHECK(f->original);
    set_generic(f,f->original,0,&f->generic); set_custom(f,f->original,&f->custom);
    mm_matrix_identity(&matrix);
    for (i=0;i<6;++i)
    {
        matrix.m[3][0]=100+i;
        mm_body_shape_matrix(&f->original->state.shape,i+2,&matrix,c);
        if (!(i%2)) set_model(f,f->original,i,&f->model);
    }
    matrix.m[3][0]=37;
    matrix.m[0][0]=2*65536L;
    mm_body_shape_matrix(&f->original->state.shape,0,&matrix,c);
    f->original->state.shape.hidden=visible; f->original->state.render.context=f; f->original->state.geometry.bounds.left=2; f->original->state.geometry.last_visible.left=7;
    if (hilite) highlight(f,f->original);
    f->event_count=0; f->allocation=0; f->hide=f->show=0;
}
static void refs(Fixture *f,Body *a,Body *b)
{
    Body *bodies[2]; int models=1,generic=1,custom=1,i,j;
    bodies[0]=a; bodies[1]=b;
    for (i=0;i<2;++i) if (bodies[i])
    {
        Body *body=bodies[i]; Part *parts=body->state.shape.actors; Ref **materials=body->state.shape.materials;
        for (j=0;j<body->state.shape.set_count;++j) if (materials[j]) ++custom;
        for (j=0;j<body->state.shape.part_count;++j)
        {
            const MMBodyPartBinding *binding=&body->state.shape.bindings[j+2];
            CHECK(binding->model_handle==parts[j+2].model && binding->material==parts[j+2].material);
            if (binding->model_owner)
            {
                CHECK(binding->model_owner==&f->model && binding->model_handle==&f->model.handle);
                ++models;
            }
            else CHECK(!binding->model_handle);
            if (binding->material==&f->generic) ++generic;
        }
    }
    CHECK(f->model.count==models && f->generic.count==generic && f->custom.count==custom);
}
int main(void)
{
    Fixture f; int operation,visible,n,hilite,failure,i,ok;
    for (operation=0;operation<2;++operation) for (visible=0;visible<4;visible+=visible?2:1)
    for (n=4;n<=8;n+=4) for (hilite=0;hilite<2;++hilite) for (failure=0;failure<9;++failure)
    {
        Body *copy=NULL; MMActorMatrix world;
        setup(&f,visible,n,hilite); f.fail=failure;
        if (operation) ok=mm_body_change_shape(f.original,&f.new_parents,&f.new_sets,&f.s);
        else { copy=mm_body_duplicate(f.original,&f.s); ok=copy!=NULL; }
        CHECK(ok==(!failure || failure>(operation?7:4)));
        CHECK(f.original->state.shape.hidden==visible && f.original->state.render.context==&f);
        CHECK(f.original->state.geometry.last_visible.left==7 && f.original->state.geometry.bounds.left==(visible?2:0));
        refs(&f,f.original,copy);
        if (copy)
        {
            CHECK(copy->state.shape.hidden==1 && copy->state.render.context==&f && copy->state.geometry.bounds.left==f.original->state.geometry.bounds.left);
            mm_body_restore(f.original,copy,&f.s);
            CHECK(copy->state.shape.hidden==visible);
            for (i=0;i<copy->state.shape.part_count+2;++i)
                CHECK(((Part *)copy->state.shape.actors)[i].owner==copy);
            release(&f,copy); copy=NULL;
            CHECK(f.original->state.shape.hidden==visible && f.identified==16);
        }
        CHECK(f.original->state.shape.part_count==(operation && ok?n:6));
        CHECK(((Part *)f.original->state.shape.actors)[0].matrix==37 && highlighted(&f,f.original->state.shape.actors)==hilite);
        mm_body_shape_part_world(&f.original->state.shape,3,&world);
        CHECK(world.m[0][0]==2*65536L && world.m[3][0]==37+2*(operation && ok?406:304));
        for (i=0;i<f.original->state.shape.part_count;++i)
        {
            Part *a=(Part *)f.original->state.shape.actors+i+2;
            CHECK(a->owner==f.original && a->matrix==(i<6?100+i:0));
            CHECK((a->model!=NULL)==(i<6 && !(i%2)));
            CHECK(f.original->state.shape.bindings[i+2].parent==(operation && ok?f.new_parents.values[i]:f.parents.values[i]));
        }
        if (!operation && !ok && failure>1 && !visible)
        {
            int show_at=-1,release_at=-1;
            for (i=0;i<f.event_count;++i) { if (f.events[i]==30) show_at=i; if (f.events[i]==80) release_at=i; }
            CHECK(show_at>=0 && release_at>show_at);
        }
        refs(&f,f.original,NULL); release(&f,f.original);
        CHECK(f.model.count==1 && f.generic.count==1 && f.custom.count==1 && f.created==f.released);
    }
    puts("C89 body duplication, restoration and reshape checks passed"); return 0;
}
