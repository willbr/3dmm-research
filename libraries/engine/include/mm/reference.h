/* Reference counting converted from Kauai base.cpp and file.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef MM_ENGINE_REFERENCE_H
#define MM_ENGINE_REFERENCE_H
#include "types.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct MMReference { MMI32 count; } MMReference;
/* Fresh owners start with one reference. A cached/marked owner can be retained
 * from zero. Valid counts do not overflow signed 32-bit storage; arithmetic is
 * defined modulo 32 bits for diagnostic callers. These operations are not atomic.
 * The borrowed state must remain live until a terminal destroy callback. */
void mm_reference_init(MMReference *);
MMI32 mm_reference_add(MMReference *);
MMI32 mm_reference_drop(MMReference *);
/* Callbacks are synchronous and do not unwind through C. Destroy may free the
 * object and its counter; release never accesses either afterward. Ordinary
 * release requires a positive count. Marked-file release diagnoses a nonpositive
 * count before clamping it to zero, without destroying that owner. An issue
 * callback must leave the owner live. A marked zero-count file stays open. */
void mm_reference_release(MMReference *,void *object,void (*destroy)(void *));
void mm_reference_release_marked(MMReference *,int marked,void *object,
    void (*issue)(void *),void (*destroy)(void *));
#ifdef __cplusplus
}
#endif
#endif
