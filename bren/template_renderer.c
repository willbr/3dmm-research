#include "template_renderer.h"
#include "asset_resources.h"
#include "body_renderer.h"
#include "mm/movie_session.h"
#include "mm/template_runtime.h"
#include <string.h>

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

int mm_brender_text_adjust_body(MMTemplateOwner *owner, MMBodyOwner *body)
{
    MMTextGenerationServices services = {0};
    mm_kauai_template_validate(owner);
    mm_brender_text_generation_services(&services);
    return mm_text_adjust_body(owner, body, &services);
}

MMMaterialOwner *mm_brender_template_costume(MMTemplateOwner *owner, MMI32 id)
{
    mm_kauai_template_validate(owner);
    /* Costume IDs are child slots and can have gaps. */
    if (id < 0 || mm_template_owner_is_text(owner))
        return NULL;
    return mm_template_fetch_child(mm_template_owner_binding(owner), (MMU32)id, MM_TEMPLATE_CUSTOM,
                                   mm_brender_costume_read, mm_material_owner_services(), NULL);
}

MMI32 mm_brender_template_costume_count(MMTemplateOwner *owner, MMI32 set)
{
    mm_kauai_template_validate(owner);
    return mm_template_costumes_count(mm_template_owner_state(owner)->costumes, set);
}

MMI32 mm_brender_template_costume_id(MMTemplateOwner *owner, MMI32 set, MMI32 index)
{
    MMI32 id = -1;
    mm_kauai_template_validate(owner);
    mm_template_costumes_id(mm_template_owner_state(owner)->costumes, set, index, &id);
    return id;
}

static void *create_body(void *context, const MMPartIndexView *parents, const MMPartIndexView *sets)
{
    (void)context;
    return mm_brender_body_create(mm_kauai_body_renderer(), parents, sets);
}

static void release_body(void *context, void *body)
{
    (void)context;
    mm_body_owner_release(body);
}

static void apply_custom(void *context, void *body, void *material)
{
    (void)context;
    mm_body_set_custom(body, material, &mm_kauai_body_renderer()->edit.runtime);
}

static MMI32 costume_id(void *context, void *object, MMI32 set, MMI32 index)
{
    (void)context;
    return mm_brender_template_costume_id(object, set, index);
}

static void *costume(void *context, void *object, MMI32 id)
{
    (void)context;
    return mm_brender_template_costume(object, id);
}

static int allocate_cel(void *context, size_t bytes, void **out)
{
    const MMBRenderBodyHost *host = &mm_kauai_body_renderer()->host;
    (void)context;
    return bytes <= host->max_bytes && host->allocate((MMI32)bytes, out);
}

static int allocate_costumes(void *context, MMI32 count, void **out)
{
    const MMBRenderBodyHost *host = &mm_kauai_body_renderer()->host;
    if (count < 0 || (MMU32)count > host->max_bytes / sizeof(MMMaterialOwner *))
        return 0;
    return allocate_cel(context, (size_t)count * sizeof(MMMaterialOwner *), out);
}

static void free_storage(void *context, void **out)
{
    (void)context;
    mm_kauai_body_renderer()->host.free_memory(out);
}

static int default_costume(void *context, void *object, void *body)
{
    (void)context;
    return mm_brender_template_default_costume(object, body);
}

static void body_services(MMTemplateCreateServices *s)
{
    s->state = template_state;
    s->create_body = create_body;
    s->release_body = release_body;
    s->default_costume = default_costume;
    s->costume_id = costume_id;
    s->custom = costume;
    s->custom_set = custom_set;
    s->release_custom = release_material;
    s->apply_custom = apply_custom;
    s->allocate_refs = allocate_costumes;
    s->free_memory = free_storage;
    mm_kauai_template_read_services(s);
}

MMBodyOwner *mm_brender_template_body(MMTemplateOwner *owner)
{
    MMTemplateCreateServices services = {0};
    mm_kauai_template_validate(owner);
    body_services(&services);
    return mm_template_create_body(owner, &services);
}

int mm_brender_template_default_costume(MMTemplateOwner *owner, MMBodyOwner *body)
{
    MMTemplateCreateServices services = {0};
    mm_kauai_template_validate(owner);
    if (mm_template_owner_is_text(owner))
    {
        MMTextGenerationServices generation = {0};
        mm_brender_text_generation_services(&generation);
        return mm_text_default_costume(owner, body, &generation);
    }
    body_services(&services);
    return mm_template_default_costume(owner, body, &services);
}

static void *generated_action(void *context, void *object, MMI32 id)
{
    const MMTemplateProvider *provider = mm_template_owner_provider(object);
    (void)context;
    if (mm_template_owner_is_text(object))
    {
        MMTextGenerationServices services = {0};
        mm_brender_text_generation_services(&services);
        return mm_text_fetch_action(object, id, &services);
    }
    return provider->action(provider->context, id);
}

static void *generated_model(void *context, void *object, MMU32 id, MMMovieAsset *asset)
{
    const MMTemplateProvider *provider = mm_template_owner_provider(object);
    MMModelOwner *model;
    (void)context;
    if (mm_template_owner_is_text(object))
    {
        MMTextGenerationServices services = {0};
        mm_brender_text_generation_services(&services);
        model = mm_text_fetch_model(object, id, &services);
    }
    else
        model = provider->model(provider->context, id);
    if (model)
    {
        asset->source = 0;
        asset->type = MM_TEMPLATE_BMDL;
        asset->number = mm_model_owner_binding(model)->number;
    }
    return model;
}

