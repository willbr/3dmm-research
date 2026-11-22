#ifndef HELP_BALLOON_HOST_H
#define HELP_BALLOON_HOST_H
static MMHelpTopic BalloonTopicData(const Topic &p)
{
    return {p.cnoBalloon, p.hidThis, p.hidTarget, p.cnoScript, p.dxp, p.dyp, p.ckiSnd.ctg, p.ckiSnd.cno};
}
static Topic BalloonTopicNative(const MMHelpTopic &p)
{
    return {p.balloon, p.hid, p.target, p.script, p.width, p.height, {p.sound_type, p.sound_number}};
}
static MMMaskedRect BalloonRect(const RC &p)
{
    return {p.xpLeft, p.ypTop, p.xpRight, p.ypBottom};
}
static RC BalloonRect(const MMMaskedRect &p)
{
    return {p.left, p.top, p.right, p.bottom};
}
static void BalloonDiagnostic(void *, int kind)
{
    if (kind == MM_HELP_DUPLICATE)
    {
        Bug("command handler with this ID already exists");
    }
    else if (kind == MM_HELP_ENTER_DEAD)
    {
        Warn("Balloon immediately destroyed!");
    }
    else if (kind == MM_HELP_HIT_DEAD)
    {
        Warn("Balloon immediately destroyed 2!");
    }
    else if (kind == MM_HELP_BUTTON_DEAD)
    {
        Warn("KidspaceGraphicObject immediately destroyed!");
    }
    else
    {
        Bug("why isn't my parent a TopicGraphicsObject?");
    }
}
struct Balloon::BalloonHost : TextDocumentFactoryHost
{
    Balloon *owner = nullptr;
    PWorldOfKidspace world = nullptr;
    PGraphicsObject parent = nullptr;
    PResourceCache cache = nullptr;
    PTextDocument document = nullptr;
    PTopic input = nullptr;
    PGraphicalObjectRepresentation representation = nullptr;
    long identity = 0;
    explicit BalloonHost(Balloon *p = nullptr) : owner(p)
    {
    }
    MMHelpBalloonFactoryServices Factory()
    {
        MMHelpBalloonFactoryServices s = {};
        s.context = this;
        s.source = [](void *v, MMHelpTopic *out) -> int {
            auto &h = *static_cast<BalloonHost *>(v);
            return h.Guard([&] {
                Topic p;
                h.document->GetHtop(&p);
                *out = BalloonTopicData(p);
                return 1;
            });
        };
        s.unique = [](void *v, MMI32 *out) -> int {
            auto &h = *static_cast<BalloonHost *>(v);
            return h.Guard([&] {
                *out = CommandHandler::HidUnique();
                return 1;
            });
        };
        s.find = [](void *v, MMI32 id, void **out) -> int {
            auto &h = *static_cast<BalloonHost *>(v);
            return h.Guard([&] {
                *out = h.world->PcmhFromHid(id);
                return *out != nullptr;
            });
        };
        s.is_balloon = [](void *, void *p) -> int { return static_cast<PCommandHandler>(p)->FIs(kclsBalloon); };
#ifdef REVIEW
        s.review_reuse = 1;
#endif
        s.same_source = [](void *v, void *p, MMU32 number) -> int {
            auto &h = *static_cast<BalloonHost *>(v);
            auto node = static_cast<PBalloon>(p);
            return mm_kidspace_owner_binding(node->_pgokd)->number == number && h.cache == node->_prca;
        };
        s.replace = [](void *v, void *p, const MMHelpTopic *info) -> int {
            auto &h = *static_cast<BalloonHost *>(v);
            return h.Guard([&] {
                auto t = BalloonTopicNative(*info);
                return static_cast<PBalloon>(p)->FSetTopic(h.document, &t, h.cache);
            });
        };
        s.create = [](void *v, MMI32 id, void **out) -> int {
            auto &h = *static_cast<BalloonHost *>(v);
            return h.Guard([&] {
                GraphicsObjectBlock b(id, h.parent, fgobNil, kginMark);
                *out = NewObj Balloon(&b);
                return *out != nullptr;
            });
        };
        s.snapshot = [](void *v, void *p) {
            static_cast<BalloonHost *>(v)->identity = static_cast<PBalloon>(p)->Grid();
        };
        s.initialize = [](void *v, void *p, const MMHelpTopic *info) -> int {
            auto &h = *static_cast<BalloonHost *>(v);
            return h.Guard([&] {
                auto t = BalloonTopicNative(*info);
                return static_cast<PBalloon>(p)->_FInit(h.world, h.document, &t, h.cache);
            });
        };
        s.enter = [](void *v, void *p) -> int {
            auto &h = *static_cast<BalloonHost *>(v);
            return h.Guard([&] { return static_cast<PBalloon>(p)->_FEnterState(ksnoInit); });
        };
        s.hit = [](void *v, void *p) -> int {
            auto &h = *static_cast<BalloonHost *>(v);
            return h.Guard([&] {
                static_cast<PTopicGraphicsObject>(static_cast<PBalloon>(p)->_balloon.topic)
                    ->DoHit(0, cnoNil, fcustNil, hidNil);
                return 1;
            });
        };
        s.alive = [](void *v, void *p) -> int {
            auto &h = *static_cast<BalloonHost *>(v);
            return h.world->PgobFromGrid(h.identity) == p;
        };
        s.release = [](void *, void *p) { static_cast<BASE *>(p)->Release(); };
        s.diagnostic = BalloonDiagnostic;
        s.validate = [](void *, void *p) { AssertPo(static_cast<PBalloon>(p), 0); };
        return s;
    }
    MMHelpBalloonReadServices Read()
    {
        MMHelpBalloonReadServices s = {};
        s.context = this;
        s.find = [](void *v, MMU32 n, void **out) -> int {
            auto &h = *static_cast<BalloonHost *>(v);
            return h.Guard([&] {
                *out = h.cache->FindChunk(kctgHelpTopic, n);
                return *out != nullptr;
            });
        };
        s.read = [](void *v, void *p, MMU32 n, void **out) -> int {
            auto &h = *static_cast<BalloonHost *>(v);
            return h.Guard([&] {
                *out = TextDocument::PtxhdReadChunk(h.cache, static_cast<MMResourceCache *>(p), kctgHelpTopic,
                                                    n, h.world->Pstrg());
                return *out != nullptr;
            });
        };
        s.hide = [](void *v, void *p) -> int {
            auto &h = *static_cast<BalloonHost *>(v);
            return h.Guard([&] {
                static_cast<PTextDocument>(p)->HideButtons();
                return 1;
            });
        };
        s.build = [](void *v, void *p, void **out) -> int {
            auto &h = *static_cast<BalloonHost *>(v);
            return h.Guard([&] {
                *out = Balloon::PhbalNew(h.world, h.parent, h.cache, static_cast<PTextDocument>(p), h.input);
                return *out != nullptr;
            });
        };
        s.release = [](void *, void *p) { static_cast<BASE *>(p)->Release(); };
        return s;
    }
    MMHelpBalloonStages Stages()
    {
        MMHelpBalloonStages s = {};
        s.context = this;
        s.base = [](void *v) -> int {
            auto &h = *static_cast<BalloonHost *>(v);
            return h.Guard(
                [&] { return h.owner->KidspaceGraphicObject::_FInit(h.world, h.input->cnoBalloon, h.cache); });
        };
        s.set = [](void *v) -> int {
            auto &h = *static_cast<BalloonHost *>(v);
            return h.Guard([&] { return h.owner->_FSetTopic(h.document, h.input, h.cache); });
        };
        s.enter = [](void *v) -> int {
            auto &h = *static_cast<BalloonHost *>(v);
            return h.Guard([&] { return h.owner->_FEnterState(ksnoInit); });
        };
        return s;
    }
    MMHelpBalloonTopicServices TopicServices()
    {
        MMHelpBalloonTopicServices s = {};
        s.context = this;
        s.create = [](void *v, void **out) -> int {
            auto &h = *static_cast<BalloonHost *>(v);
            return h.Guard([&] {
                GraphicsObjectBlock b(CommandHandler::HidUnique(), h.owner, fgobNil, kginMark);
                *out = TopicGraphicsObject::PtxhgNew(h.owner->_pwoks, h.document, &b);
                return *out != nullptr;
            });
        };
        s.assign = [](void *v, void *p, int number, MMU32 value) -> int {
            auto &h = *static_cast<BalloonHost *>(v);
            return h.Guard([&] {
                String name(number ? PszLit("_cnoSound") : PszLit("_ctgSound"));
                RuntimeVariableName key;
                key.SetFromStn(&name);
                return mm_script_variables_set(static_cast<PTopicGraphicsObject>(p)->ScriptVariables(),
                                               key.lu1, key.lu2, mm_signed_word(value));
            });
        };
        s.info = [](void *v, MMHelpTopic *out) { *out = BalloonTopicData(*static_cast<BalloonHost *>(v)->input); };
        s.release = [](void *, void *p) { static_cast<BASE *>(p)->Release(); };
        s.natural = [](void *v, void *p) -> int {
            auto &h = *static_cast<BalloonHost *>(v);
            return h.Guard([&] {
                static_cast<PTopicGraphicsObject>(p)->GetNaturalSize(&h.owner->_dxpPref, &h.owner->_dypPref);
                return 1;
            });
        };
        s.find = [](void *v, MMI32 id, void **out) -> int {
            auto &h = *static_cast<BalloonHost *>(v);
            return h.Guard([&] {
                *out = h.owner->_pwoks->PgobFromHid(id);
                return *out != nullptr;
            });
        };
        s.parent = [](void *v) -> void * { return static_cast<BalloonHost *>(v)->owner->PgobPar(); };
        s.kid = [](void *, void *p) -> int { return static_cast<PGraphicsObject>(p)->FIs(kclsKidspaceGraphicObject); };
        s.registration = [](void *v, void *p, MMHelpPoint *out) -> int {
            auto &h = *static_cast<BalloonHost *>(v);
            return h.Guard([&] {
                PT pt;
                static_cast<GraphicalObjectRepresentation::PKidspaceGraphicObject>(p)->GetPtReg(&pt);
                *out = {MMI32(pt.xp), MMI32(pt.yp)};
                return 1;
            });
        };
        s.bounds = [](void *v, void *p, MMMaskedRect *out) -> int {
            auto &h = *static_cast<BalloonHost *>(v);
            return h.Guard([&] {
                RC rc;
                static_cast<PGraphicsObject>(p)->GetRc(&rc, cooParent);
                *out = BalloonRect(rc);
                return 1;
            });
        };
        s.map_target = [](void *v, void *p, MMHelpPoint *point) -> int {
            auto &h = *static_cast<BalloonHost *>(v);
            return h.Guard([&] {
                PT pt(point->x, point->y);
                static_cast<PGraphicsObject>(p)->MapPt(&pt, cooParent, cooGlobal);
                *point = {MMI32(pt.xp), MMI32(pt.yp)};
                return 1;
            });
        };
        s.map_parent = [](void *v, MMHelpPoint *point) -> int {
            auto &h = *static_cast<BalloonHost *>(v);
            return h.Guard([&] {
                PT pt(point->x, point->y);
                h.owner->PgobPar()->MapPt(&pt, cooGlobal, cooLocal);
                *point = {MMI32(pt.xp), MMI32(pt.yp)};
                return 1;
            });
        };
        s.own_registration = [](void *v, MMHelpPoint *out) -> int {
            auto &h = *static_cast<BalloonHost *>(v);
            return h.Guard([&] {
                PT pt;
                h.owner->GetPtReg(&pt);
                *out = {MMI32(pt.xp), MMI32(pt.yp)};
                return 1;
            });
        };
        s.representation = [](void *v, MMI32 x, MMI32 y) -> int {
            auto &h = *static_cast<BalloonHost *>(v);
            return h.Guard([&] {
                h.owner->_SetGorp(h.owner->_pgorp, x, y);
                return 1;
            });
        };
        return s;
    }
    MMHelpBalloonLayoutServices Layout()
    {
        MMHelpBalloonLayoutServices s = {};
        s.context = this;
        s.base = [](void *v, MMI32 x, MMI32 y) -> int {
            auto &h = *static_cast<BalloonHost *>(v);
            return h.Guard([&] {
                h.owner->KidspaceGraphicObject::_SetGorp(h.representation, x, y);
                return 1;
            });
        };
        s.bounds = [](void *v, int kind, MMMaskedRect *out) -> int {
            auto &h = *static_cast<BalloonHost *>(v);
            return h.Guard([&] {
                RC rc;
                if (kind == 0)
                    h.owner->GetRc(&rc, cooParent);
                else if (kind == 1)
                    h.owner->PgobPar()->GetRc(&rc, cooLocal);
                else
                    h.owner->GetRcContent(&rc);
                *out = BalloonRect(rc);
                return 1;
            });
        };
        s.position = [](void *v, int child, const MMMaskedRect *p) -> int {
            auto &h = *static_cast<BalloonHost *>(v);
            return h.Guard([&] {
                auto rc = BalloonRect(*p);
                if (child)
                    static_cast<PTopicGraphicsObject>(h.owner->_balloon.topic)->SetPos(&rc);
                else
                    h.owner->SetPos(&rc);
                return 1;
            });
        };
        s.preferred = [](void *v, MMI32 *w, MMI32 *hgt) {
            auto &h = *static_cast<BalloonHost *>(v);
            *w = h.owner->_dxpPref;
            *hgt = h.owner->_dypPref;
        };
        return s;
    }
};
struct BalloonButton::ButtonHost : TextDocumentFactoryHost
{
    BalloonButton *owner = nullptr;
    PWorldOfKidspace world = nullptr;
    PGraphicsObject parent = nullptr;
    PResourceCache cache = nullptr;
    ChunkNumber number = 0;
    PCMD_MOUSE command = nullptr;
    explicit ButtonHost(BalloonButton *p = nullptr) : owner(p)
    {
    }
    MMHelpButtonFactoryServices Factory()
    {
        MMHelpButtonFactoryServices s = {};
        s.context = this;
        s.find = [](void *v, MMI32 id) -> int {
            auto &h = *static_cast<ButtonHost *>(v);
            return h.Guard([&] { return h.world->PcmhFromHid(id) != nullptr; });
        };
        s.create = [](void *v, MMI32 id, void **out) -> int {
            auto &h = *static_cast<ButtonHost *>(v);
            return h.Guard([&] {
                GraphicsObjectBlock b(id, h.parent, fgobNil, kginMark);
                *out = NewObj BalloonButton(&b);
                return *out != nullptr;
            });
        };
        s.state = [](void *, void *p) -> MMHelpButton * { return &static_cast<PBalloonButton>(p)->_button; };
        s.initialize = [](void *v, void *p) -> int {
            auto &h = *static_cast<ButtonHost *>(v);
            return h.Guard([&] { return static_cast<PBalloonButton>(p)->_FInit(h.world, h.number, h.cache); });
        };
        s.enter = [](void *v, void *p) -> int {
            auto &h = *static_cast<ButtonHost *>(v);
            return h.Guard([&] { return static_cast<PBalloonButton>(p)->_FEnterState(ksnoInit); });
        };
        s.bounds = [](void *v, void *p, MMMaskedRect *out) -> int {
            auto &h = *static_cast<ButtonHost *>(v);
            return h.Guard([&] {
                RC rc;
                static_cast<PBalloonButton>(p)->GetRc(&rc, cooParent);
                *out = BalloonRect(rc);
                return 1;
            });
        };
        s.position = [](void *v, void *p, const MMMaskedRect *rect) -> int {
            auto &h = *static_cast<ButtonHost *>(v);
            return h.Guard([&] {
                auto rc = BalloonRect(*rect);
                static_cast<PBalloonButton>(p)->SetPos(&rc, nullptr);
                return 1;
            });
        };
        s.release = [](void *, void *p) { static_cast<BASE *>(p)->Release(); };
        s.diagnostic = BalloonDiagnostic;
        s.validate = [](void *, void *p) { AssertPo(static_cast<PBalloonButton>(p), 0); };
        return s;
    }
    MMHelpButtonServices Interaction()
    {
        MMHelpButtonServices s = {};
        s.context = this;
        s.base_point = [](void *v, MMI32 x, MMI32 y) -> int {
            auto &h = *static_cast<ButtonHost *>(v);
            return h.Guard([&] { return h.owner->KidspaceGraphicObject::FPtIn(x, y); });
        };
        s.parent_topic = [](void *v, void **out) -> int {
            auto &h = *static_cast<ButtonHost *>(v);
            return h.Guard([&] {
                auto p = h.owner->PgobPar();
                if (!p->FIs(kclsTopicGraphicsObject))
                    return false;
                *out = h.owner->PgobPar();
                return true;
            });
        };
        s.map = [](void *v, MMHelpPoint *out) -> int {
            auto &h = *static_cast<ButtonHost *>(v);
            return h.Guard([&] {
                PT pt(out->x, out->y);
                h.owner->MapPt(&pt, cooLocal, cooParent);
                *out = {MMI32(pt.xp), MMI32(pt.yp)};
                return 1;
            });
        };
        s.group = [](void *v, void *p, const MMHelpPoint *pt, unsigned *g, MMU32 *t) -> int {
            auto &h = *static_cast<ButtonHost *>(v);
            byte group = 0;
            ChunkNumber target = 0;
            int r = h.Guard(
                [&] { return static_cast<PTopicGraphicsObject>(p)->FGroupFromPt(pt->x, pt->y, &group, &target); });
            *g = group;
            *t = target;
            return r;
        };
        s.id = [](void *v) -> MMI32 { return static_cast<ButtonHost *>(v)->owner->Hid(); };
        s.flags = [](void *v) -> MMU32 { return static_cast<ButtonHost *>(v)->command->grfcust; };
        s.hit = [](void *v, void *p, unsigned g, MMU32 target, MMU32 flags, MMI32 id) -> int {
            auto &h = *static_cast<ButtonHost *>(v);
            return h.Guard([&] {
                static_cast<PTopicGraphicsObject>(p)->DoHit(byte(g), target, flags, id);
                return 1;
            });
        };
        s.diagnostic = BalloonDiagnostic;
        return s;
    }
};
#endif
