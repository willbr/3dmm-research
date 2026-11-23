/* Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */

// Native renderer adapter for C89 actor bodies and scoped costume snapshots.
#ifndef Body_H
#define Body_H

#include "body_renderer.h"
#include "mm/part_indices.h"
#include "mm/body_edit.h"
#include "mm/body_runtime.h"
#include "mm/render_frame.h"

using namespace BRender;

using PBody = MMBodyOwner *;

class ScopedBodyCostume
{
    MMBodyCostume _costume = {};

  public:
    ScopedBodyCostume() = default;
    ScopedBodyCostume(const ScopedBodyCostume &) = delete;
    ScopedBodyCostume &operator=(const ScopedBodyCostume &) = delete;
    ~ScopedBodyCostume()
    {
        mm_body_costume_clear(&_costume, &mm_kauai_body_renderer()->edit.runtime);
    }
    bool FGet(PBody body)
    {
        return mm_body_costume_capture(&_costume, body, &mm_kauai_body_renderer()->edit.runtime) != 0;
    }
    void Set(PBody body, bool allowDifferentShape = fFalse) const
    {
        mm_body_costume_apply(&_costume, body, allowDifferentShape, &mm_kauai_body_renderer()->edit.runtime);
    }
};
using PBodyCostume = const ScopedBodyCostume *;

#endif // !Body_H
