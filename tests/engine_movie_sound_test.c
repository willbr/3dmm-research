#include "mm/movie_sound.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "chunk_seed_fixture.h"
#include "tag_copy_fixture.h"
#define CHECK(c) do { if (!(c)) { fprintf(stderr,"movie sound check %d: %s\n",__LINE__,#c); exit(1); } } while (0)
#define M MM_SOUND_MOVIE
#define S MM_SOUND_SCENE
#define A MM_SOUND_CHUNK
typedef struct Fixture Fixture;
/* Graph observers and payload I/O share the archive's address. */
typedef struct File { TagCopyFile archive; Fixture *f; int refs,midi; } File;
struct Fixture
{
    File a,b,source,*current,*last_destination; MMMovieSoundServices s; MMTagServices tags; MMTag *original;
    int reads,files,views,enums,scene_rows,adopts,opens,closes,saves,ensures,temps,inspects,copies,waves,midis,deleted,flushed,gc,kinds;
    int fail_entry,fail_enum,fail_scene,fail_adopt,fail_tag,fail_ensure,fail_copy,fail_temp,fail_save,bad_sound,move_cache;
    int missing_copy;
    MMU32 fault_parent;
    MMU32 last_slot,deleted_number; MMI32 style; int diagnostic[MM_SOUND_DELETE_FAILED + 1];
};
static void edge(File *file, MMU32 type, MMU32 number, MMU32 child_type, MMU32 child, MMU32 slot)
{
    fixture_chunk_seed_link(&file->archive.index, type, number, child_type, child, slot);
}
static void read_state(void *p,MMMovieSoundState *state)
{ Fixture *f=(Fixture *)p; ++f->reads; state->cache=f->current; state->movie=10; state->scene=0; }
static void *file(void *p) { File *v=(File *)p; CHECK(v); ++v->f->files; return p; }
static int graph_entry(void *p, MMU32 position, MMChunkEntry *entry)
{
    const ChunkIndexFixture *index = p;
    const File *file = index->observer;
    return !file->f->fail_entry && fixture_chunk_entry(p, position, entry);
}
static int graph_child(void *p, MMU32 parent, MMU32 position, MMChunkChild *out)
{
    const ChunkIndexFixture *index = p;
    const File *file = index->observer;
    Fixture *f = file->f;
    MMU32 parent_type;
    CHECK(parent < index->count);
    parent_type = index->rows[parent].entry.type;
    if ((!f->fault_parent || f->fault_parent == parent_type) && ++f->enums == f->fail_enum)
        return 0;
    if (!fixture_chunk_child(p, parent, position, out))
        return 0;
    if (parent_type == M && out->type == S)
    {
        ++f->scene_rows;
        if (f->reads == f->fail_scene)
            return 0;
    }
    return 1;
}
static int insert_child(void *p, MMU32 parent, MMU32 position, const MMChunkChild *child)
{
    const ChunkIndexFixture *index = p;
    File *file = index->observer;
    Fixture *f = file->f;
    ++f->adopts;
    f->last_slot = child->slot;
    f->last_destination = file;
    return !f->fail_adopt && fixture_chunk_insert_link(p, parent, position, child);
}
static MMChunkEdit edit_index(void *p)
{
    File *file = p;
    MMChunkEdit edit = fixture_chunk_edit(&file->archive.index);
    ++file->f->views;
    edit.entry = graph_entry;
    edit.child = graph_child;
    edit.insert_child = insert_child;
    return edit;
}
static void retain_cache(void *p)
{
    File *cache = p;
    Fixture *f = cache->f;
    if (f->original)
        CHECK(f->original->cache == &f->a && f->original->number == 999);
    ++f->opens;
    ++cache->refs;
}
static void release_cache(void *p)
{
    File *cache = p;
    Fixture *f = cache->f;
    if (f->original)
        CHECK(f->opens == 1 && f->b.refs == 2 && cache == &f->a && f->original->cache == cache);
    ++f->closes;
    --cache->refs;
}
static MMChunkEndpoint bind_file(void *p, MMChunkFileCopy *binding)
{
    const MMChunkEndpoint denied = {0};
    File *file = p;
    Fixture *f = file->f;
    if (file == &f->source)
    {
        ++f->copies;
        if (f->fail_copy)
            return denied;
    }
    else
        f->last_destination = file;
    return tag_copy_file_endpoint(&file->archive, binding);
}
static MMChunkEndpoint bind_cache(void *p, MMResourceChunkCopy *binding)
{
    const MMChunkEndpoint denied = {0};
    File *file = p;
    ++file->f->saves;
    return file->f->fail_tag ? denied : tag_copy_file_endpoint(&file->archive, &binding->live);
}
static void missing_copy(void *p) { ++((Fixture *)p)->missing_copy; }
static void invalidate_number(MMU32 *out) { *out = 400; }
static int ensure(void *p)
{ Fixture *f=(Fixture *)p; ++f->ensures; if (f->fail_ensure) return 0; if (f->move_cache) f->current=&f->b; return 1; }
static int temp(void *p)
{ Fixture *f=(Fixture *)p; ++f->temps; if (f->fail_temp) return 0; if (f->move_cache) f->current=&f->b; return 1; }
static void inspect(void *p) { ++((File *)p)->f->inspects; }
static int import_sound(void *source, void *destination, MMU32 *out)
{
    Fixture *f = ((File *)source)->f;
    File *file = destination;
    CHECK(source == &f->source);
    ++f->copies;
    f->last_destination = file;
    *out = 400;
    if (f->fail_copy)
        return 0;
    fixture_chunk_add(&file->archive.index, A, *out);
    return 1;
}
static void erase_chunk(void *p, const MMChunkEntry *entry)
{
    Fixture *f = ((File *)p)->f;
    CHECK(entry->type == A);
    ++f->deleted;
    f->deleted_number = entry->number;
}
static int save_file(void *p,MMU32 creator)
{ Fixture *f=((File *)p)->f; CHECK(creator==MM_SOUND_CREATOR && f->adopts==1); ++f->flushed; return !f->fail_save; }
static int is_midi(void *p) { File *v=(File *)p; ++v->f->kinds; return v->midi; }
static int copy_midi(void *source,void *destination,MMU32 *out,void *name)
{ Fixture *f=((File *)source)->f; CHECK(name==f); ++f->midis; return import_sound(source,destination,out); }
static int copy_wave(void *source,void *destination,MMI32 style,MMU32 *out,void *name)
{ Fixture *f=((File *)source)->f; CHECK(name==f); ++f->waves; f->style=style; return import_sound(source,destination,out); }
static int has_bad(void *p) { return ((Fixture *)p)->bad_sound; }
static void mark_gc(void *p) { Fixture *f=(Fixture *)p; CHECK(f->flushed==1 && !f->fail_save); ++f->gc; }
static void diagnostic(void *p,int which) { Fixture *f=(Fixture *)p; CHECK(which>=0 && which<=MM_SOUND_DELETE_FAILED); ++f->diagnostic[which]; }
static void init(Fixture *f)
{
    MMMovieSoundServices *s;
    memset(f,0,sizeof(*f)); f->a.f=f->b.f=f->source.f=f; f->a.refs=f->b.refs=f->source.refs=1; f->current=&f->a;
    tag_copy_file_init(&f->a.archive);
    tag_copy_file_init(&f->b.archive);
    tag_copy_file_init(&f->source.archive);
    fixture_chunk_add(&f->source.archive.index, A, 123);
    edge(&f->a,M,10,S,20,0); edge(&f->a,S,20,A,100,0); edge(&f->a,S,20,99,999,1); edge(&f->a,S,20,A,101,2); edge(&f->a,M,10,A,100,0);
    edge(&f->b,M,10,S,20,0); edge(&f->b,S,20,A,102,3); edge(&f->b,M,10,A,101,0); edge(&f->b,M,10,A,102,1);
    fixture_chunk_add(&f->a.archive.index, A, 102);
    fixture_chunk_add(&f->b.archive.index, A, 100);
    f->a.archive.index.observer = &f->a;
    f->b.archive.index.observer = &f->b;
    f->a.archive.index.before_delete = f->b.archive.index.before_delete = erase_chunk;
    f->tags.context=f; f->tags.file=file; f->tags.retain=retain_cache; f->tags.release=release_cache;
    f->tags.bind_file=bind_file; f->tags.bind_cache=bind_cache; f->tags.missing_chunk=missing_copy;
    s=&f->s; s->context=f; s->read=read_state; s->tags=&f->tags; s->edit=edit_index;
    s->ensure_autosave=ensure; s->use_temp=temp;
    s->inspect_source=inspect; s->invalidate_number=invalidate_number; s->save_file=save_file;
    s->is_midi=is_midi; s->copy_midi=copy_midi; s->copy_wave=copy_wave; s->has_bad_sound_error=has_bad; s->mark_gc=mark_gc;
    s->diagnostic=diagnostic;
}
int main(void)
{
    Fixture f; MMTag tag; MMChunkEdit edit; MMChunkAccess graph; MMU32 out,parent; int valid,mode,result;
    init(&f); CHECK(mm_movie_sound_next_scene(&f.s)==1 && mm_movie_sound_next_movie(&f.s)==1);
    init(&f); f.fail_scene=1; CHECK(!mm_movie_sound_next_scene(&f.s));
    init(&f);
    edge(&f.a, S, 20, A, 102, 0); /* Two sounds at one slot occupy it once. */
    edit = edit_index(&f.a);
    graph = mm_chunk_edit_access(&edit);
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
    init(&f); edge(&f.b,S,20,A,103,5); CHECK(mm_movie_sound_adopt(&f.b,20,&f.s)); CHECK(f.last_slot==1 && f.last_destination==&f.b);
    graph = fixture_chunk_access(&f.b.archive.index);
    CHECK(mm_chunk_find(&graph, M, 10, &parent) == MM_CHUNK_OK);
    CHECK(mm_chunk_find_child(&graph, parent, 1, A, 102, &out) == MM_CHUNK_OK);
    CHECK(mm_chunk_find_child(&graph, parent, 1, A, 103, &out) == MM_CHUNK_OK);
    for (mode=0;mode<3;++mode)
    {
        init(&f); tag.sid=0; tag.cache=&f.a; tag.type=A; tag.number=mode==2?102:999; f.a.refs=2; f.original=&tag;
        result=mm_movie_sound_resolve(&tag,mode==1?4:3,MM_SOUND_NO_SCENE,&f.b,&f.s); CHECK(result==(mode!=1));
        if (mode==0) CHECK(tag.number==102 && tag.cache==&f.b && f.opens==1 && f.closes==1 && f.a.refs==1 && f.b.refs==2);
        else CHECK(tag.cache==&f.a && f.a.refs==2 && f.b.refs==1 && !f.opens && !f.closes);
    }
    init(&f); tag.sid=-1; tag.cache=&f.a; tag.number=999; CHECK(mm_movie_sound_resolve(&tag,999,MM_SOUND_NO_SCENE,&f.b,&f.s)); CHECK(f.files==1 && !f.views);
    init(&f); tag.sid=0; f.fail_scene=1; CHECK(!mm_movie_sound_resolve(&tag,3,MM_SOUND_NO_SCENE,NULL,&f.s) && !f.opens);
    init(&f); tag.sid=0; tag.cache=&f.a; tag.type=A; tag.number=999; f.fail_scene=1;
    CHECK(!mm_movie_sound_resolve(&tag,999,20,NULL,&f.s) && !f.scene_rows && !f.opens);
    init(&f); tag.cache=&f.source; tag.number=123; f.source.refs=2; f.fail_tag=1;
    CHECK(!mm_movie_sound_save(&tag,&f.s) && tag.cache==&f.source && f.source.refs==2 && f.a.refs==1);
    f.fail_tag=0;
    CHECK(mm_movie_sound_save(&tag,&f.s) && f.saves==2 && tag.cache==&f.a && tag.number==123 &&
          f.source.refs==1 && f.a.refs==2);
    init(&f); out=777; CHECK(mm_movie_sound_child(100,&out,&f.s) && out==0 && !f.adopts);
    init(&f); out=777; CHECK(mm_movie_sound_child(102,&out,&f.s) && out==1 && f.views==2 && f.adopts==1);
    init(&f); out=777; f.fail_adopt=1; CHECK(!mm_movie_sound_child(102,&out,&f.s) && out==1 && f.adopts==1);
    init(&f); out=777;
    CHECK(!mm_movie_sound_child(999,&out,&f.s) && out==1 && !f.adopts && f.diagnostic[MM_SOUND_MISSING_CHUNK]==1);
    init(&f); out=777; f.fail_enum=1; f.fault_parent=S; CHECK(!mm_movie_sound_child(102,&out,&f.s) && out==777 && !f.adopts);
    init(&f); out=777; f.fail_scene=2; CHECK(mm_movie_sound_child(102,&out,&f.s) && out==0 && f.adopts==1 && f.last_slot==0);
    init(&f); out=777; f.fail_ensure=1; CHECK(!mm_movie_sound_copy(&f.source,123,&out,&f.s) && out==777 && !f.reads && !f.copies);
    init(&f); out=777; f.fail_copy=1; CHECK(!mm_movie_sound_copy(&f.source,123,&out,&f.s) && out==400 && !f.adopts && !f.deleted);
    init(&f); out=777; CHECK(!mm_movie_sound_copy(&f.source,124,&out,&f.s) && out==400 && f.missing_copy==1 && !f.adopts);
    init(&f); out=777; f.fail_adopt=1; CHECK(!mm_movie_sound_copy(&f.source,123,&out,&f.s) && out==123 && f.deleted==1 && f.deleted_number==123 && !f.flushed);
    init(&f); out=777; f.fail_save=1; CHECK(!mm_movie_sound_copy(&f.source,123,&out,&f.s) && out==123 && f.adopts==1 && !f.deleted && !f.gc);
    init(&f); out=777; f.move_cache=1; fixture_chunk_add(&f.b.archive.index,A,123);
    CHECK(mm_movie_sound_copy(&f.source,123,&out,&f.s) && out==0 && f.last_destination==&f.b && f.last_slot==2 && !f.gc && f.inspects==1);
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