static void set_cel_model(void *context, void *body, MMI32 part, void *model)
{
    (void)context;
    mm_body_set_model(body, part, model, &mm_kauai_body_renderer()->edit.runtime);
}

static void set_cel_matrix(void *context, void *body, MMI32 part, const MMActorMatrix *matrix)
{
    (void)context;
    mm_body_set_matrix(body, part, matrix, &mm_kauai_body_renderer()->edit.runtime);
}

static int action_sound(void *context, void *action, MMI32 cel, void *out)
{
    MMTag *tag = out;
    MMTag value;
    (void)context;
    tag->type = 0;
    if (mm_action_cel_sound(mm_action_resource_definition(action), cel, &value))
    {
        tag->sid = value.sid;
        tag->cache = value.cache;
        tag->type = value.type;
        tag->number = value.number;
    }
    return tag->type != 0;
}

static void sound_exists(void *context, void *out, int present)
{
    (void)context;
    *(int *)out = present;
}

static int action_name(void *context, void *cache, MMU32 type, MMU32 number, void *out)
{
    (void)context;
    return mm_kauai_resource_name(cache, type, number, out);
}

static void runtime_services(MMTemplateRuntimeServices *s, MMTemplateOwner *owner)
{
    const MMTemplateProvider *provider = mm_template_owner_provider(owner);
    const int text = mm_template_owner_is_text(owner);
    mm_kauai_template_validate(owner);
    s->storage.state = template_state;
    s->storage.free_memory = free_storage;
    s->resource = mm_template_owner_binding(owner);
    s->model_reader = mm_brender_model_read;
    s->model_objects = mm_model_owner_services();
    s->name = action_name;
    mm_template_use_action_resource(s, mm_brender_action_read);
    if (text || provider->action)
        s->generated_action = generated_action;
    if (text || provider->model)
        s->generated_model = generated_model;
    s->allocate_cel = allocate_cel;
    s->set_model = set_cel_model;
    s->set_matrix = set_cel_matrix;
    s->sound = action_sound;
    s->sound_exists = sound_exists;
}

void mm_brender_template_rest(MMTemplateOwner *owner, unsigned short *x, unsigned short *y, unsigned short *z)
{
    const MMTemplateState *state = mm_template_owner_state(owner);
    mm_kauai_template_validate(owner);
    *x = state->x;
    *y = state->y;
    *z = state->z;
}

int mm_brender_template_action_name(MMTemplateOwner *owner, MMI32 id, const MMStringValue *name)
{
    MMTemplateRuntimeServices services = {0};
    runtime_services(&services, owner);
    if (mm_template_owner_is_text(owner))
    {
        MMTextResourceServices text = {0};
        mm_kauai_text_name_services(&text);
        return mm_text_action_name(mm_brender_text_action_names(), id, (void *)name, &text);
    }
    return mm_template_action_name(id, (void *)name, &services);
}

int mm_brender_template_set_cel(MMTemplateOwner *owner, MMBodyOwner *body, MMI32 id, MMI32 cel, void *distance)
{
    MMTemplateRuntimeServices services = {0};
    runtime_services(&services, owner);
    return mm_template_set_cel(owner, body, id, cel, distance, &services);
}

int mm_brender_template_distance(MMTemplateOwner *owner, MMI32 id, MMI32 cel, void *distance)
{
    MMTemplateRuntimeServices services = {0};
    MMI32 value;
    runtime_services(&services, owner);
    if (!mm_template_distance(owner, id, cel, &value, &services))
        return 0;
    memcpy(distance, &value, sizeof(value));
    return 1;
}

int mm_brender_template_cel_count(MMTemplateOwner *owner, MMI32 id, void *count)
{
    MMTemplateRuntimeServices services = {0};
    runtime_services(&services, owner);
    return mm_template_cel_count(owner, id, count, &services);
}

int mm_brender_template_action_flags(MMTemplateOwner *owner, MMI32 id, void *flags)
{
    MMTemplateRuntimeServices services = {0};
    runtime_services(&services, owner);
    return mm_template_action_flags(owner, id, flags, &services);
}

int mm_brender_template_sound(MMTemplateOwner *owner, MMI32 id, MMI32 cel, int *exists, MMTag *tag)
{
    MMTemplateRuntimeServices services = {0};
    runtime_services(&services, owner);
    return mm_template_sound(owner, id, cel, exists, tag, &services);
}

int mm_brender_template_is_accessory(MMTemplateOwner *owner, MMI32 set)
{
    MMTemplateRuntimeServices services = {0};
    runtime_services(&services, owner);
    return mm_template_is_accessory(owner, set, &services);
}

int mm_brender_template_accessory_set(MMTemplateOwner *owner, MMI32 set, void *out)
{
    MMTemplateRuntimeServices services = {0};
    runtime_services(&services, owner);
    return mm_template_accessory_set(owner, set, out, &services);
}

int mm_brender_template_same_accessory(MMTemplateOwner *owner, MMI32 first, MMI32 second)
{
    MMTemplateRuntimeServices services = {0};
    runtime_services(&services, owner);
    return mm_template_same_accessory(first, second, &services);
}
