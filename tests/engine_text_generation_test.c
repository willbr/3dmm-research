#include "three_mm/text_generation.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>
typedef struct Object { int kind,id,refs,entry,count,capacity,stride; unsigned char *data; } Object;
typedef struct Test
{
    ThreeMMTextGenerated generated; ThreeMMTemplateState template_state;
    int length,shape,phase,fail,live,buffers,next_id,events[128],event_count,published,missing_font,missing_glyph,body_fail,last_retain_id,last_release_id;
    unsigned char chars[2]; Object *font; ThreeMMTextGenerationServices services;
} Test;
static Test *t(void *p) { return (Test *)p; }
static int checkpoint(Test *x) { return ++x->phase==x->fail; }
static Object *object(Test *x,int kind,int entry,int capacity)
{
    Object *o=(Object *)calloc(1,sizeof(*o)); assert(o); o->kind=kind; o->id=++x->next_id; o->refs=1; o->entry=entry; o->capacity=capacity;
    if (kind==1) { o->data=(unsigned char *)calloc(capacity?capacity:1,entry); assert(o->data); }
    ++x->live; return o;
}
static ThreeMMTextGenerated *state(void *p,void *owner) { assert(p==owner); return &t(p)->generated; }
static ThreeMMI32 length(void *p,void *owner) { assert(p==owner); return t(p)->length; }
static ThreeMMI32 character(void *p,void *owner,ThreeMMI32 i) { assert(p==owner && i>=0 && i<t(p)->length); return t(p)->chars[i]; }
static ThreeMMI32 shape(void *p,void *owner) { assert(p==owner); return t(p)->shape; }
static void template_read(void *p,void *owner,ThreeMMTemplateState *out) { assert(p==owner); *out=t(p)->template_state; }
static void template_write(void *p,void *owner,const ThreeMMTemplateState *in) { assert(p==owner); ++t(p)->published; t(p)->template_state=*in; }
static void *list_new(void *p,ThreeMMI32 entry,ThreeMMI32 capacity)
{ if (checkpoint(t(p))) return NULL; assert(entry==2 || entry==48); return object(t(p),1,entry,capacity); }
static void list_count(void *p,void *list,ThreeMMI32 n) { Object *o=(Object *)list; (void)p; assert(o->kind==1 && n<=o->capacity); o->count=n; }
static void list_put(void *p,void *list,ThreeMMI32 i,const void *data)
{ Object *o=(Object *)list; (void)p; assert(o->kind==1 && i>=0 && i<o->count); memcpy(o->data+i*o->entry,data,o->entry); }
static void *group_new(void *p,ThreeMMI32 fixed,ThreeMMI32 rows,ThreeMMI32 bytes)
{ if (checkpoint(t(p))) return NULL; assert((fixed==4 && rows==1 && bytes==4) || (fixed==8 && !rows && !bytes)); return object(t(p),2,fixed,0); }
static int group_add(void *p,void *group,const void *fixed,ThreeMMI32 bytes,const void *variable)
{
    Object *o=(Object *)group; assert(o->kind==2); if (checkpoint(t(p))) return 0;
    o->stride=o->entry+bytes; o->data=(unsigned char *)realloc(o->data,(o->count+1)*o->stride); assert(o->data);
    memcpy(o->data+o->count*o->stride,fixed,o->entry); if (bytes) memcpy(o->data+o->count*o->stride+o->entry,variable,bytes); ++o->count; return 1;
}
static int allocate(void *p,ThreeMMI32 bytes,void **out) { if (checkpoint(t(p))) return 0; *out=calloc(bytes?bytes:1,1); assert(*out); ++t(p)->buffers; return 1; }
static void free_memory(void *p,void **out) { if (*out) { free(*out); *out=NULL; --t(p)->buffers; } }
static void *material_new(void *p) { if (checkpoint(t(p))) return NULL; return object(t(p),3,0,0); }
static void retain(void *p,void *v) { Object *o=(Object *)v; assert(o->refs>0); if (v==t(p)->generated.action) t(p)->last_retain_id=t(p)->generated.action_id; ++o->refs; t(p)->events[t(p)->event_count++]=o->id; }
static void release(void *p,void *v)
{ Object *o=(Object *)v; assert(o->refs>0); if (v==t(p)->generated.action) t(p)->last_release_id=t(p)->generated.action_id; t(p)->events[t(p)->event_count++]=-o->id; if (!--o->refs) { free(o->data); free(o); --t(p)->live; } }
static void *font(void *p,void *owner)
{ assert(owner==p); if (t(p)->missing_font) return NULL; retain(p,t(p)->font); return t(p)->font; }
static ThreeMMI32 font_width(void *p,void *f,ThreeMMI32 ch) { assert(f==t(p)->font); return (ch%4+1)*65536L+1; }
static ThreeMMI32 font_height(void *p,void *f,ThreeMMI32 ch) { assert(f==t(p)->font); return (ch%3+1)*65536L+3; }
static ThreeMMI32 font_maximum(void *p,void *f) { assert(f==t(p)->font); return 262144L; }
static void *glyph(void *p,void *f,ThreeMMI32 ch) { assert(f==t(p)->font && ch==233); return t(p)->missing_glyph?NULL:object(t(p),6,0,0); }
static void *action_new(void *p,void *cels,void *matrices,ThreeMMU32 flags)
{
    Object *c=(Object *)cels,*m=(Object *)matrices; int i,j; short part[2]; ThreeMMI32 fixed[2];
    assert(c->kind==2 && m->kind==1 && m->entry==48 && m->count==c->count*t(p)->length && flags==10);
    for (i=0;i<c->count;++i) { memcpy(fixed,c->data+i*c->stride,8); assert(!fixed[0] && fixed[1]==327680L);
        for (j=0;j<t(p)->length;++j) { memcpy(part,c->data+i*c->stride+8+j*4,4); assert(part[0]==j && part[1]==i*t(p)->length+j); } }
    if (checkpoint(t(p))) return NULL; return object(t(p),4,0,0);
}
static int body_shape(void *p,void *body,void *parents,void *sets)
{ assert(p==body && parents==t(p)->template_state.parents && sets==t(p)->template_state.sets); return !t(p)->body_fail; }
static void body_material(void *p,void *body,ThreeMMI32 set,void *material) { assert(p==body && !set && material==t(p)->generated.material); }
static ThreeMMTextGenerationServices setup(Test *x)
{
    ThreeMMTextGenerationServices s; memset(x,0,sizeof(*x)); memset(&s,0,sizeof(s)); x->length=2; x->chars[0]=233; x->chars[1]=65;
    x->template_state.flags=4; x->template_state.x=1234; x->font=object(x,5,0,0);
    s.context=x; s.state=state; s.length=length; s.character=character; s.shape=shape; s.template_read=template_read; s.template_write=template_write;
    s.list_new=list_new; s.list_count=list_count; s.list_put=list_put; s.group_new=group_new; s.group_add=group_add; s.allocate=allocate; s.free_memory=free_memory;
    s.material_new=material_new; s.retain=retain; s.release=release; s.font=font; s.font_width=font_width; s.font_height=font_height; s.font_maximum=font_maximum;
    s.glyph=glyph; s.action_new=action_new; s.body_shape=body_shape; s.body_material=body_material; x->services=s; return s;
}
static void cleanup(Test *x)
{
    ThreeMMTextGenerationServices *s=&x->services; three_mm_text_dispose(&x->generated,s);
    if (x->template_state.parents) release(x,x->template_state.parents);
    if (x->template_state.sets) release(x,x->template_state.sets);
    if (x->template_state.costumes) release(x,x->template_state.costumes);
    release(x,x->font); assert(!x->live && !x->buffers);
}
int main(void)
{
    Test x; ThreeMMTextGenerationServices s; int fail,i,ok,ids[5],before; Object *old,*a,*again,*list,*group,*model; ThreeMMTemplateState saved; short indices[2]; ThreeMMI32 row[2]; ThreeMMActorMatrix m;
    const int counts[]={1,12,12,12,12,12,12,12,24,24,24,24,24,24,12,12};
    for (i=0;i<16;++i) assert(three_mm_text_cel_count(i)==counts[i]); assert(!three_mm_text_cel_count(-1) && !three_mm_text_cel_count(16));
    for (fail=0;fail<=5;++fail)
    {
        s=setup(&x); x.fail=fail; ok=three_mm_text_initialize_lists(&x,&s); assert(ok==!fail);
        if (!ok) assert(!x.published && !x.generated.material && !x.template_state.parents && x.template_state.flags==4);
        else { assert(x.generated.action_id==-1 && x.template_state.flags==6 && x.template_state.x==1234 && x.template_state.set_count==1 && x.template_state.action_count==16);
            list=(Object *)x.template_state.parents; memcpy(indices,list->data,4); assert(indices[0]==-1 && indices[1]==-1);
            list=(Object *)x.template_state.sets; memcpy(indices,list->data,4); assert(!indices[0] && !indices[1]);
            group=(Object *)x.template_state.costumes; memcpy(row,group->data,8); assert(row[0]==1 && !row[1]); }
        cleanup(&x);
    }
    for (fail=0;fail<=4;++fail)
    {
        s=setup(&x); assert(three_mm_text_initialize_lists(&x,&s)); x.generated.action=object(&x,4,0,0); x.generated.action_id=7;
        saved=x.template_state; old=(Object *)x.generated.material;
        ids[0]=((Object *)saved.parents)->id; ids[1]=((Object *)saved.sets)->id; ids[2]=((Object *)saved.costumes)->id; ids[3]=old->id; ids[4]=((Object *)x.generated.action)->id;
        x.phase=0; x.fail=fail; x.published=0; x.event_count=0; ok=three_mm_text_initialize_lists(&x,&s); assert(ok==!fail);
        assert(x.generated.material==old && old->refs==1);
        if (!ok) assert(!x.published && x.template_state.parents==saved.parents && x.template_state.sets==saved.sets && x.template_state.costumes==saved.costumes && x.generated.action && x.generated.action_id==7);
        else { assert(!x.generated.action && x.generated.action_id==-1 && x.events[0]==ids[3]); for (i=0;i<5;++i) assert(x.events[i+1]==-ids[i]); }
        cleanup(&x);
    }
    for (fail=0;fail<=28;++fail)
    {
        s=setup(&x); assert(three_mm_text_initialize_lists(&x,&s)); old=object(&x,4,0,0); x.generated.action=old; x.generated.action_id=7; retain(&x,old);
        x.phase=0; x.fail=fail; a=(Object *)three_mm_text_fetch_action(&x,THREE_MM_TEXT_WAVE,&s); assert((a!=NULL)==!fail && old->refs==1 && !x.buffers && x.font->refs==1 && x.last_release_id==7);
        if (!a) assert(!x.generated.action && x.generated.action_id==-1);
        else { assert(a->refs==2 && x.generated.action_id==THREE_MM_TEXT_WAVE && x.last_retain_id==-1); before=x.phase; again=(Object *)three_mm_text_fetch_action(&x,THREE_MM_TEXT_WAVE,&s); assert(again==a && a->refs==3 && x.phase==before && x.last_retain_id==THREE_MM_TEXT_WAVE); release(&x,again); release(&x,a); }
        release(&x,old); cleanup(&x);
    }
    s=setup(&x); x.missing_font=1; assert(!three_mm_text_build_matrices(&x,0,&s) && !x.phase && x.font->refs==1); model=(Object *)three_mm_text_fetch_model(&x,0,&s); assert(!model); cleanup(&x);
    s=setup(&x); model=(Object *)three_mm_text_fetch_model(&x,0,&s); assert(model && model->refs==1 && x.font->refs==1); release(&x,model); x.missing_glyph=1; assert(!three_mm_text_fetch_model(&x,0,&s) && x.font->refs==1); cleanup(&x);
    s=setup(&x); assert(three_mm_text_initialize_lists(&x,&s)); assert(three_mm_text_adjust_body(&x,&x,&s) && three_mm_text_default_costume(&x,&x,&s)); x.body_fail=1; assert(!three_mm_text_adjust_body(&x,&x,&s)); cleanup(&x);
    s=setup(&x); group=(Object *)three_mm_text_build_cels(&x,THREE_MM_TEXT_WALK,&s); memcpy(row,group->data,8); assert(group->count==24 && !row[0] && row[1]==65536); release(&x,group); x.length=0; list=(Object *)three_mm_text_build_matrices(&x,0,&s); assert(list && !list->count); release(&x,list); cleanup(&x);
    three_mm_matrix_identity(&m); three_mm_text_apply_shape(&m,THREE_MM_TEXT_NORMAL,1,0,-3,65536,0,65536,65536); assert(m.m[3][0]==-3);
    three_mm_matrix_identity(&m); three_mm_text_apply_action(&m,THREE_MM_TEXT_LETTER_ROT_X,0,12,0,0,65536); assert(m.m[0][0]==65536 && m.m[1][1]==65534 && m.m[2][2]==65534);
    three_mm_matrix_identity(&m); three_mm_text_apply_shape(&m,THREE_MM_TEXT_VERTICAL,2,1,0,65536,65537,262144,196609); assert(m.m[3][1]==131072);
    three_mm_matrix_identity(&m); three_mm_text_apply_action(&m,THREE_MM_TEXT_WALK,1,24,0,0,65536); assert(m.m[3][2]==786432);
    return 0;
}
