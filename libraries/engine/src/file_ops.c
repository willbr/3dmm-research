/* File factories, registries and name transactions converted from Kauai
 * file.cpp/file_native.cpp. Copyright (c) Microsoft Corporation.
 * Licensed under the MIT License. */
#include "mm/file_ops.h"
#include <stddef.h>
static void validate_name(const MMFileOpsHost *h, void *name)
{ if (h->validate_name) h->validate_name(h->context, name); }
static void validate(const MMFileOpsHost *h, void *object)
{ if (h->validate_object) h->validate_object(h->context, object); }
static void issue(const MMFileOpsHost *h, int code)
{ if (h->issue) h->issue(h->context, code); }
static void assertion(const MMFileOpsHost *h, int code)
{ if (h->assertion) h->assertion(h->context, code); }
static int open_owner(const MMFileOpsHost *h, void *object, int create, MMU32 flags)
{
    MMFileOwnerHost host;
    h->owner_host(h->context, object, &host);
    return mm_file_owner_open(h->owner(h->context, object), &host, create, flags);
}
static void close_owner(const MMFileOpsHost *h, void *object, int final)
{
    MMFileOwnerHost host;
    h->owner_host(h->context, object, &host);
    mm_file_owner_close(h->owner(h->context, object), &host, final);
}
static int set_flags(const MMFileOpsHost *h, void *object, MMU32 flags, MMU32 mask)
{
    MMFileOwnerHost host;
    h->owner_host(h->context, object, &host);
    return mm_file_owner_set_flags(h->owner(h->context, object), &host, flags, mask);
}
void *mm_file_ops_find(const MMFileOpsHost *h, void *name)
{
    void *p;
    int equal;
    validate_name(h, name); h->enter_list(h->context);
    for (p = h->first(h->context); p; p = h->next(h->context, p)) {
        validate(h, p); h->enter_object(h->context, p);
        equal = h->equal_names(h->context, name, h->name(h->context, p));
        h->leave_object(h->context, p);
        if (equal) break;
    }
    h->leave_list(h->context); return p;
}
void *mm_file_ops_open(const MMFileOpsHost *h, void *name, MMU32 flags)
{
    void *p;
    validate_name(h, name);
    if (flags & MM_FILE_TEMP) assertion(h, MM_FILE_OP_TEMP_OPEN);
    p = mm_file_ops_find(h, name);
    if (p) {
        if (!set_flags(h, p, flags, 0xffffffffUL)) return NULL;
        h->retain(h->context, p); return p;
    }
    p = h->construct(h->context, name, flags);
    if (!p) goto fail;
    if (!open_owner(h, p, 0, flags)) { h->destroy(h->context, p); goto fail; }
    validate(h, p); return p;
fail:
    issue(h, MM_FILE_OP_OPEN); return NULL;
}
void *mm_file_ops_create(const MMFileOpsHost *h, void *name, MMU32 flags)
{
    void *p;
    validate_name(h, name);
    if (mm_file_ops_find(h, name)) { assertion(h, MM_FILE_OP_CREATE_OPEN); return NULL; }
    flags |= MM_FILE_WRITE;
    p = h->construct(h->context, name, flags);
    if (!p) goto fail;
    if (!open_owner(h, p, 1, flags)) { h->destroy(h->context, p); goto fail; }
    validate(h, p); return p;
fail:
    issue(h, MM_FILE_OP_CREATE); return NULL;
}
void *mm_file_ops_temp(const MMFileOpsHost *h, void *basis, void *scratch)
{
    if (basis) {
        h->copy_name(h->context, scratch, basis);
        if (!h->unique_name(h->context, scratch)) goto fail;
    } else if (!h->temp_name(h->context, scratch)) goto fail;
    return mm_file_ops_create(h, scratch, MM_FILE_TEMP | MM_FILE_WRITE | MM_FILE_DENY_WRITE);
fail:
    issue(h, MM_FILE_OP_CREATE); return NULL;
}
int mm_file_ops_swap(const MMFileOpsHost *h, void *a, void *b, void *scratch)
{
    int result = 0;
    void *an, *bn;
    if (a == b) { assertion(h, MM_FILE_OP_SWAP_SELF); return 1; }
    h->enter_object(h->context, a); h->enter_object(h->context, b);
    an = h->name(h->context, a); bn = h->name(h->context, b);
    if (h->assertion && !h->same_directory(h->context, an, bn)) assertion(h, MM_FILE_OP_SWAP_DIRECTORY);
    h->copy_name(h->context, scratch, bn);
    if (!h->unique_name(h->context, scratch)) goto done;
    close_owner(h, a, 0); close_owner(h, b, 0);
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
    open_owner(h, a, 0, h->owner(h->context, a)->flags);
    open_owner(h, b, 0, h->owner(h->context, b)->flags);
done:
    h->leave_object(h->context, a); h->leave_object(h->context, b);
    if (!result) issue(h, MM_FILE_OP_SWAP);
    validate(h, a); validate(h, b); return result;
}
int mm_file_ops_rename(const MMFileOpsHost *h, void *object, void *name)
{
    int result;
    void *old;
    h->enter_object(h->context, object); old = h->name(h->context, object);
    if (h->assertion && !h->same_directory(h->context, old, name)) assertion(h, MM_FILE_OP_RENAME_DIRECTORY);
    close_owner(h, object, 0);
    result = h->rename_name(h->context, old, name);
    if (result) h->copy_name(h->context, old, name);
    if (!open_owner(h, object, 0, h->owner(h->context, object)->flags)) result = 0;
    h->leave_object(h->context, object);
    if (!result) issue(h, MM_FILE_OP_RENAME);
    validate(h, object); return result;
}
int mm_file_ops_set_name(const MMFileOpsHost *h, void *object, void *name)
{
    MMFileOwnerHost host;
    void *old = mm_file_ops_find(h, name);
    if (old) {
        if (old == object) return 1;
        if (!h->swap_objects(h->context, object, old)) return 0;
        h->owner_host(h->context, old, &host);
        mm_file_owner_set_temp(h->owner(h->context, old), &host, 1); return 1;
    }
    if (h->name_status(h->context, name) != MM_FILE_NAME_MISSING) h->delete_name(h->context, name);
    return h->rename_object(h->context, object, name);
}
void mm_file_ops_clear_marks(const MMFileOpsHost *h)
{
    void *p;
    h->enter_list(h->context);
    for (p = h->first(h->context); p; p = h->next(h->context, p)) {
        validate(h, p); set_flags(h, p, 0, MM_FILE_MARK);
    }
    h->leave_list(h->context);
}
void mm_file_ops_close_unmarked(const MMFileOpsHost *h)
{
    void *p, *next;
    h->enter_list(h->context);
    for (p = h->first(h->context); p; p = next) {
        validate(h, p); next = h->next(h->context, p);
        if (!(h->owner(h->context, p)->flags & MM_FILE_MARK) && h->references(h->context, p)->count == 0)
            h->destroy(h->context, p);
    }
    h->leave_list(h->context);
}
void mm_file_ops_shutdown(const MMFileOpsHost *h)
{
    void *p;
    h->enter_list(h->context);
    for (p = h->first(h->context); p; p = h->next(h->context, p)) {
        validate(h, p); close_owner(h, p, 1);
    }
    h->leave_list(h->context);
}
int mm_file_ops_contains(const MMFileOpsHost *h, void *object)
{
    void *p;
    h->enter_list(h->context);
    for (p = h->first(h->context); p && p != object; p = h->next(h->context, p)) {}
    h->leave_list(h->context); return p != NULL;
}
