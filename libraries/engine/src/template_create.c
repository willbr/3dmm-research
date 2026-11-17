/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "mm/template_create.h"
#include <string.h>
void mm_template_state_init(MMTemplateState *state)
{ memset(state, 0, sizeof(*state)); }
static void diagnostic(const MMTemplateCreateServices *s,int n) { if (s->diagnostic) s->diagnostic(s->context,n); }
static MMU32 word(const unsigned char *p,int n,int big)
{ MMU32 v=0; int i; for (i=0;i<n;++i) v=(v<<8)|p[big?i:n-1-i]; return v; }
static void put(unsigned char *p,int n,MMU32 value,int big)
{ int i; for (i=0;i<n;++i) { p[big?n-1-i:i]=(unsigned char)(value&255U); value>>=8; } }
static void swap(unsigned char *p,MMI32 count,int stride)
{ MMI32 i; int j; unsigned char t; for (i=0;i<count;++i,p+=stride) for (j=0;j<stride/2;++j) { t=p[j]; p[j]=p[stride-1-j]; p[stride-1-j]=t; } }

int mm_template_scan_costumes(MMI32 set, const MMTemplateCostumeScan *s)
{
    MMTemplateChild child;
    MMU32 ordinal;
    MMI32 candidate;
    int step;
    for (ordinal = 0;; ++ordinal)
    {
        step = s->child(s->context, ordinal, &child);
        if (step <= 0)
            return step == 0;
        if (child.type != MM_CMTL)
            continue;
        if (!s->costume_set(s->context, &child, &candidate))
            return 0;
        if (set >= 0 && candidate != set)
            continue;
        step = s->visit(s->context, &child, candidate);
        if (step <= 0)
            return step == 0;
    }
}

int mm_template_bind_defaults(MMI32 sets, const MMTemplateDefaultServices *s)
{
    MMI32 set, id;
    for (set = 0; set < sets; ++set)
        if (!s->costume_id(s->context, set, 0, &id) || !s->prepare(s->context, set, id))
            return 0;
    if (s->apply)
        for (set = 0; set < sets; ++set)
            s->apply(s->context, set);
    return 1;
}

int mm_template_read_header(void *object,void *file,MMU32 type,MMU32 number,const MMTemplateCreateServices *s)
{
    unsigned char bytes[16]; int big; MMTemplateState t; void *block;
    block=s->find(s->context,file,type,number);
    if (!block || !s->unpack(s->context,block) || s->block_size(s->context,block,0)<16 ||
        !s->block_read(s->context,block,bytes,sizeof(bytes))) return 0;
    if (bytes[0]==1 && !bytes[1]) big=0;
    else if (!bytes[0] && bytes[1]==1) big=1;
    else { diagnostic(s,MM_TEMPLATE_BAD_ORDER); big=s->host_big_endian; }
    t=*s->state(s->context,object); t.x=word(bytes+4,2,big); t.y=word(bytes+6,2,big);
    t.z=word(bytes+8,2,big); t.flags=word(bytes+12,4,big); *s->state(s->context,object)=t;
    return s->read_name(s->context,object,file,type,number);
}
int mm_template_write_header(void *object,void *file,MMU32 type,MMU32 *number,const MMTemplateCreateServices *s)
{
    MMTemplateState t; unsigned char bytes[16]; int big=s->host_big_endian;
    t=*s->state(s->context,object); put(bytes,2,1,big); put(bytes+2,2,s->system,big);
    put(bytes+4,2,t.x,big); put(bytes+6,2,t.y,big); put(bytes+8,2,t.z,big);
    bytes[10]=bytes[11]=0; put(bytes+12,4,t.flags,big);
    if (!s->add_chunk(s->context,file,type,bytes,sizeof(bytes),number)) return 0;
    if (!s->set_name(s->context,object,file,MM_TMPL,*number))
    { s->delete_chunk(s->context,file,type,*number); return 0; }
    return 1;
}
int mm_template_fetch(void *file,MMU32 type,MMU32 number,void *block,void **out,MMI32 *bytes,const MMTemplateCreateServices *s)
{
    void *object; MMTemplateChild child; MMTemplateState t; int text;
    *bytes=s->block_size(s->context,block,1); if (!out) return 1;
    text=s->child(s->context,file,type,number,0,MM_TDT,&child);
    object=s->create(s->context,text);
    if (!object || !s->initialize(s->context,object,file,type,number))
    {
        if (object) s->release(s->context,object);
        s->invalidate(s->context,out,sizeof(*out)); s->invalidate(s->context,bytes,sizeof(*bytes)); return 0;
    }
    *out=object; t=*s->state(s->context,object);
    if (t.flags&2U) *bytes=s->object_size(s->context,1);
    else *bytes=s->object_size(s->context,0)+s->table_size(s->context,t.parents)+
        s->table_size(s->context,t.sets)+s->table_size(s->context,t.costumes);
    return 1;
}
typedef struct LegacyCostumeTable
{
    const MMTemplateCreateServices *services;
    void *file, *cache;
    MMU32 type, number;
    MMI32 ids[50], count;
} LegacyCostumeTable;

