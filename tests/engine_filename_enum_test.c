#include "mm/filename_enum.h"
#include "mm/string_ops.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static void check(int ok, int line) { if (!ok) { fprintf(stderr,"Filename enum: %d\n",line); exit(1); } }
#define CHECK(x) check(!!(x),__LINE__)
typedef union Buffer { unsigned char bytes[257]; unsigned short words[257]; } Buffer;
typedef union EntryBuffer { unsigned char bytes[1024]; unsigned short words[1024]; } EntryBuffer;
typedef struct Name { MMFilenameState state; Buffer path; } Name;
typedef struct Frame { Name name; MMFilenameEnumCursor cursor; EntryBuffer entry; } Frame;
typedef struct Entry { const char *name; MMFilenameEnumInfo info; } Entry;
typedef struct Directory { const char *pattern; Entry entries[8]; unsigned count, index, opens, closes; int end; } Directory;
typedef struct Allocation { unsigned calls, fail, live; } Allocation;
typedef struct Fixture {
    MMFilenameEnumState state; MMFilenameEnumHost host; Frame frame; Name directory, output, scratch;
    MMFilenameEnumFrame view; MMFilenameValue dir, out, temp;
    Directory dirs[8]; unsigned count, unit, issues, path_issues, warnings, root_calls, validations, last_root;
    MMU32 flags, flags_at_issue, root_mask; MMI32 type_at_issue;
    int info_status, root_fail, build_fail;
    Allocation types, stack;
} Fixture;
static unsigned get(const void *p, unsigned size, int i)
{ return size == 1 ? ((const unsigned char *)p)[i] : ((const unsigned short *)p)[i]; }
static void put(void *p, unsigned size, int i, unsigned ch)
{ if (size == 1) ((unsigned char *)p)[i] = (unsigned char)ch; else ((unsigned short *)p)[i] = (unsigned short)ch; }
static MMFilenameValue name_view(Fixture *f, Name *n)
{
    MMFilenameValue v; memset(&v,0,sizeof(v)); v.owner = n; v.state = &n->state; v.path.buffer = &n->path; v.path.unit = f->unit;
    return v;
}
static void set(Fixture *f, Name *n, const char *text, MMI32 type)
{ int i, length = (int)strlen(text); CHECK(length <= 255); n->state.type = type; put(&n->path,f->unit,0,(unsigned)length);
  for (i = 0; i <= length; ++i) put(&n->path,f->unit,i+1,(unsigned char)text[i]); }
static void raw(Fixture *f, const void *p, char *text)
{ int i = 0; while (get(p,f->unit,i)) { CHECK(i < 1023); text[i] = (char)get(p,f->unit,i); ++i; } text[i] = 0; }
static void equals(Fixture *f, const char *text)
{ int i, count = (int)strlen(text); CHECK(get(&f->output.path,f->unit,0) == (unsigned)count);
  for (i = 0; i <= count; ++i) CHECK(get(&f->output.path,f->unit,i+1) == (unsigned char)text[i]); }
static void *resize(void *c, void *p, MMU32 old_bytes, MMU32 bytes, int priority)
{
    Allocation *a = (Allocation *)c; void *out; (void)old_bytes; (void)priority;
    ++a->calls; if (a->calls == a->fail) return NULL; out = realloc(p,bytes); CHECK(out); if (!p) ++a->live; return out;
}
static void release(void *c, void *p, MMU32 bytes)
{ Allocation *a = (Allocation *)c; (void)bytes; CHECK(p && a->live); --a->live; free(p); }
static unsigned upper(void *c, unsigned ch) { (void)c; return ch >= 'a' && ch <= 'z' ? ch - 'a' + 'A' : ch; }
static int separator(void *c, unsigned ch) { (void)c; return ch == '/'; }
static unsigned slash(void *c) { (void)c; return '/'; }
static int info(void *c, const void *p, MMFilenameInfo *out)
{ Fixture *f = (Fixture *)c; (void)p; out->directory = 1; out->hidden = out->system = 0; return f->info_status; }
static void issue(void *c)
{ Fixture *f = (Fixture *)c; ++f->issues; f->flags_at_issue = f->flags; f->type_at_issue = f->output.state.type; }
static void path_issue(void *c, const MMFilenameValue *v, int code)
{ Fixture *f = (Fixture *)c; (void)v; CHECK(code > 0); ++f->path_issues; }
static void assertion(void *c, int code) { Fixture *f = (Fixture *)c; CHECK(code > 0); ++f->warnings; }
static void validate(void *c, int base) { Fixture *f = (Fixture *)c; (void)base; ++f->validations; }
static MMU32 roots(void *c) { return ((Fixture *)c)->root_mask; }
static int root_path(void *c, unsigned index, void *out, MMI32 capacity)
{ Fixture *f = (Fixture *)c; CHECK(capacity == 255); ++f->root_calls; f->last_root = index;
  if (f->root_fail) return 0; put(out,f->unit,0,'/'); put(out,f->unit,1,'A'+index); put(out,f->unit,2,'/'); put(out,f->unit,3,0); return 1; }
