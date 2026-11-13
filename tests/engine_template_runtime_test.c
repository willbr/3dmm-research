#include "three_mm/template_runtime.h"
#include <assert.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
typedef struct Edge { ThreeMMU32 type,number,id,child_type,child_number; } Edge;
typedef struct Test
{
    ThreeMMTemplateState state; int action,models[2],refs[2],action_refs;
    int parts,fail_action,fail_allocate,fail_model,fetches,sets,freed,releases,cel;
    ThreeMMI32 distance,body_models[6],body_matrices[6],parents[6],sets_list[6],counts[3],ids[3][2];
    void *allocated; int exists,skip_distance; void *probe; int probe_release;
    Edge edges[32]; int edge_count,has_cache,resource_kind,name_fail;
} Test;
static Test *t(void *p) { return (Test *)p; }
static void read_state(void *p,void *object,ThreeMMTemplateState *out) { assert(object==p); *out=t(p)->state; }
static ThreeMMI32 count(void *p,void *list) { assert(list==t(p)->parents); return t(p)->parts; }
static void *action(void *p,void *object,ThreeMMI32 id)
{ assert(object==p && id==0); if (t(p)->fail_action) return NULL; ++t(p)->action_refs; return &t(p)->action; }
static ThreeMMI32 cel_count(void *p,void *a) { assert(a==&t(p)->action); return 3; }
static ThreeMMI32 distance(void *p,void *a,ThreeMMI32 cel)
{ assert(a==&t(p)->action && cel>=0 && cel<3); t(p)->cel=cel; return 100+cel; }
static ThreeMMU32 flags(void *p,void *a) { assert(a==&t(p)->action); return 42; }
static void part(void *p,void *a,ThreeMMI32 cel,ThreeMMI32 n,ThreeMMI32 *model,ThreeMMI32 *matrix)
{ assert(a==&t(p)->action && cel>=0 && cel<3); *model=n%3==0?-1:(n+cel)%2; *matrix=cel; }
static void matrix(void *p,void *a,ThreeMMI32 n,ThreeMMI32 out[12])
{ int i; assert(a==&t(p)->action); for (i=0;i<12;++i) out[i]=1000+n*100+i; }
static int allocate(void *p,ThreeMMI32 n,void **out)
{
    assert(n==t(p)->parts); if (t(p)->fail_allocate) return 0;
    *out=calloc(n?n:1,sizeof(void *)); t(p)->allocated=*out; return *out!=NULL;
}
static void *model(void *p,void *object,ThreeMMU32 id)
{
    assert(object==p && id<2 && !t(p)->sets && t(p)->distance==-77); ++t(p)->fetches;
    if (t(p)->fail_model==t(p)->fetches) return NULL;
    ++t(p)->refs[id]; return &t(p)->models[id];
}
static void set_model(void *p,void *body,ThreeMMI32 n,void *m)
{
    assert(body==p && t(p)->distance==(t(p)->skip_distance?-77:100+t(p)->cel));
    assert(t(p)->fetches==t(p)->parts-t(p)->parts/3-(t(p)->parts%3!=0));
    assert(m==&t(p)->models[0] || m==&t(p)->models[1]);
    t(p)->body_models[n]=m==&t(p)->models[0]?0:1;
}
static void set_matrix(void *p,void *body,ThreeMMI32 n,const ThreeMMI32 in[12])
{
    int i; assert(body==p && t(p)->distance==(t(p)->skip_distance?-77:100+t(p)->cel));
    for (i=0;i<12;++i) assert(in[i]==1000+t(p)->cel*100+i);
    ++t(p)->sets; t(p)->body_matrices[n]=in[0];
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
    else if (v==&x->models[0]) { assert(x->refs[0]>0); --x->refs[0]; }
    else { assert(v==&x->models[1] && x->refs[1]>0); --x->refs[1]; }
}
static void free_memory(void *p,void **v)
{
    ThreeMMI32 i; assert(*v==t(p)->allocated && !t(p)->refs[0] && !t(p)->refs[1]);
    for (i=0;i<t(p)->parts;++i) assert(!((void **)*v)[i]);
    free(*v); *v=NULL; t(p)->allocated=NULL; ++t(p)->freed;
}
static int sound(void *p,void *a,ThreeMMI32 cel,void *tag)
{ assert(a==&t(p)->action && !t(p)->exists); *(int *)tag=cel==2?0:7+cel; return cel!=2; }
static void sound_exists(void *p,void *out,int exists) { assert(out==&t(p)->exists); t(p)->exists=exists; }
static void binding(void *p,void *object,ThreeMMTemplateBinding *out)
{ assert(object==p); out->file=p; out->cache=t(p)->has_cache?p:NULL; out->type=THREE_MM_TMPL; out->number=1; }
static int child(void *p,void *file,ThreeMMU32 type,ThreeMMU32 number,ThreeMMU32 id,ThreeMMU32 ct,ThreeMMTemplateChild *out)
{
    int i; assert(file==p);
    for (i=0;i<t(p)->edge_count;++i)
    {
        Edge *e=&t(p)->edges[i]; if (e->type==type && e->number==number && e->id==id && e->child_type==ct)
        { out->type=e->child_type; out->number=e->child_number; out->id=id; return 1; }
    }
    return 0;
}
static void *fetch(void *p,void *cache,ThreeMMU32 type,ThreeMMU32 number,int kind)
{ assert(cache==p && number==10+(ThreeMMU32)kind); assert(type==(kind==0?THREE_MM_ACTN:kind==1?THREE_MM_TEMPLATE_BMDL:THREE_MM_CMTL)); t(p)->resource_kind=kind; return &t(p)->action; }
static int name(void *p,void *file,ThreeMMU32 type,ThreeMMU32 number,void *out)
{ assert(file==p && type==THREE_MM_ACTN && number==10); if (t(p)->name_fail) return 0; *(int *)out=42; return 1; }
static unsigned char *fixed(void *p,void *group,ThreeMMI32 n) { assert(group==t(p)->counts); return (unsigned char *)&t(p)->counts[n]; }
static unsigned char *variable(void *p,void *group,ThreeMMI32 n) { assert(group==t(p)->counts); return (unsigned char *)t(p)->ids[n]; }
static ThreeMMI32 item(void *p,void *list,ThreeMMI32 n)
{ assert(n>=0 && n<t(p)->parts); assert(list==t(p)->parents || list==t(p)->sets_list); return ((ThreeMMI32 *)list)[n]; }
static ThreeMMTemplateRuntimeServices init(Test *x,int parts)
{
    ThreeMMTemplateRuntimeServices s; int i; memset(x,0,sizeof(*x)); memset(&s,0,sizeof(s));
    x->parts=parts; x->distance=-77; x->has_cache=1;
    x->state.parents=x->parents; x->state.sets=x->sets_list; x->state.costumes=x->counts; x->state.set_count=3;
    for (i=0;i<3;++i) { x->counts[i]=2; x->ids[i][0]=i; x->ids[i][1]=i+3; }
    for (i=0;i<6;++i) { x->body_models[i]=-7; x->body_matrices[i]=-8; x->parents[i]=i?i-1:-1; x->sets_list[i]=i%3; }
    s.storage.context=x; s.storage.read=read_state; s.storage.count=count; s.storage.allocate_refs=allocate;
    s.storage.free_memory=free_memory; s.storage.release=release; s.storage.child=child;
    s.storage.fixed_data=fixed; s.storage.variable_data=variable; s.storage.item=item;
    s.action=action; s.model=model; s.cel_count=cel_count; s.distance=distance; s.flags=flags; s.part=part;
    s.matrix=matrix; s.set_model=set_model; s.set_matrix=set_matrix; s.sound=sound; s.sound_exists=sound_exists;
    s.binding=binding; s.fetch=fetch; s.name=name; return s;
}
static void edge(Test *x,ThreeMMU32 type,ThreeMMU32 number,ThreeMMU32 id,ThreeMMU32 ct,ThreeMMU32 cn)
{ Edge *e=&x->edges[x->edge_count++]; e->type=type; e->number=number; e->id=id; e->child_type=ct; e->child_number=cn; }
int main(void)
{
    Test x; ThreeMMTemplateRuntimeServices s; int n,mode,c,i,expected,ok,tag,out; ThreeMMI32 value; ThreeMMU32 f;
    const ThreeMMI32 cels[]={INT_MIN,-7,-1,0,2,INT_MAX};
    for (n=0;n<=6;n+=2) for (mode=0;mode<7;++mode) for (c=0;c<6;++c)
    {
        s=init(&x,n); x.fail_action=mode==1; x.fail_allocate=mode==2; x.fail_model=mode>=3?mode-2:0;
        expected=cels[c]%3; if (expected<0) expected+=3;
        ok=three_mm_template_set_cel(&x,&x,0,cels[c],&x.distance,&s);
        assert(ok==(!x.fail_action && !x.fail_allocate && !(x.fail_model && x.fail_model<=n-n/3-(n%3!=0))));
        assert(!x.action_refs && !x.refs[0] && !x.refs[1]);
        if (ok)
        {
            assert(x.distance==100+expected && x.sets==n);
            for (i=0;i<n;++i) { assert(x.body_models[i]==(i%3==0?-7:(i+expected)%2)); assert(x.body_matrices[i]==1000+expected*100); }
        }
        else { assert(x.distance==-77 && !x.sets); for (i=0;i<n;++i) assert(x.body_models[i]==-7 && x.body_matrices[i]==-8); }
    }
    for (mode=0;mode<2;++mode) for (c=0;c<6;++c)
    {
        s=init(&x,2); x.fail_action=mode; value=-77; x.probe=&value;
        assert(three_mm_template_distance(&x,0,cels[c],&value,&s)==!mode);
        expected=cels[c]%3; if (expected<0) expected+=3;
        assert(value==(mode?-77:100+expected)); if (!mode) assert(x.probe_release==-77);
        value=-77; assert(three_mm_template_cel_count(&x,0,&value,&s)==!mode); assert(value==(mode?-77:3)); if (!mode) assert(x.probe_release==3);
        f=99; x.probe=&f; assert(three_mm_template_action_flags(&x,0,&f,&s)==!mode && f==(mode?99U:42U));
        if (!mode) assert(x.probe_release==42);
        tag=123; x.exists=1; x.probe=&x.exists; assert(three_mm_template_sound(&x,0,cels[c],&x.exists,&tag,&s)==!mode);
        assert(x.exists==(!mode && expected!=2)); assert(tag==(mode?123:expected==2?0:7+expected)); assert(!x.action_refs);
        if (!mode) assert(x.probe_release==x.exists);
    }
    s=init(&x,6); x.skip_distance=1;
    assert(three_mm_template_set_cel(&x,&x,0,-1,NULL,&s) && x.distance==-77 && x.sets==6 && x.freed==1);
    s=init(&x,4);
    for (i=0;i<3;++i) edge(&x,THREE_MM_TMPL,1,0,i==0?THREE_MM_ACTN:i==1?THREE_MM_TEMPLATE_BMDL:THREE_MM_CMTL,10+i);
    for (i=0;i<3;++i) { assert(three_mm_template_fetch_child(&x,0,i,&s)==&x.action && x.resource_kind==i); assert(!three_mm_template_fetch_child(&x,1,i,&s)); }
    out=-77; assert(three_mm_template_action_name(&x,0,&out,&s) && out==42);
    x.name_fail=1; out=-77; assert(!three_mm_template_action_name(&x,0,&out,&s) && out==-77);
    assert(!three_mm_template_action_name(&x,1,&out,&s) && out==-77);
    s=init(&x,4); x.sets_list[3]=2; x.parents[3]=0;
    for (i=0;i<3;++i) edge(&x,THREE_MM_TMPL,1,i,THREE_MM_CMTL,20+i);
    edge(&x,THREE_MM_CMTL,22,0,THREE_MM_TEMPLATE_BMDL,100);
    for (i=0;i<3;++i) { assert(three_mm_template_costume_count(&x,i,&s)==2); assert(three_mm_template_costume_id(&x,i,1,&s)==i+3); }
    assert(!three_mm_template_is_accessory(&x,0,&s) && three_mm_template_is_accessory(&x,2,&s));
    for (i=0;i<3;++i) { value=-77; assert(three_mm_template_accessory_set(&x,i,&value,&s) && value==2); }
    assert(!three_mm_template_same_accessory(&x,0,2,&s) && three_mm_template_same_accessory(&x,0,1,&s));
    assert(!three_mm_template_same_accessory(&x,0,9,&s));
    edge(&x,THREE_MM_CMTL,21,2,THREE_MM_TEMPLATE_BMDL,999);
    assert(three_mm_template_same_accessory(&x,0,1,&s)); /* both stop at missing ID zero */
    edge(&x,THREE_MM_CMTL,20,0,THREE_MM_TEMPLATE_BMDL,100);
    assert(!three_mm_template_same_accessory(&x,0,1,&s));
    edge(&x,THREE_MM_CMTL,21,0,THREE_MM_TEMPLATE_BMDL,100);
    assert(three_mm_template_same_accessory(&x,0,1,&s)); /* gap one hides extra ID two */
    edge(&x,THREE_MM_CMTL,21,1,THREE_MM_TEMPLATE_BMDL,100);
    assert(!three_mm_template_same_accessory(&x,0,1,&s));
    x.has_cache=0; value=-77; assert(!three_mm_template_is_accessory(&x,2,&s));
    assert(!three_mm_template_accessory_set(&x,0,&value,&s) && value==-77);
    return 0;
}
