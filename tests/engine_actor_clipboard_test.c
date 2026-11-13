#include "three_mm/actor_clipboard.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(c) do { if (!(c)) { fprintf(stderr,"clipboard check at %d: %s\n",__LINE__,#c); exit(1); } } while (0)
typedef struct Text { unsigned char data[512]; unsigned unit,count; } Text;
typedef struct Actor { int refs,id; } Actor;
typedef struct Fixture
{
    ThreeMMActorClipboard clip; ThreeMMActorClipboardServices s;
    Text name,prefix,stored; Actor copied,duplicate; int source,movie;
    int fail,created,released,copies,names,prefixes,assigned,spaces,appended,errors,sets,actor_releases,ids;
    int duplications,pastes,renames,removes,routes,route_result,entire;
    char order[64]; unsigned order_count;
} Fixture;
static void mark(Fixture *f,char c) { CHECK(f->order_count+1<sizeof(f->order)); f->order[f->order_count++]=c; }
static unsigned at(const Text *t,unsigned i)
{ unsigned short value; if (t->unit==1) return t->data[i]; memcpy(&value,t->data+i*2,2); return value; }
static void put(Text *t,unsigned i,unsigned v)
{ unsigned short value=(unsigned short)v; if (t->unit==1) t->data[i]=(unsigned char)v; else memcpy(t->data+i*2,&value,2); }
static void text_set(Text *t,const char *value,unsigned unit)
{ unsigned i; memset(t,0,sizeof(*t)); t->unit=unit; t->count=(unsigned)strlen(value); for (i=0;i<t->count;++i) put(t,i,(unsigned char)value[i]); }
static int append(Text *t,const Text *source)
{
    unsigned n=source->count,i; int ok=n<=255-t->count;
    if (!ok) n=255-t->count;
    for (i=0;i<n;++i) put(t,t->count+i,at(source,i));
    t->count+=n; put(t,t->count,0); return ok;
}
static void *create(void *p) { Fixture *f=(Fixture *)p; ++f->created; mark(f,'C'); return f->fail==1?NULL:f; }
static void release_actor(void *p,void *a)
{
    Fixture *f=(Fixture *)p; Actor *actor=(Actor *)a; CHECK(actor->refs>0);
    if (a==&f->copied) CHECK(f->clip.actor==a);
    --actor->refs; ++f->actor_releases; mark(f,'R');
}
static void release_clipboard(void *p,void *c)
{ Fixture *f=(Fixture *)p; CHECK(c==f); ++f->released; mark(f,'D'); three_mm_actor_clipboard_dispose(&f->clip,&f->s); }
static int copy(void *p,void *source,int route,int entire,void **out)
{
    Fixture *f=(Fixture *)p; CHECK(source==&f->source && f->created==1 && !f->clip.actor);
    CHECK(route==0 || route==1); ++f->copies; f->entire=entire; mark(f,'c');
    if (f->fail==2) { *out=NULL; return 0; } f->copied.refs=1; *out=&f->copied; return 1;
}
static void source_name(void *p,void *source,void *name)
{ Fixture *f=(Fixture *)p; CHECK(source==&f->source && name==&f->name && f->copied.refs==1); ++f->names; mark(f,'n'); }
static void copy_prefix(void *p,void *source,void *prefix)
{ Fixture *f=(Fixture *)p; CHECK(source==&f->source && prefix==&f->prefix && f->names==1); ++f->prefixes; mark(f,'p'); }
static const void *text(void *name) { return ((Text *)name)->data; }
static void assign_name(void *clipboard,void *name)
{ Fixture *f=(Fixture *)clipboard; f->stored=*(Text *)name; ++f->assigned; mark(f,'='); }
static int append_space(void *clipboard)
{ Fixture *f=(Fixture *)clipboard; Text space; text_set(&space," ",f->s.unit); ++f->spaces; mark(f,' '); return append(&f->stored,&space); }
static int append_name(void *clipboard,void *name)
{ Fixture *f=(Fixture *)clipboard; CHECK(name==&f->name); ++f->appended; mark(f,'+'); return append(&f->stored,(Text *)name); }
static void too_long(void *p) { Fixture *f=(Fixture *)p; ++f->errors; mark(f,'!'); }
static void set(void *clipboard,void *actor,int route)
{ Fixture *f=(Fixture *)clipboard; CHECK(actor==&f->copied && !f->clip.actor); f->clip.actor=actor; f->clip.route_only=route; ++f->sets; mark(f,'S'); }
static int duplicate(void *p,void *actor,int reset,void **out)
{
    Fixture *f=(Fixture *)p; CHECK(actor==&f->copied && reset==1); ++f->duplications; mark(f,'d');
    if (f->fail==3) { *out=NULL; return 0; } f->duplicate.refs=1; f->duplicate.id=91; *out=&f->duplicate; return 1;
}
static int paste_route(void *p,void *movie,void *actor)
{ Fixture *f=(Fixture *)p; CHECK(movie==&f->movie && actor==&f->copied); ++f->routes; mark(f,'r'); return f->route_result; }
static int paste_actor(void *p,void *movie,void *actor)
{ Fixture *f=(Fixture *)p; CHECK(movie==&f->movie && actor==&f->duplicate); ++f->pastes; mark(f,'P'); return f->fail!=4; }
static ThreeMMI32 actor_id(void *actor) { return ((Actor *)actor)->id; }
static int rename_actor(void *p,void *movie,ThreeMMI32 actor,void *name)
{ Fixture *f=(Fixture *)p; CHECK(movie==&f->movie && actor==91 && name==&f->stored); ++f->renames; mark(f,'N'); f->duplicate.id=92; return f->fail!=5; }
static void remove_actor(void *p,void *movie,ThreeMMI32 actor)
{ Fixture *f=(Fixture *)p; CHECK(movie==&f->movie && actor==92 && f->duplicate.refs==1); ++f->removes; mark(f,'X'); }
static void init(Fixture *f,unsigned unit)
{
    ThreeMMActorClipboardServices *s;
    memset(f,0,sizeof(*f)); three_mm_actor_clipboard_init(&f->clip); s=&f->s;
    s->context=f; s->name=&f->name; s->prefix=&f->prefix; s->unit=unit;
    s->create=create; s->release_clipboard=release_clipboard; s->copy=copy;
    s->source_name=source_name; s->copy_prefix=copy_prefix; s->text=text;
    s->assign_name=assign_name; s->append_space=append_space; s->append_name=append_name;
    s->name_too_long=too_long; s->set=set; s->release_actor=release_actor;
    s->duplicate=duplicate; s->paste_route=paste_route; s->paste_actor=paste_actor;
    s->actor_id=actor_id; s->rename=rename_actor; s->remove=remove_actor;
    text_set(&f->name,"Alex",unit); text_set(&f->prefix,"Copy of",unit); text_set(&f->stored,"",unit);
}
static void name_is(Fixture *f,const char *expected)
{ unsigned i; CHECK(f->stored.count==strlen(expected)); for (i=0;i<f->stored.count;++i) CHECK(at(&f->stored,i)==(unsigned char)expected[i]); CHECK(!at(&f->stored,i)); }
static void *make(Fixture *f,int route,int entire) { return three_mm_actor_clipboard_create(&f->source,route,entire,&f->s); }
int main(void)
{
    Fixture f; unsigned unit,i; int fail,route,result;
    for (unit=1;unit<=2;++unit)
    {
        init(&f,unit); CHECK(make(&f,0,1)==&f); name_is(&f,"Copy of Alex"); CHECK(!strcmp(f.order,"Ccnp= +S"));
        CHECK(f.entire==1 && f.copied.refs==1); three_mm_actor_clipboard_dispose(&f.clip,&f.s); CHECK(!f.clip.actor && !f.copied.refs);
        init(&f,unit); text_set(&f.name,"Copy ofX",unit); CHECK(make(&f,0,0)==&f); name_is(&f,"Copy ofX"); CHECK(!f.spaces && !f.appended);
        init(&f,unit); text_set(&f.name,"copy of Alex",unit); CHECK(make(&f,0,0)==&f); name_is(&f,"Copy of copy of Alex");
        init(&f,unit); text_set(&f.prefix,"",unit); CHECK(make(&f,0,0)==&f); name_is(&f,"Alex");
        init(&f,unit); text_set(&f.prefix,"C.X",unit); put(&f.prefix,1,0); text_set(&f.name,"Cat",unit);
        CHECK(make(&f,0,0)==&f); name_is(&f,"Cat");
        init(&f,unit); text_set(&f.prefix,"C.X",unit); put(&f.prefix,1,0); CHECK(make(&f,0,0)==&f);
        CHECK(f.stored.count==8 && at(&f.stored,0)=='C' && !at(&f.stored,1) && at(&f.stored,2)=='X' && at(&f.stored,3)==' ' && at(&f.stored,4)=='A');
        for (fail=1;fail<=2;++fail)
        { init(&f,unit); f.fail=fail; CHECK(!make(&f,0,0)); CHECK(f.released==(fail==2) && !f.actor_releases && !f.names && !f.sets); }
        for (i=250;i<=255;i+=5)
        {
            unsigned j; init(&f,unit); f.prefix.count=i; for (j=0;j<i;++j) put(&f.prefix,j,'P'); put(&f.prefix,i,0);
            text_set(&f.name,"Alexis",unit); CHECK(!make(&f,0,0));
            CHECK(f.errors==1 && f.released==1 && !f.sets && !f.clip.actor && f.copied.refs==1 && !f.actor_releases);
            CHECK(f.stored.count==255 && f.appended==(i==250)); /* Legacy leak, nonallocating mock. */
        }
    }
    for (route=0;route<2;++route) for (result=0;result<2;++result)
    {
        init(&f,2); CHECK(make(&f,route,0)==&f); CHECK(f.clip.route_only==route);
        if (route) CHECK(!f.names && !f.prefixes && !f.assigned);
        f.order_count=0; memset(f.order,0,sizeof(f.order)); f.route_result=result;
        CHECK(three_mm_actor_clipboard_paste(&f.clip,&f.stored,&f.movie,&f.s)==(route?result:1));
        CHECK(f.copied.refs==1 && !f.duplicate.refs);
        CHECK(!strcmp(f.order,route?"r":"dPNR"));
    }
    for (fail=3;fail<=5;++fail)
    {
        init(&f,2); CHECK(make(&f,0,0)==&f); f.fail=fail; f.order_count=0; memset(f.order,0,sizeof(f.order));
        CHECK(!three_mm_actor_clipboard_paste(&f.clip,&f.stored,&f.movie,&f.s));
        CHECK(!strcmp(f.order,fail==3?"d":fail==4?"dPR":"dPNXR"));
        CHECK(f.copied.refs==1 && !f.duplicate.refs && f.removes==(fail==5));
    }
    puts("C89 actor clipboard policy checks passed"); return 0;
}
