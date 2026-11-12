/* Container links converted from Kauai BaseLinkedList.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "three_mm/chunk_context.h"
#include <string.h>
void three_mm_chunk_context_init(ThreeMMChunkContext *context)
{
    context->first = NULL;
}
void three_mm_chunk_link_init(ThreeMMChunkLink *link, void *owner)
{
    memset(link, 0, sizeof(*link));
    link->owner = owner;
}
int three_mm_chunk_context_valid(const ThreeMMChunkLink *link)
{
    if (!link || !link->owner)
        return 0;
    if (!link->context)
        return !link->next && !link->previous;
    return link->previous && *link->previous == link &&
           (!link->next || (link->next->context == link->context && link->next->previous == &link->next));
}
int three_mm_chunk_context_attach(ThreeMMChunkContext *context, ThreeMMChunkLink *link)
{
    if (!context || !three_mm_chunk_context_valid(link) || link->context ||
        (context->first && context->first->previous != &context->first))
        return THREE_MM_CHUNK_BAD_FILE;
    link->next = context->first;
    link->previous = &context->first;
    link->context = context;
    if (link->next)
        link->next->previous = &link->next;
    context->first = link;
    return THREE_MM_CHUNK_OK;
}
int three_mm_chunk_context_detach(ThreeMMChunkLink *link)
{
    if (!three_mm_chunk_context_valid(link))
        return THREE_MM_CHUNK_BAD_FILE;
    if (!link->context)
        return THREE_MM_CHUNK_OK;
    *link->previous = link->next;
    if (link->next)
        link->next->previous = link->previous;
    link->previous = NULL;
    link->next = NULL;
    link->context = NULL;
    return THREE_MM_CHUNK_OK;
}
void *three_mm_chunk_context_first(const ThreeMMChunkContext *context)
{
    return context && context->first ? context->first->owner : NULL;
}
void *three_mm_chunk_context_next(const ThreeMMChunkLink *link)
{
    return link && link->next ? link->next->owner : NULL;
}
