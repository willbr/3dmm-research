#ifndef TEXT_PROPERTIES_FIXTURE_H
#define TEXT_PROPERTIES_FIXTURE_H
#pragma push_macro("size")
#undef size
struct PropertyProbe : RichTextDocument
{
    bool Init()
    {
        static achar text[] = {'a', 'b', '\r', 'c', 'd', '\r', 'e', 'f', '\r'};
        return _FInit() && _pbsf->FReplace(text, sizeof(text), 0, sizeof(achar));
    }
    void Cache()
    {
        _cpMinChp = 1;
        _cpLimChp = 2;
        _cpMinPap = 3;
        _cpLimPap = 4;
    }
    bool Apply(long cp, long span, byte kind, LegacyLong value, LegacyLong mask = -1)
    {
        SPVM p = {kind, value, mask};
        if (mm_array_reserve(mm_text_collection_property_array(_properties, nullptr), 2, 0) != MM_STORAGE_OK)
            return false;
        Cache();
        _ApplyRgspvm(cp, span, &p, 1);
        return true;
    }
    bool Argument(byte kind, const char *text, LegacyLong *value)
    {
        return _FEnsureInAg(kind, const_cast<char *>(text), long(std::strlen(text)), value);
    }
    bool Adjust(long cp, long added, long removed)
    {
        std::vector<achar> text(added, 'X');
        if (!_pbsf->FReplace(text.data(), added * sizeof(achar), cp * sizeof(achar), removed * sizeof(achar)))
            return false;
        Cache();
        _AdjustMpe(cp, added, removed);
        return true;
    }
    bool State(LiveTextTrace &t)
    {
        if (!t.Word(_cpMinChp) || !t.Word(_cpLimChp) || !t.Word(_cpMinPap) || !t.Word(_cpLimPap) ||
            !DocumentStream(t, this) || !t.Word(mm_text_collection_count(_properties)))
            return false;
        for (LegacyLong i = 0; i < mm_text_collection_count(_properties); ++i)
        {
            MMU32 row[2];
            if (!mm_text_collection_property(_properties, i, row) || !t.Raw(row, sizeof(row)))
                return false;
        }
        if (!t.Word(mm_text_collection_count(_arguments)))
            return false;
        if (_arguments)
            for (LegacyLong i = 0; i < mm_text_collection_count(_arguments); ++i)
            {
                MMI32 bytes;
                const void *data = mm_text_collection_argument(_arguments, i, &bytes);
                if (!t.Word(data == nullptr))
                    return false;
                if (!data)
                    continue;
                const MMU32 fixed = mm_text_collection_argument_ref(_arguments, i);
                if (!t.Raw(&fixed, 4) || !t.Word(bytes) || !t.Raw(data, bytes))
                    return false;
            }
        return true;
    }
    bool Lookup(LiveTextTrace &t)
    {
        const byte kinds[] = {0, 1, 2, 3, 4, 63, 64, 65, 127, 128, 129, 130, 159, 160, 191, 192, 193, 254, 255};
        for (byte kind : kinds)
            for (long cp = 0; cp <= CpMac() + 1; ++cp)
            {
                MPE p;
                long end, index;
                bool ok = _FFindMpe(_SpcpFromSprmCp(kind, cp), &p, &end, &index);
                if (!t.Word(ok) || !t.Word(end) || !t.Word(index) || (ok && !t.Raw(&p, 8)))
                    return false;
                if (_FFindMpe(_SpcpFromSprmCp(kind, cp), nullptr, nullptr, nullptr) != ok)
                    return false;
            }
        for (long i = -1; i <= static_cast<long>(mm_text_collection_count(_properties)); ++i)
        {
            byte kind = 0x55;
            long value = 111, begin = 222, end = 333;
            bool ok = _FFetchProp(i, &kind, &value, &begin, &end);
            if (!t.Word(ok) || !t.Word(kind) || !t.Word(value) || !t.Word(begin) || !t.Word(end))
                return false;
            if (_FFetchProp(i, nullptr, nullptr, nullptr, nullptr) != ok)
                return false;
        }
        return true;
    }
    bool Seed()
    {
        return Apply(0, 3, 1, 0x1200) && Apply(2, 3, 1, 0x34, 0xff) && Apply(6, 3, 1, 0x5600, 0xff00) &&
               Apply(0, 3, 128, 1) && Apply(3, 3, 128, 2) && Apply(6, 3, 128, 3) && Apply(3, 3, 129, 4) &&
               Apply(6, 3, 129, 5);
    }
    bool Object(long cp, LegacyLong value)
    {
        MPE p = {_SpcpFromSprmCp(192, cp), value};
        long index;
        _FFindMpe(p.spcp, nullptr, nullptr, &index);
        const MMU32 row[2] = {static_cast<MMU32>(p.spcp), static_cast<MMU32>(p.lw)};
        return mm_text_collection_put_property(_properties, index, row, 1) != 0;
    }
};
static bool TestTextProperties()
{
    LiveTextTrace t("MM_TEXT_PROPERTIES_SNAPSHOT");
    auto p = NewObj PropertyProbe;
    if (!p || !p->Init() || !p->Lookup(t) || !p->State(t))
        return false;
    const long commands[][5] = {{2, 3, 1, 0x1234, -1},     {3, 2, 1, 0x56, 0xff}, {2, 1, 1, 0, -1},
                                {0, 9, 3, 0x01020304, -1}, {4, 2, 3, 0, -1},      {1, 4, 4, 0x100, -1},
                                {2, 3, 65, 17, -1},        {2, 3, 65, 17, -1},    {0, 9, 65, 0, -1},
                                {8, 1, 1, 0x66, 0xff},     {0, 9, 1, 0, -1}};
    for (const auto &c : commands)
        if (!p->Apply(c[0], c[1], byte(c[2]), LegacyLong(c[3]), LegacyLong(c[4])) || !p->State(t) || !p->Lookup(t))
            return false;
    LegacyLong a, b, c;
    if (!p->Argument(64, "one", &a) || !p->Argument(64, "two", &b) || !p->Argument(64, "one", &c) || a != c ||
        !p->State(t))
        return false;
    if (!p->Apply(0, 3, 64, a) || !p->Apply(2, 4, 64, b) || !p->Apply(6, 2, 64, c) || !p->State(t) || !p->Lookup(t))
        return false;
    if (!p->Apply(0, 9, 64, 0) || !p->State(t) || !p->Argument(160, "one", &a) || !p->Apply(0, 9, 160, a) ||
        !p->State(t))
        return false;
    ReleasePpo(&p);
    const long edits[][3] = {{0, 2, 0}, {2, 1, 0}, {3, 0, 2}, {1, 2, 5}, {0, 0, 6}, {6, 1, 2}, {2, 0, 1}, {0, 0, 8}};
    for (const auto &edit : edits)
    {
        p = NewObj PropertyProbe;
        if (!p || !p->Init() || !p->Seed() || !p->Argument(192, "object", &a) || !p->Object(4, a) || !p->State(t) ||
            !p->Adjust(edit[0], edit[1], edit[2]) || !p->State(t) || !p->Lookup(t))
            return false;
        ReleasePpo(&p);
    }
    bool golden = true;
#ifdef _WIN64
    golden = t.bytes == 59429 && t.hash == 0x30CB2692U;
#endif
    return t.Finish("Text properties", false) && golden;
}
#pragma pop_macro("size")
#endif
