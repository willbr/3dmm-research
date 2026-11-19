#include "mm/movie_sound.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(c) do { if (!(c)) { fprintf(stderr,"movie sound check %d: %s\n",__LINE__,#c); exit(1); } } while (0)
#define M MM_SOUND_MOVIE
#define S MM_SOUND_SCENE
#define A MM_SOUND_CHUNK
typedef struct Fixture Fixture;
typedef struct Edge { MMU32 type,number; MMChunkChild child; } Edge;
typedef struct File { Fixture *f; Edge edges[32]; int count,refs,midi; } File;
struct Fixture
{
    File a,b,source,*current,*last_destination; MMMovieSoundServices s; MMTag *original;
    int reads,files,views,enums,scene_rows,adopts,opens,closes,saves,ensures,temps,inspects,copies,waves,midis,deleted,flushed,gc,kinds;
    int fail_entry,fail_enum,fail_scene,fail_adopt,fail_open,fail_tag,fail_ensure,fail_copy,fail_temp,fail_save,bad_sound,move_cache;
    MMU32 fault_parent;
    MMU32 last_slot,deleted_number; MMI32 style; int diagnostic[4];
};
static int order(MMU32 a, MMU32 b)
{
    return a < b ? -1 : a != b;
}
static int edge_order(const void *left, const void *right)
{
    const Edge *a = (const Edge *)left, *b = (const Edge *)right;
    int result = order(a->type, b->type);
    if (!result) result = order(a->number, b->number);
    if (!result) result = order(a->child.slot, b->child.slot);
    if (!result) result = order(a->child.type, b->child.type);
    return result ? result : order(a->child.number, b->child.number);
}
static void edge(File *file, MMU32 type, MMU32 number, MMU32 child_type, MMU32 child, MMU32 slot)
{
    Edge *e;
    CHECK(file->count < 32);
    e = &file->edges[file->count++];
    e->type = type;
    e->number = number;
    e->child.type = child_type;
    e->child.number = child;
    e->child.slot = slot;
    qsort(file->edges, (size_t)file->count, sizeof(*e), edge_order);
}
static void read_state(void *p,MMMovieSoundState *state)
{ Fixture *f=(Fixture *)p; ++f->reads; state->cache=f->current; state->movie=10; state->scene=0; }
static void *file(void *p) { File *v=(File *)p; CHECK(v); ++v->f->files; return p; }
static const MMU32 keys[][2] = {{A, 100}, {A, 101}, {A, 102}, {M, 10}, {S, 20}};
static int graph_entry(void *p, MMU32 position, MMChunkEntry *entry)
{
    const File *file = (const File *)p;
    const Edge *e;
    int i;
    CHECK(position < sizeof(keys) / sizeof(keys[0]));
    if (file->f->fail_entry)
        return 0;
    memset(entry, 0, sizeof(*entry));
    entry->type = keys[position][0];
    entry->number = keys[position][1];
    for (i = 0; i < file->count; ++i)
    {
        e = &file->edges[i];
        if (e->type == entry->type && e->number == entry->number)
            ++entry->child_count;
        if (e->child.type == entry->type && e->child.number == entry->number)
            ++entry->parent_count;
    }
    return 1;
}
static int graph_child(void *p, MMU32 parent, MMU32 index, MMChunkChild *out)
{
    File *file = (File *)p;
    Fixture *f = file->f;
    const Edge *e;
    int i;
    CHECK(parent < sizeof(keys) / sizeof(keys[0]));
    if ((!f->fault_parent || f->fault_parent == keys[parent][0]) && ++f->enums == f->fail_enum)
        return 0;
    for (i = 0; i < file->count; ++i)
    {
        e = &file->edges[i];
        if (e->type != keys[parent][0] || e->number != keys[parent][1] || index--)
            continue;
        if (e->type == M && e->child.type == S)
        {
            ++f->scene_rows;
            if (f->reads == f->fail_scene)
                return 0;
        }
        *out = e->child;
        return 1;
    }
    return 0;
}
static MMChunkAccess chunks(void *p)
{
    File *file = (File *)p;
    MMChunkAccess view = {NULL, sizeof(keys) / sizeof(keys[0]), graph_entry, graph_child};
    view.context = p;
    ++file->f->views;
    return view;
}
static int adopt(void *p, MMU32 type, MMU32 number, MMU32 child_type, MMU32 value, MMU32 slot)
{
    File *file = (File *)p;
    Fixture *f = file->f;
    const Edge *e;
    int i;
    ++f->adopts;
    f->last_slot = slot;
    f->last_destination = file;
    if (f->fail_adopt)
        return 0;
    for (i = 0; i < file->count; ++i)
    {
        e = &file->edges[i];
        if (e->type == type && e->number == number && e->child.type == child_type && e->child.slot == slot)
            return 0;
    }
    edge(file, type, number, child_type, value, slot);
    return 1;
}
static int open_tag(void *p,MMTag *tag,void *cache)
{
    Fixture *f=(Fixture *)p; ++f->opens; CHECK(f->original->cache==&f->a && f->original->number==999);
    if (f->fail_open) { tag->cache=NULL; return 0; }
    ++((File *)cache)->refs; tag->cache=cache; return 1;
}
static void close_tag(void *p,MMTag *tag)
{ Fixture *f=(Fixture *)p; ++f->closes; CHECK(f->opens==1 && f->b.refs==2 && tag->cache==&f->a); --f->a.refs; tag->cache=NULL; }
static int save_tag(void *p,MMTag *tag,void *cache,int redirect)
{ Fixture *f=(Fixture *)p; CHECK(cache==f->current && redirect==1 && tag); ++f->saves; return !f->fail_tag; }
static int ensure(void *p)
{ Fixture *f=(Fixture *)p; ++f->ensures; if (f->fail_ensure) return 0; if (f->move_cache) f->current=&f->b; return 1; }
static int temp(void *p)
{ Fixture *f=(Fixture *)p; ++f->temps; if (f->fail_temp) return 0; if (f->move_cache) f->current=&f->b; return 1; }
static void inspect(void *p) { ++((File *)p)->f->inspects; }
static int copy_chunk(void *source,MMU32 type,MMU32 number,void *destination,MMU32 *out)
{ Fixture *f=((File *)source)->f; CHECK(source==&f->source && type==A && number==123); ++f->copies; f->last_destination=(File *)destination; *out=400; return !f->fail_copy; }
static void delete_chunk(void *p,MMU32 type,MMU32 number)
{ Fixture *f=((File *)p)->f; CHECK(type==A); ++f->deleted; f->deleted_number=number; }
static int save_file(void *p,MMU32 creator)
{ Fixture *f=((File *)p)->f; CHECK(creator==MM_SOUND_CREATOR && f->adopts==1); ++f->flushed; return !f->fail_save; }
static int is_midi(void *p) { File *v=(File *)p; ++v->f->kinds; return v->midi; }
static int copy_midi(void *source,void *destination,MMU32 *out,void *name)
{ Fixture *f=((File *)source)->f; CHECK(name==f); ++f->midis; return copy_chunk(source,A,123,destination,out); }
static int copy_wave(void *source,void *destination,MMI32 style,MMU32 *out,void *name)
{ Fixture *f=((File *)source)->f; CHECK(name==f); ++f->waves; f->style=style; return copy_chunk(source,A,123,destination,out); }
static int has_bad(void *p) { return ((Fixture *)p)->bad_sound; }
static void mark_gc(void *p) { Fixture *f=(Fixture *)p; CHECK(f->flushed==1 && !f->fail_save); ++f->gc; }
static void diagnostic(void *p,int which) { Fixture *f=(Fixture *)p; CHECK(which>=0 && which<4); ++f->diagnostic[which]; }
static void init(Fixture *f)
{
    MMMovieSoundServices *s;
    memset(f,0,sizeof(*f)); f->a.f=f->b.f=f->source.f=f; f->a.refs=f->b.refs=1; f->current=&f->a;
    edge(&f->a,M,10,S,20,0); edge(&f->a,S,20,A,100,0); edge(&f->a,S,20,99,999,1); edge(&f->a,S,20,A,101,2); edge(&f->a,M,10,A,100,0);
    edge(&f->b,M,10,S,20,0); edge(&f->b,S,20,A,102,3); edge(&f->b,M,10,A,101,0); edge(&f->b,M,10,A,102,1);
    s=&f->s; s->context=f; s->read=read_state; s->file=file; s->chunks=chunks; s->adopt=adopt;
    s->open_tag=open_tag; s->close_tag=close_tag; s->save_tag=save_tag; s->ensure_autosave=ensure; s->use_temp=temp;
    s->inspect_source=inspect; s->copy_chunk=copy_chunk; s->delete_chunk=delete_chunk; s->save_file=save_file;
    s->is_midi=is_midi; s->copy_midi=copy_midi; s->copy_wave=copy_wave; s->has_bad_sound_error=has_bad; s->mark_gc=mark_gc;
    s->diagnostic=diagnostic;
}
int main(void)
{
    Fixture f; MMTag tag; MMChunkAccess graph; MMU32 out; int valid,mode,result;
    init(&f); CHECK(mm_movie_sound_next_scene(&f.s)==1 && mm_movie_sound_next_movie(&f.s)==1);
    init(&f); f.fail_scene=1; CHECK(!mm_movie_sound_next_scene(&f.s));
    init(&f);
    edge(&f.a, S, 20, A, 102, 0); /* Two sounds at one slot occupy it once. */
    graph = chunks(&f.a);
    out = 777;
    CHECK(mm_movie_sound_free_slot(&graph, S, 20, &out) == MM_CHUNK_OK && out == 1);
    CHECK(mm_movie_sound_free_slot(&graph, S, 999, &out) == MM_CHUNK_NOT_FOUND && out == 1);
    f.fail_enum = f.enums + 1;
    CHECK(mm_movie_sound_free_slot(&graph, S, 20, &out) == MM_CHUNK_IO && out == 1);
    init(&f); CHECK(mm_movie_sound_is_child(&f.a,A,100,&f.s)); CHECK(!mm_movie_sound_is_child(&f.a,A,102,&f.s));
    init(&f); f.fail_enum=1; CHECK(!mm_movie_sound_is_child(&f.a,A,100,&f.s) && f.diagnostic[0]==1);
    init(&f); f.fail_enum=1; CHECK(mm_movie_sound_adopt(&f.a,20,&f.s) && !f.adopts && f.diagnostic[0]==1);
    init(&f); edge(&f.a,S,20,A,102,4); CHECK(mm_movie_sound_adopt(&f.a,20,&f.s)); CHECK(f.adopts==2 && f.last_slot==2);
    init(&f); f.fail_adopt=1; CHECK(!mm_movie_sound_adopt(&f.a,20,&f.s) && f.adopts==1);
    /* An external graph still takes its candidate ID from the working file. */
    init(&f); edge(&f.b,S,20,A,103,5); CHECK(!mm_movie_sound_adopt(&f.b,20,&f.s)); CHECK(f.last_slot==1 && f.last_destination==&f.b);
    for (mode=0;mode<3;++mode)
    {
        init(&f); tag.sid=0; tag.cache=&f.a; tag.type=A; tag.number=mode==2?102:999; f.a.refs=2; f.original=&tag; f.fail_open=mode==1;
        result=mm_movie_sound_resolve(&tag,3,MM_SOUND_NO_SCENE,&f.b,&f.s); CHECK(result==(mode!=1));
        if (mode==0) CHECK(tag.number==102 && tag.cache==&f.b && f.opens==1 && f.closes==1 && f.a.refs==1 && f.b.refs==2);
        else CHECK(tag.cache==&f.a && f.a.refs==2 && f.b.refs==1 && !f.closes);
    }
    init(&f); tag.sid=-1; tag.cache=&f.a; tag.number=999; CHECK(mm_movie_sound_resolve(&tag,999,MM_SOUND_NO_SCENE,&f.b,&f.s)); CHECK(f.files==1 && !f.views);
    init(&f); tag.sid=0; f.fail_scene=1; CHECK(!mm_movie_sound_resolve(&tag,3,MM_SOUND_NO_SCENE,NULL,&f.s) && !f.opens);
    init(&f); tag.sid=0; tag.cache=&f.a; tag.type=A; tag.number=999; f.fail_scene=1;
    CHECK(!mm_movie_sound_resolve(&tag,999,20,NULL,&f.s) && !f.scene_rows && !f.opens);
    init(&f); CHECK(mm_movie_sound_save(&tag,&f.s) && f.saves==1); f.fail_tag=1; CHECK(!mm_movie_sound_save(&tag,&f.s));
    init(&f); out=777; CHECK(mm_movie_sound_child(100,&out,&f.s) && out==0 && !f.adopts);
    init(&f); out=777; CHECK(mm_movie_sound_child(102,&out,&f.s) && out==1 && f.views==2 && f.adopts==1);
    init(&f); out=777; f.fail_adopt=1; CHECK(!mm_movie_sound_child(102,&out,&f.s) && out==1 && f.adopts==1);
    init(&f); out=777; f.fail_enum=1; f.fault_parent=S; CHECK(!mm_movie_sound_child(102,&out,&f.s) && out==777 && !f.adopts);
    init(&f); out=777; f.fail_scene=2; CHECK(!mm_movie_sound_child(102,&out,&f.s) && out==0 && f.adopts==1 && f.last_slot==0);
    init(&f); out=777; f.fail_ensure=1; CHECK(!mm_movie_sound_copy(&f.source,123,&out,&f.s) && out==777 && !f.reads && !f.copies);
    init(&f); out=777; f.fail_copy=1; CHECK(!mm_movie_sound_copy(&f.source,123,&out,&f.s) && out==400 && !f.adopts && !f.deleted);
    init(&f); out=777; f.fail_adopt=1; CHECK(!mm_movie_sound_copy(&f.source,123,&out,&f.s) && out==400 && f.deleted==1 && f.deleted_number==400 && !f.flushed);
    init(&f); out=777; f.fail_save=1; CHECK(!mm_movie_sound_copy(&f.source,123,&out,&f.s) && out==400 && f.adopts==1 && !f.deleted && !f.gc);
    init(&f); out=777; f.move_cache=1; CHECK(mm_movie_sound_copy(&f.source,123,&out,&f.s) && out==400 && f.last_destination==&f.b && f.last_slot==2 && !f.gc && f.inspects==1);
    init(&f); out=777; f.fail_scene=2; CHECK(mm_movie_sound_copy(&f.source,123,&out,&f.s) && f.diagnostic[MM_SOUND_NO_CURRENT_SCENE]==1);
    for (mode=0;mode<2;++mode)
    {
        init(&f); f.source.midi=mode; out=777;
        CHECK(mm_movie_sound_import(&f.source,34,&out,&f,&f.s));
        CHECK(out==400 && f.gc==1 && f.midis==mode && f.waves==!mode && (mode || f.style==34));
        CHECK(!f.ensures && f.temps==1 && f.kinds==1 && !f.inspects);
    }
    init(&f); f.fail_temp=1; out=777; CHECK(!mm_movie_sound_import(&f.source,34,&out,&f,&f.s) && out==777 && !f.copies && !f.kinds);
    for (mode=0;mode<2;++mode)
    {
        init(&f); f.fail_copy=1; f.bad_sound=mode; out=777;
        CHECK(!mm_movie_sound_import(&f.source,34,&out,&f,&f.s));
        CHECK(out==400 && f.diagnostic[MM_SOUND_CANNOT_COPY]==!mode && !f.adopts && !f.gc);
    }
    init(&f); f.move_cache=1; out=777; CHECK(mm_movie_sound_import(&f.source,34,&out,&f,&f.s));
    CHECK(f.last_destination==&f.a && f.current==&f.b && f.last_slot==2); /* Captured destination, refreshed slot source. */
    init(&f); f.fail_adopt=1; out=777; CHECK(!mm_movie_sound_import(&f.source,34,&out,&f,&f.s) && f.deleted==1 && !f.gc);
    init(&f); f.fail_save=1; out=777; CHECK(!mm_movie_sound_import(&f.source,34,&out,&f,&f.s) && f.flushed==1 && !f.deleted && !f.gc);
    init(&f); valid=1; CHECK(mm_movie_sound_unused(&valid,&f.s) && !valid && mm_movie_sound_unused(NULL,&f.s));
    edge(&f.a,A,102,99,500,0); CHECK(mm_movie_sound_unused(&valid,&f.s) && valid);
    init(&f); f.fail_entry=1; valid=1;
    CHECK(!mm_movie_sound_unused(&valid,&f.s) && !valid && f.diagnostic[MM_SOUND_BAD_CHUNK_ENUM]==1);
    init(&f); f.current=NULL; valid=1; CHECK(!mm_movie_sound_unused(&valid,&f.s) && !valid && !f.files);
    puts("C89 movie sound integration checks passed"); return 0;
}
