#ifndef ENGINE_BODY_RUNTIME_FIXTURE_H
#define ENGINE_BODY_RUNTIME_FIXTURE_H
static void SetCostumeModel(PCustomMaterial_CMTL costume, int index, PModel model)
{
    mm_material_owner_custom(costume)->models[index] = model;
    mm_model_owner_retain(model);
}
static bool TestBodyRuntime()
{
    FILE *snapshot=nullptr;
    if (const char *path=std::getenv("MM_BODY_RUNTIME_SNAPSHOT")) if (!(snapshot=std::fopen(path,"wb"))) return false;
    auto record=[&](const void *p,size_t n) { return !snapshot || std::fwrite(p,1,n,snapshot)==n; };
    BRV vertices[3]={}; BRF face={}; vertices[1].p.v[0]=BR_SCALAR(1); vertices[2].p.v[1]=BR_SCALAR(1); face.vertices[1]=1; face.vertices[2]=2;
    auto a = mm_model_new(3, vertices, 1, &face, mm_kauai_model_host());
    auto b = mm_model_new(3, vertices, 1, &face, mm_kauai_model_host());
    auto ma = mm_brender_material_new(-1, -1), mb = mm_brender_material_new(-1, -1);
    PMaterial_MTRL parts[]={ma,mb,ma,mb,ma,mb};
    auto ca = ma && mb ? mm_custom_material_new(0, 6, parts, mm_kauai_material_host()) : nullptr;
    auto cb = ma && mb ? mm_custom_material_new(1, 6, parts, mm_kauai_material_host()) : nullptr;
    bool ok=a && b && ma && mb && ca && cb; int captures=0,failed=0;
    if (ok) { SetCostumeModel(ca,0,a); SetCostumeModel(ca,2,b);
        SetCostumeModel(cb,0,b); SetCostumeModel(cb,2,a); }
    const int counts[]={0,2,6};
    for (int shape=0;ok && shape<3;++shape) for (int visibility=0;ok && visibility<3;++visibility)
    for (int accessory=0;ok && accessory<2;++accessory) for (int failure=0;ok && failure<5;++failure)
    {
#ifdef DEBUG
        const LegacyLong base=vdmglob.dmaglBase.cv,hq=vdmglob.dmaglHq.cv,pv=vdmglob.dmaglPv.cv;
#endif
        BodyEditRenderHost host={BrActorAllocate(BR_ACTOR_NONE,0),0,0};
        auto parents=DynamicArray::PglNew(sizeof(short)),sets=DynamicArray::PglNew(sizeof(short));
        auto newParents=DynamicArray::PglNew(sizeof(short)),newSets=DynamicArray::PglNew(sizeof(short));
        ok=host.root && parents && sets && newParents && newSets;
        int n=counts[shape],next=counts[(shape+visibility+1)%3];
        for (int i=0;ok && i<n;++i) { short par=i?static_cast<short>((i-1)/2):-1,set=static_cast<short>(i%2); ok=parents->FAdd(&par) && sets->FAdd(&set); }
        for (int i=0;ok && i<next;++i) { short par=i?static_cast<short>(i-1):-1,set=static_cast<short>(i%3); ok=newParents->FAdd(&par) && newSets->FAdd(&set); }
        auto body=ok?EngineBody::Create(parents,sets):nullptr;
        ok=ok && body;
        auto state=[&](int stage) {
            LegacyLong values[]={shape,visibility,accessory,failure,stage,mm_model_owner_references(a),mm_model_owner_references(b),mm_material_owner_references(ma),mm_material_owner_references(mb),mm_material_owner_references(ca),mm_material_owner_references(cb),host.attached,host.changed};
            return record(values,sizeof(values)) && BodyEditFixtureAccess::Record(body,snapshot,host.root);
        };
        if (ok)
        {
            ScopedBodyCostume costume;
            for (int i=0;i<EngineBody::SetCount(body);++i) EngineBody::SetPartSetMtrl(body, i,ma);
            EngineBody::SetRenderCallbacks(body, &host,BodyEditAttach,BodyEditChanged);
            if (!visibility) EngineBody::Show(body); else if (visibility==2) { EngineBody::Hide(body); EngineBody::Hide(body); }
            BodyEditFixtureAccess::Seed(body); ok=state(0);
            for (int i=0;i<n;++i) { EngineBody::SetPartModel(body, i,a); EngineBody::SetPartModel(body, i,a); EngineBody::SetPartModel(body, i,b); BMAT34 m; BrMatrix34Identity(&m); m.m[3][1]=BR_SCALAR(i+3); EngineBody::SetPartMatrix(body, i,&m); }
            BMAT34 root; BrMatrix34Identity(&root); root.m[3][0]=BR_SCALAR(9); EngineBody::LocateOrient(body, BR_SCALAR(2),BR_SCALAR(-3),BR_SCALAR(4),&root);
            EngineBody::Hilite(body); EngineBody::Hilite(body); EngineBody::Unhilite(body); ok=state(1) && ok;
            ok=costume.FGet(body) && state(2) && ok;
            for (int i=0;i<EngineBody::SetCount(body);++i) { EngineBody::SetPartSetMtrl(body, i,mb); EngineBody::SetPartSetMtrl(body, i,mb); }
            ok=state(3) && ok;
            if (accessory) { if (EngineBody::SetCount(body)) { EngineBody::SetPartSetCmtl(body, ca); EngineBody::SetPartSetCmtl(body, ca); } if (EngineBody::SetCount(body)>1) { EngineBody::SetPartSetCmtl(body, cb); EngineBody::SetPartSetCmtl(body, cb); } }
            ok=state(4) && ok;
            for (int i=0;i<EngineBody::SetCount(body);++i)
            {
                bool generic=false; PMaterial_MTRL material=reinterpret_cast<PMaterial_MTRL>(0x1357); PCustomMaterial_CMTL custom=nullptr;
                EngineBody::GetPartSetMaterial(body, i,&generic,&material,&custom);
                LegacyLong identity=generic?(material==ma?1:material==mb?2:0):(custom==ca?3:custom==cb?4:0);
                ok=identity!=0 && record(&identity,sizeof(identity)) &&
                    (generic?record(&custom,sizeof(custom)):record(&material,sizeof(material))) && ok;
            }
#ifdef DEBUG
            if (failure) { vmutxMem.Enter(); vdmglob.dmaglPv.cactDo=failure-1; vdmglob.dmaglPv.cactFail=1; vmutxMem.Leave(); }
#endif
            bool captured=costume.FGet(body);
#ifdef DEBUG
            vmutxMem.Enter(); vdmglob.dmaglPv.cactFail=0; vmutxMem.Leave();
#endif
            if (captured) ++captures; else ++failed;
            LegacyLong result=captured; ok=record(&result,sizeof(result)) && state(5) && ok;
            if (captured) { costume.Set(body); ok=state(6) && ok; }
            ok=EngineBody::ChangeShape(body, newParents,newSets) && ok;
            for (int i=0;i<EngineBody::SetCount(body);++i) EngineBody::SetPartSetMtrl(body, i,ma);
            if (captured) costume.Set(body,true);
            ok=state(7) && ok;
            EngineBody::Hide(body); EngineBody::Hide(body); EngineBody::Show(body); EngineBody::Show(body); ok=state(8) && ok;
        }
        mm_body_owner_release(body);
        ok=ok && host.root->children==nullptr && mm_model_owner_references(a)==3 && mm_model_owner_references(b)==3 && mm_material_owner_references(ma)==7 && mm_material_owner_references(mb)==7 && mm_material_owner_references(ca)==1 && mm_material_owner_references(cb)==1;
        ReleasePpo(&parents); ReleasePpo(&sets); ReleasePpo(&newParents); ReleasePpo(&newSets); BrActorFree(host.root);
#ifdef DEBUG
        ok=ok && vdmglob.dmaglBase.cv==base && vdmglob.dmaglHq.cv==hq && vdmglob.dmaglPv.cv==pv;
#endif
        vpers->Clear();
        if (!ok) std::fprintf(stderr,"Body runtime failed: %d %d %d %d\n",shape,visibility,accessory,failure);
    }
    std::fprintf(stderr,"[body runtime] captures=%d failures=%d\n",captures,failed);
#ifdef DEBUG
    ok=ok && captures==72 && failed==18;
#else
    ok=ok && captures==90 && failed==0;
#endif
    mm_material_owner_release(ca); mm_material_owner_release(cb); mm_model_owner_release(a); mm_model_owner_release(b); mm_material_owner_release(ma); mm_material_owner_release(mb);
    if (snapshot) ok=std::fclose(snapshot)==0 && ok; return ok && captures>0;
}
#endif
