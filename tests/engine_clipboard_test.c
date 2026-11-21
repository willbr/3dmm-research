#include "mm/clipboard.h"
#include "mm/piece_table.h"
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

typedef struct TextTransfer
{
    void *slot, *input;
    const void *expected_released;
    char events[40], fail;
    unsigned steps;
    int failure, deferred, foreign;
    MMI32 characters, inserted, deleted, requested;
    MMPieceTable source, alternate, *published;
    int switch_source, file_refs;
    unsigned char buffer[2052];
} TextTransfer;

static int text_event(void *p, char code)
{
    TextTransfer *t = p;
    CHECK(t->steps + 1 < sizeof(t->events));
    t->events[t->steps++] = code;
    t->events[t->steps] = 0;
    return t->fail == code ? t->failure : 1;
}

static void *text_get(void *p)
{
    TextTransfer *t = p;
    text_event(p, 'g');
    return t->slot;
}

static void text_publish(void *p, void *document)
{
    TextTransfer *t = p;
    text_event(p, document ? 'p' : '0');
    t->slot = document;
}

static void text_delay(void *p, int deferred)
{
    text_event(p, 'd');
    ((TextTransfer *)p)->deferred = deferred;
}

static int text_is_text(void *p, void *document)
{
    TextTransfer *t = p;
    CHECK(document == t->slot);
    text_event(p, 'i');
    return !t->foreign;
}

static int text_release(void *p, void *document)
{
    TextTransfer *t = p;
    CHECK(document == t->expected_released && t->slot == document);
    return text_event(p, 'l');
}

static int text_create(void *p, void **document)
{
    TextTransfer *t = p;
    int status = text_event(p, 'c');
    CHECK(!t->slot);
    if (status > 0)
    {
        *document = &t->slot;
        t->foreign = 0;
        t->characters = 1;
    }
    return status;
}

static int text_suspend(void *p, void *document)
{
    CHECK(((TextTransfer *)p)->slot == document);
    return text_event(p, 's');
}

static int export_characters(void *p, MMI32 *characters)
{
    *characters = ((TextTransfer *)p)->characters;
    return text_event(p, 'n');
}

static int text_characters(void *p, void *document, MMI32 *characters)
{
    CHECK(((TextTransfer *)p)->slot == document);
    return export_characters(p, characters);
}

static int text_replace(void *p, void *document, void *data, MMI32 inserted, MMI32 deleted)
{
    TextTransfer *t = p;
    CHECK(t->slot == document && data == t->input);
    t->inserted = inserted;
    t->deleted = deleted;
    return text_event(p, 'w');
}

static int text_resume(void *p, void *document)
{
    CHECK(((TextTransfer *)p)->slot == document);
    return text_event(p, 'r');
}

static int export_begin(void *p)
{
    return text_event(p, 'b');
}

static int export_buffer(void *p, MMI32 bytes, MMI32 format, void **buffer)
{
    TextTransfer *t = p;
    int status = text_event(p, 'a');
    CHECK(format == WIDE_FORMAT);
    t->requested = bytes;
    if (t->switch_source)
        t->published = &t->alternate;
    if (status > 0)
        *buffer = t->buffer;
    return status;
}

static MMPieceTable *export_pieces(void *p)
{
    TextTransfer *t = p;
    text_event(p, 'n');
    return t->published;
}

static void export_retain(void *p, void *file)
{
    TextTransfer *t = p;
    CHECK(file == t);
    ++t->file_refs;
}

static void export_release(void *p, void *file)
{
    TextTransfer *t = p;
    CHECK(file == t && t->file_refs > 0);
    --t->file_refs;
}

static int export_read(void *p, const MMFilePiece *piece, void *buffer)
{
    TextTransfer *t = p;
    CHECK(piece->file == t && !piece->offset && piece->length == 2048 && buffer == t->buffer);
    return text_event(p, 'r') > 0;
}

static int export_end(void *p)
{
    return text_event(p, 'e');
}

