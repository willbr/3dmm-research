/* Copyright (c) 3DMMForever contributors. Licensed under the MIT License. */
#include "brender.h"
#include "brlists.h"
#include <stdio.h>

int main(void)
{
    /* Reproduce a list head inside a four-byte-packed actor on a 64-bit host.
     * Struct member access honors its packing; a raw pointer-to-pointer
     * dereference of the saved link slot does not.
     */
#pragma pack(push, 8)
    union {
        double alignment;
        unsigned char bytes[sizeof(struct br_simple_list) + 8];
    } storage;
#pragma pack(pop)
    struct br_simple_list *list = (struct br_simple_list *)(storage.bytes + 4);
    struct br_simple_node a, b, c;
    BrSimpleNewList(list);
    BrSimpleAddHead(list, &a);
    BrSimpleAddHead(list, &b);
    BrSimpleAddHead(list, &c);
    if (list->head != &c || c.next != &b || b.next != &a)
        return 1;
    BrSimpleRemove(&b);
    if (list->head != &c || c.next != &a || a.prev != &c.next || b.prev || b.next)
        return 1;
    BrSimpleRemove(&c);
    if (list->head != &a || a.prev != &list->head || c.prev || c.next)
        return 1;
    BrSimpleRemove(&a);
    if (list->head || a.prev || a.next)
        return 1;
    puts("BRender lists: packed head, middle and tail removal passed.");
    return 0;
}
