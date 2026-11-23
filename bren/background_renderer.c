#include "background_renderer.h"
#include "asset_resources.h"
#include "scene_renderer.h"
#include "brender.h"
#include <string.h>

typedef char BackgroundMatrixSize[sizeof(br_matrix34) == sizeof(MMActorMatrix) ? 1 : -1];

typedef struct BackgroundLoad
{
    const MMBRenderBackgroundHost *host;
    MMResourceCache *cache;
    MMOwnedBlock *block;
    void **output;
    MMI32 *bytes;
} BackgroundLoad;

typedef struct BackgroundRead
{
    MMBackgroundOwner *owner;
    MMResourceCache *cache;
} BackgroundRead;

typedef struct BackgroundWorld
{
    const MMBRenderBackgroundWorld *world;
    MMResourceCache *cache;
} BackgroundWorld;

void mm_brender_background_light_services(MMBackgroundServices *s)
{
    const unsigned short one = 1;
    s->host_big_endian = *(const unsigned char *)&one == 0;
    s->actor_bytes = sizeof(br_actor);
    s->light_bytes = sizeof(br_light);
    s->configure_light = mm_brender_background_light;
    s->enable_light = mm_brender_background_light_enable;
    s->disable_light = mm_brender_background_light_disable;
    s->remove_light = mm_brender_background_light_remove;
}

static void *new_background(void *context)
{
    const BackgroundLoad *load = context;
    return mm_background_owner_create(&load->host->owner);
}

static int read_name(void *context, MMU32 type, MMU32 number)
{
    const BackgroundRead *read = context;
    const MMStringValue name = mm_background_owner_name(read->owner);
    return mm_kauai_resource_name(read->cache, type, number, &name);
}

static int load_background(void *context, void *object, MMU32 type, MMU32 number)
{
    const BackgroundLoad *load = context;
    BackgroundRead read;
    MMBackgroundServices services = *load->host->owner.teardown;
    read.owner = object;
    read.cache = load->cache;
    services.context = &read;
    services.name = read_name;
    return mm_background_read_cached(mm_background_owner_resource(object), mm_background_owner_camera(object),
                                     type, number, load->cache, load->block, load->host->decode, &services);
}

static MMI32 light_count(void *context, void *object)
{
    (void)context;
    return mm_background_owner_resource(object)->light_count;
}

static void release_background(void *context, void *object)
{
    (void)context;
    mm_background_owner_release(object);
}

static void store_size(void *context, MMI32 bytes)
{
    const BackgroundLoad *load = context;
    *load->bytes = bytes;
}

static void store_background(void *context, void *object)
{
    const BackgroundLoad *load = context;
    mm_kauai_background_validate(object);
    *load->output = object;
}

static void invalidate_background(void *context)
{
    const BackgroundLoad *load = context;
    load->host->invalidate(load->output, load->bytes);
}

int mm_brender_background_read(MMResourceCache *cache, MMU32 type, MMU32 number, MMOwnedBlock *block,
                               void **out, MMI32 *bytes)
{
    const MMBRenderBackgroundHost *host = mm_kauai_background_host();
    const MMBackgroundServices *renderer = host->owner.teardown;
    MMBackgroundFactoryServices services = {0};
    BackgroundLoad load;
    load.host = host;
    load.cache = cache;
    load.block = block;
    load.output = out;
    load.bytes = bytes;
    services.context = &load;
    services.object_bytes = host->cache_bytes;
    services.actor_bytes = renderer->actor_bytes;
    services.light_bytes = renderer->light_bytes;
    services.create = new_background;
    services.initialize = load_background;
    services.lights = light_count;
    services.release = release_background;
    services.size = store_size;
    services.publish = store_background;
    services.failure = invalidate_background;
    return mm_background_factory(type, number, out != NULL, &services);
}

MMBackgroundOwner *mm_brender_background_fetch(MMResourceCache *cache, MMU32 type, MMU32 number)
{
    return mm_resource_cache_fetch_owned_chunk(cache, type, number, mm_brender_background_read,
                                               mm_background_owner_services());
}

static void add_light(void *context, MMBackgroundResource *resource, MMI32 index)
{
    const MMBRenderBackgroundWorld *world = context;
    world->add_light(world->context, (br_actor *)resource->actors + index);
}

void mm_brender_background_lights_on(MMBackgroundOwner *owner, const MMBRenderBackgroundWorld *world)
{
    MMBackgroundServices services = *mm_kauai_background_host()->owner.teardown;
    services.context = (void *)world;
    services.add_light = add_light;
    mm_background_lights_on(mm_background_owner_resource(owner), &services);
}

static int select_images(void *context, MMU32 rgb, MMU32 depth)
{
    const BackgroundWorld *host = context;
    return host->world->images(host->world->context, host->cache, rgb, depth);
}

static void camera_euler_yxy(void *context, const MMActorMatrix *pose, unsigned short *a, unsigned short *c)
{
    br_matrix34 matrix;
    br_euler angles;
    (void)context;
    memcpy(&matrix, pose, sizeof(matrix));
    angles.order = BR_EULER_YXY_R;
    BrMatrix34ToEuler(&angles, &matrix);
    *a = angles.a;
    *c = angles.c;
}

static void mouse_rotate_y(void *context, MMActorMatrix *out, unsigned short yaw)
{
    br_matrix34 matrix;
    (void)context;
    BrMatrix34RotateY(&matrix, yaw);
    memcpy(out, &matrix, sizeof(matrix));
}

static void mouse_translate(void *context, MMActorMatrix *out, const MMActorPoint *point)
{
    br_matrix34 matrix;
    (void)context;
    memcpy(&matrix, out, sizeof(matrix));
    BrMatrix34PostTranslate(&matrix, point->dxr, point->dyr, point->dzr);
    memcpy(out, &matrix, sizeof(matrix));
}

static void apply_camera(void *context, const MMBackgroundCameraData *camera)
{
    const BackgroundWorld *host = context;
    host->world->camera(host->world->context, camera);
}

static void mark_dirty(void *context)
{
    const BackgroundWorld *host = context;
    host->world->dirty(host->world->context);
}

int mm_brender_background_select(MMBackgroundOwner *owner, MMU32 slot, const MMBRenderBackgroundWorld *world)
{
    const MMResourceObject *binding = mm_background_owner_binding(owner);
    BackgroundWorld host;
    MMBackgroundCameraServices services = {0};
    host.world = world;
    host.cache = binding->cache;
    services.context = &host;
    services.background = select_images;
    services.euler_yxy = camera_euler_yxy;
    services.rotate_y = mouse_rotate_y;
    services.translate = mouse_translate;
    services.camera = apply_camera;
    services.dirty = mark_dirty;
    services.diagnostic = mm_kauai_background_camera_diagnostic;
    mm_brender_background_lights_on(owner, world);
    return mm_background_select_cached(mm_background_owner_camera(owner), binding->cache, binding->number, slot,
                                       mm_kauai_background_host()->decode, &services);
}
