#ifndef ENGINE_TEMPLATE_CREATE_FIXTURE_H
#define ENGINE_TEMPLATE_CREATE_FIXTURE_H
class TemplateCreateFixtureAccess : public Template
{
  public:
    static bool Author(PChunkyFile file,ChunkNumber *number,int parts,int variant)
    {
        auto t=NewObj TemplateCreateFixtureAccess;
        if (!t) return false;
        t->_xaRest=0x1234; t->_yaRest=0xabcd; t->_zaRest=0x8000; t->_grftmpl=ftmplProp;
        t->_stn.SetSzs(PszLit("Template construction fixture")); t->_cbset=parts?2:0; t->_ccmid=parts?4:0; t->_cactn=1;
        t->_pglibactPar=DynamicArray::PglNew(sizeof(short)); t->_pglibset=DynamicArray::PglNew(sizeof(short));
        t->_pggcmid=GeneralGroup::PggNew(sizeof(int32_t));
        bool ok=t->_pglibactPar && t->_pglibset && t->_pggcmid;
        for (int i=0;ok && i<parts;++i) { short parent=i?static_cast<short>((i-1)/2):-1,set=static_cast<short>(i%2); ok=t->_pglibactPar->FAdd(&parent) && t->_pglibset->FAdd(&set); }
        for (int i=0;ok && i<t->_cbset;++i) { int32_t ids[]={i,i+2},count=2; ok=t->_pggcmid->FAdd(sizeof(ids),nullptr,ids,&count); }
        ok=ok && t->_FWriteTmplf(file,kctgTmpl,number);
        TemplateOnFile header={}; DataBlock block;
        ok=ok && file->FFind(kctgTmpl,*number,&block) && block.FReadRgb(&header,sizeof(header),0);
        header.swPad=0; if (variant==1) SwapBytesBom(&header,kbomTmplf);
        ok=ok && file->FPutPv(&header,variant==5?3:sizeof(header),kctgTmpl,*number);
        if (variant==6 && ok) { String empty; ok=file->FSetName(kctgTmpl,*number,&empty); }
        const bool other=variant==1 && parts;
        if (other)
        {
            SwapBytesRgsw(t->_pglibactPar->QvGet(0),parts); SwapBytesRgsw(t->_pglibset->QvGet(0),parts);
            for (int i=0;i<t->_cbset;++i) { SwapBytesRglw(t->_pggcmid->QvGet(i),2); SwapBytesRglw(t->_pggcmid->QvFixedGet(i),1); }
        }
        auto table=[&](ChunkTagOrType type,auto *data) { ChunkNumber child; DataBlock b;
            return file->FAddChild(kctgTmpl,*number,0,data->CbOnFile(),type,&child,&b) && data->FWrite(&b,other?kboOther:kboCur); };
        if (ok && variant!=3) ok=table(kctgGlpi,t->_pglibactPar);
        if (ok && variant!=4) ok=table(kctgGlbs,t->_pglibset);
        if (ok && variant!=2) ok=table(kctgGgcm,t->_pggcmid);
        byte action=0; ChunkNumber child;
        ok=ok && file->FAddChildPv(kctgTmpl,*number,0,&action,1,kctgActn,&child) && file->FAddChildPv(kctgTmpl,*number,2,&action,1,kctgActn,&child);
        auto material=Material_MTRL::PmtrlNew(); ChunkNumber materialNumber=0;
        ok=ok && material && material->FWrite(file,kctgMtrl,&materialNumber); ReleasePpo(&material);
        for (int i=0;ok && i<(parts?4:0);++i)
        {
            CustomMaterialOnFile costume={kboCur,koskCur,i%2};
            ok=file->FAddChildPv(kctgTmpl,*number,i,&costume,sizeof(costume),kctgCmtl,&child);
            for (int j=0;ok && j<3;++j) ok=file->FAdoptChild(kctgCmtl,child,kctgMtrl,materialNumber,j);
        }
        // Restore payloads before the author's debug validation/destruction.
        if (other) { SwapBytesRgsw(t->_pglibactPar->QvGet(0),parts); SwapBytesRgsw(t->_pglibset->QvGet(0),parts);
            for (int i=0;i<t->_cbset;++i) { SwapBytesRglw(t->_pggcmid->QvGet(i),2); SwapBytesRglw(t->_pggcmid->QvFixedGet(i),1); } }
        ReleasePpo(&t); return ok;
    }
    static bool Write(PTemplate t,PChunkyFile file,ChunkNumber *number)
    { auto member=&TemplateCreateFixtureAccess::_FWriteTmplf; return (t->*member)(file,kctgTmpl,number); }
    static bool Record(PTemplate t,FILE *out)
    {
        auto record=[&](const void *p,size_t n) { return !out || std::fwrite(p,1,n,out)==n; };
        if (!t) return true;
        auto sets=&TemplateCreateFixtureAccess::_cbset,customs=&TemplateCreateFixtureAccess::_ccmid;
        auto parents=&TemplateCreateFixtureAccess::_pglibactPar,partsets=&TemplateCreateFixtureAccess::_pglibset;
        BRA x,y,z; t->GetRestOrien(&x,&y,&z); String name; t->GetName(&name);
        LegacyLong values[]={t->*sets,t->*customs,t->Cactn(),x,y,z,t->FIsProp(),name.FEqualSz(PszLit("Template construction fixture"))};
        if (!record(values,sizeof(values))) return false;
        for (int i=0;i<(t->*parents)->IvMac();++i) { short p,s; (t->*parents)->Get(i,&p); (t->*partsets)->Get(i,&s); if (!record(&p,sizeof(p)) || !record(&s,sizeof(s))) return false; }
        for (int i=0;i<t->*sets;++i) { LegacyLong n=t->CcmidOfBset(i); if (!record(&n,sizeof(n))) return false; for (int j=0;j<n;++j) { LegacyLong id=t->CmidOfBset(i,j); if (!record(&id,sizeof(id))) return false; } }
        return true;
    }
};
static bool TestTemplateConstruction()
{
    FILE *snapshot=nullptr; if (const char *path=std::getenv("THREE_MM_TEMPLATE_CREATE_SNAPSHOT")) if (!(snapshot=std::fopen(path,"wb"))) return false;
    auto record=[&](const void *p,size_t n) { return !snapshot || std::fwrite(p,1,n,snapshot)==n; };
    bool ok=true; int loaded=0,bodies=0;
    for (int shape=0;ok && shape<2;++shape) for (int variant=0;ok && variant<8;++variant)
    for (int stage=0;ok && stage<2;++stage) for (int kind=0;ok && kind<3;++kind) for (int failure=0;ok && failure<14;++failure)
    {
        auto file=ChunkyFile::PcflCreateTemp(); auto cache=file?ChunkyResourceFile::PcrfNew(file,4*1024*1024):nullptr; ChunkNumber number=0;
        ok=cache && TemplateCreateFixtureAccess::Author(file,&number,shape?6:0,variant);
        DataBlock block; LegacyLong estimate=-1;
        ok=ok && file->FFind(kctgTmpl,number,&block) && Template::FReadTmpl(cache,kctgTmpl,number,&block,nullptr,&estimate);
        auto inject=[&](bool enabled) {
#ifdef DEBUG
            vmutxMem.Enter(); auto allocator=kind==0?&vdmglob.dmaglBase:kind==1?&vdmglob.dmaglHq:&vdmglob.dmaglPv;
            allocator->cactDo=failure-1; allocator->cactFail=enabled && failure?1:0; vmutxMem.Leave();
#endif
        };
        if (!stage) inject(true);
        auto t=ok?static_cast<PTemplate>(cache->PbacoFetch(kctgTmpl,number,Template::FReadTmpl)):nullptr;
        inject(false);
        PBody body=nullptr; bool written=false; TemplateOnFile wire={};
        if (t)
        {
            ++loaded; auto copy=ChunkyFile::PcflCreateTemp(); ChunkNumber copyNumber=0; DataBlock copyBlock;
            written=copy && TemplateCreateFixtureAccess::Write(t,copy,&copyNumber) && copy->FFind(kctgTmpl,copyNumber,&copyBlock) && copyBlock.FReadRgb(&wire,sizeof(wire),0);
            wire.swPad=0; // Native wrote undefined stack padding; compare defined wire fields.
            ReleasePpo(&copy); ok=ok && written;
            if (variant==7 && shape) { ChildChunkIdentification child; ok=file->FGetKidChidCtg(kctgTmpl,number,0,kctgCmtl,&child) && ok; file->DeleteChild(kctgTmpl,number,kctgCmtl,child.cki.cno,0); }
            if (stage) inject(true);
            body=t->PbodyCreate(); inject(false); if (body) ++bodies;
        }
        LegacyLong values[]={shape,variant,stage,kind,failure,estimate,t!=nullptr,body!=nullptr,written};
        ok=record(values,sizeof(values)) && TemplateCreateFixtureAccess::Record(t,snapshot) && (!written || record(&wire,sizeof(wire))) && ok;
        if (body) { ok=ok && body->Cbact()==(shape?8:2) && body->Cbset()==(shape?2:0);
            for (int i=0;i<(shape?6:0);++i) ok=ok && body->PbactPart(i)->material!=nullptr;
            ok=BodyEditFixtureAccess::Record(body,snapshot,nullptr) && ok; }
        ReleasePpo(&body); ReleasePpo(&t); ReleasePpo(&cache); ReleasePpo(&file); vpers->Clear();
        if (!ok) std::fprintf(stderr,"Template construction failed: %d %d %d %d %d\n",shape,variant,stage,kind,failure);
    }
    std::fprintf(stderr,"[template construction] loaded=%d bodies=%d\n",loaded,bodies);
#ifdef DEBUG
    ok=ok && loaded==613 && bodies==478;
#else
    ok=ok && loaded==672 && bodies==588;
#endif
    if (snapshot) ok=std::fclose(snapshot)==0 && ok; return ok && loaded>0 && bodies>0;
}
#endif
