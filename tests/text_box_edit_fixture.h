#ifndef TEXT_BOX_EDIT_FIXTURE_H
#define TEXT_BOX_EDIT_FIXTURE_H
#pragma push_macro("size")
#undef size
// Register a viewless, invisible document without running the scene's visible
// Add command. This isolates document transactions from window construction.
struct TextBoxEditScene : Scene
{
    static bool Register(PScene scene, PTBOX box)
    {
        auto &owner = scene->*&TextBoxEditScene::_owner;
        auto list = static_cast<PDynamicArray>(owner.owned[MM_SCENE_TEXTS]);
        if (!list->FAdd(&box)) return false;
        box->AddRef();
        return true;
    }
    static void Unregister(PScene scene, PTBOX box)
    {
        scene->Pmvie()->ClearUndo();
        scene->SelectTbox(nullptr);
        auto &owner = scene->*&TextBoxEditScene::_owner;
        auto list = static_cast<PDynamicArray>(owner.owned[MM_SCENE_TEXTS]);
        for (long i = 0; i < list->IvMac(); ++i)
        {
            PTBOX found;
            list->Get(i, &found);
            if (found == box) { list->Delete(i); box->Release(); break; }
        }
    }
};
static bool TestTextBoxEdit(PScene scene, PChunkyFile file)
{
    LiveTextTrace trace("MM_TEXT_BOX_EDIT_SNAPSHOT");
    auto snapshot = [&](PTBOX box) {
        RC bounds; box->GetRc(&bounds);
        CHP style; box->FetchChp(0, &style);
        std::vector<achar> text(box->CpMac()); box->FetchRgch(0, box->CpMac(), text.data());
        return trace.Word(box->CpMac()) && trace.Raw(text.data(), text.size() * sizeof(achar)) &&
            trace.Word(box->Pscen() != nullptr) && trace.Word(box->FStory()) && trace.Word(box->FSelected()) &&
            trace.Word(box->NfrmFirst()) && trace.Word(box->nfrmMax()) && trace.Word(box->AcrBack().LwGet()) &&
            trace.Word(bounds.xpLeft) && trace.Word(bounds.ypTop) && trace.Word(bounds.xpRight) &&
            trace.Word(bounds.ypBottom) && trace.Word(style.grfont) && trace.Word(style.dypFont) &&
            trace.Word(style.dypOffset) && trace.Word(style.acrFore.LwGet());
    };
    bool ok = true;
    for (int variant = 0; variant < 4 && ok; ++variant)
    {
        RC bounds(-7,3,120 + variant,90);
        auto source = TextBox::PtboxNew(nullptr, &bounds, variant % 2 != 0);
        PTBOX copy = nullptr;
        if (!source) return false;
        source->SuspendUndo();
        achar text[] = {'A', achar(0xe9), 0, '\r', '\n', 'V'};
        ok = source->FReplaceRgch(text, variant + 2, 0, 0);
        source->ResumeUndo();
        source->SetDypFontDef(17 + variant);
        source->SetAcrBack(variant % 2 ? kacrYellow : kacrBlue, fdocNil);
        ok = ok && snapshot(source) && source->FDup(&copy) && copy && snapshot(copy);
        if (copy)
        {
            ok = ok && copy->FSetType(copy->FStory());
            CHP value = {};
            ulong flags = 99;
            copy->FetchChpSel(&value, &flags);
            ok = ok && trace.Word(flags) && trace.Word(copy->FSetAcrText(kacrRed)) &&
                trace.Word(copy->FSetDypFontText(20)) && trace.Word(copy->FSetStyleText(fontBold)) &&
                trace.Word(copy->FSetOnnText(0));
            auto clipboard = TextBoxClipboard::PtclpNew(copy); // adopts this reference
            ok = ok && clipboard && trace.Word(clipboard && clipboard->FIs(kclsTextBoxClipboard));
            if (clipboard) { copy = nullptr; ReleasePpo(&clipboard); }
        }
        ReleasePpo(&copy); ReleasePpo(&source);
    }
    // Invalid lifetime keeps this registered document invisible during undo.
    auto resources = ChunkyResourceFile::PcrfNew(file, 0);
    auto box = LiveCaptionInput(scene, file, {'x','y'}, 0);
    if (!resources || !box) { ReleasePpo(&resources); ReleasePpo(&box); return false; }
    // HideCore sets end to first-1; moving first forward leaves an invalid lifetime.
    box->HideCore(); box->SetStartFrame(scene->Nfrm() + 10);
    if (!TextBoxEditScene::Register(scene, box)) { ReleasePpo(&resources); ReleasePpo(&box); return false; }
    auto movie = scene->Pmvie();
    movie->ClearUndo();
    ok = ok && snapshot(box) && box->FSetType(false) && snapshot(box);
    ok = ok && movie->FUndo() && snapshot(box) && movie->FRedo() && snapshot(box);
    movie->ClearUndo();
    ok = ok && box->FSetAcrBack(kacrRed) && snapshot(box);
    ok = ok && movie->FUndo() && snapshot(box) && movie->FRedo() && snapshot(box);
    movie->ClearUndo();
    achar inserted = 'Z';
    ok = ok && box->FReplaceRgch(&inserted, 1, 0, 0) && snapshot(box);
    ok = ok && movie->FUndo() && snapshot(box) && movie->FRedo() && snapshot(box);
    TextBoxEditScene::Unregister(scene, box);
    ReleasePpo(&box); ReleasePpo(&resources);
    std::printf("Text-box edit trace: bytes=%u fingerprint=%08X\n", trace.bytes, trace.hash);
    return ok && trace.bytes == 1244 && trace.hash == 0xE86E47B5U;
}
#pragma pop_macro("size")
#endif
