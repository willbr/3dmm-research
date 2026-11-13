#include "three_mm/actor_transaction.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(c) do { if (!(c)) { fprintf(stderr,"transaction check %d: %s\n",__LINE__,#c); exit(1); } } while (0)
typedef struct Fixture
{
    ThreeMMActorTransactionServices transaction;
    ThreeMMActorDeleteServices deletion;
    ThreeMMActorDeleteState state;
    ThreeMMActorTransactionResult result;
    ThreeMMActorDeleteResult alive;
    int snapshot,refs,edited,registered,restored,detached,releases,publications,trash,alive_writes;
    int fail_duplicate,fail_apply,fail_undo,fail_subroute,fail_seek,fail_lifetime,has_output;
    int seeks,frames[2],forward,all,wants_alive,original,events,event_queries,add_queries,sounds,faults,last_fault;
    int return_alive,override_frame,subroutes;
    ThreeMMI32 add_frame;
    char trace[96]; int count;
} Fixture;
static void mark(Fixture *f,char c) { CHECK(f->count<94); f->trace[f->count++]=c; f->trace[f->count]=0; }
static int duplicate(void *p,void **snapshot)
{ Fixture *f=(Fixture *)p; mark(f,'D'); *snapshot=f->fail_duplicate?NULL:&f->snapshot; f->refs=!f->fail_duplicate; return !f->fail_duplicate; }
static int apply(void *p) { Fixture *f=(Fixture *)p; mark(f,'A'); ++f->edited; return !f->fail_apply; }
static void restore(void *p,void *snapshot)
{ Fixture *f=(Fixture *)p; mark(f,'R'); CHECK(snapshot==(f->fail_duplicate?NULL:&f->snapshot)); f->edited=0; ++f->restored; }
static int undo(void *p,void *snapshot)
{
    Fixture *f=(Fixture *)p; mark(f,'U'); CHECK(snapshot==(f->fail_duplicate?NULL:&f->snapshot));
    if (f->fail_undo) return 0;
    CHECK(snapshot); ++f->refs; ++f->registered; f->detached=1; return 1;
}
static void release(void *p,void *snapshot)
{ Fixture *f=(Fixture *)p; mark(f,'L'); CHECK(snapshot==&f->snapshot && f->refs>0); --f->refs; ++f->releases; }
static void publish_snapshot(void *p,void *snapshot)
{ Fixture *f=(Fixture *)p; mark(f,'O'); CHECK(snapshot==&f->snapshot && f->result.snapshot==snapshot && f->detached && f->refs==2); ++f->publications; }
static void publish_alive(void *p,int value,int trashed)
{ Fixture *f=(Fixture *)p; mark(f,'O'); CHECK(value==f->alive.alive && trashed==f->alive.trashed); if (trashed) ++f->trash; else ++f->alive_writes; }
static void read_state(void *p,ThreeMMActorDeleteState *state) { Fixture *f=(Fixture *)p; mark(f,'q'); *state=f->state; }
static ThreeMMI32 add_frame(void *p,ThreeMMI32 event)
{ Fixture *f=(Fixture *)p; mark(f,'a'); CHECK(event==f->state.add_event); ++f->add_queries; return f->add_frame; }
static ThreeMMI32 events(void *p) { Fixture *f=(Fixture *)p; mark(f,'n'); ++f->event_queries; return f->events; }
static int subroute(void *p) { Fixture *f=(Fixture *)p; mark(f,'E'); ++f->edited; ++f->subroutes; return !f->fail_subroute; }
static int seek(void *p,ThreeMMI32 frame)
{
    Fixture *f=(Fixture *)p; mark(f,'S'); CHECK(f->seeks<2);
    if (f->seeks==1 && f->has_output) CHECK(f->alive_writes==1);
    f->frames[f->seeks++]=frame; f->state.frame=frame; return f->seeks!=f->fail_seek;
}
static int forward(void *p,int all,int wants_alive,ThreeMMI32 original)
{
    Fixture *f=(Fixture *)p; mark(f,'F'); ++f->forward; ++f->edited;
    f->all=all; f->wants_alive=wants_alive; f->original=original; f->state.next_event=99;
    if (f->override_frame) f->state.frame=f->override_frame;
    return f->return_alive;
}
static int lifetime(void *p) { Fixture *f=(Fixture *)p; mark(f,'T'); return !f->fail_lifetime; }
static void sound(void *p) { Fixture *f=(Fixture *)p; mark(f,'H'); ++f->sounds; }
static void diagnostic(void *p,int fault) { Fixture *f=(Fixture *)p; mark(f,'X'); ++f->faults; f->last_fault=fault; }
static void init(Fixture *f)
{
    memset(f,0,sizeof(*f)); f->transaction.context=f; f->transaction.duplicate=duplicate; f->transaction.apply=apply;
    f->transaction.restore=restore; f->transaction.create_undo=undo; f->transaction.release=release;
    f->result.snapshot=f; f->result.context=f; f->result.publish=publish_snapshot;
    f->alive.alive=7; f->alive.trashed=3; f->alive.context=f; f->alive.publish=publish_alive;
    f->deletion.context=f; f->deletion.read=read_state; f->deletion.add_frame=add_frame;
    f->deletion.event_count=events; f->deletion.delete_subroute=subroute; f->deletion.seek=seek;
    f->deletion.delete_forward=forward; f->deletion.lifetime=lifetime; f->deletion.sound_frame=sound; f->deletion.diagnostic=diagnostic;
    f->state.frame=10; f->state.add_event=4; f->state.next_event=42; f->state.scene_present=1;
    f->add_frame=9; f->events=5; f->return_alive=1;
}
static int delete_actor(Fixture *f,int all,int output)
{ f->has_output=output; return three_mm_actor_transaction_delete(all,output?&f->alive:NULL,&f->transaction,&f->deletion); }
int main(void)
{
    Fixture f; int kind,fail,output;
    for (kind=0;kind<=THREE_MM_TRANSACTION_REMOVE;++kind) for (output=0;output<2;++output) for (fail=0;fail<4;++fail)
    {
        init(&f); f.fail_duplicate=fail==1; f.fail_apply=fail==2; f.fail_undo=fail==3;
        /* The legacy costume failure proceeds to core/undo with NULL snapshot.
         * Mock a rejecting undo callback instead of invoking invalid native data. */
        if (kind==THREE_MM_TRANSACTION_COSTUME && fail==1) f.fail_undo=1;
        CHECK(three_mm_actor_transaction_apply(kind,output?&f.result:NULL,&f.transaction)==!fail);
        if (!fail)
        {
            CHECK(f.registered==1 && f.detached && f.edited==1);
            CHECK(f.publications==(kind==THREE_MM_TRANSACTION_ACTION && output));
            CHECK(f.refs==(f.publications?2:1) && f.releases==!f.publications);
            CHECK(!strcmp(f.trace,f.publications?"DAUO":"DAUL"));
        }
        else
        {
            CHECK(f.result.snapshot==&f && !f.publications && !f.registered && !f.edited);
            if (fail==1) CHECK(!strcmp(f.trace,kind==THREE_MM_TRANSACTION_COSTUME?"DAUR":"D"));
            if (fail==2) CHECK(!strcmp(f.trace,kind==THREE_MM_TRANSACTION_REMOVE?"DAR":"DARL"));
            if (fail==3) CHECK(!strcmp(f.trace,"DAURL"));
            CHECK(f.refs==((kind==THREE_MM_TRANSACTION_REMOVE && fail==2)?1:0));
        }
    }
    init(&f); f.override_frame=50; CHECK(delete_actor(&f,0,1));
    CHECK(!strcmp(f.trace,"DqaSFOqSqHTUL") && f.frames[0]==9 && f.frames[1]==51);
    CHECK(f.original==42 && f.wants_alive && !f.all && f.alive.alive==1 && f.refs==1 && f.edited==1);
    init(&f); f.add_frame=10; CHECK(delete_actor(&f,0,1));
    CHECK(!strcmp(f.trace,"DqaEnOTUL") && !f.seeks && !f.sounds && f.subroutes==1);
    init(&f); f.add_frame=10; f.events=0; CHECK(delete_actor(&f,0,1) && !f.alive.alive);
    init(&f); f.add_frame=10; CHECK(delete_actor(&f,0,0) && !f.event_queries && !f.alive_writes && f.alive.alive==7);
    init(&f); f.add_frame=10; CHECK(delete_actor(&f,1,1) && !f.subroutes && !f.add_queries && f.all);
    init(&f); f.state.add_event=-1; f.state.scene_present=0;
    CHECK(delete_actor(&f,0,0) && !f.add_queries && !f.sounds && !f.wants_alive && !f.alive_writes);
    init(&f); f.fail_duplicate=1; CHECK(!delete_actor(&f,0,1));
    CHECK(!strcmp(f.trace,"DO") && f.trash==1 && !f.alive_writes && !f.faults && !f.refs);
    init(&f); f.fail_duplicate=1; CHECK(!delete_actor(&f,0,0) && !strcmp(f.trace,"D") && !f.trash);
    init(&f); f.add_frame=10; f.fail_subroute=1; CHECK(!delete_actor(&f,0,1));
    CHECK(!strcmp(f.trace,"DqaEXL") && f.edited==1 && !f.restored && !f.refs && f.alive.alive==7);
    for (fail=1;fail<=2;++fail)
    {
        init(&f); f.fail_seek=fail; CHECK(!delete_actor(&f,0,1));
        CHECK(f.seeks==fail && !f.restored && !f.registered && !f.refs && f.last_fault==THREE_MM_TRANSACTION_SEEK_FAILED);
        CHECK(f.alive_writes==(fail==2) && f.edited==(fail==2) && f.frames[0]==9);
    }
    init(&f); f.fail_lifetime=1; CHECK(delete_actor(&f,0,1));
    CHECK(f.last_fault==THREE_MM_TRANSACTION_BAD_LIFETIME && f.registered==1 && !f.restored && f.refs==1);
    init(&f); f.fail_undo=1; CHECK(!delete_actor(&f,0,1));
    CHECK(f.restored==1 && f.alive.alive==1 && f.alive_writes==1 && !f.edited && !f.refs && !f.faults);
    puts("actor edit transaction C89 checks passed"); return 0;
}
