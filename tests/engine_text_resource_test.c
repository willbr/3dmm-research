#include "mm/text_resource.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>
typedef struct Root { int refs,object; } Root;
typedef struct Object
{
    Root root; MMTextDescriptor descriptor; MMTextGenerated generated;
    MMTemplateState template_state; char text[256]; int provider;
} Object;
typedef struct Context
{
    MMTextResourceServices services; char saved[256]; unsigned char wire[24];
    MMI32 bytes; int fail,step,live,header,child,found,unpacked,appended,deleted;
    int error,released_error,bad,warnings,custom,defaults,names_published;
    MMTextNames *names;
} Context;
static void copy_string(char *to,const char *from) { memcpy(to,from,strlen(from)+1); }
static void *allocate(Context *c,size_t n,int object)
{ Root *r; if (++c->step==c->fail) return NULL; r=(Root *)calloc(1,n); assert(r); r->refs=1; r->object=object; ++c->live; return r; }
static void retain(void *p,void *v)
{ Context *c=(Context *)p; if (c->names && c->names->table==v) c->names_published=1; ++((Root *)v)->refs; }
static void release(void *p,void *v)
{
    Context *c=(Context *)p; Root *r=(Root *)v; Object *o;
    if (--r->refs) return;
    c->released_error=c->error;
    if (r->object)
    {
        o=(Object *)v; mm_text_dispose(&o->generated,&c->services.generation);
        mm_part_indices_dispose(o->template_state.parents);
        mm_part_indices_dispose(o->template_state.sets);
        mm_template_costumes_dispose(o->template_state.costumes);
    }
    --c->live; free(v);
}
static void *create(void *p) { return allocate((Context *)p,sizeof(Object),1); }
static MMTextDescriptor *state(void *p,void *o) { (void)p; return &((Object *)o)->descriptor; }
static MMTextGenerated *generated(void *p,void *o) { (void)p; return &((Object *)o)->generated; }
static void *text(void *p,void *o) { (void)p; return ((Object *)o)->text; }
static MMI32 length(void *p,void *o) { (void)p; return (MMI32)strlen(((Object *)o)->text); }
static void assign(void *p,void *to,const void *from) { (void)p; copy_string((char *)to,(const char *)from); }
static void space(void *p,void *t) { (void)p; copy_string((char *)t," "); }
static void clear(void *p,void *t) { (void)p; *(char *)t=0; }
static void *saved(void *p) { return ((Context *)p)->saved; }
static void provider_copy(void *p,void *to,void *from) { (void)p; ((Object *)to)->provider=((Object *)from)->provider; }
static int has_provider(void *p,void *o) { (void)p; return ((Object *)o)->provider; }
static void *provider(void *p,void *o,const MMTag *t) { (void)o; (void)t; ++((Context *)p)->custom; return NULL; }
static void *default_font(void *p,const MMTag *t) { (void)t; ++((Context *)p)->defaults; return p; }
static void publish_tag(void *p,void *out,const MMTag *t) { (void)p; memcpy(out,t,sizeof(*t)); }
static MMTemplateState *template_state(void *p,void *o) { (void)p; return &((Object *)o)->template_state; }
static void *list_new(void *p,MMI32 bytes,MMI32 count) { (void)bytes; (void)count; return allocate((Context *)p,sizeof(Root),0); }
typedef struct IndicesOwner
{
    Root root;
    MMPartIndexList list;
    Context *context;
} IndicesOwner;
static void release_indices(void *p)
{
    IndicesOwner *owner = (IndicesOwner *)p;
    release(owner->context, owner);
}
static MMPartIndexList *indices_new(void *p)
{
    Context *c = (Context *)p;
    IndicesOwner *owner = (IndicesOwner *)allocate(c, sizeof(*owner), 0);
    if (!owner)
        return NULL;
    owner->context = c;
    mm_part_indices_init(&owner->list, NULL, owner, release_indices);
    return &owner->list;
}
typedef struct CostumeOwner
{
    Root root;
    MMTemplateCostumeTable table;
    Context *context;
} CostumeOwner;
static void release_costume(void *p)
{
    CostumeOwner *owner = (CostumeOwner *)p;
    release(owner->context, owner);
}
static MMTemplateCostumeTable *costumes_new(void *p)
{
    Context *c = (Context *)p;
    CostumeOwner *owner = (CostumeOwner *)allocate(c, sizeof(*owner), 0);
    if (!owner)
        return NULL;
    owner->context = c;
    mm_template_costumes_init(&owner->table, NULL, owner, release_costume);
    if (++c->step == c->fail)
    {
        mm_template_costumes_dispose(&owner->table);
        return NULL;
    }
    return &owner->table;
}
static void *material_new(void *p) { return list_new(p,0,0); }
static int read_template(void *p,void *o,void *file,MMU32 type,MMU32 n,void *block)
{ Context *c=(Context *)p; assert(file==p && block==p); (void)type; (void)n; copy_string(((Object *)o)->text,"loaded"); return c->header; }
static int write_template(void *p,void *o,void *file,MMU32 type,void *out)
{ MMU32 n=37; (void)o; (void)file; (void)type; memcpy(out,&n,4); return ((Context *)p)->header; }
static int child(void *p,void *file,MMU32 type,MMU32 n,MMU32 *out)
{ (void)file; (void)type; assert(n==37); *out=19; return ((Context *)p)->child; }
static void *find(void *p,void *file,MMU32 n) { (void)file; assert(n==19); return ((Context *)p)->found?p:NULL; }
static int unpack(void *p,void *b) { assert(p==b); return ((Context *)p)->unpacked; }
static MMI32 block_size(void *p,void *b) { (void)b; return ((Context *)p)->bytes; }
static int read_block(void *p,void *b,void *out,MMI32 n) { (void)b; assert(n==24); memcpy(out,((Context *)p)->wire,24); return 1; }
static void *add(void *p,void *file,MMU32 type,MMU32 n,MMU32 *out) { (void)file; (void)type; assert(n==37); *out=19; return ((Context *)p)->child?p:NULL; }
static int write_block(void *p,void *b,const void *in,MMI32 n) { (void)b; assert(n==24); memcpy(((Context *)p)->wire,in,24); return ((Context *)p)->found; }
static void delete_child(void *p,void *file,MMU32 type,MMU32 n,MMU32 kid) { (void)file; (void)type; assert(n==37 && kid==19); ++((Context *)p)->deleted; }
static void publish_error(void *p,void *out,int v) { ((Context *)p)->error=v; *(int *)out=v; }
static void *tag_list(void *p) { assert(!((Context *)p)->error); return list_new(p,0,0); }
static int append_tag(void *p,void *list,const MMTag *t) { (void)list; assert(!t->cache); ++((Context *)p)->appended; return ((Context *)p)->child!=2; }
static MMI32 name_count(void *p,void *t) { (void)p; (void)t; return 3; }
static MMI32 name_id(void *p,void *t,MMI32 i) { (void)p; (void)t; return i==1?0:7; }
static void name_text(void *p,void *t,MMI32 i,void *out) { (void)p; (void)t; copy_string((char *)out,i==0?"Pulse":i==1?"Rest":"duplicate"); }
static void missing(void *p) { ++((Context *)p)->warnings; }
static void bad(void *p) { ++((Context *)p)->bad; }
static void init(Context *c)
{
    MMTextResourceServices *s; MMTextGenerationServices *g;
    memset(c,0,sizeof(*c)); c->bytes=24; c->header=c->child=c->found=c->unpacked=1; s=&c->services; g=&s->generation;
    s->context=c; g->context=c; g->state=generated; g->length=length; g->template_state=template_state;
    g->indices_new=indices_new; g->costumes_new=costumes_new; g->material_new=material_new; g->retain=retain; g->release=release;
    s->state=state; s->create=create; s->text=text; s->assign=assign; s->space=space; s->clear=clear; s->saved_text=saved; s->provider_copy=provider_copy;
    s->has_provider=has_provider; s->provider_font=provider; s->default_font=default_font; s->publish_tag=publish_tag; s->read_template=read_template; s->write_template=write_template;
    s->child=child; s->find=find; s->unpack=unpack; s->block_size=block_size; s->read=read_block; s->add=add; s->write=write_block; s->delete_child=delete_child;
    s->publish_error=publish_error; s->tag_list=tag_list; s->append_tag=append_tag; s->name_count=name_count; s->name_id=name_id; s->name_text=name_text; s->missing_name=missing; s->bad_order=bad;
}
static void lifecycle(void)
{
    Context c; Object *o,*copy; MMTag font={123,NULL,0x54444620U,29},out; MMI32 shape; char value[256]; int failure;
    font.cache=&font;
    for (failure=0;failure<=6;++failure)
    {
        init(&c); c.fail=failure; o=(Object *)mm_text_create("",9,&font,&c.services);
        assert((o!=NULL)==(failure==0)); if (o) { assert(!strcmp(o->text," ")); release(&c,o); } assert(!c.live);
    }
    init(&c); o=(Object *)mm_text_create("old",9,&font,&c.services); assert(o); o->provider=1;
    assert(!mm_text_font(o,&c.services) && c.custom==1 && !c.defaults);
    o->provider=0; assert(mm_text_font(o,&c.services)==&c && c.defaults==1); o->provider=1;
    for (failure=1;failure<=4;++failure)
    {
        void *parents=o->template_state.parents; c.step=0; c.fail=failure;
        assert(!mm_text_change(o,"",0,NULL,&c.services));
        assert(!strcmp(o->text,"old") && o->descriptor.shape==9 && o->descriptor.font.cache==font.cache && o->template_state.parents==parents);
    }
    c.fail=0; assert(mm_text_change(o,"",0,NULL,&c.services) && !strcmp(o->text," "));
    o->generated.action=list_new(&c,0,0); o->generated.action_id=7;
    assert(mm_text_change(o,NULL,-1,NULL,&c.services) && !o->generated.action && o->generated.action_id==-1);
    copy=(Object *)mm_text_duplicate(o,&c.services); assert(copy && copy->provider && copy->generated.material!=o->generated.material);
    mm_text_info(copy,value,&shape,&out,&c.services); assert(!strcmp(value," ") && !shape && out.cache==font.cache && out.sid==123);
    mm_text_info(copy,NULL,NULL,NULL,&c.services); release(&c,copy); release(&c,o); assert(!c.live);
}
static void wire(void)
{
    Context c; Object *o,*loaded; void *tags; MMTag font={-1,NULL,0xfedcba98U,0x87654321U}; MMU32 number; int endian,failure,error;
    for (endian=0;endian<2;++endian)
    {
        init(&c); c.services.host_big_endian=endian; c.services.system=0x1234; o=(Object *)mm_text_create("saved",9,&font,&c.services); assert(o);
        assert(mm_text_write(o,&c,1,&number,&c.services) && number==37);
        assert(c.wire[endian?1:0]==1 && c.wire[2]==(endian?0x12:0x34) && c.wire[3]==(endian?0x34:0x12));
        assert(!c.wire[12] && !c.wire[13] && !c.wire[14] && !c.wire[15]);
        for (failure=0;failure<2;++failure)
        {
            loaded=(Object *)create(&c); c.services.host_big_endian=endian^failure;
            assert(mm_text_read(loaded,&c,1,37,&c,&c.services));
            assert(loaded->descriptor.shape==9 && loaded->descriptor.font.sid==-1 && loaded->descriptor.font.type==font.type && loaded->descriptor.font.number==font.number && !loaded->descriptor.font.cache);
            release(&c,loaded);
        }
        for (failure=0;failure<7;++failure)
        {
            c.step=0; c.fail=failure==1?1:0; c.child=failure==2?0:failure==6?2:1; c.found=failure!=3; c.unpacked=failure!=4; c.bytes=failure==5?23:100;
            error=-1; c.released_error=-1; tags=mm_text_tags(&c,1,37,&error,&c.services);
            assert((tags!=NULL)==!failure && error==(failure!=0));
            if (tags) release(&c,tags); else if (failure!=1) assert(c.released_error==1);
        }
        c.fail=0; c.bytes=24; c.child=0; c.found=1;
        assert(!mm_text_write(o,&c,1,&number,&c.services) && number==37 && !c.deleted);
        c.child=1; c.found=0; assert(!mm_text_write(o,&c,1,&number,&c.services) && c.deleted==1);
        c.header=0; c.found=1; loaded=(Object *)create(&c); assert(!mm_text_read(loaded,&c,1,37,&c,&c.services) && !strcmp(loaded->text,"loaded")); release(&c,loaded);
        c.header=1; c.wire[0]=c.wire[1]=0x7f; loaded=(Object *)create(&c);
        assert(mm_text_read(loaded,&c,1,37,&c,&c.services) && c.bad==1); release(&c,loaded);
        release(&c,o); assert(!c.live);
    }
}
typedef struct DraftOutput
{
    unsigned char header[MM_TEMPLATE_HEADER_BYTES], descriptor[MM_TEXT_DESCRIPTOR_BYTES];
    int step, fail, deleted, copies;
} DraftOutput;
static int draft_add(void *p, MMU32 type, const void *bytes, size_t count, MMU32 *number)
{
    DraftOutput *d = p;
    assert(type == MM_TMPL && count == sizeof(d->header));
    memcpy(d->header, bytes, count);
    *number = 37;
    return ++d->step != d->fail;
}
static int draft_name(void *p, MMU32 type, MMU32 number, const void *name)
{
    DraftOutput *d = p;
    assert(type == MM_TMPL && number == 37 && !strcmp(name, "saved"));
    return ++d->step != d->fail;
}
static int draft_child(void *p, MMU32 type, MMU32 parent, MMU32 slot, MMU32 child_type,
                       const void *bytes, size_t count)
{
    DraftOutput *d = p;
    assert(type == MM_TMPL && parent == 37 && slot == 0 && child_type == MM_TDT && count == sizeof(d->descriptor));
    memcpy(d->descriptor, bytes, count);
    return ++d->step != d->fail;
}
static void draft_delete(void *p, MMU32 type, MMU32 number)
{
    DraftOutput *d = p;
    assert(type == MM_TMPL && number == 37);
    ++d->deleted;
}
static int draft_entry(void *p, MMU32 i, MMChunkEntry *e)
{
    (void)p;
    assert(i == 0);
    memset(e, 0, sizeof(*e));
    e->type = MM_TMPL;
    e->number = 37;
    e->child_count = 1;
    return 1;
}
static int draft_link(void *p, MMU32 parent, MMU32 i, MMChunkChild *e)
{
    (void)p;
    assert(parent == 0 && i == 0);
    e->type = MM_TDT;
    e->number = 19;
    e->slot = 0;
    return 1;
}
static int preview_read(void *p, MMU32 number, unsigned char *bytes)
{
    DraftOutput *d = p;
    assert(number == 19);
    memcpy(bytes, d->descriptor, sizeof(d->descriptor));
    return 1;
}
static int preview_copy(void *p, MMU32 number, MMU32 *copied)
{
    DraftOutput *d = p;
    assert(number == 29);
    ++d->copies;
    *copied = 0x87654321UL;
    return 1;
}
static int preview_write(void *p, MMU32 number, const unsigned char *bytes)
{
    DraftOutput *d = p;
    assert(number == 19);
    memcpy(d->descriptor, bytes, sizeof(d->descriptor));
    return 1;
}
static void drafts(void)
{
    Context c;
    DraftOutput output;
    MMTextDescriptor draft = {9, {123, NULL, 0x54444620UL, 29}}, localized;
    MMTextDraftServices services = {NULL, 0, 0x1234, draft_add, draft_name, draft_child, draft_delete};
    MMTextPreviewFontServices preview = {NULL, preview_read, preview_copy, preview_write};
    MMChunkAccess graph = {NULL, 1, draft_entry, draft_link};
    MMTemplateState header;
    MMU32 number;
    Object *object;
    int big, failure;
    services.context = preview.context = &output;
    for (big = 0; big < 2; ++big)
    {
        init(&c);
        c.services.host_big_endian = services.big_endian = big;
        c.services.system = services.system;
        object = mm_text_create("saved", draft.shape, &draft.font, &c.services);
        assert(object && mm_text_write(object, &c, MM_TMPL, &number, &c.services));
        release(&c, object);
        for (failure = 1; failure <= 3; ++failure)
        {
            memset(&output, 0, sizeof(output));
            output.fail = failure;
            number = 99;
            assert(!mm_text_write_draft("saved", &draft, &number, &services));
            assert(number == 99 && output.deleted == (failure > 1));
        }
        memset(&output, 0, sizeof(output));
        assert(mm_text_write_draft("saved", &draft, &number, &services) && number == 37);
        assert(mm_template_decode_header(output.header, 0, &header));
        assert(!header.x && !header.y && !header.z && header.flags == MM_TEMPLATE_TEXT);
        assert(!memcmp(output.descriptor, c.wire, sizeof(c.wire)));
        output.descriptor[0] = 127;
        assert(!mm_text_bind_preview_font(&graph, 37, 29, &preview) && !output.copies);
        memcpy(output.descriptor, c.wire, sizeof(c.wire));
        output.descriptor[12] = 37;
        assert(mm_text_bind_preview_font(&graph, 37, 29, &preview) && output.copies == 1);
        assert(mm_text_decode_descriptor(output.descriptor, 0, &localized));
        assert(localized.shape == 9 && localized.font.sid == 0 && localized.font.type == draft.font.type &&
               localized.font.number == 0x87654321UL && output.descriptor[12] == 37);
        assert(!memcmp(output.descriptor, c.wire, 8) && !memcmp(output.descriptor + 16, c.wire + 16, 4));
    }
}
static void author_names(void)
{
    char name[MM_TEXT_NAME_BYTES] = "old";
    char long_name[MM_TEXT_NAME_BYTES + 1];
    assert(mm_text_author_name(NULL, name) == MM_TEXT_NAME_EMPTY);
    assert(mm_text_author_name("", name) == MM_TEXT_NAME_EMPTY);
    assert(mm_text_author_name(" \t\r\n", name) == MM_TEXT_NAME_LENGTH);
    assert(mm_text_author_name("A\xc3", name) == MM_TEXT_NAME_UTF8);
    assert(mm_text_author_name("A\xed\xa0\x80", name) == MM_TEXT_NAME_UTF8);
    assert(mm_text_author_name("\xf0\x9f\x98\x80", name) == MM_TEXT_NAME_CHARACTERS);
    assert(mm_text_author_name("A\tB", name) == MM_TEXT_NAME_CHARACTERS);
    assert(!strcmp(name, "old"));
    assert(mm_text_author_name("\xc3\xa9 \xe2\x82\xac ?", name) == MM_TEXT_NAME_OK);
    assert(!strcmp(name, "\xe9 \x80 ?"));
    memset(long_name, 'X', sizeof(long_name) - 1);
    long_name[sizeof(long_name) - 1] = '\0';
    assert(mm_text_author_name(long_name, name) == MM_TEXT_NAME_LENGTH);
    long_name[MM_TEXT_NAME_BYTES - 1] = '\0';
    assert(mm_text_author_name(long_name, name) == MM_TEXT_NAME_OK && !strcmp(name, long_name));
}
static void names(void)
{
    Context c; MMTextNames n={NULL}; void *table; char out[256];
    init(&c); copy_string(out,"dirty"); assert(!mm_text_action_name(&n,0,out,&c.services) && !out[0] && !c.warnings);
    table=list_new(&c,0,0); c.names=&n; assert(mm_text_set_names(&n,table,&c.services) && c.names_published && ((Root *)table)->refs==2);
    assert(mm_text_action_name(&n,7,out,&c.services) && !strcmp(out,"Pulse"));
    assert(mm_text_action_name(&n,0,out,&c.services) && !strcmp(out,"Rest"));
    assert(!mm_text_action_name(&n,15,out,&c.services) && !out[0] && c.warnings==1);
    release(&c,table); release(&c,table); assert(!c.live);
}
int main(void) { lifecycle(); wire(); drafts(); author_names(); names(); return 0; }
