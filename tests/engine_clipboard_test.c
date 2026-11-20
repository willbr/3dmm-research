#include "mm/clipboard.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "clipboard check failed at %d\n", __LINE__); exit(1); } } while (0)

enum { BYTE_FORMAT = 1, WIDE_FORMAT = 13, FOREIGN_FORMAT = 51, DOCUMENT_CLASS = 97 };

typedef struct Document
{
    int refs, views;
} Document;

typedef struct Test
{
    MMClipboard state;
    MMClipboardServices services;
    Document local, second, imported;
    Document *rename_owner;
    void *published;
    unsigned char data[16], export_buffer[16];
    MMI32 data_bytes, imported_bytes, allocated_bytes, flushed_format;
    int open, data_available, text_available, delay, import_error;
    int reads, imports, probes, closes, locks, unlocks, allocations, flushes, diagnostic;
    char events[32];
    size_t events_used;
} Test;

static void event(Test *t, char value)
{
    CHECK(t->events_used + 1 < sizeof(t->events));
    t->events[t->events_used++] = value;
    t->events[t->events_used] = 0;
}

static int callback(void *context, MMClipboardOperation op, MMClipboardRequest *q)
{
    Test *t = context;
    Document *document = q->document;
    switch (op)
    {
    case MM_CLIPBOARD_HAS_VIEWS: event(t, 'v'); return document->views;
    case MM_CLIPBOARD_RENAME:
        CHECK(t->state.document == t->rename_owner);
        event(t, 'n');
        break;
    case MM_CLIPBOARD_RETAIN: event(t, '+'); ++document->refs; break;
    case MM_CLIPBOARD_RELEASE: event(t, '-'); --document->refs; break;
    case MM_CLIPBOARD_ACTIVATE: event(t, 'a'); break;
    case MM_CLIPBOARD_SHOW: event(t, 's'); break;
    case MM_CLIPBOARD_EXPORT_DOCUMENT: event(t, 'e'); break;
    case MM_CLIPBOARD_IS_FORMAT: return q->format == DOCUMENT_CLASS;
    case MM_CLIPBOARD_DOCUMENT_FORMAT: event(t, 'd'); return 0;
    case MM_CLIPBOARD_PUBLISH_DOCUMENT: event(t, 'p'); t->published = document; break;
    case MM_CLIPBOARD_DIAGNOSTIC: t->diagnostic = q->diagnostic; break;
    case MM_CLIPBOARD_READ_TEXT:
        ++t->reads;
        q->buffer = t->data;
        q->bytes = t->data_bytes;
        return t->text_available;
    case MM_CLIPBOARD_OPEN_READ:
    case MM_CLIPBOARD_OPEN_WRITE:
        return t->open;
    case MM_CLIPBOARD_NEXT_FORMAT:
        q->format = q->format == 0 ? FOREIGN_FORMAT : q->format == FOREIGN_FORMAT ? WIDE_FORMAT : 0;
        return q->format != 0;
    case MM_CLIPBOARD_GET_DATA:
        q->handle = t->data;
        return t->data_available;
    case MM_CLIPBOARD_LOCK_DATA:
        CHECK(q->handle == t->data);
        ++t->locks;
        q->buffer = t->data;
        q->bytes = t->data_bytes;
        break;
    case MM_CLIPBOARD_UNLOCK_DATA: ++t->unlocks; break;
    case MM_CLIPBOARD_CLOSE: ++t->closes; break;
    case MM_CLIPBOARD_IMPORT_DOCUMENT:
        CHECK(t->state.current && t->state.importing && !t->state.exporting);
        if (!q->output)
        {
            ++t->probes;
            return q->format == WIDE_FORMAT;
        }
        ++t->imports;
        if (t->import_error)
            return t->import_error;
        t->imported_bytes = q->bytes;
        if (!document)
        {
            q->document = &t->imported;
            ++t->imported.refs;
        }
        q->delay = q->allow_delay && t->delay;
        break;
    case MM_CLIPBOARD_ALLOCATE_EXPORT:
        CHECK(t->state.exporting && !t->state.importing);
        ++t->allocations;
        t->allocated_bytes = q->bytes;
        q->buffer = t->export_buffer;
        break;
    case MM_CLIPBOARD_FLUSH_EXPORT:
        CHECK(t->state.exporting);
        ++t->flushes;
        t->flushed_format = q->format;
        break;
    }
    return 1;
}

