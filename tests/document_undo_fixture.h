#ifndef DOCUMENT_UNDO_FIXTURE_H
#define DOCUMENT_UNDO_FIXTURE_H
#pragma push_macro("size")
#undef size
struct DocumentUndoProbe;
struct DocumentUndoToken : UndoBase
{
    DocumentUndoProbe *document;
    unsigned id;
    bool failUndo = false, failRedo = false;
    ~DocumentUndoToken();
    bool FUndo(PDocumentBase p) override;
    bool FDo(PDocumentBase p) override;
    unsigned References()
    {
        return unsigned(_references.count);
    }
};
struct DocumentUndoProbe : DocumentBase
{
    std::vector<unsigned> events;
    std::vector<DocumentUndoToken *> tokens;
    unsigned value = 0;
    void ClearRedo() override
    {
        events.push_back(100);
        DocumentBase::ClearRedo();
    }
    void ClearUndo() override
    {
        events.push_back(101);
        DocumentBase::ClearUndo();
    }
    bool Token(unsigned id)
    {
        auto p = NewObj DocumentUndoToken;
        if (!p)
            return false;
        p->document = this;
        p->id = id;
        tokens.push_back(p);
        return true;
    }
    bool Snapshot(LiveTextTrace &t)
    {
        if (!t.Word(CundbMax()) || !t.Word(CundbUndo()) || !t.Word(CundbRedo()) || !t.Word(_undoList.ready) ||
            !t.Word(_undoList.count) || !t.Word(value))
            return false;
        if (_undoList.ready)
            for (long i = 0; i < long(_undoList.count); ++i)
            {
                auto p = static_cast<DocumentUndoToken *>(_PundbGet(i));
                if (!t.Word(p->id))
                    return false;
            }
        for (auto p : tokens)
            if (!t.Word(p->References()))
                return false;
        bool ok = t.Word(unsigned(events.size())) && t.Raw(events.data(), events.size() * 4);
        events.clear();
        return ok;
    }
    bool Finish(LiveTextTrace &t)
    {
        ClearUndo();
        if (!Snapshot(t))
            return false;
        for (auto &p : tokens)
            ReleasePpo(&p);
        tokens.clear();
        return t.Word(unsigned(events.size())) && t.Raw(events.data(), events.size() * 4);
    }
};
DocumentUndoToken::~DocumentUndoToken()
{
    document->events.push_back(300 + id);
}
bool DocumentUndoToken::FUndo(PDocumentBase p)
{
    if (p != document)
        return false;
    document->events.push_back(400 + id);
    if (failUndo)
        return false;
    document->value = id * 10;
    return true;
}
bool DocumentUndoToken::FDo(PDocumentBase p)
{
    if (p != document)
        return false;
    document->events.push_back(500 + id);
    if (failRedo)
        return false;
    document->value = id * 10 + 1;
    return true;
}
static bool TestDocumentUndo()
{
    LiveTextTrace t("MM_DOCUMENT_UNDO_SNAPSHOT");
    for (int mode = 0; mode < 3; ++mode)
    {
        auto p = NewObj DocumentUndoProbe;
        if (!p)
            return false;
        for (unsigned id = 1; id <= 6; ++id)
            if (!p->Token(id))
                return false;
        if (!p->Snapshot(t) || p->FUndo() || p->FRedo())
            return false;
        if (mode == 0)
        {
            p->SetCundbMax(0);
            if (!p->FAddUndo(p->tokens[0]) || !p->Snapshot(t))
                return false;
            p->ClearRedo();
        }
        else
        {
            p->SetCundbMax(mode == 1 ? 3 : 5);
            for (int i = 0; i < (mode == 1 ? 4 : 5); ++i)
                if (!p->FAddUndo(p->tokens[i]) || !p->Snapshot(t))
                    return false;
            for (int i = 0; i < (mode == 1 ? 2 : 3); ++i)
                if (!p->FUndo() || !p->Snapshot(t))
                    return false;
            if (mode == 1)
            {
                p->tokens[2]->failRedo = true;
                if (p->FRedo() || !p->Snapshot(t))
                    return false;
                p->tokens[2]->failRedo = false;
                if (!p->FRedo() || !p->FAddUndo(p->tokens[4]) || !p->Snapshot(t))
                    return false;
                p->tokens[4]->failUndo = true;
                if (p->FUndo() || !p->Snapshot(t))
                    return false;
                p->tokens[4]->failUndo = false;
                p->SetCundbMax(2);
                if (!p->Snapshot(t) || !p->FUndo())
                    return false;
            }
            else
                p->SetCundbMax(3);
            if (!p->Snapshot(t))
                return false;
            p->SetCundbMax(1);
            if (!p->Snapshot(t) || !p->FUndo() || !p->Snapshot(t))
                return false;
            p->SetCundbMax(0);
            if (!p->FAddUndo(p->tokens[5]) || !p->Snapshot(t))
                return false;
        }
        if (!p->Finish(t))
            return false;
        ReleasePpo(&p);
    }
    bool golden = true;
#ifdef _WIN64
    golden = t.bytes == 2160 && t.hash == 0x4BD922B5U;
#endif
    return t.Finish("Document undo", false) && golden;
}
#pragma pop_macro("size")
#endif
