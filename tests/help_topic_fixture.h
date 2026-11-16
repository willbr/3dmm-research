#ifndef HELP_TOPIC_FIXTURE_H
#define HELP_TOPIC_FIXTURE_H
#pragma push_macro("size")
#undef size
struct HelpTopicWorld : HelpTraceWorld
{
    bool collide = false;
    HelpTopicWorld(PGraphicsObjectBlock b) : HelpTraceWorld(b)
    {
    }
    long Base()
    {
        if (!*Ppglrtvm())
            return 0;
        String name(PszLit("_gidBase"));
        RuntimeVariableName key;
        key.SetFromStn(&name);
        LegacyLong value = 0;
        FFindRtvm(*Ppglrtvm(), &key, &value, nullptr);
        return value;
    }
    PCommandHandler PcmhFromHid(long id) override
    {
        if (collide && id == Base() + 3)
            return this;
        return HelpTraceWorld::PcmhFromHid(id);
    }
};
struct HelpTopicProbe : Help::TopicGraphicsObject
{
    HelpTopicProbe(HelpTopicWorld *w, Help::PTextDocument d, PGraphicsObjectBlock b) : TopicGraphicsObject(w, d, b)
    {
    }
    bool Init()
    {
        return TopicGraphicsObject::_FInit();
    }
    void _FetchChp(long cp, PCHP chp, long *first = nullptr, long *end = nullptr) override
    {
        RichTextDocumentGraphicsObject::_FetchChp(cp, chp, first, end);
        chp->dypOffset = -2;
        chp->dypFont = 12;
        chp->acrBack = cp & 1 ? kacrClear : kacrBlue;
        chp->acrFore = kacrRed;
    }
};
static bool TestHelpTopic()
{
    LiveTextTrace t("MM_HELP_TOPIC_SNAPSHOT");
    auto before = GraphicsObject::PgobScreen();
    RC bounds(0, 0, 140, 90);
    GraphicsObjectBlock block(0x7a400001, before, fgobNil, kginMark, &bounds);
    auto root = NewObj GraphicsObject(&block);
    block.Set(0x7a400002, root, fgobNil, kginMark, &bounds);
    auto world = NewObj HelpTopicWorld(&block);
    auto file = ChunkyFile::PcflCreateTemp();
    auto cache = file ? ChunkyResourceFile::PcrfNew(file, 0) : nullptr;
    if (!world || !cache)
        return false;
    struct
    {
        short order, system;
        RC bounds;
        LegacyLong fore, back;
        byte pattern[8];
    } fill = {kboCur,     koskCur,    RC(0, 0, 8, 5),
              0x01000000, 0x01ffffff, {0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff}};
    if (!file->FPutPv(&fill, sizeof(fill), kctgFill, 29) || !HelpBalloonDescriptor(file, 31, 29))
        return false;
    byte pixels[8] = {1, 2, 3, 4, 5, 6, 7, 8};
    RC image(0, 0, 4, 2);
    auto bitmap = MaskedBitmapMBMP::PmbmpNew(pixels, 4, 2, &image, 0, 0, 0);
    DataBlock data;
    if (!bitmap || !file->FPut(bitmap->CbOnFile(), kctgMbmp, 32, &data) || !bitmap->FWrite(&data) ||
        !file->FAdoptChild(kctgGokd, 31, kctgMbmp, 32, 0x10000))
        return false;
    ReleasePpo(&bitmap);
    for (int mode = 0; mode < 3; ++mode)
    {
        auto doc = NewObj HelpTextProbe(cache);
        if (!doc || !doc->Init())
            return false;
        if (mode)
        {
            Help::EditControl edit = {kctgEditControl, 18};
            if (!doc->FInsertPicture(32, nullptr, 0, 0, 0) || !doc->FInsertButton(31, 77, nullptr, 0, 1, 0) ||
                !doc->FInsertButton(31, cnoNil, nullptr, 0, 2, 0) || !doc->FInsertObject(&edit, sizeof(edit), 3, 0) ||
                !doc->FInsertObject(&edit, sizeof(edit), 4, 0) || !doc->FGroupText(1, 4, 3, 99) ||
                !doc->FGroupText(6, 7, 9))
                return false;
        }
        world->collide = mode == 2;
        block.Set(0x7a400003, world, fgobNil, kginMark, &bounds);
        Help::PTopicGraphicsObject view;
        if (!mode)
            view = Help::TopicGraphicsObject::PtxhgNew(world, doc, &block);
        else
        {
            auto p = NewObj HelpTopicProbe(world, doc, &block);
            if (!p || !p->Init())
                return false;
            view = p;
        }
        if (!view || !HelpBalloonState(t, view) || !t.Word(doc->CactRef()) || !t.Word(world->Base() != 0))
            return false;
        for (auto p = view->PgobFirstChild(); p; p = p->PgobNextSib())
        {
            if (!t.Word(p->Hid() - world->Base()))
                return false;
            if (p->FIs(kclsBalloonButton))
            {
                CMD_MOUSE cmd = {};
                cmd.grfcust = 7;
                if (!static_cast<Help::PBalloonButton>(p)->FCmdClicked(&cmd))
                    return false;
            }
        }
        if (!HelpInteractionEvents(t, world))
            return false;
        ReleasePpo(&view);
        if (!t.Word(doc->CactRef()))
            return false;
        ReleasePpo(&doc);
    }
    ReleasePpo(&cache);
    ReleasePpo(&file);
    root->Release();
    bool golden = true;
#ifdef _WIN64
    golden = t.bytes == 552 && t.hash == 0x8AB0C61FU;
#endif
    return GraphicsObject::PgobScreen() == before && t.Finish("Help topic", false) && golden;
}
#pragma pop_macro("size")
#endif
