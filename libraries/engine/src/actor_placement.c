/* Actor placement converted from engine actor.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "three_mm/actor_placement.h"
#include "three_mm/actor_math.h"

void three_mm_actor_placement(ThreeMMActorPoint *point, int text,
                             const ThreeMMActorPlacementServices *services)
{
    ThreeMMActorMatrix camera;
    ThreeMMI32 hither, yon, z;
    if (point->dxr == 0 && point->dyr == 0 && point->dzr == 0)
    {
        services->camera(services->context, &camera, &hither, &yon);
        z = -1638400L;
        if (1638400L > yon)
            z = three_mm_fixed_add(yon, hither) / -2;
        point->dxr = three_mm_fixed_add(three_mm_fixed_mac3(0, camera.m[0][0],
            0, camera.m[1][0], z, camera.m[2][0]), camera.m[3][0]);
        point->dyr = 0;
        point->dzr = three_mm_fixed_add(three_mm_fixed_mac3(0, camera.m[0][2],
            0, camera.m[1][2], z, camera.m[2][2]), camera.m[3][2]);
    }
    if (text)
        point->dyr = three_mm_fixed_add(point->dyr, 655360L);
}
