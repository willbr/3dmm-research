#ifndef ENGINE_BODY_CREATE_FIXTURE_H
#define ENGINE_BODY_CREATE_FIXTURE_H
static bool TestBodyCreation()
{
    FILE *snapshot=nullptr;
    if (const char *path=std::getenv("MM_BODY_CREATE_SNAPSHOT"))
        if (!(snapshot=std::fopen(path,"wb"))) return false;
    auto record=[&](const void *p,size_t n) { return !snapshot || std::fwrite(p,1,n,snapshot)==n; };
    short parents[6][7]={{-1},{-1},{-1,0,1,2,3,4},{-1,0,0,1,2,2},{-1,-1,0,-1,1,3},{-1,0,0,1,1,2,2}};
    short sets[6][7]={{0},{0},{0,1,2,3,4,5},{0,1,1,2,2,2},{0,0,1,1,0,1},{0,2,4,6,2,4,6}};
    const int counts[6]={0,1,6,6,6,7};
    bool ok=true; int successes=0,failures=0;
    for (int shape=0;ok && shape<6;++shape)
    {
        auto par=DynamicArray::PglNew(sizeof(short)); auto set=DynamicArray::PglNew(sizeof(short));
        ok=par && set;
        for (int i=0;ok && i<counts[shape];++i) ok=par->FAdd(&parents[shape][i]) && set->FAdd(&sets[shape][i]);
        // Warm the shared highlight material before allocator failure injection.
        auto warm=ok?CreateFixtureBody(par,set):nullptr; ok=ok && warm; mm_body_owner_release(warm);
        for (int kind=0;ok && kind<3;++kind) for (int failure=0;ok && failure<9;++failure)
        {
#ifdef DEBUG
            vmutxMem.Enter();
            auto allocator=kind==0?&vdmglob.dmaglBase:kind==1?&vdmglob.dmaglHq:&vdmglob.dmaglPv;
            const LegacyLong beforeBase=vdmglob.dmaglBase.cv,beforeHq=vdmglob.dmaglHq.cv,beforePv=vdmglob.dmaglPv.cv;
            if (failure) { allocator->cactDo=failure; allocator->cactFail=1; }
            vmutxMem.Leave();
#endif
            auto body=CreateFixtureBody(par,set);
#ifdef DEBUG
            vmutxMem.Enter(); allocator->cactFail=0; vmutxMem.Leave();
#endif
            LegacyLong values[]={shape,kind,failure,body!=nullptr,body?mm_body_owner_state(body)->shape.set_count:-1,
                body?(mm_body_owner_state(body)->shape.part_count + 2):-1,body?mm_body_owner_state(body)->shape.hidden:-1};
            ok=record(values,sizeof(values)) && ok;
            if (body)
            {
                ++successes; ok=ok && (mm_body_owner_state(body)->shape.part_count + 2)==counts[shape]+2 && mm_body_owner_state(body)->shape.hidden==1;
                auto root=mm_brender_body_root(body);
                auto index=[&](PBACT actor)->LegacyLong { return actor?static_cast<LegacyLong>(actor-root):-1; };
                for (int i=0;ok && i<(mm_body_owner_state(body)->shape.part_count + 2);++i)
                {
                    auto actor=root+i;
                    LegacyLong fields[]={actor->type,actor->render_style,actor->t.type,index(actor->parent),
                        index(actor->children),index(actor->next),actor->prev!=nullptr,actor->identifier==(schar *)body,
                        actor->material!=nullptr,actor->model!=nullptr};
                    ok=record(fields,sizeof(fields)) && record(&actor->t.t.mat,sizeof(actor->t.t.mat)) && ok;
                    if (i>=2) ok=ok && actor->parent==root+(parents[shape][i-2]<0?0:parents[shape][i-2]+2);
                    MMI32 partSet=-2;
                    ok=ok && mm_body_from_actor(actor, &partSet, &mm_kauai_body_renderer()->geometry)==body &&
                        partSet==(i<2?-1:sets[shape][i-2]);
                }
                if ((kind+failure)%2) mm_body_show(body, &mm_kauai_body_renderer()->edit.runtime); // disposal must hide a visible body first
            }
            else ++failures;
            mm_body_owner_release(body);
#ifdef DEBUG
            ok=ok && vdmglob.dmaglBase.cv==beforeBase && vdmglob.dmaglHq.cv==beforeHq && vdmglob.dmaglPv.cv==beforePv;
#endif
            vpers->Clear();
            if (!ok) std::fprintf(stderr,"Body creation fixture failed: shape=%d kind=%d failure=%d\n",shape,kind,failure);
        }
        ReleasePpo(&par); ReleasePpo(&set);
    }
    std::fprintf(stderr,"[body creation] successes=%d failures=%d\n",successes,failures);
    ok=ok && successes>0;
#ifdef DEBUG
    ok=ok && failures>0;
#endif
    if (snapshot) ok=std::fclose(snapshot)==0 && ok;
    return ok;
}
#endif
