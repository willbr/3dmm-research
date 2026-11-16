#ifndef DOCUMENT_LIFECYCLE_FIXTURE_H
#define DOCUMENT_LIFECYCLE_FIXTURE_H
#pragma push_macro("size")
#undef size
struct LifeProbe : DocumentBase
{
    unsigned id;
    std::vector<unsigned> *events;
    Filename *key;
    tribool answer = tYes;
    bool saveResult = true, named = false, choose = true;
    LifeProbe(unsigned n, std::vector<unsigned> *log, Filename *name, LifeProbe *parent = nullptr, ulong flags = 0)
        : DocumentBase(parent, flags), id(n), events(log), key(name)
    {
    }
    ~LifeProbe()
    {
        events->push_back(100 + id);
    }
    tribool _TQuerySave(bool force) override
    {
        events->push_back(200 + id);
        events->push_back(force);
        return answer;
    }
    bool FSave(long = cidSave) override
    {
        events->push_back(300 + id);
        return saveResult;
    }
    bool FGetFni(Filename *out) override
    {
        events->push_back(400 + id);
        if (named)
            *out = *key;
        return named;
    }
    bool FGetFniSave(Filename *out) override
    {
        events->push_back(500 + id);
        if (choose)
            *out = *key;
        return choose;
    }
    bool FSaveToFni(Filename *name, bool adopt) override
    {
        events->push_back(600 + id);
        events->push_back(adopt);
        return name->FEqual(key);
    }
    void UpdateName() override
    {
        events->push_back(700 + id);
    }
};
static unsigned LifeId(PDocumentBase p, const std::vector<LifeProbe *> &nodes)
{
    for (auto node : nodes)
        if (p == node)
            return node->id;
    return 0;
}
static bool LifeEvents(LiveTextTrace &t, std::vector<unsigned> &events)
{
    bool ok = t.Word(unsigned(events.size())) && t.Raw(events.data(), events.size() * 4);
    events.clear();
    return ok;
}
static bool LifeLinks(LiveTextTrace &t, const std::vector<LifeProbe *> &nodes)
{
    for (auto p : nodes)
        if (!t.Word(p->id) || !t.Word(LifeId(p->PdocbPar(), nodes)) || !t.Word(LifeId(p->PdocbSib(), nodes)) ||
            !t.Word(LifeId(p->PdocbChd(), nodes)) || !t.Word(p->FInternal()) || !t.Word(p->FDirty()))
            return false;
    for (auto p = DocumentBase::PdocbFirst(); p; p = p->PdocbSib())
        if (LifeId(p, nodes) && !t.Word(LifeId(p, nodes)))
            return false;
    return t.Word(0);
}
static bool LifeTree(LiveTextTrace &t, LifeProbe *root, unsigned skip, const std::vector<LifeProbe *> &nodes)
{
    DocumentTreeEnumerator e;
    e.Init(root);
    PDocumentBase p = root;
    ulong flags = 123, input = 0;
    while (e.FNextDoc(&p, &flags, input))
    {
        unsigned id = LifeId(p, nodes);
        if (!t.Word(id) || !t.Word(flags))
            return false;
        input = id == skip ? fdteSkipToSib : fdteNil;
    }
    if (!t.Word(flags) || !t.Word(LifeId(p, nodes)))
        return false;
    p = root;
    return !e.FNextDoc(&p, &flags) && p == root && flags == 0;
}
static bool TestDocumentLifecycle()
{
    LiveTextTrace t("MM_DOCUMENT_LIFECYCLE_SNAPSHOT");
    std::vector<unsigned> events;
    Filename key;
    if (!key.FGetTemp())
        return false;
    auto before = DocumentBase::PdocbFirst();
    auto a = NewObj LifeProbe(1, &events, &key);
    auto b = NewObj LifeProbe(2, &events, &key);
    auto c = NewObj LifeProbe(3, &events, &key, a);
    auto d = NewObj LifeProbe(4, &events, &key, c, fdocSibling);
    auto e = NewObj LifeProbe(5, &events, &key, a);
    auto f = NewObj LifeProbe(6, &events, &key, c);
    auto g = NewObj LifeProbe(7, &events, &key, a, fdocSibling);
    if (!a || !b || !c || !d || !e || !f || !g)
        return false;
    std::vector<LifeProbe *> nodes = {a, b, c, d, e, f, g};
    for (auto p : nodes)
        p->SetDirty(true);
    if (!LifeLinks(t, nodes) || !LifeTree(t, a, 0, nodes) || !LifeTree(t, a, 1, nodes) || !LifeTree(t, a, 3, nodes) ||
        !LifeTree(t, f, 0, nodes) || !LifeTree(t, nullptr, 0, nodes))
        return false;
    for (auto answer : {tNo, tYes, tMaybe})
        for (ulong flags : {0UL, ulong(fdocForceClose), ulong(fdocAssumeYes), ulong(fdocForceClose | fdocAssumeYes)})
        {
            c->answer = answer;
            f->saveResult = answer != tNo;
            if (!t.Word(a->FQueryClose(flags)) || !LifeEvents(t, events))
                return false;
        }
    f->SetInternal();
    c->answer = tYes;
    f->saveResult = true;
    if (!LifeLinks(t, nodes) || !t.Word(a->FQueryClose(0)) || !LifeEvents(t, events))
        return false;
    for (long command : {long(cidSave), long(cidSaveAs), long(cidSaveCopy)})
        for (int n = 0; n < 3; ++n)
        {
            b->named = n == 0;
            b->choose = n != 2;
            if (!t.Word(b->DocumentBase::FSave(command)) || !LifeEvents(t, events))
                return false;
        }
    a->named = b->named = g->named = true;
    if (!t.Word(LifeId(DocumentBase::PdocbFromFni(&key), nodes)) || !LifeEvents(t, events))
        return false;
    b->named = false;
    if (!t.Word(LifeId(DocumentBase::PdocbFromFni(&key), nodes)) || !LifeEvents(t, events))
        return false;
    const unsigned order[] = {6, 3, 4, 5, 7, 1, 2};
    for (unsigned id : order)
    {
        auto it = std::find_if(nodes.begin(), nodes.end(), [=](LifeProbe *p) { return p->id == id; });
        auto p = *it;
        nodes.erase(it);
        p->Release();
        if (!LifeLinks(t, nodes) || !LifeEvents(t, events))
            return false;
    }
    if (DocumentBase::PdocbFirst() != before)
        return false;
    bool golden = true;
#ifdef _WIN64
    golden = t.bytes == 1900 && t.hash == 0xFE5F544AU;
#endif
    return t.Finish("Document lifecycle", false) && golden;
}
#pragma pop_macro("size")
#endif
