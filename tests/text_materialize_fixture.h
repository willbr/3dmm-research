#ifndef TEXT_MATERIALIZE_FIXTURE_H
#define TEXT_MATERIALIZE_FIXTURE_H
#pragma push_macro("size")
#undef size
struct MaterializeProbe : RichTextDocument
{
    int fail = -1;
    std::vector<unsigned> opened;
    bool Load(PChunkyFile file, ChunkIdentification root, bool copy)
    {
        return _FReadChunk(file, root.ctg, root.cno, copy);
    }
    bool _FOpenArg(long index, byte kind, short order, short system) override
    {
        const unsigned v[] = {unsigned(index), kind, unsigned(order), unsigned(system)};
        opened.insert(opened.end(), v, v + 4);
        if (index == fail)
            return false;
        if (kind == 0x80)
            return true;
        return RichTextDocument::_FOpenArg(index, kind, order, system);
    }
    bool Record(LiveTextTrace &t, bool ok)
    {
        const unsigned v[] = {ok,
                              unsigned(_dxpDef),
                              unsigned(_dypFontDef),
                              unsigned(_acrBack.LwGet()),
                              unsigned(_oskFont),
                              unsigned(_onnDef),
                              unsigned(_stnFontDef.Cch()),
                              unsigned(opened.size())};
        if (!t.Raw(v, sizeof(v)) || !t.Raw(_stnFontDef.Prgch(), _stnFontDef.Cch() * sizeof(achar)) ||
            !t.Raw(opened.data(), opened.size() * 4) || !DocumentStream(t, this))
            return false;
        if (!t.Word(mm_text_collection_count(_properties)))
            return false;
        if (_properties)
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
};
static bool MaterializeInput(PChunkyFile file, int kind, ChunkIdentification *root)
{
    RichTextData text;
    text.defaults.dxpDef = 321;
    text.defaults.dypFont = 19;
    text.defaults.lwAcrBack = 0x01010203;
    text.fontName.SetSzs(PszLit("Arial"));
    text.properties = DynamicArray::PglNew(8);
    text.text.pfil = FileObject::PfilCreateTemp();
    std::vector<byte> bytes = {'A', 0, 'B', '\r'};
    const bool foreign = kind == 1 || kind == 4 || kind == 6 || kind == 9;
    if (kind == 1)
    {
        bytes = {'A', 0, 0, 0, 'B', 0, '\r', 0};
        text.textOsk = 0x0505;
    }
    if (kind == 3)
        text.textOsk = 0x1111;
    text.text.cb = LegacyLong(bytes.size());
    if (!text.properties || !text.text.pfil || !text.text.pfil->FSetFpMac(text.text.cb) ||
        !text.text.FWriteRgb(bytes.data(), text.text.cb, 0))
        return false;
    if (kind != 2 && kind != 9)
    {
        text.arguments = AllocatedGroup::PagNew(4);
        if (!text.arguments)
            return false;
        for (int i = 0; i < 3; ++i)
        {
            std::vector<byte> data;
            LegacyLong refs;
            if (i == 1)
            {
                data = {1, 2, 3, 4};
                refs = kind == 5 ? 0x7e000001 : LegacyLong(0x80000001U);
            }
            else
            {
                String font;
                font.SetSzs(i == 0 ? PszLit("Courier New") : PszLit("Times New Roman"));
                data.resize(6 + font.CbData(), 0);
                short osk = koskCur;
                std::memcpy(data.data() + 4, &osk, 2);
                font.GetData(data.data() + 6);
                refs = 0x02000001;
            }
            if (foreign)
                SwapBytesRglw(&refs, 1);
            if (!text.arguments->FAdd(LegacyLong(data.size()), nullptr, data.data(), &refs))
                return false;
        }
        if (kind == 6)
            text.arguments->Delete(1);
    }
    std::vector<RichTextPropertyEntry> p = {{0x01000000, 1}, {0x03000002, 0x010000ff}};
    if (kind != 2 && kind != 9)
        p.insert(p.begin() + 1, {0x02000000, 1});
    if (kind == 9)
        p.clear();
    if (foreign)
    {
        text.propertyBo = text.argumentBo = kboOther;
        text.propertyOsk = text.argumentOsk = koskSbMac;
    }
    for (auto entry : p)
    {
        if (foreign)
            SwapBytesRglw(&entry, 2);
        if (!text.properties->FAdd(&entry))
            return false;
    }
    if (!text.FWrite(file, root))
        return false;
    if (foreign)
    {
        DataBlock block;
        RichTextProperties header;
        if (!file->FFind(root->ctg, root->cno, &block) || !block.FReadRgb(&header, 16, 0))
            return false;
        SwapBytesBom(&header, kbomRdop);
        if (!block.FWriteRgb(&header, 16, 0))
            return false;
    }
    if (kind == 8)
    {
        ChildChunkIdentification child;
        if (!file->FGetKidChidCtg(root->ctg, root->cno, 0, kctgTxtProps, &child))
            return false;
        file->DeleteChild(root->ctg, root->cno, child.cki.ctg, child.cki.cno, 0);
    }
    return true;
}
static bool TestTextMaterialization()
{
    LiveTextTrace trace("MM_TEXT_MATERIALIZE_SNAPSHOT");
    auto file = ChunkyFile::PcflCreateTemp();
    if (!file)
        return false;
    for (int kind = 0; kind < 10; ++kind)
    {
        ChunkIdentification root;
        if (!MaterializeInput(file, kind, &root))
            return false;
        auto doc = NewObj MaterializeProbe;
        if (!doc)
            return false;
        if (kind == 4)
            doc->fail = 1;
        bool ok = doc->Load(file, root, kind != 7);
        const bool expected = kind != 3 && kind != 4 && kind != 5 && kind != 8;
        if (ok != expected)
        {
            std::fprintf(stderr, "[materialize] case %d returned %d\n", kind, ok);
            return false;
        }
        bool recorded = trace.Word(kind) && doc->Record(trace, ok);
        ReleasePpo(&doc);
        if (!recorded)
            return false;
    }
    ReleasePpo(&file);
    bool golden = true;
#ifdef _WIN64
#ifdef KAUAI_SDL3
    golden = trace.bytes == 1551 && trace.hash == 0xCFB08F3AU;
#else
    golden = trace.bytes == 1551 && trace.hash == 0x2573FB83U;
#endif
#endif
    return trace.Finish("Text materialization", false) && golden;
}
#pragma pop_macro("size")
#endif
