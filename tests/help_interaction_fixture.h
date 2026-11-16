#ifndef HELP_INTERACTION_FIXTURE_H
#define HELP_INTERACTION_FIXTURE_H
#include "kidworld.h"
#include "screxeg.h"
#pragma push_macro("size")
#undef size
struct HelpTraceTracking : CommandExecutionManager
{
    PGraphicsObject tracked = nullptr;
    std::vector<unsigned> *events;
    bool Init()
    {
        return _FInit(20, 20);
    }
    void TrackMouse(PGraphicsObject p) override
    {
        events->push_back(10);
        tracked = p;
    }
    void EndMouseTracking() override
    {
        events->push_back(11);
        tracked = nullptr;
    }
    PGraphicsObject PgobTracking() override
    {
        return tracked;
    }
};
struct HelpTraceWorld;
struct HelpTraceInterpreter : ScriptInterpreter::GraphicsObjectInterpreter
{
    HelpTraceWorld *world;
    PGraphicsObject target;
    HelpTraceInterpreter(HelpTraceWorld *, PResourceCache, PGraphicsObject);
    ~HelpTraceInterpreter();
    bool FRunScript(ScriptInterpreter::PScript, LegacyLong *, LegacyLong, LegacyLong *, bool *) override;
};
struct HelpTraceWorld : WorldOfKidspace
{
    std::vector<unsigned> events;
    int mode = 0;
    HelpTraceWorld(PGraphicsObjectBlock p) : WorldOfKidspace(p)
    {
    }
    ulong GrfcustAdjust(ulong flags) override
    {
        events.push_back(20);
        events.push_back(flags);
        return flags ^ 0x400;
    }
    Help::PBalloon PhbalNew(PGraphicsObject, PResourceCache, ChunkNumber number, Help::PTopic) override
    {
        events.push_back(21);
        events.push_back(number);
        return nullptr;
    }
    PGraphicsObjectInterpreter PscegNew(PResourceCache cache, PGraphicsObject target) override
    {
        events.push_back(30);
        return mode == 1 ? nullptr : NewObj HelpTraceInterpreter(this, cache, target);
    }
};
inline HelpTraceInterpreter::HelpTraceInterpreter(HelpTraceWorld *w, PResourceCache c, PGraphicsObject p)
    : GraphicsObjectInterpreter(w, c, p), world(w), target(p)
{
}
inline HelpTraceInterpreter::~HelpTraceInterpreter()
{
    world->events.push_back(33);
}
inline bool HelpTraceInterpreter::FRunScript(ScriptInterpreter::PScript, LegacyLong *args, LegacyLong count,
                                             LegacyLong *out, bool *)
{
    world->events.push_back(31);
    world->events.push_back(count);
    for (long i = 0; i < count; ++i)
        world->events.push_back(args[i]);
    if (out)
        *out = 55;
    if (world->mode == 3)
        target->Release();
    return world->mode != 2;
}
struct HelpInteractionProbe : Help::TopicGraphicsObject
{
    HelpTraceWorld *world;
    bool group_found = true, virtual_result = true;
    byte group = 7;
    ChunkNumber topic = 99;
    LegacyLong value = 0;
    CMD_MOUSE *command = nullptr;
    HelpInteractionProbe(HelpTraceWorld *w, Help::PTextDocument d, PGraphicsObjectBlock b)
        : TopicGraphicsObject(w, d, b), world(w)
    {
    }
    bool Init()
    {
        _interaction.base = 1000;
        return RichTextDocumentGraphicsObject::_FInit();
    }
    bool State(LiveTextTrace &t)
    {
        return t.Word(_interaction.group) && t.Word(_interaction.topic) && t.Word(_interaction.flags) &&
               t.Word(_interaction.base);
    }
    bool FGroupFromPt(long x, long y, byte *g = nullptr, ChunkNumber *n = nullptr) override
    {
        world->events.push_back(40);
        world->events.push_back(x);
        world->events.push_back(y);
        if (g)
            *g = group;
        if (n)
            *n = topic;
        return group_found;
    }
    void SetCursor(ulong flags) override
    {
        world->events.push_back(41);
        world->events.push_back(flags);
        if (command)
            command->grfcust ^= 0x800;
    }
    bool _FRunScript(byte group, ulong flags, long id, achar ch, ChunkNumber number, LegacyLong *out) override
    {
        world->events.push_back(42);
        world->events.push_back(group);
        world->events.push_back(flags);
        world->events.push_back(id);
        world->events.push_back(ch);
        world->events.push_back(number);
        if (out)
            *out = value;
        return virtual_result;
    }
    bool Run(byte g, ulong flags, long id, achar ch, ChunkNumber n, LegacyLong *out)
    {
        return TopicGraphicsObject::_FRunScript(g, flags, id, ch, n, out);
    }
};
static bool HelpInteractionEvents(LiveTextTrace &t, HelpTraceWorld *w)
{
    if (!t.Word(unsigned(w->events.size())))
        return false;
    for (auto n : w->events)
        if (!t.Word(n))
            return false;
    w->events.clear();
    return true;
}
static bool TestHelpInteraction()
{
    LiveTextTrace t("MM_HELP_INTERACTION_SNAPSHOT");
    auto previous = vpcex;
    auto tracking = NewObj HelpTraceTracking;
    if (!tracking || !tracking->Init())
        return false;
    vpcex = tracking;
    auto before = GraphicsObject::PgobScreen();
    RC rc(0, 0, 100, 80);
    GraphicsObjectBlock block(0x7a200001, before, fgobNil, kginMark, &rc);
    auto root = NewObj GraphicsObject(&block);
    block.Set(0x7a200002, root, fgobNil, kginMark, &rc);
    auto world = NewObj HelpTraceWorld(&block);
    tracking->events = &world->events;
    auto file = ChunkyFile::PcflCreateTemp();
    auto cache = file ? ChunkyResourceFile::PcrfNew(file, 0) : nullptr;
    auto doc = cache ? NewObj HelpTextProbe(cache) : nullptr;
    if (!world || !doc || !doc->Init())
        return false;
    block.Set(0x7a200003, world, fgobNil, kginMark, &rc);
    auto view = NewObj HelpInteractionProbe(world, doc, &block);
    if (!view || !view->Init())
        return false;
    for (int mode = 0; mode < 5; ++mode)
    {
        view->group_found = mode != 0;
        view->group = 7;
        view->topic = 99;
        CMD_MOUSE mouse = {};
        mouse.cid = cidMouseDown;
        mouse.xp = 12;
        mouse.yp = 13;
        mouse.grfcust = fcustMouse | 2;
        view->command = mode == 4 ? &mouse : nullptr;
        if (!t.Word(view->FCmdTrackMouse(&mouse)) || !t.Word(mouse.grfcust) || !view->State(t) ||
            !HelpInteractionEvents(t, world))
            return false;
        if (mode == 0)
            continue;
        mouse.cid = cidTrackMouse;
        if (!t.Word(view->FCmdTrackMouse(&mouse)) || !HelpInteractionEvents(t, world))
            return false;
        mouse.grfcust = 2;
        if (mode == 2)
            view->group = 8;
        if (mode == 3)
            view->topic = 100;
        if (!t.Word(view->FCmdTrackMouse(&mouse)) || !t.Word(mouse.grfcust) || !HelpInteractionEvents(t, world))
            return false;
    }
    view->command = nullptr;
    for (long id : {999L, 1000L, 1001L, 1256L, 1257L})
    {
        CMD_BADKEY key = {};
        key.hid = id;
        key.ch = 0x1234;
        key.grfcust = 9;
        if (!t.Word(view->FCmdBadKey(&key)) || !t.Word(key.grfcust) || !HelpInteractionEvents(t, world))
            return false;
    }
    for (int mode = 0; mode < 4; ++mode)
    {
        view->group_found = mode & 1;
        CMD_MOUSE mouse = {};
        mouse.xp = 4;
        mouse.yp = 5;
        mouse.grfcust = 6;
        if (!t.Word(view->FCmdMouseMove(&mouse)) || !t.Word(mouse.grfcust) || !HelpInteractionEvents(t, world))
            return false;
        view->virtual_result = mode != 0;
        view->value = mode == 2 ? 12 : 0;
        view->DoHit(3, mode == 3 ? cnoNil : 99, 5, 6);
        if (!HelpInteractionEvents(t, world))
            return false;
    }
    ReleasePpo(&view);
    auto words = DynamicArray::PglNew(4);
    LegacyLong zero = 0;
    DataBlock data;
    if (!words || !words->FAdd(&zero) || !file->FPut(words->CbOnFile(), kctgScript, 17, &data) || !words->FWrite(&data))
        return false;
    ReleasePpo(&words);
    for (int mode = 0; mode < 6; ++mode)
    {
        Help::Topic topic;
        doc->GetHtop(&topic);
        topic.cnoScript = mode == 0 ? cnoNil : mode == 5 ? 99999 : 17;
        doc->SetHtop(&topic);
        view = NewObj HelpInteractionProbe(world, doc, &block);
        if (!view || !view->Init())
            return false;
        world->mode = mode;
        LegacyLong out = -77;
        bool alive = view->Run(255, 0x87654321, -17, achar(0x1234), 0xabcdef12, &out);
        if (!t.Word(alive) || !t.Word(out) || !HelpInteractionEvents(t, world))
            return false;
        if (mode == 3)
            view = nullptr;
        ReleasePpo(&view);
    }
    ReleasePpo(&doc);
    ReleasePpo(&cache);
    ReleasePpo(&file);
    root->Release();
    vpcex = previous;
    tracking->Release();
    bool golden = true;
#ifdef _WIN64
    golden = t.bytes == 1148 && t.hash == 0xDDC10C3CU;
#endif
    return GraphicsObject::PgobScreen() == before && t.Finish("Help interaction", false) && golden;
}
#pragma pop_macro("size")
#endif
