#include "mm/text_generation.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>
typedef struct Object { int kind,id,refs; MMActionDefinition action; } Object;
typedef struct Test
{
    MMTextGenerated generated; MMTemplateState template_state;
    int length,shape,phase,fail,live,buffers,next_id,events[128],event_count,missing_font,missing_glyph,body_fail,last_retain_id,last_release_id;
    unsigned char chars[2]; Object *font; MMTextGenerationServices services;
} Test;
static Test *t(void *p) { return (Test *)p; }
/* Compare defined fields: struct padding is not part of the owner contract. */
static int same_template(const MMTemplateState *a,const MMTemplateState *b)
{
    return a->parents==b->parents && a->sets==b->sets && a->costumes==b->costumes &&
        a->set_count==b->set_count && a->custom_count==b->custom_count && a->action_count==b->action_count &&
        a->x==b->x && a->y==b->y && a->z==b->z && a->flags==b->flags;
}
static int checkpoint(Test *x) { return ++x->phase==x->fail; }
static Object *object(Test *x,int kind)
{
    Object *o=(Object *)calloc(1,sizeof(*o)); assert(o); o->kind=kind; o->id=++x->next_id; o->refs=1;
    ++x->live; return o;
}
static MMTextGenerated *state(void *p,void *owner) { assert(p==owner); return &t(p)->generated; }
static MMI32 length(void *p,void *owner) { assert(p==owner); return t(p)->length; }
static MMI32 character(void *p,void *owner,MMI32 i) { assert(p==owner && i>=0 && i<t(p)->length); return t(p)->chars[i]; }
static MMI32 shape(void *p,void *owner) { assert(p==owner); return t(p)->shape; }
static MMTemplateState *template_state(void *p,void *o) { assert(p==o); return &t(p)->template_state; }
static void *resize_table(void *p,void *data,MMU32 old,MMU32 bytes,int priority)
{
    Test *x=t(p);
    void *result;
    (void)priority;
    /* Keep a storage fault active through the allocator's exact-size retry. */
    ++x->phase;
    if (x->fail && x->phase>=x->fail) return NULL;
    result=realloc(data,bytes); assert(result);
    if (!old) ++x->buffers;
    return result;
}
static void free_table(void *p,void *data,MMU32 bytes)
{ (void)bytes; assert(data && t(p)->buffers>0); free(data); --t(p)->buffers; }
typedef struct TestActionTable { MMActionTable table; Test *context; } TestActionTable;
static void release_table(void *p)
{
    TestActionTable *owner=p;
    assert(owner->context->live>0);
    --owner->context->live;
    free(owner);
}
static MMActionTable *table_new(void *p)
{
    TestActionTable *owner;
    MMStorageAllocator allocator={NULL,resize_table,free_table};
    if (checkpoint(t(p))) return NULL;
    owner=(TestActionTable *)calloc(1,sizeof(*owner)); assert(owner);
    owner->context=t(p);
    ++owner->context->live;
    allocator.context=p;
    mm_action_table_init(&owner->table,&allocator,owner,release_table);
    return &owner->table;
}
static void *material_new(void *p) { if (checkpoint(t(p))) return NULL; return object(t(p),3); }
static void retain(void *p,void *v) { Object *o=(Object *)v; assert(o->refs>0); if (v==t(p)->generated.action) t(p)->last_retain_id=t(p)->generated.action_id; ++o->refs; t(p)->events[t(p)->event_count++]=o->id; }
static void release(void *p,void *v)
{ Object *o=(Object *)v; assert(o->refs>0); if (v==t(p)->generated.action) t(p)->last_release_id=t(p)->generated.action_id; t(p)->events[t(p)->event_count++]=-o->id; if (!--o->refs) { if (o->kind==4) mm_action_dispose(&o->action); free(o); --t(p)->live; } }
typedef struct TestIndices
{
    MMPartIndexList list;
    Test *context;
    Object *object;
} TestIndices;
static void release_indices(void *p)
{
    TestIndices *owner = (TestIndices *)p;
    release(owner->context, owner->object);
    free(owner);
}
static MMPartIndexList *indices_new(void *p)
{
    Test *x = (Test *)p;
    TestIndices *owner;
    if (checkpoint(x))
        return NULL;
    owner = (TestIndices *)calloc(1, sizeof(*owner));
    assert(owner);
    owner->context = x;
    owner->object = object(x, 0);
    mm_part_indices_init(&owner->list, NULL, owner, release_indices);
    return &owner->list;
}
typedef struct TestCostume
{
    MMTemplateCostumeTable table;
    Test *context;
    Object *object;
} TestCostume;
static void release_costume(void *p)
{
    TestCostume *owner = (TestCostume *)p;
    release(owner->context, owner->object);
    free(owner);
}
static MMTemplateCostumeTable *costumes_new(void *p)
{
    Test *x = (Test *)p;
    TestCostume *owner;
    if (checkpoint(x))
        return NULL;
    owner = (TestCostume *)calloc(1, sizeof(*owner));
    assert(owner);
    owner->context = x;
    owner->object = object(x, 2);
    mm_template_costumes_init(&owner->table, NULL, owner, release_costume);
    if (checkpoint(x))
    {
        mm_template_costumes_dispose(&owner->table);
        return NULL;
    }
    return &owner->table;
}
static void *font(void *p,void *owner)
{ assert(owner==p); if (t(p)->missing_font) return NULL; retain(p,t(p)->font); return t(p)->font; }
static MMI32 font_width(void *p,void *f,MMI32 ch) { assert(f==t(p)->font); return (ch%4+1)*65536L+1; }
static MMI32 font_height(void *p,void *f,MMI32 ch) { assert(f==t(p)->font); return (ch%3+1)*65536L+3; }
static MMI32 font_maximum(void *p,void *f) { assert(f==t(p)->font); return 262144L; }
static void *glyph(void *p,void *f,MMI32 ch) { assert(f==t(p)->font && ch==233); return t(p)->missing_glyph?NULL:object(t(p),6); }
static void *action_new(void *p,MMActionTable *cels,MMActionTable *matrices,MMU32 flags)
{
    MMActionDefinition view={NULL,NULL,NULL,0};
    Object *action;
    int i,j,count;
    short part[2];
    MMI32 fixed[2];
    view.cels=cels; view.matrices=matrices; view.flags=flags;
    count=mm_action_cel_count(&view);
    assert(matrices->header.count==(MMU32)(count*t(p)->length) && flags==10);
    for (i=0;i<count;++i)
    {
        assert(mm_action_get_cel(&view,i,fixed) && !fixed[0] && fixed[1]==327680L);
        for (j=0;j<t(p)->length;++j)
            assert(mm_action_get_part(&view,i,j,part) && part[0]==j && part[1]==i*t(p)->length+j);
    }
    if (checkpoint(t(p))) return NULL;
    action=object(t(p),4);
    action->action=view;
    return action;
}
static int body_shape(void *p,void *body,const MMPartIndexView *parents,const MMPartIndexView *sets)
{ assert(p==body && parents==&t(p)->template_state.parents->view && sets==&t(p)->template_state.sets->view); return !t(p)->body_fail; }
static void body_material(void *p,void *body,MMI32 set,void *material) { assert(p==body && !set && material==t(p)->generated.material); }
static MMTextGenerationServices setup(Test *x)
{
    MMTextGenerationServices s; memset(x,0,sizeof(*x)); memset(&s,0,sizeof(s)); x->length=2; x->chars[0]=233; x->chars[1]=65;
    x->template_state.flags=4; x->template_state.x=1234; x->font=object(x,5);
    s.context=x; s.state=state; s.length=length; s.character=character; s.shape=shape; s.template_state=template_state;
    s.indices_new=indices_new; s.costumes_new=costumes_new; s.table_new=table_new;
    s.material_new=material_new; s.retain=retain; s.release=release; s.font=font; s.font_width=font_width; s.font_height=font_height; s.font_maximum=font_maximum;
    s.glyph=glyph; s.action_new=action_new; s.body_shape=body_shape; s.body_material=body_material; x->services=s; return s;
}
static void cleanup(Test *x)
{
    MMTextGenerationServices *s=&x->services; mm_text_dispose(&x->generated,s);
    mm_part_indices_dispose(x->template_state.parents);
    mm_part_indices_dispose(x->template_state.sets);
    mm_template_costumes_dispose(x->template_state.costumes);
    release(x,x->font); assert(!x->live && !x->buffers);
}
typedef struct MatrixCapture { MMActorMatrix matrices[2]; int count; } MatrixCapture;
static void capture_matrix(void *p,MMI32 index,const MMActorMatrix *matrix)
{
    MatrixCapture *capture=p;
    assert(index==capture->count && index<2);
    capture->matrices[capture->count++]=*matrix;
}
int main(void)
{
    Test x; MMTextGenerationServices s; int fail,i,ok,ids[5],before,steps=0,shape_id,cel;
    Object *old,*a,*again,*model;
    MMActionTable *table;
    MMActionDefinition view={NULL,NULL,NULL,0};
    MatrixCapture capture;
    MMTemplateState saved; MMI32 row[2]; MMActorMatrix m;
    const int counts[]={1,12,12,12,12,12,12,12,24,24,24,24,24,24,12,12};
    for (i=0;i<16;++i) assert(mm_text_cel_count(i)==counts[i]); assert(!mm_text_cel_count(-1) && !mm_text_cel_count(16));
    for (fail=0;fail<=5;++fail)
    {
        s=setup(&x); saved=x.template_state; x.fail=fail; ok=mm_text_initialize_lists(&x,&s); assert(ok==!fail);
        if (!ok) assert(same_template(&x.template_state,&saved) && !x.generated.material && !x.template_state.parents && x.template_state.flags==4);
        else { assert(x.generated.action_id==-1 && x.template_state.flags==6 && x.template_state.x==1234 && x.template_state.set_count==1 && x.template_state.action_count==16);
            assert(x.template_state.parents->view.count==2 && x.template_state.sets->view.count==2);
            for (i=0;i<2;++i) { assert(mm_part_index_get(&x.template_state.parents->view,i)==-1); assert(mm_part_index_get(&x.template_state.sets->view,i)==0); }
            assert(mm_template_costumes_count(x.template_state.costumes,0)==1 && mm_template_costumes_id(x.template_state.costumes,0,0,&row[1]) && !row[1]); }
        cleanup(&x);
    }
    for (fail=0;fail<=4;++fail)
    {
        s=setup(&x); assert(mm_text_initialize_lists(&x,&s)); x.generated.action=object(&x,4); x.generated.action_id=7;
        saved=x.template_state; old=(Object *)x.generated.material;
        ids[0]=((TestIndices *)saved.parents->owner)->object->id; ids[1]=((TestIndices *)saved.sets->owner)->object->id; ids[2]=((TestCostume *)saved.costumes->owner)->object->id; ids[3]=old->id; ids[4]=((Object *)x.generated.action)->id;
        x.phase=0; x.fail=fail; x.event_count=0; ok=mm_text_initialize_lists(&x,&s); assert(ok==!fail);
        assert(x.generated.material==old && old->refs==1);
        if (!ok) assert(same_template(&x.template_state,&saved) && x.template_state.parents==saved.parents && x.template_state.sets==saved.sets && x.template_state.costumes==saved.costumes && x.generated.action && x.generated.action_id==7);
        else { assert(!x.generated.action && x.generated.action_id==-1 && x.events[0]==ids[3]); for (i=0;i<5;++i) assert(x.events[i+1]==-ids[i]); }
        cleanup(&x);
    }
    for (fail=0;fail<=steps;++fail)
    {
        s=setup(&x); assert(mm_text_initialize_lists(&x,&s)); old=object(&x,4); x.generated.action=old; x.generated.action_id=7; retain(&x,old);
        x.phase=0; x.fail=fail; a=(Object *)mm_text_fetch_action(&x,MM_TEXT_WAVE,&s); assert((a!=NULL)==!fail && old->refs==1 && x.font->refs==1 && x.last_release_id==7);
        if (!a) assert(!x.generated.action && x.generated.action_id==-1 && !x.buffers);
        else { steps=x.phase; assert(a->refs==2 && x.buffers==3 && x.generated.action_id==MM_TEXT_WAVE && x.last_retain_id==-1); before=x.phase; again=(Object *)mm_text_fetch_action(&x,MM_TEXT_WAVE,&s); assert(again==a && a->refs==3 && x.phase==before && x.last_retain_id==MM_TEXT_WAVE); release(&x,again); release(&x,a); }
        release(&x,old); cleanup(&x);
    }
    s=setup(&x); x.missing_font=1; assert(!mm_text_build_matrices(&x,0,&s) && !x.phase && x.font->refs==1); model=(Object *)mm_text_fetch_model(&x,0,&s); assert(!model); cleanup(&x);
    s=setup(&x); model=(Object *)mm_text_fetch_model(&x,0,&s); assert(model && model->refs==1 && x.font->refs==1); release(&x,model); x.missing_glyph=1; assert(!mm_text_fetch_model(&x,0,&s) && x.font->refs==1); cleanup(&x);
    s=setup(&x); assert(mm_text_initialize_lists(&x,&s)); assert(mm_text_adjust_body(&x,&x,&s) && mm_text_default_costume(&x,&x,&s)); x.body_fail=1; assert(!mm_text_adjust_body(&x,&x,&s)); cleanup(&x);
    s=setup(&x);
    view.cels=mm_text_build_cels(&x,MM_TEXT_WALK,&s);
    assert(mm_action_cel_count(&view)==24 && mm_action_get_cel(&view,0,row) && !row[0] && row[1]==65536);
    mm_action_table_dispose(view.cels); view.cels=NULL;
    x.length=0; table=mm_text_build_matrices(&x,0,&s);
    assert(table && !table->header.count); mm_action_table_dispose(table); cleanup(&x);
    for (shape_id=0;shape_id<MM_TEXT_SHAPE_COUNT;++shape_id)
        for (i=0;i<MM_TEXT_ACTION_COUNT;++i)
        {
            int letter;
            s=setup(&x); x.shape=shape_id; cel=mm_text_cel_count(i)/2;
            view.matrices=mm_text_build_matrices(&x,i,&s); assert(view.matrices);
            before=x.phase; capture.count=0;
            assert(mm_text_evaluate_cel(&x,i,cel,capture_matrix,&capture,&s) && capture.count==2);
            assert(x.phase==before && x.font->refs==1);
            for (letter=0;letter<2;++letter)
                assert(mm_action_get_matrix(&view,2*cel+letter,&m) && !memcmp(&m,&capture.matrices[letter],sizeof(m)));
            assert(!mm_text_evaluate_cel(&x,i,mm_text_cel_count(i),capture_matrix,&capture,&s));
            mm_action_table_dispose(view.matrices); cleanup(&x);
        }
    mm_matrix_identity(&m); mm_text_apply_shape(&m,MM_TEXT_NORMAL,1,0,-3,65536,0,65536,65536); assert(m.m[3][0]==-3);
    mm_matrix_identity(&m); mm_text_apply_action(&m,MM_TEXT_LETTER_ROT_X,0,12,0,0,65536); assert(m.m[0][0]==65536 && m.m[1][1]==65534 && m.m[2][2]==65534);
    mm_matrix_identity(&m); mm_text_apply_shape(&m,MM_TEXT_VERTICAL,2,1,0,65536,65537,262144,196609); assert(m.m[3][1]==131072);
    mm_matrix_identity(&m); mm_text_apply_action(&m,MM_TEXT_WALK,1,24,0,0,65536); assert(m.m[3][2]==786432);
    return 0;
}
