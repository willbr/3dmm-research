/* HQ handle policy converted from Kauai memwin.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "mm/handle_memory.h"
#include <stdlib.h>
#include <string.h>
static MMI32 bits(MMU32 v) { return v <= 0x7fffffffUL ? (MMI32)v : -(MMI32)(~v) - 1; }
static void address(const MMHandleLayout *l, const MMHandleHost *h, const void *p, size_t n)
{ if (l->debug && h && h->address) h->address(h->context, p, n); }
static void check(const MMHandleLayout *l, const MMHandleHost *h, int ok, int code, const void *p, size_t n)
{ if (l->debug && !ok && h && h->assertion) h->assertion(h->context, code, p, n); }
static void range(const MMHandleLayout *l, const MMHandleHost *h, MMI32 n)
{
    check(l, h, n >= 0, MM_HANDLE_SMALL, NULL, 0);
    check(l, h, n < (MMI32)l->limit, MM_HANDLE_BIG, NULL, 0);
}
static void failed(const MMHandleHost *h) { if (h && h->failed) h->failed(h->context); }
static void *raw(const MMHandleLayout *l, void *p) { return (unsigned char *)p - l->prefix; }
static MMHandleState *state(const MMHandleLayout *l, void *p)
{ return l->debug ? &((MMHandleDebugRecord *)p)->state : (MMHandleState *)p; }
static int total(const MMHandleLayout *l, MMU32 n, MMU32 *out)
{
    if (l->prefix > 0x7fffffffUL || n > 0x7fffffffUL - l->prefix) return 0;
    *out = n + l->prefix; return 1;
}
void mm_handle_validate(const MMHandleLayout *l, const MMHandleHost *h, void *p)
{
    void *r;
    MMHandleDebugRecord *record;
    if (!l->debug) return;
    if (!p) { check(l, h, 0, MM_HANDLE_NIL, NULL, 0); return; }
    if (!l->expensive) return;
    r = raw(l, p); record = (MMHandleDebugRecord *)r;
    if (record->magic != l->magic) { check(l, h, 0, MM_HANDLE_MAGIC, r, l->prefix); return; }
    range(l, h, bits((MMU32)record->state.locks));
    if (h && h->validate_fixed) h->validate_fixed(h->context, r, bits((MMU32)record->state.bytes + l->prefix));
}
int mm_handle_allocate(const MMHandleLayout *l, const MMHandleHost *h, void **owner, MMU32 n, MMU32 flags, MMI32 priority)
{
    void *r = NULL;
    MMHandleState *s;
    MMU32 bytes;
    int ok;
    address(l, h, owner, sizeof(*owner)); range(l, h, bits(n));
    if (!total(l, n, &bytes)) goto fail;
    if (h && h->allocate) ok = h->allocate(h->context, &r, bytes, flags, priority);
    else { r = flags & 1 ? calloc(1, bytes ? bytes : 1) : malloc(bytes ? bytes : 1); ok = r != NULL; }
    if (!ok) goto fail;
    s = state(l, r); s->bytes = bits(n); s->locks = 0;
    if (l->debug) ((MMHandleDebugRecord *)r)->magic = l->magic;
    *owner = (unsigned char *)r + l->prefix;
    mm_handle_validate(l, h, *owner); return 1;
fail:
    failed(h); *owner = NULL; return 0;
}
int mm_handle_resize(const MMHandleLayout *l, const MMHandleHost *h, void **owner, MMU32 n, MMU32 flags, MMI32 priority)
{
    void *r, *next;
    MMHandleState *s;
    MMU32 bytes, old_bytes;
    int ok;
    address(l, h, owner, sizeof(*owner)); mm_handle_validate(l, h, *owner); range(l, h, bits(n));
    r = raw(l, *owner); s = state(l, r);
    if (s->locks > 0) { check(l, h, 0, MM_HANDLE_RESIZE_LOCKED, NULL, 0); failed(h); return 0; }
    if (!total(l, n, &bytes) || !total(l, (MMU32)s->bytes, &old_bytes)) goto fail;
    if (h && h->resize) ok = h->resize(h->context, &r, bytes, old_bytes, flags, priority);
    else {
        next = realloc(r, bytes ? bytes : 1); ok = next != NULL;
        if (ok) {
            r = next;
            if ((flags & 1) && bytes > old_bytes) memset((unsigned char *)r + old_bytes, 0, bytes - old_bytes);
        }
    }
    if (!ok) goto fail;
    state(l, r)->bytes = bits(n); *owner = (unsigned char *)r + l->prefix;
    mm_handle_validate(l, h, *owner); return 1;
fail:
    failed(h); mm_handle_validate(l, h, *owner); return 0;
}
void mm_handle_free(const MMHandleLayout *l, const MMHandleHost *h, void **owner)
{
    void *r;
    address(l, h, owner, sizeof(*owner));
    if (!*owner) return;
    mm_handle_validate(l, h, *owner); r = raw(l, *owner); *owner = NULL;
    check(l, h, state(l, r)->locks == 0, MM_HANDLE_FREE_LOCKED, NULL, 0);
    if (h && h->release) h->release(h->context, &r); else free(r);
}
MMI32 mm_handle_size(const MMHandleLayout *l, const MMHandleHost *h, void *p)
{ mm_handle_validate(l, h, p); return bits((MMU32)state(l, raw(l, p))->bytes); }
void *mm_handle_pointer(const MMHandleLayout *l, const MMHandleHost *h, void *p)
{ mm_handle_validate(l, h, p); return p; }
void *mm_handle_lock(const MMHandleLayout *l, const MMHandleHost *h, void *p)
{
    MMHandleState *s;
    mm_handle_validate(l, h, p); s = state(l, raw(l, p)); s->locks = bits((MMU32)s->locks + 1);
    check(l, h, s->locks > 0, MM_HANDLE_LOCK_OVERFLOW, NULL, 0); return p;
}
void mm_handle_unlock(const MMHandleLayout *l, const MMHandleHost *h, void *p)
{
    MMHandleState *s;
    mm_handle_validate(l, h, p); s = state(l, raw(l, p));
    check(l, h, s->locks > 0, MM_HANDLE_NOT_LOCKED, NULL, 0);
    if (s->locks > 0) --s->locks;
}
int mm_handle_copy(const MMHandleLayout *l, const MMHandleHost *h, void *source, void **destination, MMI32 priority)
{
    MMI32 bytes;
    void *from, *to;
    mm_handle_validate(l, h, source); address(l, h, destination, sizeof(*destination));
    bytes = mm_handle_size(l, h, source);
    if (!mm_handle_allocate(l, h, destination, (MMU32)bytes, 0, priority)) return 0;
    from = mm_handle_pointer(l, h, source); to = mm_handle_pointer(l, h, *destination);
    range(l, h, bytes); address(l, h, from, (size_t)bytes); address(l, h, to, (size_t)bytes);
    memcpy(to, from, (size_t)bytes); return 1;
}
void mm_handle_mark(const MMHandleLayout *l, const MMHandleHost *h, void *p)
{
    if (!l->debug || !p) return;
    mm_handle_validate(l, h, p);
    if (h && h->mark_fixed) h->mark_fixed(h->context, raw(l, p));
}
