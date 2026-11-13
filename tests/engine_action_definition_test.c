#include "three_mm/action_definition.h"
#include <assert.h>
#include <string.h>
typedef struct Table { unsigned char fixed[16],variable[16],data[96]; ThreeMMTag tags[2]; int count,live; } Table;
typedef struct Test
{
    ThreeMMActionDefinition state; ThreeMMActionDefinitionServices services;
    Table tables[4]; unsigned char header[8],expected_fixed[16],expected_variable[16],expected_matrix[96];
    int phase,fail,other,short_header,bad,optional_absent,released[8],releases,object_live,initializations;
    int invalidations,read_sounds,clears; void *output; ThreeMMI32 size;
} Test;
static Test *t(void *p) { return (Test *)p; }
static int checkpoint(Test *x) { return ++x->phase==x->fail; }
static void put(unsigned char *p,ThreeMMU32 n,int big)
{ int i; for (i=0;i<4;++i) { p[big?3-i:i]=(unsigned char)n; n>>=8; } }
static void reverse(unsigned char *p,int bytes,int width)
{ int i,j; unsigned char b; for (i=0;i<bytes;i+=width) for (j=0;j<width/2;++j) { b=p[i+j]; p[i+j]=p[i+width-j-1]; p[i+width-j-1]=b; } }
static ThreeMMActionDefinition *state(void *p,void *object) { assert(p==object); return &t(p)->state; }
static void *create(void *p) { if (checkpoint(t(p))) return NULL; assert(!t(p)->object_live); t(p)->object_live=1; return p; }
static void release(void *p,void *object)
{
    Test *x=t(p); int i;
    if (p==object) { assert(x->object_live && !x->invalidations); three_mm_action_dispose(&x->state,&x->services); x->object_live=0; return; }
    for (i=0;i<4;++i) if (object==&x->tables[i]) break;
    assert(i<4 && x->tables[i].live==1); x->tables[i].live=0; x->released[x->releases++]=i;
}
static void *duplicate(void *p,void *table,int kind)
{ Test *x=t(p); assert(table==&x->tables[kind]); assert(x->state.flags==0x8000000dUL); if (checkpoint(x)) return NULL; x->tables[kind].live=1; return table; }
static void *find(void *p,void *file,ThreeMMU32 type,ThreeMMU32 number)
{
    assert(file==p); if (checkpoint(t(p))) return NULL;
    if (type==0x4143544eUL) { assert(number==7); return t(p)->header; }
    assert(number<3 && type==(number==0?THREE_MM_ACTION_GGCL:number==1?THREE_MM_ACTION_GLXF:THREE_MM_ACTION_GLMS)); return &t(p)->tables[number];
}
static int unpack(void *p,void *block) { assert(block==t(p)->header); return !checkpoint(t(p)); }
static ThreeMMI32 block_size(void *p,void *block,int packed) { assert(block==t(p)->header); return packed?88:t(p)->short_header?7:8; }
static int block_read(void *p,void *block,void *out,size_t n)
{ assert(block==t(p)->header && n==8); if (checkpoint(t(p))) return 0; memcpy(out,block,n); return 1; }
static int child(void *p,void *file,ThreeMMU32 type,ThreeMMU32 number,ThreeMMU32 ct,ThreeMMU32 *out)
{
    Test *x=t(p); assert(file==p && type==0x4143544eUL && number==7); if (checkpoint(x)) return 0;
    *out=ct==THREE_MM_ACTION_GGCL?0:ct==THREE_MM_ACTION_GLXF?1:2;
    if (*out==2 && x->optional_absent) return 0; return 1;
}
static void *read_table(void *p,void *block,int kind,int *other)
{ Test *x=t(p); assert(block==&x->tables[kind]); if (checkpoint(x)) return NULL; *other=x->other; x->tables[kind].live=1; return block; }
static ThreeMMI32 count(void *p,void *table) { (void)p; return ((Table *)table)->count; }
static ThreeMMI32 table_size(void *p,void *table)
{ assert(t(p)->output==p); return table==&t(p)->tables[0]?20:30; }
static unsigned char *fixed(void *p,void *group,ThreeMMI32 i) { assert(group==&t(p)->tables[0] && i>=0 && i<2); return ((Table *)group)->fixed+i*8; }
static unsigned char *variable(void *p,void *group,ThreeMMI32 i) { assert(group==&t(p)->tables[0] && i>=0 && i<2); return ((Table *)group)->variable+i*8; }
static ThreeMMI32 variable_size(void *p,void *group,ThreeMMI32 i) { assert(group==&t(p)->tables[0] && i>=0 && i<2); return 8; }
static unsigned char *data(void *p,void *list,ThreeMMI32 i)
{ assert(i>=0 && i<((Table *)list)->count); return ((Table *)list)->data+i*(list==&t(p)->tables[2]?16:48); }
static void validate(void *p,void *wire) { assert(wire==&t(p)->tables[2]); }
static void *create_sounds(void *p,ThreeMMI32 capacity)
{ Test *x=t(p); assert(capacity==x->tables[2].count); if (checkpoint(x)) return NULL; x->tables[3].live=1; x->tables[3].count=0; return &x->tables[3]; }
static int append(void *p,void *list,const ThreeMMTag *tag)
{ Table *table=(Table *)list; assert(list==&t(p)->tables[3] && table->count<2); if (checkpoint(t(p))) return 0; table->tags[table->count++]=*tag; return 1; }
static void get_sound(void *p,void *list,ThreeMMI32 i,ThreeMMTag *out)
{ assert(list==&t(p)->tables[3] && i>=0 && i<2); ++t(p)->read_sounds; *out=((Table *)list)->tags[i]; }
static void clear_type(void *p,void *out) { ++t(p)->clears; ((ThreeMMTag *)out)->type=0; }
static void publish(void *p,void *out,const ThreeMMTag *tag) { (void)p; assert(!((ThreeMMTag *)out)->type); *(ThreeMMTag *)out=*tag; }
static int initialize(void *p,void *object,void *file,ThreeMMU32 type,ThreeMMU32 number)
{ assert(p==object); ++t(p)->initializations; return three_mm_action_initialize(&t(p)->state,file,type,number,&t(p)->services); }
static ThreeMMI32 object_size(void *p) { (void)p; return 80; }
static void invalidate(void *p,void *out,size_t n) { ++t(p)->invalidations; memset(out,0xab,n); }
static void bad_order(void *p) { ++t(p)->bad; }
static ThreeMMActionDefinitionServices setup(Test *x,int host,int other)
{
    ThreeMMActionDefinitionServices s; int i; memset(x,0,sizeof(*x)); memset(&s,0,sizeof(s)); x->other=other;
    x->header[host?1:0]=1; put(x->header+4,0x8000000dUL,host);
    for (i=0;i<3;++i) x->tables[i].count=2;
    for (i=0;i<2;++i) { put(x->tables[0].fixed+i*8,(ThreeMMU32)i,host); put(x->tables[0].fixed+i*8+4,100+i,host); }
    for (i=0;i<16;++i) x->tables[0].variable[i]=(unsigned char)(i+3);
    for (i=0;i<96;++i) x->tables[1].data[i]=(unsigned char)(i+5);
    memcpy(x->expected_fixed,x->tables[0].fixed,16); memcpy(x->expected_variable,x->tables[0].variable,16); memcpy(x->expected_matrix,x->tables[1].data,96);
    if (other) { reverse(x->tables[0].fixed,16,4); reverse(x->tables[0].variable,16,2); reverse(x->tables[1].data,96,4); }
    for (i=0;i<2;++i) { unsigned char *row=x->tables[2].data+i*16;
        put(row,i?0:0x80000001UL,host!=other); put(row+4,0xfedcba98UL,host!=other); put(row+8,0x4d534e44UL,host!=other); put(row+12,123+i,host!=other); }
    s.context=x; s.host_big_endian=host; s.state=state; s.create=create; s.release=release; s.duplicate=duplicate;
    s.find=find; s.unpack=unpack; s.block_size=block_size; s.block_read=block_read; s.child=child; s.read_table=read_table;
    s.count=count; s.table_size=table_size; s.fixed=fixed; s.variable=variable; s.variable_size=variable_size; s.data=data;
    s.validate_wire_sounds=validate; s.create_sounds=create_sounds; s.append_sound=append; s.read_sound=get_sound;
    s.clear_sound_type=clear_type; s.publish_sound=publish; s.initialize=initialize; s.object_size=object_size; s.invalidate=invalidate; s.bad_order=bad_order;
    x->services=s; return s;
}
static void empty(Test *x)
{ int i; assert(!x->state.cels && !x->state.matrices && !x->state.sounds); for (i=0;i<4;++i) assert(!x->tables[i].live); }
int main(void)
{
    Test x; ThreeMMActionDefinitionServices s; ThreeMMTag tag; ThreeMMU32 id; int host,other,fail,ok,i; unsigned char out[48];
    for (host=0;host<2;++host) for (other=0;other<2;++other)
    {
        s=setup(&x,host,other); assert(three_mm_action_initialize(&x.state,&x,0x4143544eUL,7,&s));
        assert(x.state.flags==0x8000000dUL && x.tables[3].count==2);
        assert(!memcmp(x.tables[0].fixed,x.expected_fixed,16) && !memcmp(x.tables[0].variable,x.expected_variable,16) && !memcmp(x.tables[1].data,x.expected_matrix,96));
        for (i=0;i<2;++i) { ThreeMMTag *p=&x.tables[3].tags[i]; assert(p->sid==(i?0:(ThreeMMI32)(-2147483647)) && !p->cache && p->type==0x4d534e44UL && p->number==123+(ThreeMMU32)i); }
        assert(three_mm_action_cel_count(&x.state,&s)==2);
        three_mm_action_get_cel(&x.state,1,out,&s); assert(!memcmp(out,x.expected_fixed+8,8));
        three_mm_action_get_part(&x.state,1,1,out,&s); assert(!memcmp(out,x.expected_variable+12,4));
        three_mm_action_get_matrix(&x.state,1,out,&s); assert(!memcmp(out,x.expected_matrix+48,48));
        three_mm_action_dispose(&x.state,&s); empty(&x); assert(x.releases==4 && x.released[0]==2 && x.released[1]==0 && x.released[2]==1 && x.released[3]==3);
        three_mm_action_dispose(&x.state,&s); assert(x.releases==4 && x.state.flags==0x8000000dUL);
    }
    for (fail=0;fail<=15;++fail)
    {
        s=setup(&x,0,0); x.fail=fail; ok=three_mm_action_initialize(&x.state,&x,0x4143544eUL,7,&s); assert(ok==(fail==0 || fail==10));
        if (fail>=4) assert(x.state.flags==0x8000000dUL);
        if (fail==14 || fail==15) assert(x.state.sounds && x.tables[3].count==fail-14 && !x.tables[2].live);
        three_mm_action_dispose(&x.state,&s); empty(&x);
    }
    for (fail=0;fail<=17;++fail)
    {
        s=setup(&x,0,0); x.fail=fail; x.output=NULL; x.size=-77;
        ok=three_mm_action_fetch(&x,0x4143544eUL,7,x.header,&x.output,&x.size,&s); assert(ok==(fail==0 || fail==12));
        if (ok) { assert(x.output==&x && x.size==130); release(&x,&x); }
        else assert(x.invalidations==2 && x.size==(ThreeMMI32)-1414812757);
        empty(&x);
    }
    s=setup(&x,0,0); x.fail=1; assert(three_mm_action_fetch(&x,0x4143544eUL,7,x.header,NULL,&x.size,&s) && x.size==88 && !x.phase);
    for (fail=0;fail<4;++fail)
    {
        s=setup(&x,0,0); x.fail=fail; assert((three_mm_action_create(&x.tables[0],&x.tables[1],0x8000000dUL,&s)!=NULL)==!fail);
        if (!fail) release(&x,&x); empty(&x);
    }
    s=setup(&x,0,0); x.short_header=1; assert(!three_mm_action_initialize(&x.state,&x,0x4143544eUL,7,&s) && !x.state.flags); empty(&x);
    s=setup(&x,0,1); x.tables[0].count=x.tables[1].count=x.tables[2].count=0;
    assert(three_mm_action_initialize(&x.state,&x,0x4143544eUL,7,&s) && x.state.sounds && !x.tables[3].count);
    three_mm_action_dispose(&x.state,&s); empty(&x);
    s=setup(&x,0,0); x.header[0]=3; assert(three_mm_action_initialize(&x.state,&x,0x4143544eUL,7,&s) && x.bad==1 && x.state.flags==0x8000000dUL); three_mm_action_dispose(&x.state,&s);
    s=setup(&x,0,0); assert(three_mm_action_initialize(&x.state,&x,0x4143544eUL,7,&s));
    for (i=0;i<4;++i)
    {
        tag.sid=77; tag.cache=&x; tag.type=99; tag.number=88; id=i==0?0:i==1?1:i==2?0xffffffffUL:0x80000000UL;
        memcpy(x.tables[0].fixed,&id,4); three_mm_action_get_sound(&x.state,0,&tag,&s);
        if (i<2) assert(tag.sid==x.tables[3].tags[i].sid && !tag.cache && tag.number==123+(ThreeMMU32)i);
        else assert(tag.sid==77 && tag.cache==&x && !tag.type && tag.number==88);
    }
    assert(x.read_sounds==2 && x.clears==4); three_mm_action_dispose(&x.state,&s); empty(&x);
    s=setup(&x,0,0); tag.sid=77; tag.cache=&x; tag.type=99; tag.number=88; three_mm_action_get_sound(&x.state,0,&tag,&s);
    assert(tag.sid==77 && tag.cache==&x && !tag.type && tag.number==88 && !x.read_sounds);
    return 0;
}
