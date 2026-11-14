#ifndef THREE_MM_PREVIEW_ACTOR_TREE_H
#define THREE_MM_PREVIEW_ACTOR_TREE_H

#include <map>
#include <vector>

namespace ThreeMM
{
namespace Internal
{
// Body embeds its NONE/MODEL actors. Copy only their render state, keeping
// models/materials shared and leaving the original links and ownership intact.
// Preserve the tree: fixed-point transform multiplication is not associative.
inline bool CopyBodyActorTree(br_actor *parent, const br_actor *source,
                              std::map<br_actor *, int> *owners, int owner)
{
    auto actor = BrActorAllocate(source->type, nullptr);
    if (!actor) return false;
    BrActorAdd(parent, actor);
    actor->model = source->model;
    actor->material = source->material;
    actor->render_style = source->render_style;
    actor->t = source->t;
    if (owners && source->type == BR_ACTOR_MODEL && source->model)
        owners->emplace(actor, owner);

    // BrActorAdd inserts at the head, so visit siblings backwards to retain
    // their original draw order (significant when surfaces have equal depth).
    std::vector<const br_actor *> children;
    for (auto child = source->children; child; child = child->next) children.push_back(child);
    for (auto child = children.rbegin(); child != children.rend(); ++child)
        if (!CopyBodyActorTree(actor, *child, owners, owner)) return false;
    return true;
}
} // namespace Internal
} // namespace ThreeMM
#endif
