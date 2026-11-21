#ifndef TEXT_ACCESS_FIXTURE_H
#define TEXT_ACCESS_FIXTURE_H
#pragma push_macro("size")
#undef size
struct AccessProbe : PropertyProbe
{
    unsigned cacheCalls = 0, characterCalls = 0, invalidations = 0, notifications = 0;
    void _CacheRange(long first, long end) override
    {
        ++cacheCalls;
        TextDocumentBase::_CacheRange(first, end);
    }
    achar _ChFetch(long cp) override
    {
        ++characterCalls;
        return TextDocumentBase::_ChFetch(cp);
    }
    void _InvalCache(long cp, long ins, long del) override
    {
        ++invalidations;
        TextDocumentBase::_InvalCache(cp, ins, del);
    }
    void InvalAllDdg(long cp, long ins, long del, ulong flags = fdocUpdate) override
    {
        ++notifications;
        TextDocumentBase::InvalAllDdg(cp, ins, del, flags);
    }
    bool InitText(long count = 1800)
    {
        const unsigned char seed[] = {'A', 'b', ' ', 'C', '\r', '\n', '\n', 'D', '\t', 'E', 'f', 0, 0xc0, 0xe0};
        std::vector<achar> text(count);
        for (long i = 0; i < count; ++i)
            text[i] = achar(seed[i % sizeof(seed)]);
        text.back() = '\r';
        return Init() && _pbsf->FReplace(text.data(), count * sizeof(achar), 0, _pbsf->IbMac());
    }
    bool Snapshot(LiveTextTrace &t)
    {
        return t.Word(_textCache.first) && t.Word(_textCache.end) &&
               t.Raw(_textCache.data, (_textCache.end - _textCache.first) * sizeof(achar)) && t.Word(cacheCalls) &&
               t.Word(characterCalls) && t.Word(invalidations) && t.Word(notifications) && t.Word(FDirty()) && State(t);
    }
    bool CacheTests(LiveTextTrace &t)
    {
        const long ranges[][2] = {{0, 1},  {20, 40},     {510, 512},   {500, 700},   {1000, 1001}, {490, 491},
                                  {1, 30}, {1799, 1800}, {1600, 1750}, {1500, 1510}, {900, 1200},  {700, 1212}};
        for (const auto &r : ranges)
        {
            _CacheRange(r[0], r[1]);
            if (!Snapshot(t))
                return false;
        }
        const long firsts[] = {0, 1, 510, 850, 1200};
        for (long first : firsts)
        {
            std::vector<achar> out(600);
            FetchRgch(first, 600, out.data());
            if (!t.Raw(out.data(), out.size() * sizeof(achar)) || !Snapshot(t))
                return false;
        }
        return true;
    }
    bool Navigation(LiveTextTrace &t)
    {
        const long points[] = {0, 1, 4, 5, 6, 7, 8, 13, 14, 15, 511, 512, 513, 799, 1798, 1799, 1800};
        for (long cp : points)
        {
            if (!t.Word(FMinPara(cp)) || !t.Word(CpMinPara(cp)) || !t.Word(CpLimPara(cp)) || !t.Word(CpPrev(cp)) ||
                !t.Word(CpPrev(cp, true)) || !t.Word(CpNext(cp)) || !t.Word(CpNext(cp, true)) || !Snapshot(t))
                return false;
        }
        return t.Word(FMinPara(-1)) && t.Word(CpMinPara(-1)) && t.Word(CpLimPara(-1));
    }
    bool Searches(LiveTextTrace &t)
    {
        const std::vector<std::vector<achar>> needles = {
            {'A', 'B'}, {'C', '\r', '\n'}, {'f', 0}, {achar(0xc0)}, {'q', 'q'}, {'\r'}, {'D', '\t', 'E'}};
        for (const auto &needle : needles)
            for (long cp : {0L, 1L, 503L, 510L, 700L, 1790L, 1800L})
                for (int sensitive = 0; sensitive < 2; ++sensitive)
                {
                    auto pattern = needle;
                    long first, end;
                    bool found = FFind(pattern.data(), long(pattern.size()), cp, &first, &end, sensitive != 0);
                    if (!t.Word(found) || !t.Raw(pattern.data(), pattern.size() * sizeof(achar)) ||
                        (found && (!t.Word(first) || !t.Word(end))) || !t.Word(characterCalls) || !t.Word(cacheCalls))
                        return false;
                }
        return Snapshot(t);
    }
    bool BaseEdits(LiveTextTrace &t)
    {
        SetCundbMax(0);
        const long edits[][3] = {{0, 2, 0}, {40, 0, 5}, {300, 3, 2}, {1000, 0, 600}, {10, 4, 100}};
        for (const auto &e : edits)
        {
            _CacheRange(e[0], LwMin(e[0] + 400, CpMac()));
            std::vector<achar> inserted(e[1], 'x');
            if (!TextDocumentBase::FReplaceRgch(inserted.data(), e[1], e[0], e[2], fdocNil) || !Snapshot(t))
                return false;
        }
        auto source = NewObj FileByteStream;
        static achar text[] = {'Z', '\r', '\n', 'Y'};
        if (!source || !source->FReplace(text, sizeof(text), 0, 0) ||
            !TextDocumentBase::FReplaceBsf(source, 1, 3, 20, 4, fdocNil) || !Snapshot(t))
            return false;
        ReleasePpo(&source);
        auto file = FileObject::PfilCreateTemp();
        FileLocation location = {file, 0, sizeof(text)};
        if (!file || !location.FWriteRgb(text, sizeof(text), 0) ||
            !TextDocumentBase::FReplaceFlo(&location, true, 21, 2, koskCur, fdocNil) || !Snapshot(t))
            return false;
        ReleasePpo(&file);
        return true;
    }
    bool RichEdits(LiveTextTrace &t)
    {
        static achar inserted[] = {'X', '\r'};
        SetCundbMax(0);
        if (!FReplaceRgch(inserted, 2, 1, 3, fdocNil) || !Snapshot(t))
            return false;
        CHP chp;
        FetchChp(0, &chp);
        chp.dypFont += 3;
        chp.grfont = 3;
        if (!FReplaceRgch(inserted, 2, 2, 1, &chp, nullptr, fdocNil) || !Snapshot(t))
            return false;
        PAP pap = {1, 2, 20, 256, 3, 300, 2};
        if (!FReplaceRgch(inserted, 2, 0, 4, &chp, &pap, fdocNil) || !Snapshot(t))
            return false;
        auto source = NewObj FileByteStream;
        if (!source || !source->FReplace(inserted, sizeof(inserted), 0, 0) ||
            !FReplaceBsf(source, 0, 2, 0, 2, fdocNil) || !Snapshot(t) ||
            !FReplaceBsf(source, 0, 2, 0, 2, &chp, &pap, fdocNil) || !Snapshot(t))
            return false;
        ReleasePpo(&source);
        auto file = FileObject::PfilCreateTemp();
        FileLocation location = {file, 0, sizeof(inserted)};
        if (!file || !location.FWriteRgb(inserted, sizeof(inserted), 0) ||
            !FReplaceFlo(&location, true, 0, 2, koskCur, fdocNil) || !Snapshot(t))
            return false;
        if (sizeof(achar) == 1 && (!FReplaceFlo(&location, false, 0, 2, &chp, &pap, koskCur, fdocNil) || !Snapshot(t)))
            return false;
        ReleasePpo(&file);
        return true;
    }
    bool UndoTests(LiveTextTrace &t)
    {
        static achar text[] = {'Q', 'R'};
        std::fprintf(stderr, "[access] real undo setup\n");
        if (!FReplaceRgch(text, 2, 1, 1, fdocNil) || !t.Word(CundbUndo()) || !Snapshot(t))
            return false;
        std::fprintf(stderr, "[access] undo\n");
        if (!FUndo() || !t.Word(CundbRedo()) || !Snapshot(t))
            return false;
        std::fprintf(stderr, "[access] redo\n");
        if (!FRedo() || !Snapshot(t))
            return false;
        return true;
    }
};
static bool TestTextAccess()
{
    LiveTextTrace t("MM_TEXT_ACCESS_SNAPSHOT");
    auto p = NewObj AccessProbe;
    if (!p || !p->InitText() || !p->CacheTests(t) || !p->Navigation(t) || !p->Searches(t) || !p->BaseEdits(t))
        return false;
    ReleasePpo(&p);
    p = NewObj AccessProbe;
    if (!p || !p->InitText(40) || !p->RichEdits(t))
        return false;
    ReleasePpo(&p);
    p = NewObj AccessProbe;
    if (!p || !p->InitText(20) || !p->UndoTests(t))
        return false;
    ReleasePpo(&p);
    bool golden = true;
#ifdef _WIN64
    golden = t.bytes == 102964 && t.hash == 0xC2C720A8U;
#endif
    return t.Finish("Text access", false) && golden;
}
#pragma pop_macro("size")
#endif
