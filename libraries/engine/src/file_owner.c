/* File lifecycle and I/O policy converted from Kauai file.cpp/file_native.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "mm/file_owner.h"
#include "mm/wide.h"
#include <stddef.h>
static void enter(const MMFileOwnerHost *h) { if (h->enter) h->enter(h->context); }
static void leave(const MMFileOwnerHost *h) { if (h->leave) h->leave(h->context); }
static void validate(const MMFileOwnerHost *h, int base) { if (h->validate) h->validate(h->context, base); }
static void issue(const MMFileOwnerHost *h, int code) { if (h->issue) h->issue(h->context, code); }
static void assertion(const MMFileOwnerHost *h, int valid, int code)
{ if (!valid && h->assertion) h->assertion(h->context, code); }
static void arguments(const MMFileOwnerHost *h, int op, const void *p, MMI32 bytes, MMI32 position)
{ if (h->arguments) h->arguments(h->context, op, p, bytes, position); }
void mm_file_owner_init(MMFileOwner *s, MMU32 flags)
{
    s->flags = flags; s->error = MM_FILE_ERROR_NONE;
    s->open = s->ever_open = s->wrote = 0; s->handle = NULL;
}
int mm_file_owner_open(MMFileOwner *s, const MMFileOwnerHost *h, int create, MMU32 flags)
{
    int result = 0;
    validate(h, 1); enter(h);
    if (s->error >= MM_FILE_ERROR_CRITICAL) goto done;
    flags &= MM_FILE_PERMISSIONS;
    if (s->open) {
        assertion(h, !create, MM_FILE_CHECK_CREATE);
        if (!(~s->flags & flags)) { result = 1; goto done; }
        h->close(h->context, s->handle); s->handle = NULL;
        flags |= s->flags & MM_FILE_PERMISSIONS;
    }
    s->handle = h->open(h->context, create, flags | (s->flags & MM_FILE_TEMP));
    if (!s->handle) {
        if (s->open) {
            s->handle = h->open(h->context, 0, s->flags);
            if (s->handle) goto done;
        }
        s->open = 0; s->error = MM_FILE_ERROR_CRITICAL; goto done;
    }
    s->open = s->ever_open = 1;
    s->flags = (s->flags & ~MM_FILE_PERMISSIONS) | flags;
    result = 1;
done:
    leave(h); return result;
}
void mm_file_owner_flush(MMFileOwner *s, const MMFileOwnerHost *h)
{
    validate(h, 0); enter(h);
    if (s->open) h->flush(h->context, s->handle);
    leave(h);
}
void mm_file_owner_close(MMFileOwner *s, const MMFileOwnerHost *h, int final)
{
    validate(h, 1); enter(h);
    if (s->open) {
        mm_file_owner_flush(s, h); h->close(h->context, s->handle);
        s->open = 0; s->handle = NULL;
    }
    if ((s->flags & MM_FILE_TEMP) && final && s->ever_open && !h->remove(h->context))
        issue(h, MM_FILE_ISSUE_DELETE);
    leave(h);
}
void mm_file_owner_seek(MMFileOwner *s, const MMFileOwnerHost *h, MMI32 position)
{
    validate(h, 0);
    if (!s->open) mm_file_owner_open(s, h, 0, s->flags);
    if (s->error < MM_FILE_ERROR_SEEK && !h->seek(h->context, s->handle, (MMU32)position)) {
        issue(h, MM_FILE_ISSUE_GENERAL); s->error = MM_FILE_ERROR_SEEK;
    }
}
int mm_file_owner_resize(MMFileOwner *s, const MMFileOwnerHost *h, MMI32 bytes)
{
    int result;
    validate(h, 0); arguments(h, MM_FILE_ARGUMENT_RESIZE, NULL, 0, bytes); enter(h);
    assertion(h, (s->flags & MM_FILE_WRITE) != 0, MM_FILE_CHECK_WRITE);
    mm_file_owner_seek(s, h, bytes);
    if (s->error < MM_FILE_ERROR_WRITE) {
        s->wrote = 1;
        if (!h->resize(h->context, s->handle, (MMU32)bytes)) {
            issue(h, MM_FILE_ISSUE_GENERAL); s->error = MM_FILE_ERROR_WRITE;
        }
    }
    result = s->error < MM_FILE_ERROR_WRITE;
    leave(h); return result;
}
MMU32 mm_file_owner_size(MMFileOwner *s, const MMFileOwnerHost *h)
{
    MMU32 length = 0;
    validate(h, 0); enter(h);
    if (!s->open) mm_file_owner_open(s, h, 0, s->flags);
    if (s->error < MM_FILE_ERROR_SEEK && !h->size(h->context, s->handle, &length)) {
        issue(h, MM_FILE_ISSUE_GENERAL); s->error = MM_FILE_ERROR_SEEK;
    }
    if (s->error >= MM_FILE_ERROR_SEEK) length = 0;
    leave(h); return length;
}
int mm_file_owner_read(MMFileOwner *s, const MMFileOwnerHost *h, void *p, MMI32 bytes, MMI32 position)
{
    int result = 0;
    MMI32 available = 0;
    validate(h, 0); arguments(h, MM_FILE_ARGUMENT_READ, p, bytes, position);
    if (bytes <= 0) return 1;
    enter(h);
    if (!s->open) mm_file_owner_open(s, h, 0, s->flags);
    if (h->debug_read) available = mm_signed_word(mm_file_owner_size(s, h) - (MMU32)position);
    mm_file_owner_seek(s, h, position);
    if (s->error >= MM_FILE_ERROR_READ) goto done;
    if (h->debug_read) assertion(h, available >= bytes, MM_FILE_CHECK_EOF);
    if (!h->read(h->context, s->handle, p, (MMU32)bytes)) {
        issue(h, MM_FILE_ISSUE_GENERAL); s->error = MM_FILE_ERROR_READ; goto done;
    }
    result = 1;
done:
    leave(h); return result;
}
int mm_file_owner_write(MMFileOwner *s, const MMFileOwnerHost *h, const void *p, MMI32 bytes, MMI32 position)
{
    int result = 0;
    validate(h, 0); arguments(h, MM_FILE_ARGUMENT_WRITE, p, bytes, position);
    if (bytes <= 0) return 1;
    enter(h);
    assertion(h, (s->flags & MM_FILE_WRITE) != 0, MM_FILE_CHECK_WRITE);
    if (!s->open) mm_file_owner_open(s, h, 0, s->flags);
    mm_file_owner_seek(s, h, position);
    if (s->error >= MM_FILE_ERROR_WRITE) goto done;
    s->wrote = 1;
    if (!h->write(h->context, s->handle, p, (MMU32)bytes)) {
        issue(h, MM_FILE_ISSUE_GENERAL); s->error = MM_FILE_ERROR_WRITE; goto done;
    }
    result = 1;
done:
    leave(h); return result;
}
int mm_file_owner_set_flags(MMFileOwner *s, const MMFileOwnerHost *h, MMU32 flags, MMU32 mask)
{
    int result = 0;
    validate(h, 0); flags &= mask; enter(h);
    if ((~s->flags & flags & MM_FILE_PERMISSIONS) && !mm_file_owner_open(s, h, 0, flags)) {
        issue(h, MM_FILE_ISSUE_PERMISSION); goto done;
    }
    if (flags & MM_FILE_MARK) s->flags |= MM_FILE_MARK;
    if (mask & MM_FILE_TEMP) {
        if (flags & MM_FILE_TEMP) s->flags |= MM_FILE_TEMP;
        else s->flags &= ~MM_FILE_TEMP;
    }
    result = 1;
done:
    leave(h); return result;
}
void mm_file_owner_set_temp(MMFileOwner *s, const MMFileOwnerHost *h, int temporary)
{
    validate(h, 0); enter(h);
    if (temporary) s->flags |= MM_FILE_TEMP; else s->flags &= ~MM_FILE_TEMP;
    leave(h);
}
