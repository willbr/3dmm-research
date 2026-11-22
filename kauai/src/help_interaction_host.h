#ifndef HELP_INTERACTION_HOST_H
#define HELP_INTERACTION_HOST_H
struct TopicGraphicsObject::InteractionHost : TextDocumentFactoryHost
{
    TopicGraphicsObject *topic;
    PCMD_MOUSE mouse = nullptr;
    PCMD_BADKEY key = nullptr;
    byte *groupOutput = nullptr;
    ChunkNumber *topicOutput = nullptr;
    LegacyLong *returnOutput = nullptr;
    PTextDocument document = nullptr;
    PResourceCache cache = nullptr;
    PWorldOfKidspace world = nullptr;
    long grid = 0;
    explicit InteractionHost(TopicGraphicsObject *p) : topic(p)
    {
    }
    MMHelpInteractionServices Commands()
    {
        MMHelpInteractionServices s = {};
        s.context = this;
        s.mouse = [](void *v, MMHelpMouse *out) {
            auto p = static_cast<InteractionHost *>(v)->mouse;
            *out = {MMI32(p->xp), MMI32(p->yp), MMU32(p->grfcust), p->cid == cidMouseDown};
        };
        s.key = [](void *v, MMHelpKey *out) {
            auto p = static_cast<InteractionHost *>(v)->key;
            *out = {MMI32(p->hid), MMU32(p->grfcust), MMU32(p->ch)};
        };
        s.publish_flags = [](void *v, MMU32 flags) {
            auto &h = *static_cast<InteractionHost *>(v);
            if (h.key)
                h.key->grfcust = mm_signed_word(flags);
            else
                h.mouse->grfcust = mm_signed_word(flags);
        };
        s.adjust = [](void *v, MMU32 flags, MMU32 *out) -> int {
            auto &h = *static_cast<InteractionHost *>(v);
            return h.Guard([&] {
                *out = h.topic->_pwoks->GrfcustAdjust(flags);
                return 1;
            });
        };
        s.hit = [](void *v, MMI32 x, MMI32 y, unsigned *g, MMU32 *t) -> int {
            auto &h = *static_cast<InteractionHost *>(v);
            byte group = g ? byte(*g) : 0;
            ChunkNumber target = t ? *t : 0;
            int r = h.Guard([&] { return h.topic->FGroupFromPt(x, y, g ? &group : nullptr, t ? &target : nullptr); });
            if (g)
                *g = group;
            if (t)
                *t = target;
            return r;
        };
        s.check_tracking = [](void *v, int down) {
            auto &h = *static_cast<InteractionHost *>(v);
            if (down)
            {
                Assert(vpcex->PgobTracking() == nullptr, "mouse already being tracked!");
            }
            else
            {
                Assert(vpcex->PgobTracking() == h.topic, "not tracking mouse!");
                Assert(h.mouse->cid == cidTrackMouse, 0);
            }
        };
        s.track = [](void *v, int start) -> int {
            auto &h = *static_cast<InteractionHost *>(v);
            return h.Guard([&] {
                if (start)
                    vpcex->TrackMouse(h.topic);
                else
                    vpcex->EndMouseTracking();
                return 1;
            });
        };
        s.cursor = [](void *v, MMU32 flags) -> int {
            auto &h = *static_cast<InteractionHost *>(v);
            return h.Guard([&] {
                h.topic->SetCursor(flags);
                return 1;
            });
        };
        s.run = [](void *v, unsigned group, MMU32 flags, MMI32 id, MMU32 ch, MMU32 target, MMI32 *out) -> int {
            auto &h = *static_cast<InteractionHost *>(v);
            LegacyLong value = out ? *out : 0;
            int r = h.Guard([&] {
                return h.topic->_FRunScript(byte(group), flags, id, achar(ch), target, out ? &value : nullptr);
            });
            if (out)
                *out = value;
            return r;
        };
        s.do_hit = [](void *v, unsigned group, MMU32 target, MMU32 flags, MMI32 id) -> int {
            auto &h = *static_cast<InteractionHost *>(v);
            return h.Guard([&] {
                h.topic->DoHit(byte(group), target, flags, id);
                return 1;
            });
        };
        s.balloon = [](void *v, MMU32 target) -> int {
            auto &h = *static_cast<InteractionHost *>(v);
            return h.Guard([&] {
                auto p = h.topic;
                p->_pwoks->PhbalNew(p->PgobPar()->PgobPar(), p->Ptxhd()->Prca(), target);
                return 1;
            });
        };
        s.base_point = [](void *v, MMI32 x, MMI32 y) -> int {
            auto &h = *static_cast<InteractionHost *>(v);
            return h.Guard([&] { return h.topic->RichTextDocumentGraphicsObject::FPtIn(x, y); });
        };
        s.locate = [](void *v, MMI32 x, MMI32 y, MMI32 *out) -> int {
            auto &h = *static_cast<InteractionHost *>(v);
            return h.Guard([&] {
                long cp;
                if (!h.topic->_FGetCpFromPt(x, y, &cp, false))
                    return false;
                *out = MMI32(cp);
                return true;
            });
        };
        s.group_at = [](void *v, MMI32 cp) -> int {
            auto &h = *static_cast<InteractionHost *>(v);
            return h.Guard(
                [&] { return h.topic->Ptxhd()->FGrouped(cp, nullptr, nullptr, h.groupOutput, h.topicOutput); });
        };
        s.parent = [](void *, void *p) -> void * { return static_cast<PGraphicsObject>(p)->PgobPar(); };
        s.kid = [](void *, void *p) -> int { return static_cast<PGraphicsObject>(p)->FIs(kclsKidspaceGraphicObject); };
        s.set_cursor = [](void *v, void *p, MMU32 flags) -> int {
            auto &h = *static_cast<InteractionHost *>(v);
            return h.Guard([&] {
                if (p)
                    static_cast<GraphicalObjectRepresentation::PKidspaceGraphicObject>(p)->SetCursor(flags);
                else
                    vpappb->SetCurs(nullptr);
                return 1;
            });
        };
        return s;
    }
    MMHelpScriptServices Script()
    {
        document = topic->Ptxhd();
        cache = document->Prca();
        MMHelpScriptServices s = {};
        s.context = this;
        s.reset_return = [](void *v) {
            auto p = static_cast<InteractionHost *>(v)->returnOutput;
            if (p)
                *p = 0;
        };
        s.number = [](void *v, MMU32 *out) -> int {
            auto &h = *static_cast<InteractionHost *>(v);
            return h.Guard([&] {
                Topic info;
                h.document->GetHtop(&info);
                *out = info.cnoScript;
                return 1;
            });
        };
        s.fetch = [](void *v, MMU32 number, void **out) -> int {
            auto &h = *static_cast<InteractionHost *>(v);
            return h.Guard([&] {
                *out = ScriptInterpreter::FetchScript(h.cache->Resources(), kctgScript, number);
                return *out != nullptr;
            });
        };
        s.create = [](void *v, void **out) -> int {
            auto &h = *static_cast<InteractionHost *>(v);
            return h.Guard([&] {
                *out = h.topic->_pwoks->PscegNew(h.cache, h.topic);
                return *out != nullptr;
            });
        };
        s.snapshot = [](void *v) {
            auto &h = *static_cast<InteractionHost *>(v);
            h.world = h.topic->_pwoks;
            h.grid = h.topic->Grid();
        };
        s.run = [](void *v, void *interpreter, void *script, const MMI32 *args) -> int {
            auto &h = *static_cast<InteractionHost *>(v);
            LegacyLong values[5] = {args[0], args[1], args[2], args[3], args[4]};
            return h.Guard([&] {
                return static_cast<PGraphicsObjectInterpreter>(interpreter)
                    ->FRunScript(static_cast<PScript>(script), values, 5, h.returnOutput);
            });
        };
        s.release = [](void *, int interpreter, void *p) {
            if (interpreter)
                static_cast<PGraphicsObjectInterpreter>(p)->Release();
            else
                mm_script_release(static_cast<PScript>(p));
        };
        s.alive = [](void *v) -> int {
            auto &h = *static_cast<InteractionHost *>(v);
            return h.world->PgobFromGrid(h.grid) == h.topic;
        };
        return s;
    }
};
#endif
