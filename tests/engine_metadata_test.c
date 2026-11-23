#include "mm/chunk_metadata.h"
#include "mm/codec_registry.h"
#include "mm/codec.h"
#include "mm/string_wire.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static void check(int value, int line)
{
    if (!value)
    {
        fprintf(stderr, "C89 metadata failed at %d\n", line);
        exit(1);
    }
}
#define CHECK(x) check((x), __LINE__)
typedef struct Allocation
{
    int live, calls, fail_all, fail_speed;
} Allocation;
static void *resize(void *c, void *old, MMU32 old_bytes, MMU32 bytes, int priority)
{
    Allocation *a = (Allocation *)c;
    void *fresh;
    ++a->calls;
    if (a->fail_all || (a->fail_speed && priority == MM_STORAGE_SPEED))
        return NULL;
    fresh = malloc(bytes);
    CHECK(fresh != NULL);
    if (old)
    {
        memcpy(fresh, old, bytes < old_bytes ? bytes : old_bytes);
        free(old);
    }
    else
        ++a->live;
    return fresh;
}
static void release(void *c, void *data, MMU32 bytes)
{
    Allocation *a = (Allocation *)c;
    (void)bytes;
    CHECK(a->live > 0);
    --a->live;
    free(data);
}
static void runtime_cache(void)
{
    Allocation a, b;
    MMStorageAllocator memory, other;
    MMChunkRuntime first, second;
    MMChunkRuntimeEntry e, previous;
    int i, calls;
    memset(&a, 0, sizeof(a));
    memset(&b, 0, sizeof(b));
    memory.context = &a;
    memory.resize = resize;
    memory.release = release;
    other = memory;
    other.context = &b;
    CHECK(mm_chunk_runtime_init(&first, &memory) == MM_CHUNK_OK);
    CHECK(mm_chunk_runtime_init(&second, &other) == MM_CHUNK_OK);
    CHECK(mm_chunk_runtime_get(&first, 1, 2) == 0 &&
          mm_chunk_runtime_set(&first, 1, 2, 0) == MM_CHUNK_OK && !a.calls);
    for (i = 20; i >= 0; --i)
        CHECK(mm_chunk_runtime_set(&first, (MMU32)i % 3, (MMU32)i, i + 10) == MM_CHUNK_OK);
    for (i = 0; i < 21; ++i)
    {
        CHECK(mm_chunk_runtime_get(&first, (MMU32)i % 3, (MMU32)i) == i + 10);
        CHECK(mm_chunk_runtime_at(&first, (MMU32)i, &e) == MM_CHUNK_OK);
        if (i)
            CHECK(previous.type < e.type || (previous.type == e.type && previous.number < e.number));
        previous = e;
    }
    for (i = 0; i < 21; i += 2)
        CHECK(mm_chunk_runtime_set(&first, (MMU32)i % 3, (MMU32)i, 0) == MM_CHUNK_OK);
    a.fail_all = 1;
    calls = a.calls;
    CHECK(mm_chunk_runtime_set(&first, 1, 1, -7) == MM_CHUNK_OK && a.calls == calls);
    /* Fill retained capacity, then make a missing insertion fail. */
    for (i = 100; first.count < first.storage.buffers[0].capacity / sizeof(e); ++i)
        CHECK(mm_chunk_runtime_set(&first, 9, (MMU32)i, i) == MM_CHUNK_OK);
    calls = (int)first.count;
    CHECK(mm_chunk_runtime_set(&first, 9, 9999, 3) == MM_CHUNK_MEMORY && first.count == (MMU32)calls);
    CHECK(mm_chunk_runtime_set(&first, 99, 99, 0) == MM_CHUNK_OK &&
          !mm_chunk_runtime_get(&first, 9, 9999));
    CHECK(mm_chunk_runtime_swap(&first, &second) == MM_CHUNK_OK && !first.count &&
          second.count == (MMU32)calls);
    CHECK(mm_chunk_runtime_get(&second, 1, 1) == -7);
    CHECK(mm_chunk_runtime_swap(&first, &second) == MM_CHUNK_OK && first.count == (MMU32)calls);
    while (first.count)
    {
        CHECK(mm_chunk_runtime_at(&first, 0, &e) == MM_CHUNK_OK);
        CHECK(mm_chunk_runtime_set(&first, e.type, e.number, 0) == MM_CHUNK_OK);
    }
    CHECK(first.created && first.storage.buffers[0].data != NULL);
    CHECK(mm_chunk_runtime_dispose(&first) == MM_CHUNK_OK && !a.live);
    CHECK(mm_chunk_runtime_dispose(&second) == MM_CHUNK_OK && !b.live);
    a.fail_all = 1;
    CHECK(mm_chunk_runtime_init(&first, &memory) == MM_CHUNK_OK);
    CHECK(mm_chunk_runtime_set(&first, 0xffffffffUL, 0xffffffffUL, 1) == MM_CHUNK_MEMORY && !first.count &&
          !first.created);
    a.fail_all = 0;
    a.fail_speed = 1;
    CHECK(mm_chunk_runtime_set(&first, 0xffffffffUL, 0xffffffffUL, 1) == MM_CHUNK_OK);
    CHECK(mm_chunk_runtime_get(&first, 0xffffffffUL, 0xffffffffUL) == 1);
    CHECK(mm_chunk_runtime_dispose(&first) == MM_CHUNK_OK && !a.live);
}
typedef struct Stream
{
    unsigned char bytes[1024];
    int reads, writes, fail_read, fail_write;
} Stream;
static int read_at(void *c, MMU32 offset, void *out, size_t bytes)
{
    Stream *s = (Stream *)c;
    CHECK(offset + bytes <= sizeof(s->bytes));
    ++s->reads;
    if (s->reads == s->fail_read)
        return 0;
    memcpy(out, s->bytes + offset, bytes);
    return 1;
}
static int write_at(void *c, MMU32 offset, const void *in, size_t bytes)
{
    Stream *s = (Stream *)c;
    CHECK(offset + bytes <= sizeof(s->bytes));
    ++s->writes;
    if (s->writes == s->fail_write)
        return 0;
    memcpy(s->bytes + offset, in, bytes);
    return 1;
}
static MMI32 convert(void *c, const void *in, MMU32 bytes, int encoding, void *out, MMU32 capacity)
{
    unsigned *unit = (unsigned *)c;
    return (MMI32)(*unit == 1 ? mm_text_ansi(NULL, in, bytes, encoding, (char *)out, capacity)
                                   : mm_text_utf16(NULL, in, bytes, encoding, (unsigned short *)out, capacity));
}
static MMU32 hash_bytes(MMU32 hash, const void *p, size_t bytes)
{
    const unsigned char *b = (const unsigned char *)p;
    while (bytes--)
        hash = (hash ^ *b++) * 16777619UL;
    return hash;
}
static MMU32 hash_word(MMU32 hash, MMU32 word)
{
    unsigned char b[4];
    unsigned i;
    for (i = 0; i < 4; ++i)
        b[i] = (unsigned char)(word >> (8 * i));
    return hash_bytes(hash, b, 4);
}
static void string_wire(void)
{
    Stream source, destination;
    MMReadStream reader;
    MMStringWriter writer;
    MMStringWire text;
    unsigned char buffer[514], encoded[1024], wire[1024];
    unsigned unit = 1, lengths[4] = {0, 1, 31, 255};
    MMU32 count, i, end, bytes, hash = 2166136261UL, total = 0, record[9], offset;
    int kind, length, malformed, counted, path, ok;
    memset(&text, 0, sizeof(text));
    text.buffer = buffer;
    text.unit = 1;
    text.current_encoding = MM_TEXT_WINDOWS;
    text.swapped_unicode_encoding = MM_TEXT_UTF16_BE;
    text.context = &unit;
    text.convert = convert;
    reader.context = &source;
    reader.read_at = read_at;
    for (kind = 0; kind < 4; ++kind)
        for (length = 0; length < 4; ++length)
            for (malformed = 0; malformed < 5; ++malformed)
                for (counted = 0; counted < 2; ++counted)
                    for (path = 0; path < 3; ++path)
                    {
                        unsigned char system = (unsigned char)(kind == 0 ? 3 : kind == 1 ? 2 : kind == 2 ? 5 : 4);
                        memset(&source, 0, sizeof(source));
                        count = 0;
                        wire[count++] = system;
                        wire[count++] = system;
                        if (kind < 2)
                            wire[count++] = (unsigned char)lengths[length];
                        else
                        {
                            wire[count++] = kind == 3 ? 0 : (unsigned char)lengths[length];
                            wire[count++] = kind == 3 ? (unsigned char)lengths[length] : 0;
                        }
                        for (i = 0; i < lengths[length]; ++i)
                        {
                            unsigned char ch = i % 7 == 0 ? (kind == 1 ? 0x8e : 0xe9) : (unsigned char)('a' + i % 26);
                            if (kind < 2)
                                wire[count++] = ch;
                            else
                            {
                                wire[count++] = kind == 3 ? 0 : ch;
                                wire[count++] = kind == 3 ? ch : 0;
                            }
                        }
                        wire[count++] = 0;
                        if (kind >= 2)
                            wire[count++] = 0;
                        if (malformed == 1)
                            --count;
                        if (malformed == 2)
                            wire[count - 1] = 1;
                        if (malformed == 3)
                        {
                            if (lengths[length])
                                wire[kind < 2 ? 3 : 4] = wire[kind < 2 ? 3 : 5] = 0;
                            else
                                wire[0] = 0xff;
                        }
                        if (malformed == 4)
                            wire[count++] = 0xaa;
                        offset = path ? 7 : 0;
                        memcpy(source.bytes + offset, wire, count);
                        reader.length = offset + count;
                        end = 12345;
                        memset(buffer, 0xcc, sizeof(buffer));
                        buffer[0] = 7;
                        memcpy(buffer + 1, "initial", 8);
                        ok = mm_string_wire_read(&text, &reader, offset, counted ? &end : NULL);
                        bytes = mm_string_wire_size(&text);
                        CHECK(mm_string_wire_get(&text, encoded, sizeof(encoded)));
                        record[0] = (MMU32)kind;
                        record[1] = lengths[length];
                        record[2] = (MMU32)malformed;
                        record[3] = (MMU32)counted;
                        record[4] = (MMU32)path;
                        record[5] = (MMU32)ok;
                        record[6] = ok && counted ? end : 0xffffffffUL;
                        record[7] = buffer[0];
                        record[8] = bytes;
                        for (i = 0; i < 9; ++i)
                            hash = hash_word(hash, record[i]);
                        hash = hash_bytes(hash, encoded, bytes);
                        total += 36 + bytes;
                        if (!ok)
                            CHECK(buffer[0] == 0 && buffer[1] == 0 && end == 12345);
                    }
    CHECK(total == 29532 && hash == 0x54d0f109UL); /* Frozen native snapshot. */
    memset(&source, 0, sizeof(source));
    memcpy(source.bytes, "\3\3\1x\0", 5);
    reader.length = 5;
    for (i = 1; i <= 3; ++i)
    {
        source.reads = 0;
        source.fail_read = (int)i;
        end = 99;
        CHECK(!mm_string_wire_read(&text, &reader, 0, &end) && end == 99 && !buffer[0] && !buffer[1]);
    }
    source.fail_read = 0;
    source.reads = 0;
    CHECK(mm_string_wire_read(&text, &reader, 0, NULL));
    writer.context = &destination;
    writer.length = 12;
    writer.write_at = write_at;
    for (i = 1; i <= 2; ++i)
    {
        memset(&destination, 0, sizeof(destination));
        destination.fail_write = (int)i;
        CHECK(!mm_string_wire_write(&text, &writer, 7) && destination.writes == (int)i);
    }
    memset(&destination, 0, sizeof(destination));
    CHECK(mm_string_wire_write(&text, &writer, 7) && destination.writes == 2);
    CHECK(!memcmp(destination.bytes + 7, source.bytes, 5));
    CHECK(!mm_string_wire_write(&text, &writer, 8) && destination.writes == 2);
    unit = 2;
    text.unit = 2;
    text.current_encoding = MM_TEXT_UTF16_LE;
    source.reads = 0;
    CHECK(mm_string_wire_read(&text, &reader, 0, NULL) && mm_string_wire_size(&text) == 8);
    text.current_encoding = MM_TEXT_UTF16_BE;
    text.native_big_endian = 1;
    text.swapped_unicode_encoding = MM_TEXT_UTF16_LE;
    memset(&source, 0, sizeof(source));
    memcpy(source.bytes, "\4\4\0\1\0A\0\0", 8);
    reader.length = 8;
    CHECK(mm_string_wire_read(&text, &reader, 0, NULL) &&
          mm_string_wire_get(&text, encoded, sizeof(encoded)) && !memcmp(encoded, source.bytes, 8));
}
typedef struct Names
{
    unsigned char bytes[128], decoded[257];
    MMU32 count;
    int locked, fail, pins;
    MMStringWire text;
} Names;
static MMU32 name_count(void *c)
{
    (void)c;
    return 1;
}
static int name_entry(void *c, MMU32 i, MMChunkEntry *out)
{
    Names *n = (Names *)c;
    CHECK(i == 0);
    memset(out, 0, sizeof(*out));
    out->child_count = 1;
    out->variable_bytes = n->count;
    return 1;
}
static int name_insert(void *c, MMU32 i, MMU32 offset, MMU32 bytes)
{
    Names *n = (Names *)c;
    CHECK(!i && !n->locked && offset <= n->count && n->count + bytes <= 128);
    if (n->fail)
        return 0;
    memmove(n->bytes + offset + bytes, n->bytes + offset, n->count - offset);
    n->count += bytes;
    return 1;
}
static void name_erase(void *c, MMU32 i, MMU32 offset, MMU32 bytes)
{
    Names *n = (Names *)c;
    CHECK(!i && !n->locked && offset + bytes <= n->count);
    n->count -= bytes;
    memmove(n->bytes + offset, n->bytes + offset + bytes, n->count - offset);
}
static void *name_pin(void *c, MMU32 i)
{
    Names *n = (Names *)c;
    CHECK(!i && !n->locked);
    ++n->locked;
    ++n->pins;
    return n->bytes;
}
static void name_unpin(void *c)
{
    Names *n = (Names *)c;
    CHECK(n->locked == 1);
    --n->locked;
}
static int name_decode(void *c, const void *data, MMU32 bytes)
{
    Names *n = (Names *)c;
    MMMemoryStream source;
    CHECK(n->locked == 1);
    CHECK(mm_memory_stream_init(&source, data, bytes));
    return mm_string_wire_read(&n->text, &source.stream, 0, NULL);
}
static void name_clear(void *c)
{
    Names *n = (Names *)c;
    n->decoded[0] = n->decoded[1] = 0;
}
static MMU32 name_chars(void *c)
{
    return ((Names *)c)->decoded[0];
}
static void names(void)
{
    Names n;
    MMChunkNames store;
    MMChunkNameDecoder decoder;
    MMChunkNameValue value;
    int pins;
    unsigned char wire[] = {3, 3, 3, 'a', 'b', 'c', 0}, short_wire[] = {3, 3, 1, 'x', 0}, before[128];
    memset(&n, 0, sizeof(n));
    n.count = 12;
    memcpy(n.bytes, "child prefix", 12);
    n.text.buffer = n.decoded;
    n.text.unit = 1;
    n.text.current_encoding = MM_TEXT_WINDOWS;
    store.context = &n;
    store.count = name_count;
    store.entry = name_entry;
    store.insert = name_insert;
    store.erase = name_erase;
    store.pin = name_pin;
    store.unpin = name_unpin;
    store.max_bytes = 80;
    decoder.context = &n;
    decoder.decode = name_decode;
    decoder.clear = name_clear;
    decoder.characters = name_chars;
    value.wire = wire;
    value.bytes = sizeof(wire);
    value.characters = 3;
    CHECK(mm_chunk_name_set(&store, 0, &value) == MM_CHUNK_OK && n.count == 19 && !n.locked);
    CHECK(mm_chunk_name_get(&store, 0, &decoder) && n.decoded[0] == 3 && !n.locked);
    value.wire = short_wire;
    value.bytes = sizeof(short_wire);
    value.characters = 1;
    CHECK(mm_chunk_name_set(&store, 0, &value) == MM_CHUNK_OK && n.count == 17 &&
          !memcmp(n.bytes, "child prefix", 12));
    memcpy(before, n.bytes, sizeof(before));
    pins = n.pins;
    n.fail = 1;
    value.wire = wire;
    value.bytes = sizeof(wire);
    value.characters = 3;
    CHECK(mm_chunk_name_set(&store, 0, &value) == MM_CHUNK_MEMORY && n.count == 17 &&
          !memcmp(before, n.bytes, sizeof(before)) && n.pins == pins);
    n.bytes[16] = 1;
    CHECK(!mm_chunk_name_get(&store, 0, &decoder) && !n.decoded[0] && !n.locked);
    CHECK(mm_chunk_name_set(&store, 0, NULL) == MM_CHUNK_OK && n.count == 12);
    pins = n.pins;
    CHECK(!mm_chunk_name_get(&store, 0, &decoder) && n.pins == pins);
}
typedef struct Codec
{
    int refs, id, encode, decode;
    MMI32 format;
} Codec;
typedef struct CodecContext
{
    int released[128], releases, queried[128], queries;
} CodecContext;
static void retain_codec(void *c, void *codec)
{
    (void)c;
    ++((Codec *)codec)->refs;
}
static void release_codec(void *c, void *codec)
{
    CodecContext *ctx = (CodecContext *)c;
    Codec *value = (Codec *)codec;
    CHECK(value->refs > 1);
    --value->refs;
    CHECK(ctx->releases < 128);
    ctx->released[ctx->releases++] = value->id;
}
static int can_do(void *c, void *codec, int encode, MMI32 format)
{
    CodecContext *ctx = (CodecContext *)c;
    Codec *value = (Codec *)codec;
    CHECK(ctx->queries < 128);
    ctx->queried[ctx->queries++] = value->id;
    return value->format == format && (encode ? value->encode : value->decode);
}

