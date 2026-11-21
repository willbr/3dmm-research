/* Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */

/***************************************************************************

    modl.cpp: BRender model resource bindings

    Primary Author: ******
    Review Status: REVIEWED - any changes to this file must be reviewed!

***************************************************************************/
#include "soc_core.h"
#include "handle_services.h"
#include "model_renderer.h"
ASSERTNAME

static_assert(sizeof(BrFaceOnFile) == MM_MODEL_FACE_BYTES, "model face wire layout");

const MMModelOwnerHost &EngineModel::Host()
{
    static const auto renderer = [] {
        MMModelServices s = {};
        const unsigned short one = 1;
        s.host_big_endian = !*reinterpret_cast<const unsigned char *>(&one);
        s.system = koskCur;
        mm_brender_model_services(&s);
        s.decode = KauaiDecodeBlockHandle;
        s.allocator = KauaiCopyAllocator();
        s.invalidate = [](void *, void *out, size_t bytes) { TrashPvCb(out, static_cast<LegacyLong>(bytes)); };
        s.diagnostic = [](void *, int code) {
            if (code == MM_MODEL_BAD_ORDER)
                Assert(false, "bad Model!");
        };
        s.save_failed = [](void *) { Warn("model save failed."); };
        return s;
    }();
    static const MMModelOwnerHost host = {
        &renderer, KauaiResourceObjectAllocator(),
#ifdef DEBUG
        [](void *memory) { MarkPv(memory); }
#else
        nullptr
#endif
    };
    return host;
}

PModel EngineModel::New(LegacyLong nv, BRV *vertices, LegacyLong nf, BRF *faces)
{
    AssertIn(nv, 0, ksuMax);
    AssertIn(nf, 0, ksuMax);
    AssertPvCb(vertices, LwMul(nv, size(BRV)));
    AssertPvCb(faces, LwMul(nf, size(BRF)));
    return mm_model_new(nv, vertices, nf, faces, &Host());
}

int EngineModel::Read(MMResourceCache *, MMU32, MMU32, MMOwnedBlock *block, void **out, MMI32 *bytes)
{
    return mm_model_fetch(block, out, bytes, &Host());
}

PModel EngineModel::Fetch(MMResourceCache *cache, ChunkTagOrType type, ChunkNumber number)
{
    return static_cast<PModel>(mm_resource_cache_fetch_owned_chunk(cache, type, number, Read,
                                                                  mm_model_owner_services()));
}

PModel EngineModel::FromDat(Filename *filename)
{
    AssertPo(filename, ffniFile);
    String path;
    filename->GetStnPath(&path);
    return mm_model_dat(path.Psz(), &Host());
}

PBMDL EngineModel::Handle(PModel owner)
{
    return static_cast<PBMDL>(mm_model_owner_resource(owner)->handle);
}

PModel EngineModel::FromHandle(PBMDL handle)
{
    AssertVarMem(handle);
    auto owner = static_cast<PModel>(mm_brender_model_owner(handle));
    Assert(owner && Handle(owner) == handle, "Bad Model identifier");
    return owner;
}

bool EngineModel::Write(PModel owner, PChunkyFile file, ChunkTagOrType type, ChunkNumber number)
{
    AssertPo(file, 0);
    return Write(owner, file->WriteOwnerIndex(), type, number);
}

bool EngineModel::Write(PModel owner, const MMArchiveWriter &archive, ChunkTagOrType type, ChunkNumber number)
{
#ifdef DEBUG
    Validate(owner);
#endif
    return mm_model_write(mm_model_owner_resource(owner), &archive.edit, archive.payloads->files,
                          type, number, Host().renderer) != 0;
}

void EngineModel::AdjustGlyph(PModel owner)
{
#ifdef DEBUG
    Validate(owner);
#endif
    mm_model_adjust_glyph(mm_model_owner_resource(owner), Host().renderer);
}

BRS EngineModel::Extent(PModel owner, unsigned axis)
{
    return mm_model_extent(mm_model_owner_resource(owner), axis, Host().renderer);
}

bool EngineModel::Prelight(PModel owner, LegacyLong count, BVEC3 *directions)
{
    AssertIn(count, 1, 10);
    AssertPvCb(directions, LwMul(count, size(BVEC3)));
    return mm_brender_model_prelight(mm_model_owner_resource(owner), count, [](void *) {
        Assert(false, "code needs to get smarter...");
    }) != 0;
}

#ifdef DEBUG
void EngineModel::Validate(PModel owner)
{
    Assert(owner, "Bad Model owner");
    AssertVarMem(Handle(owner));
    Assert(mm_brender_model_owner(Handle(owner)) == owner, "Bad Model identifier");
}

void EngineModel::Mark(PModel owner)
{
    if (owner)
    {
        Validate(owner);
        mm_model_owner_services()->mark_memory(owner);
        const auto binding = mm_model_owner_binding(owner);
        if (binding->cache && !binding->attached)
            MarkMemObj(static_cast<PChunkyResourceFile>(binding->cache->owner));
    }
}
#endif
