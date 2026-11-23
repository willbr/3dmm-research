/* Container links converted from Kauai BaseLinkedList.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "mm/chunk_context.h"
#include "mm/chunk_owner.h"
#include <string.h>
void mm_chunk_context_init(MMChunkContext *context)
{
    context->first = NULL;
}
void mm_chunk_link_init(MMChunkLink *link, void *owner, MMChunkOwnerRead *reader, const MMReference *references)
{
    memset(link, 0, sizeof(*link));
    link->owner = owner;
    link->reader = reader;
    link->references = references;
}
int mm_chunk_context_valid(const MMChunkLink *link)
{
    if (!link || !link->owner)
        return 0;
    if (!link->context)
        return !link->next && !link->previous;
    return link->previous && *link->previous == link &&
           (!link->next || (link->next->context == link->context && link->next->previous == &link->next));
}
int mm_chunk_context_attach(MMChunkContext *context, MMChunkLink *link)
{
    if (!context || !mm_chunk_context_valid(link) || !link->reader || !link->reader->storage ||
        !link->reader->storage->owner || !link->references || link->context ||
        (context->first && context->first->previous != &context->first))
        return MM_CHUNK_BAD_FILE;
    link->next = context->first;
    link->previous = &context->first;
    link->context = context;
    if (link->next)
        link->next->previous = &link->next;
    context->first = link;
    return MM_CHUNK_OK;
}
int mm_chunk_context_detach(MMChunkLink *link)
{
    if (!mm_chunk_context_valid(link))
        return MM_CHUNK_BAD_FILE;
    if (!link->context)
        return MM_CHUNK_OK;
    *link->previous = link->next;
    if (link->next)
        link->next->previous = link->previous;
    link->previous = NULL;
    link->next = NULL;
    link->context = NULL;
    return MM_CHUNK_OK;
}
void *mm_chunk_context_first(const MMChunkContext *context)
{
    return context && context->first ? context->first->owner : NULL;
}
void *mm_chunk_context_next(const MMChunkLink *link)
{
    return link && link->next ? link->next->owner : NULL;
}
const MMChunkLink *mm_chunk_context_find(const MMChunkContext *context, void *file, void (*extra_collision)(void *))
{
    const MMChunkLink *link;
    if (!context || !file)
        return NULL;
    for (link = context->first; link; link = link->next)
    {
        const MMChunkReopenState *files = &link->reader->storage->owner->files;
        if (extra_collision && files->extra_file == file)
            extra_collision(link->owner);
        if (files->main_file == file && !files->invalid_main)
            return link;
    }
    return NULL;
}
void mm_chunk_context_clear_marks(const MMChunkContext *context, void (*validate)(void *))
{
    const MMChunkLink *link;
    for (link = context->first; link; link = link->next)
    {
        if (validate)
            validate(link->owner);
        link->reader->storage->owner->marked = 0;
        if (validate)
            validate(link->owner);
    }
}
void mm_chunk_context_close_unmarked(const MMChunkContext *context, void (*validate)(void *),
                                     void (*destroy)(void *))
{
    const MMChunkLink *link, *next;
    for (link = context->first; link; link = next)
    {
        if (validate)
            validate(link->owner);
        next = link->next;
        if (!link->reader->storage->owner->marked && !link->references->count)
            destroy(link->owner);
    }
}
