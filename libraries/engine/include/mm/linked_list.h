/* Intrusive list policy converted from Kauai base.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef MM_ENGINE_LINKED_LIST_H
#define MM_ENGINE_LINKED_LIST_H
#ifdef __cplusplus
extern "C" {
#endif
typedef struct MMLinkedListNode MMLinkedListNode;
struct MMLinkedListNode {
    MMLinkedListNode *next;
    MMLinkedListNode **previous;
    void *owner;
};
enum {
    MM_LIST_UNLINK_PREVIOUS = 1, MM_LIST_UNLINK_NEXT, MM_LIST_ATTACH_NEXT,
    MM_LIST_VALIDATE_NEXT, MM_LIST_VALIDATE_PREVIOUS
};
typedef struct MMLinkedListHost {
    void *context;
    void (*address)(void *, const void *address, int owner);
    void (*assertion)(void *, int code);
} MMLinkedListHost;
/* The C record owns links, not owner storage. Initialize before use. Nodes and
 * head slots must stay at fixed addresses until detached; owners stay live.
 * A slot is either a C node's next field or a caller-owned MMLinkedListNode*.
 * Never cast a typed owner-pointer slot to a node-pointer slot. Attach first
 * unlinks, then inserts at the requested slot. A destination inside this node
 * is invalid. Detach before destroying a linked node or its head slot. Repeated
 * detach and attaching at the current previous slot are valid. Synchronization
 * is caller-owned; synchronous diagnostic hooks cannot mutate links or unwind.
 * A null host suppresses diagnostics. Validation assumes readable addresses;
 * reported link corruption does not stop the original attach transaction. */
void mm_linked_list_init(MMLinkedListNode *, void *owner);
void mm_linked_list_attach(MMLinkedListNode *, MMLinkedListNode **slot, const MMLinkedListHost *);
void mm_linked_list_validate(const MMLinkedListNode *, const MMLinkedListHost *);
#ifdef __cplusplus
}
#endif
#endif
