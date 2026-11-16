#ifndef TEXT_UNDO_FIXTURE_H
#define TEXT_UNDO_FIXTURE_H
#pragma push_macro("size")
#undef size
struct UndoProbe : PropertyProbe
{
    std::vector<unsigned> events;
    void SuspendUndo() override
    {
        events.push_back(1);
        TextDocumentBase::SuspendUndo();
    }
    void ResumeUndo() override
    {
        events.push_back(2);
        TextDocumentBase::ResumeUndo();
    }
    void HideSel() override
    {
        events.push_back(3);
    }
    void ShowSel() override
    {
        events.push_back(4);
    }
    void SetSel(long a, long b, long mode = kginDraw) override
    {
        events.push_back(5);
        events.push_back(unsigned(a));
        events.push_back(unsigned(b));
        events.push_back(unsigned(mode));
    }
    bool Snapshot(LiveTextTrace &t)
    {
        bool ok = t.Word(_textUndo.suspended) && t.Word(_textUndo.combine) && t.Word(_textUndo.pending != nullptr) &&
                  t.Word(CundbUndo()) && t.Word(CundbRedo()) && t.Word(FDirty()) && t.Word(unsigned(events.size())) &&
                  t.Raw(events.data(), events.size() * 4) && State(t);
        events.clear();
        return ok;
    }
    bool Replay(LiveTextTrace &t)
    {
        return FUndo() && Snapshot(t) && FRedo() && Snapshot(t);
    }
    bool Edits(LiveTextTrace &t)
    {
        static achar letters[] = {'X', 'Y', 'Z'};
        for (long i = 0; i < 3; ++i)
            if (!FReplaceRgch(letters + i, 1, i + 1, 0, fdocNil) || !Snapshot(t))
                return false;
        if (!Replay(t))
            return false;
        BumpCombineUndo();
        for (int i = 0; i < 2; ++i)
            if (!FReplaceRgch(nullptr, 0, 4, 1, fdocNil) || !Snapshot(t))
                return false;
        if (!Replay(t))
            return false;
        BumpCombineUndo();
        for (long cp : {3L, 2L})
            if (!FReplaceRgch(nullptr, 0, cp, 1, fdocNil) || !Snapshot(t))
                return false;
        if (!Replay(t))
            return false;
        BumpCombineUndo();
        static byte object[] = {1, 0, 3, 7};
        if (!FInsertObject(object, sizeof(object), 1, 0, nullptr, fdocNil) || !Snapshot(t) || !Replay(t) || !FUndo() ||
            !Snapshot(t))
            return false;
        // A new edit after undo discards redo, even when adjacent typing combines.
        if (!FReplaceRgch(letters, 1, 1, 0, fdocNil) || !Snapshot(t) || !Replay(t))
            return false;
        ClearUndo();
        if (!FSetUndo(2, 2, 0) || !Snapshot(t))
            return false;
        CommitUndo();
        SuspendUndo();
        SuspendUndo();
        BumpCombineUndo();
        if (!FSetUndo(1, 3, 2) || !Snapshot(t))
            return false;
        CancelUndo();
        ResumeUndo();
        ResumeUndo();
        if (!Snapshot(t) || !FSetUndo(1, 3, 2) || !Snapshot(t))
            return false;
        CancelUndo();
        SetCundbMax(0);
        if (!FSetUndo(1, 3, 2) || !Snapshot(t))
            return false;
        CommitUndo();
        return Snapshot(t);
    }
};
struct UndoRecordProbe : RichTextUndo
{
    bool Init(long combine, long at, long inserted, PRichTextDocument source, long first, long count)
    {
        _undo.combine = combine;
        _undo.at = at;
        _undo.inserted = inserted;
        if (!source)
            return true;
        _undo.saved = RichTextDocument::PtxrdNew();
        auto saved = static_cast<PRichTextDocument>(_undo.saved);
        if (!_undo.saved)
            return false;
        saved->SetInternal();
        saved->SuspendUndo();
        return saved->FReplaceTxrd(source, first, count, 0, 0, fdocNil);
    }
    bool Snapshot(LiveTextTrace &t)
    {
        return t.Word(_undo.combine) && t.Word(_undo.at) && t.Word(_undo.inserted) && t.Word(_undo.saved != nullptr) &&
               (!_undo.saved || DocumentStream(t, static_cast<PRichTextDocument>(_undo.saved)));
    }
};
static bool TestTextUndo()
{
    LiveTextTrace t("MM_TEXT_UNDO_SNAPSHOT");
    auto document = NewObj UndoProbe;
    if (!document || !document->Init() || !document->Snapshot(t) || !document->Edits(t))
        return false;
    ReleasePpo(&document);
    auto source = NewObj PropertyProbe;
    if (!source || !source->Init() || !source->Apply(0, 6, 1, 0x00030001))
        return false;
    // counter, position, inserted count, saved source first/count for both records
    const long cases[][10] = {{1, 2, 2, 0, -1, 1, 4, 3, 0, -1}, {1, 2, 2, 0, -1, 2, 4, 3, 0, -1},
                              {1, 2, 2, 0, -1, 1, 3, 1, 0, -1}, {1, 2, 0, 0, 2, 1, 2, 0, 3, 2},
                              {1, 4, 0, 3, 2, 1, 2, 0, 0, 2},   {1, 4, 0, 3, 2, 1, 1, 0, 0, 2},
                              {1, 2, 1, 0, 2, 1, 2, 0, 3, 2}};
    for (const auto &c : cases)
    {
        auto a = NewObj UndoRecordProbe;
        auto b = NewObj UndoRecordProbe;
        if (!a || !b || !a->Init(c[0], c[1], c[2], c[4] < 0 ? nullptr : source, c[3], c[4]) ||
            !b->Init(c[5], c[6], c[7], c[9] < 0 ? nullptr : source, c[8], c[9]) || !a->Snapshot(t) || !b->Snapshot(t))
            return false;
        bool ok = a->FCombine(b);
        if (!t.Word(ok) || !a->Snapshot(t) || !b->Snapshot(t))
            return false;
        ReleasePpo(&a);
        ReleasePpo(&b);
    }
    ReleasePpo(&source);
    document = NewObj UndoProbe;
    if (!document || !document->Init())
        return false;
    auto record = RichTextUndo::PrtunNew(7, document, 5, 2, 1);
    static achar letter = 'R';
    document->SuspendUndo();
    if (!record || !document->FReplaceRgch(&letter, 1, 2, 3, fdocNil))
        return false;
    document->ResumeUndo();
    if (!document->Snapshot(t) || !record->FUndo(document) || !document->Snapshot(t) || !record->FDo(document) ||
        !document->Snapshot(t))
        return false;
    ReleasePpo(&record);
    ReleasePpo(&document);
    bool golden = true;
#ifdef _WIN64
    golden = t.bytes == 3754 && t.hash == 0x092FF546U;
#endif
    return t.Finish("Text undo", false) && golden;
}
#pragma pop_macro("size")
#endif
