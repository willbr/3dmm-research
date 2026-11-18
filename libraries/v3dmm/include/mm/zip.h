/* Licensed under the MIT License. */
#ifndef MM_ZIP_H
#define MM_ZIP_H
#include "mm/types.h"
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct MMZip MMZip;
typedef struct MMZipSize { MMU32 low, high; } MMZipSize;
typedef struct MMZipInput
{
    void *context;
    MMZipSize length;
    int (*read)(void *, MMZipSize offset, void *, size_t);
} MMZipInput;
typedef enum MMZipResult { MM_ZIP_OK, MM_ZIP_INVALID, MM_ZIP_MEMORY } MMZipResult;
typedef struct MMZipMember
{
    MMZipSize bytes;
    MMU32 crc;
    int directory, encrypted, supported;
} MMZipMember;
typedef int (*MMZipSink)(void *, MMZipSize offset, const void *, size_t);
/* A successful open borrows the input context until close. */
MMZipResult mm_zip_open(const MMZipInput *, MMZip **);
void mm_zip_close(MMZip *);
MMU32 mm_zip_count(MMZip *);
int mm_zip_member(MMZip *, MMU32 slot, MMZipMember *);
/* Return the required length with the terminator, or zero for an invalid slot. */
size_t mm_zip_name(MMZip *, MMU32 slot, char *buffer, size_t capacity);
int mm_zip_extract(MMZip *, MMU32 slot, MMZipSink, void *context);
int mm_zip_extract_memory(MMZip *, MMU32 slot, void *bytes, size_t capacity);

typedef struct MMZipOutput
{
    MMU32 expected, written, crc;
    void *context;
    int (*append)(void *, const void *, size_t);
} MMZipOutput;
/* For a size-bounded extraction, including one performed by a legacy decoder.
 * The append callback can stop extraction on cancellation or I/O failure. */
void mm_zip_output_init(MMZipOutput *, MMU32 expected, int (*append)(void *, const void *, size_t), void *context);
int mm_zip_output_write(void *, MMZipSize offset, const void *, size_t);
int mm_zip_output_complete(const MMZipOutput *, MMU32 expected_crc);

#ifdef __cplusplus
}
#endif
#endif
