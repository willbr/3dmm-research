#ifndef HELP_BALLOON_FIXTURE_H
#define HELP_BALLOON_FIXTURE_H
#pragma push_macro("size")
#undef size
static bool HelpBalloonDescriptor(PChunkyFile file, ChunkNumber number, ChunkNumber fill)
{
    struct
    {
        GOKDF header;
        LOP location;
    } bytes = {{kboCur, koskCur, gokkRectangle}, {0, 0, 0, 0}};
    if (!file->FPutPv(&bytes, sizeof(bytes), kctgGokd, number))
        return false;
    for (int i = 0; i <= 0x10; ++i)
        if (!file->FAdoptChild(kctgGokd, number, kctgFill, fill, 0x10000 + i))
            return false;
    return true;
}
static bool HelpBalloonState(LiveTextTrace &t, PGraphicsObject node, unsigned depth = 0)
{
    RC rc;
    node->GetRc(&rc, cooParent);
    if (!t.Word(depth) || !t.Word(node->FIs(kclsBalloon)) || !t.Word(node->FIs(kclsBalloonButton)) ||
        !t.Word(node->FIs(kclsTopicGraphicsObject)) || !t.Word(node->FIs(kclsEditControlSingleLine)) || !t.Raw(&rc, 16))
        return false;
    unsigned children = 0;
    for (auto p = node->PgobFirstChild(); p; p = p->PgobNextSib())
        ++children;
    if (!t.Word(children))
        return false;
    for (auto p = node->PgobFirstChild(); p; p = p->PgobNextSib())
        if (!HelpBalloonState(t, p, depth + 1))
            return false;
    return true;
}
static bool TestHelpBalloon()
{
    LiveTextTrace t("MM_HELP_BALLOON_SNAPSHOT");
    auto before = GraphicsObject::PgobScreen();
    RC rc(0, 0, 140, 90);
    GraphicsObjectBlock block(0x7a300001, before, fgobNil, kginMark, &rc);
    auto root = NewObj GraphicsObject(&block);
    block.Set(0x7a300002, root, fgobNil, kginMark, &rc);
    auto world = NewObj HelpTraceWorld(&block);
    auto file = ChunkyFile::PcflCreateTemp();
    auto cache = file ? ChunkyResourceFile::PcrfNew(file, 0) : nullptr;
    if (!root || !world || !cache)
        return false;
    struct
    {
        short order, system;
        RC bounds;
        LegacyLong fore, back;
        byte pattern[8];
    } fill = {kboCur,     koskCur,    RC(-5, -8, 55, 32),
              0x01000000, 0x01ffffff, {0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff}};
    if (!file->FPutPv(&fill, sizeof(fill), kctgFill, 29) || !HelpBalloonDescriptor(file, 30, 29) ||
        !HelpBalloonDescriptor(file, 31, 29))
        return false;
    byte pixels[8] = {1, 2, 3, 4, 5, 6, 7, 8};
    RC image(0, 0, 4, 2);
    auto bitmap = MaskedBitmapMBMP::PmbmpNew(pixels, 4, 2, &image, 0, 0, 0);
    DataBlock data;
    if (!bitmap || !file->FPut(bitmap->CbOnFile(), kctgMbmp, 32, &data) || !bitmap->FWrite(&data) ||
        !file->FAdoptChild(kctgGokd, 31, kctgMbmp, 32, 0x10000))
        return false;
    ReleasePpo(&bitmap);
    auto first = NewObj HelpTextProbe(cache), second = NewObj HelpTextProbe(cache);
    if (!first || !second || !first->Init() || !second->Init())
        return false;
    Help::Topic info = {30, 0x7a300003, 0, cnoNil, 20, 13, {0x534e4449, 17}};
    first->SetHtop(&info);
    info.dxp = -90;
    info.dyp = 60;
    second->SetHtop(&info);
    if (!second->FGroupText(0, 4, 3, 99) || !second->FInsertButton(31, cnoNil, nullptr, 0, 1, 0))
        return false;
    Help::EditControl edit = {kctgEditControl, 18};
    if (!second->FInsertObject(&edit, sizeof(edit), 2, 0))
        return false;
    auto balloon = Help::Balloon::PhbalNew(world, world, cache, first);
    if (!balloon || !HelpBalloonState(t, balloon) || !t.Word(first->CactRef()))
        return false;
    if (!balloon->FSetTopic(second, &info, cache) || !HelpBalloonState(t, balloon) || !t.Word(first->CactRef()) ||
        !t.Word(second->CactRef()))
        return false;
    for (auto p = balloon->PgobFirstChild()->PgobFirstChild(); p; p = p->PgobNextSib())
        if (p->FIs(kclsBalloonButton))
        {
            auto button = static_cast<Help::PBalloonButton>(p);
            for (int x : {-4, 0, 2, 20})
                for (int y : {-3, 0, 2, 20})
                    if (!t.Word(button->FPtIn(x, y)))
                        return false;
            CMD_MOUSE command = {};
            command.grfcust = 7;
            if (!t.Word(button->FCmdClicked(&command)) || !HelpInteractionEvents(t, world))
                return false;
        }
    Help::Topic overlay = {cnoNil, info.hidThis, 0, cnoNil, -30, -20, {0, cnoNil}};
    balloon = Help::Balloon::PhbalNew(world, world, cache, first, &overlay);
    if (!balloon || !HelpBalloonState(t, balloon) || !t.Word(second->CactRef()))
        return false;
    balloon->Release();
    ChunkIdentification saved;
    if (!first->FSaveToChunk(file, &saved))
        return false;
    balloon = Help::Balloon::PhbalCreate(world, world, cache, saved.cno, &overlay);
    if (!balloon || !HelpBalloonState(t, balloon))
        return false;
    balloon->Release();
    auto button = Help::BalloonButton::PhbtnNew(world, world, 0x7a300004, 31, cache, 0, cnoNil, 12, 24);
    if (!button || !HelpBalloonState(t, button) || !t.Word(button->FPtIn(1, 1)))
        return false;
    button->Release();
    ReleasePpo(&first);
    ReleasePpo(&second);
    ReleasePpo(&cache);
    ReleasePpo(&file);
    root->Release();
    bool golden = true;
#ifdef _WIN64
    golden = t.bytes == 540 && t.hash == 0xEB200D33U;
#endif
    return GraphicsObject::PgobScreen() == before && t.Finish("Help balloon", false) && golden;
}
#pragma pop_macro("size")
#endif