static void initialize(Test *t, int transport)
{
    static const unsigned char data[] = {'A', 0, 0, 0, 'B', 0, 'Z'};
    memset(t, 0, sizeof(*t));
    t->services.context = t;
    t->services.transport = transport;
    t->services.call = callback;
    t->services.text_unit = 2;
    t->services.text_format = t->services.wide_format = WIDE_FORMAT;
    t->services.byte_format = BYTE_FORMAT;
    t->services.max_bytes = 16;
    t->open = t->data_available = t->text_available = 1;
    t->local.refs = t->second.refs = 1;
    t->diagnostic = -1;
    memcpy(t->data, data, sizeof(data));
    t->data_bytes = sizeof(data);
}

static void documents(void)
{
    Test t;
    initialize(&t, MM_CLIPBOARD_TEXT);
    t.local.views = t.second.views = 1;
    t.rename_owner = &t.local;
    CHECK(mm_clipboard_set(&t.state, &t.local, 1, &t.services) == 1);
    CHECK(strcmp(t.events, "+vne") == 0 && t.state.document == &t.local && t.local.refs == 2);
    t.events_used = 0;
    t.rename_owner = &t.second;
    CHECK(mm_clipboard_set(&t.state, &t.second, 0, &t.services) == 1);
    CHECK(strcmp(t.events, "vn-+vn") == 0 && t.state.document == &t.second && t.local.refs == 1);
    CHECK(mm_clipboard_set(&t.state, &t.second, 1, &t.services) == 1 && t.events_used == 6);

    t.events_used = 0;
    CHECK(mm_clipboard_get(&t.state, DOCUMENT_CLASS, 0, &t.services) == 1 && !t.events_used);
    CHECK(mm_clipboard_get(&t.state, DOCUMENT_CLASS, 1, &t.services) == 1);
    CHECK(strcmp(t.events, "p+") == 0 && t.published == &t.second && t.second.refs == 3);
    --t.second.refs;
    CHECK(mm_clipboard_get(&t.state, FOREIGN_FORMAT, 1, &t.services) == 0 && strcmp(t.events, "p+d") == 0);
    CHECK(mm_clipboard_show(&t.state, &t.services) == 1 && strcmp(t.events, "p+dva") == 0);
    t.second.views = 0;
    CHECK(mm_clipboard_show(&t.state, &t.services) == 1 && strcmp(t.events, "p+dvavs") == 0);
    CHECK(mm_clipboard_invalidate(&t.state, &t.services) == 1 && !t.state.current && t.second.refs == 2);
    t.second.views = 1;
    t.rename_owner = NULL;
    CHECK(mm_clipboard_invalidate(&t.state, &t.services) == 1 && !t.state.document && t.second.refs == 1);
}

static void imports(void)
{
    Test t;
    initialize(&t, MM_CLIPBOARD_FORMATS);
    t.delay = 1;
    CHECK(mm_clipboard_get(&t.state, DOCUMENT_CLASS, 0, &t.services) == 1);
    CHECK(t.state.delayed && t.state.import_format == WIDE_FORMAT && t.imported.refs == 1 && !t.published);
    CHECK(t.probes == 2 && t.imports == 1 && t.imported_bytes == 2 && t.closes == 1);
    t.data_available = 0;
    CHECK(mm_clipboard_get(&t.state, DOCUMENT_CLASS, 1, &t.services) == 1);
    CHECK(t.imports == 1 && t.state.delayed && t.imported.refs == 2 && t.closes == 2);
    --t.imported.refs;
    t.data_available = 1;
    CHECK(mm_clipboard_get(&t.state, DOCUMENT_CLASS, 1, &t.services) == 1);
    CHECK(t.imports == 2 && !t.state.delayed && !t.state.import_format && t.imported.refs == 2 && t.closes == 3);
    CHECK(t.locks == 2 && t.unlocks == 2);

    initialize(&t, MM_CLIPBOARD_TEXT);
    CHECK(mm_clipboard_get(&t.state, DOCUMENT_CLASS, 0, &t.services) == 1);
    CHECK(t.reads == 1 && t.imports == 1 && t.imported_bytes == 2 && t.imported.refs == 1);
    t.text_available = 0;
    CHECK(mm_clipboard_invalidate(&t.state, &t.services) == 1);
    CHECK(mm_clipboard_get(&t.state, DOCUMENT_CLASS, 0, &t.services) == 1);
    CHECK(t.reads == 2 && t.imports == 1 && t.imported.refs == 1);
    t.text_available = 1;
    t.data_bytes = 0;
    CHECK(mm_clipboard_invalidate(&t.state, &t.services) == 1);
    CHECK(mm_clipboard_is_document(&t.state, NULL, &t.services) == 1 && !t.imported.refs);

    initialize(&t, MM_CLIPBOARD_TEXT);
    t.import_error = MM_CLIPBOARD_ALLOCATION_ERROR;
    CHECK(mm_clipboard_is_document(&t.state, NULL, &t.services) == 1 && t.state.importing && t.state.current);
    initialize(&t, MM_CLIPBOARD_TEXT);
    t.import_error = -2;
    CHECK(mm_clipboard_is_document(&t.state, NULL, &t.services) == -2 && t.state.importing && t.state.current);
}

