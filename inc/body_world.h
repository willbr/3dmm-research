/* Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */

#ifndef BODY_WORLD_H
#define BODY_WORLD_H

class BodyWorld
{
    static void Attach(void *context, Body *body);
    static void Changed(void *context);

  public:
    static void Bind(Body *body, PWorld world);
    static Body *PbodyClicked(long xp, long yp, PWorld world, long *pibset = pvNil);
    static bool FPtInBody(Body *body, PWorld world, long xp, long yp, long *pibset);
};
#endif
