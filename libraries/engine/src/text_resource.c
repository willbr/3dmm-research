/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "mm/text_resource.h"
#include "mm/chunk_ops.h"
#include "mm/collections.h"
#include "mm/font_resource.h"
#include "mm/text.h"
#include "mm/material_resource.h"
#include "mm/string_wire.h"
#include <stdlib.h>
#include <string.h>

struct MMTemplateOwner
{
    MMResourceObject binding;
    MMReference references;
    const MMTemplateOwnerHost *host;
    MMTemplateState state;
    MMTemplateProvider provider;
    int text;
};

typedef struct MMTemplateText
{
    MMTextDescriptor descriptor;
    MMTextGenerated generated;
    MMTemplateFontProvider font;
} MMTemplateText;

static MMU32 owner_bytes(const MMTemplateOwnerHost *host, int text)
{
    return sizeof(MMTemplateOwner) + (text ? sizeof(MMTemplateText) : 0) +
           (MM_STRING_MAX + 2) * host->name_unit;
}

MMTemplateOwner *mm_template_owner_create(const MMTemplateOwnerHost *host, int text)
{
    const MMStorageAllocator *allocator = host->allocator;
    MMTemplateOwner *owner;
    MMU32 bytes;
    if (host->name_unit != 1 && host->name_unit != 2)
        return NULL;
    bytes = owner_bytes(host, text);
    owner = allocator ? allocator->resize(allocator->context, NULL, 0, bytes, MM_STORAGE_NORMAL) : malloc(bytes);
    if (owner)
    {
        memset(owner, 0, bytes);
        mm_reference_init(&owner->references);
        owner->host = host;
        owner->text = text != 0;
    }
    return owner;
}

MMTemplateState *mm_template_owner_state(MMTemplateOwner *owner) { return &owner->state; }
const MMResourceObject *mm_template_owner_binding(const MMTemplateOwner *owner) { return &owner->binding; }
MMI32 mm_template_owner_references(const MMTemplateOwner *owner) { return owner->references.count; }
int mm_template_owner_is_text(const MMTemplateOwner *owner) { return owner->text; }
MMTemplateProvider *mm_template_owner_provider(MMTemplateOwner *owner) { return &owner->provider; }

static MMTemplateText *text_data(MMTemplateOwner *owner)
{
    return (MMTemplateText *)(owner + 1);
}

MMTextDescriptor *mm_template_owner_descriptor(MMTemplateOwner *owner) { return &text_data(owner)->descriptor; }
MMTextGenerated *mm_template_owner_generated(MMTemplateOwner *owner) { return &text_data(owner)->generated; }
MMTemplateFontProvider *mm_template_owner_font(MMTemplateOwner *owner) { return &text_data(owner)->font; }

MMStringValue mm_template_owner_name(MMTemplateOwner *owner)
{
    MMStringValue name = {0};
    name.buffer = (unsigned char *)(owner + 1) + (owner->text ? sizeof(MMTemplateText) : 0);
    name.unit = owner->host->name_unit;
    return name;
}

static MMResourceObject *template_binding(void *object) { return &((MMTemplateOwner *)object)->binding; }
static MMReference *template_references(void *object) { return &((MMTemplateOwner *)object)->references; }

static void destroy_template(void *object)
{
    MMTemplateOwner *owner = object;
    const MMTemplateOwnerHost *host = owner->host;
    const MMStorageAllocator *allocator = host->allocator;
    if (owner->text)
    {
        MMTextGenerated *generated = mm_template_owner_generated(owner);
        mm_material_owner_release(generated->material);
        mm_action_resource_release(generated->action);
    }
    if (owner->provider.release)
        owner->provider.release(owner->provider.context);
    mm_template_state_dispose(&owner->state);
    mm_resource_object_dispose(owner, mm_template_owner_services());
    if (allocator)
        allocator->release(allocator->context, owner, owner_bytes(host, owner->text));
    else
        free(owner);
}

static void mark_template(void *object)
{
    MMTemplateOwner *owner = object;
    if (owner->host->mark)
        owner->host->mark(owner);
}

const MMResourceObjectServices *mm_template_owner_services(void)
{
    static const MMResourceObjectServices services = {
        template_binding, template_references, destroy_template, NULL, mark_template
    };
    return &services;
}

