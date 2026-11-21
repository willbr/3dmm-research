#ifndef TEXT_DOCUMENT_FIXTURE_H
#define TEXT_DOCUMENT_FIXTURE_H
#pragma push_macro("size")
#undef size
static bool DocumentStream(LiveTextTrace &t, PTextDocumentBase doc)
{
    std::vector<byte> bytes(doc->Pbsf()->IbMac());
    doc->Pbsf()->FetchRgb(0, LegacyLong(bytes.size()), bytes.data());
    return t.Word(doc->CpMac()) && t.Word(doc->FDirty()) && t.Word(unsigned(bytes.size())) &&
           t.Raw(bytes.data(), bytes.size());
}
static bool DocumentPath(LiveTextTrace &t, PTextDocumentBase doc, Filename *expected)
{
    Filename name;
    bool found = doc->FGetFni(&name);
    return found == (expected != nullptr) && (!found || name.FEqual(expected)) && t.Word(found);
}
static bool RawDocumentFile(LiveTextTrace &t, Filename &name)
{
    auto file = FileObject::PfilOpen(&name);
    if (!file)
    {
        std::fprintf(stderr, "[document] failed at %d\n", __LINE__);
        return false;
    }
    std::vector<byte> bytes(file->FpMac());
    bool ok = bytes.empty() || file->FReadRgb(bytes.data(), LegacyLong(bytes.size()), 0);
    ReleasePpo(&file);
    return ok && t.Word(unsigned(bytes.size())) && t.Raw(bytes.data(), bytes.size());
}
static bool TestByteDocumentFiles()
{
    byte expected[] = {0xef, 0xbb, 0xbf, 0, '\r'};
    for (LegacyLong length : {LegacyLong(0), LegacyLong(sizeof(expected))})
    {
        Filename copy, adopt, current;
        if (!copy.FGetTemp() || !adopt.FGetTemp())
            return false;
        auto matches_file = [&](Filename &name) {
            auto file = FileObject::PfilOpen(&name);
            byte bytes[sizeof(expected)];
            const bool ok = file && file->FpMac() == length &&
                            (!length || (file->FReadRgb(bytes, length, 0) && !std::memcmp(bytes, expected, length)));
            ReleasePpo(&file);
            return ok;
        };
        auto stream = NewObj FileByteStream;
        auto doc = stream && stream->FReplace(expected, length, 0, 0)
                       ? TextDocumentByteStream::PtxdcNew(nullptr, stream)
                       : nullptr;
        if (doc)
            doc->SetDirty();
        bool ok = doc && doc->Pbsf() == stream && stream->CactRef() == 2 && stream->IbMac() == length &&
                  !doc->FGetFni(&current) && doc->FSaveToFni(&copy, false) && doc->FDirty() && !doc->FGetFni(&current) &&
                  matches_file(copy) && doc->FSaveToFni(&adopt, true) && !doc->FDirty() && doc->FGetFni(&current) &&
                  current.FEqual(&adopt) && matches_file(adopt);
        if (ok)
        {
            doc->SetDirty();
            ok = doc->FSaveToFni(nullptr, false) && !doc->FDirty() && matches_file(adopt);
        }
        ReleasePpo(&doc);
        ReleasePpo(&stream);
        if (ok)
        {
            doc = TextDocumentByteStream::PtxdcNew(&adopt);
            ok = doc && doc->Pbsf()->IbMac() == length && doc->FGetFni(&current) && current.FEqual(&adopt);
            if (ok && length)
            {
                byte bytes[sizeof(expected)];
                doc->Pbsf()->FetchRgb(0, length, bytes);
                ok = std::memcmp(bytes, expected, length) == 0;
            }
            ReleasePpo(&doc);
        }
        if (!ok || !copy.FDelete() || !adopt.FDelete())
            return false;
    }
    return true;
}
static bool RichDocumentFile(LiveTextTrace &t, Filename &name)
{
    auto file = ChunkyFile::PcflOpen(&name, fcflNil);
    if (!file)
    {
        std::fprintf(stderr, "[document] failed at %d\n", __LINE__);
        return false;
    }
    ChunkIdentification root;
    bool ok = file->FGetCkiCtg(kctgRichText, 0, &root) && t.Tree(file, root);
    ReleasePpo(&file);
    return ok;
}
static bool TestTextDocumentFiles(PScene scene, PChunkyFile source)
{
    LiveTextTrace t("MM_TEXT_DOCUMENT_SNAPSHOT");
    for (int kind = 0; kind < 4; ++kind)
    {
        std::fprintf(stderr, "[document] case %d\n", kind);
        Filename copy, adopt, second;
        if (!copy.FGetTemp() || !adopt.FGetTemp() || !second.FGetTemp())
        {
            std::fprintf(stderr, "[document] failed at %d\n", __LINE__);
            return false;
        }
        std::vector<achar> text = kind & 1 ? std::vector<achar>{'A', 0, '\r', '\n', achar(0xe9)} : std::vector<achar>{};
        PTextDocumentBase doc = nullptr;
        PFileByteStream borrowed = nullptr;
        if (kind < 2)
        {
            borrowed = NewObj FileByteStream;
            if (!borrowed || !borrowed->FReplace(text.data(), LegacyLong(text.size() * sizeof(achar)), 0, 0))
            {
                std::fprintf(stderr, "[document] failed at %d\n", __LINE__);
                return false;
            }
            doc = PlainTextDocument::PtxpdNew(nullptr, borrowed, koskCur);
            if (!doc || borrowed->CactRef() != 2 || borrowed->IbMac() != LegacyLong((text.size() + 1) * sizeof(achar)))
            {
                std::fprintf(stderr, "[document] failed at %d\n", __LINE__);
                return false;
            }
        }
        else
        {
            auto box = LiveCaptionInput(scene, source, text, kind);
            ChunkIdentification root;
            if (!box || !box->FSaveToChunk(source, &root))
                return false;
            doc = RichTextDocument::PtxrdReadChunk(source, root.ctg, root.cno);
            ReleasePpo(&box);
        }
        if (!doc || !t.Word(kind) || !DocumentPath(t, doc, nullptr) || !DocumentStream(t, doc))
        {
            std::fprintf(stderr, "[document] failed at %d\n", __LINE__);
            return false;
        }
        doc->SetDirty();
        if (!doc->FSaveToFni(&copy, false) || !doc->FDirty() || !DocumentPath(t, doc, nullptr) ||
            !DocumentStream(t, doc) || !(kind < 2 ? RawDocumentFile(t, copy) : RichDocumentFile(t, copy)))
        {
            std::fprintf(stderr, "[document] failed at %d\n", __LINE__);
            return false;
        }
        if (!doc->FSaveToFni(&adopt, true) || doc->FDirty() || !DocumentPath(t, doc, &adopt) ||
            !DocumentStream(t, doc) || !(kind < 2 ? RawDocumentFile(t, adopt) : RichDocumentFile(t, adopt)))
        {
            std::fprintf(stderr, "[document] failed at %d\n", __LINE__);
            return false;
        }
        doc->SetDirty();
        if (!doc->FSaveToFni(&second, false) || !doc->FDirty() || !DocumentPath(t, doc, &adopt) ||
            !(kind < 2 ? RawDocumentFile(t, second) : RichDocumentFile(t, second)))
        {
            std::fprintf(stderr, "[document] failed at %d\n", __LINE__);
            return false;
        }
        if (!doc->FSaveToFni(nullptr, false) || doc->FDirty() || !DocumentPath(t, doc, &adopt) ||
            !DocumentStream(t, doc) || !(kind < 2 ? RawDocumentFile(t, adopt) : RichDocumentFile(t, adopt)))
        {
            std::fprintf(stderr, "[document] failed at %d\n", __LINE__);
            return false;
        }
        ReleasePpo(&doc);
        ReleasePpo(&borrowed);
        doc = kind < 2 ? static_cast<PTextDocumentBase>(PlainTextDocument::PtxpdNew(&adopt))
                       : static_cast<PTextDocumentBase>(RichTextDocument::PtxrdNew(&adopt));
        if (!doc || !DocumentPath(t, doc, &adopt) || !DocumentStream(t, doc))
        {
            std::fprintf(stderr, "[document] failed at %d\n", __LINE__);
            return false;
        }
        ReleasePpo(&doc);
        if (!copy.FDelete() || !adopt.FDelete() || !second.FDelete())
        {
            std::fprintf(stderr, "[document] failed at %d\n", __LINE__);
            return false;
        }
    }
    std::fprintf(stderr, "[document] fresh rich document\n");
    auto fresh = RichTextDocument::PtxrdNew();
    if (!fresh || !DocumentStream(t, fresh) || !DocumentPath(t, fresh, nullptr))
    {
        std::fprintf(stderr, "[document] failed at %d\n", __LINE__);
        return false;
    }
    auto temp = ChunkyFile::PcflCreateTemp();
    ChunkIdentification root;
    bool ok = temp && fresh->FSaveToChunk(temp, &root) && t.Tree(temp, root);
    ReleasePpo(&fresh);
    ReleasePpo(&temp);
#ifdef _WIN64
#ifdef KAUAI_SDL3
    ok = ok && t.bytes == 1612 && t.hash == 0x85C06CCFU;
#else
    ok = ok && t.bytes == 1612 && t.hash == 0x88276D87U;
#endif
#endif
    return ok && t.Finish("Text document", false);
}
#pragma pop_macro("size")
#endif