static MMI32 full_path(void *c, const void *input, void *out, MMI32 capacity)
{ Fixture *f = (Fixture *)c; int i = 0; CHECK(capacity == 255); if (f->build_fail) return 0;
  do { put(out,f->unit,i,get(input,f->unit,i)); } while (get(input,f->unit,i++)); return i-1; }
static int emit(Fixture *f, Directory *d, void *name, MMI32 capacity, MMFilenameEnumInfo *info_out)
{
    const char *text; int i;
    CHECK(capacity == 1024); if (d->index >= d->count) return d->end;
    text = d->entries[d->index].name; *info_out = d->entries[d->index].info;
    for (i = 0; text[i]; ++i) { CHECK(i < 1023); put(name,f->unit,i,(unsigned char)text[i]); }
    put(name,f->unit,i,0); return MM_FILENAME_CASE_ENTRY;
}
static int first(void *c, const void *pattern, void **iterator, void *name, MMI32 capacity, MMFilenameEnumInfo *out)
{
    Fixture *f = (Fixture *)c; char text[1024]; unsigned i; raw(f,pattern,text);
    for (i = 0; i < f->count; ++i) if (!strcmp(f->dirs[i].pattern,text)) {
        Directory *d = &f->dirs[i]; CHECK(d->opens == d->closes); ++d->opens; d->index = 0; *iterator = d; return emit(f,d,name,capacity,out);
    }
    *iterator = NULL; return MM_FILENAME_CASE_ERROR;
}
static int next(void *c, void *iterator, void *name, MMI32 capacity, MMFilenameEnumInfo *out)
{ Fixture *f = (Fixture *)c; Directory *d = (Directory *)iterator; CHECK(d && d->opens > d->closes); ++d->index; return emit(f,d,name,capacity,out); }
static void close_directory(void *c, void *iterator)
{ Directory *d = (Directory *)iterator; (void)c; CHECK(d && d->opens == d->closes + 1); ++d->closes; }
static Directory *directory(Fixture *f, const char *pattern)
{ Directory *d = &f->dirs[f->count++]; CHECK(f->count <= 8); d->pattern = pattern; d->end = MM_FILENAME_CASE_END; return d; }
static void entry(Directory *d, const char *name, int dir, int hidden, int system, int link)
{ Entry *e = &d->entries[d->count++]; CHECK(d->count <= 8); e->name = name; e->info.directory = dir;
  e->info.hidden = hidden; e->info.system = system; e->info.link = link; }
static void init(Fixture *f, unsigned size)
{
    MMStorageAllocator types, stack;
    memset(f,0,sizeof(*f)); f->unit = size; f->flags = 0xdeadbeefUL; f->info_status = MM_FILENAME_FOUND;
    f->dir = name_view(f,&f->directory); f->out = name_view(f,&f->output); f->temp = name_view(f,&f->scratch);
    f->view.record = &f->frame; f->view.cursor = &f->frame.cursor; f->view.name = name_view(f,&f->frame.name); f->view.entry = &f->frame.entry;
    set(f,&f->directory,"/r/",MM_FILENAME_DIRECTORY); set(f,&f->output,"/original.bin",0x42494eL); set(f,&f->frame.name,"",MM_FILENAME_NIL);
    f->host.context = f->host.path.context = f->host.path.value.context = f;
    f->host.path.value.upper = upper; f->host.path.value.separator = separator; f->host.path.separator = slash;
    f->host.path.info = info; f->host.path.full_path = full_path; f->host.path.issue = path_issue;
    f->host.first = first; f->host.next = next; f->host.close = close_directory; f->host.issue = issue;
    f->host.assertion = assertion; f->host.validate = validate; f->host.roots = roots; f->host.root_path = root_path;
    types.context = &f->types; types.resize = resize; types.release = release; stack = types; stack.context = &f->stack;
    CHECK(mm_filename_enum_construct(&f->state,sizeof(Frame),&types,&stack,1));
}
static int begin(Fixture *f, const MMI32 *types, MMI32 count, MMU32 flags)
{ return mm_filename_enum_init(&f->state,&f->host,&f->view,&f->dir,types,count,flags); }
static int step(Fixture *f, MMU32 flags)
{ return mm_filename_enum_next(&f->state,&f->host,&f->view,&f->out,&f->flags,flags,&f->temp); }
static void finish(Fixture *f)
{ unsigned i; mm_filename_enum_free(&f->state,&f->host,&f->view);
  CHECK(!f->types.live && !f->stack.live && !f->state.depth && !f->frame.cursor.iterator && !f->state.initialized);
  for (i = 0; i < f->count; ++i) CHECK(f->dirs[i].opens == f->dirs[i].closes); }