void mm_template_owner_retain(MMTemplateOwner *owner)
{
    mm_reference_add(&owner->references);
}

void mm_template_owner_release(MMTemplateOwner *owner)
{
    if (owner)
        mm_resource_object_release(owner, mm_template_owner_services());
}


int mm_text_author_name(const char *utf8, char name[MM_TEXT_NAME_BYTES])
{
    unsigned short units[MM_TEXT_NAME_BYTES - 1];
    char ansi[MM_TEXT_NAME_BYTES - 1];
    size_t count, bytes, i;
    int result, visible = 0, single_line = 1;
    if (!utf8 || !*utf8)
        return MM_TEXT_NAME_EMPTY;
    result = mm_text_read_utf8(utf8, strlen(utf8), units, MM_TEXT_NAME_BYTES - 1, &count);
    if (!result)
        return MM_TEXT_NAME_UTF8;
    if (result < 0)
        return MM_TEXT_NAME_LENGTH;
    for (i = 0; i < count; ++i)
    {
        if (units[i] == '\r' || units[i] == '\n' || units[i] == '\t')
            single_line = 0;
        else if (units[i] != ' ')
            visible = 1;
    }
    if (!visible)
        return MM_TEXT_NAME_LENGTH;
    bytes = mm_text_ansi_from_units(NULL, units, count, ansi, sizeof(ansi), 1);
    if (!single_line || !bytes)
        return MM_TEXT_NAME_CHARACTERS;
    memcpy(name, ansi, bytes);
    name[bytes] = '\0';
    return MM_TEXT_NAME_OK;
}

int mm_text_author_prepare_name(const char *utf8, MMTextAuthorName *out)
{
    MMTextAuthorName name;
    int result;
    size_t bytes;
    memset(&name, 0, sizeof(name));
    result = mm_text_author_name(utf8, name.legacy);
    if (result != MM_TEXT_NAME_OK)
        return result;
    bytes = strlen(utf8);
    if (bytes >= sizeof(name.utf8))
        return MM_TEXT_NAME_LENGTH;
    memcpy(name.utf8, utf8, bytes + 1);
    *out = name;
    return MM_TEXT_NAME_OK;
}

static void normalize_text(const MMStringValue *name)
{
    const unsigned char narrow = ' ';
    const unsigned short wide = ' ';
    if (mm_string_value_equal(name, NULL, 0))
        mm_string_value_set(name, name->unit == 1 ? (const void *)&narrow : (const void *)&wide, 1);
}

MMTemplateOwner *mm_text_create(const MMTemplateOwnerHost *host, const MMStringValue *text, MMI32 shape,
                                const MMTag *font, const MMTemplateFontProvider *provider,
                                const MMTextGenerationServices *services)
{
    MMTemplateOwner *owner;
    MMTemplateText *data;
    MMStringValue name;
    if (text->unit != host->name_unit)
        return NULL;
    owner = mm_template_owner_create(host, 1);
    if (!owner)
        return NULL;
    name = mm_template_owner_name(owner);
    mm_string_value_copy(&name, text->buffer);
    normalize_text(&name);
    data = text_data(owner);
    data->descriptor.shape = shape;
    memcpy(&data->descriptor.font, font, sizeof(*font));
    if (!mm_text_initialize_lists(owner, services))
    {
        mm_template_owner_release(owner);
        return NULL;
    }
    if (provider)
        data->font = *provider;
    return owner;
}

MMTemplateOwner *mm_text_duplicate(MMTemplateOwner *owner, const MMTextGenerationServices *services)
{
    const MMTemplateText *data;
    MMStringValue name;
    if (!mm_template_owner_is_text(owner))
        return NULL;
    data = text_data(owner);
    name = mm_template_owner_name(owner);
    return mm_text_create(owner->host, &name, data->descriptor.shape, &data->descriptor.font, &data->font,
                          services);
}

