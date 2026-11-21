/* Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */

// Native renderer adapter for C89 actor bodies and scoped costume snapshots.
#ifndef Body_H
#define Body_H

#include "mm/body_create.h"
#include "mm/part_indices.h"
#include "mm/body_edit.h"
#include "mm/body_runtime.h"
#include "mm/render_frame.h"

using namespace BRender;

using PBody = MMBodyOwner *;

namespace EngineBody
{
PBody Create(PDynamicArray parents, PDynamicArray sets);
PBody Create(const MMPartIndexView &parents, const MMPartIndexView &sets);
PBody FromActor(PBACT actor, LegacyLong *set = pvNil);
PBody Duplicate(PBody);
void Restore(PBody, PBody snapshot);

bool ChangeShape(PBody, PDynamicArray parents, PDynamicArray sets);
bool ChangeShape(PBody, const MMPartIndexView &parents, const MMPartIndexView &sets);
// The renderer binding must outlive the body and any duplicates. Bind while hidden.
void SetRenderCallbacks(PBody, void *context, void (*attach)(void *, void *), void (*changed)(void *));
void Show(PBody);
void Hide(PBody);
void Hilite(PBody);
void Unhilite(PBody);
void SetHiliteColor(LegacyLong);
void ReleaseSharedMaterial();

inline MMBodyState &State(PBody body) { return *mm_body_owner_state(body); }
inline PBACT Root(PBody body) { return static_cast<PBACT>(State(body).shape.actors); }
inline PBACT Part(PBody body, LegacyLong part) { return Root(body) + 2 + part; }
inline LegacyLong ActorCount(PBody body) { return State(body).shape.part_count + 2; }
inline LegacyLong SetCount(PBody body) { return State(body).shape.set_count; }
inline const MMActorMatrix &RootMatrix(PBody body) { return State(body).shape.bindings[0].matrix; }
inline MMBodyDrawView DrawView(PBody body) { return {State(body).shape.bindings, State(body).shape.part_count}; }

void LocateOrient(PBody, BRS x, BRS y, BRS z, BMAT34 *);
void ApplyPose(PBody, const MMActorBodyPose &);
bool PartSetBounds(PBody, LegacyLong set, MMBodyBounds &);
void SetPartModel(PBody, LegacyLong part, PModel);
void SetPartMatrix(PBody, LegacyLong part, BMAT34 *);
void SetPartMatrix(PBody, LegacyLong part, const MMActorMatrix &);
void SetPartSetMtrl(PBody, LegacyLong set, PMaterial_MTRL);
void SetPartSetCmtl(PBody, PCustomMaterial_CMTL);
void GetPartSetMaterial(PBody, LegacyLong set, bool *generic, PMaterial_MTRL *, PCustomMaterial_CMTL *);
void GetBcbBounds(PBody, BCB *, bool world = fFalse);
void GetRcBounds(PBody, RC *);
void GetCenter(PBody, LegacyLong *x, LegacyLong *y);
void GetPosition(PBody, BRS *x, BRS *y, BRS *z);
bool IsInView(PBody);

MMBodyGeometryServices GeometryServices();
int ReadRenderBody(PBACT, MMRenderBody *);
void PrepareRenderBody(const MMRenderBody *);
void ActorRendered(PBACT, RC *);
void RenderRect(PBACT, RC *);

#ifdef DEBUG
void Validate(PBody, LegacyUlong flags = 0);
void Mark(PBody);
#endif
}

class ScopedBodyCostume
{
    MMBodyCostume _costume = {};

  public:
    ScopedBodyCostume() = default;
    ScopedBodyCostume(const ScopedBodyCostume &) = delete;
    ScopedBodyCostume &operator=(const ScopedBodyCostume &) = delete;
    ~ScopedBodyCostume();
    bool FGet(PBody);
    void Set(PBody, bool allowDifferentShape = fFalse) const;
};
using PBodyCostume = const ScopedBodyCostume *;

#endif // !Body_H
