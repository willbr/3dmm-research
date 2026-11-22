#ifndef MM_ENGINE_CHUNKY_LISTING_H
#define MM_ENGINE_CHUNKY_LISTING_H
#include "chunky_file.h"
#include "read_stream.h"
#include "string_table.h"
#ifdef __cplusplus
extern "C"
{
#endif

#define MM_CHUNKY_LISTING_LINE_LIMIT 255

    /* Lines borrow 1-byte or native unsigned-short text for the callback.
     * Length counts units. Byte listings preserve comment bytes above ASCII. */
    typedef void (*MMChunkyWriteLine)(void *context, const void *text, size_t length, unsigned unit);

    /* Emit a BYTE directive and rows of at most eight bytes. An empty stream
     * emits only the directive. A read error can leave a partial listing. */
    int mm_chunky_list_bytes(const MMReadStream *stream, size_t indent, MMChunkyWriteLine write_line, void *context);
    void mm_chunky_list_pack(MMU32 format, MMChunkyWriteLine write_line, void *context);
    void mm_chunky_list_header(const MMChunkEntry *, const void *name, MMU32 name_length, unsigned unit,
                              MMChunkyWriteLine, void *context);
    void mm_chunky_list_adopt(const MMChunkEntry *, const MMChunkChild *, MMChunkyWriteLine, void *context);

    typedef enum MMChunkyListKind
    {
        MM_CHUNKY_LIST_NONE,
        MM_CHUNKY_LIST_GL,
        MM_CHUNKY_LIST_AL,
        MM_CHUNKY_LIST_GG,
        MM_CHUNKY_LIST_AG,
        MM_CHUNKY_LIST_GST,
        MM_CHUNKY_LIST_AST
    } MMChunkyListKind;
    MMChunkyListKind mm_chunky_list_kind(MMU32 tag);

    typedef struct MMChunkyCollectionMode
    {
        int foreign_order, system;
    } MMChunkyCollectionMode;
    typedef struct MMChunkyCollectionListing
    {
        void *context;
        MMChunkyWriteLine write_line;
        MMStringConvert convert_string;
        unsigned native_unit;
        int native_system, windows_system, windows_is_native;
    } MMChunkyCollectionListing;

    /* Mode initially holds {0, native_system}. Input is the decoded payload;
     * packed and format describe its original storage. Return MMStorageResult.
     * A failed read or invalid collection emits nothing and leaves mode intact. */
    int mm_chunky_list_collection(const MMReadStream *, MMChunkyListKind, int packed, MMU32 format,
                                  const MMChunkyCollectionListing *, MMChunkyCollectionMode *);

#ifdef __cplusplus
}
#endif
#endif
