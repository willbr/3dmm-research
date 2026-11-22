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
    struct MMMovieArchiveEditor;
    int mm_text_box_decode(const MMReadStream *, MMTextBoxState *);
    int mm_text_box_encode(const MMTextBoxState *, int system, int big, unsigned char bytes[36]);
    int mm_text_box_read(MMTextBoxState *, MMU32, MMTextChunk *optional_child, const struct MMMovieArchiveEditor *);
    int mm_text_box_write(const MMTextBoxState *, MMU32, const struct MMMovieArchiveEditor *);
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
    /* TEXT has its own encoding marker, independent of the RTXT root order. */
    int mm_rich_text_encoding(const unsigned char marker[2], int big);
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
    typedef struct MMTextEncoded
    {
        const void *text;
        MMU32 bytes;
        int encoding;
        const MMReadStream *stream; /* Optional range source instead of text. */
    } MMTextEncoded;
    /* Use the caller's encoded bytes directly for TEXT. A null replacement
     * keeps the authored encoding and copies from the existing file location. */
    int mm_rich_text_write_encoded(const MMRichTextState *, const MMTextEncoded *replacement, MMTextChunk *,
                                   int big, const MMRichTextWriteServices *);

    typedef struct MMRichTextArchiveCollections
    {
        void (*release)(void *, MMU32 kind, void *);
        MMI32 (*width)(void *, MMU32 kind, void *);
        int (*read)(void *, MMU32 kind, const MMReadStream *, void **, int *order, int *system);
        MMU32 (*bytes)(void *, MMU32 kind, void *);
        MMI32 (*count)(void *, void *);
        int (*write)(void *, MMU32 kind, const MMStorageWriter *, void *, int order, int system);
    } MMRichTextArchiveCollections;
    struct MMChunkOwnerRead;
    struct MMBlockFileServices;
    typedef struct MMRichTextArchive
    {
        const struct MMMovieArchiveEditor *editor;
        struct MMChunkOwnerRead *owner;
        const struct MMBlockFileServices *files;
        void *context;
        int swapped_unicode_encoding;
        /* Native hosts can wrap these in a guard for exception reporting. */
        int (*location)(const struct MMRichTextArchive *, MMU32 number, MMTextLocation *borrowed);
        int (*read_at)(const struct MMRichTextArchive *, const MMTextLocation *, MMU32 offset, void *, size_t);
        const MMRichTextArchiveCollections *collections; /* NULL uses C collections. */
    } MMRichTextArchive;
    /* The owner supplies the stored TEXT extent, including when flagged packed.
     * It can be NULL for writing an existing extent or drafting encoded text.
     * Without an owner, fallback files serve the state's existing file tokens.
     * Omit both only when no file references or reads are needed. */
    MMRichTextArchive mm_rich_text_file_archive(struct MMChunkOwnerRead *,
                                               const struct MMBlockFileServices *fallback_files,
                                               const struct MMMovieArchiveEditor *, int swapped_unicode_encoding);
    int mm_rich_text_file_location(const MMRichTextArchive *, MMU32 number, MMTextLocation *borrowed);
    int mm_rich_text_file_read(const MMRichTextArchive *, const MMTextLocation *, MMU32 offset, void *, size_t);
    typedef struct MMRichTextArchiveReader
    {
        const MMRichTextArchive *archive;
        MMReadStream root;
    } MMRichTextArchiveReader;
    typedef struct MMRichTextArchiveWriter
    {
        const MMRichTextArchive *archive;
        MMStorageWriter writer;
    } MMRichTextArchiveWriter;
    /* The supplied state must outlive the services. */
    void mm_rich_text_archive_reader(MMRichTextArchiveReader *, const MMRichTextArchive *, MMRichTextReadServices *);
    void mm_rich_text_archive_writer(MMRichTextArchiveWriter *, const MMRichTextArchive *, MMRichTextWriteServices *);
    typedef struct MMRichTextStream
    {
        MMReadStream stream;
        const MMRichTextArchive *archive;
        const MMTextLocation *location;
    } MMRichTextStream;
    void mm_rich_text_archive_stream(MMRichTextStream *, const MMRichTextArchive *, const MMTextLocation *);
    /* Uses the configured collections. Archive callbacks are synchronous and do not throw.
     * The editable archive supplies discard for partial tree cleanup. */
    int mm_rich_text_archive_read(MMRichTextState *, const MMTextChunk *, const MMRichTextArchive *);
    int mm_rich_text_archive_write(const MMRichTextState *, const MMTextEncoded *, MMTextChunk *,
                                   const MMRichTextArchive *);
    void mm_rich_text_archive_dispose(MMRichTextState *, const MMRichTextArchive *);

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
