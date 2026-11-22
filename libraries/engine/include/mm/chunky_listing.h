#ifndef MM_ENGINE_CHUNKY_LISTING_H
#define MM_ENGINE_CHUNKY_LISTING_H
#include "read_stream.h"
#ifdef __cplusplus
extern "C"
{
#endif

#define MM_CHUNKY_LISTING_LINE_LIMIT 255

    /* A line borrows byte-valued text for the duration of the callback.
     * Bytes above ASCII are preserved for the host's native text sink. */
    typedef void (*MMChunkyWriteLine)(void *context, const unsigned char *text, size_t length);

    /* Emit a BYTE directive and rows of at most eight bytes. An empty stream
     * emits only the directive. A read error can leave a partial listing. */
    int mm_chunky_list_bytes(const MMReadStream *stream, size_t indent, MMChunkyWriteLine write_line, void *context);

#ifdef __cplusplus
}
#endif
#endif
