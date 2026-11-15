/* Actor placement converted from engine actor.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "mm/actor_placement.h"
#include "mm/actor_math.h"

void mm_actor_placement(MMActorPoint *point, int text,
                             const MMActorPlacementServices *services)
{
    MMActorMatrix camera;
    MMI32 hither, yon, z;
    if (point->dxr == 0 && point->dyr == 0 && point->dzr == 0)
    {
        services->camera(services->context, &camera, &hither, &yon);
        z = -1638400L;
        if (1638400L > yon)
            z = mm_fixed_add(yon, hither) / -2;
        point->dxr = mm_fixed_add(mm_fixed_mac3(0, camera.m[0][0],
            0, camera.m[1][0], z, camera.m[2][0]), camera.m[3][0]);
        point->dyr = 0;
        point->dzr = mm_fixed_add(mm_fixed_mac3(0, camera.m[0][2],
            0, camera.m[1][2], z, camera.m[2][2]), camera.m[3][2]);
    }
    if (text)
        point->dyr = mm_fixed_add(point->dyr, 655360L);
}
