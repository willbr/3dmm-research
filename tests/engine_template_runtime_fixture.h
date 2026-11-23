#ifndef ENGINE_TEMPLATE_RUNTIME_FIXTURE_H
#define ENGINE_TEMPLATE_RUNTIME_FIXTURE_H
#include "engine_action_tables_fixture.h"
struct TemplateRuntimeFixture
{
    MMActionResource *action=nullptr;
    PModel models[2]={};
    int mode=0,trace[64]={},used=0;
    PTemplate Create(int parts)
    {
        auto t = mm_template_owner_create(mm_kauai_template_host(), 0);
        if (!t) return nullptr;
        *mm_template_owner_provider(t) = {
            this,
            [](void *p, MMI32 id) { return static_cast<TemplateRuntimeFixture *>(p)->FetchAction(id); },
            [](void *p, MMU32 id) -> void * { return static_cast<TemplateRuntimeFixture *>(p)->FetchModel(id); },
            nullptr
        };
        auto &state = *mm_template_owner_state(t);
        state.action_count=1; state.set_count=parts?2:0;
        const auto allocator = CollectionAllocator();
        state.parents=mm_part_indices_new(&allocator); state.sets=mm_part_indices_new(&allocator);
        state.costumes=mm_template_costumes_new(&allocator);
        bool ok=state.parents && state.sets && state.costumes;
        for (int i=0;ok && i<parts;++i) { short p=i?static_cast<short>((i-1)/2):-1,s=static_cast<short>(i%2); ok=mm_part_indices_append(state.parents,p) && mm_part_indices_append(state.sets,s); }
        for (int i=0;ok && i<state.set_count;++i) { int32_t id=i,count=1; ok=mm_template_costumes_append(state.costumes,&id,count); }
        if (!ok) { mm_template_owner_release(t); return nullptr; } return t;
    }
    MMActionResource *FetchAction(MMI32 id)
    { trace[used++]=100+id; if (mode==1) return nullptr; mm_action_resource_retain(action); return action; }
    PModel FetchModel(MMU32 id)
    { trace[used++]=200+id; if (mode==2 && id==1) return nullptr; mm_model_owner_retain(models[id]); return models[id]; }
};
static bool TestTemplateRuntime()
{
    FILE *snapshot=nullptr; if (const char *path=std::getenv("MM_TEMPLATE_RUNTIME_SNAPSHOT")) if (!(snapshot=std::fopen(path,"wb"))) return false;
    auto record=[&](const void *p,size_t n) { return !snapshot || std::fwrite(p,1,n,snapshot)==n; };
    BRV vertices[3]={}; BRF face={}; vertices[1].p.v[0]=BR_SCALAR(1); vertices[2].p.v[1]=BR_SCALAR(1); face.vertices[1]=1; face.vertices[2]=2;
    auto a = mm_model_new(3, vertices, 1, &face, mm_kauai_model_host());
    auto b = mm_model_new(3, vertices, 1, &face, mm_kauai_model_host());
    auto material = mm_brender_material_new(ivNil, ivNil);
    bool ok=a && b && material; int successes=0,failures=0;
    const int counts[]={0,2,6},cels[]={-7,-1,0,2,7};
    for (int shape=0;ok && shape<3;++shape) for (int mode=0;ok && mode<3;++mode) for (int cn=0;ok && cn<5;++cn)
    for (int kind=0;ok && kind<3;++kind) for (int failure=0;ok && failure<3;++failure)
    {
        int n=counts[shape]; TemplateRuntimeFixture fixture; auto t=fixture.Create(n); auto group=GeneralGroup::PggNew(sizeof(AnimationCel));
        auto matrices=DynamicArray::PglNew(sizeof(BMAT34)); ok=t && group && matrices;
        for (int i=0;ok && i<3;++i)
        {
            AnimationCel cel={static_cast<ChildChunkID>(i%2),BR_SCALAR(i+1)}; CelPartSpec parts[6]={};
            for (int j=0;j<n;++j) { parts[j].chidModl=j%3==0?-1:static_cast<short>((i+j)%2); parts[j].imat34=static_cast<short>(i); }
            BMAT34 matrix; BrMatrix34Identity(&matrix); matrix.m[3][0]=BR_SCALAR(i+5);
            ok=group->FAdd(n*sizeof(CelPartSpec),nullptr,parts,&cel) && matrices->FAdd(&matrix);
        }
        auto sounds=cn%2?mm_action_table_new(nullptr):nullptr;
        if (cn%2)
        {
            ok=ok && sounds && mm_action_sounds_reserve(sounds,2);
            for (int i=0;ok && i<2;++i) { MMTag sound={123+i,nullptr,kctgMsnd,static_cast<ChunkNumber>(456+i)}; ok=mm_action_sounds_append(sounds,&sound); }
        }
        auto action=ok?MakeFixtureAction(group,matrices,13,sounds):nullptr;
        if (!action) mm_action_table_dispose(sounds);
        PBody body=nullptr;
        if (t) { fixture.action=action; fixture.models[0]=a; fixture.models[1]=b; fixture.mode=mode; }
        auto parents=DynamicArray::PglNew(sizeof(short)),sets=DynamicArray::PglNew(sizeof(short));
        for (int i=0;ok && i<n;++i) { short p=i?static_cast<short>((i-1)/2):-1,s=static_cast<short>(i%2); ok=parents->FAdd(&p) && sets->FAdd(&s); }
        body=ok?CreateFixtureBody(parents,sets):nullptr; ok=ok && action && body;
        if (body) { for (int i=0;i<mm_body_owner_state(body)->shape.set_count;++i) mm_body_set_generic(body, i,material, &mm_kauai_body_renderer()->edit.runtime); for (int i=0;i<n;++i) mm_body_set_model(body, i,a, &mm_kauai_body_renderer()->edit.runtime); }
        BRS distance=BR_SCALAR(-33); bool applied=false;
        if (ok)
        {
#ifdef DEBUG
            vmutxMem.Enter(); auto allocator=kind==0?&vdmglob.dmaglBase:kind==1?&vdmglob.dmaglHq:&vdmglob.dmaglPv;
            allocator->cactDo=failure-1; allocator->cactFail=failure?1:0; vmutxMem.Leave();
#endif
            applied=mm_brender_template_set_cel(t, body,0,cels[cn],cn%2?nullptr:&distance);
#ifdef DEBUG
            vmutxMem.Enter(); allocator->cactFail=0; vmutxMem.Leave();
#endif
            if (applied) ++successes; else ++failures;
            LegacyLong values[]={shape,mode,cn,kind,failure,applied,distance,mm_model_owner_references(a),mm_model_owner_references(b),mm_action_resource_references(action),fixture.used};
            ok=record(values,sizeof(values)) && record(fixture.trace,fixture.used*sizeof(int)) && BodyEditFixtureAccess::Record(body,snapshot,nullptr) && ok;
            for (int i=0;i<n;++i) { LegacyLong identity=mm_brender_body_part(body, i)->model==mm_brender_model_handle(a)?1:mm_brender_body_part(body, i)->model==mm_brender_model_handle(b)?2:0; ok=record(&identity,sizeof(identity)) && ok; }
            LegacyLong count=-77; LegacyUlong flags=0x1357; int exists=1; MMTag tag={123,nullptr,0x12345678,456};
            MMI32 d=BR_SCALAR(-99);
            bool dOk=mm_brender_template_distance(t, 0,cels[cn],&d)!=0;
            bool countOk=mm_brender_template_cel_count(t, 0,&count)!=0;
            bool flagsOk=mm_brender_template_action_flags(t, 0,&flags)!=0;
            bool soundOk=mm_brender_template_sound(t, 0,cels[cn],&exists,&tag)!=0;
            LegacyLong queries[]={dOk,d,countOk,count,flagsOk,static_cast<LegacyLong>(flags),soundOk,exists,tag.sid,static_cast<LegacyLong>(tag.type),static_cast<LegacyLong>(tag.number)};
            ok=record(queries,sizeof(queries)) && mm_action_resource_references(action)==1 && ok;
        }
        mm_body_owner_release(body); mm_template_owner_release(t); mm_action_resource_release(action); ReleasePpo(&group); ReleasePpo(&matrices); ReleasePpo(&parents); ReleasePpo(&sets); vpers->Clear();
        ok=ok && mm_model_owner_references(a)==1 && mm_model_owner_references(b)==1 && mm_material_owner_references(material)==1;
        if (!ok) std::fprintf(stderr,"Template runtime failed: %d %d %d %d %d\n",shape,mode,cn,kind,failure);
    }
    for (int variant=0;ok && variant<5;++variant)
    {
        auto file=ChunkyFile::PcflCreateTemp(); auto cache=file?ChunkyResourceFile::PcrfNew(file,1024*1024):nullptr; ChunkNumber number=0;
        ok=cache && TemplateCreateFixtureAccess::Author(file,&number,6,0);
        auto t=ok?mm_brender_template_fetch(cache->CoreCache(), kctgTmpl,number,nullptr):nullptr;
        ChildChunkIdentification costumes[4]={}; ChunkNumber modelA=0,modelB=0; byte dummy=0;
        ok=ok && t && file->FAddPv(&dummy,1,kctgBmdl,&modelA) && file->FAddPv(&dummy,1,kctgBmdl,&modelB);
        for (int i=0;ok && i<4;++i) ok=file->FGetKidChidCtg(kctgTmpl,number,i,kctgCmtl,&costumes[i]);
        if (ok && variant)
        {
            ChildChunkID slot=variant==3?2:0;
            ok=file->FAdoptChild(kctgCmtl,costumes[1].cki.cno,kctgBmdl,modelA,slot) &&
                file->FAdoptChild(kctgCmtl,costumes[3].cki.cno,kctgBmdl,variant==2?modelB:modelA,slot);
            if (variant==4) { file->SetLoner(kctgCmtl,costumes[1].cki.cno,true); file->DeleteChild(kctgTmpl,number,kctgCmtl,costumes[1].cki.cno,1); }
        }
        for (int set=0;ok && set<2;++set)
        {
            LegacyLong accessory=-77; bool has=mm_brender_template_is_accessory(t, set),found=mm_brender_template_accessory_set(t, set,&accessory);
            LegacyLong values[]={variant,set,has,found,accessory,mm_brender_template_costume_count(t, set),mm_brender_template_costume_id(t, set,0),mm_brender_template_costume_id(t, set,1)};
            ok=record(values,sizeof(values));
        }
        const auto graph = file ? file->ReadChunkAccess() : MMChunkAccess{};
        for (int i=0;ok && i<4;++i) for (int j=0;ok && j<4;++j)
        {
            LegacyLong values[] = {variant, i, j, mm_brender_template_same_accessory(t, i, j),
                                   mm_template_has_models(&graph, kctgCmtl, costumes[i].cki.cno),
                                   mm_template_equal_models(&graph, costumes[i].cki.cno, costumes[j].cki.cno)};
            ok=record(values,sizeof(values));
        }
        String name,expectedName; expectedName.SetSzs(PszLit("Runtime action")); ChildChunkIdentification actionChild;
        ok=file->FGetKidChidCtg(kctgTmpl,number,0,kctgActn,&actionChild) && file->FSetName(kctgActn,actionChild.cki.cno,&expectedName) && ok;
        const MMStringValue nameValue = {name.Pst(), sizeof(achar)};
        LegacyLong named=t && mm_brender_template_action_name(t, 0,&nameValue) && name.FEqualSz(PszLit("Runtime action")); ok=named && record(&named,sizeof(named)) && ok;
        mm_template_owner_release(t); ReleasePpo(&cache); ReleasePpo(&file); vpers->Clear();
    }
    std::fprintf(stderr,"[template runtime] successes=%d failures=%d\n",successes,failures);
#ifdef DEBUG
    ok=ok && successes==168 && failures==237;
#else
    ok=ok && successes==189 && failures==216;
#endif
    mm_model_owner_release(a); mm_model_owner_release(b); mm_material_owner_release(material);
    if (snapshot) ok=std::fclose(snapshot)==0 && ok; return ok && successes && failures;
}
#endif
