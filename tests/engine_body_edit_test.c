#include "three_mm/body_edit.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(c) do { if (!(c)) { fprintf(stderr,"body edit check %d: %s\n",__LINE__,#c); exit(1); } } while (0)
typedef struct Ref { int count; } Ref;
typedef struct List { int kind,count; short values[8]; } List;
typedef struct Part { void *owner; Ref *model,*material; int role,parent,matrix; } Part;
typedef struct Body { int kind,binding,bounds[2]; ThreeMMBodyShape shape; } Body;
typedef struct Fixture
{
    ThreeMMBodyEditServices s; Ref model,generic,custom;
    List parents,sets,new_parents,new_sets; Body *original;
    int allocation,fail,created,released,hide,show,rebased,identified;
    int events[256],event_count;
} Fixture;
static void event(Fixture *f,int e) { CHECK(f->event_count<256); f->events[f->event_count++]=e; }
static int fail(Fixture *f) { return ++f->allocation==f->fail; }
static void read_body(void *p,void *b,ThreeMMBodyShape *s) { (void)p; *s=((Body *)b)->shape; }
static void write_body(void *p,void *b,const ThreeMMBodyShape *s) { (void)p; ((Body *)b)->shape=*s; }
static void *highlight_material(void *p) { return p; }
static void *create(void *p)
{
    Fixture *f=p; Body *b; if (fail(f)) return NULL;
    b=calloc(1,sizeof(*b)); CHECK(b); b->kind=1; ++f->created; event(f,1); return b;
}
static void release(void *p,void *object)
{
    Fixture *f=p; CHECK(object);
    if (*(int *)object==1) { event(f,object==f->original?90:80); three_mm_body_shape_dispose(object,&f->s.shape); ++f->released; }
    else CHECK(*(int *)object==2);
    free(object);
}
static void *duplicate_sets(void *p,void *list)
{
    Fixture *f=p; List *copy; if (fail(f)) return NULL;
    copy=malloc(sizeof(*copy)); CHECK(copy); *copy=*(List *)list; return copy;
}
static ThreeMMI32 count(void *p,void *list) { (void)p; return ((List *)list)->count; }
static ThreeMMI32 item(void *p,void *list,ThreeMMI32 i) { (void)p; CHECK(i>=0 && i<((List *)list)->count); return ((List *)list)->values[i]; }
static int allocate_materials(void *p,ThreeMMI32 n,void **out)
{ Fixture *f=p; *out=NULL; if (fail(f)) return 0; *out=calloc((size_t)n+1,sizeof(Ref *)); CHECK(*out); return 1; }
static int allocate_actors(void *p,ThreeMMI32 n,void **out)
{ Fixture *f=p; *out=NULL; if (fail(f)) return 0; *out=calloc((size_t)n,sizeof(Part)); CHECK(*out); return 1; }
static void initialize(void *p,void *b,void *actors,ThreeMMI32 i,int role)
{ Part *a=actors; (void)p; CHECK(((Body *)b)->shape.actors==actors); a[i].owner=b; a[i].role=role; a[i].parent=-1; }
static void link_actor(void *p,void *actors,ThreeMMI32 parent,ThreeMMI32 child)
{ (void)p; CHECK(parent>=0 && parent<child); ((Part *)actors)[child].parent=parent; }
static void hide(void *p,void *object)
{
    Fixture *f=p; Body *b=object; ++f->hide; event(f,object==f->original?20:21);
    if (!b->shape.hidden++) b->bounds[0]=0;
}
static void show(void *p,void *object)
{
    Fixture *f=p; Body *b=object; CHECK(b->shape.hidden>0); --b->shape.hidden; ++f->show; event(f,object==f->original?30:31);
}
static void drop(Ref *r) { CHECK(r && r->count>1); --r->count; }
static void release_model(void *p,void *actors,ThreeMMI32 part)
{ Part *a=(Part *)actors+part+2; (void)p; if (a->model) { drop(a->model); a->model=NULL; } }
static void remove_material(void *p,void *object,ThreeMMI32 set)
{
    Body *b=object; Ref **custom=b->shape.materials; Part *actors=b->shape.actors;
    int i,is_custom=custom[set]!=NULL; (void)p;
    if (is_custom) { drop(custom[set]); custom[set]=NULL; }
    for (i=0;i<b->shape.part_count;++i) if (((List *)b->shape.sets)->values[i]==set)
    { if (actors[i+2].material && !is_custom) drop(actors[i+2].material); actors[i+2].material=NULL; }
}
static void free_memory(void *p,void **memory) { (void)p; free(*memory); *memory=NULL; }
static void copy_bindings(void *p,void *to,void *from) { (void)p; ((Body *)to)->binding=((Body *)from)->binding; }
static void copy_actors(void *p,void *to,void *from,ThreeMMI32 n) { (void)p; memcpy(to,from,(size_t)n*sizeof(Part)); }
static void rebase(void *p,void *body,void *to,void *from,ThreeMMI32 i,int root)
{
    Fixture *f=p; Part *a=to; (void)from; CHECK(root==(i==0)); ++f->rebased; a[i].owner=body; if (root) a[i].parent=-1;
}
static void retain_model(void *p,void *actors,ThreeMMI32 part)
{ Part *a=(Part *)actors+part+2; (void)p; if (a->model) ++a->model->count; }
static void copy_materials(void *p,void *to,void *from,ThreeMMI32 n) { (void)p; memcpy(to,from,(size_t)n*sizeof(Ref *)); }
static void copy_bounds(void *p,void *to,void *from) { (void)p; memcpy(((Body *)to)->bounds,((Body *)from)->bounds,sizeof(((Body *)to)->bounds)); }
static void *material(void *p,void *object,ThreeMMI32 set,int *generic)
{
    Body *b=object; Ref **custom=b->shape.materials; int i; (void)p;
    if (custom[set]) { *generic=0; return custom[set]; }
    *generic=1;
    for (i=0;i<b->shape.part_count;++i) if (((List *)b->shape.sets)->values[i]==set)
    { Ref *r=((Part *)b->shape.actors)[i+2].material; CHECK(r); return r; }
    CHECK(0); return NULL;
}
static void retain(void *p,void *object) { (void)p; CHECK(object); ++((Ref *)object)->count; }
static void swap_bodies(void *p,void *a,void *b)
{ Body temp=*(Body *)a; (void)p; CHECK(((Body *)b)->shape.hidden==1); *(Body *)a=*(Body *)b; *(Body *)b=temp; }
static void identify(void *p,void *b,void *actors,ThreeMMI32 i) { ++((Fixture *)p)->identified; ((Part *)actors)[i].owner=b; }
static void set_generic(void *p,void *object,ThreeMMI32 set,void *m)
{
    Body *b=object; int i; remove_material(p,b,set);
    for (i=0;i<b->shape.part_count;++i) if (((List *)b->shape.sets)->values[i]==set)
    { ((Part *)b->shape.actors)[i+2].material=m; retain(p,m); }
}
static void set_custom(void *p,void *object,void *m)
{
    Body *b=object; int i; remove_material(p,b,1); ((Ref **)b->shape.materials)[1]=m; retain(p,m);
    for (i=0;i<b->shape.part_count;++i) if (((List *)b->shape.sets)->values[i]==1) ((Part *)b->shape.actors)[i+2].material=m;
}
static void *model(void *p,void *actors,ThreeMMI32 i) { (void)p; return ((Part *)actors)[i+2].model; }
static void set_model(void *p,void *object,ThreeMMI32 i,void *m)
{ Part *a=(Part *)((Body *)object)->shape.actors+i+2; if (a->model) drop(a->model); a->model=m; retain(p,m); }
static void copy_matrix(void *p,void *to,ThreeMMI32 i,void *from,ThreeMMI32 j) { (void)p; ((Part *)to)[i].matrix=((Part *)from)[j].matrix; }
static int highlighted(void *p,void *actors) { (void)p; return ((Part *)actors)[1].role==THREE_MM_BODY_PART; }
static void highlight(void *p,void *object) { (void)p; ((Part *)((Body *)object)->shape.actors)[1].role=THREE_MM_BODY_PART; }
static void list(List *l,int n) { int i; l->kind=2; l->count=n; for (i=0;i<n;++i) l->values[i]=(short)(i%2); }
static void setup(Fixture *f,int visible,int n,int hilite)
{
    int i; ThreeMMBodyCreateServices *c;
    memset(f,0,sizeof(*f)); f->model.count=f->generic.count=f->custom.count=1;
    list(&f->sets,6); list(&f->parents,6); list(&f->new_sets,n); list(&f->new_parents,n);
    for (i=0;i<6;++i) f->parents.values[i]=(short)(i?((i-1)/2):-1);
    for (i=0;i<n;++i) f->new_parents.values[i]=(short)(i?i-1:-1);
    c=&f->s.shape; c->context=f; c->read=read_body; c->write=write_body; c->highlight=highlight_material;
    c->create=create; c->release=release; c->duplicate_sets=duplicate_sets; c->count=count; c->item=item;
    c->allocate_materials=allocate_materials; c->allocate_actors=allocate_actors; c->initialize_actor=initialize;
    c->link=link_actor; c->hide=hide; c->release_model=release_model; c->remove_material=remove_material; c->free_memory=free_memory;
    f->s.copy_bindings=copy_bindings; f->s.copy_actors=copy_actors; f->s.rebase_actor=rebase; f->s.retain_model=retain_model;
    f->s.copy_materials=copy_materials; f->s.copy_bounds=copy_bounds; f->s.material=material; f->s.retain=retain;
    f->s.show=show; f->s.swap_bodies=swap_bodies; f->s.identify=identify; f->s.set_generic=set_generic; f->s.set_custom=set_custom;
    f->s.model=model; f->s.set_model=set_model; f->s.copy_matrix=copy_matrix; f->s.highlighted=highlighted; f->s.highlight=highlight;
    f->original=three_mm_body_create(&f->parents,&f->sets,c); CHECK(f->original);
    set_generic(f,f->original,0,&f->generic); set_custom(f,f->original,&f->custom);
    for (i=0;i<6;++i) { Part *a=(Part *)f->original->shape.actors+i+2; a->matrix=100+i; if (!(i%2)) set_model(f,f->original,i,&f->model); }
    ((Part *)f->original->shape.actors)[0].matrix=37;
    f->original->shape.hidden=visible; f->original->binding=93; f->original->bounds[0]=2; f->original->bounds[1]=7;
    if (hilite) highlight(f,f->original);
    f->event_count=0; f->allocation=0; f->hide=f->show=0;
}
static void refs(Fixture *f,Body *a,Body *b)
{
    Body *bodies[2]; int models=1,generic=1,custom=1,i,j;
    bodies[0]=a; bodies[1]=b;
    for (i=0;i<2;++i) if (bodies[i])
    {
        Body *body=bodies[i]; Part *parts=body->shape.actors; Ref **materials=body->shape.materials;
        for (j=0;j<body->shape.set_count;++j) if (materials[j]) ++custom;
        for (j=0;j<body->shape.part_count;++j) { if (parts[j+2].model) ++models; if (parts[j+2].material==&f->generic) ++generic; }
    }
    CHECK(f->model.count==models && f->generic.count==generic && f->custom.count==custom);
}
int main(void)
{
    Fixture f; int operation,visible,n,hilite,failure,i,ok;
    for (operation=0;operation<2;++operation) for (visible=0;visible<4;visible+=visible?2:1)
    for (n=4;n<=8;n+=4) for (hilite=0;hilite<2;++hilite) for (failure=0;failure<9;++failure)
    {
        Body *copy=NULL; setup(&f,visible,n,hilite); f.fail=failure;
        if (operation) ok=three_mm_body_change_shape(f.original,&f.new_parents,&f.new_sets,&f.s);
        else { copy=three_mm_body_duplicate(f.original,&f.s); ok=copy!=NULL; }
        CHECK(ok==(!failure || failure>(operation?7:4)));
        CHECK(f.original->shape.hidden==visible && f.original->binding==93);
        CHECK(f.original->bounds[1]==7 && f.original->bounds[0]==(visible?2:0));
        refs(&f,f.original,copy);
        if (copy)
        {
            CHECK(copy->shape.hidden==1 && copy->binding==93 && copy->bounds[0]==f.original->bounds[0]);
            three_mm_body_restore(f.original,copy,&f.s); release(&f,copy); copy=NULL;
            CHECK(f.original->shape.hidden==visible && f.identified==8);
        }
        CHECK(f.original->shape.part_count==(operation && ok?n:6));
        CHECK(((Part *)f.original->shape.actors)[0].matrix==37 && highlighted(&f,f.original->shape.actors)==hilite);
        for (i=0;i<f.original->shape.part_count;++i)
        {
            Part *a=(Part *)f.original->shape.actors+i+2;
            CHECK(a->owner==f.original && a->matrix==(i<6?100+i:0));
            CHECK((a->model!=NULL)==(i<6 && !(i%2)));
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
