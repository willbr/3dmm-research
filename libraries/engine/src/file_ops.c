/* File factories, registries and name transactions converted from Kauai
 * file.cpp/file_native.cpp. Copyright (c) Microsoft Corporation.
 * Licensed under the MIT License. */
#include "mm/file_ops.h"
#include <stddef.h>

static void enter_registry(const MMFileRegistry *registry)
{ if (registry->enter) registry->enter(registry->lock); }
static void leave_registry(const MMFileRegistry *registry)
{ if (registry->leave) registry->leave(registry->lock); }

void mm_file_registry_init(MMFileRegistry *registry, void *lock, void (*enter)(void *), void (*leave)(void *))
{
    registry->head = NULL;
    registry->lock = lock;
    registry->enter = enter;
    registry->leave = leave;
}

void mm_registered_file_init(MMRegisteredFile *file, void *object, void *name, MMFileOwner *owner,
                             MMReference *references, void *file_context, MMFileOwnerHost (*file_host)(void *))
{
    mm_linked_list_init(&file->link, file);
    file->object = object;
    file->name = name;
    file->owner = owner;
    file->references = references;
    file->file_context = file_context;
    file->file_host = file_host;
}

void mm_file_registry_attach(MMFileRegistry *registry, MMRegisteredFile *file)
{
    enter_registry(registry);
    mm_linked_list_attach(&file->link, &registry->head, NULL);
    leave_registry(registry);
}

void mm_file_registry_detach(MMFileRegistry *registry, MMRegisteredFile *file)
{
    enter_registry(registry);
    mm_linked_list_attach(&file->link, NULL, NULL);
    leave_registry(registry);
}

MMRegisteredFile *mm_file_registry_first(const MMFileRegistry *registry)
{ return registry->head ? registry->head->owner : NULL; }
MMRegisteredFile *mm_file_registry_next(const MMRegisteredFile *file)
{ return file->link.next ? file->link.next->owner : NULL; }

int mm_file_registry_contains(const MMFileRegistry *registry, const MMRegisteredFile *file)
{
    MMRegisteredFile *p;
    enter_registry(registry);
    for (p = mm_file_registry_first(registry); p && p != file; p = mm_file_registry_next(p)) {}
    leave_registry(registry);
    return p != NULL;
}