static int table_child(void *context, MMU32 ordinal, MMTemplateChild *out)
{
    LegacyCostumeTable *table = (LegacyCostumeTable *)context;
    const MMTemplateCreateServices *s = table->services;
    return s->enumerate(s->context, table->file, table->type, table->number, (MMI32)ordinal, out);
}

static int table_costume_set(void *context, const MMTemplateChild *child, MMI32 *set)
{
    LegacyCostumeTable *table = (LegacyCostumeTable *)context;
    const MMTemplateCreateServices *s = table->services;
    void *costume = s->fetch_custom(s->context, table->cache, child->type, child->number);
    if (!costume)
        return 0;
    *set = s->custom_set(s->context, costume);
    s->release(s->context, costume);
    return 1;
}

static int table_add_costume(void *context, const MMTemplateChild *child, MMI32 set)
{
    LegacyCostumeTable *table = (LegacyCostumeTable *)context;
    (void)set;
    if (table->count == (MMI32)(sizeof(table->ids) / sizeof(table->ids[0])))
    {
        diagnostic(table->services, MM_TEMPLATE_COSTUME_LIMIT);
        return -1;
    }
    table->ids[table->count++] = (MMI32)child->id;
    return 1;
}

static int build_costumes(void *object, void *file, MMU32 type, MMU32 number, MMTemplateState *state,
                          const MMTemplateCreateServices *s)
{
    LegacyCostumeTable table;
    MMTemplateCostumeScan scan;
    MMI32 set;
    int ok = 0;
    diagnostic(s, MM_TEMPLATE_BUILD_COSTUMES);
    table.cache = s->create_cache(s->context, file);
    if (!table.cache)
        return 0;
    state->costumes = s->create_costumes(s->context);
    *s->state(s->context, object) = *state;
    if (!state->costumes)
        goto done;
    table.services = s;
    table.file = file;
    table.type = type;
    table.number = number;
    scan.context = &table;
    scan.child = table_child;
    scan.costume_set = table_costume_set;
    scan.visit = table_add_costume;
    for (set = 0; set < state->set_count; ++set)
    {
        table.count = 0;
        if (!mm_template_scan_costumes(set, &scan) ||
            !s->append_costumes(s->context, state->costumes, table.ids, table.count))
            goto done;
    }
    ok = 1;
done:
    s->release(s->context, table.cache);
    return ok;
}

