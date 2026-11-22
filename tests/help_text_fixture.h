#ifndef HELP_TEXT_FIXTURE_H
#define HELP_TEXT_FIXTURE_H
#pragma push_macro("size")
#undef size
struct HelpTextProbe : Help::TextDocument
{
    HelpTextProbe(PResourceCache cache) : Help::TextDocument(cache)
    {
    }
    bool Init()
    {
        static achar text[] = {'a', 'b', '\r', 'c', 'd', '\r'};
        SetCundbMax(0);
        return _FInit() && _pbsf->FReplace(text, sizeof(text), 0, sizeof(achar));
    }
    bool Load(PChunkyFile file, ChunkIdentification root, ScriptCompiler::PStringRegistry strings = nullptr,
              ulong flags = 0)
    {
        auto cache = ChunkyResourceFile::PcrfNew(file, 0);
        const bool ok = cache && _FReadResource(cache, root.ctg, root.cno, strings, flags);
        ReleasePpo(&cache);
        return ok;
    }
    bool Load(PChunkyResourceFile cache, ChunkIdentification root)
    {
        return _FReadResource(cache, root.ctg, root.cno, nullptr, 0);
    }
    bool Arg(LiveTextTrace &t, byte kind, const std::vector<byte> &bytes, short order)
    {
        LegacyLong ref;
        if (!_FEnsureInAg(kind, const_cast<byte *>(bytes.data()), long(bytes.size()), &ref))
            return false;
        bool ok = _FOpenArg(ref - 1, kind, order, koskCur);
        MMI32 count;
        const void *out = mm_text_collection_argument(_arguments, ref - 1, &count);
        return out && t.Word(ok) && t.Word(count) && t.Raw(out, count);
    }
    bool State(LiveTextTrace &t)
    {
        Help::Topic topic;
        GetHtop(&topic);
        if (!t.Raw(&topic, 32) || !t.Word(FInternal()) || !t.Word(_textUndo.suspended) || !DocumentStream(t, this) ||
            !t.Word(mm_text_collection_count(_properties)))
            return false;
        for (LegacyLong i = 0; i < mm_text_collection_count(_properties); ++i)
        {
            MMU32 row[2];
            if (!mm_text_collection_property(_properties, i, row) || !t.Raw(row, sizeof(row)))
                return false;
        }
        for (long cp = 0; cp < CpMac(); ++cp)
        {
            long first = -1, end = -1;
            byte group = 0;
            ChunkNumber target = 0;
            String name;
            name.SetSzs(PszLit("stale"));
            bool found = FGrouped(cp, &first, &end, &group, &target, &name);
            if (!t.Word(found) || !t.Word(first) || !t.Word(end) || !t.Word(group) || !t.Word(target) ||
                !t.Word(name.Cch()) || !t.Raw(name.Prgch(), name.Cch() * sizeof(achar)) || FGrouped(cp) != found)
                return false;
        }
        for (long cp = 0; cp < CpMac(); ++cp)
        {
            long at, bytes = -1;
            void *data = nullptr;
            bool found = FFetchObject(cp, &at, &data, &bytes);
            if (!t.Word(found) || !t.Word(at) || !t.Word(bytes) ||
                (found && (!t.Word(data != nullptr) || (data && !t.Raw(data, bytes)))))
                return false;
            FreePpv(&data);
        }
        return true;
    }
};
static bool TestHelpText()
{
    LiveTextTrace t("MM_HELP_TEXT_SNAPSHOT");
    auto file = ChunkyFile::PcflCreateTemp();
    auto cache = file ? ChunkyResourceFile::PcrfNew(file, 0) : nullptr;
    if (!file || !cache)
        return false;
    auto p = NewObj HelpTextProbe(cache);
    if (!p || !p->Init() || !p->State(t))
        return false;
    String label(PszLit("Topic"));
    if (!p->FGroupText(4, 1, 3, 0x12345678, &label) || !p->State(t) || !p->FGroupText(2, 2, 4) ||
        !p->FGroupText(2, 3, 0) || !p->State(t))
        return false;
    static byte extra[] = {1, 0, 4};
    if (!p->FInsertPicture(7, extra, sizeof(extra), 1, 1, nullptr, fdocNil) || !p->State(t) ||
        !p->FInsertButton(8, 9, nullptr, 0, 4, 0, nullptr, fdocNil) || !p->State(t))
        return false;
    Help::Topic topic = {11, 12, 13, 14, 15, 16, {0x534e4449, 0x12345678}};
    p->SetHtop(&topic);
    ChunkIdentification root;
    if (!p->FSaveToChunk(file, &root) || !t.Tree(file, root))
        return false;
    ReleasePpo(&p);
    for (int mode = 0; mode < 5; ++mode)
    {
        DataBlock block;
        Help::TopicFile header;
        if (!file->FFind(root.ctg, root.cno, &block) || !block.FRead(&header))
            return false;
        header.bo = mode & 1 ? kboCur : kboOther;
        header.osk = koskCur;
        header.htop = topic;
        if (header.bo == kboOther)
            SwapBytesRglw(reinterpret_cast<LegacyLong *>(&header.htop), 8);
        long bytes = mode == 1 || mode == 2 ? 28 : 36;
        if (!file->FPut(bytes, root.ctg, root.cno, &block) || !block.FWriteRgb(&header, bytes, 0))
            return false;
        p = NewObj HelpTextProbe(cache);
        if (!p || !p->Load(file, root) || !p->State(t))
            return false;
        ReleasePpo(&p);
        // Restore full-size data for the next variant.
        header.bo = kboCur;
        header.htop = topic;
        if (!file->FPut(36, root.ctg, root.cno, &block) || !block.FWrite(&header))
            return false;
    }
    p = NewObj HelpTextProbe(cache);
    if (!p || !p->Init())
        return false;
    static achar placeholders[] = {'~', '1', '2', 'x', '~', '0', '~', '9', '9', '\r'};
    if (!p->FReplaceRgch(placeholders, sizeof(placeholders) / sizeof(achar), 0, p->CpMac() - 1, fdocNil) ||
        !p->FSaveToChunk(file, &root))
        return false;
    const auto expectedLength = p->CpMac();
    Help::Topic expectedTopic;
    p->GetHtop(&expectedTopic);
    ReleasePpo(&p);
    ScriptCompiler::StringRegistry strings;
    String a(PszLit("one~99")), b(PszLit("zero"));
    if (!strings.FPut(12, &a) || !strings.FPut(0, &b))
        return false;
    p = NewObj HelpTextProbe(cache);
    if (!p || !p->Load(file, root, &strings, Help::ftxhdExpandStrings) || !p->State(t))
        return false;
    ReleasePpo(&p);
    for (byte kind : {byte(64), byte(192), byte(194)})
        for (int order = 0; order < 2; ++order)
            for (int size : {3, 4, 5, 7, 8, 11, 12, 15})
            {
                p = NewObj HelpTextProbe(cache);
                if (!p || !p->Init())
                    return false;
                std::vector<byte> bytes(size, 0x17);
                LegacyLong words[] = {kctgGokd, 0x12345678, 0x10203040};
                if (order)
                    SwapBytesRglw(words, 3);
                std::memcpy(bytes.data(), words, size < 12 ? size : 12);
                if (!p->Arg(t, kind, bytes, order ? kboOther : kboCur))
                    return false;
                ReleasePpo(&p);
            }
    auto forestFile = FileObject::PfilCreateTemp();
    LegacyLong forestBytes;
    ChunkNumber forestNumber;
    if (!forestFile || !file->FWriteChunkTree(root.ctg, root.cno, forestFile, 0, &forestBytes))
        return false;
    {
        DataBlock forest(forestFile, 0, forestBytes);
        if (!file->FAddBlck(&forest, root.ctg, &forestNumber))
            return false;
    }
    ReleasePpo(&forestFile);
    file->SetForest(root.ctg, forestNumber, true);
    for (bool packed : {false, true})
    {
        if (packed && (!file->FPackData(root.ctg, forestNumber) || !file->FPacked(root.ctg, forestNumber)))
            return false;
        p = NewObj HelpTextProbe(cache);
        Help::Topic loaded;
        if (!p || !p->Load(cache, {root.ctg, forestNumber}) || p->CpMac() != expectedLength)
            return false;
        p->GetHtop(&loaded);
        if (loaded.cnoBalloon != expectedTopic.cnoBalloon || loaded.hidThis != expectedTopic.hidThis)
            return false;
        ReleasePpo(&p);
    }
    ReleasePpo(&cache);
    ReleasePpo(&file);
    bool golden = true;
#ifdef _WIN64
    golden = t.bytes == 5324 && t.hash == 0xAD5FE208U;
#endif
    return t.Finish("Help text", false) && golden;
}
#pragma pop_macro("size")
#endif
