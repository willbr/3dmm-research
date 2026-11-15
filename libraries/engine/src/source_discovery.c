/* Source discovery converted from engine tagman.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "mm/source_discovery.h"
int mm_source_discover_local(MMI32 sid, const MMSourceDiscovery *s, int *exists)
{
    int access;
    *exists = 0;
    access = s->access(s->context, sid);
    if (access != MM_SOURCE_LEGACY)
    {
        *exists = access == MM_SOURCE_AVAILABLE;
        return 1;
    }
    if (!s->names(s->context, sid))
        return 0;
    s->root(s->context);
    if (!s->descend(s->context, 0) && !s->descend(s->context, 1))
        return 1;
    s->output(s->context);
    *exists = 1;
    return 1;
}
int mm_source_discover_media(MMI32 sid, const MMSourceDiscovery *s, int *changed)
{
    int access = s->access(s->context, sid);
    if (access != MM_SOURCE_LEGACY)
    {
        *changed = access == MM_SOURCE_AVAILABLE;
        return *changed;
    }
    *changed = 0;
    if (s->known(s->context))
        return s->ensure_known(s->context);
    if (!s->names(s->context, sid) || !s->begin_drives(s->context))
        return 0;
    while (s->next_drive(s->context))
    {
        if (s->floppy(s->context))
            continue;
        s->candidate(s->context);
        if (!s->descend(s->context, 1) && !s->descend(s->context, 0))
            continue;
        *changed = 1;
        s->output(s->context);
        return 1;
    }
    return 0;
}
