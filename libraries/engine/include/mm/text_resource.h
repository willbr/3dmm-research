/* Three-dimensional text resources converted from engine tdt.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef MM_ENGINE_TEXT_RESOURCE_H
#define MM_ENGINE_TEXT_RESOURCE_H
#include "chunk_storage.h"
#include "chunky_file.h"
#include "resource_cache.h"
#include "text_generation.h"
#include "template_create.h"
#include "tag.h"
#include "string_value.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct MMTextDescriptor { MMI32 shape; MMTag font; } MMTextDescriptor;

typedef struct MMTemplateOwner MMTemplateOwner;
struct MMFontOwner;
typedef struct MMTemplateFontProvider
{
    void *context;
    struct MMFontOwner *(*fetch)(void *, const MMTag *);
} MMTemplateFontProvider;
/* In-memory templates may supply owned actions or models without archive links. */
typedef struct MMTemplateProvider
{
    void *context;
    MMActionResource *(*action)(void *, MMI32);
    void *(*model)(void *, MMU32);
    void (*release)(void *);
} MMTemplateProvider;
typedef struct MMTemplateOwnerHost
{
    const MMStorageAllocator *allocator;
    unsigned name_unit;
    void (*mark)(MMTemplateOwner *);
} MMTemplateOwnerHost;
/* The host outlives the owner. The name view borrows its counted storage.
 * Only owners created with a text extension expose descriptor/font state. */
MMTemplateOwner *mm_template_owner_create(const MMTemplateOwnerHost *, int text);
MMTemplateState *mm_template_owner_state(MMTemplateOwner *);
MMStringValue mm_template_owner_name(MMTemplateOwner *);
const MMResourceObject *mm_template_owner_binding(const MMTemplateOwner *);
const MMResourceObjectServices *mm_template_owner_services(void);
MMI32 mm_template_owner_references(const MMTemplateOwner *);
void mm_template_owner_retain(MMTemplateOwner *);
void mm_template_owner_release(MMTemplateOwner *);
int mm_template_owner_is_text(const MMTemplateOwner *);
MMTextDescriptor *mm_template_owner_descriptor(MMTemplateOwner *);
MMTextGenerated *mm_template_owner_generated(MMTemplateOwner *);
MMTemplateFontProvider *mm_template_owner_font(MMTemplateOwner *);
MMTemplateProvider *mm_template_owner_provider(MMTemplateOwner *);
enum { MM_TEXT_DESCRIPTOR_BYTES = 24 };
enum { MM_TEXT_NAME_BYTES = 51, MM_TEXT_NAME_UTF8_BYTES = 3 * (MM_TEXT_NAME_BYTES - 1) + 1 };
enum { MM_TEXT_NAME_OK, MM_TEXT_NAME_EMPTY, MM_TEXT_NAME_LENGTH, MM_TEXT_NAME_UTF8, MM_TEXT_NAME_CHARACTERS };
/* Validate an authored 3D text name and publish its terminated legacy text.
 * Uses the fixed Windows character map used by the original glyph set. */
int mm_text_author_name(const char *utf8, char name[MM_TEXT_NAME_BYTES]);
typedef struct MMTextAuthorName
{
    char utf8[MM_TEXT_NAME_UTF8_BYTES];
    char legacy[MM_TEXT_NAME_BYTES];
} MMTextAuthorName;
/* Publish both spellings after validation. Failure leaves the output unchanged. */
int mm_text_author_prepare_name(const char *utf8, MMTextAuthorName *);
/* Decodes the descriptor even when its marker is invalid. The result reports
 * whether the marker was valid. A bad marker uses the opposite host order. */
