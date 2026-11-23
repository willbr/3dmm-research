/* Native text services converted from Kauai platform_text.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "mm/native_text.h"

#if defined(_WIN32) && !defined(KAUAI_PORTABLE_TEXT)
#define NOMINMAX
#include <windows.h>

static size_t decode(void *context, const void *input, size_t bytes, int source,
                     unsigned short *output, size_t capacity)
{
    const UINT page = source == MM_TEXT_WINDOWS ? CP_ACP : CP_MACCP;
    const DWORD flags = page == CP_ACP && GetACP() == CP_UTF8 ? 0 : MB_PRECOMPOSED;
    int needed;
    (void)context;
    if (!bytes || bytes > INT_MAX)
        return 0;
    needed = MultiByteToWideChar(page, flags, input, (int)bytes, NULL, 0);
    if (!needed || !output)
        return (size_t)needed;
    if (capacity < (size_t)needed)
        return 0;
    return (size_t)MultiByteToWideChar(page, flags, input, (int)bytes, output, needed);
}

static size_t encode(void *context, const unsigned short *input, size_t count, char *output, size_t capacity)
{
    int needed;
    (void)context;
    if (!count || count > INT_MAX)
        return 0;
    needed = WideCharToMultiByte(CP_ACP, 0, input, (int)count, NULL, 0, NULL, NULL);
    if (!needed || !output)
        return (size_t)needed;
    if (capacity < (size_t)needed)
        return 0;
    return (size_t)WideCharToMultiByte(CP_ACP, 0, input, (int)count, output, needed, NULL, NULL);
}

static int compare(void *context, const void *a, size_t na, const void *b, size_t nb, int wide, int ignore_case)
{
    const DWORD flags = ignore_case ? NORM_IGNORECASE : 0;
    (void)context;
    return wide ? CompareStringW(LOCALE_USER_DEFAULT, flags, a, (int)na, b, (int)nb) :
                  CompareStringA(LOCALE_USER_DEFAULT, flags, a, (int)na, b, (int)nb);
}

static const MMTextHost native_text = {NULL, decode, encode};
static const MMTextCompareHost native_compare = {NULL, compare};
static INIT_ONCE case_once = INIT_ONCE_STATIC_INIT;
static MMTextCaseMaps case_maps;

static void map_bytes(void *context, char *text, size_t count, int upper)
{
    (void)context;
    mm_native_text_case_bytes(text, count, upper);
}

static BOOL CALLBACK initialize_maps(PINIT_ONCE once, PVOID parameter, PVOID *context)
{
    (void)once;
    (void)parameter;
    (void)context;
    mm_text_case_maps_init(&case_maps, NULL, map_bytes);
    return TRUE;
}

const MMTextHost *mm_native_text_host(void) { return &native_text; }

int mm_native_text_compare(const void *a, size_t na, const void *b, size_t nb, int wide, int ignore_case)
{
    return mm_text_compare_host(&native_compare, a, na, b, nb, wide, ignore_case);
}

void mm_native_text_case_bytes(char *text, size_t count, int upper)
{
    if (upper)
        CharUpperBuffA(text, (DWORD)count);
    else
        CharLowerBuffA(text, (DWORD)count);
}

void mm_native_text_case_utf16(unsigned short *text, size_t count, int upper)
{
    if (upper)
        CharUpperBuffW(text, (DWORD)count);
    else
        CharLowerBuffW(text, (DWORD)count);
}

void mm_native_text_case_mapped(char *text, size_t count, int upper)
{
    InitOnceExecuteOnce(&case_once, initialize_maps, NULL, NULL);
    mm_text_case_maps_apply(&case_maps, text, count, upper);
}

#else

const MMTextHost *mm_native_text_host(void) { return NULL; }

int mm_native_text_compare(const void *a, size_t na, const void *b, size_t nb, int wide, int ignore_case)
{
    return mm_text_compare_host(NULL, a, na, b, nb, wide, ignore_case);
}

void mm_native_text_case_bytes(char *text, size_t count, int upper)
{
    mm_text_case_bytes(text, count, upper);
}

void mm_native_text_case_utf16(unsigned short *text, size_t count, int upper)
{
    mm_text_case_utf16(text, count, upper);
}

void mm_native_text_case_mapped(char *text, size_t count, int upper)
{
    mm_text_case_bytes(text, count, upper);
}

#endif
