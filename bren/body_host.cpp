/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "kauai_core.h"
#include "body_renderer.h"
#include "handle_services.h"
#include "asset_resources.h"
#include "material_renderer.h"
#include "brender.h"

ASSERTNAME

extern "C" const MMBRenderBody *mm_kauai_body_renderer(void)
{
    struct Binding
    {
        MMBRenderBody renderer;

        Binding()
        {
            const MMBRenderBodyHost host = {
                KauaiResourceObjectAllocator(), kcbMax,
                [](MMI32 bytes, void **out) -> int { return FAllocPv(out, bytes, fmemClear, mprNormal); },
                [](void **storage) { FreePpv(storage); },
                [](void **out) { TrashVar(out); },
                [](int code) {
                    switch (code)
                    {
                    case MM_BODY_MISSING_MATERIAL: Assert(false, "body set has no material"); break;
                    case MM_BODY_EMPTY_SET: Assert(false, "empty body set"); break;
                    case MM_BODY_SHORT_COSTUME: Assert(false, "not enough custom materials!"); break;
                    case MM_BODY_COSTUME_SHAPE: Assert(false, "different Body shapes!"); break;
                    }
                },
                [] { Bug("What actor is this?  It has no Body"); }
            };
            mm_brender_body_init(&renderer, &host);
        }
    };
    static const Binding binding;
    return &binding.renderer;
}

#ifdef DEBUG
void mm_kauai_body_validate(MMBodyOwner *body, unsigned full)
{
    LegacyLong ibact;
    LegacyLong ibset;
    br_actor *pbact;

    if (!body)
        return;
    const auto &shape = mm_body_owner_state(body)->shape;
    const auto root = mm_brender_body_root(body);
    AssertIn(shape.hidden, 0, 100); // 100 is sanity check
    AssertIn(shape.set_count, 0, shape.part_count + 1);
    size_t bytes = 0, offset = 0;
    AssertDo(mm_brender_body_layout(shape.part_count + 2, kcbMax, &bytes, &offset), "bad body actor storage size");
    AssertPvCb(root, static_cast<LegacyLong>(bytes));
    const auto storage = static_cast<unsigned char *>(shape.actors);
    Assert(shape.bindings == reinterpret_cast<MMBodyPartBinding *>(storage + offset),
           "bad body binding storage");
    AssertPvCb(shape.custom_materials, LwMul(shape.set_count, size(void *)));
    Assert(pvNil == root->model, "Body root shouldn't have a model!!");
    Assert(pvNil == root->material, "Body root shouldn't have a material!!");

    if (full)
    {
        for (ibact = 0; ibact < shape.part_count; ibact++)
        {
            pbact = mm_brender_body_part(body, ibact);
            const auto &binding = shape.bindings[ibact+2];
            AssertIn(binding.set, 0, shape.set_count);
            Assert(binding.model_handle == pbact->model, "body part model binding mismatch");
            if (binding.model_owner)
                mm_kauai_model_validate(static_cast<MMModelOwner *>(binding.model_owner));
            Assert(binding.material == pbact->material, "body part material binding mismatch");
            if (pvNil != pbact->material)
            {
                auto material = mm_brender_material_owner(pbact->material);
                Assert(material && mm_brender_material_handle(material) == pbact->material, "Bad Material identifier");
                mm_kauai_material_validate(material);
                Assert(!binding.material_owner || binding.material_owner == material, "body part material owner mismatch");
            }
        }
        for (ibset = 0; ibset < shape.set_count; ibset++)
        {
            if (auto costume = static_cast<MMMaterialOwner *>(shape.custom_materials[ibset]))
                mm_kauai_costume_validate(costume);
        }
    }
}

/***************************************************************************
    Mark memory used by the Body
***************************************************************************/
void mm_kauai_body_mark(MMBodyOwner *body)
{
    if (!body)
        return;
    mm_kauai_body_validate(body, 0);
    const auto &shape = mm_body_owner_state(body)->shape;

    MarkPv(body);
    MarkPv(shape.actors);
    MarkPv(shape.custom_materials);
    for (LegacyLong part = 0; part < shape.part_count; ++part)
    {
        const auto &binding = shape.bindings[part+2];
        mm_brender_model_mark(static_cast<MMModelOwner *>(binding.model_owner));
        if (binding.material_owner)
            mm_brender_material_mark(static_cast<MMMaterialOwner *>(binding.material_owner));
    }
    for (LegacyLong set = 0; set < shape.set_count; ++set)
        if (shape.custom_materials[set])
            mm_brender_material_mark(static_cast<MMMaterialOwner *>(shape.custom_materials[set]));
}
#else
void mm_kauai_body_validate(MMBodyOwner *, unsigned) {}
void mm_kauai_body_mark(MMBodyOwner *) {}
#endif
