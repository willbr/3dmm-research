#ifndef MM_ENGINE_NATIVE_TEXT_H
#define MM_ENGINE_NATIVE_TEXT_H
#include "text.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Windows borrows the system code pages and user collation. A null host
 * selects the engine's portable encoding tables. */
const MMTextHost *mm_native_text_host(void);
int mm_native_text_compare(const void *, size_t, const void *, size_t, int wide, int ignore_case);
void mm_native_text_case_bytes(char *, size_t, int upper);
void mm_native_text_case_utf16(unsigned short *, size_t, int upper);
/* Use the cached legacy byte map, including in multibyte code pages. */
void mm_native_text_case_mapped(char *, size_t, int upper);

#ifdef __cplusplus
}
#endif
#endif