int mm_text_change(MMTemplateOwner *owner, const MMStringValue *text, MMI32 shape, const MMTag *font,
                   const MMTextGenerationServices *services)
{
    unsigned short saved_units[MM_STRING_MAX + 2];
    MMStringValue saved_name = {0};
    MMStringValue current;
    MMTextDescriptor saved, *descriptor;
    if (!mm_template_owner_is_text(owner) || (text && text->unit != owner->host->name_unit))
        return 0;
    current = mm_template_owner_name(owner);
    saved_name.buffer = saved_units;
    saved_name.unit = current.unit;
    mm_string_value_copy(&saved_name, current.buffer);
    descriptor = mm_template_owner_descriptor(owner);
    memcpy(&saved, descriptor, sizeof(saved));
    if (text)
        mm_string_value_copy(&current, text->buffer);
    if (shape != -1)
        descriptor->shape = shape;
    if (font)
        memmove(&descriptor->font, font, sizeof(*font));
    normalize_text(&current);
    if (mm_text_initialize_lists(owner, services))
        return 1;
    mm_string_value_copy(&current, saved_name.buffer);
    memcpy(descriptor, &saved, sizeof(saved));
    return 0;
}
static MMU32 word(const unsigned char *p,int n,int big)
{ MMU32 v=0; int i; for (i=0;i<n;++i) v=(v<<8)|p[big?i:n-1-i]; return v; }
static void put(unsigned char *p,int n,MMU32 v,int big)
{ int i; for (i=0;i<n;++i) { p[big?n-1-i:i]=(unsigned char)(v&255U); v>>=8; } }
static void encode_descriptor(unsigned char *b, int big, MMU32 system, const MMTextDescriptor *d)
{
    put(b,2,1U,big); put(b+2,2,system,big); put(b+4,4,(MMU32)d->shape,big);
    put(b+8,4,(MMU32)d->font.sid,big); put(b+12,4,0U,big);
    put(b+16,4,d->font.type,big); put(b+20,4,d->font.number,big);
}
int mm_text_decode_descriptor(const unsigned char b[24], int big, MMTextDescriptor *d)
{
    MMU32 v;
    if (word(b,2,big)!=1U) big=!big;
    v=word(b+4,4,big); memcpy(&d->shape,&v,4);
    v=word(b+8,4,big); memcpy(&d->font.sid,&v,4); d->font.cache=NULL;
    d->font.type=word(b+16,4,big); d->font.number=word(b+20,4,big);
    return word(b,2,big)==1U;
}
static int descriptor(const MMTextResourceSource *source, MMU32 type, MMU32 number,
                      MMTextDescriptor *d, const MMTextResourceServices *s)
{
    const MMChunkPayloadServices *payloads = s->payloads;
    const MMChunkAccess chunks = source->cache ? mm_resource_cache_chunks(source->cache) : source->chunks;
    MMOwnedBlock block;
    MMBlockIO io;
    MMU32 child;
    unsigned char bytes[MM_TEXT_DESCRIPTOR_BYTES];
    int found, ok;
    if (mm_chunk_link_number(&chunks, type, number, 0, MM_TDT, &child) != MM_CHUNK_OK ||
        mm_owned_block_init(&block, payloads->handles, payloads->files) != MM_DATA_OK)
        return 0;
    found = source->cache ? mm_resource_cache_read_block(source->cache, MM_TDT, child, &block) == MM_CHUNK_OK :
                           mm_chunk_payload_bind_block(payloads, source->archive, MM_TDT, child, &block) ==
                               MM_CHUNK_PAYLOAD_OK;
    ok = found && mm_owned_block_code(&block, 0, payloads->decode_context, payloads->decode, NULL) == MM_DATA_OK &&
         mm_owned_block_io(&block, &io) == MM_DATA_OK && mm_block_read(&io, 0, bytes, sizeof(bytes), 0) == MM_DATA_OK;
    if (ok && !mm_text_decode_descriptor(bytes, s->host_big_endian, d) && s->bad_order)
        s->bad_order(s->context);
    mm_owned_block_clear(&block);
    return ok;
}
int mm_text_read(MMTemplateOwner *owner, const MMTextResourceSource *source, MMU32 type, MMU32 number, MMOwnedBlock *block,
                 const MMTextResourceServices *s)
{
    MMTextDescriptor read,*d;
    if (!mm_template_owner_is_text(owner) ||
        !s->read_template(s->context,owner,type,number,block) || !descriptor(source,type,number,&read,s))
        return 0;
    d=mm_template_owner_descriptor(owner); memcpy(&d->font,&read.font,sizeof(d->font)); d->shape=read.shape;
    return mm_text_initialize_lists(owner,&s->generation);
}
static int adopt_descriptor(const MMChunkEdit *edit, MMU32 type, MMU32 parent, const MMChunkChild *child)
{
    if (mm_chunk_adopt(edit, type, parent, child, 1) == MM_CHUNK_OK)
        return 1;
    mm_chunk_delete(edit, child->type, child->number);
    return 0;
}

