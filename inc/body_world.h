/* Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */

#ifndef BODY_WORLD_H
#define BODY_WORLD_H

class BodyWorld
{
    static void Attach(void *context, void *body);
    static void Changed(void *context);

  public:
    static void Bind(PBody body, PWorld world);
    static PBody PbodyClicked(long xp, long yp, PWorld world, long *pibset = pvNil);
    static bool FPtInBody(PBody body, PWorld world, long xp, long yp, long *pibset);
};
#endif
