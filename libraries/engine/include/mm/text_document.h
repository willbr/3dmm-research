/* Text document ownership, initialization and file-save transactions. */
#ifndef MM_ENGINE_TEXT_DOCUMENT_H
#define MM_ENGINE_TEXT_DOCUMENT_H
#include "piece_table.h"
#include "text_store.h"
#ifdef __cplusplus
extern "C"
{
#endif
    enum
    {
        MM_TEXT_PLAIN,
        MM_TEXT_RICH
    };
    enum
    {
        MM_TEXT_OWNER_FILE,
        MM_TEXT_OWNER_STREAM,
        MM_TEXT_OWNER_ARCHIVE,
        MM_TEXT_OWNER_PROPERTIES,
        MM_TEXT_OWNER_ARGUMENTS
    };
    enum
    {
        MM_TEXT_DOC_NO_FILE,
        MM_TEXT_DOC_ENCODING,
        MM_TEXT_DOC_PARTIAL_CHAR
    };
    enum
    {
        MM_TEXT_DOC_READ,
        MM_TEXT_DOC_SAVE
    };
    typedef struct MMTextDocumentConfig
    {
        unsigned unit;
        int system, big;
    } MMTextDocumentConfig;
    typedef struct MMTextDocumentObjects
    {
        void *context;
        void *(*get)(void *, int slot);
        void (*set)(void *, int slot, void *);
        void (*retain)(void *, int kind, void *);
        void (*release)(void *, int kind, void *);
    } MMTextDocumentObjects;
    typedef struct MMTextBaseServices
    {
        void *context;
        MMTextDocumentObjects objects;
        MMPieceTable *(*pieces)(void *stream);
        MMI32 (*file_bytes)(void *, void *);
        int (*open)(void *, void *name, void **owned);
        int (*new_stream)(void *, void **owned);
        int (*load)(void *, int system);
        int (*translate)(void *, MMTextLocation *owned, int system);
        void (*diagnostic)(void *, int kind);
    } MMTextBaseServices;
    typedef struct MMTextRichInitServices
    {
        void *context;
        MMTextDocumentObjects objects;
        int (*open)(void *, void *name, void **owned);
        int (*first)(void *, void *archive, MMU32 type, MMTextChunk *);
        int (*read)(void *, void *archive, const MMTextChunk *);
        int (*base)(void *);
        int (*properties)(void *, MMU32 width, void **owned);
        void (*min_grow)(void *, void *, MMU32);
        int (*font)(void *, int system, MMI32 height);
        void (*error)(void *, int kind);
    } MMTextRichInitServices;
    typedef struct MMTextFileServices
    {
        void *context;
        MMTextDocumentObjects objects;
        int (*name)(void *, int profile, void *owner, void *output);
        int (*is_temp)(void *, int profile, void *owner);
        MMPieceTable *(*pieces)(void *stream);
        const MMHandleServices *handles;
        const MMBlockFileServices *files;
        const MMStorageAllocator *copy_allocator;
        int (*create)(void *, int profile, void *name, void **owned);
        int (*chunk)(void *, void *archive, int redirect);
        int (*save)(void *, void *archive);
        int (*rename)(void *, int profile, void *owner, void *name);
        void (*keep)(void *, int profile, void *owner), (*clean)(void *);
        void (*diagnostic)(void *, int kind), (*error)(void *, int kind);
    } MMTextFileServices;
    typedef struct MMTextFactoryServices
    {
        void *context;
        int (*create)(void *, void **owned), (*init)(void *, void *);
        void (*release)(void *, void *), (*read_error)(void *);
    } MMTextFactoryServices;
    /* Services are synchronous, return 1/0 or -1/-2 for native allocation/foreign
     * exceptions, and never throw across C. Getters/release/publication never throw.
     * Owned outputs remain valid if returned on failure. Base initialization keeps
     * published partial owners for the object's destructor; borrowed streams retain
     * before publication and gain a native CR sentinel. A virtual load hook may
     * replace the published stream; the sentinel is appended to its current owner.
     * Translation owns a retained, mutable file location and releases its final
     * owner on all exits. Piece and block operations use nonthrowing services.
     * Rich open holds its archive privately until a successful read, releasing before
     * ordinary read errors. New rich documents use width-eight groups, grow ten and
     * height twelve. Factory failures release the partial object before native rethrow;
     * ordinary chunk-read errors precede object release like the original factory. */
    int mm_text_base_init(const MMTextDocumentConfig *, void *name, void *borrowed_stream, int encoding,
                          const MMTextBaseServices *);
    int mm_text_base_load(int encoding, const MMTextBaseServices *);
    int mm_text_rich_init(const MMTextDocumentConfig *, void *name, MMU32 type, const MMTextRichInitServices *);
    void mm_text_document_dispose(int rich_part, const MMTextDocumentObjects *);
    int mm_text_document_create(void **owned, int read_error, const MMTextFactoryServices *);
    /* Normal save selects the current name and forces adoption. Copies preserve the
     * current owner/dirty flag. Plain writes omit the stream sentinel, prepend a native
     * UTF-16 BOM for two-byte units, and redirect before renaming; rich chunk saves also
     * redirect before container commit/rename. Ordinary redirect failure is ignored.
     * Failure releases the temporary owner but does not undo stream redirection.
     * Successful rename clears temporary status; adoption releases old owner, transfers
     * new owner and clears dirty. Only rich ordinary failures publish save errors.
     * Missing-current-file/name failures never publish an ordinary save error. */
    int mm_text_document_name(int profile, void *output, const MMTextFileServices *);
    int mm_text_document_save(int profile, const MMTextDocumentConfig *, void *name, void *fallback_name, int adopt,
                              const MMTextFileServices *);
#ifdef __cplusplus
}
#endif
#endif
