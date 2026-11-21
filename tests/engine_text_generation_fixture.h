#ifndef ENGINE_TEXT_GENERATION_FIXTURE_H
#define ENGINE_TEXT_GENERATION_FIXTURE_H
struct TextGenerationFontContext { PChunkyResourceFile cache; bool fail; int fetches; };
static MMFontOwner *TextGenerationFont(void *p,const TAG &)
{
    auto c=static_cast<TextGenerationFontContext *>(p);
    ++c->fetches;
    return c->fail?nullptr:static_cast<MMFontOwner *>(mm_resource_cache_fetch_owned_chunk(
        c->cache->CoreCache(),kctgTdf,0,EngineFont::Read,mm_font_owner_services()));
}
class TextGenerationFixtureAccess : public ThreeDText
{
  public:
    static void *Build(PThreeDText t,int kind,int action)
    {
        if (kind==2) { auto member=&TextGenerationFixtureAccess::_FetchAction; return (t->*member)(action); }
        auto services=_GenerationServices();
        return kind==3 ? mm_text_build_matrices(t,action,&services) : mm_text_build_cels(t,action,&services);
    }
    static bool Record(PThreeDText t,FILE *out)
    {
        if (!t) return true;
        auto record=[&](const void *p,size_t n) { return !out || std::fwrite(p,1,n,out)==n; };
        auto field=&TextGenerationFixtureAccess::_generated; auto &state=t->*field;
        auto material=static_cast<PMaterial_MTRL>(state.material); auto action=state.action;
        String text; LegacyLong shape; TAG font; t->GetInfo(&text,&shape,&font);
        LegacyLong fields[]={shape,text.Cch(),font.sid,static_cast<LegacyLong>(font.ctg),static_cast<LegacyLong>(font.cno),font.pcrf!=nullptr,state.action_id,action!=nullptr,mm_action_resource_references(action),mm_material_owner_references(material)};
        return record(fields,sizeof(fields)) && record(text.Psz(),text.Cch()) && TemplateCreateFixtureAccess::Record(t,out);
    }
    static bool Resource(void *resource,int kind,int chars,FILE *out)
    {
        if (!resource) return true;
        auto record=[&](const void *p,size_t n) { return !out || std::fwrite(p,1,n,out)==n; };
        auto action=kind==2?mm_action_resource_definition(static_cast<MMActionResource *>(resource)):nullptr;
        auto table=kind==2?nullptr:static_cast<MMActionTable *>(resource);
        MMActionDefinition view={kind==4?table:nullptr,kind==3?table:nullptr,nullptr,0};
        LegacyLong count=action?mm_action_cel_count(action):static_cast<LegacyLong>(table->header.count);
        if (!record(&count,sizeof(count))) return false;
        for (int i=0;i<count;++i)
        {
            if (kind==3) { BMAT34 m; if (!mm_action_get_matrix(&view,i,&m) || !record(&m,sizeof(m))) return false; }
            else
            {
                AnimationCel cel;
                if (!mm_action_get_cel(action?action:&view,i,&cel)) return false;
                if (!record(&cel,sizeof(cel))) return false;
                for (int j=0;j<chars;++j) { CelPartSpec part;
                    if (!mm_action_get_part(action?action:&view,i,j,&part)) return false;
                    if (!record(&part,sizeof(part))) return false;
                    if (action) { BMAT34 m; if (!mm_action_get_matrix(action,part.imat34,&m) || !record(&m,sizeof(m))) return false; } }
            }
        }
        return true;
    }
};
static bool TestTextGeneration()
{
    FILE *snapshot=nullptr; if (const char *path=std::getenv("MM_TEXT_GENERATION_SNAPSHOT")) if (!(snapshot=std::fopen(path,"wb"))) return false;
    auto record=[&](const void *p,size_t n) { return !snapshot || std::fwrite(p,1,n,snapshot)==n; };
    auto file=ChunkyFile::PcflCreateTemp(); auto cache=file?ChunkyResourceFile::PcrfNew(file,4*1024*1024):nullptr;
    std::vector<byte> wire(12+256*8); int16_t order=kboCur,system=koskCur; int32_t count=256; BRS maximum=BR_SCALAR(4);
    std::memcpy(wire.data(),&order,2); std::memcpy(wire.data()+2,&system,2); std::memcpy(wire.data()+4,&count,4); std::memcpy(wire.data()+8,&maximum,4);
    for (int i=0;i<256;++i) { BRS width=BR_SCALAR(i%4+1)+(i%2),height=BR_SCALAR(i%3+1)+(i%5); std::memcpy(wire.data()+12+i*4,&width,4); std::memcpy(wire.data()+12+256*4+i*4,&height,4); }
    BRV vertices[3]={}; BRF face={}; vertices[1].p.v[0]=BR_SCALAR(1); vertices[2].p.v[1]=BR_SCALAR(2); face.vertices[1]=1; face.vertices[2]=2;
    auto model=EngineModel::New(3,vertices,1,&face); ChunkNumber number=0,modelNumber=0;
    bool ok=cache && model && file->FAddPv(wire.data(),static_cast<LegacyLong>((wire.size)()),kctgTdf,&number) && number==0 && file->FAdd(0,kctgBmdl,&modelNumber) && EngineModel::Write(model, file,kctgBmdl,modelNumber);
    for (int i=0;ok && i<256;++i) ok=file->FAdoptChild(kctgTdf,0,kctgBmdl,modelNumber,i);
    mm_model_owner_release(model); TAG font={ksidUseCrf,cache,kctgTdf,0}; TextGenerationFontContext context={cache,false,0};
    const char *strings[]={"","A","AiB","\xe9 A","ABBAi"}; int generated=0,attempts=0,successful=0;
    for (int text=0;ok && text<5;++text) for (int shape=0;ok && shape<tdtsLim;++shape)
    {
        String string; string.SetSzs(const_cast<char *>(strings[text])); auto t=ThreeDText::PtdtNew(&string,shape,&font); auto body=t?t->PbodyCreate():nullptr; ok=t && body;
        if (t) t->SetFontResolver(&context,TextGenerationFont);
        String normalized; if (t) t->GetInfo(&normalized,nullptr,nullptr);
        for (int action=0;ok && action<tdaLim;++action)
        {
            auto a=static_cast<MMActionResource *>(TextGenerationFixtureAccess::Build(t,2,action)); auto again=static_cast<MMActionResource *>(TextGenerationFixtureAccess::Build(t,2,action));
            ok=a && again==a && mm_action_resource_references(a)==3; mm_action_resource_release(again); if (!ok) { mm_action_resource_release(a); break; } ++generated;
            const auto definition=mm_action_resource_definition(a);
            const auto count=mm_action_cel_count(definition);
            LegacyLong fields[]={text,shape,action,normalized.Cch(),count,static_cast<LegacyLong>(definition->flags)};
            ok=record(fields,sizeof(fields)) && TextGenerationFixtureAccess::Resource(a,2,normalized.Cch(),snapshot) && TextGenerationFixtureAccess::Record(t,snapshot) && ok;
            const int positions[]={0,count/2,count-1,-1};
            for (auto cel:positions) { BRS distance=-77; ok=t->FSetActnCel(body,action,cel,&distance) && ok; LegacyLong values[]={cel,distance}; ok=record(values,sizeof(values)) && BodyEditFixtureAccess::Record(body,snapshot,nullptr) && ok; }
            mm_action_resource_release(a);
        }
        ReleasePpo(&body); ReleasePpo(&t); vpers->Clear();
    }
    for (int text=0;ok && text<2;++text) for (int operation=0;ok && operation<5;++operation)
    for (int missing=0;ok && missing<2;++missing) for (int kind=0;ok && kind<3;++kind) for (int failure=0;ok && failure<24;++failure)
    {
        if (missing && operation!=2 && operation!=3) continue;
#ifdef DEBUG
        const LegacyLong base=vdmglob.dmaglBase.cv,hq=vdmglob.dmaglHq.cv,pv=vdmglob.dmaglPv.cv;
#endif
        String string,changed; string.SetSzs(text?"i A":""); changed.SetSzs(text?"":"Bie "); context.fail=false; context.fetches=0;
        auto t=operation?ThreeDText::PtdtNew(&string,tdtsNormal,&font):nullptr; PBody body=nullptr; MMActionResource *held=nullptr;
        if (t) { t->SetFontResolver(&context,TextGenerationFont); body=t->PbodyCreate(); held=static_cast<MMActionResource *>(TextGenerationFixtureAccess::Build(t,2,tdaPulse)); ok=body && held && ok; }
        context.fail=missing!=0; context.fetches=0; void *result=nullptr; bool success=false;
#ifdef DEBUG
        vmutxMem.Enter(); auto allocator=kind==0?&vdmglob.dmaglBase:kind==1?&vdmglob.dmaglHq:&vdmglob.dmaglPv;
        allocator->cactDo=failure-1; allocator->cactFail=failure?1:0; vmutxMem.Leave();
#endif
        if (!operation) { t=ThreeDText::PtdtNew(&string,tdtsCircleZ,&font); success=t!=nullptr; }
        else if (operation==1) success=t->FChange(&changed,tdtsCircleZ,&font);
        else { result=TextGenerationFixtureAccess::Build(t,operation,tdaStretch); success=result!=nullptr; }
#ifdef DEBUG
        vmutxMem.Enter(); allocator->cactFail=0; vmutxMem.Leave();
#endif
        ++attempts; if (success) ++successful;
        if (!operation && t) { t->SetFontResolver(&context,TextGenerationFont); body=t->PbodyCreate(); ok=body && ok; }
        if (operation==1 && success) { ok=t->FAdjustBody(body) && t->FSetDefaultCost(body) && t->FSetActnCel(body,tdaWave,0) && ok; }
        String normalized; if (t) t->GetInfo(&normalized,nullptr,nullptr);
        LegacyLong values[]={text,operation,missing,kind,failure,success,context.fetches,mm_action_resource_references(held)};
        ok=record(values,sizeof(values)) && TextGenerationFixtureAccess::Record(t,snapshot) && BodyEditFixtureAccess::Record(body,snapshot,nullptr) && ok;
        if (result) ok=TextGenerationFixtureAccess::Resource(result,operation,normalized.Cch(),snapshot) && ok;
        if (result) { if (operation==2) mm_action_resource_release(static_cast<MMActionResource *>(result)); else mm_action_table_dispose(static_cast<MMActionTable *>(result)); }
        mm_action_resource_release(held); ReleasePpo(&body); ReleasePpo(&t); vpers->Clear();
#ifdef DEBUG
        ok=ok && vdmglob.dmaglBase.cv==base && vdmglob.dmaglHq.cv==hq && vdmglob.dmaglPv.cv==pv;
#endif
        if (!ok) std::fprintf(stderr,"Text generation failed: %d %d %d %d %d\n",text,operation,missing,kind,failure);
    }
    ReleasePpo(&cache); ReleasePpo(&file);
    std::fprintf(stderr,"[text generation] generated=%d attempts=%d successful=%d\n",generated,attempts,successful);
#ifdef DEBUG
    ok=ok && successful==690;
#else
    ok=ok && successful==720;
#endif
    if (snapshot) ok=std::fclose(snapshot)==0 && ok; return ok && generated==800 && attempts==1008 && successful;
}
#endif