int mm_text_write(const MMTemplateState *state, const MMTextDescriptor *descriptor, const MMChunkNameValue *name,
                  MMU32 type, MMU32 *number, const MMArchiveWriter *writer)
{
    const MMChunkPayloadServices *payloads = writer->payloads;
    const MMChunkEdit *edit = &writer->edit;
    unsigned char bytes[MM_TEXT_DESCRIPTOR_BYTES];
    MMOwnedBlock block;
    MMBlockIO io;
    MMChunkChild child = {MM_TDT, 0, 0};
    int ok = 0;
    if (!mm_template_write_header(state, name, type, number, writer) ||
        mm_owned_block_init(&block, payloads->handles, payloads->files) != MM_DATA_OK)
        return 0;
    encode_descriptor(bytes, writer->big_endian, writer->system, descriptor);
    if (mm_chunk_add_block(edit, child.type, sizeof(bytes), &child.number, &block) == MM_CHUNK_OK &&
        adopt_descriptor(edit, type, *number, &child))
    {
        ok = mm_owned_block_io(&block, &io) == MM_DATA_OK &&
             mm_block_write(&io, 0, bytes, sizeof(bytes), 0) == MM_DATA_OK;
        if (!ok)
            mm_chunk_unlink(edit, type, *number, &child);
    }
    mm_owned_block_clear(&block);
    return ok;
}
int mm_text_write_draft(const MMChunkNameValue *name, const MMTextDescriptor *d, MMU32 *number,
                        const MMArchiveWriter *writer)
{
    MMTemplateState state = {0};
    unsigned char descriptor[MM_TEXT_DESCRIPTOR_BYTES];
    const MMChunkEdit *edit = &writer->edit;
    MMChunkChild child = {MM_TDT, 0, 0};
    MMU32 parent = 0;
    if (d->shape < 0 || d->shape >= MM_TEXT_SHAPE_COUNT || d->font.type != MM_FONT_TYPE)
        return 0;
    state.flags = MM_TEMPLATE_TEXT;
    encode_descriptor(descriptor, writer->big_endian, writer->system, d);
    if (!mm_template_write_header(&state, name, MM_TMPL, &parent, writer))
        return 0;
    if (mm_chunk_add_bytes(edit, writer->payloads->files, child.type, descriptor, sizeof(descriptor),
                           &child.number) != MM_CHUNK_OK || !adopt_descriptor(edit, MM_TMPL, parent, &child))
    {
        mm_chunk_delete(edit, MM_TMPL, parent);
        return 0;
    }
    *number = parent;
    return 1;
}
int mm_text_bind_preview_font(const MMChunkEndpoint *source, const MMChunkEndpoint *draft, MMU32 tmpl, MMU32 font)
{
    MMChunkAccess chunks;
    unsigned char bytes[MM_TEXT_DESCRIPTOR_BYTES];
    MMOwnedBlock block;
    MMBlockIO io;
    MMChunkEntry entry;
    MMU32 descriptor, position, copied;
    int big, status = MM_CHUNK_BAD_FILE;
    if (!draft || !draft->bind_payload)
        return status;
    chunks = mm_chunk_edit_access(&draft->edit);
    if (mm_chunk_link_number(&chunks, MM_TMPL, tmpl, 0, MM_TDT, &descriptor) != MM_CHUNK_OK ||
        mm_chunk_find(&chunks, MM_TDT, descriptor, &position) != MM_CHUNK_OK ||
        mm_chunk_at(&chunks, position, &entry) != MM_CHUNK_OK ||
        mm_owned_block_init(&block, draft->handles, draft->files) != MM_DATA_OK)
        return status;
    if (!draft->bind_payload(draft->context, position, &entry, &block) ||
        mm_owned_block_io(&block, &io) != MM_DATA_OK ||
        mm_block_read(&io, 0, bytes, sizeof(bytes), 0) != MM_DATA_OK)
        goto done;
    big = bytes[0] == 0;
    if (word(bytes, 2, big) != 1 || word(bytes + 16, 4, big) != MM_FONT_TYPE)
        goto done;
    status = mm_chunk_copy(source, draft, MM_FONT_TYPE, font, 0, &copied);
    if (status == MM_CHUNK_OK)
    {
        put(bytes + 8, 4, 0, big);
        put(bytes + 20, 4, copied, big);
        if (mm_block_write(&io, 0, bytes, sizeof(bytes), 0) != MM_DATA_OK)
            status = MM_CHUNK_IO;
    }
done:
    mm_owned_block_clear(&block);
    return status;
}
void *mm_text_tags(const MMTextResourceSource *source, MMU32 type, MMU32 number, void *error_out,
                   const MMTextResourceServices *s)
{
    void *list; MMTextDescriptor d;
    s->publish_error(s->context,error_out,0); list=s->tag_list(s->context);
    if (list && descriptor(source,type,number,&d,s) && s->append_tag(s->context,list,&d.font)) return list;
    s->publish_error(s->context,error_out,1); if (list) s->release_tag_list(s->context,list); return NULL;
}
struct MMTextNames
{
    MMCollectionStorage storage;
    MMStringTable strings;
    int id_big_endian;
};

