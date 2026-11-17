/* Intrusive list policy converted from Kauai base.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "mm/linked_list.h"
#include <stddef.h>
static void assertion(const MMLinkedListHost *host, int valid, int code)
{
    if (!valid && host && host->assertion) host->assertion(host->context, code);
}
void mm_linked_list_init(MMLinkedListNode *node, void *owner)
{
    node->next = NULL;
    node->previous = NULL;
    node->owner = owner;
}
void mm_linked_list_attach(MMLinkedListNode *node, MMLinkedListNode **slot, const MMLinkedListHost *host)
{
    if (node->previous) {
        assertion(host, *node->previous == node, MM_LIST_UNLINK_PREVIOUS);
        *node->previous = node->next;
        if (node->next) {
            assertion(host, node->next->previous == &node->next, MM_LIST_UNLINK_NEXT);
            node->next->previous = node->previous;
        }
    }
    node->previous = slot;
    if (!slot) {
        node->next = NULL;
        return;
    }
    node->next = *slot;
    if (node->next) {
        assertion(host, node->next->previous == slot, MM_LIST_ATTACH_NEXT);
        node->next->previous = &node->next;
    }
    *slot = node;
}
void mm_linked_list_validate(const MMLinkedListNode *node, const MMLinkedListHost *host)
{
    if (node->next) {
        if (host && host->address) host->address(host->context, node->next->owner, 1);
        assertion(host, node->next->previous == &node->next, MM_LIST_VALIDATE_NEXT);
    }
    if (node->previous) {
        if (host && host->address) host->address(host->context, node->previous, 0);
        assertion(host, *node->previous == node, MM_LIST_VALIDATE_PREVIOUS);
    }
}
