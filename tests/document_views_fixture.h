#ifndef DOCUMENT_VIEWS_FIXTURE_H
#define DOCUMENT_VIEWS_FIXTURE_H
#pragma push_macro("size")
#undef size
struct ViewDocumentProbe : DocumentBase
{
    Filename key;
    bool named = false;
    unsigned id;
    std::vector<unsigned> *events;
    ViewDocumentProbe(unsigned n, std::vector<unsigned> *log, ViewDocumentProbe *parent = nullptr)
        : DocumentBase(parent), id(n), events(log)
    {
    }
    static long Counter(long next)
    {
        long old = _documents.untitled;
        _documents.untitled = next;
        return old;
    }
    bool FGetFni(Filename *out) override
    {
        events->push_back(100 + id);
        if (named)
            *out = key;
        return named;
    }
    void GetName(PString out) override
    {
        events->push_back(200 + id);
        DocumentBase::GetName(out);
    }
    bool Find(PDocumentDisplayGraphicsObject p, long *at)
    {
        return _FFindDdg(p, at);
    }
    bool Ready()
    {
        return _viewList.ready;
    }
};
struct BorrowedViewProbe : DocumentDisplayGraphicsObject
{
    unsigned id;
    BorrowedViewProbe(ViewDocumentProbe *doc, PGraphicsObjectBlock block, unsigned n)
        : DocumentDisplayGraphicsObject(doc, block), id(n)
    {
    }
    bool Init()
    {
        return _FInit();
    }
    void Active(bool active)
    {
        _fActive = active;
    }
};
static unsigned BorrowedViewId(PDocumentDisplayGraphicsObject p, const std::vector<BorrowedViewProbe *> &views)
{
    for (auto view : views)
        if (view == p)
            return view->id;
    return 0;
}
static bool ViewSnapshot(LiveTextTrace &t, ViewDocumentProbe *doc, const std::vector<BorrowedViewProbe *> &views)
{
    if (!t.Word(doc->Ready()) || !t.Word(doc->Cddg()) || !t.Word(doc->CactRef()) ||
        !t.Word(BorrowedViewId(doc->PddgActive(), views)))
        return false;
    for (long i = 0; i <= doc->Cddg(); ++i)
        if (!t.Word(BorrowedViewId(doc->PddgGet(i), views)))
            return false;
    for (auto p : views)
    {
        long at = -1;
        bool found = doc->Find(p, &at);
        if (!t.Word(found) || (found && !t.Word(unsigned(at))))
            return false;
    }
    return true;
}
static bool ViewName(LiveTextTrace &t, ViewDocumentProbe *doc, std::vector<unsigned> &events)
{
    String name;
    doc->GetName(&name);
    return t.Word(name.Cch()) && t.Raw(name.Prgch(), name.Cch() * sizeof(achar)) && LifeEvents(t, events);
}
static bool TestDocumentViews()
{
    LiveTextTrace t("MM_DOCUMENT_VIEWS_SNAPSHOT");
    std::vector<unsigned> events;
    auto doc = NewObj ViewDocumentProbe(1, &events);
    auto child = NewObj ViewDocumentProbe(2, &events, doc);
    if (!doc || !child)
        return false;
    long counter = ViewDocumentProbe::Counter(100);
    String path(PszLit("document-views-title.3mm"));
    if (!doc->key.FBuildFromPath(&path) || !ViewName(t, doc, events) || !ViewName(t, doc, events))
        return false;
    doc->named = true;
    if (!ViewName(t, doc, events))
        return false;
    doc->named = false;
    if (!ViewName(t, doc, events) || !ViewName(t, child, events))
        return false;
    auto before = GraphicsObject::PgobScreen();
    GraphicsObjectBlock block(0x7a100001, before, fgobNil, ginNil);
    auto root = NewObj GraphicsObject(&block);
    if (!root || root->Hwnd() != hNil)
        return false;
    std::vector<BorrowedViewProbe *> views;
    if (!ViewSnapshot(t, doc, views))
        return false;
    for (unsigned i = 1; i <= 3; ++i)
    {
        block.Set(0x7a100001 + i, root, fgobNil, ginNil);
        auto view = NewObj BorrowedViewProbe(doc, &block, i);
        if (!view || !view->Init())
            return false;
        views.push_back(view);
        if (!ViewSnapshot(t, doc, views))
            return false;
    }
    views[1]->Active(true);
    if (!ViewSnapshot(t, doc, views))
        return false;
    doc->MakeFirstDdg(views[1]);
    if (!ViewSnapshot(t, doc, views))
        return false;
    doc->MakeFirstDdg(views[2]);
    if (!ViewSnapshot(t, doc, views) || !doc->FAddDdg(views[1]) || !ViewSnapshot(t, doc, views))
        return false;
    doc->RemoveDdg(views[1]);
    if (!ViewSnapshot(t, doc, views))
        return false;
    doc->RemoveDdg(nullptr);
    doc->RemoveDdg(views[2]);
    if (!ViewSnapshot(t, doc, views) || !doc->FAddDdg(views[2]) || !ViewSnapshot(t, doc, views))
        return false;
    // In-memory GOBs only: no windows, activation or drawing is created here.
    doc->ActivateDmd();
    doc->UpdateName();
    if (!LifeEvents(t, events))
        return false;
    while (!views.empty())
    {
        auto p = views.back();
        views.pop_back();
        p->Release();
        if (!ViewSnapshot(t, doc, views))
            return false;
    }
    root->Release();
    if (GraphicsObject::PgobScreen() != before)
        return false;
    ViewDocumentProbe::Counter(counter);
    ReleasePpo(&child);
    ReleasePpo(&doc);
    bool golden = true;
#ifdef _WIN64
    golden = t.bytes == 808 && t.hash == 0x48EF6639U;
#endif
    return t.Finish("Document views", false) && golden;
}
#pragma pop_macro("size")
#endif
