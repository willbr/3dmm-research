#include "three_mm/actor_group_undo.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(c) do { if (!(c)) { fprintf(stderr,"group undo check %d: %s\n",__LINE__,#c); exit(1); } } while (0)
typedef struct Child { int id,refs,context; } Child;
typedef struct Fixture
{
    ThreeMMActorGroupState state;
    ThreeMMActorGroupServices services;
    void *published;
    Child children[3],*rows[4]; int count,counts,dead,fail_child,fail_append,contexts,calls,order[4],released;
    int ids[4],stored[4],names[4],scratch,old,missing,fail_rename,fail_store,reads,captures,renames,stores;
    int fail_factory,created,tables,publications,owner_releases;
} Fixture;
static ThreeMMI32 count(void *p,void *entries)
{ Fixture *f=(Fixture *)p; CHECK(!f->dead && entries==f); ++f->counts; return f->count; }
static void *child(void *p,void *entries,ThreeMMI32 i)
{ Fixture *f=(Fixture *)p; CHECK(!f->dead && entries==f && i>=0 && i<f->count); return f->rows[i]; }
static void retain(void *p,void *item) { Fixture *f=(Fixture *)p; CHECK(!f->dead && item); ++((Child *)item)->refs; }
static void release(void *p,void *item) { Fixture *f=(Fixture *)p; CHECK(!f->dead && item && ((Child *)item)->refs>0); --((Child *)item)->refs; }
static int append(void *p,void *entries,void *item)
{ Fixture *f=(Fixture *)p; CHECK(entries==f && ((Child *)item)->refs==2); if (f->fail_append) return 0; f->rows[f->count++]=(Child *)item; return 1; }
static void context(void *p,void *item,void *movie,ThreeMMI32 scene,ThreeMMI32 frame)
{ Fixture *f=(Fixture *)p; CHECK(!f->dead && movie==f && scene==7 && frame==13); ++f->contexts; ((Child *)item)->context=1; }
static int undo(void *p,void *item,void *document)
{
    Fixture *f=(Fixture *)p; Child *c=(Child *)item; CHECK(!f->dead && document==f && c->context);
    f->order[f->calls++]=c->id; if (c->id==f->fail_child) { f->dead=1; return 0; } return 1;
}
static void release_entries(void *p,void *entries)
{ Fixture *f=(Fixture *)p; CHECK(!f->dead && entries==f && f->published==entries); ++f->released; }
static void publish_entries(void *p,void *entries)
{ Fixture *f=(Fixture *)p; CHECK(!f->dead && !entries && f->released==1); f->published=entries; }
static ThreeMMI32 read_name(void *p,void *entries,ThreeMMI32 i)
{ Fixture *f=(Fixture *)p; CHECK(!f->dead && entries==f); ++f->reads; f->scratch=f->stored[i]; return f->ids[i]; }
static int capture(void *p,void *movie,ThreeMMI32 actor)
{ Fixture *f=(Fixture *)p; CHECK(movie==f); ++f->captures; f->old=f->names[actor]; return actor!=f->missing; }
static int rename_actor(void *p,void *movie,ThreeMMI32 actor)
{ Fixture *f=(Fixture *)p; CHECK(movie==f); ++f->renames; if (actor==f->fail_rename) return 0; f->names[actor]=f->scratch; return 1; }
static int store(void *p,void *entries,ThreeMMI32 i)
{ Fixture *f=(Fixture *)p; CHECK(entries==f); ++f->stores; if (i==f->fail_store) return 0; f->stored[i]=f->old; return 1; }
static int append_name(void *p,void *entries,ThreeMMI32 id,const void *name)
{ Fixture *f=(Fixture *)p; CHECK(entries==f); if (f->fail_append) return 0; f->ids[f->count]=id; f->stored[f->count++]=*(const int *)name; return 1; }
static void init(Fixture *f)
{
    ThreeMMActorGroupServices *s; int i;
    memset(f,0,sizeof(*f)); s=&f->services; s->context=f; f->state.entries=f->state.movie=f;
    f->state.scene=7; f->state.frame=13; f->published=f; f->missing=f->fail_rename=f->fail_store=-1;
    s->count=count; s->child=child; s->retain=retain; s->release=release; s->append_child=append; s->child_context=context;
    s->undo_child=undo; s->release_entries=release_entries; s->publish_entries=publish_entries;
    s->read_name=read_name; s->capture_name=capture; s->rename=rename_actor; s->store_name=store; s->append_name=append_name;
    for (i=0;i<3;++i) { f->children[i].id=i+1; f->children[i].refs=1; }
}
static void *create_owner(void *p) { Fixture *f=(Fixture *)p; ++f->created; return f->fail_factory==1?NULL:f; }
static void *create_table(void *p) { Fixture *f=(Fixture *)p; CHECK(f->created==1); ++f->tables; return f->fail_factory==2?NULL:&f->count; }
static void factory_publish(void *p,void *owner,void *entries)
{ Fixture *f=(Fixture *)p; CHECK(owner==f && f->tables==1); f->published=entries; ++f->publications; }
static void release_owner(void *p,void *owner) { Fixture *f=(Fixture *)p; CHECK(owner==f && !f->published && f->publications==1); ++f->owner_releases; }
int main(void)
{
    Fixture f; ThreeMMActorGroupFactory factory; int fail,i,value=17;
    init(&f); f.state.entries=NULL; CHECK(!three_mm_actor_group_count(&f.state,&f.services) && !f.counts);
    three_mm_actor_group_dispose(&f.state,1,&f.services); CHECK(!f.released);
    for (fail=0;fail<4;++fail)
    {
        init(&f); f.fail_child=fail;
        for (i=0;i<3;++i)
        {
            CHECK(three_mm_actor_group_add_child(&f.state,&f.children[i],&f.services));
            if (!i) f.rows[f.count++]=NULL;
        }
        CHECK(three_mm_actor_group_count(&f.state,&f.services)==4);
        CHECK(three_mm_actor_group_move_toggle(&f.state,&f,&f.services)==!fail);
        CHECK(f.calls==(fail?fail:3) && f.contexts==f.calls);
        for (i=0;i<f.calls;++i) CHECK(f.order[i]==i+1);
        /* The failing child can destroy the native composite; any additional
         * storage reads/context calls would have tripped the dead sentinel. */
        f.dead=0; three_mm_actor_group_dispose(&f.state,1,&f.services);
        CHECK(!f.state.entries && !f.published && f.released==1);
        for (i=0;i<3;++i) CHECK(f.children[i].refs==1);
    }
    init(&f); f.fail_append=1;
    CHECK(!three_mm_actor_group_add_child(&f.state,&f.children[0],&f.services) && f.children[0].refs==1 && !f.count);
    init(&f); CHECK(three_mm_actor_group_move_toggle(&f.state,&f,&f.services) && !f.calls);
    for (fail=0;fail<4;++fail)
    {
        init(&f); f.names[1]=100; f.names[2]=200; f.names[3]=300;
        f.ids[0]=1; f.ids[1]=2; f.ids[2]=3; f.stored[0]=10; f.stored[1]=20; f.stored[2]=30; f.count=3;
        if (fail==1) f.missing=2; if (fail==2) f.fail_rename=2; if (fail==3) f.fail_store=0;
        CHECK(three_mm_actor_group_name_toggle(&f.state,&f.services)==(fail!=2)); CHECK(f.counts==1);
        CHECK(f.names[1]==10 && f.stored[0]==(fail==3?10:100));
        if (fail==2) CHECK(f.names[2]==200 && f.names[3]==300 && f.reads==2 && f.stores==1);
        else
        {
            CHECK(f.names[3]==30 && f.stored[2]==300 && f.reads==3);
            CHECK(f.names[2]==(fail==1?200:20));
            if (!fail) { CHECK(three_mm_actor_group_name_toggle(&f.state,&f.services)); CHECK(f.names[1]==100 && f.names[2]==200 && f.names[3]==300); }
        }
    }
    init(&f); f.names[1]=100; f.ids[0]=f.ids[1]=1; f.stored[0]=10; f.stored[1]=20; f.count=2;
    CHECK(three_mm_actor_group_name_toggle(&f.state,&f.services)); CHECK(f.names[1]==20 && f.stored[0]==100 && f.stored[1]==10);
    three_mm_actor_group_dispose(&f.state,0,&f.services); CHECK(!f.state.entries && f.released==1);
    init(&f); CHECK(three_mm_actor_group_add_name(&f.state,2,&value,&f.services)); CHECK(f.ids[0]==2 && f.stored[0]==17);
    f.fail_append=1; CHECK(!three_mm_actor_group_add_name(&f.state,3,&value,&f.services) && f.count==1);
    for (fail=0;fail<3;++fail)
    {
        init(&f); f.fail_factory=fail; memset(&factory,0,sizeof(factory)); factory.context=&f;
        factory.create_owner=create_owner; factory.create_entries=create_table; factory.publish_entries=factory_publish; factory.release_owner=release_owner;
        CHECK(three_mm_actor_group_create(&factory)==(fail?NULL:&f));
        CHECK(f.created==1 && f.tables==(fail!=1) && f.publications==(fail!=1) && f.owner_releases==(fail==2));
    }
    puts("composite actor undo C89 checks passed"); return 0;
}
