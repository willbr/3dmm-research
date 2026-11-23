#include "preview_actor_renderer.h"
#include "asset_resources.h"
#include "body_renderer.h"
#include "brender.h"
#include "template_renderer.h"
#include "mm/actor_persistence.h"
#include "mm/movie_session.h"
#include "mm/preview_live.h"
#include <string.h>

#define TEMPLATE_TYPE 0x544d504cUL

typedef char PreviewScalarSize[BASED_FIXED && sizeof(br_scalar) == sizeof(MMI32) ? 1 : -1];

static int fault(void *p)
{
    const MMBRenderPreviewActor *binding = p;
    return binding->fault ? binding->fault(binding->context) : 0;
}

static int actor_header(void *p, MMU32 number, MMActorHeader *header)
{
    const MMBRenderPreviewActor *binding = p;
    return mm_movie_actor_header(binding->graph, number, header) == MM_MOVIE_OK;
}

static int actor_route(void *p, MMU32 number, void **out)
{
    const MMBRenderPreviewActor *binding = p;
    MMActorPathFactory factory;
    MMActorPath *route = NULL;
    int status;
    factory.allocator = binding->track_allocator;
    factory.trash_unused = binding->trash_unused;
    status = mm_actor_movie_path(binding->graph, number, &factory, &route);
    *out = route;
    return status == MM_MOVIE_OK;
}

static int actor_events(void *p, MMU32 number, void **out)
{
    const MMBRenderPreviewActor *binding = p;
    MMActorEventFactory factory;
    MMActorEventTable *events = NULL;
    int status;
    factory.allocator = binding->track_allocator;
    factory.trash_unused = binding->trash_unused;
    status = mm_actor_movie_events(binding->graph, number, &factory, &events);
    *out = events;
    return status == MM_MOVIE_OK;
}

static int fetch_template(void *p, const MMActorHeader *header, void **out)
{
    const MMBRenderPreviewActor *binding = p;
    MMResourceCache *cache = binding->resolve(binding->context, header->source_id,
                                               header->template_type, header->template_number);
    if (!cache)
        return fault(p);
    *out = mm_brender_template_fetch(cache, header->template_type, header->template_number, NULL);
    return *out != NULL;
}

static int bind_template(void *p, void *tmpl)
{
    const MMBRenderPreviewActor *binding = p;
    return binding->bind ? binding->bind(binding->context, tmpl) : 1;
}

static int body_create(void *p, void *tmpl, void **out)
{
    (void)p;
    *out = mm_brender_template_body(tmpl);
    return *out != NULL;
}

static int duplicate_body(void *p, void *body, void **out)
{
    (void)p;
    *out = mm_body_duplicate(body, &mm_kauai_body_renderer()->edit);
    return *out != NULL;
}

static int adjust_text_body(void *p, void *tmpl, void *body)
{
    (void)p;
    return mm_brender_text_adjust_body(tmpl, body);
}

static int is_prop(void *p, void *tmpl)
{
    (void)p;
    return (mm_template_owner_state(tmpl)->flags & MM_TEMPLATE_PROP) != 0;
}

static int is_text(void *p, void *tmpl)
{
    (void)p;
    return (mm_template_owner_state(tmpl)->flags & MM_TEMPLATE_TEXT) != 0;
}

static void release_body(void *p, void *body)
{
    (void)p;
    mm_body_owner_release(body);
}

static void release_template(void *p, void *tmpl)
{
    (void)p;
    mm_template_owner_release(tmpl);
}

static void rest(void *p, void *tmpl, unsigned short *angles)
{
    (void)p;
    mm_brender_template_rest(tmpl, angles, angles + 1, angles + 2);
}

static MMI32 scalar(float value) { return BrFloatToScalar(value); }
static float scalar_float(MMI32 value) { return BrScalarToFloat(value); }

static void locate(void *p, void *body, const MMActorBodyPose *pose)
{
    (void)p;
    mm_body_locate(body, pose, &mm_kauai_body_renderer()->edit.runtime);
}

static int pose(void *p, void *tmpl, void *body, MMI32 action, MMI32 cel)
{
    (void)p;
    return mm_brender_template_set_cel(tmpl, body, action, cel, NULL);
}

static MMMaterialOwner *fetch_event_material(void *p, const MMTag *tag)
{
    const MMBRenderPreviewActor *binding = p;
    MMResourceCache *cache = binding->resolve(binding->context, tag->sid, tag->type, tag->number);
    return cache ? mm_brender_material_fetch(cache, tag->type, tag->number) : NULL;
}

static int fetch_material(void *p, MMI32 source, MMU32 number, void **out)
{
    const MMBRenderPreviewActor *binding = p;
    MMResourceCache *cache = binding->resolve(binding->context, source, MM_MATERIAL_CHUNK, number);
    if (!cache)
        return fault(p);
    *out = mm_brender_material_fetch(cache, MM_MATERIAL_CHUNK, number);
    return *out != NULL;
}

static void release_material(void *p, void *material)
{
    (void)p;
    mm_material_owner_release(material);
}

static int apply_material(void *p, void *body, void *material)
{
    (void)p;
    mm_body_set_generic(body, 0, material, &mm_kauai_body_renderer()->edit.runtime);
    return 1;
}

