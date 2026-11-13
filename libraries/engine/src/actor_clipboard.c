/* Actor clipboard policy converted from engine actredit.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "three_mm/actor_clipboard.h"
#include <string.h>
void three_mm_actor_clipboard_init(ThreeMMActorClipboard *c) { c->actor=NULL; c->route_only=0; }
void *three_mm_actor_clipboard_create(void *source,int route_only,int entire,const ThreeMMActorClipboardServices *s)
{
    void *clipboard,*actor;
    const unsigned char *prefix;
    size_t count=0;
    clipboard=s->create(s->context); if (!clipboard) return NULL;
    if (!s->copy(s->context,source,route_only,entire,&actor))
    { s->release_clipboard(s->context,clipboard); return NULL; }
    if (!route_only)
    {
        s->source_name(s->context,source,s->name); s->copy_prefix(s->context,source,s->prefix);
        prefix=(const unsigned char *)s->text(s->prefix);
        while (prefix[count*s->unit] || (s->unit==2 && prefix[count*s->unit+1])) ++count;
        if (memcmp(s->text(s->name),prefix,count*s->unit))
        {
            s->assign_name(clipboard,s->prefix);
            if (!s->append_space(clipboard) || !s->append_name(clipboard,s->name))
            { s->name_too_long(s->context); s->release_clipboard(s->context,clipboard); return NULL; }
        }
        else s->assign_name(clipboard,s->name);
    }
    s->set(clipboard,actor,route_only); return clipboard;
}
void three_mm_actor_clipboard_dispose(ThreeMMActorClipboard *c,const ThreeMMActorClipboardServices *s)
{
    if (c->actor) s->release_actor(s->context,c->actor);
    c->actor=NULL;
}
int three_mm_actor_clipboard_paste(const ThreeMMActorClipboard *c,void *name,void *movie,const ThreeMMActorClipboardServices *s)
{
    void *actor;
    if (c->route_only) return s->paste_route(s->context,movie,c->actor);
    if (!s->duplicate(s->context,c->actor,1,&actor)) return 0;
    if (!s->paste_actor(s->context,movie,actor)) { s->release_actor(s->context,actor); return 0; }
    if (!s->rename(s->context,movie,s->actor_id(actor),name))
    {
        s->remove(s->context,movie,s->actor_id(actor)); s->release_actor(s->context,actor); return 0;
    }
    s->release_actor(s->context,actor); return 1;
}
