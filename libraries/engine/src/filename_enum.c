/* Filename enumeration converted from Kauai filename.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "mm/filename_enum.h"
#include "mm/string_ops.h"
#include <string.h>
typedef union EnumBuffer { unsigned char bytes[257]; unsigned short words[257]; } EnumBuffer;
static unsigned unit(const MMStringValue *v, MMI32 i)
{ return v->unit == 1 ? ((const unsigned char *)v->buffer)[i] : ((const unsigned short *)v->buffer)[i]; }
static const void *chars(const MMStringValue *v) { return (const unsigned char *)v->buffer + v->unit; }
static void put(EnumBuffer *b, unsigned size, unsigned ch)
{ if (size == 1) b->bytes[0] = (unsigned char)ch; else b->words[0] = (unsigned short)ch; }
static void validate(const MMFilenameEnumHost *h, int base) { if (h->validate) h->validate(h->context,base); }
static void issue(const MMFilenameEnumHost *h) { if (h->issue) h->issue(h->context); }
static void publish_flags(void *out, MMU32 flags) { if (out) memcpy(out,&flags,sizeof(flags)); }
static void assertion(const MMFilenameEnumHost *h, int ok, int code)
{ if (!ok && h->assertion) h->assertion(h->context,code); }
static void copy(const MMFilenameEnumHost *h, const MMFilenameValue *a, const MMFilenameValue *b)
{ if (h->path.copy_name) h->path.copy_name(h->path.context,a,b);
  else { a->state->type = b->state->type; mm_string_value_copy(&a->path,b->path.buffer); } }
static void check_name(const MMFilenameEnumHost *h, const MMFilenameValue *v, int flags)
{ if (h->path.value.validate) h->path.value.validate(h->path.value.context,v,flags); }
static MMArray array(MMFilenameEnumState *s)
{
    MMArray a; memset(&a,0,sizeof(a)); a.storage = &s->stack; a.width = s->frame_bytes; a.count = s->depth;
    a.free_count = -1; a.trash_unused = s->trash_unused; return a;
}
static int raw_pop(MMFilenameEnumState *s, const MMFilenameEnumFrame *f)
{
    MMArray a; int result;
    if (!s->depth) return 0;
    a = array(s); result = mm_array_pop(&a,f->record); s->depth = a.count; return result == MM_STORAGE_OK;
}
static int pop(MMFilenameEnumState *s, const MMFilenameEnumHost *h, const MMFilenameEnumFrame *f)
{
    validate(h,1);
    if (f->cursor->iterator) { h->close(h->context,f->cursor->iterator); f->cursor->iterator = 0; }
    return raw_pop(s,f);
}
static int push(MMFilenameEnumState *s, const MMFilenameEnumFrame *f)
{
    MMArray a = array(s); int result;
    if (!s->stack_ready) {
        if (mm_array_reserve(&a,5,0) != MM_STORAGE_OK) { mm_storage_dispose(&s->stack); return 0; }
        s->stack_ready = 1;
    }
    result = mm_array_add(&a,f->record,0); s->depth = a.count; return result == MM_STORAGE_OK;
}
int mm_filename_enum_construct(MMFilenameEnumState *s, MMU32 width, const MMStorageAllocator *types,
                                const MMStorageAllocator *stack, int trash)
{
    MMFilenameEnumState fresh; MMCollectionStorage allocator;
    if (!s || !width || width >= MM_STORAGE_LIMIT / 5) return 0;
    memset(&fresh,0,sizeof(fresh));
    if (mm_storage_init(&fresh.stack,stack) != MM_STORAGE_OK || mm_storage_init(&allocator,types) != MM_STORAGE_OK) return 0;
    fresh.type_allocator = allocator.allocator; fresh.frame_bytes = width; fresh.trash_unused = trash;
    fresh.stack.min_grow[0] = 128; fresh.stack.min_grow[1] = 16;
    *s = fresh; s->types = s->inline_types; return 1;
}
void mm_filename_enum_free(MMFilenameEnumState *s, const MMFilenameEnumHost *h, const MMFilenameEnumFrame *f)
{
    if (s->types != s->inline_types) {
        s->type_allocator.release(s->type_allocator.context,s->types,(MMU32)s->type_count * 4); s->types = s->inline_types;
    }
    do { if (f->cursor->iterator) h->close(h->context,f->cursor->iterator); } while (raw_pop(s,f));
    f->cursor->iterator = 0; s->initialized = 0;
    mm_storage_dispose(&s->stack); s->depth = 0; s->stack_ready = 0; validate(h,0);
}
int mm_filename_enum_init(MMFilenameEnumState *s, const MMFilenameEnumHost *h, const MMFilenameEnumFrame *f,
                           const MMFilenameValue *directory, const void *types, MMI32 count, MMU32 flags)
{
    EnumBuffer star; MMI32 i;
    validate(h,0); mm_filename_enum_free(s,h,f);
    if (count <= 0) s->type_count = 0;
    else {
        if ((MMU32)count > 0x1fffffffUL) { issue(h); validate(h,0); return 0; }
        if (count > MM_FILENAME_ENUM_INLINE) {
            s->types = (MMI32 *)s->type_allocator.resize(s->type_allocator.context,0,0,(MMU32)count * 4,MM_STORAGE_NORMAL);
            if (!s->types) { s->types = s->inline_types; issue(h); validate(h,0); return 0; }
        }
        memcpy(s->types,types,(size_t)count * 4); s->type_count = count;
        for (i = count; i > 0; --i) s->types[i-1] = mm_filename_clean_type(s->types[i-1],&h->path.value,0);
    }
    if (!directory) { f->cursor->root_character = 'A'; f->cursor->roots = h->roots(h->context); }
    else {
        f->cursor->roots = 0; f->cursor->root_character = 0; copy(h,&f->name,directory);
        put(&star,f->name.path.unit,'*');
        if (!mm_filename_change_leaf(&f->name,&h->path.value,&star,1)) {
            issue(h); mm_filename_enum_free(s,h,f); validate(h,0); return 0;
        }
    }
    f->cursor->iterator = 0; s->recurse = !!(flags & MM_FILENAME_ENUM_RECURSE); s->initialized = 1; validate(h,0); return 1;
}
int mm_filename_enum_next(MMFilenameEnumState *s, const MMFilenameEnumHost *h, const MMFilenameEnumFrame *f,
                           const MMFilenameValue *out, void *flags, MMU32 input, const MMFilenameValue *scratch)
{
    EnumBuffer buffer, root, character; MMStringValue text;
    MMI32 count, i; MMU32 bit; int result, ok;
    validate(h,0); text = out->path; text.buffer = &buffer;
    if (!s->initialized) { assertion(h,0,MM_FILENAME_ENUM_UNINITIALIZED); return 0; }
    if ((input & MM_FILENAME_ENUM_SKIP) && !pop(s,h,f)) goto done;
    if (f->cursor->root_character) {
        bit = (MMU32)1 << (f->cursor->root_character - 'A');
        while (f->cursor->root_character <= 'Z' && !(f->cursor->roots & bit)) { ++f->cursor->root_character; bit <<= 1; }
        if (f->cursor->root_character > 'Z') goto done;
        if (!h->root_path(h->context,(unsigned)(f->cursor->root_character++ - 'A'),&root,255)) goto done;
        count = h->name_length ? h->name_length(h->context,&root) : (MMI32)mm_string_zero_length(&root,text.unit);
        mm_string_value_set(&text,&root,count);
        ok = mm_filename_build(out,&h->path,&text,MM_FILENAME_DIRECTORY); assertion(h,ok,MM_FILENAME_ENUM_BUILD);
        goto got;
    }
    for (;;) {
        if (!f->cursor->iterator)
            result = h->first(h->context,chars(&f->name.path),&f->cursor->iterator,f->entry,1024,&f->cursor->info);
        else result = h->next(h->context,f->cursor->iterator,f->entry,1024,&f->cursor->info);
        if (result != MM_FILENAME_CASE_ENTRY) { if (result == MM_FILENAME_CASE_ERROR) issue(h); goto finish_directory; }
        if (f->cursor->info.hidden || f->cursor->info.system) continue;
        count = h->name_length ? h->name_length(h->context,f->entry) : (MMI32)mm_string_zero_length(f->entry,text.unit);
        mm_string_value_set(&text,f->entry,count); copy(h,out,&f->name);
        if (f->cursor->info.directory) {
            count = (MMI32)unit(&text,0);
            if ((count == 1 || (count == 2 && unit(&text,2) == '.')) && unit(&text,1) == '.') continue;
            ok = mm_filename_change_leaf(out,&h->path.value,0,0); assertion(h,ok,MM_FILENAME_ENUM_LEAF);
            ok = mm_filename_down(out,&h->path,&text,MM_FILENAME_MOVE_TO_DIR,scratch);
        } else ok = mm_filename_change_leaf(out,&h->path.value,chars(&text),(MMI32)unit(&text,0));
        if (!ok) { issue(h); continue; }
        if (!s->type_count) goto got;
        for (i = s->type_count; i > 0; --i) if (s->types[i-1] == out->state->type) goto got;
    }
finish_directory:
    if (!s->depth) goto done;
    publish_flags(flags,MM_FILENAME_ENUM_POST);
    copy(h,out,&f->name); ok = mm_filename_change_leaf(out,&h->path.value,0,0); assertion(h,ok,MM_FILENAME_ENUM_LEAF);
    ok = pop(s,h,f); assertion(h,ok,MM_FILENAME_ENUM_POP); check_name(h,out,MM_FILENAME_DIR); validate(h,0); return 1;
got:
    check_name(h,out,MM_FILENAME_FILE | MM_FILENAME_DIR); publish_flags(flags,MM_FILENAME_ENUM_PRE | MM_FILENAME_ENUM_POST);
    if (s->recurse && out->state->type == MM_FILENAME_DIRECTORY && (!f->cursor->iterator || !f->cursor->info.link)) {
        if (push(s,f)) {
            copy(h,&f->name,out); put(&character,text.unit,'*');
            if (!mm_filename_change_leaf(&f->name,&h->path.value,&character,1)) {
                ok = raw_pop(s,f); assertion(h,ok,MM_FILENAME_ENUM_POP);
            } else {
                f->cursor->iterator = 0; f->cursor->roots = 0; f->cursor->root_character = 0;
                publish_flags(flags,MM_FILENAME_ENUM_PRE);
            }
        } else issue(h);
    }
    validate(h,0); return 1;
done:
    mm_filename_enum_free(s,h,f); validate(h,0); return 0;
}
