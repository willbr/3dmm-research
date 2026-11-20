#ifndef MM_ENGINE_TEXT_H
#define MM_ENGINE_TEXT_H
#include "types.h"
#include <stddef.h>
#if USHRT_MAX != 65535
#error Legacy text requires sixteen-bit unsigned short units
#endif
#ifdef __cplusplus
extern "C"
{
#endif
    enum
    {
        MM_TEXT_MAC = 0x0202,
        MM_TEXT_WINDOWS = 0x0303,
        MM_TEXT_UTF16_BE = 0x0404,
        MM_TEXT_UTF16_LE = 0x0505
    };
    /* Optional native code-page services. Null output queries the count. Callbacks
     * must reject short buffers without writing. UTF-16 units are native endian.
     * No callback is used for fixed legacy byte-to-byte maps or UTF-16 byte order. */
    typedef struct MMTextHost
    {
        void *context;
        size_t (*decode)(void *, const void *, size_t, int, unsigned short *, size_t);
        size_t (*encode)(void *, const unsigned short *, size_t, char *, size_t);
    } MMTextHost;
    size_t mm_text_utf16(const MMTextHost *, const void *, size_t, int, unsigned short *, size_t);
    size_t mm_text_ansi(const MMTextHost *, const void *, size_t, int, char *, size_t);
    /* Strict counted UTF-8/UTF-16 conversion. Each successful write receives
     * one scalar in the destination encoding. Embedded zero is data. Invalid
     * input returns 0 and keeps the valid prefix. The sink must not throw and
     * any nonpositive sink result stops conversion and is returned unchanged. */
    int mm_text_encode_utf8(const unsigned short *, size_t, void *, int (*write)(void *, const char *, size_t));
    int mm_text_decode_utf8(const char *, size_t, void *, int (*write)(void *, const unsigned short *, size_t));
    /* Legacy lossy Mac/Windows maps; supports in-place counted conversion. */
    void mm_text_map_bytes(void *, size_t, int mac_to_windows);
    void mm_text_case_bytes(char *, size_t, int upper);
    void mm_text_case_utf16(unsigned short *, size_t, int upper);
    int mm_text_compare(const void *, size_t, const void *, size_t, int wide, int ignore_case);
    typedef struct MMTextCompareHost {
        void *context;
        /* Ranked result: 0 failure, 1 less, 2 equal, 3 greater. */
        int (*compare)(void *, const void *, size_t, const void *, size_t, int wide, int ignore_case);
    } MMTextCompareHost;
    /* Empty counts precede the native signed-32-bit count limit. Null host uses
     * portable collation. Return -1/0/+1, or 2 on native failure/count overflow. */
    int mm_text_compare_host(const MMTextCompareHost *, const void *, size_t, const void *, size_t, int wide, int ignore_case);
    typedef struct MMTextCaseMaps { char upper[256], lower[256]; } MMTextCaseMaps;
    /* Host owns initialization synchronization/publication. Map callbacks run
     * upper then lower after both identity arrays are filled. Null uses portable
     * case rules. Mapping a whole multibyte buffer remains a distinct operation. */
    void mm_text_case_maps_init(MMTextCaseMaps *, void *context, void (*map)(void *, char *, size_t, int upper));
    void mm_text_case_maps_apply(const MMTextCaseMaps *, char *, size_t, int upper);
#ifdef __cplusplus
}
#endif
#endif
