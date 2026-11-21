/* Help document policy, wire compatibility and interrupted host transactions. */
#include "mm/help_text.h"
#define MM_TEXT_FORMAT_NO_LATE_INPUT
#include "text_format_test_host.h"
#include "text_argument_view_fixture.h"
typedef struct HelpHost
{
    Host h;
    MMHelpEditServices edit;
    MMHelpArgumentServices arg;
    MMHelpReadServices read;
    MMHelpSaveServices save;
    MMHelpTopic topic, output;
    MMCollectionStorage argument_storage;
    MMGroup argument_rows;
    MMCollectionStorage group_storage;
    MMGroupLocation group_location;
    unsigned char group_record[4 + 523];
    unsigned char wire[36], allocation[64], payload[64];
    MMI32 wire_size, payload_size;
    MMU32 published[4];
    int frees, bad, partial, forest, source, owned, base_result;
    int name_size, decoded, cleared, missing, replacements, copy, erase_result, reject_adopt;
    char content[128];
    MMI32 length, ids[8];
    unsigned id_count, unit;
} HelpHost;
static MMI32 help_characters(void *v)
{
    (void)v;
    return 9;
}
static int name_bytes(void *v, void *out, MMI32 capacity, MMI32 *n)
{
    HelpHost *h = (HelpHost *)v;
    CHECK(strchr(h->h.log, 'U') && capacity >= h->name_size);
    memcpy(out, "name", (size_t)h->name_size);
    *n = h->name_size;
    return effect(&h->h, 'n');
}
static MMGroup group_view(void *v)
{
    HelpHost *h = (HelpHost *)v;
    CHECK(!h->group_storage.buffers[0].locks);
    CHECK(h->h.length >= 0 && h->h.length <= (MMI32)sizeof(h->h.bytes));
    h->group_location.offset = h->h.free ? -1 : 0;
    h->group_location.bytes = h->h.free ? 0 : 4 + h->h.length;
    memcpy(h->group_record + 4, h->h.bytes, (size_t)h->h.length);
    event(&h->h, 'q');
    return text_argument_view(&h->group_storage, h->group_record, sizeof(h->group_record), &h->group_location, 1);
}
static int read_name(void *v, const void *p, MMI32 n)
{
    HelpHost *h = (HelpHost *)v;
    CHECK(h->group_storage.buffers[0].locks == 1 && h->published[MM_HELP_TARGET] == 0x12345678UL);
    CHECK(n == h->name_size && !memcmp(p, "name", (size_t)n));
    ++h->decoded;
    return effect(&h->h, 'd');
}
static int clear_name(void *v)
{
    HelpHost *h = (HelpHost *)v;
    ++h->cleared;
    return effect(&h->h, 'c');
}
static void help_publish(void *v, int kind, MMU32 value)
{
    HelpHost *h = (HelpHost *)v;
    if (kind != MM_HELP_TARGET)
        CHECK(!h->group_storage.buffers[0].locks);
    h->published[kind] = value;
    event(&h->h, "fegt"[kind]);
}
static int allocate_payload(void *v, MMI32 n, void **p)
{
    HelpHost *h = (HelpHost *)v;
    int r = effect(&h->h, 'a');
    CHECK(n <= 64);
    if (r > 0 || h->partial)
        *p = h->allocation;
    return r;
}
static void free_payload(void *v, void *p)
{
    HelpHost *h = (HelpHost *)v;
    CHECK(p == h->allocation);
    ++h->frees;
    event(&h->h, 'z');
}
static int insert_payload(void *v, const void *p, MMI32 n, MMI32 at, MMI32 deleted)
{
    HelpHost *h = (HelpHost *)v;
    CHECK(at == 2 && deleted == 3 && n <= 64);
    h->payload_size = n;
    memcpy(h->payload, p, (size_t)n);
    return effect(&h->h, 'i');
}
static void bad_group(void *v)
{
    ++((HelpHost *)v)->bad;
}
static int notify(void *v, MMI32 at, MMI32 n)
{
    CHECK(at == 2 && n == 3);
    return effect((Host *)v, 'V');
}
static int help_dirty(void *v)
{
    HelpHost *h = (HelpHost *)v;
    CHECK(!memcmp(&h->topic, &h->output, sizeof(h->topic)));
    return effect(&h->h, 'D');
}
static int base_argument(void *v, MMI32 i, unsigned kind, int order, int system)
{
    HelpHost *h = (HelpHost *)v;
    CHECK(i == 0 && kind && (order == 1 || order == 256) && system == 3);
    event(&h->h, 'B');
    return h->base_result;
}
static MMGroup argument_rows(void *v)
{
    HelpHost *h = (HelpHost *)v;
    event(&h->h, 's');
    return h->argument_rows;
}
static int is_forest(void *v, void *f, MMU32 t, MMU32 n)
{
    HelpHost *h = (HelpHost *)v;
    int r = effect(&h->h, 'F');
    CHECK(f == &h->source && t == MM_HELP_TOPIC && n == 9);
    return r <= 0 ? r : h->forest;
}
static int open_forest(void *v, void *f, MMU32 t, MMU32 n, void **out)
{
    HelpHost *h = (HelpHost *)v;
    int r = effect(&h->h, 'O');
    CHECK(f == &h->source && t == MM_HELP_TOPIC && n == 9);
    if (r > 0 || h->partial)
    {
        ++h->owned;
        *out = &h->owned;
    }
    return r;
}
static int first_topic(void *v, void *f, MMU32 t, MMU32 *n)
{
    HelpHost *h = (HelpHost *)v;
    CHECK(f == &h->owned && t == MM_HELP_TOPIC);
    *n = 9;
    return effect(&h->h, 'N');
}
static void retain_file(void *v, void *f)
{
    HelpHost *h = (HelpHost *)v;
    CHECK(f == &h->source);
    ++h->source;
    event(&h->h, '+');
}
static void release_file(void *v, void *f)
{
    HelpHost *h = (HelpHost *)v;
    int *p = (int *)f;
    CHECK((p == &h->source || p == &h->owned) && *p > 0);
    --*p;
    event(&h->h, '-');
}
static int header(void *v, void *f, MMU32 t, MMU32 n, void *out, MMI32 *size)
{
    HelpHost *h = (HelpHost *)v;
    (void)f;
    CHECK(t == MM_HELP_TOPIC && n == 9);
    *size = h->wire_size;
    memcpy(out, h->wire, 36);
    return effect(&h->h, 'H');
}
static int child(void *v, void *f, MMU32 t, MMU32 n, MMTextChunk *out)
{
    (void)f;
    CHECK(t == MM_HELP_TOPIC && n == 9);
    out->type = 17;
    out->number = 23;
    return effect((Host *)v, 'K');
}
static int rich_read(void *v, void *f, const MMTextChunk *c, int copy)
{
    HelpHost *h = (HelpHost *)v;
    (void)f;
    CHECK(c->type == 17 && c->number == 23);
    h->copy = copy;
    return effect(&h->h, 'R');
}
static int internal(void *v)
{
    return effect((Host *)v, 'I');
}
static int suspend(void *v)
{
    return effect((Host *)v, 'S');
}
static MMI32 text_length(void *v)
{
    return ((HelpHost *)v)->length;
}
static int character(void *v, MMI32 cp, MMU32 *out)
{
    HelpHost *h = (HelpHost *)v;
    CHECK(cp >= 0 && cp < h->length);
    *out = (unsigned char)h->content[cp];
    return effect(&h->h, 'q');
}
static int string_value(void *v, MMI32 id, MMMovieTitle *out)
{
    HelpHost *h = (HelpHost *)v;
    const char *text = id == 12 ? "one~99" : id == 0 ? "zero" : "";
    unsigned short wide[16];
    unsigned i, n = (unsigned)strlen(text);
    int r = effect(&h->h, 'T');
    CHECK(h->id_count < 8);
    h->ids[h->id_count++] = id;
    for (i = 0; i < n; ++i)
        wide[i] = (unsigned short)text[i];
    CHECK(mm_movie_title_set(out, h->unit == 1 ? (const void *)text : (const void *)wide, n));
    return r <= 0 ? r : id == 12 || id == 0;
}
static int replace_text(void *v, const MMMovieTitle *text, MMI32 at, MMI32 deleted)
{
    HelpHost *h = (HelpHost *)v;
    const unsigned char *narrow = (const unsigned char *)mm_movie_title_text(text);
    const unsigned short *wide = (const unsigned short *)mm_movie_title_text(text);
    MMHelpTopic zero;
    unsigned i;
    int r = effect(&h->h, 'P');
    memset(&zero, 0, sizeof(zero));
    CHECK(!memcmp(&h->output, &zero, sizeof(h->output)));
    ++h->replacements;
    if (r > 0)
    {
        CHECK(h->length - deleted + text->count < 128);
        memmove(h->content + at + text->count, h->content + at + deleted, (size_t)(h->length - at - deleted));
        for (i = 0; i < text->count; ++i)
            h->content[at + i] = (char)(h->unit == 1 ? narrow[i] : wide[i]);
        h->length += (MMI32)text->count - deleted;
    }
    return r;
}
static void missing_string(void *v)
{
    ++((HelpHost *)v)->missing;
}
static void save_type(void *v, MMU32 type)
{
    CHECK(type == MM_HELP_TOPIC);
    event((Host *)v, 'Y');
}
static int save_add(void *v, MMU32 type, MMU32 *number)
{
    CHECK(type == MM_HELP_TOPIC);
    *number = 9;
    return effect((Host *)v, 'A');
}
static int save_write(void *v, const unsigned char *bytes)
{
    HelpHost *h = (HelpHost *)v;
    CHECK(!memcmp(bytes, h->wire, 36));
    return effect(&h->h, 'W');
}
static int save_rich(void *v, MMTextChunk *out)
{
    out->type = 17;
    out->number = 23;
    return effect((Host *)v, 'R');
}
static int save_adopt(void *v, const MMTextChunk *p, const MMTextChunk *c)
{
    HelpHost *h = (HelpHost *)v;
    CHECK(p->type == MM_HELP_TOPIC && p->number == 9 && c->type == 17 && c->number == 23);
    return h->reject_adopt ? (event(&h->h, 'J'), 0) : effect(&h->h, 'J');
}
static int save_erase(void *v, const MMTextChunk *p)
{
    HelpHost *h = (HelpHost *)v;
    if (h->erase_result && p->type != MM_HELP_TOPIC)
    {
        event(&h->h, 'c');
        return h->erase_result;
    }
    return effect((Host *)v, p->type == MM_HELP_TOPIC ? 'p' : 'c');
}
static void save_error(void *v)
{
    event((Host *)v, '!');
}
static void help_init(HelpHost *h, unsigned unit, int big)
{
    memset(h, 0, sizeof(*h));
    init(&h->h, unit, big);
    h->h.group = h->h.bytes;
    h->unit = unit;
    h->source = 1;
    h->topic.balloon = 0x11223344UL;
    h->topic.hid = -12;
    h->topic.target = 13;
    h->topic.script = 0x56789abcUL;
    h->topic.width = 15;
    h->topic.height = -16;
    h->topic.sound_type = 0x534e4449UL;
    h->topic.sound_number = 0x10203040UL;
    mm_help_topic_encode(&h->topic, 3, big, h->wire);
    h->wire_size = 36;
    h->edit.context = h;
    h->edit.properties = h->h.edit.format.properties;
    h->edit.characters = help_characters;
    h->edit.undo = undo;
    h->edit.reserve = reserve;
    h->edit.cancel = cancel;
    h->edit.commit = commit;
    h->edit.group_name = name_bytes;
    CHECK(mm_storage_init(&h->group_storage, NULL) == MM_STORAGE_OK);
    h->edit.arguments = group_view;
    h->edit.read_name = read_name;
    h->edit.clear_name = clear_name;
    h->edit.publish = help_publish;
    h->edit.allocate = allocate_payload;
    h->edit.free = free_payload;
    h->edit.insert = insert_payload;
    h->edit.validate = validate;
    h->edit.bad_group = bad_group;
    h->edit.notify = notify;
    h->edit.dirty = help_dirty;
    h->arg.context = h;
    h->arg.base = base_argument;
    h->arg.arguments = argument_rows;
    h->read.context = h;
    h->read.forest = is_forest;
    h->read.open_forest = open_forest;
    h->read.first = first_topic;
    h->read.retain = retain_file;
    h->read.release = release_file;
    h->read.header = header;
    h->read.child = child;
    h->read.rich = rich_read;
    h->read.internal = internal;
    h->read.suspend = suspend;
    h->read.characters = text_length;
    h->read.character = character;
    h->read.string = string_value;
    h->read.replace = replace_text;
    h->read.missing = missing_string;
    h->read.validate = validate;
    h->save.context = h;
    h->save.publish_type = save_type;
    h->save.add = save_add;
    h->save.write = save_write;
    h->save.rich = save_rich;
    h->save.adopt = save_adopt;
    h->save.erase = save_erase;
    h->save.error = save_error;
}
static int read_topic(HelpHost *h, int flags)
{
    return mm_help_read(&h->output, &h->source, MM_HELP_TOPIC, 9, (MMU32)flags, 1, h->unit, h->h.config.text.big,
                        &h->read);
}
static void codecs(int big)
{
    HelpHost h;
    MMHelpTopic saved;
    unsigned char other[36];
    int n;
    help_init(&h, 1, big);
    CHECK(mm_help_topic_decode(&h.output, h.wire, 36, big));
    CHECK(!memcmp(&h.output, &h.topic, sizeof(h.topic)));
    CHECK(mm_help_topic_decode(&h.output, h.wire, 28, big));
    CHECK(h.output.sound_type == 0 && h.output.sound_number == MM_TEXT_NIL);
    mm_help_topic_encode(&h.topic, 99, !big, other);
    CHECK(mm_help_topic_decode(&h.output, other, 36, big));
    CHECK(h.output.balloon == h.topic.balloon && h.output.hid == -12 && h.output.height == -16);
    CHECK(h.output.sound_type == 0x49444e53UL && h.output.sound_number == 0x40302010UL);
    saved = h.output;
    for (n = 0; n < 40; ++n)
        if (n != 28 && n != 36)
        {
            CHECK(!mm_help_topic_decode(&h.output, h.wire, n, big));
            CHECK(!memcmp(&saved, &h.output, sizeof(saved)));
        }
    h.wire[0] = h.wire[1] = 0;
    CHECK(!mm_help_topic_decode(&h.output, h.wire, 36, big));
    mm_help_topic_init(&h.output);
    CHECK(h.output.balloon == MM_TEXT_NIL && h.output.script == MM_TEXT_NIL && h.output.sound_number == MM_TEXT_NIL &&
          h.output.sound_type == 0);
}
static int open_argument(HelpHost *h, unsigned kind, int order, int big)
{
    const MMU32 fixed_word = 0x12345678UL;
    MMU32 fixed;
    int result;
    memset(&h->argument_rows, 0, sizeof(h->argument_rows));
    CHECK(mm_storage_init(&h->argument_storage, NULL) == MM_STORAGE_OK);
    h->argument_rows.storage = &h->argument_storage;
    h->argument_rows.fixed_bytes = 4;
    CHECK(h->h.length >= 0 && h->h.length <= 16);
    CHECK(mm_storage_ensure(&h->argument_storage, 20, 8, 0) == MM_STORAGE_OK);
    memcpy(h->argument_storage.buffers[0].data + 4, h->h.bytes, 16);
    CHECK(mm_group_add(&h->argument_rows, (MMU32)h->h.length, h->h.bytes, &fixed_word, NULL) == MM_STORAGE_OK);
    result = mm_help_open_argument(0, kind, order, 3, big, &h->arg);
    memcpy(&fixed, h->argument_storage.buffers[0].data, sizeof(fixed));
    CHECK(fixed == fixed_word);
    memcpy(h->h.bytes, h->argument_storage.buffers[0].data + 4, 16);
    CHECK(mm_storage_dispose(&h->argument_storage) == MM_STORAGE_OK);
    return result;
}
static void arguments(int big)
{
    HelpHost h;
    unsigned char native[12];
    int base, n, kind;
    for (base = -2; base <= 1; ++base)
    {
        help_init(&h, 1, big);
        h.base_result = base;
        CHECK(open_argument(&h, 64, 1, big) == base);
        CHECK(!strcmp(h.h.log, base ? "B" : "Bs"));
    }
    for (kind = 0; kind < 4; ++kind)
    {
        help_init(&h, 1, big);
        CHECK(mm_help_insert(kind == 2, 0x12345678UL, 0x10203040UL, 0, 0, 2, 3, big, &h.edit));
        memcpy(native, h.payload, 12);
        if (kind == 1)
            memcpy(native, big ? "EDIT" : "TIDE", 4);
        if (kind == 3)
            memcpy(native, "????", 4);
        for (n = 3; n <= 15; ++n)
        {
            int i, j, expected = kind != 3 && n >= (kind == 2 ? 12 : 8);
            help_init(&h, 1, big);
            memset(h.h.bytes, 0x99, 16);
            for (i = 0; i < 3; ++i)
                for (j = 0; j < 4; ++j)
                    h.h.bytes[i * 4 + j] = native[i * 4 + 3 - j];
            h.h.length = n;
            CHECK(open_argument(&h, 192, 256, big) == expected);
            if (n >= 4)
                CHECK(!memcmp(h.h.bytes, native, 4));
            if (expected)
                CHECK(!memcmp(h.h.bytes, native, (size_t)(kind == 2 ? 12 : 8)));
            CHECK(h.h.bytes[12] == 0x99);
        }
    }
    help_init(&h, 1, big);
    h.h.length = 7;
    memcpy(h.h.bytes, "x\x12\x34\x56\x78yz", 7);
    CHECK(open_argument(&h, 64, 256, big));
    CHECK(h.h.bytes[0] == 'x' && h.h.bytes[1] == 0x78 && h.h.bytes[4] == 0x12 && h.h.bytes[5] == 'y');
    CHECK(!open_argument(&h, 194, 1, big));
}
static void editing(int big)
{
    HelpHost h;
    const char *faults = "UnMV";
    unsigned i;
    int r;
    help_init(&h, 1, big);
    CHECK(mm_help_group(2, 2, 3, 7, big, &h.edit) && !h.h.used);
    h.name_size = 4;
    CHECK(mm_help_group(5, 2, 3, 0x12345678UL, big, &h.edit));
    CHECK(h.h.length == 9 && h.h.bytes[0] == 3 && h.h.count == 2);
    h.h.used = 0;
    CHECK(mm_help_grouped(3, big, 1, &h.edit));
    CHECK(!strcmp(h.h.log, "eqtdgf"));
    CHECK(h.published[MM_HELP_FIRST] == 2 && h.published[MM_HELP_END] == 5 && h.decoded == 1);
    h.h.used = 0;
    h.h.fail = 'd';
    h.h.result = 0;
    CHECK(mm_help_grouped(3, big, 1, &h.edit));
    h.h.seen = 0;
    h.h.result = -2;
    CHECK(mm_help_grouped(3, big, 1, &h.edit) == -2 && !h.group_storage.buffers[0].locks);
    h.h.fail = 0;
    h.h.bytes[0] = 0;
    CHECK(!mm_help_grouped(3, big, 1, &h.edit) && h.bad == 1 && !h.group_storage.buffers[0].locks);
    CHECK(h.published[MM_HELP_GROUP] == 0 && h.published[MM_HELP_TARGET] == MM_TEXT_NIL);
    h.h.length = 4;
    CHECK(!mm_help_grouped(3, big, 0, &h.edit) && h.bad == 2 && !h.group_storage.buffers[0].locks);
    for (i = 0; faults[i]; ++i)
        for (r = -2; r < 0; ++r)
        {
            help_init(&h, 1, big);
            h.h.fail = faults[i];
            h.h.result = r;
            CHECK(mm_help_group(2, 5, 3, 7, big, &h.edit) == r);
            CHECK(!strchr(h.h.log, 'C'));
            CHECK((faults[i] == 'M' || faults[i] == 'V') ? h.h.count == 2 : !h.h.count);
        }
    help_init(&h, 1, big);
    h.h.reserve_false = 1;
    h.h.fail = 'C';
    h.h.result = -2;
    CHECK(mm_help_group(2, 5, 3, 7, big, &h.edit) == -2 && h.h.free);
    for (r = -2; r <= 0; ++r)
    {
        help_init(&h, 1, big);
        h.h.fail = 'R';
        h.h.result = r;
        CHECK(mm_help_group(2, 5, 3, 7, big, &h.edit) == r);
        CHECK(r ? !h.h.free && !strchr(h.h.log, 'C') : h.h.free && strchr(h.h.log, 'C'));
        help_init(&h, 1, big);
        h.h.fail = 'A';
        h.h.result = r;
        CHECK(mm_help_group(2, 5, 3, 7, big, &h.edit) == r);
        CHECK(!h.h.count && (r ? !strchr(h.h.log, 'C') : strchr(h.h.log, 'C') != 0));
        help_init(&h, 1, big);
        h.h.fail = 'i';
        h.h.result = r;
        CHECK(mm_help_insert(1, 42, 13, "abc", 3, 2, 3, big, &h.edit) == r && h.frees == 1);
        CHECK(h.payload_size == 15 && !memcmp(h.payload + 12, "abc", 3));
        help_init(&h, 1, big);
        h.partial = 1;
        h.h.fail = 'a';
        h.h.result = r;
        CHECK(mm_help_insert(0, 42, 0, "abc", 3, 2, 3, big, &h.edit) == r && h.frees == 1 && !h.payload_size);
    }
    help_init(&h, 1, big);
    CHECK(!mm_help_grouped(3, big, 1, &h.edit));
    CHECK(h.published[MM_HELP_FIRST] == 0 && h.published[MM_HELP_END] == 9 && h.cleared == 1);
    CHECK(mm_help_group(2, 5, 0, 0, big, &h.edit) && !h.h.length);
    h.h.fail = 'D';
    h.h.result = -1;
    CHECK(mm_help_topic_set(&h.output, &h.topic, &h.edit) == -1);
}
static void reading(unsigned unit, int big)
{
    HelpHost h;
    const char *faults = "FONHKRISqTP";
    unsigned i;
    int r, forest;
    for (forest = 0; forest <= 1; ++forest)
    {
        help_init(&h, unit, big);
        h.forest = forest;
        memcpy(h.content, "~12x~0~99\r", 10);
        h.length = 10;
        CHECK(read_topic(&h, 3));
        CHECK(h.length == 12 && !memcmp(h.content, "one~99xzero\r", 12));
        CHECK(h.source == 1 && !h.owned && h.copy && h.missing == 1 && h.id_count == 3);
        CHECK(h.ids[0] == 12 && h.ids[1] == 0 && h.ids[2] == 99);
        CHECK(!memcmp(&h.output, &h.topic, sizeof(h.topic)));
        for (i = 0; faults[i]; ++i)
            for (r = -2; r <= 0; ++r)
            {
                if (!forest && (faults[i] == 'O' || faults[i] == 'N'))
                    continue;
                help_init(&h, unit, big);
                h.forest = forest;
                h.h.fail = faults[i];
                h.h.result = r;
                h.partial = 1;
                memcpy(h.content, "~12x~0\r", 8);
                h.length = 8;
                if (!r && (faults[i] == 'F' || faults[i] == 'T' || faults[i] == 'P'))
                    CHECK(read_topic(&h, 3) == 1);
                else
                {
                    MMHelpTopic zero;
                    memset(&zero, 0, sizeof(zero));
                    CHECK(read_topic(&h, 3) == r);
                    CHECK(!memcmp(&h.output, &zero, sizeof(zero)));
                }
                CHECK(h.source == 1 && !h.owned);
            }
    }
    help_init(&h, unit, big);
    h.wire_size = 29;
    CHECK(!read_topic(&h, 0) && h.source == 1);
    help_init(&h, unit, big);
    memcpy(h.content, "~4294967296\r", 12);
    h.length = 12;
    CHECK(read_topic(&h, 2) && h.ids[0] == 0 && h.length == 5);
}
static void saving(int big)
{
    HelpHost h;
    MMTextChunk out;
    const char *faults = "AWRJ";
    const char *logs[] = {"YA!", "YAWp!", "YAWRp!", "YAWRJcp!"};
    unsigned i;
    int r;
    help_init(&h, 1, big);
    CHECK(mm_help_save(&h.topic, &out, 3, big, &h.save));
    CHECK(!strcmp(h.h.log, "YAWRJ") && out.type == MM_HELP_TOPIC && out.number == 9);
    for (i = 0; i < 4; ++i)
        for (r = -2; r <= 0; ++r)
        {
            help_init(&h, 1, big);
            h.h.fail = faults[i];
            h.h.result = r;
            CHECK(mm_help_save(&h.topic, &out, 3, big, &h.save) == r);
            CHECK(out.type == MM_HELP_TOPIC && out.number == 9);
            if (!r)
                CHECK(!strcmp(h.h.log, logs[i]));
            else
                CHECK(!strchr(h.h.log, '!') && !strchr(h.h.log, 'p') && !strchr(h.h.log, 'c'));
        }
    for (r = -2; r < 0; ++r)
    {
        help_init(&h, 1, big);
        h.reject_adopt = 1;
        h.erase_result = r;
        CHECK(mm_help_save(&h.topic, &out, 3, big, &h.save) == r);
        CHECK(!strcmp(h.h.log, "YAWRJc"));
        help_init(&h, 1, big);
        h.reject_adopt = 1;
        h.h.fail = 'p';
        h.h.result = r;
        CHECK(mm_help_save(&h.topic, &out, 3, big, &h.save) == r);
        CHECK(!strcmp(h.h.log, "YAWRJcp"));
    }
}
int main(void)
{
    int big;
    unsigned unit;
    for (big = 0; big <= 1; ++big)
    {
        codecs(big);
        arguments(big);
        editing(big);
        saving(big);
        for (unit = 1; unit <= 2; ++unit)
            reading(unit, big);
    }
    return 0;
}
