#include "mm/linked_list.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static void check(int value, int line)
{
    if (!value) { fprintf(stderr, "Linked list check failed at %d\n", line); exit(1); }
}
#define CHECK(x) check((x), __LINE__)
typedef struct Observer {
    int events[16], count;
    const void *addresses[16];
} Observer;
static void address(void *context, const void *p, int owner)
{
    Observer *o = (Observer *)context;
    CHECK(o->count < 16);
    o->events[o->count] = owner ? 10 : 11;
    o->addresses[o->count++] = p;
}
static void assertion(void *context, int code)
{
    Observer *o = (Observer *)context;
    CHECK(o->count < 16);
    o->events[o->count++] = code;
}
static void list(MMLinkedListNode **head, MMLinkedListNode **nodes, int count)
{
    int i;
    MMLinkedListNode **slot = head;
    for (i = 0; i < count; ++i) {
        CHECK(*slot == nodes[i] && nodes[i]->previous == slot);
        mm_linked_list_validate(nodes[i], NULL);
        slot = &nodes[i]->next;
    }
    CHECK(!*slot);
}
int main(void)
{
    MMLinkedListNode a, b, c, d, *head = NULL, *other = NULL, *order[4];
    Observer o;
    MMLinkedListHost host;
    int owners[4], i;
    memset(&o, 0, sizeof(o));
    host.context = &o; host.address = address; host.assertion = assertion;
    mm_linked_list_init(&a, &owners[0]); mm_linked_list_init(&b, &owners[1]);
    mm_linked_list_init(&c, &owners[2]); mm_linked_list_init(&d, &owners[3]);
    CHECK(!a.next && !a.previous && a.owner == &owners[0]);
    mm_linked_list_attach(&a, &head, &host);
    mm_linked_list_attach(&b, &head, &host);
    mm_linked_list_attach(&c, &b.next, &host);
    order[0] = &b; order[1] = &c; order[2] = &a; list(&head, order, 3);
    mm_linked_list_attach(&b, &head, &host); list(&head, order, 3);
    mm_linked_list_attach(&d, &a.next, &host);
    order[3] = &d; list(&head, order, 4);
    /* Move a head to an internal slot and a tail to a different head. */
    mm_linked_list_attach(&b, &a.next, &host);
    order[0] = &c; order[1] = &a; order[2] = &b; list(&head, order, 4);
    mm_linked_list_attach(&d, &other, &host); list(&head, order, 3);
    order[0] = &d; list(&other, order, 1);
    mm_linked_list_attach(&b, NULL, &host); mm_linked_list_attach(&b, NULL, &host);
    CHECK(!b.previous && !b.next && b.owner == &owners[1]);
    order[0] = &c; order[1] = &a; list(&head, order, 2);
    CHECK(!o.count);
    mm_linked_list_validate(&c, &host);
    CHECK(o.count == 2 && o.events[0] == 10 && o.addresses[0] == a.owner);
    CHECK(o.events[1] == 11 && o.addresses[1] == &head);
    /* Readable corruption reports in the original address/diagnostic order. */
    o.count = 0; a.previous = &other;
    mm_linked_list_validate(&c, &host);
    CHECK(o.count == 3 && o.events[0] == 10 && o.events[1] == MM_LIST_VALIDATE_NEXT && o.events[2] == 11);
    a.previous = &c.next;
    o.count = 0; head = &a;
    mm_linked_list_validate(&c, &host);
    CHECK(o.count == 3 && o.events[2] == MM_LIST_VALIDATE_PREVIOUS);
    head = &c;
    o.count = 0; a.previous = &other;
    mm_linked_list_attach(&c, NULL, &host);
    CHECK(o.count == 1 && o.events[0] == MM_LIST_UNLINK_NEXT);
    CHECK(head == &a && a.previous == &head);
    o.count = 0; head = NULL;
    mm_linked_list_attach(&a, NULL, &host);
    CHECK(o.count == 1 && o.events[0] == MM_LIST_UNLINK_PREVIOUS && !head);
    o.count = 0; d.previous = NULL;
    mm_linked_list_attach(&a, &other, &host);
    CHECK(o.count == 1 && o.events[0] == MM_LIST_ATTACH_NEXT);
    CHECK(other == &a && a.next == &d && d.previous == &a.next);
    mm_linked_list_attach(&d, NULL, NULL); mm_linked_list_attach(&a, NULL, NULL);
    CHECK(!head && !other);
    /* Repeated head and interior moves stress all predecessor/successor repairs. */
    for (i = 0; i < 1000; ++i) {
        mm_linked_list_attach(&a, &head, NULL); mm_linked_list_attach(&b, &a.next, NULL);
        mm_linked_list_attach(&c, &b.next, NULL); mm_linked_list_attach(&d, &head, NULL);
        order[0] = &d; order[1] = &a; order[2] = &b; order[3] = &c; list(&head, order, 4);
        mm_linked_list_attach(&a, &other, NULL); mm_linked_list_attach(&c, &other, NULL);
        order[0] = &d; order[1] = &b; list(&head, order, 2);
        order[0] = &c; order[1] = &a; list(&other, order, 2);
        mm_linked_list_attach(&b, NULL, NULL); mm_linked_list_attach(&c, NULL, NULL);
        mm_linked_list_attach(&d, NULL, NULL); mm_linked_list_attach(&a, NULL, NULL);
        CHECK(!head && !other);
    }
    puts("C89 linked list: slots, moves, detach, diagnostics and owner identity passed");
    return 0;
}
