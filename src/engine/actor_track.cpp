/* Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */
#include "soc_core.h"
const MMTagServices &EngineActorTagServices()
{
    static const auto services = [] {
        MMTagServices s = {};
        s.retain = [](void *p) { static_cast<PChunkyResourceFile>(p)->AddRef(); };
        s.release = [](void *p) { static_cast<PChunkyResourceFile>(p)->Release(); };
        return s;
    }();
    return services;
}
