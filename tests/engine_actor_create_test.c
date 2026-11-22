#include "mm/actor_create.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(c) do { if (!(c)) { fprintf(stderr,"creation check %d: %s\n",__LINE__,#c); exit(1); } } while (0)
typedef struct Ref { int count,kind; } Ref;
typedef struct Actor { Ref ref; MMActorOwner owner; } Actor;
typedef struct Fixture
{
    Actor actor; Ref template_object,body; MMTag tag; MMActorCreateServices s;
    int cache,scene,other_scene,fail,created,fetched,duplicated,tracks,sounds,rewound,flags,bodies,bound,releases;
    int closed,track_blocks,sound_blocks,diagnostics[3];
} Fixture;
static MMActorOwner *owner(void *p, void *actor) { (void)p; return &((Actor *)actor)->owner; }

static void *track_resize(void *p, void *ptr, MMU32 old, MMU32 size, int priority)
{
    Fixture *f = (Fixture *)p;
    void *data;
    (void)priority;
    f->tracks = 1;
    if (f->fetched)
        CHECK(f->duplicated == 1 && f->actor.owner.runtime.template_object);
    if (f->fail == 3)
        return NULL;
    data = realloc(ptr, size);
    if (data && !old)
        ++f->track_blocks;
    return data;
}

static void track_release(void *p, void *ptr, MMU32 bytes)
{
    Fixture *f = (Fixture *)p;
    (void)bytes;
    CHECK(!f->actor.owner.runtime.body && --f->track_blocks >= 0);
    free(ptr);
}

static void *sound_resize(void *p, void *ptr, MMU32 old, MMU32 size, int priority)
{
    Fixture *f = (Fixture *)p;
    void *data;
    (void)priority;
    CHECK(f->actor.owner.track.route && f->actor.owner.track.events);
    f->sounds = 1;
    if (f->fail == 4)
        return NULL;
    data = realloc(ptr, size);
    if (data && !old)
        ++f->sound_blocks;
    return data;
}

static void sound_release(void *p, void *ptr, MMU32 bytes)
{
    Fixture *f = (Fixture *)p;
    (void)bytes;
    CHECK(!f->actor.owner.runtime.template_object && --f->sound_blocks >= 0);
    free(ptr);
}

static void *create(void *p)
{
    Fixture *f = (Fixture *)p;
    MMStorageAllocator allocator = {NULL, sound_resize, sound_release};
    ++f->created;
    if (f->fail == 1)
        return NULL;
    mm_actor_owner_init(&f->actor.owner);
    allocator.context = f;
    CHECK(mm_actor_sounds_init(&f->actor.owner.sounds, &allocator));
    f->actor.owner.runtime.recording.recording = 1;
    f->actor.ref.count = 1;
    f->actor.ref.kind = 1;
    return &f->actor;
}

static void release(void *p,void *object)
{
    Fixture *f=(Fixture *)p; Ref *ref=(Ref *)object; CHECK(ref->count>0); --ref->count;
    if (ref->kind==1)
    {
        ++f->releases; CHECK(!ref->count); mm_actor_owner_dispose(object,&f->s.owner);
    }
    else if (ref->kind==2)
        CHECK(f->actor.owner.runtime.template_object == object && !f->actor.owner.track.route);
    else CHECK(ref->kind==3 && f->actor.owner.runtime.body==object);
}

static void *fetch_template(void *p,const MMTag *tag)
{
    Fixture *f=(Fixture *)p; CHECK(tag==&f->tag && f->actor.owner.runtime.template_tag.sid==-1);
    ++f->fetched; if (f->fail==2) return NULL;
    ++f->template_object.count; return &f->template_object;
}

static void retain_cache(void *p)
{
    Fixture *f = (Fixture *)p;
    CHECK(f->actor.owner.runtime.template_tag.cache == f);
    CHECK(f->actor.owner.runtime.template_object == &f->template_object && f->template_object.count == 1);
    ++f->cache;
    ++f->duplicated;
}

static void release_cache(void *p)
{
    Fixture *f = (Fixture *)p;
    CHECK(f->cache == 2);
    --f->cache;
    ++f->closed;
}