void mm_text_names_clear(MMTextNames **names)
{
    if (*names)
    {
        mm_storage_dispose(&(*names)->storage);
        free(*names);
        *names = NULL;
    }
}

int mm_text_names_read(MMTextNames **names, const MMReadStream *stream, int encoding, MMStringConvert convert,
                        void *context)
{
    const unsigned short order = 1;
    MMCollectionHeader header;
    MMTextNames *next;
    MMStringTable *view;
    MMU32 row, units;
    const void *characters;
    if (mm_collection_header_read(stream, MM_COLLECTION_STRINGS, 0, &header) != MM_STORAGE_OK || header.width != 8)
        return 0;
    next = calloc(1, sizeof(*next));
    if (!next)
        return 0;
    mm_storage_init(&next->storage, NULL);
    if (mm_storage_read(&next->storage, stream, header.header_bytes, header.first_bytes,
                         header.second_bytes) != MM_STORAGE_OK)
        goto fail;
    next->id_big_endian = header.big_endian;
    view = &next->strings;
    view->strings = next->storage.buffers[0].data;
    view->entries = next->storage.buffers[1].data;
    view->string_capacity = next->storage.buffers[0].capacity;
    view->entry_capacity = next->storage.buffers[1].capacity;
    view->bytes = header.bytes;
    view->count = header.count;
    view->stride = header.width;
    view->free_count = header.free_count;
    view->unit_bytes = encoding == MM_TEXT_UTF16_LE || encoding == MM_TEXT_UTF16_BE ? 2 : 1;
    view->context = &next->storage;
    view->reserve = mm_strings_reserve_storage;
    if (header.big_endian != (*(const unsigned char *)&order == 0))
        mm_strings_swap_offsets(view);
    if (!mm_strings_translate(view, header.system, encoding, convert, context))
        goto fail;
    for (row = 0; row < view->count; ++row)
        if (!mm_strings_get(view, row, &characters, &units))
            goto fail;
    mm_text_names_clear(names);
    *names = next;
    return 1;
fail:
    mm_text_names_clear(&next);
    return 0;
}

int mm_text_action_name(const MMTextNames *names, MMI32 id, const MMStringValue *text,
                         void (*missing)(void *), void *context)
{
    if (names && text->unit == names->strings.unit_bytes)
    {
        const MMStringTable *view = &names->strings;
        MMU32 row;
        for (row = 0; row < view->count; ++row)
            if (word(view->entries + row * view->stride + 4, 4, names->id_big_endian) == (MMU32)id)
            {
                const void *characters;
                MMU32 units;
                if (!mm_strings_get(view, row, &characters, &units))
                    break;
                mm_string_value_set(text, characters, (MMI32)units);
                return 1;
            }
        if (missing)
            missing(context);
    }
    mm_string_value_clear(text);
    return 0;
}