static int convert_codec(void *c, void *codec, int encode, MMI32 format, const void *input,
                         MMU32 bytes, void *output, MMU32 capacity, MMI32 *written)
{
    Codec *value = (Codec *)codec;
    (void)c;
    CHECK(value->format == format);
    if (encode)
    {
        CHECK(bytes == 64 && capacity >= 1);
        *(unsigned char *)output = (unsigned char)value->id;
        *written = 1;
    }
    else
    {
        CHECK(bytes == 1 && capacity == 64 && *(const unsigned char *)input == value->id);
        memset(output, value->id, capacity);
        *written = (MMI32)capacity;
    }
    return 1;
}

static void packed_codecs(void)
{
    CodecContext context;
    Codec standard = {1, 1, 1, 1, MM_CODEC_KCDC};
    Codec older = {1, 2, 1, 1, 101}, newer = {1, 3, 1, 1, 101};
    MMCodecServices services = {NULL, retain_codec, release_codec, can_do, convert_codec};
    MMCodecRegistry registry;
    unsigned char source[64], packed[64], reference[64], decoded[64];
    MMI32 size, expected, written;
    memset(&context, 0, sizeof(context));
    services.context = &context;
    memset(source, 3, sizeof(source));
    CHECK(mm_codec_registry_init_builtin(&registry, &services, NULL, MM_CODEC_KCD2) == MM_STORAGE_OK);
    CHECK(mm_codec_registry_add(&registry, &standard) == MM_STORAGE_OK &&
          mm_codec_registry_add(&registry, &older) == MM_STORAGE_OK &&
          mm_codec_registry_add(&registry, &newer) == MM_STORAGE_OK);
    CHECK(mm_codec_registry_can_code(&registry, 1, MM_CODEC_KCDC));
    CHECK(!context.queries);
    CHECK(mm_codec_registry_code(NULL, MM_CODEC_KCDC, source, sizeof(source), reference, sizeof(reference),
                                 &expected, NULL) == MM_DATA_OK);
    CHECK(mm_codec_registry_code(&registry, MM_CODEC_KCDC, source, sizeof(source), packed, sizeof(packed),
                                 &size, NULL) == MM_DATA_OK && size == expected &&
          !memcmp(reference, packed, (size_t)size));
    CHECK(mm_codec_registry_code(&registry, 0, packed, size, decoded, sizeof(decoded), &written, NULL) ==
          MM_DATA_OK && written == sizeof(source) && !memcmp(source, decoded, sizeof(source)));
    CHECK(!context.queries);
    CHECK(mm_codec_registry_code(&registry, 101, source, sizeof(source), packed, sizeof(packed),
                                 &size, NULL) == MM_DATA_OK && size == 9 && packed[8] == 3);
    CHECK(mm_codec_registry_code(&registry, 0, packed, size, decoded, sizeof(decoded), &written, NULL) ==
          MM_DATA_OK && written == sizeof(source) && !memcmp(source, decoded, sizeof(source)));
    CHECK(mm_codec_registry_code(NULL, 0, packed, size, NULL, 0, &written, NULL) == MM_DATA_CODEC);
    CHECK(mm_codec_registry_dispose(&registry) == MM_STORAGE_OK);

    CHECK(mm_codec_registry_init(&registry, &services, NULL, &older, 101) == MM_STORAGE_OK);
    CHECK(mm_codec_registry_add(&registry, &newer) == MM_STORAGE_OK);
    CHECK(!mm_codec_registry_can_code(&registry, 1, MM_CODEC_KCDC));
    CHECK(mm_codec_registry_code(&registry, 101, source, sizeof(source), packed, sizeof(packed),
                                 &size, NULL) == MM_DATA_OK && size == 9 && packed[8] == 2);
    CHECK(mm_codec_registry_dispose(&registry) == MM_STORAGE_OK && standard.refs == 1 &&
          older.refs == 1 && newer.refs == 1);
}
static void codecs(void)
{
    Allocation allocation;
    CodecContext context;
    Codec a = {1, 1, 0, 1, 101}, b = {1, 2, 1, 1, 101}, c = {1, 3, 1, 0, 101};
    MMStorageAllocator memory;
    MMCodecServices services;
    MMCodecRegistry registry;
    MMU32 count;
    memset(&allocation, 0, sizeof(allocation));
    memset(&context, 0, sizeof(context));
    memory.context = &allocation;
    memory.resize = resize;
    memory.release = release;
    services.context = &context;
    services.retain = retain_codec;
    services.release = release_codec;
    services.can_do = can_do;
    services.convert = NULL;
    CHECK(mm_codec_registry_init(&registry, &services, &memory, &a, 101) == MM_STORAGE_OK && a.refs == 2);
    CHECK(mm_codec_registry_add(&registry, &b) == MM_STORAGE_OK &&
          mm_codec_registry_add(&registry, &c) == MM_STORAGE_OK);
    CHECK(mm_codec_registry_find(&registry, 1, 101) == &c && context.queried[0] == 1 && context.queried[1] == 3);
    context.queries = 0;
    CHECK(mm_codec_registry_find(&registry, 0, 101) == &a && context.queries == 1);
    CHECK(mm_codec_registry_add(&registry, &b) == MM_STORAGE_OK && b.refs == 3);
    CHECK(mm_codec_registry_find(&registry, 1, 101) == &b);
    CHECK(mm_codec_registry_default(&registry, 303) == MM_STORAGE_OK && registry.default_format == 303);
    CHECK(mm_codec_registry_default(&registry, 0) == MM_STORAGE_ARGUMENT && registry.default_format == 303);
    context.queries = 0;
    CHECK(!mm_codec_registry_find(&registry, 1, 0) && !context.queries);
    CHECK(mm_codec_registry_dispose(&registry) == MM_STORAGE_OK && !allocation.live && a.refs == 1 &&
          b.refs == 1 && c.refs == 1);
    CHECK(context.releases == 4 && context.released[0] == 1 && context.released[1] == 2 && context.released[2] == 3 &&
          context.released[3] == 2);
    context.releases = context.queries = 0;
    allocation.fail_all = 1;
    CHECK(mm_codec_registry_init(&registry, &services, &memory, NULL, 101) == MM_STORAGE_OK);
    CHECK(mm_codec_registry_add(&registry, &b) == MM_STORAGE_MEMORY && !registry.count && b.refs == 1);
    allocation.fail_all = 0;
    allocation.fail_speed = 1;
    CHECK(mm_codec_registry_add(&registry, &b) == MM_STORAGE_OK && b.refs == 2);
    allocation.fail_speed = 0;
    while (registry.count < registry.storage.buffers[0].capacity / sizeof(void *))
        CHECK(mm_codec_registry_add(&registry, &b) == MM_STORAGE_OK);
    count = registry.count;
    allocation.fail_all = 1;
    CHECK(mm_codec_registry_add(&registry, &c) == MM_STORAGE_MEMORY && registry.count == count &&
          c.refs == 1);
    CHECK(mm_codec_registry_dispose(&registry) == MM_STORAGE_OK && b.refs == 1 && !allocation.live);
}
int main(void)
{
    runtime_cache();
    string_wire();
    names();
    codecs();
    packed_codecs();
    return 0;
}
