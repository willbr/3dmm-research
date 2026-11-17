#include "mm/filename_owned.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stddef.h>
static void check(int ok,int line) { if(!ok){fprintf(stderr,"Filename owned C: %d\n",line);exit(1);} }
#define CHECK(x) check(!!(x),__LINE__)
typedef union Storage { MMFilenameOwned8 bytes; MMFilenameOwned16 words; } Storage;
typedef union Buffer { unsigned char bytes[300]; unsigned short words[300]; } Buffer;
typedef struct Fixture {
    MMFilenameOwnedHost host; MMFilenameValue a,b; int issues[32],issue_count,checks,arguments,mode;
    unsigned unit; MMI32 expected_type; int enforce_type;
} Fixture;
static unsigned unit(const void *p,unsigned width,size_t i)
{ return width==1?((const unsigned char *)p)[i]:((const unsigned short *)p)[i]; }
static void put(void *p,unsigned width,size_t i,unsigned ch)
{ if(width==1)((unsigned char *)p)[i]=(unsigned char)ch;else ((unsigned short *)p)[i]=(unsigned short)ch; }
static void assertion(void *c,int code,MMI32 observed)
{ Fixture *f=(Fixture *)c;(void)observed;CHECK(f->issue_count<32);f->issues[f->issue_count++]=code; }
static void memory(void *c,const void *p,size_t bytes)
{ Fixture *f=(Fixture *)c;CHECK(p&&bytes==257*f->unit); }
static void path_check(void *c,const MMStringValue *v)
{
    Fixture *f=(Fixture *)c;++f->checks;
    if(f->enforce_type)CHECK(f->b.state->type==f->expected_type);
    mm_filename_owned_check_path(v,&f->host);
}
static void arguments(void *c,const void *p,MMI32 count,unsigned width)
{ Fixture *f=(Fixture *)c;CHECK((p||!count)&&count>=0&&width==f->unit);++f->arguments; }
static size_t full(void *c,const void *input,void *out,size_t capacity,void **leaf)
{
    Fixture *f=(Fixture *)c;size_t n=0;
    if(f->mode==1)return 0;
    while(unit(input,f->unit,n))++n;
    CHECK(n<capacity);memcpy(out,input,(n+1)*f->unit);
    *leaf=f->mode==3?NULL:(unsigned char *)out+f->unit;return n;
}
static int equal(void *c,const void *a,const void *b)
{ Fixture *f=(Fixture *)c;(void)a;(void)b;return f->mode!=2; }
static int separator(void *c,unsigned ch) { (void)c;return ch=='/'; }
static void set(Fixture *f,MMFilenameValue *v,const char *s)
{
    Buffer input;size_t i,n=strlen(s);
    for(i=0;i<=n;++i)put(&input,f->unit,i,(unsigned char)s[i]);
    mm_filename_owned_set_path(v,&input,(MMI32)n,&f->host);
}
static void run(unsigned width)
{
    Storage a,b,relocated;Fixture f;Buffer output,input;MMStringValue text;
    unsigned poison=width==1?0xa5:0xa5a5;size_t i;MMFilenameValue moved;
    memset(&a,0xa5,sizeof(a));memset(&b,0xa5,sizeof(b));memset(&f,0,sizeof(f));f.unit=width;
    f.a=width==1?mm_filename_owned8_view(&a.bytes,&a):mm_filename_owned16_view(&a.words,&a);
    f.b=width==1?mm_filename_owned8_view(&b.bytes,&b):mm_filename_owned16_view(&b.words,&b);
    f.host.context=&f;f.host.check_path=path_check;f.host.memory=memory;f.host.assertion=assertion;
    f.host.arguments=arguments;f.host.full_path=full;f.host.equal=equal;f.host.separator=separator;
    mm_filename_owned_init(&f.a,&f.host);mm_filename_owned_init(&f.b,&f.host);
    CHECK(f.a.state->type==MM_FILENAME_NIL&&unit(f.a.path.buffer,width,0)==0&&unit(f.a.path.buffer,width,1)==0);
    CHECK(unit(f.a.path.buffer,width,2)==poison&&unit(f.a.path.buffer,width,256)==poison);
    set(&f,&f.a,"/short.txt");set(&f,&f.b,"/a considerably longer name.bin");f.a.state->type=123;
    f.expected_type=123;f.enforce_type=1;f.checks=0;
    mm_filename_owned_copy(&f.b,&f.a,1,&f.host);CHECK(f.checks==3);
    CHECK(unit(f.b.path.buffer,width,0)==10&&unit(f.b.path.buffer,width,11)==0&&unit(f.b.path.buffer,width,12)=='a');
    memset(&output,0xa5,sizeof(output));memset(&text,0,sizeof(text));text.buffer=&output;text.unit=width;
    mm_filename_owned_export(&f.b,&text,&f.host);CHECK(unit(&output,width,0)==10&&unit(&output,width,12)==poison);
    mm_filename_owned_copy(&f.b,&f.b,1,&f.host);CHECK(!f.issue_count);
    memcpy(&relocated,&b,sizeof(b));moved=width==1?mm_filename_owned8_view(&relocated.bytes,&relocated):mm_filename_owned16_view(&relocated.words,&relocated);
    CHECK(moved.state->type==123&&unit(moved.path.buffer,width,0)==10);
    f.enforce_type=0;f.checks=0;mm_filename_owned_copy(&f.b,&f.a,0,&f.host);CHECK(f.checks==2);
    for(i=0;i<300;++i)put(&input,width,i,'q');mm_filename_owned_set_path(&f.a,&input,299,&f.host);
    CHECK(unit(f.a.path.buffer,width,0)==255&&unit(f.a.path.buffer,width,256)==0);
    mm_filename_owned_copy(&f.b,&f.a,1,&f.host);CHECK(unit(f.b.path.buffer,width,255)=='q'&&unit(f.b.path.buffer,width,256)==0);
    set(&f,&f.a,"/a.txt");f.a.state->type=0;f.mode=0;f.issue_count=0;
    mm_filename_owned_validate(&f.a,MM_FILENAME_FILE,&f.host);CHECK(!f.issue_count);
    mm_filename_owned_validate(&f.a,MM_FILENAME_DIR,&f.host);CHECK(f.issue_count==1&&f.issues[0]==MM_FILENAME_CHECK_FILE);
    f.mode=1;f.issue_count=0;mm_filename_owned_validate(&f.a,0,&f.host);CHECK(f.issue_count==1&&f.issues[0]==MM_FILENAME_CHECK_FULL);
    f.mode=2;f.issue_count=0;mm_filename_owned_validate(&f.a,0,&f.host);CHECK(f.issue_count==1&&f.issues[0]==MM_FILENAME_CHECK_FULL);
    f.mode=3;f.issue_count=0;mm_filename_owned_validate(&f.a,0,&f.host);CHECK(f.issue_count==1&&f.issues[0]==MM_FILENAME_CHECK_LEAF);
    set(&f,&f.a,"/dir/");f.a.state->type=MM_FILENAME_DIRECTORY;f.issue_count=0;mm_filename_owned_validate(&f.a,0,&f.host);CHECK(!f.issue_count);
    f.mode=0;f.issue_count=0;mm_filename_owned_validate(&f.a,MM_FILENAME_FILE,&f.host);
    CHECK(f.issue_count==2&&f.issues[0]==MM_FILENAME_CHECK_DIR&&f.issues[1]==MM_FILENAME_CHECK_NO_LEAF);
    set(&f,&f.a,"x");f.a.state->type=MM_FILENAME_NIL;f.issue_count=0;mm_filename_owned_validate(&f.a,MM_FILENAME_FILE,&f.host);
    CHECK(f.issue_count==2&&f.issues[0]==MM_FILENAME_CHECK_EMPTY&&f.issues[1]==MM_FILENAME_CHECK_NAMED_EMPTY);
    f.issue_count=0;put(f.a.path.buffer,width,2,'x');put(f.a.path.buffer,width,3,0);
    mm_filename_owned_check_path(&f.a.path,&f.host);CHECK(f.issue_count==2&&f.issues[0]==MM_FILENAME_CHECK_TERMINATOR&&f.issues[1]==MM_FILENAME_CHECK_NULL);
    if(width==2){f.issue_count=0;put(f.a.path.buffer,width,0,256);mm_filename_owned_check_path(&f.a.path,&f.host);CHECK(f.issue_count==1&&f.issues[0]==MM_FILENAME_CHECK_COUNT);}
    mm_filename_owned_init(&f.a,NULL);CHECK(f.a.state->type==MM_FILENAME_NIL&&!unit(f.a.path.buffer,width,0));
}
int main(void)
{
    CHECK(offsetof(MMFilenameOwned8,path)==sizeof(MMFilenameState)&&offsetof(MMFilenameOwned16,path)==sizeof(MMFilenameState));
    run(1);run(2);puts("C89 owned filenames: storage, copy, relocation and validation passed");return 0;
}