int mm_template_initialize(void *object,void *file,MMU32 type,MMU32 number,const MMTemplateCreateServices *s)
{
    MMTemplateState t; MMTemplateChild child; MMI32 i,count,set;
    void *block; int other;
    if (!s->header(s->context,object,file,type,number)) return 0;
    t=*s->state(s->context,object);
    if (!s->child(s->context,file,type,number,0,MM_GLPI,&child) ||
        !(block=s->find(s->context,file,child.type,child.number))) return 0;
    t.parents=s->read_table(s->context,block,MM_TEMPLATE_PARENTS,&other); *s->state(s->context,object)=t;
    if (!t.parents) return 0;
    s->validate_table(s->context,t.parents,MM_TEMPLATE_PARENTS,0);
    if (other) swap(s->list_data(s->context,t.parents),s->count(s->context,t.parents),2);
    if (!s->child(s->context,file,type,number,0,MM_GLBS,&child) ||
        !(block=s->find(s->context,file,child.type,child.number))) return 0;
    t.sets=s->read_table(s->context,block,MM_TEMPLATE_SETS,&other); *s->state(s->context,object)=t;
    if (!t.sets) return 0;
    s->validate_table(s->context,t.sets,MM_TEMPLATE_SETS,0);
    if (other) swap(s->list_data(s->context,t.sets),s->count(s->context,t.sets),2);
    if (s->debug_checks && s->child(s->context,file,type,number,0,MM_GLDC,&child)) diagnostic(s,MM_TEMPLATE_OBSOLETE);
    t.set_count=-1; *s->state(s->context,object)=t;
    for (i=0;i<s->count(s->context,t.sets);++i)
    { set=s->item(s->context,t.sets,i); if (set>t.set_count) { t.set_count=set; *s->state(s->context,object)=t; } }
    ++t.set_count; t.custom_count=0; *s->state(s->context,object)=t;
    while (s->child(s->context,file,type,number,(MMU32)t.custom_count,MM_CMTL,&child))
    { ++t.custom_count; *s->state(s->context,object)=t; }
    t.action_count=0; *s->state(s->context,object)=t;
    while (s->child(s->context,file,type,number,(MMU32)t.action_count,MM_ACTN,&child))
    { ++t.action_count; *s->state(s->context,object)=t; }
    if (s->child(s->context,file,type,number,0,MM_GGCM,&child))
    {
        block=s->find(s->context,file,child.type,child.number); if (!block) return 0;
        t.costumes=s->read_table(s->context,block,MM_TEMPLATE_COSTUMES,&other); *s->state(s->context,object)=t;
        if (!t.costumes) return 0;
        s->validate_table(s->context,t.costumes,MM_TEMPLATE_COSTUMES,t.set_count);
        if (other) for (i=0;i<t.set_count;++i)
        {
            swap(s->fixed_data(s->context,t.costumes,i),1,4);
            count=(MMI32)word(s->fixed_data(s->context,t.costumes,i),4,s->host_big_endian);
            swap(s->variable_data(s->context,t.costumes,i),count,4);
        }
        return 1;
    }
    return build_costumes(object, file, type, number, &t, s);
}
void mm_template_dispose(void *object,const MMTemplateCreateServices *s)
{
    MMTemplateState t; t=*s->state(s->context,object);
    if (t.parents) { s->release(s->context,t.parents); } t.parents=NULL; *s->state(s->context,object)=t;
    if (t.sets) { s->release(s->context,t.sets); } t.sets=NULL; *s->state(s->context,object)=t;
    if (t.costumes) { s->release(s->context,t.costumes); } t.costumes=NULL; *s->state(s->context,object)=t;
}
void *mm_template_create_body(void *object,const MMTemplateCreateServices *s)
{
    MMTemplateState t; void *body; t=*s->state(s->context,object);
    body=s->create_body(s->context,t.parents,t.sets);
    if (!body || !s->default_costume(s->context,object,body)) { if (body) s->release(s->context,body); return NULL; }
    return body;
}
typedef struct DefaultCostumes
{
    const MMTemplateCreateServices *services;
    void *object, *body, **refs;
} DefaultCostumes;

static int live_costume_id(void *context, MMI32 set, MMI32 index, MMI32 *id)
{
    DefaultCostumes *costumes = (DefaultCostumes *)context;
    const MMTemplateCreateServices *s = costumes->services;
    *id = s->costume_id(s->context, costumes->object, set, index);
    return 1;
}

static int live_costume_prepare(void *context, MMI32 set, MMI32 id)
{
    DefaultCostumes *costumes = (DefaultCostumes *)context;
    const MMTemplateCreateServices *s = costumes->services;
    void *costume = s->custom(s->context, costumes->object, id);
    if (!costume)
        return 0;
    costumes->refs[set] = costume;
    if (s->custom_set(s->context, costume) != set)
        diagnostic(s, MM_TEMPLATE_WRONG_SET);
    return 1;
}

static void live_costume_apply(void *context, MMI32 set)
{
    DefaultCostumes *costumes = (DefaultCostumes *)context;
    const MMTemplateCreateServices *s = costumes->services;
    s->apply_custom(s->context, costumes->body, costumes->refs[set]);
}

int mm_template_default_costume(void *object, void *body, const MMTemplateCreateServices *s)
{
    DefaultCostumes costumes;
    MMTemplateDefaultServices defaults;
    void *storage = NULL;
    MMI32 i, sets = s->state(s->context, object)->set_count;
    int ok;
    if (!s->allocate_refs(s->context, sets, &storage))
        return 0;
    costumes.services = s;
    costumes.object = object;
    costumes.body = body;
    costumes.refs = storage;
    defaults.context = &costumes;
    defaults.costume_id = live_costume_id;
    defaults.prepare = live_costume_prepare;
    defaults.apply = live_costume_apply;
    ok = mm_template_bind_defaults(sets, &defaults);
    for (i = 0; i < sets; ++i)
        if (costumes.refs[i])
        {
            s->release(s->context, costumes.refs[i]);
            costumes.refs[i] = NULL;
        }
    s->free_memory(s->context, &storage);
    return ok;
}
