#ifndef HELP_TOPIC_HOST_H
#define HELP_TOPIC_HOST_H
struct TopicGraphicsObject::BuildHost : TextDocumentFactoryHost
{
    TopicGraphicsObject *owner;
    PTextDocument initial;
    PResourceCache cache = nullptr;
    explicit BuildHost(TopicGraphicsObject *p) : owner(p), initial(p->Ptxhd())
    {
    }
    MMHelpTopicServices Services()
    {
        MMHelpTopicServices s = {};
        s.context = this;
        s.base = [](void *v) -> int {
            auto &h = *static_cast<BuildHost *>(v);
            return h.Guard([&] { return h.owner->RichTextDocumentGraphicsObject::_FInit(); });
        };
        s.characters = [](void *v) -> MMI32 { return static_cast<BuildHost *>(v)->initial->CpMac(); };
        s.scan = [](void *v, MMI32 cp, MMI32 *end, unsigned *out) -> int {
            auto &h = *static_cast<BuildHost *>(v);
            long limit = cp;
            byte group = 0;
            int r = h.Guard([&] { return h.initial->FGrouped(cp, nullptr, &limit, &group); });
            *end = MMI32(limit);
            *out = group;
            return r;
        };
        s.unique = [](void *v, MMI32 count, MMI32 *out) -> int {
            auto &h = *static_cast<BuildHost *>(v);
            return h.Guard([&] {
                *out = CommandHandler::HidUnique(count);
                return 1;
            });
        };
        s.assign_base = [](void *v, MMI32 value) -> int {
            auto &h = *static_cast<BuildHost *>(v);
            return h.Guard([&] {
                String name(PszLit("_gidBase"));
                RuntimeVariableName key;
                key.SetFromStn(&name);
                return mm_script_variables_set(h.owner->PgobPar()->ScriptVariables(), key.lu1, key.lu2, value);
            });
        };
        s.cache = [](void *v) {
            auto &h = *static_cast<BuildHost *>(v);
            h.cache = h.owner->Ptxhd()->Prca();
        };
        s.fetch = [](void *v, MMI32 from, MMI32 *at, void **out, MMI32 *bytes) -> int {
            auto &h = *static_cast<BuildHost *>(v);
            long cp = from, count = 0;
            int r = h.Guard([&] { return h.owner->Ptxhd()->FFetchObject(from, &cp, out, &count); });
            *at = MMI32(cp);
            *bytes = MMI32(count);
            return r;
        };
        s.free = [](void *, void *p) { FreePpv(&p); };
        s.position = [](void *v, MMI32 cp, MMI32 *x, MMI32 *y) -> int {
            auto &h = *static_cast<BuildHost *>(v);
            return h.Guard([&] {
                LegacyLong xp, yp;
                h.owner->_GetXpYpFromCp(cp, nullptr, nullptr, &xp, &yp, false);
                *x = xp;
                *y = yp;
                return 1;
            });
        };
        s.character = [](void *v, MMI32 cp, MMTextCharacter *out) -> int {
            auto &h = *static_cast<BuildHost *>(v);
            return h.Guard([&] {
                CHP chp;
                h.owner->_FetchChp(cp, &chp);
                *out = {chp.grfont,
                        chp.onn,
                        chp.dypFont,
                        chp.dypOffset,
                        MMU32(chp.acrFore.LwGet()),
                        MMU32(chp.acrBack.LwGet())};
                return 1;
            });
        };
        s.font = [](void *v, const MMTextCharacter *p) -> int {
            auto &h = *static_cast<BuildHost *>(v);
            return h.Guard([&] {
                h.owner->_pgnv->SetFont(p->font, p->style, p->height, tahLeft, tavBaseline);
                return 1;
            });
        };
        s.measure = [](void *v, MMMaskedRect *out) -> int {
            auto &h = *static_cast<BuildHost *>(v);
            return h.Guard([&] {
                RC rc;
                h.owner->_pgnv->GetRcFromRgch(&rc, nullptr, 0);
                *out = {rc.xpLeft, rc.ypTop, rc.xpRight, rc.ypBottom};
                return 1;
            });
        };
        s.group = [](void *v, MMI32 cp, unsigned *out, MMU32 *target) -> int {
            auto &h = *static_cast<BuildHost *>(v);
            byte group = 0;
            ChunkNumber number = target ? *target : 0;
            int r = h.Guard(
                [&] { return h.owner->Ptxhd()->FGrouped(cp, nullptr, nullptr, &group, target ? &number : nullptr); });
            *out = group;
            if (target)
                *target = number;
            return r;
        };
        s.exists = [](void *v, MMI32 id) -> int {
            auto &h = *static_cast<BuildHost *>(v);
            return h.Guard([&] { return h.owner->_pwoks->PcmhFromHid(id) != nullptr; });
        };
        s.edit = [](void *v, MMI32 id, const MMMaskedRect *rect, const MMTextCharacter *p) -> int {
            auto &h = *static_cast<BuildHost *>(v);
            return h.Guard([&] {
                RC rc(rect->left, rect->top, rect->right, rect->bottom);
                AbstractColor fore, back;
                fore.SetFromLw(mm_signed_word(p->foreground));
                back.SetFromLw(mm_signed_word(p->background));
                EditParameter params;
                params.Set(id, h.owner, fgobNil, kginMark, &rc, nullptr, p->font, p->style, p->height, tahLeft, tavTop,
                           fore, back);
                return EditControlSingleLine::PedslNew(&params) != nullptr;
            });
        };
        s.button = [](void *v, MMI32 id, MMU32 resource, unsigned group, MMU32 target, MMI32 x, MMI32 y) -> int {
            auto &h = *static_cast<BuildHost *>(v);
            return h.Guard([&] {
                return BalloonButton::PhbtnNew(h.owner->_pwoks, h.owner, id, resource, h.cache, byte(group), target, x,
                                               y) != nullptr;
            });
        };
        s.validate = [](void *v) { AssertPo(static_cast<BuildHost *>(v)->owner, 0); };
        return s;
    }
};
#endif
