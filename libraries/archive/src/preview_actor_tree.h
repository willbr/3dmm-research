#ifndef THREE_MM_PREVIEW_ACTOR_TREE_H
#define THREE_MM_PREVIEW_ACTOR_TREE_H
#include "three_mm/preview_render.h"
#include <exception>
#include <map>
namespace ThreeMM
{
namespace Internal
{
inline ThreeMMPreviewTreeServices PreviewTreeServices()
{
    ThreeMMPreviewTreeServices s = {};
    s.first = [](void *, const void *v) -> void * { return static_cast<const br_actor *>(v)->children; };
    s.next = [](void *, const void *v) -> void * { return static_cast<const br_actor *>(v)->next; };
    s.model = [](void *, const void *v) -> void * {
        auto a = static_cast<const br_actor *>(v);
        return a->type == BR_ACTOR_MODEL ? a->model : nullptr;
    };
    s.clone_attach = [](void *, void *parent, const void *source) -> void * {
        auto v = static_cast<const br_actor *>(source);
        auto copy = BrActorAllocate(v->type, nullptr);
        if (copy)
        {
            BrActorAdd(static_cast<br_actor *>(parent), copy);
            copy->model = v->model;
            copy->material = v->material;
            copy->render_style = v->render_style;
            copy->t = v->t;
        }
        return copy;
    };
    return s;
}
inline bool CopyBodyActorTree(br_actor *parent, const br_actor *source, std::map<br_actor *, int> *owners, int owner)
{
    struct Host
    {
        std::map<br_actor *, int> *owners;
        std::exception_ptr exception;
    } host = {owners, {}};
    auto s = PreviewTreeServices();
    s.context = &host;
    s.owner = [](void *context, void *copy, ThreeMMI32 owner) -> int {
        auto &h = *static_cast<Host *>(context);
        try
        {
            if (h.owners)
                h.owners->emplace(static_cast<br_actor *>(copy), owner);
            return 1;
        }
        catch (...)
        {
            h.exception = std::current_exception();
            return 0;
        }
    };
    const bool ok = three_mm_preview_copy_tree(parent, source, owner, nullptr, &s) != 0;
    if (host.exception)
        std::rethrow_exception(host.exception);
    return ok;
}
} // namespace Internal
} // namespace ThreeMM
#endif
