#include "three_mm/movie_sound.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(c) do { if (!(c)) { fprintf(stderr,"movie sound check %d: %s\n",__LINE__,#c); exit(1); } } while (0)
#define M THREE_MM_SOUND_MOVIE
#define S THREE_MM_SOUND_SCENE
#define A THREE_MM_SOUND_CHUNK
typedef struct Fixture Fixture;
typedef struct Edge { ThreeMMU32 type,number; ThreeMMChunkChild child; } Edge;
typedef struct File { Fixture *f; Edge edges[32]; int count,refs,midi; } File;
struct Fixture
{
    File a,b,source,*current,*last_destination; ThreeMMMovieSoundServices s; ThreeMMTag *original;
    int reads,files,enums,finds,scene_lookups,adopts,opens,closes,saves,ensures,temps,inspects,copies,waves,midis,deleted,flushed,gc,kinds;
    int fail_enum,fail_scene,fail_adopt,fail_open,fail_tag,fail_ensure,fail_copy,fail_temp,fail_save,bad_sound,move_cache;
    ThreeMMU32 last_slot,deleted_number; ThreeMMI32 style; int diagnostic[4];
};
static void edge(File *file,ThreeMMU32 type,ThreeMMU32 number,ThreeMMU32 child_type,ThreeMMU32 child,ThreeMMU32 slot)
{ Edge *e; CHECK(file->count<32); e=&file->edges[file->count++]; e->type=type; e->number=number; e->child.type=child_type; e->child.number=child; e->child.slot=slot; }
static void read_state(void *p,ThreeMMMovieSoundState *state)
{ Fixture *f=(Fixture *)p; ++f->reads; state->cache=f->current; state->movie=10; state->scene=0; }
static void *file(void *p) { File *v=(File *)p; CHECK(v); ++v->f->files; return p; }
static ThreeMMI32 count(void *p,ThreeMMU32 type,ThreeMMU32 number)
{ File *v=(File *)p; int i,n=0; for (i=0;i<v->count;++i) if (v->edges[i].type==type && v->edges[i].number==number) ++n; return n; }
static int child(void *p,ThreeMMU32 type,ThreeMMU32 number,ThreeMMI32 index,ThreeMMChunkChild *out)
{
    File *v=(File *)p; int i; ++v->f->enums;
    if (v->f->fail_enum==v->f->enums) return 0;
    for (i=0;i<v->count;++i) if (v->edges[i].type==type && v->edges[i].number==number)
        if (!index--) { *out=v->edges[i].child; return 1; }
    return 0;
}
static int find(void *p,ThreeMMU32 type,ThreeMMU32 number,ThreeMMU32 slot,ThreeMMU32 child_type,ThreeMMU32 *out)
{
    File *v=(File *)p; Fixture *f=v->f; int i; ++f->finds;
    if (type==M && child_type==S) { ++f->scene_lookups; if (f->scene_lookups==f->fail_scene) return 0; }
    for (i=0;i<v->count;++i)
        if (v->edges[i].type==type && v->edges[i].number==number && v->edges[i].child.slot==slot && v->edges[i].child.type==child_type)
        { *out=v->edges[i].child.number; return 1; }
    return 0;
}
static int adopt(void *p,ThreeMMU32 type,ThreeMMU32 number,ThreeMMU32 child_type,ThreeMMU32 value,ThreeMMU32 slot)
{
    File *v=(File *)p; Fixture *f=v->f; ThreeMMU32 present; ++f->adopts; f->last_slot=slot; f->last_destination=v;
    if (f->fail_adopt || find(v,type,number,slot,child_type,&present)) return 0;
    edge(v,type,number,child_type,value,slot); return 1;
}
static int open_tag(void *p,ThreeMMTag *tag,void *cache)
{
    Fixture *f=(Fixture *)p; ++f->opens; CHECK(f->original->cache==&f->a && f->original->number==999);
    if (f->fail_open) { tag->cache=NULL; return 0; }
    ++((File *)cache)->refs; tag->cache=cache; return 1;
}
static void close_tag(void *p,ThreeMMTag *tag)
{ Fixture *f=(Fixture *)p; ++f->closes; CHECK(f->opens==1 && f->b.refs==2 && tag->cache==&f->a); --f->a.refs; tag->cache=NULL; }
static int save_tag(void *p,ThreeMMTag *tag,void *cache,int redirect)
{ Fixture *f=(Fixture *)p; CHECK(cache==f->current && redirect==1 && tag); ++f->saves; return !f->fail_tag; }
static int ensure(void *p)
{ Fixture *f=(Fixture *)p; ++f->ensures; if (f->fail_ensure) return 0; if (f->move_cache) f->current=&f->b; return 1; }
static int temp(void *p)
{ Fixture *f=(Fixture *)p; ++f->temps; if (f->fail_temp) return 0; if (f->move_cache) f->current=&f->b; return 1; }
static void inspect(void *p) { ++((File *)p)->f->inspects; }
static int copy_chunk(void *source,ThreeMMU32 type,ThreeMMU32 number,void *destination,ThreeMMU32 *out)
{ Fixture *f=((File *)source)->f; CHECK(source==&f->source && type==A && number==123); ++f->copies; f->last_destination=(File *)destination; *out=400; return !f->fail_copy; }
static void delete_chunk(void *p,ThreeMMU32 type,ThreeMMU32 number)
{ Fixture *f=((File *)p)->f; CHECK(type==A); ++f->deleted; f->deleted_number=number; }
static int save_file(void *p,ThreeMMU32 creator)
{ Fixture *f=((File *)p)->f; CHECK(creator==THREE_MM_SOUND_CREATOR && f->adopts==1); ++f->flushed; return !f->fail_save; }
static int is_midi(void *p) { File *v=(File *)p; ++v->f->kinds; return v->midi; }
static int copy_midi(void *source,void *destination,ThreeMMU32 *out,void *name)
{ Fixture *f=((File *)source)->f; CHECK(name==f); ++f->midis; return copy_chunk(source,A,123,destination,out); }
static int copy_wave(void *source,void *destination,ThreeMMI32 style,ThreeMMU32 *out,void *name)
{ Fixture *f=((File *)source)->f; CHECK(name==f); ++f->waves; f->style=style; return copy_chunk(source,A,123,destination,out); }
static int has_bad(void *p) { return ((Fixture *)p)->bad_sound; }
static void mark_gc(void *p) { Fixture *f=(Fixture *)p; CHECK(f->flushed==1 && !f->fail_save); ++f->gc; }
static ThreeMMI32 sound_count(void *p) { (void)p; return 3; }
static int sound_at(void *p,ThreeMMI32 index,ThreeMMU32 *out) { (void)p; CHECK(index>=0 && index<3); *out=100+(ThreeMMU32)index; return 1; }
static ThreeMMI32 references(void *p,ThreeMMU32 type,ThreeMMU32 number)
{ File *v=(File *)p; int i,n=0; for (i=0;i<v->count;++i) if (v->edges[i].child.type==type && v->edges[i].child.number==number) ++n; return n; }
static int at_slot(void *p,ThreeMMU32 type,ThreeMMU32 number,ThreeMMU32 slot)
{ File *v=(File *)p; int i; for (i=0;i<v->count;++i) if (v->edges[i].type==type && v->edges[i].number==number && v->edges[i].child.slot==slot) return 1; return 0; }
static void diagnostic(void *p,int which) { Fixture *f=(Fixture *)p; CHECK(which>=0 && which<4); ++f->diagnostic[which]; }
static void init(Fixture *f)
{
    ThreeMMMovieSoundServices *s;
    memset(f,0,sizeof(*f)); f->a.f=f->b.f=f->source.f=f; f->a.refs=f->b.refs=1; f->current=&f->a;
    edge(&f->a,M,10,S,20,0); edge(&f->a,S,20,A,100,0); edge(&f->a,S,20,99,999,1); edge(&f->a,S,20,A,101,2); edge(&f->a,M,10,A,100,0);
    edge(&f->b,M,10,S,20,0); edge(&f->b,S,20,A,102,3); edge(&f->b,M,10,A,101,0); edge(&f->b,M,10,A,102,1);
    s=&f->s; s->context=f; s->read=read_state; s->file=file; s->count=count; s->child=child; s->find=find; s->adopt=adopt;
    s->open_tag=open_tag; s->close_tag=close_tag; s->save_tag=save_tag; s->ensure_autosave=ensure; s->use_temp=temp;
    s->inspect_source=inspect; s->copy_chunk=copy_chunk; s->delete_chunk=delete_chunk; s->save_file=save_file;
    s->is_midi=is_midi; s->copy_midi=copy_midi; s->copy_wave=copy_wave; s->has_bad_sound_error=has_bad; s->mark_gc=mark_gc;
    s->sound_count=sound_count; s->sound_at=sound_at; s->references=references; s->child_at_slot=at_slot; s->diagnostic=diagnostic;
}
int main(void)
{
    Fixture f; ThreeMMTag tag; ThreeMMU32 out; int valid,mode,result;
    init(&f); CHECK(three_mm_movie_sound_next_scene(&f.s)==1 && three_mm_movie_sound_next_movie(&f.s)==1);
    init(&f); f.fail_scene=1; CHECK(!three_mm_movie_sound_next_scene(&f.s));
    init(&f); CHECK(three_mm_movie_sound_is_child(&f.a,A,100,&f.s)); CHECK(!three_mm_movie_sound_is_child(&f.a,A,102,&f.s));
    init(&f); f.fail_enum=1; CHECK(!three_mm_movie_sound_is_child(&f.a,A,100,&f.s) && f.diagnostic[0]==1);
    init(&f); f.fail_enum=1; CHECK(three_mm_movie_sound_adopt(&f.a,20,&f.s) && !f.adopts && f.diagnostic[0]==1);
    init(&f); edge(&f.a,S,20,A,102,4); CHECK(three_mm_movie_sound_adopt(&f.a,20,&f.s)); CHECK(f.adopts==2 && f.last_slot==2);
    init(&f); f.fail_adopt=1; CHECK(!three_mm_movie_sound_adopt(&f.a,20,&f.s) && f.adopts==1);
    /* An external graph still takes its candidate ID from the working file. */
    init(&f); edge(&f.b,S,20,A,103,5); CHECK(!three_mm_movie_sound_adopt(&f.b,20,&f.s)); CHECK(f.last_slot==1 && f.last_destination==&f.b);
    for (mode=0;mode<3;++mode)
    {
        init(&f); tag.sid=0; tag.cache=&f.a; tag.type=A; tag.number=mode==2?102:999; f.a.refs=2; f.original=&tag; f.fail_open=mode==1;
        result=three_mm_movie_sound_resolve(&tag,3,THREE_MM_SOUND_NO_SCENE,&f.b,&f.s); CHECK(result==(mode!=1));
        if (mode==0) CHECK(tag.number==102 && tag.cache==&f.b && f.opens==1 && f.closes==1 && f.a.refs==1 && f.b.refs==2);
        else CHECK(tag.cache==&f.a && f.a.refs==2 && f.b.refs==1 && !f.closes);
    }
    init(&f); tag.sid=-1; tag.cache=&f.a; tag.number=999; CHECK(three_mm_movie_sound_resolve(&tag,999,THREE_MM_SOUND_NO_SCENE,&f.b,&f.s)); CHECK(f.files==1 && !f.finds);
    init(&f); tag.sid=0; f.fail_scene=1; CHECK(!three_mm_movie_sound_resolve(&tag,3,THREE_MM_SOUND_NO_SCENE,NULL,&f.s) && !f.opens);
    init(&f); tag.sid=0; tag.cache=&f.a; tag.type=A; tag.number=999; f.fail_scene=1;
    CHECK(!three_mm_movie_sound_resolve(&tag,999,20,NULL,&f.s) && !f.scene_lookups && !f.opens);
    init(&f); CHECK(three_mm_movie_sound_save(&tag,&f.s) && f.saves==1); f.fail_tag=1; CHECK(!three_mm_movie_sound_save(&tag,&f.s));
    init(&f); out=777; CHECK(three_mm_movie_sound_child(100,&out,&f.s) && out==0 && !f.adopts);
    init(&f); out=777; CHECK(three_mm_movie_sound_child(102,&out,&f.s) && out==1 && f.scene_lookups==2 && f.adopts==1);
    init(&f); out=777; f.fail_adopt=1; CHECK(!three_mm_movie_sound_child(102,&out,&f.s) && out==1 && f.adopts==1);
    init(&f); out=777; f.fail_enum=1; CHECK(!three_mm_movie_sound_child(102,&out,&f.s) && out==777 && !f.adopts);
    init(&f); out=777; f.fail_scene=2; CHECK(!three_mm_movie_sound_child(102,&out,&f.s) && out==0 && f.adopts==1 && f.last_slot==0);
    init(&f); out=777; f.fail_ensure=1; CHECK(!three_mm_movie_sound_copy(&f.source,123,&out,&f.s) && out==777 && !f.reads && !f.copies);
    init(&f); out=777; f.fail_copy=1; CHECK(!three_mm_movie_sound_copy(&f.source,123,&out,&f.s) && out==400 && !f.adopts && !f.deleted);
    init(&f); out=777; f.fail_adopt=1; CHECK(!three_mm_movie_sound_copy(&f.source,123,&out,&f.s) && out==400 && f.deleted==1 && f.deleted_number==400 && !f.flushed);
    init(&f); out=777; f.fail_save=1; CHECK(!three_mm_movie_sound_copy(&f.source,123,&out,&f.s) && out==400 && f.adopts==1 && !f.deleted && !f.gc);
    init(&f); out=777; f.move_cache=1; CHECK(three_mm_movie_sound_copy(&f.source,123,&out,&f.s) && out==400 && f.last_destination==&f.b && f.last_slot==2 && !f.gc && f.inspects==1);
    init(&f); out=777; f.fail_scene=1; CHECK(three_mm_movie_sound_copy(&f.source,123,&out,&f.s) && f.diagnostic[THREE_MM_SOUND_NO_CURRENT_SCENE]==1);
    for (mode=0;mode<2;++mode)
    {
        init(&f); f.source.midi=mode; out=777;
        CHECK(three_mm_movie_sound_import(&f.source,34,&out,&f,&f.s));
        CHECK(out==400 && f.gc==1 && f.midis==mode && f.waves==!mode && (mode || f.style==34));
        CHECK(!f.ensures && f.temps==1 && f.kinds==1 && !f.inspects);
    }
    init(&f); f.fail_temp=1; out=777; CHECK(!three_mm_movie_sound_import(&f.source,34,&out,&f,&f.s) && out==777 && !f.copies && !f.kinds);
    for (mode=0;mode<2;++mode)
    {
        init(&f); f.fail_copy=1; f.bad_sound=mode; out=777;
        CHECK(!three_mm_movie_sound_import(&f.source,34,&out,&f,&f.s));
        CHECK(out==400 && f.diagnostic[THREE_MM_SOUND_CANNOT_COPY]==!mode && !f.adopts && !f.gc);
    }
    init(&f); f.move_cache=1; out=777; CHECK(three_mm_movie_sound_import(&f.source,34,&out,&f,&f.s));
    CHECK(f.last_destination==&f.a && f.current==&f.b && f.last_slot==2); /* Captured destination, refreshed slot source. */
    init(&f); f.fail_adopt=1; out=777; CHECK(!three_mm_movie_sound_import(&f.source,34,&out,&f,&f.s) && f.deleted==1 && !f.gc);
    init(&f); f.fail_save=1; out=777; CHECK(!three_mm_movie_sound_import(&f.source,34,&out,&f,&f.s) && f.flushed==1 && !f.deleted && !f.gc);
    init(&f); valid=1; CHECK(three_mm_movie_sound_unused(&valid,&f.s) && !valid && three_mm_movie_sound_unused(NULL,&f.s));
    edge(&f.a,A,102,99,500,0); CHECK(three_mm_movie_sound_unused(&valid,&f.s) && valid);
    init(&f); f.current=NULL; valid=1; CHECK(!three_mm_movie_sound_unused(&valid,&f.s) && !valid && !f.files);
    puts("C89 movie sound integration checks passed"); return 0;
}
