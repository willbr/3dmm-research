#ifndef BODY_FIXTURE_H
#define BODY_FIXTURE_H
#include "soc_core.h"
#include "engine_part_indices.h"
#include <cstring>

inline PBody CreateFixtureBody(PDynamicArray parents, PDynamicArray sets)
{
    const auto parentView = EnginePartIndices::Borrow(parents);
    const auto setView = EnginePartIndices::Borrow(sets);
    auto body = mm_brender_body_create(mm_kauai_body_renderer(), &parentView, &setView);
    mm_kauai_body_validate(body, 1);
    return body;
}

inline bool ChangeFixtureBody(PBody body, PDynamicArray parents, PDynamicArray sets)
{
    const auto parentView = EnginePartIndices::Borrow(parents);
    const auto setView = EnginePartIndices::Borrow(sets);
    mm_kauai_body_validate(body, 1);
    const bool changed = mm_body_change_shape(body, &parentView, &setView, &mm_kauai_body_renderer()->edit) != 0;
    mm_kauai_body_validate(body, 1);
    return changed;
}

inline void BindFixtureBody(PBody body, void *context, void (*attach)(void *, void *), void (*changed)(void *))
{
    const MMBodyRenderBinding binding = {context, attach, changed};
    mm_body_owner_bind_render(body, &binding);
}

inline void SetFixturePartMatrix(PBody body, MMI32 part, const BMAT34 *matrix)
{
    MMActorMatrix value;
    static_assert(sizeof(value) == sizeof(*matrix), "body matrix layout");
    std::memcpy(&value, matrix, sizeof(value));
    mm_body_set_matrix(body, part, &value, &mm_kauai_body_renderer()->edit.runtime);
}

#endif