int mm_text_decode_descriptor(const unsigned char bytes[24], int host_big, MMTextDescriptor *);
typedef struct MMTextNames MMTextNames;
typedef struct MMTextResourceSource
{
    MMResourceCache *cache;
    void *archive;
    MMChunkAccess chunks;
} MMTextResourceSource;
typedef struct MMTextResourceServices
{
    void *context;
    int host_big_endian;
    MMTextGenerationServices generation;
    int (*read_template)(void *,MMTemplateOwner *,MMU32 type,MMU32 number,MMOwnedBlock *);
    const MMChunkPayloadServices *payloads;
    void (*publish_error)(void *,void *output,int value);
    void *(*tag_list)(void *);
    void (*release_tag_list)(void *,void *);
    int (*append_tag)(void *,void *list,const MMTag *);
    void (*bad_order)(void *);
} MMTextResourceServices;
/* Copy counted text in the owner's character width and preserve runtime tag
 * cache bits. Empty authored text becomes a space. Change borrows optional
 * text and font and a shape of -1 keeps the current shape. Even a no-op change
 * rebuilds generated owners. Failure preserves the old text, descriptor and
 * generated owners. Font providers bind after successful fresh construction.
 * Generation callbacks must not reenter owners. */
MMTemplateOwner *mm_text_create(const MMTemplateOwnerHost *, const MMStringValue *, MMI32 shape, const MMTag *,
                                const MMTemplateFontProvider *, const MMTextGenerationServices *);
MMTemplateOwner *mm_text_duplicate(MMTemplateOwner *, const MMTextGenerationServices *);
int mm_text_change(MMTemplateOwner *, const MMStringValue *, MMI32 shape, const MMTag *,
                   const MMTextGenerationServices *);
/* Read publishes base-template fields first, then font and shape, retaining
 * partial state on direct failure. Tag enumeration clears error before allocation
 * and publishes failure before releasing the list. The 24-byte record allows
 * trailing bytes and swaps whenever its marker differs from the native marker.
 * Write keeps the parent/output number on child failure, deletes only the child
 * on payload-write failure, and always writes a zero pointer pad. */
/* Supply a live cache or a standalone archive and its chunk access. */
int mm_text_read(MMTemplateOwner *,const MMTextResourceSource *,MMU32 type,MMU32 number,MMOwnedBlock *,
                 const MMTextResourceServices *);
/* The live owner and draft name use the writer's native character encoding. */
int mm_text_write_owner(MMTemplateOwner *, MMU32 type, MMU32 *number, const MMArchiveWriter *);
/* Write the two persisted chunks for new 3D text without generating renderer
 * state. Only a successful draft publishes its template number. */
int mm_text_write_draft(const MMStringValue *, const MMTextDescriptor *, MMU32 *number,
                        const MMArchiveWriter *);
struct MMChunkEndpoint;
/* Localize the font tag in an editable text preview. The descriptor must be
 * unpacked and valid before the copy. Retains its byte order and fields outside
 * the local source and new font number. The caller saves or discards the draft.
 * MM_CHUNK_NOT_FOUND identifies a missing source font. */
int mm_text_bind_preview_font(const struct MMChunkEndpoint *source, const struct MMChunkEndpoint *draft,
                              MMU32 tmpl, MMU32 font);
void *mm_text_tags(const MMTextResourceSource *,MMU32 type,MMU32 number,void *error_out,
                   const MMTextResourceServices *);
/* Use a zero-initialized owner. Read a dense GST with one signed 32-bit
 * action ID per row. The copied table uses the requested host text encoding.
 * A failed read leaves the previous catalogue available. */
int mm_text_names_read(MMTextNames **, const MMReadStream *, int encoding, MMStringConvert, void *context);
void mm_text_names_clear(MMTextNames **);
/* Use an output with the same native character width as the catalogue.
 * Lookup takes the first matching ID. A missing catalogue clears silently.
 * A missing entry invokes the optional diagnostic before clearing. */
int mm_text_action_name(const MMTextNames *, MMI32 id, const MMStringValue *, void (*missing)(void *), void *);
#ifdef __cplusplus
}
#endif
#endif
