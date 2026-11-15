#ifndef TEXT_LIVE_FIXTURE_H
#define TEXT_LIVE_FIXTURE_H
#pragma push_macro("size")
#undef size
// Existing engine integration fixture; no Marcus or window interaction.
struct LiveTextTrace
{
    FILE *file;
    unsigned bytes = 0, hash = 2166136261U;
    LiveTextTrace()
    {
        const char *p = std::getenv("MM_TEXT_LIVE_SNAPSHOT");
        file = p ? std::fopen(p, "wb") : std::tmpfile();
    }
    ~LiveTextTrace()
    {
        if (file)
            std::fclose(file);
    }
    bool Raw(const void *v, std::size_t n)
    {
        const auto p = static_cast<const byte *>(v);
        for (std::size_t i = 0; i < n; ++i)
            hash = (hash ^ p[i]) * 16777619U;
        bytes += unsigned(n);
        return file && std::fwrite(v, 1, n, file) == n;
    }
    bool Word(unsigned n)
    {
        return Raw(&n, 4);
    }
    bool Block(PChunkyFile file, ChunkIdentification id)
    {
        DataBlock b;
        if (!file->FFind(id.ctg, id.cno, &b) || !b.FUnpackData())
        {
            std::fprintf(stderr, "[live] failed at %d\n", __LINE__);
            return false;
        }
        std::vector<byte> data(b.Cb());
        return (data.empty() || b.FReadRgb(data.data(), b.Cb(), 0)) && Word(id.ctg) && Word(id.cno) &&
               Word(unsigned(data.size())) && Raw(data.data(), data.size());
    }
    bool Tree(PChunkyFile file, ChunkIdentification root)
    {
        if (!Block(file, root) || !Word(file->Ckid(root.ctg, root.cno)))
        {
            std::fprintf(stderr, "[live] failed at %d\n", __LINE__);
            return false;
        }
        for (LegacyLong i = 0; i < file->Ckid(root.ctg, root.cno); ++i)
        {
            ChildChunkIdentification child;
            if (!file->FGetKid(root.ctg, root.cno, i, &child) || !Word(child.chid))
            {
                std::fprintf(stderr, "[live] failed at %d\n", __LINE__);
                return false;
            }
            if (child.cki.ctg != kctgTxtPropArgs)
            {
                if (!Block(file, child.cki))
                {
                    std::fprintf(stderr, "[live] failed at %d\n", __LINE__);
                    return false;
                }
            }
            else
            {
                DataBlock block;
                if (!file->FFind(child.cki.ctg, child.cki.cno, &block))
                {
                    std::fprintf(stderr, "[live] failed at %d\n", __LINE__);
                    return false;
                }
                short bo, osk;
                auto args = AllocatedGroup::PagRead(&block, &bo, &osk);
                if (!args)
                {
                    std::fprintf(stderr, "[live] failed at %d\n", __LINE__);
                    return false;
                }
                bool ok = Word(child.cki.ctg) && Word(child.cki.cno) && Word(unsigned(bo)) && Word(unsigned(osk)) &&
                          Word(args->IvMac()) && Word(args->CbFixed());
                for (LegacyLong j = 0; ok && j < args->IvMac(); ++j)
                {
                    bool free = args->FFree(j);
                    ok = Word(free);
                    if (free)
                        continue;
                    std::vector<byte> data(args->Cb(j)), fixed(args->CbFixed());
                    args->Get(j, data.data());
                    args->GetFixed(j, fixed.data());
                    ok = ok && Word(unsigned(data.size())) && Raw(fixed.data(), fixed.size()) &&
                         Raw(data.data(), data.size());
                }
                ReleasePpo(&args);
                if (!ok)
                {
                    std::fprintf(stderr, "[live] failed at %d\n", __LINE__);
                    return false;
                }
            }
        }
        return true;
    }
    bool Finish()
    {
        bool ok = file && std::fclose(file) == 0;
        file = nullptr;
#ifdef _WIN64
#ifdef KAUAI_SDL3
        ok = ok && bytes == 2856 && hash == 0x5D914817U;
#else
        ok = ok && bytes == 2856 && hash == 0x8FB30D47U;
#endif
#endif
        std::fprintf(stderr, "Live text trace: bytes=%u fingerprint=%08X\n", bytes, hash);
        return ok;
    }
};
static PTBOX LiveCaptionInput(PScene scene, PChunkyFile file, const std::vector<achar> &bytes, int kind)
{
    RichTextData source;
    source.fontName.SetSzs(PszLit("Arial"));
    source.defaults.dypFont = 16 + (kind & 1);
    source.defaults.dxpDef = 170;
    source.properties = DynamicArray::PglNew(8);
    source.text.pfil = FileObject::PfilCreateTemp();
    source.text.cb = LegacyLong(bytes.size() * sizeof(achar));
    if (!source.properties || !source.text.pfil || !source.text.pfil->FSetFpMac(source.text.cb) ||
        (source.text.cb && !source.text.FWriteRgb(const_cast<achar *>(bytes.data()), source.text.cb, 0)))
        return nullptr;
    if (!bytes.empty())
    {
        RichTextPropertyEntry style = {0x01000000, kind % 3}, fontProperty = {0x02000000, 1};
        String font;
        font.SetSzs(PszLit("Courier New"));
        std::vector<byte> argument(6 + font.CbData(), 0);
        short osk = koskCur;
        std::memcpy(argument.data() + 4, &osk, 2);
        font.GetData(argument.data() + 6);
        source.arguments = AllocatedGroup::PagNew(4);
        LegacyLong refs = 0x02000001;
        if (!source.arguments ||
            !source.arguments->FAdd(LegacyLong(argument.size()), nullptr, argument.data(), &refs) ||
            !source.properties->FAdd(&style) || !source.properties->FAdd(&fontProperty))
            return nullptr;
    }
    ChunkIdentification root;
    ChunkNumber number;
    TextBoxData metadata;
    metadata.firstFrame = 1;
    metadata.maxFrame = 61;
    metadata.story = true;
    metadata.bounds.Set(10, 20, 180, 100);
    if (!source.FWrite(file, &root) || !file->FAdd(0, kctgTbox, &number) || !metadata.FWrite(file, number) ||
        !file->FAdoptChild(kctgTbox, number, root.ctg, root.cno, 0))
        return nullptr;
    auto resources = ChunkyResourceFile::PcrfNew(file, 0);
    if (!resources)
        return nullptr;
    auto box = TextBox::PtboxRead(resources, number, scene);
    ReleasePpo(&resources);
    return box;
}
static bool TestLiveCaptionSaving(PScene scene, PChunkyFile file)
{
    LiveTextTrace trace;
    for (int kind = 0; kind < 8; ++kind)
    {
        const bool redirect = (kind & 1) != 0;
        std::vector<achar> text;
        if (kind >= 2 && kind < 4)
            text = {'A'};
        if (kind >= 4 && kind < 6)
            text = {'B', 0, '\r', '\n', achar(0xe9)};
        if (kind >= 6)
            text = {'x', 'y', 'z', '\r'};
        struct Owner
        {
            PTBOX value;
            ~Owner()
            {
                ReleasePpo(&value);
            }
        } owner = {LiveCaptionInput(scene, file, text, kind)};
        auto caption = owner.value;
        if (!caption)
        {
            std::fprintf(stderr, "[live] failed at %d\n", __LINE__);
            return false;
        }
        ChunkIdentification root = {22, 33};
        if (!caption->FSaveToChunk(file, &root, redirect) || !trace.Word(kind) || !trace.Tree(file, root))
        {
            std::fprintf(stderr, "[live] failed at %d\n", __LINE__);
            return false;
        }
        RichTextData loaded;
        if (!loaded.FRead(file, root.ctg, root.cno) || loaded.text.cb != long(text.size() * sizeof(achar)))
        {
            std::fprintf(stderr, "[live] failed at %d\n", __LINE__);
            return false;
        }
        std::vector<achar> live(text.size() + 1);
        caption->Pbsf()->FetchRgb(0, long(live.size() * sizeof(achar)), live.data());
        if (!trace.Word(unsigned(live.size())) || !trace.Raw(live.data(), live.size() * sizeof(achar)))
        {
            std::fprintf(stderr, "[live] failed at %d\n", __LINE__);
            return false;
        }
        if (!text.empty())
        {
            ChildChunkIdentification child;
            DataBlock block;
            achar changed = 'Z', first;
            if (!file->FGetKidChidCtg(root.ctg, root.cno, 0, kctgText, &child) ||
                !file->FFind(child.cki.ctg, child.cki.cno, &block) || !block.FWriteRgb(&changed, sizeof(changed), 2))
            {
                std::fprintf(stderr, "[live] failed at %d\n", __LINE__);
                return false;
            }
            caption->Pbsf()->FetchRgb(0, sizeof(first), &first);
            if (!trace.Word(unsigned(first)))
            {
                std::fprintf(stderr, "[live] failed at %d\n", __LINE__);
                return false;
            }
        }
        ChunkNumber number;
        TextBoxData stored;
        ChunkIdentification child;
        if (!file->FAdd(0, kctgTbox, &number) || !caption->FWrite(file, number) ||
            !stored.FRead(file, number, &child) || !trace.Block(file, {kctgTbox, number}) || !trace.Tree(file, child))
        {
            std::fprintf(stderr, "[live] failed at %d\n", __LINE__);
            return false;
        }
    }
    return trace.Finish();
}
#pragma pop_macro("size")
#endif