static void exports(void)
{
    Test t;
    void *buffer;
    initialize(&t, MM_CLIPBOARD_TEXT);
    CHECK(mm_clipboard_export_buffer(&t.state, 3, WIDE_FORMAT, &buffer, &t.services) == 0 && !buffer);
    CHECK(t.diagnostic == MM_CLIPBOARD_NOT_EXPORTING);
    CHECK(mm_clipboard_begin_export(&t.state, &t.services) == 1);
    CHECK(!mm_clipboard_begin_export(&t.state, &t.services) && t.diagnostic == MM_CLIPBOARD_BUSY_WRITE);
    CHECK(mm_clipboard_set(&t.state, &t.local, 0, &t.services) == 1 && !t.state.document);
    CHECK(t.diagnostic == MM_CLIPBOARD_BUSY_SET);
    CHECK(mm_clipboard_export_buffer(&t.state, 3, WIDE_FORMAT, &buffer, &t.services) == 1);
    CHECK(buffer == t.export_buffer && t.allocated_bytes == 5 && t.flushes == 1);
    CHECK(!mm_clipboard_export_buffer(&t.state, 3, FOREIGN_FORMAT, &buffer, &t.services) && !buffer);
    CHECK(t.allocations == 1 && t.flushes == 2 && t.flushed_format == WIDE_FORMAT);
    CHECK(!mm_clipboard_export_buffer(&t.state, 15, WIDE_FORMAT, &buffer, &t.services) && !buffer);
    CHECK(mm_clipboard_end_export(&t.state, &t.services) == 1 && !t.state.exporting && t.flushes == 4);
    CHECK(mm_clipboard_end_export(&t.state, &t.services) == 1 && t.flushes == 4);

    initialize(&t, MM_CLIPBOARD_FORMATS);
    t.open = 0;
    CHECK(!mm_clipboard_begin_export(&t.state, &t.services) && !t.state.exporting);
    t.open = 1;
    CHECK(mm_clipboard_begin_export(&t.state, &t.services) == 1);
    CHECK(mm_clipboard_export_buffer(&t.state, 3, WIDE_FORMAT, &buffer, &t.services) == 1 && t.allocated_bytes == 6);
    CHECK(mm_clipboard_export_buffer(&t.state, 3, FOREIGN_FORMAT, &buffer, &t.services) == 1 && t.allocated_bytes == 3);
    CHECK(mm_clipboard_end_export(&t.state, &t.services) == 1 && t.flushed_format == FOREIGN_FORMAT && t.closes == 1);
}

int main(void)
{
    unsigned short text[] = {'a', 10, 10, 13, 10, 'b', 13, 'c'};
    static const unsigned short normalized[] = {'a', 13, 13, 13, 'b', 13, 'c'};
    static const unsigned char wide[] = {0, 'a', 'b', 0, 0, 0, 'c'};
    CHECK(mm_clipboard_normalize_paragraphs(text, sizeof(text) / sizeof(text[0])) == 7);
    CHECK(memcmp(text, normalized, sizeof(normalized)) == 0);
    CHECK(mm_clipboard_text_bytes(wide, sizeof(wide), 2) == 4);
    CHECK(mm_clipboard_text_bytes(wide + 1, 6, 1) == 2);
    CHECK(mm_clipboard_text_bytes(wide, 3, 2) == 2);
    documents();
    imports();
    exports();
    puts("Clipboard ownership, import, export and text policy passed");
    return 0;
}
