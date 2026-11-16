#include "mm/reference.h"
#include <stdio.h>
#include <stdlib.h>
#define CHECK(e) do { if (!(e)) { fprintf(stderr,"reference check %d: %s\n",__LINE__,#e); exit(1); } } while (0)
typedef struct Object
{
    int id;
    MMReference references;
    struct Object *child;
} Object;
static int events[8],count,issues,observed;
static Object *create(int id)
{
    Object *o=malloc(sizeof(*o)); CHECK(o!=NULL);
    o->id=id; o->child=NULL; mm_reference_init(&o->references);
    CHECK(o->references.count==1); return o;
}
static void destroy(void *p)
{
    Object *o=p,*child=o->child;
    CHECK(o->references.count==0 && count<8); events[count++]=o->id;
    free(o);
    if (child) mm_reference_release(&child->references,child,destroy);
}
static void issue(void *p)
{
    Object *o=p; observed=o->references.count; ++issues;
    /* Diagnosis precedes the final zero publication even if the callback writes. */
    o->references.count=27;
}
int main(void)
{
    Object *o,*child; MMReference state; int i;
    o=create(1); child=create(2); o->child=child;
    CHECK(mm_reference_add(&o->references)==2);
    mm_reference_release(&o->references,o,destroy);
    CHECK(!count && o->references.count==1 && child->references.count==1);
    mm_reference_release(&o->references,o,destroy);
    CHECK(count==2 && events[0]==1 && events[1]==2);
    o=create(3);
    mm_reference_release_marked(&o->references,1,o,issue,destroy);
    CHECK(o->references.count==0 && count==2 && !issues);
    CHECK(mm_reference_add(&o->references)==1);
    mm_reference_release_marked(&o->references,0,o,issue,destroy);
    CHECK(count==3 && events[2]==3);
    o=create(4); o->references.count=0;
    mm_reference_release_marked(&o->references,0,o,issue,destroy);
    CHECK(observed==0 && issues==1 && o->references.count==0 && count==3);
    o->references.count=-7;
    mm_reference_release_marked(&o->references,1,o,issue,destroy);
    CHECK(observed==-7 && issues==2 && o->references.count==0 && count==3);
    CHECK(mm_reference_add(&o->references)==1);
    mm_reference_release(&o->references,o,destroy); CHECK(count==4 && events[3]==4);
    mm_reference_init(&state);
    for(i=0;i<4096;++i) CHECK(mm_reference_add(&state)==i+2);
    for(i=4096;i>=0;--i) CHECK(mm_reference_drop(&state)==i);
    CHECK(mm_reference_add(&state)==1);
    state.count=0x7fffffffL;
    CHECK(mm_reference_add(&state)==(-0x7fffffffL-1));
    CHECK(mm_reference_drop(&state)==0x7fffffffL);
    state.count=0; CHECK(mm_reference_drop(&state)==-1);
    puts("C89 references: publication, marked owners, zero revival and freeing callbacks passed");
    return 0;
}
