#include "mm/template_runtime.h"
#include "mm/movie_session.h"
#include <assert.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
typedef struct Edge { MMU32 type,number,id,child_type,child_number; } Edge;
typedef struct CachedResource { MMResourceObject state; MMReference references; } CachedResource;
typedef struct Test
{
    MMTemplateState state; int action,models[2],refs[2],action_refs;
    int parts,fail_action,fail_allocate,fail_model,fail_read,fetches,indices_reads,sets,freed,releases,cel;
    MMI32 distance,body_models[6],body_matrices[6];
    short parents[6],sets_list[6];
    MMPartIndexList parent_indices,set_indices;
    MMTemplateCostumeTable costumes;
    void *allocated; int exists,skip_distance; void *probe; int probe_release;
    MMResourceObject resource;
    MMResourceCache cache;
    MMResourceEntry resident[3];
    CachedResource resources[3];
    MMChunkEntry entries[64];
    Edge edges[32];
    int entry_count,edge_count,name_fail;
} Test;
static Test *t(void *p) { return (Test *)p; }
static MMTemplateState *template_state(void *p,void *o) { assert(o==p); return &t(p)->state; }
static void *action(void *p,void *object,MMI32 id)
{ assert(object==p && id==0); if (t(p)->fail_action) return NULL; ++t(p)->action_refs; return &t(p)->action; }
static MMI32 cel_count(void *p,void *a)
{ assert(a==&t(p)->action || a==&t(p)->resources[0]); return t(p)->fail_read==4?0:3; }
static int distance(void *p,void *a,MMI32 cel,MMI32 *out)
{
    assert(a==&t(p)->action && cel>=0 && cel<3);
    if (t(p)->fail_read==1) return 0;
    t(p)->cel=cel; *out=100+cel; return 1;
}
static MMU32 flags(void *p,void *a) { assert(a==&t(p)->action); return 42; }
static int part(void *p,void *a,MMI32 cel,MMI32 n,MMI32 *model,MMI32 *matrix)
{
    assert(a==&t(p)->action && cel>=0 && cel<3);
    if (t(p)->fail_read==2) return 0;
    ++t(p)->indices_reads; *model=n%3==0?-1:(n+cel)%2; *matrix=cel; return 1;
}
static int matrix(void *p,void *a,MMI32 n,MMActorMatrix *out)
{
    int i; assert(a==&t(p)->action && !t(p)->sets && t(p)->distance==-77);
    if (t(p)->fail_read==3 && t(p)->indices_reads==2) return 0;
    for (i=0;i<12;++i) out->m[i/3][i%3]=1000+n*100+i;
    return 1;
}
static int allocate(void *p,size_t bytes,void **out)
{
    if (t(p)->fail_allocate) return 0;
    *out=calloc(1,bytes?bytes:1); t(p)->allocated=*out; return *out!=NULL;
}
static void *model(void *p,void *object,MMU32 id,MMMovieAsset *asset)
{
    assert(object==p && id<2 && !t(p)->sets && t(p)->distance==-77); ++t(p)->fetches;
    if (t(p)->fail_model==t(p)->fetches) return NULL;
    asset->source=0; asset->type=MM_TEMPLATE_BMDL; asset->number=100+id;
    ++t(p)->refs[id]; return &t(p)->models[id];
}
static void set_model(void *p,void *body,MMI32 n,void *m)
{
    assert(body==p && t(p)->distance==(t(p)->skip_distance?-77:100+t(p)->cel));
    assert(t(p)->fetches==t(p)->parts-t(p)->parts/3-(t(p)->parts%3!=0));
    assert(m==&t(p)->models[0] || m==&t(p)->models[1]);
    t(p)->body_models[n]=m==&t(p)->models[0]?0:1;
}
static void set_matrix(void *p,void *body,MMI32 n,const MMActorMatrix *in)
{
    int i; assert(body==p && t(p)->distance==(t(p)->skip_distance?-77:100+t(p)->cel));
    for (i=0;i<12;++i) assert(in->m[i/3][i%3]==1000+t(p)->cel*100+i);
    ++t(p)->sets; t(p)->body_matrices[n]=in->m[0][0];
}
static void release(void *p,void *v)
{
    Test *x=t(p); ++x->releases;
    if (v==&x->action)
    {
        assert(x->action_refs==1); --x->action_refs;
        assert(!x->allocated);
        if (x->probe) { x->probe_release=*(int *)x->probe; }
    }
    else if (v==&x->resources[0]) { assert(x->resources[0].references.count==2); mm_reference_drop(&x->resources[0].references); }
    else if (v==&x->models[0]) { assert(x->refs[0]>0); --x->refs[0]; }
    else { assert(v==&x->models[1] && x->refs[1]>0); --x->refs[1]; }
}
static void free_memory(void *p,void **v)
{
    assert(*v==t(p)->allocated && !t(p)->refs[0] && !t(p)->refs[1]);
    free(*v); *v=NULL; t(p)->allocated=NULL; ++t(p)->freed;
}
static int sound(void *p,void *a,MMI32 cel,void *tag)
{ assert(a==&t(p)->action && !t(p)->exists); *(int *)tag=cel==2?0:7+cel; return cel!=2; }
static void sound_exists(void *p,void *out,int exists) { assert(out==&t(p)->exists); t(p)->exists=exists; }
static int graph_entry(void *p, MMU32 index, MMChunkEntry *out)
{
    *out = t(p)->entries[index];
    return 1;
}
static int graph_child(void *p, MMU32 parent, MMU32 index, MMChunkChild *out)
{
    const Test *fixture = t(p);
    const MMChunkEntry *entry = &fixture->entries[parent];
    int i;
    for (i = 0; i < fixture->edge_count; ++i)
    {
        const Edge *e = &fixture->edges[i];
        if (e->type == entry->type && e->number == entry->number && index-- == 0)
        {
            out->type = e->child_type;
            out->number = e->child_number;
            out->slot = e->id;
            return 1;
        }
    }
    return 0;
}
static MMChunkAccess graph(void *p)
{
    MMChunkAccess a;
    a.context=p; a.count=t(p)->entry_count; a.entry=graph_entry; a.child=graph_child;
    return a;
}
static int reader(MMResourceCache *cache, MMU32 type, MMU32 number, MMOwnedBlock *block, void **out, MMI32 *bytes)
{
    (void)cache; (void)type; (void)number; (void)block; (void)out; (void)bytes;
    assert(0);
    return 0;
}
static int compare_reader(MMResourceBlockReader a, MMResourceBlockReader b) { assert(a == reader && b == reader); return 0; }
static MMResourceObject *cached_state(void *object) { return &((CachedResource *)object)->state; }
static MMReference *cached_references(void *object) { return &((CachedResource *)object)->references; }
static int name(void *p,void *file,MMU32 type,MMU32 number,void *out)
{ assert(file==p && type==MM_ACTN && number==10); if (t(p)->name_fail) return 0; *(int *)out=42; return 1; }
static MMTemplateRuntimeServices init(Test *x,int parts)
{
    MMTemplateRuntimeServices s; int i; memset(x,0,sizeof(*x)); memset(&s,0,sizeof(s));
    x->parts=parts; x->distance=-77;
    x->state.parents=&x->parent_indices; x->state.sets=&x->set_indices; x->state.set_count=3;
    x->parent_indices.view.bytes=(const unsigned char *)x->parents; x->parent_indices.view.count=parts;
    x->set_indices.view.bytes=(const unsigned char *)x->sets_list; x->set_indices.view.count=parts;
    for (i=0;i<6;++i) { x->body_models[i]=-7; x->body_matrices[i]=-8; x->parents[i]=i?i-1:-1; x->sets_list[i]=i%3; }
    s.storage.context=x; s.storage.state=template_state;
    s.storage.free_memory=free_memory; s.storage.release=release; s.storage.release_custom=release;
    s.generated_action=action; s.generated_model=model;
    s.allocate_cel=allocate; s.cel_count=cel_count; s.distance=distance; s.flags=flags; s.part=part;
    s.matrix=matrix; s.set_model=set_model; s.set_matrix=set_matrix; s.sound=sound; s.sound_exists=sound_exists;
    x->resource.cache=&x->cache; x->resource.type=MM_TMPL; x->resource.number=1;
    x->cache.file=x; x->cache.owner=x; x->cache.services.live_chunks=graph;
    x->cache.services.compare_reader=compare_reader;
    x->cache.services.objects.state=cached_state; x->cache.services.objects.references=cached_references;
    x->cache.storage.buffers[0].data=(unsigned char *)x->resident;
    x->cache.storage.buffers[0].capacity=sizeof(x->resident);
    s.resource=&x->resource; s.action_reader=reader; s.model_reader=reader; s.name=name;
    return s;
}
static MMChunkEntry *add_entry(Test *x, MMU32 type, MMU32 number)
{
    int i = 0;
    while (i < x->entry_count && (x->entries[i].type < type ||
                                 (x->entries[i].type == type && x->entries[i].number < number)))
        ++i;
    if (i == x->entry_count || x->entries[i].type != type || x->entries[i].number != number)
    {
        assert(x->entry_count < (int)(sizeof(x->entries)/sizeof(x->entries[0])));
        memmove(x->entries+i+1, x->entries+i, (size_t)(x->entry_count-i)*sizeof(x->entries[0]));
        memset(x->entries+i, 0, sizeof(x->entries[0]));
        x->entries[i].type=type; x->entries[i].number=number;
        ++x->entry_count;
    }
    return &x->entries[i];
}
static int edge_order(const void *left, const void *right)
{
    const Edge *a = left, *b = right;
    /* Only sibling order matters; the graph callback filters by parent. */
    if (a->id != b->id) return a->id < b->id ? -1 : 1;
    if (a->child_type != b->child_type) return a->child_type < b->child_type ? -1 : 1;
    return a->child_number == b->child_number ? 0 : a->child_number < b->child_number ? -1 : 1;
}
static void edge(Test *x, MMU32 type, MMU32 number, MMU32 id, MMU32 ct, MMU32 cn)
{
    Edge e;
    add_entry(x, ct, cn);
    ++add_entry(x, type, number)->child_count;
    assert(x->edge_count < (int)(sizeof(x->edges)/sizeof(x->edges[0])));
    e.type=type; e.number=number; e.id=id; e.child_type=ct; e.child_number=cn;
    x->edges[x->edge_count++] = e;
    qsort(x->edges, x->edge_count, sizeof(e), edge_order);
}
static void cache_resource(Test *x, int i, MMU32 type, MMU32 number)
{
    CachedResource *resource = &x->resources[i];
    resource->state.type=type; resource->state.number=number;
    resource->state.cache=&x->cache; resource->state.attached=1;
    resource->references.count=1;
    x->resident[i].reader=reader; x->resident[i].object=resource;
    ++x->cache.count;
}
int main(void)
{
    Test x; MMTemplateRuntimeServices s; int n,mode,c,i,expected,ok,tag,out; MMI32 value; MMU32 f;
    const MMI32 cels[]={INT_MIN,-7,-1,0,2,INT_MAX};
    for (mode=1;mode<=4;++mode)
    {
        s=init(&x,2); x.fail_read=mode;
        assert(!mm_template_set_cel(&x,&x,0,0,&x.distance,&s));
        assert(x.distance==-77 && !x.sets && !x.allocated && !x.action_refs && !x.refs[0] && !x.refs[1]);
    }
    for (n=0;n<=6;n+=2) for (mode=0;mode<7;++mode) for (c=0;c<6;++c)
    {
        s=init(&x,n); x.fail_action=mode==1; x.fail_allocate=mode==2; x.fail_model=mode>=3?mode-2:0;
        expected=cels[c]%3; if (expected<0) expected+=3;
        ok=mm_template_set_cel(&x,&x,0,cels[c],&x.distance,&s);
        assert(ok==(!x.fail_action && !x.fail_allocate && !(x.fail_model && x.fail_model<=n-n/3-(n%3!=0))));
        assert(!x.action_refs && !x.refs[0] && !x.refs[1]);
        if (ok)
        {
            assert(x.distance==100+expected && x.sets==n && x.indices_reads==n);
            for (i=0;i<n;++i) { assert(x.body_models[i]==(i%3==0?-7:(i+expected)%2)); assert(x.body_matrices[i]==1000+expected*100); }
        }
        else { assert(x.distance==-77 && !x.sets); for (i=0;i<n;++i) assert(x.body_models[i]==-7 && x.body_matrices[i]==-8); }
    }
    for (mode=0;mode<2;++mode) for (c=0;c<6;++c)
    {
        s=init(&x,2); x.fail_action=mode; value=-77; x.probe=&value;
        assert(mm_template_distance(&x,0,cels[c],&value,&s)==!mode);
        expected=cels[c]%3; if (expected<0) expected+=3;
        assert(value==(mode?-77:100+expected)); if (!mode) assert(x.probe_release==-77);
        value=-77; assert(mm_template_cel_count(&x,0,&value,&s)==!mode); assert(value==(mode?-77:3)); if (!mode) assert(x.probe_release==3);
        f=99; x.probe=&f; assert(mm_template_action_flags(&x,0,&f,&s)==!mode && f==(mode?99U:42U));
        if (!mode) assert(x.probe_release==42);
        tag=123; x.exists=1; x.probe=&x.exists; assert(mm_template_sound(&x,0,cels[c],&x.exists,&tag,&s)==!mode);
        assert(x.exists==(!mode && expected!=2)); assert(tag==(mode?123:expected==2?0:7+expected)); assert(!x.action_refs);
        if (!mode) assert(x.probe_release==x.exists);
    }
    s=init(&x,6); x.skip_distance=1;
    assert(mm_template_set_cel(&x,&x,0,-1,NULL,&s) && x.distance==-77 && x.sets==6 && x.freed==1);
    s=init(&x,4);
    for (i=0;i<3;++i)
    {
        MMU32 type = i==0 ? MM_ACTN : i==1 ? MM_TEMPLATE_BMDL : MM_CMTL;
        edge(&x,MM_TMPL,1,0,type,10+i);
        cache_resource(&x,i,type,10+i);
    }
    edge(&x,MM_TMPL,1,0,MM_TEMPLATE_BMDL,99);
    edge(&x,MM_TMPL,1,7,MM_CMTL,12);
    for (i=0;i<3;++i)
    {
        MMMovieAsset asset = {0};
        assert(mm_template_fetch_child(&x.resource,0,i,reader,NULL,&asset)==&x.resources[i]);
        assert(asset.type==x.resources[i].state.type && asset.number==10+(MMU32)i);
        assert(x.resources[i].references.count==2);
        mm_reference_drop(&x.resources[i].references);
        assert(!mm_template_fetch_child(&x.resource,1,i,reader,NULL,&asset) && asset.number==10+(MMU32)i);
    }
    assert(mm_template_fetch_child(&x.resource,7,MM_TEMPLATE_CUSTOM,reader,NULL,NULL)==&x.resources[2]);
    mm_reference_drop(&x.resources[2].references);
    assert(!mm_template_fetch_child(&x.resource,0,MM_TEMPLATE_RESOURCE_COUNT,reader,NULL,NULL));
    s.generated_action=NULL; value=-77;
    assert(mm_template_cel_count(&x,0,&value,&s) && value==3 && x.resources[0].references.count==1);
    out=-77; assert(mm_template_action_name(0,&out,&s) && out==42);
    x.name_fail=1; out=-77; assert(!mm_template_action_name(0,&out,&s) && out==-77);
    assert(!mm_template_action_name(1,&out,&s) && out==-77);
    s=init(&x,4); x.sets_list[3]=2; x.parents[3]=0;
    assert(mm_template_costumes_init(&x.costumes, NULL));
    x.state.costumes = &x.costumes;
    for (i=0;i<3;++i)
    {
        MMI32 row[2];
        row[0] = i;
        row[1] = i + 3;
        assert(mm_template_costumes_append(&x.costumes, row, 2));
    }
    for (i=0;i<3;++i) edge(&x,MM_TMPL,1,i,MM_CMTL,20+i);
    edge(&x,MM_CMTL,22,0,MM_TEMPLATE_BMDL,100);
    for (i=0;i<3;++i) { assert(mm_template_costume_count(&x,i,&s)==2); assert(mm_template_costume_id(&x,i,1,&s)==i+3); }
    assert(!mm_template_is_accessory(&x,0,&s) && mm_template_is_accessory(&x,2,&s));
    for (i=0;i<3;++i) { value=-77; assert(mm_template_accessory_set(&x,i,&value,&s) && value==2); }
    assert(!mm_template_same_accessory(0,2,&s) && mm_template_same_accessory(0,1,&s));
    assert(!mm_template_same_accessory(0,9,&s));
    edge(&x,MM_CMTL,21,2,MM_TEMPLATE_BMDL,999);
    assert(mm_template_same_accessory(0,1,&s)); /* both stop at missing ID zero */
    edge(&x,MM_CMTL,20,0,MM_TEMPLATE_BMDL,100);
    assert(!mm_template_same_accessory(0,1,&s));
    edge(&x,MM_CMTL,21,0,MM_TEMPLATE_BMDL,100);
    assert(mm_template_same_accessory(0,1,&s)); /* gap one hides extra ID two */
    edge(&x,MM_CMTL,21,1,MM_TEMPLATE_BMDL,100);
    assert(!mm_template_same_accessory(0,1,&s));
    x.resource.cache=NULL; value=-77; assert(!mm_template_is_accessory(&x,2,&s));
    assert(!mm_template_accessory_set(&x,0,&value,&s) && value==-77);
    mm_template_costumes_dispose(&x.costumes);
    return 0;
}
