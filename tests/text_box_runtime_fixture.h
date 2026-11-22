#ifndef TEXT_BOX_RUNTIME_FIXTURE_H
#define TEXT_BOX_RUNTIME_FIXTURE_H
#pragma push_macro("size")
#undef size
// Native engine evidence for viewless frame/lifetime operations. View creation
// and destruction are checked separately with C providers, without UI input.
static bool TestTextBoxRuntime(PScene scene, PChunkyFile file, ChunkIdentification text)
{
    LiveTextTrace trace("MM_TEXT_BOX_RUNTIME_SNAPSHOT");
    auto resources = ChunkyResourceFile::PcrfNew(file, 0);
    if (!resources) return false;
    bool ok = true;
    const long ranges[][2] = {{-2,-3}, {1,1}, {1,5}, {10,5}, {1,klwMax}};
    for (auto &range : ranges)
        for (int story = 0; story != 2 && ok; ++story)
        {
            TextBoxData data;
            data.state.first = range[0]; data.state.end = range[1]; data.state.story = story != 0;
            data.SetBounds(10,20,180,100);
            ChunkNumber number;
            if (!file->FAdd(0, kctgTbox, &number) || !data.FWrite(file, number) ||
                !file->FAdoptChild(kctgTbox, number, text.ctg, text.cno, 0)) { ok = false; break; }
            auto box = TextBox::PtboxRead(resources->CoreCache(), number, scene);
            if (!box) { ok = false; break; }
            auto snapshot = [&]() {
                RC bounds;
                box->GetRc(&bounds);
                long first = -99, last = -98;
                bool lifetime = box->FGetLifetime(&first, &last);
                return trace.Word(box->NfrmFirst()) && trace.Word(box->nfrmMax()) &&
                    trace.Word(box->FIsVisible()) && trace.Word(box->FStory()) && trace.Word(box->FSelected()) &&
                    trace.Word(box->Pscen() == scene) && trace.Word(box->Cddg()) && trace.Word(lifetime) &&
                    trace.Word(first) && trace.Word(last) && trace.Word(bounds.xpLeft) && trace.Word(bounds.ypTop) &&
                    trace.Word(bounds.xpRight) && trace.Word(bounds.ypBottom);
            };
            ok = snapshot();
            box->Select(true); ok = ok && snapshot();
            box->Select(true); box->Select(false);
            box->SetTypeCore(!story); box->AttachToMouse();
            ok = ok && !box->FTextSelected() && !box->FNeedToScroll() && snapshot();
            RC changed(-7,3,120,90);
            box->SetRc(&changed); box->SetRc(&changed);
            ok = ok && snapshot() && box->FGotoFrame(range[0] - 1) && snapshot();
            if (range[0] == 10)
            {
                ok = ok && box->FGotoFrame(7) && box->FShowCore() && snapshot();
                box->HideCore(); ok = ok && snapshot();
            }
            else if (range[1] != klwMax)
                ok = ok && box->FGotoFrame(range[1]) && snapshot();
            box->SetStartFrame(range[0] + 2); ok = ok && snapshot();
            box->SetScen(scene); ok = ok && snapshot();
            ReleasePpo(&box);
        }
    ReleasePpo(&resources);
    std::printf("Text-box runtime trace: bytes=%u fingerprint=%08X\n", trace.bytes, trace.hash);
    return ok && trace.bytes == 4480 && trace.hash == 0xFB2BB26DU;
}
#pragma pop_macro("size")
#endif
