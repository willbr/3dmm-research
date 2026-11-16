/* Error stack converted from Kauai utilerro.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef MM_ENGINE_ERROR_STACK_H
#define MM_ENGINE_ERROR_STACK_H
#include "types.h"
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
enum { MM_ERROR_CAPACITY = 20 };
typedef struct MMErrorRecord { MMI32 code; const char *source; MMI32 line; } MMErrorRecord;
typedef struct MMErrorStack { MMI32 count; MMI32 entries[MM_ERROR_CAPACITY]; } MMErrorStack;
typedef struct MMErrorStackDebug { MMI32 count; MMErrorRecord entries[MM_ERROR_CAPACITY]; } MMErrorStackDebug;
typedef struct MMErrorStackView { MMI32 *count; void *entries; size_t stride; int debug; } MMErrorStackView;
typedef struct MMErrorStackHost {
    void *context;
    void (*enter)(void *);
    void (*leave)(void *);
    void (*report_push)(void *, MMI32 code, const char *, MMI32 line);
    void (*report_overflow)(void *);
    /* Optional native output binding. Otherwise pop's output is MMI32*. */
    void (*write)(void *, void *output, MMI32 value);
    void (*underflow)(void *, void *output);
} MMErrorStackHost;
MMErrorStackView mm_error_stack_view(MMErrorStack *);
MMErrorStackView mm_error_stack_debug_view(MMErrorStackDebug *);
/* Use factory views without changing their layout fields. Caller owns records,
 * synchronization and borrowed source-file strings. No
 * allocation occurs. Init/clear change only count; inactive entries stay intact.
 * Report every push before locking, suppress only adjacent duplicate codes,
 * diagnose overflow while full/locked, then discard the oldest entry.
 * Pop writes/trashes output before unlocking; underflow leaves it unchanged if
 * no underflow callback is supplied. Invalid indexed reads return -1. Count
 * retains its unsynchronized contract. */
void mm_error_stack_init(const MMErrorStackView *);
void mm_error_stack_push(const MMErrorStackView *, const MMErrorStackHost *, MMI32, const char *, MMI32);
int mm_error_stack_pop(const MMErrorStackView *, const MMErrorStackHost *, void *output);
void mm_error_stack_clear(const MMErrorStackView *, const MMErrorStackHost *);
MMI32 mm_error_stack_count(const MMErrorStackView *);
MMI32 mm_error_stack_get(const MMErrorStackView *, const MMErrorStackHost *, MMI32 index);
int mm_error_stack_contains(const MMErrorStackView *, const MMErrorStackHost *, MMI32 code);
void mm_error_stack_flush(const MMErrorStackView *, const MMErrorStackHost *, MMI32 code);
#ifdef __cplusplus
}
#endif
#endif
