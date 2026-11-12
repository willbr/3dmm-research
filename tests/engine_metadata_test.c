#include "three_mm/chunk_metadata.h"
#include "three_mm/codec_registry.h"
#include "three_mm/string_wire.h"
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
static void *resize(void *c, void *old, ThreeMMU32 old_bytes, ThreeMMU32 bytes, int priority)
{
    Allocation *a = (Allocation *)c;
    void *fresh;
    ++a->calls;
    if (a->fail_all || (a->fail_speed && priority == THREE_MM_STORAGE_SPEED))
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
static void release(void *c, void *data, ThreeMMU32 bytes)
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
    ThreeMMStorageAllocator memory, other;
    ThreeMMChunkRuntime first, second;
    ThreeMMChunkRuntimeEntry e, previous;
    int i, calls;
    memset(&a, 0, sizeof(a));
    memset(&b, 0, sizeof(b));
    memory.context = &a;
    memory.resize = resize;
    memory.release = release;
    other = memory;
    other.context = &b;
    CHECK(three_mm_chunk_runtime_init(&first, &memory) == THREE_MM_CHUNK_OK);
    CHECK(three_mm_chunk_runtime_init(&second, &other) == THREE_MM_CHUNK_OK);
    CHECK(three_mm_chunk_runtime_get(&first, 1, 2) == 0 &&
          three_mm_chunk_runtime_set(&first, 1, 2, 0) == THREE_MM_CHUNK_OK && !a.calls);
    for (i = 20; i >= 0; --i)
        CHECK(three_mm_chunk_runtime_set(&first, (ThreeMMU32)i % 3, (ThreeMMU32)i, i + 10) == THREE_MM_CHUNK_OK);
    for (i = 0; i < 21; ++i)
    {
        CHECK(three_mm_chunk_runtime_get(&first, (ThreeMMU32)i % 3, (ThreeMMU32)i) == i + 10);
        CHECK(three_mm_chunk_runtime_at(&first, (ThreeMMU32)i, &e) == THREE_MM_CHUNK_OK);
        if (i)
            CHECK(previous.type < e.type || (previous.type == e.type && previous.number < e.number));
        previous = e;
    }
    for (i = 0; i < 21; i += 2)
        CHECK(three_mm_chunk_runtime_set(&first, (ThreeMMU32)i % 3, (ThreeMMU32)i, 0) == THREE_MM_CHUNK_OK);
    a.fail_all = 1;
    calls = a.calls;
    CHECK(three_mm_chunk_runtime_set(&first, 1, 1, -7) == THREE_MM_CHUNK_OK && a.calls == calls);
    /* Fill retained capacity, then make a missing insertion fail. */
    for (i = 100; first.count < first.storage.buffers[0].capacity / sizeof(e); ++i)
        CHECK(three_mm_chunk_runtime_set(&first, 9, (ThreeMMU32)i, i) == THREE_MM_CHUNK_OK);
    calls = (int)first.count;
    CHECK(three_mm_chunk_runtime_set(&first, 9, 9999, 3) == THREE_MM_CHUNK_MEMORY && first.count == (ThreeMMU32)calls);
    CHECK(three_mm_chunk_runtime_set(&first, 99, 99, 0) == THREE_MM_CHUNK_OK &&
          !three_mm_chunk_runtime_get(&first, 9, 9999));
    CHECK(three_mm_chunk_runtime_swap(&first, &second) == THREE_MM_CHUNK_OK && !first.count &&
          second.count == (ThreeMMU32)calls);
    CHECK(three_mm_chunk_runtime_get(&second, 1, 1) == -7);
    CHECK(three_mm_chunk_runtime_swap(&first, &second) == THREE_MM_CHUNK_OK && first.count == (ThreeMMU32)calls);
    while (first.count)
    {
        CHECK(three_mm_chunk_runtime_at(&first, 0, &e) == THREE_MM_CHUNK_OK);
        CHECK(three_mm_chunk_runtime_set(&first, e.type, e.number, 0) == THREE_MM_CHUNK_OK);
    }
    CHECK(first.created && first.storage.buffers[0].data != NULL);
    CHECK(three_mm_chunk_runtime_dispose(&first) == THREE_MM_CHUNK_OK && !a.live);
    CHECK(three_mm_chunk_runtime_dispose(&second) == THREE_MM_CHUNK_OK && !b.live);
    a.fail_all = 1;
    CHECK(three_mm_chunk_runtime_init(&first, &memory) == THREE_MM_CHUNK_OK);
    CHECK(three_mm_chunk_runtime_set(&first, 0xffffffffUL, 0xffffffffUL, 1) == THREE_MM_CHUNK_MEMORY && !first.count &&
          !first.created);
    a.fail_all = 0;
    a.fail_speed = 1;
    CHECK(three_mm_chunk_runtime_set(&first, 0xffffffffUL, 0xffffffffUL, 1) == THREE_MM_CHUNK_OK);
    CHECK(three_mm_chunk_runtime_get(&first, 0xffffffffUL, 0xffffffffUL) == 1);
    CHECK(three_mm_chunk_runtime_dispose(&first) == THREE_MM_CHUNK_OK && !a.live);
}
typedef struct Stream
{
    unsigned char bytes[1024];
    int reads, writes, fail_read, fail_write;
} Stream;
static int read_at(void *c, ThreeMMU32 offset, void *out, size_t bytes)
{
    Stream *s = (Stream *)c;
    CHECK(offset + bytes <= sizeof(s->bytes));
    ++s->reads;
    if (s->reads == s->fail_read)
        return 0;
    memcpy(out, s->bytes + offset, bytes);
    return 1;
}
static int write_at(void *c, ThreeMMU32 offset, const void *in, size_t bytes)
{
    Stream *s = (Stream *)c;
    CHECK(offset + bytes <= sizeof(s->bytes));
    ++s->writes;
    if (s->writes == s->fail_write)
        return 0;
    memcpy(s->bytes + offset, in, bytes);
    return 1;
}
static ThreeMMI32 convert(void *c, const void *in, ThreeMMU32 bytes, int encoding, void *out, ThreeMMU32 capacity)
{
    unsigned *unit = (unsigned *)c;
    return (ThreeMMI32)(*unit == 1 ? three_mm_text_ansi(NULL, in, bytes, encoding, (char *)out, capacity)
                                   : three_mm_text_utf16(NULL, in, bytes, encoding, (unsigned short *)out, capacity));
}
static ThreeMMU32 hash_bytes(ThreeMMU32 hash, const void *p, size_t bytes)
{
    const unsigned char *b = (const unsigned char *)p;
    while (bytes--)
        hash = (hash ^ *b++) * 16777619UL;
    return hash;
}
static ThreeMMU32 hash_word(ThreeMMU32 hash, ThreeMMU32 word)
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
    ThreeMMReadStream reader;
    ThreeMMStringWriter writer;
    ThreeMMStringWire text;
    unsigned char buffer[514], encoded[1024], wire[1024];
    unsigned unit = 1, lengths[4] = {0, 1, 31, 255};
    ThreeMMU32 count, i, end, bytes, hash = 2166136261UL, total = 0, record[9], offset;
    int kind, length, malformed, counted, path, ok;
    memset(&text, 0, sizeof(text));
    text.buffer = buffer;
    text.unit = 1;
    text.current_encoding = THREE_MM_TEXT_WINDOWS;
    text.swapped_unicode_encoding = THREE_MM_TEXT_UTF16_BE;
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
                        ok = three_mm_string_wire_read(&text, &reader, offset, counted ? &end : NULL);
                        bytes = three_mm_string_wire_size(&text);
                        CHECK(three_mm_string_wire_get(&text, encoded, sizeof(encoded)));
                        record[0] = (ThreeMMU32)kind;
                        record[1] = lengths[length];
                        record[2] = (ThreeMMU32)malformed;
                        record[3] = (ThreeMMU32)counted;
                        record[4] = (ThreeMMU32)path;
                        record[5] = (ThreeMMU32)ok;
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
        CHECK(!three_mm_string_wire_read(&text, &reader, 0, &end) && end == 99 && !buffer[0] && !buffer[1]);
    }
    source.fail_read = 0;
    source.reads = 0;
    CHECK(three_mm_string_wire_read(&text, &reader, 0, NULL));
    writer.context = &destination;
    writer.length = 12;
    writer.write_at = write_at;
    for (i = 1; i <= 2; ++i)
    {
        memset(&destination, 0, sizeof(destination));
        destination.fail_write = (int)i;
        CHECK(!three_mm_string_wire_write(&text, &writer, 7) && destination.writes == (int)i);
    }
    memset(&destination, 0, sizeof(destination));
    CHECK(three_mm_string_wire_write(&text, &writer, 7) && destination.writes == 2);
    CHECK(!memcmp(destination.bytes + 7, source.bytes, 5));
    CHECK(!three_mm_string_wire_write(&text, &writer, 8) && destination.writes == 2);
    unit = 2;
    text.unit = 2;
    text.current_encoding = THREE_MM_TEXT_UTF16_LE;
    source.reads = 0;
    CHECK(three_mm_string_wire_read(&text, &reader, 0, NULL) && three_mm_string_wire_size(&text) == 8);
    text.current_encoding = THREE_MM_TEXT_UTF16_BE;
    text.native_big_endian = 1;
    text.swapped_unicode_encoding = THREE_MM_TEXT_UTF16_LE;
    memset(&source, 0, sizeof(source));
    memcpy(source.bytes, "\4\4\0\1\0A\0\0", 8);
    reader.length = 8;
    CHECK(three_mm_string_wire_read(&text, &reader, 0, NULL) &&
          three_mm_string_wire_get(&text, encoded, sizeof(encoded)) && !memcmp(encoded, source.bytes, 8));
}
typedef struct Names
{
    unsigned char bytes[128], decoded[257];
    ThreeMMU32 count;
    int locked, fail, pins;
    ThreeMMStringWire text;
} Names;
static ThreeMMU32 name_count(void *c)
{
    (void)c;
    return 1;
}
static int name_entry(void *c, ThreeMMU32 i, ThreeMMChunkEntry *out)
{
    Names *n = (Names *)c;
    CHECK(i == 0);
    memset(out, 0, sizeof(*out));
    out->child_count = 1;
    out->variable_bytes = n->count;
    return 1;
}
static int name_insert(void *c, ThreeMMU32 i, ThreeMMU32 offset, ThreeMMU32 bytes)
{
    Names *n = (Names *)c;
    CHECK(!i && !n->locked && offset <= n->count && n->count + bytes <= 128);
    if (n->fail)
        return 0;
    memmove(n->bytes + offset + bytes, n->bytes + offset, n->count - offset);
    n->count += bytes;
    return 1;
}
static void name_erase(void *c, ThreeMMU32 i, ThreeMMU32 offset, ThreeMMU32 bytes)
{
    Names *n = (Names *)c;
    CHECK(!i && !n->locked && offset + bytes <= n->count);
    n->count -= bytes;
    memmove(n->bytes + offset, n->bytes + offset + bytes, n->count - offset);
}
static void *name_pin(void *c, ThreeMMU32 i)
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
static int name_decode(void *c, const void *data, ThreeMMU32 bytes)
{
    Names *n = (Names *)c;
    ThreeMMMemoryStream source;
    CHECK(n->locked == 1);
    CHECK(three_mm_memory_stream_init(&source, data, bytes));
    return three_mm_string_wire_read(&n->text, &source.stream, 0, NULL);
}
static void name_clear(void *c)
{
    Names *n = (Names *)c;
    n->decoded[0] = n->decoded[1] = 0;
}
static ThreeMMU32 name_chars(void *c)
{
    return ((Names *)c)->decoded[0];
}
static void names(void)
{
    Names n;
    ThreeMMChunkNames store;
    ThreeMMChunkNameDecoder decoder;
    ThreeMMChunkNameValue value;
    int pins;
    unsigned char wire[] = {3, 3, 3, 'a', 'b', 'c', 0}, short_wire[] = {3, 3, 1, 'x', 0}, before[128];
    memset(&n, 0, sizeof(n));
    n.count = 12;
    memcpy(n.bytes, "child prefix", 12);
    n.text.buffer = n.decoded;
    n.text.unit = 1;
    n.text.current_encoding = THREE_MM_TEXT_WINDOWS;
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
    CHECK(three_mm_chunk_name_set(&store, 0, &value) == THREE_MM_CHUNK_OK && n.count == 19 && !n.locked);
    CHECK(three_mm_chunk_name_get(&store, 0, &decoder) && n.decoded[0] == 3 && !n.locked);
    value.wire = short_wire;
    value.bytes = sizeof(short_wire);
    value.characters = 1;
    CHECK(three_mm_chunk_name_set(&store, 0, &value) == THREE_MM_CHUNK_OK && n.count == 17 &&
          !memcmp(n.bytes, "child prefix", 12));
    memcpy(before, n.bytes, sizeof(before));
    pins = n.pins;
    n.fail = 1;
    value.wire = wire;
    value.bytes = sizeof(wire);
    value.characters = 3;
    CHECK(three_mm_chunk_name_set(&store, 0, &value) == THREE_MM_CHUNK_MEMORY && n.count == 17 &&
          !memcmp(before, n.bytes, sizeof(before)) && n.pins == pins);
    n.bytes[16] = 1;
    CHECK(!three_mm_chunk_name_get(&store, 0, &decoder) && !n.decoded[0] && !n.locked);
    CHECK(three_mm_chunk_name_set(&store, 0, NULL) == THREE_MM_CHUNK_OK && n.count == 12);
    pins = n.pins;
    CHECK(!three_mm_chunk_name_get(&store, 0, &decoder) && n.pins == pins);
}
typedef struct Codec
{
    int refs, id, encode, decode;
    ThreeMMI32 format;
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
static int can_do(void *c, void *codec, int encode, ThreeMMI32 format)
{
    CodecContext *ctx = (CodecContext *)c;
    Codec *value = (Codec *)codec;
    CHECK(ctx->queries < 128);
    ctx->queried[ctx->queries++] = value->id;
    return value->format == format && (encode ? value->encode : value->decode);
}
static void codecs(void)
{
    Allocation allocation;
    CodecContext context;
    Codec a = {1, 1, 0, 1, 101}, b = {1, 2, 1, 1, 101}, c = {1, 3, 1, 0, 101};
    ThreeMMStorageAllocator memory;
    ThreeMMCodecServices services;
    ThreeMMCodecRegistry registry;
    ThreeMMU32 count;
    memset(&allocation, 0, sizeof(allocation));
    memset(&context, 0, sizeof(context));
    memory.context = &allocation;
    memory.resize = resize;
    memory.release = release;
    services.context = &context;
    services.retain = retain_codec;
    services.release = release_codec;
    services.can_do = can_do;
    CHECK(three_mm_codec_registry_init(&registry, &services, &memory, &a, 101) == THREE_MM_STORAGE_OK && a.refs == 2);
    CHECK(three_mm_codec_registry_add(&registry, &b) == THREE_MM_STORAGE_OK &&
          three_mm_codec_registry_add(&registry, &c) == THREE_MM_STORAGE_OK);
    CHECK(three_mm_codec_registry_find(&registry, 1, 101) == &c && context.queried[0] == 1 && context.queried[1] == 3);
    context.queries = 0;
    CHECK(three_mm_codec_registry_find(&registry, 0, 101) == &a && context.queries == 1);
    CHECK(three_mm_codec_registry_add(&registry, &b) == THREE_MM_STORAGE_OK && b.refs == 3);
    CHECK(three_mm_codec_registry_find(&registry, 1, 101) == &b);
    CHECK(three_mm_codec_registry_default(&registry, 303) == THREE_MM_STORAGE_OK && registry.default_format == 303);
    CHECK(three_mm_codec_registry_default(&registry, 0) == THREE_MM_STORAGE_ARGUMENT && registry.default_format == 303);
    context.queries = 0;
    CHECK(!three_mm_codec_registry_find(&registry, 1, 0) && !context.queries);
    CHECK(three_mm_codec_registry_dispose(&registry) == THREE_MM_STORAGE_OK && !allocation.live && a.refs == 1 &&
          b.refs == 1 && c.refs == 1);
    CHECK(context.releases == 4 && context.released[0] == 1 && context.released[1] == 2 && context.released[2] == 3 &&
          context.released[3] == 2);
    context.releases = context.queries = 0;
    allocation.fail_all = 1;
    CHECK(three_mm_codec_registry_init(&registry, &services, &memory, NULL, 101) == THREE_MM_STORAGE_OK);
    CHECK(three_mm_codec_registry_add(&registry, &b) == THREE_MM_STORAGE_MEMORY && !registry.count && b.refs == 1);
    allocation.fail_all = 0;
    allocation.fail_speed = 1;
    CHECK(three_mm_codec_registry_add(&registry, &b) == THREE_MM_STORAGE_OK && b.refs == 2);
    allocation.fail_speed = 0;
    while (registry.count < registry.storage.buffers[0].capacity / sizeof(void *))
        CHECK(three_mm_codec_registry_add(&registry, &b) == THREE_MM_STORAGE_OK);
    count = registry.count;
    allocation.fail_all = 1;
    CHECK(three_mm_codec_registry_add(&registry, &c) == THREE_MM_STORAGE_MEMORY && registry.count == count &&
          c.refs == 1);
    CHECK(three_mm_codec_registry_dispose(&registry) == THREE_MM_STORAGE_OK && b.refs == 1 && !allocation.live);
}
int main(void)
{
    runtime_cache();
    string_wire();
    names();
    codecs();
    return 0;
}
