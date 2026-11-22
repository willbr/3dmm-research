#include "template_renderer.h"
#include "asset_resources.h"
#include "body_renderer.h"

MMTextNames *mm_brender_text_action_names(void)
{
    static MMTextNames names;
    return &names;
}

static MMTemplateState *template_state(void *context, void *object)
{
    (void)context;
    return mm_template_owner_state(object);
}

static void *new_template(void *context, int text)
{
    (void)context;
    return mm_template_owner_create(mm_kauai_template_host(), text);
}

static void release_template(void *context, void *object)
{
    (void)context;
    mm_template_owner_release(object);
}

static void release_cache(void *context, void *cache)
{
    (void)context;
    mm_resource_cache_release(cache);
}

static void *fetch_custom(void *context, void *cache, MMU32 type, MMU32 number)
{
    (void)context;
    return mm_brender_costume_fetch(cache, type, number);
}

static void release_material(void *context, void *custom)
{
    (void)context;
    mm_material_owner_release(custom);
}

static MMI32 custom_set(void *context, void *custom)
{
    (void)context;
    return mm_material_owner_custom(custom)->set;
}

static int append_costumes(void *context, MMTemplateCostumeTable *table, const MMI32 *ids, MMI32 count)
{
    (void)context;
    return mm_template_costumes_append(table, ids, count);
}

static int initialize_template(void *context, void *object, MMU32 type, MMU32 number, MMOwnedBlock *block)
{
    MMTemplateCreateServices services = {0};
    if (mm_template_owner_is_text(object))
        return mm_brender_text_initialize(object, context, type, number, block);
    mm_brender_template_read_services(&services, context);
    return mm_template_initialize(object, context, type, number, block, &services);
}

void mm_brender_template_read_services(MMTemplateCreateServices *s, MMResourceCache *cache)
{
    s->context = cache;
    s->state = template_state;
    s->create = new_template;
    s->release = release_template;
    s->initialize = initialize_template;
    s->release_cache = release_cache;
    s->fetch_custom = fetch_custom;
    s->release_custom = release_material;
    s->custom_set = custom_set;
    s->append_costumes = append_costumes;
    mm_kauai_template_read_services(s);
}

int mm_brender_template_read(MMResourceCache *cache, MMU32 type, MMU32 number, MMOwnedBlock *block,
                            void **out, MMI32 *bytes)
{
    MMTemplateCreateServices services = {0};
    mm_brender_template_read_services(&services, cache);
    return mm_template_fetch(cache, type, number, block, out, bytes, &services);
}

MMTemplateOwner *mm_brender_template_fetch(MMResourceCache *cache, MMU32 type, MMU32 number, int *error)
{
    MMResourceRequest request = {0, 0, 0, mm_brender_template_read, MM_RESOURCE_NORMAL_PRIORITY};
    request.type = type;
    request.number = number;
    return mm_resource_cache_fetch_owned(cache, &request, mm_template_owner_services(), error);
}

int mm_brender_action_read(MMResourceCache *cache, MMU32 type, MMU32 number, MMOwnedBlock *block,
                          void **out, MMI32 *bytes)
{
    return mm_action_resource_read(cache, type, number, block, out, bytes, mm_kauai_action_host());
}

static MMTextDescriptor *text_descriptor(void *context, void *object)
{
    (void)context;
    return mm_template_owner_descriptor(object);
}

static int read_text_header(void *context, void *object, MMU32 type, MMU32 number, MMOwnedBlock *block)
{
    MMTemplateCreateServices services = {0};
    mm_brender_template_read_services(&services, context);
    return mm_template_read_header(object, context, type, number, block, &services);
}

int mm_brender_text_initialize(MMTemplateOwner *owner, MMResourceCache *cache, MMU32 type, MMU32 number,
                               MMOwnedBlock *block)
{
    MMTextResourceSource source = {0};
    MMTextResourceServices services = {0};
    source.cache = cache;
    services.context = cache;
    services.state = text_descriptor;
    services.read_template = read_text_header;
    mm_kauai_text_read_services(&services);
    mm_brender_text_generation_services(&services.generation);
    return mm_text_read(owner, &source, type, number, block, &services);
}

static MMTextGenerated *generated_text(void *context, void *object)
{
    (void)context;
    return mm_template_owner_generated(object);
}

static MMI32 name_byte(const MMStringValue *name, MMI32 index)
{
    if (name->unit == 2)
        return (unsigned char)((const unsigned short *)name->buffer)[index];
    return ((const unsigned char *)name->buffer)[index];
}

static MMI32 text_length(void *context, void *object)
{
    const MMStringValue name = mm_template_owner_name(object);
    (void)context;
    return name_byte(&name, 0);
}

static MMI32 text_character(void *context, void *object, MMI32 index)
{
    const MMStringValue name = mm_template_owner_name(object);
    (void)context;
    return name_byte(&name, index + 1);
}

static MMI32 text_shape(void *context, void *object)
{
    return text_descriptor(context, object)->shape;
}

static void *new_text_material(void *context)
{
    (void)context;
    return mm_brender_material_new(-1, -1);
}

static void retain_material(void *context, void *material)
{
    (void)context;
    mm_material_owner_retain(material);
}

static void *fetch_font(void *context, void *object)
{
    const MMTemplateFontProvider *provider = mm_template_owner_font(object);
    const MMTag *tag = &text_descriptor(context, object)->font;
    if (provider->fetch)
        return provider->fetch(provider->context, tag);
    return mm_tag_fetch_owned(tag, mm_brender_font_read, mm_font_owner_services(), 0, mm_tag_local_cache_services());
}

static void release_font(void *context, void *font)
{
    (void)context;
    mm_font_owner_release(font);
}

static MMI32 font_width(void *context, void *font, MMI32 character)
{
    const MMFontResource *metrics = mm_font_owner_metrics(font);
    (void)context;
    mm_kauai_font_check_index(character, metrics->count);
    return mm_font_width(metrics, character);
}

static MMI32 font_height(void *context, void *font, MMI32 character)
{
    const MMFontResource *metrics = mm_font_owner_metrics(font);
    (void)context;
    mm_kauai_font_check_index(character, metrics->count);
    return mm_font_height(metrics, character);
}

static MMI32 font_maximum(void *context, void *font)
{
    (void)context;
    return mm_font_owner_metrics(font)->maximum_height;
}

static void *fetch_glyph(void *context, void *font, MMI32 character)
{
    (void)context;
    return mm_brender_font_glyph(font, character);
}

static int text_body_shape(void *context, void *body, const MMPartIndexView *parents, const MMPartIndexView *sets)
{
    (void)context;
    return mm_body_change_shape(body, parents, sets, &mm_kauai_body_renderer()->edit);
}

static void text_body_material(void *context, void *body, MMI32 set, void *material)
{
    (void)context;
    mm_body_set_generic(body, set, material, &mm_kauai_body_renderer()->edit.runtime);
}

void mm_brender_text_generation_services(MMTextGenerationServices *s)
{
    s->state = generated_text;
    s->length = text_length;
    s->character = text_character;
    s->shape = text_shape;
    s->template_state = template_state;
    s->action_host = mm_kauai_action_host();
    s->table_allocator = *s->action_host->tables;
    s->material_new = new_text_material;
    s->retain_material = retain_material;
    s->release_material = release_material;
    s->font = fetch_font;
    s->release_font = release_font;
    s->font_width = font_width;
    s->font_height = font_height;
    s->font_maximum = font_maximum;
    s->glyph = fetch_glyph;
    s->body_shape = text_body_shape;
    s->body_material = text_body_material;
}
