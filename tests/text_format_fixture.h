#ifndef TEXT_FORMAT_FIXTURE_H
#define TEXT_FORMAT_FIXTURE_H
#pragma push_macro("size")
#undef size
struct FormatProbe : PropertyProbe
{
    std::vector<unsigned> events;
    bool failUndo = false;
    bool FSetUndo(long first, long end, long inserted) override
    {
        events.insert(events.end(), {1, unsigned(first), unsigned(end), unsigned(inserted)});
        return !failUndo;
    }
    void CancelUndo() override
    {
        events.push_back(2);
    }
    void CommitUndo() override
    {
        events.push_back(3);
    }
    bool InitFormat()
    {
        static achar text[] = {'a', 'b', '\r', '\n', 'c', 'd', '\r', 'e', '\r'};
        return Init() && _pbsf->FReplace(text, sizeof(text), 0, _pbsf->IbMac());
    }
    static bool Character(LiveTextTrace &t, const CHP &p)
    {
        return t.Word(p.grfont) && t.Word(p.onn) && t.Word(p.dypFont) && t.Word(p.dypOffset) &&
               t.Word(p.acrFore.LwGet()) && t.Word(p.acrBack.LwGet());
    }
    static bool Paragraph(LiveTextTrace &t, const PAP &p)
    {
        return t.Word(p.jc) && t.Word(p.nd) && t.Word(p.dxpTab) && t.Word(p.numLine) && t.Word(p.dypExtraLine) &&
               t.Word(p.numAfter) && t.Word(p.dypExtraAfter);
    }
    CHP Chp(int n)
    {
        CHP p;
        p.Clear();
        p.onn = _onnDef;
        p.dypFont = _dypFontDef;
        p.acrBack = kacrClear;
        if (n)
        {
            String name;
            name.SetSzs(n % 2 ? PszLit("Courier New") : PszLit("Times New Roman"));
            p.onn = vntl.OnnMapStn(&name, koskCur);
            p.grfont = unsigned(n * 3);
            p.dypFont = 8 + n * 5;
            p.dypOffset = n % 3 == 0 ? -128 : n % 3 == 1 ? 127 : 200;
            p.acrFore.SetFromLw(0x01010203 + n);
            p.acrBack = n % 2 ? kacrClear : kacrWhite;
        }
        return p;
    }
    PAP Pap(int n)
    {
        PAP p = {byte(n % 3),   byte(n % 4),         short(18 + n * 4), short(256 + n * 40),
                 short(n * -7), short(256 - n * 32), short(n * 9)};
        return p;
    }
    bool Deltas(LiveTextTrace &t)
    {
        for (int n = 0; n < 7; ++n)
        {
            CHP p = Chp(n), old = Chp((n + 1) % 7);
            PAP pap = Pap(n), oldPap = Pap((n + 2) % 7);
            for (int diff = 0; diff < 3; ++diff)
            {
                for (byte kind = 1; kind < 5; ++kind)
                {
                    LegacyLong value, mask;
                    tribool result = _TGetLwFromChp(kind, &p,
                                                    diff == 0   ? nullptr
                                                    : diff == 1 ? &p
                                                                : &old,
                                                    &value, &mask);
                    if (!t.Word(result) || !t.Word(mask) || (result == tYes && !t.Word(value)))
                        return false;
                    if (result == tYes)
                        _ReleaseSprmLw(kind, value);
                }
                for (byte kind = 128; kind < 131; ++kind)
                {
                    LegacyLong value, mask;
                    tribool result = _TGetLwFromPap(kind, &pap,
                                                    diff == 0   ? nullptr
                                                    : diff == 1 ? &pap
                                                                : &oldPap,
                                                    &value, &mask);
                    if (!t.Word(result) || !t.Word(mask) || (result == tYes && !t.Word(value)))
                        return false;
                }
                SPVM batch[4];
                long count;
                if (!_FGetRgspvmFromChp(&p,
                                        diff == 0   ? nullptr
                                        : diff == 1 ? &p
                                                    : &old,
                                        batch, &count) ||
                    !t.Word(count))
                    return false;
                for (long i = 0; i < count; ++i)
                    if (!t.Word(batch[i].sprm) || !t.Word(batch[i].lw) || !t.Word(batch[i].lwMask))
                        return false;
                _ReleaseRgspvm(batch, count);
                if (!_FGetRgspvmFromPap(&pap,
                                        diff == 0   ? nullptr
                                        : diff == 1 ? &pap
                                                    : &oldPap,
                                        batch, &count) ||
                    !t.Word(count))
                    return false;
                for (long i = 0; i < count; ++i)
                    if (!t.Word(batch[i].sprm) || !t.Word(batch[i].lw) || !t.Word(batch[i].lwMask))
                        return false;
                _ReleaseRgspvm(batch, count);
            }
        }
        return State(t);
    }
    bool Fetches(LiveTextTrace &t)
    {
        for (long cp = 0; cp < CpMac(); ++cp)
            for (int repeat = 0; repeat < 2; ++repeat)
            {
                CHP chp;
                PAP pap;
                long first, end;
                FetchChp(cp, &chp, &first, &end);
                if (!Character(t, chp) || !t.Word(first) || !t.Word(end))
                    return false;
                FetchPap(cp, &pap, &first, &end);
                if (!Paragraph(t, pap) || !t.Word(first) || !t.Word(end))
                    return false;
            }
        return true;
    }
    bool Bounds(LiveTextTrace &t)
    {
        for (long first = 0; first < CpMac(); ++first)
            for (long end = first; end <= CpMac(); ++end)
                for (int expand = 0; expand < 2; ++expand)
                {
                    long a = first, b = end;
                    _GetParaBounds(&a, &b, expand != 0);
                    if (!t.Word(a) || !t.Word(b))
                        return false;
                }
        return true;
    }
    bool Public(LiveTextTrace &t)
    {
        for (int n = 0; n < 4; ++n)
        {
            CHP chp = Chp(n + 1), old = Chp(n);
            PAP pap = Pap(n + 1), oldPap = Pap(n);
            failUndo = n == 1;
            SetDirty(false);
            events.clear();
            bool ok = FApplyChp(1, 4, &chp, n % 2 ? &old : nullptr, fdocNil);
            if (ok == failUndo || !Transaction(t, ok) || !Fetches(t))
                return false;
            long first = -1, end = -1;
            SetDirty(false);
            events.clear();
            ok = FApplyPap(n == 3 ? 1 : 0, n == 3 ? 0 : 5, &pap, n % 2 ? &oldPap : nullptr, &first, &end, n != 3,
                           fdocNil);
            if ((n == 3 ? !ok : ok == failUndo) || !t.Word(first) || !t.Word(end) || !Transaction(t, ok) || !Fetches(t))
                return false;
        }
        return true;
    }
    bool Transaction(LiveTextTrace &t, bool ok)
    {
        return t.Word(ok) && t.Word(FDirty()) && t.Word(_textUndo.combine) && t.Word(unsigned(events.size())) &&
               t.Raw(events.data(), events.size() * 4) && State(t);
    }
    bool CacheProbe(LiveTextTrace &t)
    {
        CHP p;
        long a, b;
        FetchChp(0, &p, &a, &b);
        if (!Character(t, p))
            return false;
        _dypFontDef += 4;
        FetchChp(0, &p, &a, &b);
        if (!Character(t, p))
            return false;
        _cpMinChp = _cpLimChp = 0;
        FetchChp(0, &p, &a, &b);
        return Character(t, p);
    }
    bool FontSize(LiveTextTrace &t, int bytes)
    {
        std::vector<byte> data(bytes, 0);
        LegacyLong font = Chp(1).onn, ref;
        if (bytes >= 4)
            std::memcpy(data.data(), &font, 4);
        return _FEnsureInAg(2, data.data(), bytes, &ref) && Apply(0, 3, 2, ref) && Fetches(t);
    }
};
static bool TestTextFormat()
{
    LiveTextTrace t("MM_TEXT_FORMAT_SNAPSHOT");
    auto p = NewObj FormatProbe;
    if (!p || !p->InitFormat() || !p->Deltas(t) || !p->Bounds(t) || !p->Fetches(t) || !p->Public(t) ||
        !p->CacheProbe(t))
        return false;
    ReleasePpo(&p);
    for (int bytes : {3, 6, 522, 523})
    {
        p = NewObj FormatProbe;
        if (!p || !p->InitFormat() || !p->FontSize(t, bytes))
            return false;
        ReleasePpo(&p);
    }
    bool golden = true;
#ifdef _WIN64
#ifdef KAUAI_SDL3
    golden = t.bytes == 21343 && t.hash == 0x78F581BEU;
#else
    golden = t.bytes == 21343 && t.hash == 0x0B74543DU;
#endif
#endif
    return t.Finish("Text format", false) && golden;
}
#pragma pop_macro("size")
#endif
