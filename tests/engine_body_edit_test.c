#include "mm/body_edit.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(c) do { if (!(c)) { fprintf(stderr,"body edit check %d: %s\n",__LINE__,#c); exit(1); } } while (0)
typedef struct Ref { int count, handle; } Ref;
typedef struct Part { void *owner,*model,*material; int role,parent,matrix; } Part;
typedef struct ActorStorage { Part actors[10]; MMBodyPartBinding bindings[10]; } ActorStorage;
typedef struct Body { int kind; MMBodyState state; } Body;
typedef struct Fixture
{
    MMBodyEditServices s; Ref model,generic,custom;
    MMPartIndexView parents,sets,new_parents,new_sets;
    short parent_values[8],set_values[8],new_parent_values[8],new_set_values[8];
    Body *original;
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
    if (object==&f->model || object==&f->generic || object==&f->custom) { drop(object); return; }
    CHECK(((Body *)object)->kind==1);
    event(f,object==f->original?90:80);
    mm_body_shape_dispose(object,&f->s.runtime.shape);
    ++f->released;
    free(object);
}
static int allocate_materials(void *p,MMI32 n,void ***out)
{ Fixture *f=p; *out=NULL; if (fail(f)) return 0; *out=calloc((size_t)n+1,sizeof(**out)); CHECK(*out); return 1; }
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
static void attach(void *p,void *object)
{
    Fixture *f=p; Body *b=object; CHECK(!b->state.shape.hidden); ++f->show; event(f,object==f->original?30:31);
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
static void retain(void *p,void *object) { (void)p; CHECK(object); ++((Ref *)object)->count; }
static void identify(void *p,void *b,void *actors,MMI32 n)
{
    MMI32 i;
    for (i=0;i<n;++i) ((Part *)actors)[i].owner=b;
    ((Fixture *)p)->identified+=n;
}
static void *resource_handle(void *p,void *object) { (void)p; return &((Ref *)object)->handle; }
static void assign_model(void *p,void *actors,MMI32 i,void *model) { (void)p; ((Part *)actors)[i].model=model; }
static void assign_material(void *p,void *actors,MMI32 i,void *material) { (void)p; ((Part *)actors)[i].material=material; }
static MMI32 custom_set(void *p,void *material) { CHECK(material==&((Fixture *)p)->custom); return 1; }
static MMI32 custom_count(void *p,void *material) { CHECK(material==&((Fixture *)p)->custom); return 4; }
static void *custom_model(void *p,void *material,MMI32 slot) { (void)p; (void)material; (void)slot; return NULL; }
static void *custom_material(void *p,void *material,MMI32 slot) { CHECK(slot<4); return resource_handle(p,material); }
static int highlighted(void *p,void *actors) { (void)p; return ((Part *)actors)[1].role==MM_BODY_PART; }
static void highlight(void *p,void *actors,int enabled) { (void)p; ((Part *)actors)[1].role=enabled?MM_BODY_PART:MM_BODY_HIGHLIGHT; }
static void list(MMPartIndexView *view,const short *values,int n) { view->bytes=(const unsigned char *)values; view->count=n; }
static void setup(Fixture *f,int visible,int n,int hilite)
{
    int i; MMBodyRuntimeServices *runtime=&f->s.runtime; MMBodyCreateServices *c=&runtime->shape; MMActorMatrix matrix;
    memset(f,0,sizeof(*f)); f->model.count=f->generic.count=f->custom.count=1;
    list(&f->sets,f->set_values,6); list(&f->parents,f->parent_values,6);
    list(&f->new_sets,f->new_set_values,n); list(&f->new_parents,f->new_parent_values,n);
    for (i=0;i<6;++i) { f->parent_values[i]=(short)(i?((i-1)/2):-1); f->set_values[i]=(short)(i%2); }
    for (i=0;i<n;++i) { f->new_parent_values[i]=(short)(i?i-1:-1); f->new_set_values[i]=(short)(i%2); }
    c->context=f; c->state=body_state; c->highlight=highlight_material;
    c->create=create; c->release=release; c->release_model=release;
    c->allocate_materials=allocate_materials; c->allocate_actors=allocate_actors; c->initialize_actor=initialize;
    c->assign_matrix=assign_matrix; c->assign_material=assign_material;
    c->link=link_actor; c->hide=hide; c->free_memory=free_memory;
    runtime->retain=retain; runtime->retain_model=retain; runtime->model_handle=resource_handle; runtime->assign_model=assign_model;
    runtime->material_handle=resource_handle; runtime->custom_set=custom_set; runtime->custom_count=custom_count;
    runtime->custom_model=custom_model; runtime->custom_material=custom_material; runtime->highlight=highlight;
    f->s.copy_actors=copy_actors; f->s.identify=identify; f->s.highlighted=highlighted;
    f->original=mm_body_create(&f->parents,&f->sets,c); CHECK(f->original);
    mm_body_set_generic(f->original,0,&f->generic,runtime); mm_body_set_custom(f->original,&f->custom,runtime);
    mm_matrix_identity(&matrix);
    for (i=0;i<6;++i)
    {
        matrix.m[3][0]=100+i;
        mm_body_shape_matrix(&f->original->state.shape,i+2,&matrix,c);
        if (!(i%2)) mm_body_set_model(f->original,i,&f->model,runtime);
    }
    matrix.m[3][0]=37;
    matrix.m[0][0]=2*65536L;
    mm_body_shape_matrix(&f->original->state.shape,0,&matrix,c);
    f->original->state.shape.hidden=visible; f->original->state.render.context=f; f->original->state.geometry.bounds.left=2; f->original->state.geometry.last_visible.left=7;
    f->original->state.render.attach=attach;
    if (hilite) mm_body_highlight(f->original,1,runtime);
    f->event_count=0; f->allocation=0; f->hide=f->show=0;
}
static void refs(Fixture *f,Body *a,Body *b)
{
    Body *bodies[2]; int models=1,generic=1,custom=1,i,j;
    bodies[0]=a; bodies[1]=b;
    for (i=0;i<2;++i) if (bodies[i])
    {
        Body *body=bodies[i]; Part *parts=body->state.shape.actors; void **materials=body->state.shape.custom_materials;
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
            if (binding->material_owner)
            {
                CHECK(binding->material_owner==&f->generic && binding->material==&f->generic.handle);
                ++generic;
            }
            else if (binding->material) CHECK(binding->material==&f->custom.handle);
        }
    }
    CHECK(f->model.count==models && f->generic.count==generic && f->custom.count==custom);
}
int main(void)
{
    Fixture f; Body *unfinished_copy; int operation,visible,n,hilite,failure,i,ok;
    for (operation=0;operation<2;++operation) for (visible=0;visible<4;visible+=visible?2:1)
    for (n=4;n<=8;n+=4) for (hilite=0;hilite<2;++hilite) for (failure=0;failure<9;++failure)
    {
        Body *copy=NULL; MMActorMatrix world;
        setup(&f,visible,n,hilite); f.fail=failure;
        if (operation) ok=mm_body_change_shape(f.original,&f.new_parents,&f.new_sets,&f.s);
        else { copy=mm_body_duplicate(f.original,&f.s); ok=copy!=NULL; }
        CHECK(ok==(!failure || failure>(operation?5:3)));
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
            CHECK(f.original->state.shape.bindings[i+2].parent==(operation && ok?f.new_parent_values[i]:f.parent_values[i]));
            CHECK(f.original->state.shape.bindings[i+2].set==i%2);
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
    setup(&f,1,6,0);
    mm_body_remove_material(f.original,0,&f.s.runtime);
    unfinished_copy=mm_body_duplicate(f.original,&f.s);
    CHECK(unfinished_copy && f.generic.count==1 && f.custom.count==3);
    refs(&f,f.original,unfinished_copy);
    release(&f,unfinished_copy); release(&f,f.original);
    CHECK(f.model.count==1 && f.generic.count==1 && f.custom.count==1 && f.created==f.released);
    puts("C89 body duplication, restoration and reshape checks passed"); return 0;
}
