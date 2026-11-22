/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef KAUAI_KIDSPACE_DESCRIPTOR_H
#define KAUAI_KIDSPACE_DESCRIPTOR_H
#include "kauai_core.h"
#include "framedef.h"
#include "mm/studio_resource.h"
using namespace Chunky;
MMKidspaceOwner *FetchKidspaceDescriptor(Chunky::PResourceCache, ChunkTagOrType, ChunkNumber, bool *error = nullptr);
#ifdef DEBUG
void MarkKidspaceDescriptor(MMKidspaceOwner *);
#endif

#endif