int main(void)
{
    Fixture f; Directory *d; unsigned size, i; MMI32 types[21]; char long_path[256], parent[254], pattern[255], child[256];
    const char *patterns[] = {"/r/*","/r/d/*","/r/d/d/*","/r/d/d/d/*","/r/d/d/d/d/*","/r/d/d/d/d/d/*","/r/d/d/d/d/d/d/*"};
    for (size = 1; size <= 2; ++size) {
        init(&f,size); CHECK(!step(&f,0) && f.warnings == 1 && f.flags == 0xdeadbeefUL); equals(&f,"/original.bin"); finish(&f);
        init(&f,size); d = directory(&f,"/r/*"); entry(d,"hidden",0,1,0,0); entry(d,"system",0,0,1,0);
        CHECK(begin(&f,NULL,0,0) && !step(&f,0)); equals(&f,"/original.bin"); CHECK(f.flags == 0xdeadbeefUL); finish(&f);
        init(&f,size); d = directory(&f,"/r/*"); entry(d,".",1,0,0,0); entry(d,"..",1,0,0,0);
        CHECK(begin(&f,NULL,0,0) && !step(&f,0)); equals(&f,"/r/*"); CHECK(f.flags == 0xdeadbeefUL); finish(&f);
        init(&f,size); d = directory(&f,"/r/*"); entry(d,"a.bin",0,0,0,0); types[0] = 0x747874L;
        CHECK(begin(&f,types,1,0) && !step(&f,0)); equals(&f,"/r/a.bin"); CHECK(f.flags == 0xdeadbeefUL); finish(&f);
        init(&f,size); d = directory(&f,"/r/*"); entry(d,"sub",1,0,0,0); entry(d,"a.txt",0,0,0,0);
        d = directory(&f,"/r/sub/*"); entry(d,"b.bin",0,0,0,0);
        CHECK(begin(&f,NULL,0,MM_FILENAME_ENUM_RECURSE) && step(&f,0)); equals(&f,"/r/sub/"); CHECK(f.flags == MM_FILENAME_ENUM_PRE && f.state.depth == 1);
        CHECK(step(&f,0)); equals(&f,"/r/sub/b.bin"); CHECK(f.flags == 48);
        CHECK(step(&f,0)); equals(&f,"/r/sub/"); CHECK(f.flags == MM_FILENAME_ENUM_POST && f.state.depth == 0);
        CHECK(step(&f,0)); equals(&f,"/r/a.txt"); CHECK(!step(&f,0)); finish(&f);
        init(&f,size); d = directory(&f,"/r/*"); entry(d,"sub",1,0,0,0); entry(d,"a.txt",0,0,0,0);
        CHECK(begin(&f,NULL,0,1) && step(&f,0) && step(&f,MM_FILENAME_ENUM_SKIP)); equals(&f,"/r/a.txt"); CHECK(f.flags == 48 && !f.state.depth); finish(&f);
        init(&f,size); d = directory(&f,"/r/*"); entry(d,"sub",1,0,0,1);
        CHECK(begin(&f,NULL,0,1) && step(&f,0)); CHECK(f.flags == 48 && !f.state.depth && !f.stack.calls); finish(&f);
        init(&f,size); d = directory(&f,"/r/*"); entry(d,"sub",1,0,0,0); f.stack.fail = 1;
        CHECK(begin(&f,NULL,0,1) && step(&f,0)); CHECK(f.flags == 48 && f.issues == 1 && f.flags_at_issue == 48 && !f.state.depth); finish(&f);
        init(&f,size); for (i = 0; i < 7; ++i) { d = directory(&f,patterns[i]); entry(d,"d",1,0,0,0); }
        f.stack.fail = 2; CHECK(begin(&f,NULL,0,1)); for (i = 0; i < 6; ++i) CHECK(step(&f,0));
        CHECK(f.state.depth == 5 && f.flags == 48 && f.issues == 1 && f.stack.calls == 2);
        while (step(&f,0)) {} finish(&f);
        init(&f,size); for (i = 0; i < 21; ++i) types[i] = 0x747874L;
        f.types.fail = 1; CHECK(!begin(&f,types,21,0) && f.issues == 1 && !f.state.initialized);
        CHECK(f.state.types == f.state.inline_types && !f.types.live); CHECK(begin(&f,types,21,0));
        CHECK(f.state.types != f.state.inline_types && f.state.types[20] == 0x545854L && types[20] == 0x747874L); finish(&f);
        init(&f,size); CHECK(begin(&f,NULL,0,0) && !step(&f,MM_FILENAME_ENUM_SKIP)); CHECK(!f.issues && f.flags == 0xdeadbeefUL); finish(&f);
        init(&f,size); d = directory(&f,"/r/*"); d->end = MM_FILENAME_CASE_ERROR;
        CHECK(begin(&f,NULL,0,0) && !step(&f,0) && f.issues == 1); finish(&f);
        init(&f,size); d = directory(&f,"/r/*"); entry(d,"sub",1,0,0,0); f.info_status = MM_FILENAME_INFO_ERROR;
        CHECK(begin(&f,NULL,0,0) && !step(&f,0)); equals(&f,"/r/"); CHECK(f.path_issues == 1 && f.issues == 1 && f.flags == 0xdeadbeefUL); finish(&f);
        init(&f,size); f.root_mask = ((MMU32)1 << 25) | 0x80000000UL; types[0] = 0x747874L;
        CHECK(mm_filename_enum_init(&f.state,&f.host,&f.view,NULL,types,1,0)); CHECK(step(&f,0)); equals(&f,"/Z/");
        CHECK(f.flags == 48 && f.last_root == 25 && !step(&f,0) && f.root_calls == 1); finish(&f);
        init(&f,size); f.root_mask = 1; f.root_fail = 1;
        CHECK(mm_filename_enum_init(&f.state,&f.host,&f.view,NULL,NULL,0,0)); CHECK(!step(&f,0)); equals(&f,"/original.bin"); CHECK(!f.issues); finish(&f);
        init(&f,size); memset(long_path,'a',255); long_path[0] = long_path[254] = '/'; long_path[255] = 0;
        set(&f,&f.directory,long_path,MM_FILENAME_DIRECTORY); CHECK(!begin(&f,NULL,0,0) && f.issues == 1 && !f.state.initialized); finish(&f);
        memset(parent,'a',253); parent[0] = parent[252] = '/'; parent[253] = 0;
        memcpy(pattern,parent,253); pattern[253] = '*'; pattern[254] = 0;
        memcpy(child,parent,253); child[253] = 'd'; child[254] = '/'; child[255] = 0;
        init(&f,size); set(&f,&f.directory,parent,MM_FILENAME_DIRECTORY); d = directory(&f,pattern); entry(d,"d",1,0,0,0);
        CHECK(begin(&f,NULL,0,1) && step(&f,0)); equals(&f,child);
        CHECK(f.flags == 48 && !f.state.depth && f.stack.live == 1 && !f.issues && !d->closes && f.frame.cursor.iterator == d);
        CHECK(!step(&f,0)); finish(&f);
        init(&f,size); f.root_mask = 1 | ((MMU32)1 << 25); types[0] = 0x747874L;
        d = directory(&f,"/A/*"); entry(d,"a.txt",0,0,0,0); directory(&f,"/Z/*");
        CHECK(mm_filename_enum_init(&f.state,&f.host,&f.view,NULL,types,1,1));
        CHECK(step(&f,0)); equals(&f,"/A/"); CHECK(f.flags == 16);
        CHECK(step(&f,0)); equals(&f,"/A/a.txt");
        CHECK(step(&f,0)); equals(&f,"/A/"); CHECK(f.flags == 32);
        CHECK(step(&f,0)); equals(&f,"/Z/"); CHECK(f.flags == 16);
        CHECK(step(&f,0)); equals(&f,"/Z/"); CHECK(f.flags == 32 && !step(&f,0)); finish(&f);
        init(&f,size); f.root_mask = 1; f.build_fail = 1;
        CHECK(mm_filename_enum_init(&f.state,&f.host,&f.view,NULL,NULL,0,0)); CHECK(step(&f,0)); equals(&f,"");
        CHECK(f.output.state.type == MM_FILENAME_NIL && f.flags == 48 && f.path_issues == 1 && f.warnings == 1 && !f.issues); finish(&f);
        init(&f,size); d = directory(&f,"/r/*"); entry(d,"sub",1,0,0,0); directory(&f,"/r/sub/*");
        CHECK(begin(&f,NULL,0,1) && step(&f,0)); CHECK(begin(&f,NULL,0,0) && !f.state.depth && !f.stack.live && d->closes == 1); finish(&f);
    }
    puts("C89 filename enumeration: ownership, filters, traversal, skip/link/error and allocation recovery passed");
    return 0;
}
