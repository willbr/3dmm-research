#ifndef TEXT_COPY_FIXTURE_H
#define TEXT_COPY_FIXTURE_H
#pragma push_macro("size")
#undef size
struct CopyProbe : FormatProbe
{
    bool SeedCopy(bool source)
    {
        if (!Init())
            return false;
        _dypFontDef = source ? 18 : 24;
        LegacyLong ref;
        static const byte client[] = {4, 0, 7, 9};
        if (!Apply(0, 3, 1, source ? 0x00020003 : 0x00040001) || !Apply(3, 3, 1, source ? 0x00050001 : 0x00010002) ||
            !Apply(1, 6, 3, source ? 0x00010203 : 0x00040302) || !Argument(64, "client", &ref) ||
            !Apply(1, 5, 64, ref) || !_FEnsureInAg(160, const_cast<byte *>(client), sizeof(client), &ref) ||
            !Apply(0, 6, 160, ref) || !Apply(0, 3, 128, 0x01010002) || !Apply(3, 3, 128, 0x02020004) ||
            !Apply(3, 3, 129, 0x00400003))
            return false;
        return true;
    }
    void Copy(CopyProbe *source, long from, long at, long sourceSpan, long destinationSpan, byte first, byte end)
    {
        _CopyProps(source, from, at, sourceSpan, destinationSpan, first, end);
    }
    bool AddCustom(byte kind, long cp, const char *bytes)
    {
        LegacyLong ref;
        if (!Argument(kind, bytes, &ref))
            return false;
        MPE p = {_SpcpFromSprmCp(kind, cp), ref}, old;
        long i = 0;
        while (i < _pglmpe->IvMac())
        {
            _pglmpe->Get(i, &old);
            if (old.spcp > p.spcp)
                break;
            ++i;
        }
        achar marker = kchObject;
        if (!_pbsf->FReplace(&marker, sizeof(marker), cp * sizeof(achar), sizeof(achar)) || !_pglmpe->FInsert(i, &p))
            return false;
        _textCache.first = _textCache.end = 0;
        return true;
    }
    bool ObjectQueries(LiveTextTrace &t)
    {
        for (long cp = 0; cp < CpMac(); ++cp)
        {
            long at = -1, bytes = -1;
            void *data = nullptr;
            bool ok = FFetchObject(cp, &at, &data, &bytes);
            if (!t.Word(ok) || !t.Word(at) || !t.Word(bytes) ||
                (ok && (!t.Word(data != nullptr) || (data && !t.Raw(data, bytes)))))
                return false;
            FreePpv(&data);
            long a = -1;
            bool same = FFetchObject(cp, &a, nullptr, nullptr);
            if (ok != same || at != a)
                return false;
        }
        return true;
    }
    bool Values(LiveTextTrace &t)
    {
        for (long cp = 0; cp < CpMac(); ++cp)
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
        return t.Word(_dypFontDef) && State(t) && ObjectQueries(t);
    }
    bool Objects(LiveTextTrace &t)
    {
        static byte first[] = {1, 0, 3, 5, 7}, second[] = {2, 4, 0, 6};
        CHP chp = Chp(1);
        if (!FInsertObject(first, sizeof(first), 2, 1, &chp, fdocNil) || !Values(t) ||
            !FInsertObject(first, sizeof(first), 6, 0, nullptr, fdocNil) || !Values(t) ||
            !FApplyObjectProps(second, sizeof(second), 2, fdocNil) || !Values(t) ||
            !FApplyObjectProps(second, sizeof(second), 2, fdocNil) || !Values(t))
            return false;
        failUndo = true;
        if (FInsertObject(first, sizeof(first), 3, 0, &chp, fdocNil) ||
            FApplyObjectProps(first, sizeof(first), 2, fdocNil) || !Values(t))
            return false;
        failUndo = false;
        return true;
    }
};
static bool TestTextCopy()
{
    LiveTextTrace t("MM_TEXT_COPY_SNAPSHOT");
    const long ranges[][6] = {
        {0, 1, 6, 7, 0, 128}, {1, 2, 5, 6, 0, 192}, {3, 0, 3, 5, 128, 192}, {0, 0, 6, 9, 64, 128}};
    for (const auto &r : ranges)
    {
        auto source = NewObj CopyProbe;
        auto target = NewObj CopyProbe;
        if (!source || !target || !source->SeedCopy(true) || !target->SeedCopy(false) || !source->Values(t) ||
            !target->Values(t))
            return false;
        target->Copy(source, r[0], r[1], r[2], r[3], byte(r[4]), byte(r[5]));
        if (!target->Values(t) || !source->Values(t))
            return false;
        ReleasePpo(&source);
        ReleasePpo(&target);
    }
    const long pastes[][4] = {{1, 4, 2, 2}, {3, 3, 0, 0}, {0, 9, 0, 9}, {8, 1, 8, 1}, {0, 0, 1, 0}};
    for (const auto &r : pastes)
    {
        auto source = NewObj CopyProbe;
        auto target = NewObj CopyProbe;
        static byte payload[] = {9, 0, 1, 2, 3};
        if (!source || !target || !source->SeedCopy(true) || !target->SeedCopy(false) ||
            !source->FInsertObject(payload, sizeof(payload), 4, 1, nullptr, fdocNil) ||
            !source->AddCustom(193, 1, "odd object") || !source->AddCustom(194, 7, "custom object") ||
            !source->Values(t))
            return false;
        bool ok = target->FReplaceTxrd(source, r[0], r[1], r[2], r[3], fdocNil);
        if (!ok || !target->Values(t) || !source->Values(t))
            return false;
        ReleasePpo(&source);
        ReleasePpo(&target);
    }
    auto p = NewObj CopyProbe;
    if (!p || !p->SeedCopy(false) || !p->Objects(t))
        return false;
    ReleasePpo(&p);
    bool golden = true;
#ifdef _WIN64
#ifdef KAUAI_SDL3
    golden = t.bytes == 36132 && t.hash == 0x83E2797BU;
#else
    golden = t.bytes == 36132 && t.hash == 0x9100DDD3U;
#endif
#endif
    return t.Finish("Text copy", false) && golden;
}
#pragma pop_macro("size")
#endif
