#ifndef THREE_MM_ENGINE_TEXT_H
#define THREE_MM_ENGINE_TEXT_H
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
        THREE_MM_TEXT_MAC = 0x0202,
        THREE_MM_TEXT_WINDOWS = 0x0303,
        THREE_MM_TEXT_UTF16_BE = 0x0404,
        THREE_MM_TEXT_UTF16_LE = 0x0505
    };
    /* Optional native code-page services. Null output queries the count. Callbacks
     * must reject short buffers without writing. UTF-16 units are native endian.
     * No callback is used for fixed legacy byte-to-byte maps or UTF-16 byte order. */
    typedef struct ThreeMMTextHost
    {
        void *context;
        size_t (*decode)(void *, const void *, size_t, int, unsigned short *, size_t);
        size_t (*encode)(void *, const unsigned short *, size_t, char *, size_t);
    } ThreeMMTextHost;
    size_t three_mm_text_utf16(const ThreeMMTextHost *, const void *, size_t, int, unsigned short *, size_t);
    size_t three_mm_text_ansi(const ThreeMMTextHost *, const void *, size_t, int, char *, size_t);
    /* Legacy lossy Mac/Windows maps; supports in-place counted conversion. */
    void three_mm_text_map_bytes(void *, size_t, int mac_to_windows);
    void three_mm_text_case_bytes(char *, size_t, int upper);
    void three_mm_text_case_utf16(unsigned short *, size_t, int upper);
    int three_mm_text_compare(const void *, size_t, const void *, size_t, int wide, int ignore_case);
#ifdef __cplusplus
}
#endif
#endif
