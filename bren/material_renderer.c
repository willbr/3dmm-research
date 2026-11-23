#include "material_renderer.h"
#include "brender.h"
#include <string.h>

typedef char TextureTransformSize[sizeof(br_matrix23) == 6 * sizeof(MMI32) ? 1 : -1];

static void *allocate_material(void *context)
{
    char identifier[sizeof(void *) + 1];
    (void)context;
    memset(identifier, ' ', sizeof(void *));
    identifier[sizeof(void *)] = 0;
    return BrMaterialAllocate(identifier);
}

static void identify(void *context, void *handle, void *owner)
{
    br_material *material = (br_material *)handle;
    (void)context;
    memcpy(material->identifier, &owner, sizeof(owner));
}

br_material *mm_brender_material_handle(MMMaterialOwner *owner)
{
    return (br_material *)mm_material_owner_resource(owner)->handle;
}

MMMaterialOwner *mm_brender_material_owner(const void *handle)
{
    const br_material *material = (const br_material *)handle;
    MMMaterialOwner *owner;
    memcpy(&owner, material->identifier, sizeof(owner));
    return owner;
}

static void describe(void *context, void *handle, MMMaterialDescription *out)
{
    const br_material *material = (const br_material *)handle;
    (void)context;
    out->colour = material->colour;
    out->ambient = material->ka;
    out->diffuse = material->kd;
    out->specular = material->ks;
    out->power = material->power;
    out->index_base = material->index_base;
    out->index_range = material->index_range;
    out->opacity = material->opacity;
    out->flags = material->flags;
}

static void configure(void *context, void *handle, const MMMaterialDescription *in)
{
    br_material *material = (br_material *)handle;
    (void)context;
    material->colour = in->colour;
    material->ka = (br_ufraction)in->ambient;
    material->kd = (br_ufraction)in->diffuse;
    material->ks = (br_ufraction)in->specular;
    material->power = in->power;
    material->index_base = (br_uint_8)in->index_base;
    material->index_range = (br_uint_8)in->index_range;
    material->opacity = (br_uint_8)in->opacity;
    material->flags = in->flags;
}

static void add_material(void *context, void *handle) { (void)context; BrMaterialAdd((br_material *)handle); }
static void remove_material(void *context, void *handle) { (void)context; BrMaterialRemove((br_material *)handle); }
static void free_material(void *context, void *handle) { (void)context; BrMaterialFree((br_material *)handle); }

static void *texture(void *context, void *handle)
{
    const br_material *material = (const br_material *)handle;
    (void)context;
    return material->colour_map ? material->colour_map->identifier : NULL;
}

static void map(void *context, void *handle, void *colour, void *shade)
{
    br_material *material = (br_material *)handle;
    (void)context;
    material->colour_map = (br_pixelmap *)colour;
    if (shade)
        material->index_shade = (br_pixelmap *)shade;
}

static MMI32 height(void *context, void *pixelmap)
{
    (void)context;
    return ((const br_pixelmap *)pixelmap)->height;
}

static void transform(void *context, void *handle, const MMI32 matrix[6])
{
    br_material *material = (br_material *)handle;
    (void)context;
    memcpy(&material->map_transform, matrix, sizeof(material->map_transform));
}

static int read_mapping(void *context, void *handle, MMI32 matrix[6])
{
    const br_material *material = (const br_material *)handle;
    (void)context;
    memcpy(matrix, &material->map_transform, sizeof(material->map_transform));
    return material->colour_map != NULL;
}

static void *read_pixelmap(void *context, const void *path)
{
    (void)context;
    return BrPixelmapLoad((char *)path);
}

static void free_pixelmap(void *context, void *pixelmap)
{
    (void)context;
    BrPixelmapFree((br_pixelmap *)pixelmap);
}

void mm_brender_material_services(MMMaterialServices *s)
{
    s->light_flag = BR_MATF_LIGHT;
    s->gouraud_flag = BR_MATF_GOURAUD;
    s->smooth_flag = BR_MATF_SMOOTH;
    s->map_flag = BR_MATF_MAP_COLOUR;
    s->allocate_material = allocate_material;
    s->identify = identify;
    s->describe = describe;
    s->configure = configure;
    s->add_material = add_material;
    s->remove_material = remove_material;
    s->free_material = free_material;
    s->texture = texture;
    s->map = map;
    s->height = height;
    s->transform = transform;
    s->read_mapping = read_mapping;
    s->read_pixelmap = read_pixelmap;
    s->free_pixelmap = free_pixelmap;
}
