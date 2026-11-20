/* Stored rich-text and text-box persistence over typed file/collection services. */
#ifndef MM_ENGINE_TEXT_STORE_H
#define MM_ENGINE_TEXT_STORE_H
#include "collection_storage.h"
#include "movie_title.h"
#include "read_stream.h"
#ifdef __cplusplus
extern "C"
{
#endif
    enum
    {
        MM_TEXT_LIMIT = 0x08000000,
        MM_TEXT_ROOT = 0x52545854U,
        MM_TEXT_BOX = 0x54424f58U,
        MM_TEXT_BYTES = 0x54455854U,
        MM_TEXT_PROPERTIES = 0x474c4d50U,
        MM_TEXT_ARGUMENTS = 0x41475041U
    };
#define MM_TEXT_NIL 0xffffffffUL
    typedef struct MMTextChunk
    {
        MMU32 type, number;
    } MMTextChunk;
    typedef struct MMTextBoxState
    {
        MMI32 first, end, current, left, top, right, bottom;
        MMU32 child;
        int story;
    } MMTextBoxState;
    typedef struct MMTextBoxServices
    {
        void *context;
        int (*read)(void *, MMU32, MMReadStream *);
        int (*child)(void *, MMU32, MMU32, MMTextChunk *);
        int (*put)(void *, MMU32, const unsigned char bytes[36]);
    } MMTextBoxServices;
    int mm_text_box_decode(const MMReadStream *, MMTextBoxState *);
    int mm_text_box_encode(const MMTextBoxState *, int system, int big, unsigned char bytes[36]);
    int mm_text_box_read(MMTextBoxState *, MMU32, MMTextChunk *optional_child, const MMTextBoxServices *);
    int mm_text_box_write(const MMTextBoxState *, MMU32, int system, int big, const MMTextBoxServices *);
    typedef struct MMRichTextDefaults
    {
        int order, font_system;
        MMI32 width, height;
        MMU32 background;
    } MMRichTextDefaults;
    typedef struct MMTextLocation
    {
        void *file;
        MMU32 offset;
        MMI32 bytes;
    } MMTextLocation;
    /* Initialize before use. Owns the text-file reference and both collections;
     * do not duplicate owners by copying this record. Dispose releases handles
     * without clearing scalar metadata. Counted font units are native 1/2-byte text. */
    typedef struct MMRichTextState
    {
        MMRichTextDefaults defaults;
        MMMovieTitle font;
        MMTextLocation text;
        void *properties, *arguments;
        int text_system, property_order, property_system, argument_order, argument_system;
    } MMRichTextState;
    typedef struct MMRichTextObjects
    {
        void *context;
        void (*retain_file)(void *, void *);
        void (*release)(void *, MMU32 kind, void *);
        MMI32 (*width)(void *, MMU32 kind, void *);
    } MMRichTextObjects;
    typedef struct MMRichTextReadServices
    {
        void *context;
        MMRichTextObjects objects;
        int (*root)(void *, MMU32 type, MMU32 number, MMReadStream *);
        int (*font)(void *, MMMovieTitle *);
        int (*child)(void *, MMU32 type, MMU32 number, MMU32 kind, MMU32 *);
        int (*location)(void *, MMU32 type, MMU32 number, MMTextLocation *borrowed);
        int (*read_text)(void *, const MMTextLocation *, void *, MMU32 bytes);
        int (*group)(void *, MMU32 kind, MMU32 number, void **owned, int *order, int *system);
    } MMRichTextReadServices;
    typedef struct MMRichTextWriteServices
    {
        void *context;
        MMRichTextObjects objects;
        MMU32 (*font_bytes)(void *, const MMMovieTitle *);
        int (*add_root)(void *, MMU32 bytes, MMU32 *number);
        int (*write)(void *, MMU32 offset, const void *, MMU32 bytes);
        int (*font)(void *, const MMMovieTitle *);
        void (*remove)(void *, MMU32 number);
        int (*add_child)(void *, MMU32 root, MMU32 kind, MMU32 bytes);
        int (*destination)(void *, MMTextLocation *owned);
        int (*copy)(void *, const MMTextLocation *source, const MMTextLocation *destination);
        MMU32 (*group_bytes)(void *, MMU32 kind, void *);
        MMI32 (*count)(void *, void *arguments);
        int (*group)(void *, MMU32 kind, void *, int order, int system);
    } MMRichTextWriteServices;
    int mm_rich_text_init(MMRichTextState *, unsigned unit, int system);
    int mm_rich_text_decode(const MMReadStream *, MMRichTextDefaults *);
    int mm_rich_text_encode(const MMRichTextDefaults *, int big, unsigned char bytes[16]);
    void mm_rich_text_dispose(MMRichTextState *, const MMRichTextObjects *);
    /* Synchronous services return 1/0, -1 for allocation exceptions, -2 for other
     * exceptions. Borrowed locations are ignored on failure; groups/destinations
     * returned through owned outputs are released even on failure. MMReadStream
     * callbacks follow their boolean contract; adapters retain native exceptions
     * until the C operation returns. No callback throws
     * across C. Reads commit all fields/owners together after releasing the old owners;
     * failures release only the temporary state. Empty/unreadable fonts fall back to
     * empty native text and the caller's system. TEXT's raw system and collection
     * byte order/encoding are retained. No property/content normalization occurs.
     * Write validation precedes output changes. Root creation publishes RTXT before
     * allocation and nil on add/write failure. Later failure removes the partial tree
     * and publishes nil. Copy destinations are released before tree rollback. */
    int mm_rich_text_read(MMRichTextState *, MMU32 type, MMU32 number, int system, int native_big,
                          const MMRichTextReadServices *);
    int mm_rich_text_create(const MMRichTextDefaults *, const MMMovieTitle *, MMTextChunk *, int big,
                            const MMRichTextWriteServices *);
    int mm_rich_text_write(const MMRichTextState *, MMTextChunk *, int big, const MMRichTextWriteServices *);

    typedef struct MMTextCollection MMTextCollection;
    MMTextCollection *mm_text_collection_new(MMU32 kind);
    void mm_text_collection_dispose(MMTextCollection *);
    int mm_text_collection_read(MMU32 kind, const MMReadStream *, MMTextCollection **, int *order, int *system);
    int mm_text_collection_write(MMTextCollection *, const MMStorageWriter *, int order, int system);
    MMI32 mm_text_collection_width(const MMTextCollection *);
    MMU32 mm_text_collection_count(const MMTextCollection *);
    MMU32 mm_text_collection_bytes(const MMTextCollection *);
    /* Property words and fixed argument references are native values. Variable
     * argument bytes are borrowed in stored form until the next mutation. */
    int mm_text_collection_property(const MMTextCollection *, MMU32 row, MMU32 words[2]);
    int mm_text_collection_put_property(MMTextCollection *, MMU32 row, const MMU32 words[2], int insert);
    const void *mm_text_collection_argument(const MMTextCollection *, MMU32 row, MMI32 *bytes);
    MMU32 mm_text_collection_argument_ref(const MMTextCollection *, MMU32 row);
    int mm_text_collection_put_argument_ref(MMTextCollection *, MMU32 row, MMU32 ref);
    int mm_text_collection_delete(MMTextCollection *, MMU32 row);
#ifdef __cplusplus
}
#endif
#endif
