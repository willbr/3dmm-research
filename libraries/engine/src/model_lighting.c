/* Model prelighting control converted from engine modl.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "three_mm/model_resource.h"
#include <string.h>
int three_mm_model_prelight(const ThreeMMModelResource *m, ThreeMMI32 count, const ThreeMMModelLightingServices *s)
{
    ThreeMMModelLightingSpec spec;
    void *world = NULL, *camera, *lights[9], *colour = NULL, *depth = NULL, *material = NULL;
    ThreeMMI32 i, enabled = 0;
    int ok = 0, added = 0;
    if (count < 1 || count >= 10)
        return 0;
    memset(&spec, 0, sizeof(spec));
    spec.colour = 0x00ffffffUL;
    spec.width = 544;
    spec.height = 306;
    spec.fov = 10920;
    spec.hither = 65536;
    spec.yon = 6553600;
    spec.aspect = 116508;
    spec.camera_width = 544;
    spec.camera_height = 306;
    spec.material_lit = spec.material_gouraud = 1;
    spec.attenuation = 65536;
    spec.ambient = 6553;
    spec.diffuse = 39321;
    spec.power = 3276800;
    spec.index_base = 108;
    spec.index_range = 8;
    world = s->create(s->context, THREE_MM_MODEL_LIGHT_WORLD, &spec);
    if (!world)
        goto done;
    camera = s->create(s->context, THREE_MM_MODEL_LIGHT_CAMERA, &spec);
    if (!camera)
        goto done;
    s->attach(s->context, world, camera);
    for (i = 0; i < count; ++i)
    {
        lights[i] = s->create(s->context, THREE_MM_MODEL_LIGHT_ACTOR, &spec);
        if (!lights[i])
            goto done;
        s->attach(s->context, world, lights[i]);
        s->enable(s->context, lights[i]);
        ++enabled;
    }
    colour = s->create(s->context, THREE_MM_MODEL_LIGHT_COLOUR, &spec);
    depth = s->create(s->context, THREE_MM_MODEL_LIGHT_DEPTH, &spec);
    if (!colour || !depth)
        goto done;
    material = s->create(s->context, THREE_MM_MODEL_LIGHT_MATERIAL, &spec);
    if (!material)
        goto done;
    s->add_material(s->context, material);
    added = 1;
    s->begin(s->context, world, camera, colour, depth);
    s->light(s->context, m->handle, material, world);
    s->end(s->context);
    s->remove_material(s->context, material);
    added = 0;
    if (count != 1 && s->multiple_lights)
        s->multiple_lights(s->context);
    ok = 1;
done:
    if (added)
        s->remove_material(s->context, material);
    for (i = enabled; i > 0; --i)
        s->disable(s->context, lights[i - 1]);
    if (world)
        s->free(s->context, THREE_MM_MODEL_LIGHT_WORLD, world);
    if (material)
        s->free(s->context, THREE_MM_MODEL_LIGHT_MATERIAL, material);
    if (depth)
        s->free(s->context, THREE_MM_MODEL_LIGHT_DEPTH, depth);
    if (colour)
        s->free(s->context, THREE_MM_MODEL_LIGHT_COLOUR, colour);
    return ok;
}
