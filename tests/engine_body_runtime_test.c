#include "three_mm/body_runtime.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(c) do { if (!(c)) { fprintf(stderr,"body runtime check %d: %s\n",__LINE__,#c); exit(1); } } while (0)
typedef struct Ref Ref;
typedef struct Handle { Ref *owner; int live; } Handle;
struct Ref { int refs,kind,set,count; Handle raw,parts[4]; Ref *models[4]; };
typedef struct Part { Ref *model; Handle *material; int matrix; } Part;
typedef struct Fixture
{
    ThreeMMBodyRuntimeServices s; ThreeMMBodyShape b; short sets[6]; Part parts[8]; Ref *customs[3];
    Ref a,bmodel,ma,mb,custom,poison; int events[256],event_count;
    int parent,attached,detached,changed,bounds,highlight,fail,freed,diagnostics[4];
} Fixture;
static void event(Fixture *f,int n) { CHECK(f->event_count<256); f->events[f->event_count++]=n; }
static void read_body(void *p,void *b,ThreeMMBodyShape *out) { Fixture *f=p; CHECK(b==f); *out=f->b; }
static void write_body(void *p,void *b,const ThreeMMBodyShape *in) { Fixture *f=p; CHECK(b==f); f->b=*in; }
static ThreeMMI32 item(void *p,void *list,ThreeMMI32 i) { Fixture *f=p; CHECK(list==f->sets && i>=0 && i<6); return f->sets[i]; }
static void retain(void *p,void *object) { Fixture *f=p; Ref *r=object; CHECK(r && r->refs>0); ++r->refs; event(f,7); }
static void release(void *p,void *object)
{
    Fixture *f=p; Ref *r=object; int i; CHECK(r && r->refs>0); --r->refs; event(f,5);
    if (r->kind==2 && !r->refs) for (i=0;i<4;++i) r->parts[i].live=0;
}
static void *model(void *p,void *b,ThreeMMI32 i) { Fixture *f=p; CHECK(b==f); return f->parts[i+2].model; }
static void assign_model(void *p,void *b,ThreeMMI32 i,void *r) { Fixture *f=p; CHECK(b==f); f->parts[i+2].model=r; event(f,6); }
static void *part_material(void *p,void *b,ThreeMMI32 i) { Fixture *f=p; CHECK(b==f); return f->parts[i+2].material; }
static void assign_material(void *p,void *b,ThreeMMI32 i,void *r) { Fixture *f=p; CHECK(b==f); f->parts[i+2].material=r; }
static void *material_object(void *p,void *raw) { Handle *h=raw; (void)p; CHECK(h && h->live && h->owner->kind==1); return h->owner; }
static void *material_handle(void *p,void *r) { (void)p; CHECK(((Ref *)r)->kind==1); return &((Ref *)r)->raw; }
static void *custom(void *p,void *b,ThreeMMI32 i) { Fixture *f=p; CHECK(b==f); return f->customs[i]; }
static void assign_custom(void *p,void *b,ThreeMMI32 i,void *r) { Fixture *f=p; CHECK(b==f); f->customs[i]=r; }
static ThreeMMI32 custom_set(void *p,void *r) { (void)p; return ((Ref *)r)->set; }
static ThreeMMI32 custom_count(void *p,void *r) { (void)p; return ((Ref *)r)->count; }
static void *custom_model(void *p,void *r,ThreeMMI32 i) { (void)p; CHECK(i>=0 && i<4); return ((Ref *)r)->models[i]; }
static void *custom_material(void *p,void *r,ThreeMMI32 i) { (void)p; CHECK(i>=0 && i<4 && ((Ref *)r)->parts[i].live); return &((Ref *)r)->parts[i]; }
static void invalidate(void *p,void **out) { *out=&((Fixture *)p)->poison; }
static void matrix(void *p,void *b,ThreeMMI32 i,const void *m) { Fixture *f=p; CHECK(b==f); f->parts[i].matrix=*(const int *)m; }
static void translate(void *p,void *b,ThreeMMI32 x,ThreeMMI32 y,ThreeMMI32 z) { Fixture *f=p; CHECK(b==f); f->parts[0].matrix+=x+y+z; }
static void highlight(void *p,void *b,int enabled) { Fixture *f=p; CHECK(b==f); f->highlight=enabled; }
static void attach(void *p,void *b) { Fixture *f=p; CHECK(b==f && !f->b.hidden); f->parent=1; ++f->attached; event(f,1); }
static int attached(void *p,void *b) { Fixture *f=p; CHECK(b==f); return f->parent; }
static void detach(void *p,void *b) { Fixture *f=p; CHECK(b==f && f->b.hidden && f->parent); f->parent=0; ++f->detached; event(f,3); }
static void bounds(void *p,void *b) { Fixture *f=p; CHECK(b==f && f->b.hidden); f->bounds=0; event(f,2); }
static void changed(void *p,void *b) { Fixture *f=p; CHECK(b==f); ++f->changed; event(f,4); }
static int is_generic(void *p,void *r) { (void)p; return ((Ref *)r)->kind==1; }
static void diagnostic(void *p,int n) { CHECK(n>=0 && n<4); ++((Fixture *)p)->diagnostics[n]; }
static int allocate(void *p,ThreeMMI32 n,void **out)
{ Fixture *f=p; *out=NULL; if (f->fail) return 0; *out=calloc((size_t)n+1,sizeof(void *)); CHECK(*out); return 1; }
static void free_memory(void *p,void **out) { Fixture *f=p; if (*out) ++f->freed; free(*out); *out=NULL; }
static void setup(Fixture *f,int hidden)
{
    int i; memset(f,0,sizeof(*f));
    f->a.refs=f->bmodel.refs=f->ma.refs=f->mb.refs=f->custom.refs=1;
    f->ma.kind=f->mb.kind=1; f->custom.kind=2; f->custom.set=1; f->custom.count=4;
    f->ma.raw.owner=&f->ma; f->mb.raw.owner=&f->mb; f->ma.raw.live=f->mb.raw.live=1;
    for (i=0;i<4;++i) { f->custom.parts[i].owner=&f->ma; f->custom.parts[i].live=1; }
    f->custom.models[0]=&f->bmodel; f->custom.models[2]=&f->a;
    for (i=0;i<6;++i) f->sets[i]=(short)(i%2);
    f->b.sets=f->sets; f->b.part_count=6; f->b.set_count=2; f->b.hidden=hidden; f->bounds=99; f->parent=!hidden;
    f->s.shape.context=f; f->s.shape.read=read_body; f->s.shape.write=write_body; f->s.shape.item=item;
    f->s.shape.release=release; f->s.shape.allocate_materials=allocate; f->s.shape.free_memory=free_memory;
    f->s.retain=retain; f->s.model=model; f->s.assign_model=assign_model; f->s.part_material=part_material;
    f->s.assign_material=assign_material; f->s.material_object=material_object; f->s.material_handle=material_handle;
    f->s.custom=custom; f->s.assign_custom=assign_custom; f->s.custom_set=custom_set; f->s.custom_count=custom_count;
    f->s.custom_model=custom_model; f->s.custom_material=custom_material; f->s.invalidate_output=invalidate;
    f->s.matrix=matrix; f->s.translate=translate; f->s.highlight=highlight; f->s.attach=attach; f->s.attached=attached;
    f->s.detach=detach; f->s.clear_bounds=bounds; f->s.changed=changed; f->s.is_generic=is_generic; f->s.diagnostic=diagnostic;
    three_mm_body_set_generic(f,0,&f->ma,&f->s); three_mm_body_set_generic(f,1,&f->mb,&f->s);
    three_mm_body_set_model(f,0,&f->a,&f->s); f->changed=f->event_count=0;
}
static void cleanup(Fixture *f)
{
    int i; three_mm_body_remove_material(f,0,&f->s); three_mm_body_remove_material(f,1,&f->s);
    for (i=0;i<6;++i) if (f->parts[i+2].model) { release(f,f->parts[i+2].model); f->parts[i+2].model=NULL; }
    CHECK(f->a.refs==1 && f->bmodel.refs==1 && f->ma.refs==1 && f->mb.refs==1);
}
int main(void)
{
    Fixture f; ThreeMMBodyCostume costume; int h,n,change,initial,gen; void *m,*c;
    for (h=0;h<4;h+=h?2:1)
    {
        setup(&f,h); three_mm_body_set_model(&f,0,&f.a,&f.s); CHECK(!f.event_count);
        three_mm_body_set_model(&f,0,&f.bmodel,&f.s);
        CHECK(f.events[0]==5 && f.events[1]==6 && f.events[2]==7 && f.event_count==(h?3:4));
        initial=f.changed; three_mm_body_set_model(&f,0,&f.bmodel,&f.s); CHECK(f.changed==initial);
        n=7; three_mm_body_set_matrix(&f,0,&n,&f.s); CHECK(f.parts[2].matrix==7);
        n=3; three_mm_body_locate(&f,2,3,4,&n,&f.s); CHECK(f.parts[0].matrix==12);
        three_mm_body_highlight(&f,1,&f.s); three_mm_body_highlight(&f,1,&f.s); three_mm_body_highlight(&f,0,&f.s);
        CHECK(!f.highlight && f.changed==initial+(h?0:5));
        f.event_count=0; initial=f.changed;
        three_mm_body_hide(&f,&f.s); three_mm_body_hide(&f,&f.s); three_mm_body_show(&f,&f.s); three_mm_body_show(&f,&f.s);
        CHECK(f.b.hidden==h && f.changed==initial+(h?0:2) && f.bounds==(h?99:0));
        if (!h) CHECK(f.events[0]==2 && f.events[1]==3 && f.events[2]==4 && f.events[3]==1 && f.events[4]==4);
        initial=f.changed; three_mm_body_set_custom(&f,&f.custom,&f.s);
        CHECK(f.custom.refs==2 && f.bmodel.refs==3 && f.a.refs==2 && f.changed==initial+(h?0:3));
        change=f.changed; three_mm_body_set_custom(&f,&f.custom,&f.s);
        CHECK(f.custom.refs==2 && f.changed==change+(h?0:1));
        m=&f.mb; c=NULL; three_mm_body_get_material(&f,1,&gen,&m,&c,&f.s); CHECK(!gen && c==&f.custom && m==&f.poison);
        m=NULL; c=&f.custom; three_mm_body_get_material(&f,0,&gen,&m,&c,&f.s); CHECK(gen && m==&f.ma && c==&f.poison);
        three_mm_body_costume_init(&costume); CHECK(!costume.entries && !costume.count);
        CHECK(three_mm_body_costume_capture(&costume,&f,&f.s) && costume.count==2 && f.custom.refs==3 && f.ma.refs==5);
        three_mm_body_costume_apply(&costume,&f,0,&f.s); CHECK(f.custom.refs==3 && f.ma.refs==5);
        f.b.set_count=1; three_mm_body_costume_apply(&costume,&f,1,&f.s); CHECK(f.custom.refs==3); f.b.set_count=2;
        f.fail=1; CHECK(!three_mm_body_costume_capture(&costume,&f,&f.s));
        CHECK(!costume.entries && !costume.count && f.freed==1 && f.custom.refs==2 && f.ma.refs==4);
        f.fail=0; CHECK(three_mm_body_costume_capture(&costume,&f,&f.s));
        three_mm_body_costume_clear(&costume,&f.s); CHECK(!costume.entries && !costume.count && f.freed==2);
        m=&f.mb; c=NULL; three_mm_body_get_material(&f,2,&gen,&m,&c,&f.s);
        CHECK(gen && m==&f.mb && c==&f.poison && f.diagnostics[THREE_MM_BODY_EMPTY_SET]==1);
        f.event_count=0; cleanup(&f); CHECK(f.custom.refs==1);
    }
    /* The last custom reference can disappear before its raw handles clear.
     * material_object rejects dead handles, detecting accidental dereference. */
    setup(&f,1); three_mm_body_set_custom(&f,&f.custom,&f.s); release(&f,&f.custom);
    CHECK(f.custom.refs==1); three_mm_body_remove_material(&f,1,&f.s);
    CHECK(!f.custom.refs && !f.custom.parts[0].live && !f.customs[1]); cleanup(&f);
    puts("C89 body materials, visibility and costume ownership checks passed"); return 0;
}