static int default_costume(void *p, void *tmpl, void *body)
{
    (void)p;
    return mm_brender_template_default_costume(tmpl, body);
}

static int template_distance(void *p, MMI32 action, MMI32 cel, MMI32 *out)
{
    const MMBRenderPreviewActor *binding = p;
    return mm_brender_template_distance(binding->actor->tmpl, action, cel, out) > 0;
}

static void timeline_diagnostic(void *p, int issue)
{
    const MMBRenderPreviewActor *binding = p;
    if (binding->diagnostic)
        binding->diagnostic(binding->context, issue);
}

static MMActorTimelineServices timeline(MMBRenderPreviewActor *binding)
{
    MMActorTimelineServices services = mm_actor_track_timeline_services();
    services.context = binding;
    services.resolve = template_distance;
    services.diagnostic = timeline_diagnostic;
    return services;
}

void mm_brender_preview_actor_services(MMBRenderPreviewActor *binding, MMPreviewActorServices *services)
{
    memset(services, 0, sizeof(*services));
    services->context = binding;
    services->timeline = timeline(binding);
    services->fault = fault;
    services->header = actor_header;
    services->route = actor_route;
    services->events = actor_events;
    services->fetch = fetch_template;
    services->bind = bind_template;
    services->body_create = body_create;
    services->prop = is_prop;
    services->text = is_text;
    services->release_body = release_body;
    services->release_template = release_template;
    services->tags = mm_tag_local_cache_services();
    services->track_allocator = binding->track_allocator;
    services->trash_unused = binding->trash_unused;
    services->rest = rest;
    services->scalar = scalar;
    services->scalar_float = scalar_float;
    services->locate = locate;
    services->pose = pose;
    mm_brender_actor_effect_renderer(&services->renderer);
    services->fetch_event_material = fetch_event_material;
    services->default_costume = default_costume;
    services->duplicate_body = duplicate_body;
    services->adjust_text_body = adjust_text_body;
    services->fetch_material = fetch_material;
    services->release_material = release_material;
    services->apply_material = apply_material;
}

int mm_brender_preview_actor_load(MMPreviewActor *actor, MMU32 number, MMWide *draw_clock, MMBRenderPreviewActor *binding)
{
    MMPreviewActorServices services;
    MMChunkAccess chunks;
    actor->draw_clock = draw_clock;
    mm_brender_preview_actor_services(binding, &services);
    binding->graph->access(binding->graph->context, &chunks);
    return mm_preview_actor_load(actor, number, &chunks, 0, &services);
}

int mm_brender_preview_prop_create(MMPreviewActor *actor, const MMPreviewLiveProp *prop, int order, int system,
                                  MMWide *draw_clock, MMBRenderPreviewActor *binding)
{
    MMPreviewActorServices services;
    MMPreviewActorDefinition definition = {0};
    actor->draw_clock = draw_clock;
    definition.number = prop->number;
    definition.header.order = order;
    definition.header.system = system;
    definition.header.actor_id = prop->actor_id;
    definition.header.first_frame = prop->first;
    definition.header.last_frame = prop->last;
    definition.header.source_id = prop->source;
    definition.header.template_type = TEMPLATE_TYPE;
    definition.header.template_number = prop->template_number;
    memcpy(definition.point, prop->point, sizeof(definition.point));
    mm_brender_preview_actor_services(binding, &services);
    return mm_preview_actor_create(actor, &definition, &services);
}

void mm_brender_preview_timeline_services(MMBRenderPreviewActor *binding, MMActorEffectRenderer *renderer,
                                          MMPreviewTimelineServices *services)
{
    services->timeline = timeline(binding);
    services->timeline.put_event = NULL;
    services->tags = mm_tag_local_cache_services();
    mm_brender_actor_effect_renderer(renderer);
    services->renderer = renderer;
}

static MMI32 edit_distance(const MMActorPoint *point)
{
    return BR_LENGTH3(point->dxr, point->dyr, point->dzr);
}

void mm_brender_preview_actor_edit_services(MMPreviewActorEditServices *services)
{
    services->template_sound = mm_brender_template_sound;
    services->distance = edit_distance;
}

MMPreviewCostumeCatalog mm_brender_preview_actor_costumes(void)
{
    MMPreviewCostumeCatalog catalog;
    catalog.body = &mm_kauai_body_renderer()->edit.runtime;
    catalog.count = mm_brender_template_costume_count;
    catalog.id = mm_brender_template_costume_id;
    catalog.fetch = mm_brender_template_costume;
    catalog.accessory = mm_brender_template_is_accessory;
    return catalog;
}

static int resolve_event_track(void *p, MMI32 source, MMU32 type, MMU32 number, MMResourceCache **out)
{
    const MMBRenderPreviewActor *binding = p;
    MMResourceCache *cache = binding->resolve(binding->context, source, type, number);
    if (!cache)
        return fault(p);
    *out = cache;
    return 1;
}

int mm_brender_preview_actor_replace_events(MMPreviewActor *actor, MMI32 source, MMBlockTransform decode,
                                            const MMPreviewActorServices *playback)
{
    MMPreviewEventReplacement edit = {NULL, resolve_event_track, NULL, NULL};
    edit.context = playback->context;
    edit.decode = decode;
    return mm_preview_actor_replace_track(actor, source, &edit, playback);
}