static const MMTagServices tags = {
    NULL, retain_cache, release_cache, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL
};
static void publish(void *p,const MMActorState *state) { ((Actor *)p)->owner.runtime.state=*state; }
static void recording(void *p,int enabled)
{ CHECK(!enabled); ((Actor *)p)->owner.runtime.recording.recording=enabled; }
static void route_services(void *p,void *a,MMActorRouteEditServices *s)
{
    Fixture *f=(Fixture *)p; CHECK(((Actor *)a)->owner.sounds.storage.buffers[0].data); ++f->rewound;
    memset(s,0,sizeof(*s)); s->edits.context=a; s->edits.publish=publish; s->recording=recording;
}
static int action_flags(void *p,void *object,MMI32 action,MMU32 *flags)
{
    Fixture *f=(Fixture *)p; CHECK(object==&f->template_object && !action && f->rewound==1);
    CHECK(!f->actor.owner.runtime.recording.recording && !f->actor.owner.runtime.state.nextEvent && f->actor.owner.runtime.state.actionEvent==-1);
    CHECK(f->actor.owner.runtime.state.location.dnfrm==-1 && f->actor.owner.runtime.state.transform.rScaleStep==65536L);
    ++f->flags; *flags=0x55; return f->fail!=5;
}
static void *create_body(void *p,void *object)
{
    Fixture *f=(Fixture *)p; CHECK(object==&f->template_object && f->actor.owner.runtime.scene==&f->scene && !f->actor.owner.runtime.body);
    ++f->bodies; if (f->fail==6) return NULL; ++f->body.count; return &f->body;
}
static void bind_body(void *p,void *body,void *scene)
{
    Fixture *f=(Fixture *)p; CHECK(body==&f->body && scene==&f->scene);
    CHECK(f->actor.owner.runtime.body==body && f->actor.owner.runtime.scene==scene); ++f->bound;
}
static void diagnostic(void *p,int which)
{ Fixture *f=(Fixture *)p; CHECK(which>=0 && which<3); ++f->diagnostics[which]; }
static void init(Fixture *f)
{
    MMActorOwnerServices *o;
    memset(f,0,sizeof(*f)); f->cache=1; f->template_object.kind=2; f->body.kind=3;
    f->tag.sid=0; f->tag.cache=f; f->tag.type=17; f->tag.number=23;
    o=&f->s.owner; o->context=f; o->owner=owner; o->create=create; o->release=release; o->release_body=release; o->release_template=release;
    o->tags=&tags; o->route_services=route_services;
    f->s.track_allocator.context = f;
    f->s.track_allocator.resize = track_resize;
    f->s.track_allocator.release = track_release;
    f->s.fetch_template=fetch_template;
    f->s.action_flags=action_flags; f->s.create_body=create_body; f->s.bind_body=bind_body; f->s.diagnostic=diagnostic;
}
static void clean(Fixture *f)
{ CHECK(!f->actor.ref.count && !f->template_object.count && !f->body.count && f->cache==1 && !f->track_blocks && !f->sound_blocks); }
int main(void)
{
    Fixture f; int fail; void *actor;
    for (fail=0;fail<=5;++fail)
    {
        init(&f); f.fail=fail; actor=mm_actor_create(&f.tag,&f.s);
        CHECK((actor!=NULL)==!fail && f.created==1);
        CHECK(f.fetched==(fail!=1) && f.tracks==(fail!=1 && fail!=2));
        CHECK(f.sounds==(fail==0 || fail>=4) && f.flags==(fail==0 || fail==5));
        CHECK(f.actor.owner.runtime.state.actionFlags==(f.flags?0x55:0));
        if (actor)
        {
            CHECK(f.actor.owner.runtime.first==2147483647L && f.actor.owner.runtime.last==(-2147483647L-1L) && f.actor.owner.runtime.actor==-1);
            CHECK(f.cache==2 && f.actor.owner.runtime.template_tag.number==23 && !f.actor.owner.runtime.scene && !f.actor.owner.runtime.body);
            mm_actor_bind_scene(&f.actor.owner.runtime,&f.scene,&f.s); CHECK(f.bodies==1 && f.bound==1);
            mm_actor_bind_scene(&f.actor.owner.runtime,&f.scene,&f.s); CHECK(f.bodies==1 && f.bound==1);
            mm_actor_bind_scene(&f.actor.owner.runtime,&f.other_scene,&f.s); CHECK(f.diagnostics[MM_CREATE_WRONG_SCENE]==1 && f.actor.owner.runtime.scene==&f.scene);
            release(&f,actor);
        }
        CHECK(f.releases==(fail!=1)); clean(&f);
    }
    init(&f); actor=mm_actor_create(&f.tag,&f.s); CHECK(actor); f.fail=6;
    mm_actor_bind_scene(&f.actor.owner.runtime,&f.scene,&f.s); CHECK(f.actor.owner.runtime.scene==&f.scene && !f.actor.owner.runtime.body && !f.bound && f.bodies==1);
    mm_actor_bind_scene(&f.actor.owner.runtime,&f.scene,&f.s); CHECK(f.bodies==1 && f.diagnostics[MM_CREATE_BOUND_WITHOUT_BODY]==1);
    release(&f,actor); clean(&f);
    /* Direct group creation leaves successful track storage for caller cleanup. */
    init(&f); actor=create(&f); f.fail=4; CHECK(!mm_actor_create_groups(&f.actor.owner,&f.s));
    CHECK(f.actor.owner.track.route && f.actor.owner.track.events && !f.sound_blocks && !f.flags && !f.rewound); release(&f,actor); clean(&f);
    /* Direct initialization retains the failed query's partial state/ownership. */
    init(&f); actor=create(&f); f.fail=5; CHECK(!mm_actor_initialize(actor,&f.tag,&f.s));
    CHECK(f.actor.ref.count==1 && f.template_object.count==1 && f.cache==2 && f.actor.owner.runtime.state.actionFlags==0x55);
    release(&f,actor); clean(&f);
    puts("C89 actor creation and binding checks passed"); return 0;
}