static void text_documents(void)
{
    TextTransfer t;
    unsigned char input[1026] = {0};
    MMClipboardTextImport import = {0};
    MMClipboardTextExport export = {0};
    MMPieceFiles files = {0};
    MMFilePiece piece;
    unsigned i;

    memset(&t, 0, sizeof(t));
    t.input = input;
    import.context = &t;
    import.format = WIDE_FORMAT;
    import.unit = 2;
    import.get = text_get;
    import.publish = text_publish;
    import.delay = text_delay;
    import.is_text = text_is_text;
    import.release = text_release;
    import.create = text_create;
    import.suspend = text_suspend;
    import.characters = text_characters;
    import.replace = text_replace;
    import.resume = text_resume;
    CHECK(!mm_clipboard_import_text(FOREIGN_FORMAT, input, 8, &import) && !t.steps);
    CHECK(mm_clipboard_import_text(WIDE_FORMAT, NULL, 0, &import) == 1 && !t.steps);
    CHECK(mm_clipboard_import_text(WIDE_FORMAT, input, 1026, &import) == 1);
    CHECK(!strcmp(t.events, "gcpd") && t.slot == &t.slot && t.deferred);
    t.steps = 0;
    t.characters = 5;
    t.fail = 'w';
    CHECK(mm_clipboard_import_text(WIDE_FORMAT, input, 1024, &import) == 1);
    CHECK(!strcmp(t.events, "gidsnwr") && !t.deferred && t.inserted == 512 && t.deleted == 4);
    t.steps = 0;
    t.failure = -2;
    import.delay = NULL;
    CHECK(mm_clipboard_import_text(WIDE_FORMAT, input, 1025, &import) == -2);
    CHECK(!strcmp(t.events, "gisnw") && t.inserted == 512);
    t.steps = 0;
    t.foreign = 1;
    t.slot = input;
    t.expected_released = input;
    t.fail = 'c';
    t.failure = 0;
    CHECK(!mm_clipboard_import_text(WIDE_FORMAT, input, 4, &import));
    CHECK(!strcmp(t.events, "gil0c0") && !t.slot);

    memset(&t, 0, sizeof(t));
    files.context = &t;
    files.retain = export_retain;
    files.release = export_release;
    files.read = export_read;
    CHECK(mm_piece_table_init(&t.source, NULL, &files, 0) == MM_DATA_OK);
    CHECK(mm_piece_table_init(&t.alternate, NULL, &files, 0) == MM_DATA_OK);
    t.published = &t.source;
    export.context = &t;
    export.format = WIDE_FORMAT;
    export.unit = 2;
    export.pieces = export_pieces;
    export.begin = export_begin;
    export.buffer = export_buffer;
    export.end = export_end;
    CHECK(mm_piece_table_replace(&t.source, 0, 0, "\r\0", 2) == MM_DATA_OK);
    CHECK(mm_clipboard_export_text(&export) == 1 && !strcmp(t.events, "n"));
    t.steps = 0;
    CHECK(mm_piece_table_replace(&t.source, 0, 0, "a\0b\0\0\0c\0", 8) == MM_DATA_OK);
    CHECK(mm_piece_table_replace(&t.alternate, 0, 0, "1\0\0\0\r\nX\0\r\0", 10) == MM_DATA_OK);
    t.switch_source = 1;
    memset(t.buffer, 0xa5, sizeof(t.buffer));
    CHECK(mm_clipboard_export_text(&export) == 1 && !strcmp(t.events, "nbane") && t.requested == 8);
    CHECK(!memcmp(t.buffer, "1\0\0\0\r\nX\0", 8) && t.buffer[8] == 0xa5);
    t.switch_source = 0;
    t.steps = 0;
    t.fail = 'a';
    CHECK(mm_clipboard_export_text(&export) == 1 && !strcmp(t.events, "nbae"));
    t.steps = 0;
    t.failure = -2;
    CHECK(mm_clipboard_export_text(&export) == -2 && !strcmp(t.events, "nba"));

    t.steps = 0;
    t.published = &t.source;
    t.fail = 'r';
    t.failure = 0;
    piece.file = &t;
    piece.offset = 0;
    piece.length = 2050;
    CHECK(mm_piece_table_replace_file(&t.source, 0, t.source.length, &piece, 0) == MM_DATA_OK);
    CHECK(mm_clipboard_export_text(&export) == 1 && !strcmp(t.events, "nbanre") && t.requested == 2048);
    for (i = 0; i < 2048; ++i)
        CHECK(t.buffer[i] == 0xa3);
    CHECK(mm_piece_table_dispose(&t.alternate) == MM_DATA_OK);
    CHECK(mm_piece_table_dispose(&t.source) == MM_DATA_OK && !t.file_refs);
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
    text_documents();
    puts("Clipboard ownership, import, export and text policy passed");
    return 0;
}