static void validate_name(const MMFileOpsHost *h, void *name)
{ if (h->validate_name) h->validate_name(h->context, name); }
static void validate(const MMFileOpsHost *h, const MMRegisteredFile *file)
{ if (h->validate_object) h->validate_object(h->context, file->object); }
static void issue(const MMFileOpsHost *h, int code)
{ if (h->issue) h->issue(h->context, code); }
static void assertion(const MMFileOpsHost *h, int code)
{ if (h->assertion) h->assertion(h->context, code); }
static MMFileOwnerHost file_host(const MMRegisteredFile *file)
{
    return file->file_host(file->file_context);
}
static void enter_file(const MMFileOwnerHost *host)
{ if (host->enter) host->enter(host->context); }
static void leave_file(const MMFileOwnerHost *host)
{ if (host->leave) host->leave(host->context); }
static int open_owner(MMRegisteredFile *file, int create, MMU32 flags)
{
    const MMFileOwnerHost host = file_host(file);
    return mm_file_owner_open(file->owner, &host, create, flags);
}
static int set_flags(MMRegisteredFile *file, MMU32 flags, MMU32 mask)
{
    const MMFileOwnerHost host = file_host(file);
    return mm_file_owner_set_flags(file->owner, &host, flags, mask);
}
int mm_file_ops_matches(const MMFileOpsHost *h, const MMRegisteredFile *file, void *name)
{
    const MMFileOwnerHost host = file_host(file);
    int equal;
    enter_file(&host);
    equal = h->equal_names(h->context, name, file->name);
    leave_file(&host);
    return equal;
}
MMRegisteredFile *mm_file_ops_find(const MMFileOpsHost *h, void *name)
{
    MMRegisteredFile *p;
    validate_name(h, name); enter_registry(h->registry);
    for (p = mm_file_registry_first(h->registry); p; p = mm_file_registry_next(p)) {
        validate(h, p);
        if (mm_file_ops_matches(h, p, name)) break;
    }
    leave_registry(h->registry); return p;
}
MMRegisteredFile *mm_file_ops_open(const MMFileOpsHost *h, void *name, MMU32 flags)
{
    MMRegisteredFile *p;
    validate_name(h, name);
    if (flags & MM_FILE_TEMP) assertion(h, MM_FILE_OP_TEMP_OPEN);
    p = mm_file_ops_find(h, name);
    if (p) {
        if (!set_flags(p, flags, 0xffffffffUL)) return NULL;
        h->retain(h->context, p->object); return p;
    }
    p = h->construct(h->context, name, flags);
    if (!p) goto fail;
    if (!open_owner(p, 0, flags)) { h->destroy(h->context, p->object); goto fail; }
    validate(h, p); return p;
fail:
    issue(h, MM_FILE_OP_OPEN); return NULL;
}
MMRegisteredFile *mm_file_ops_create(const MMFileOpsHost *h, void *name, MMU32 flags)
{
    MMRegisteredFile *p;
    validate_name(h, name);
    if (mm_file_ops_find(h, name)) { assertion(h, MM_FILE_OP_CREATE_OPEN); return NULL; }
    flags |= MM_FILE_WRITE;
    p = h->construct(h->context, name, flags);
    if (!p) goto fail;
    if (!open_owner(p, 1, flags)) { h->destroy(h->context, p->object); goto fail; }
    validate(h, p); return p;
fail:
    issue(h, MM_FILE_OP_CREATE); return NULL;
}
MMRegisteredFile *mm_file_ops_temp(const MMFileOpsHost *h, void *basis, void *scratch)
{
    if (basis) {
        validate_name(h, basis);
        h->copy_name(h->context, scratch, basis);
        if (!h->unique_name(h->context, scratch)) goto fail;
    } else if (!h->temp_name(h->context, scratch)) goto fail;
    return mm_file_ops_create(h, scratch, MM_FILE_TEMP | MM_FILE_WRITE | MM_FILE_DENY_WRITE);
fail:
    issue(h, MM_FILE_OP_CREATE); return NULL;
}
int mm_file_ops_swap(const MMFileOpsHost *h, MMRegisteredFile *a, MMRegisteredFile *b, void *scratch)
{
    int result = 0;
    void *an, *bn;
    MMFileOwnerHost ah, bh;
    validate(h, a); validate(h, b);
    if (a == b) { assertion(h, MM_FILE_OP_SWAP_SELF); return 1; }
    ah = file_host(a); bh = file_host(b);
    enter_file(&ah); enter_file(&bh);
    an = a->name; bn = b->name;
    if (h->assertion && !h->same_directory(h->context, an, bn)) assertion(h, MM_FILE_OP_SWAP_DIRECTORY);
    h->copy_name(h->context, scratch, bn);
    if (!h->unique_name(h->context, scratch)) goto done;
    mm_file_owner_close(a->owner, &ah, 0);
    mm_file_owner_close(b->owner, &bh, 0);
    if (!h->rename_name(h->context, an, scratch)) goto reopen;
    if (!h->rename_name(h->context, bn, an)) goto recover_a;
    if (!h->rename_name(h->context, scratch, bn)) {
        if (!h->rename_name(h->context, an, bn)) {
            assertion(h, MM_FILE_OP_RECOVERY);
            h->copy_name(h->context, bn, an); h->copy_name(h->context, an, scratch);
            goto reopen;
        }
recover_a:
        if (!h->rename_name(h->context, scratch, an)) {
            assertion(h, MM_FILE_OP_RECOVERY); h->copy_name(h->context, an, scratch);
        }
        goto reopen;
    }
    h->copy_name(h->context, scratch, an);
    h->copy_name(h->context, an, bn); h->copy_name(h->context, bn, scratch);
    result = 1;
reopen:
    mm_file_owner_open(a->owner, &ah, 0, a->owner->flags);
    mm_file_owner_open(b->owner, &bh, 0, b->owner->flags);
done:
    leave_file(&ah); leave_file(&bh);
    if (!result) issue(h, MM_FILE_OP_SWAP);
    validate(h, a); validate(h, b); return result;
}
int mm_file_ops_rename(const MMFileOpsHost *h, MMRegisteredFile *file, void *name)
{
    const MMFileOwnerHost host = file_host(file);
    int result;
    void *old;
    validate(h, file); validate_name(h, name);
    enter_file(&host); old = file->name;
    if (h->assertion && !h->same_directory(h->context, old, name)) assertion(h, MM_FILE_OP_RENAME_DIRECTORY);
    mm_file_owner_close(file->owner, &host, 0);
    result = h->rename_name(h->context, old, name);
    if (result) h->copy_name(h->context, old, name);
    if (!mm_file_owner_open(file->owner, &host, 0, file->owner->flags)) result = 0;
    leave_file(&host);
    if (!result) issue(h, MM_FILE_OP_RENAME);
    validate(h, file); return result;
}
int mm_file_ops_rename_replacing(const MMFileOpsHost *h, MMRegisteredFile *file, void *name)
{
    if (h->name_status(h->context, name) != MM_FILE_NAME_MISSING) h->delete_name(h->context, name);
    return mm_file_ops_rename(h, file, name);
}
int mm_file_ops_set_name(const MMFileOpsHost *h, MMRegisteredFile *file, void *name, void *scratch)
{
    MMFileOwnerHost host;
    MMRegisteredFile *old = mm_file_ops_find(h, name);
    if (old) {
        if (old == file) return 1;
        if (!mm_file_ops_swap(h, file, old, scratch)) return 0;
        host = file_host(old);
        mm_file_owner_set_temp(old->owner, &host, 1); return 1;
    }
    return mm_file_ops_rename_replacing(h, file, name);
}
void mm_file_ops_clear_marks(const MMFileOpsHost *h)
{
    MMRegisteredFile *p;
    enter_registry(h->registry);
    for (p = mm_file_registry_first(h->registry); p; p = mm_file_registry_next(p)) {
        validate(h, p); set_flags(p, 0, MM_FILE_MARK);
    }
    leave_registry(h->registry);
}
void mm_file_ops_close_unmarked(const MMFileOpsHost *h)
{
    MMRegisteredFile *p, *next;
    enter_registry(h->registry);
    for (p = mm_file_registry_first(h->registry); p; p = next) {
        validate(h, p); next = mm_file_registry_next(p);
        if (!(p->owner->flags & MM_FILE_MARK) && p->references->count == 0)
            h->destroy(h->context, p->object);
    }
    leave_registry(h->registry);
}
void mm_file_ops_shutdown(const MMFileOpsHost *h)
{
    MMRegisteredFile *p;
    enter_registry(h->registry);
    for (p = mm_file_registry_first(h->registry); p; p = mm_file_registry_next(p)) {
        const MMFileOwnerHost host = file_host(p);
        validate(h, p); mm_file_owner_close(p->owner, &host, 1);
    }
    leave_registry(h->registry);
}
