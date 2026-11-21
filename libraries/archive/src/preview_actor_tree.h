#ifndef MM_PREVIEW_ACTOR_TREE_H
#define MM_PREVIEW_ACTOR_TREE_H
#include "scene_renderer.h"
#include <exception>
#include <map>
namespace MM
{
namespace Internal
{
inline bool CopyBodyActorTree(br_actor *parent, const br_actor *source, std::map<br_actor *, int> *owners, int owner)
{
    struct Host
    {
        std::map<br_actor *, int> *owners;
        std::exception_ptr exception;
    } host = {owners, {}};
    MMPreviewTreeServices s = {};
    mm_brender_preview_tree(&s);
    s.context = &host;
    s.owner = [](void *context, void *copy, MMI32 owner) -> int {
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
    const bool ok = mm_preview_copy_tree(parent, source, owner, nullptr, &s) != 0;
    if (host.exception)
        std::rethrow_exception(host.exception);
    return ok;
}
} // namespace Internal
} // namespace MM
#endif
