#ifndef MM_ENGINE_CLIPBOARD_H
#define MM_ENGINE_CLIPBOARD_H
#include "text.h"
#ifdef __cplusplus
extern "C" {
#endif

enum { MM_CLIPBOARD_TEXT, MM_CLIPBOARD_FORMATS, MM_CLIPBOARD_UNAVAILABLE };
enum { MM_CLIPBOARD_BUSY_SET, MM_CLIPBOARD_BUSY_READ, MM_CLIPBOARD_BUSY_WRITE, MM_CLIPBOARD_NOT_EXPORTING };
enum { MM_CLIPBOARD_ALLOCATION_ERROR = -1 };

typedef struct MMClipboard
{
    void *document;
    MMI32 import_format, export_format;
    int current, importing, exporting, delayed;
} MMClipboard;

typedef enum MMClipboardOperation
{
    MM_CLIPBOARD_HAS_VIEWS,
    MM_CLIPBOARD_RENAME,
    MM_CLIPBOARD_RETAIN,
    MM_CLIPBOARD_RELEASE,
    MM_CLIPBOARD_ACTIVATE,
    MM_CLIPBOARD_SHOW,
    MM_CLIPBOARD_EXPORT_DOCUMENT,
    MM_CLIPBOARD_IS_FORMAT,
    MM_CLIPBOARD_DOCUMENT_FORMAT,
    MM_CLIPBOARD_PUBLISH_DOCUMENT,
    MM_CLIPBOARD_DIAGNOSTIC,
    MM_CLIPBOARD_READ_TEXT,
    MM_CLIPBOARD_OPEN_READ,
    MM_CLIPBOARD_NEXT_FORMAT,
    MM_CLIPBOARD_GET_DATA,
    MM_CLIPBOARD_LOCK_DATA,
    MM_CLIPBOARD_UNLOCK_DATA,
    MM_CLIPBOARD_CLOSE,
    MM_CLIPBOARD_IMPORT_DOCUMENT,
    MM_CLIPBOARD_OPEN_WRITE,
    MM_CLIPBOARD_ALLOCATE_EXPORT,
    MM_CLIPBOARD_FLUSH_EXPORT
} MMClipboardOperation;

typedef struct MMClipboardRequest
{
    void *document, *buffer, *handle;
    MMI32 format, bytes;
    int output, allow_delay, delay, diagnostic;
} MMClipboardRequest;

typedef struct MMClipboardServices
{
    void *context;
    int transport;
    MMI32 text_format, byte_format, wide_format, max_bytes;
    unsigned text_unit;
    int (*call)(void *, MMClipboardOperation, MMClipboardRequest *);
} MMClipboardServices;

/* A zeroed clipboard starts stale and empty. Services run synchronously and
 * return 1/0 or negative errors. Negative results preserve the completed
 * native prefix. Lazy whole-text import absorbs allocation errors, retaining
 * the completed prefix. Other negative results propagate. Document handles
 * are borrowed except the retained clipboard owner and IMPORT_DOCUMENT's
 * in/out reference. On an acquiring format match,
 * PUBLISH_DOCUMENT writes the output before RETAIN. DOCUMENT_FORMAT delegates
 * to the document and may write the same caller output directly.
 *
 * READ_TEXT returns borrowed native text and its byte count until the next
 * call to READ_TEXT. NEXT_FORMAT starts with format 0 and updates it. GET_DATA
 * returns an OS handle, LOCK_DATA fills buffer/bytes, and CLOSE ends a
 * successful open. IMPORT_DOCUMENT with output=0 only probes the format.
 * text_unit is one or two. ALLOCATE_EXPORT receives the padded byte count
 * and returns its writable buffer. The host owns the pending allocation
 * through FLUSH_EXPORT. */
int mm_clipboard_set(MMClipboard *, void *document, int export_document, const MMClipboardServices *);
int mm_clipboard_is_document(MMClipboard *, void *document, const MMClipboardServices *);
int mm_clipboard_show(MMClipboard *, const MMClipboardServices *);
int mm_clipboard_get(MMClipboard *, MMI32 format, int output, const MMClipboardServices *);
int mm_clipboard_invalidate(MMClipboard *, const MMClipboardServices *);
int mm_clipboard_begin_export(MMClipboard *, const MMClipboardServices *);
int mm_clipboard_export_buffer(MMClipboard *, MMI32 bytes, MMI32 format, void **buffer, const MMClipboardServices *);
int mm_clipboard_end_export(MMClipboard *, const MMClipboardServices *);

/* Native clipboard text stops at the first zero unit and drops any incomplete
 * trailing unit. Byte order is immaterial. Paragraph normalization compacts
 * UTF-16 in place, turning LF and CRLF into CR. */
size_t mm_clipboard_text_bytes(const void *, size_t bytes, unsigned unit);
size_t mm_clipboard_normalize_paragraphs(unsigned short *, size_t units);

#ifdef __cplusplus
}
#endif
#endif
