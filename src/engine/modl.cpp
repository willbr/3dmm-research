/* Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */

/***************************************************************************

    modl.cpp: Model class

    Primary Author: ******
    Review Status: REVIEWED - any changes to this file must be reviewed!

***************************************************************************/
#include "soc_core.h"
#include "handle_services.h"
#include "model_renderer.h"
ASSERTNAME

RTCLASS(Model)

static_assert(sizeof(BrFaceOnFile) == MM_MODEL_FACE_BYTES, "model face wire layout");

MMModelServices Model::_Services(void *context)
{
    MMModelServices s = {};
    s.context = context;
    const unsigned short one = 1;
    s.host_big_endian = !*reinterpret_cast<const unsigned char *>(&one);
    s.system = koskCur;
    mm_brender_model_services(&s);
    s.state = [](void *, void *object) -> MMModelResource * { return &static_cast<PModel>(object)->_model; };
    s.create = [](void *) -> void * { return NewObj Model; };
    s.release = [](void *, void *object) { static_cast<PModel>(object)->Release(); };
    s.decode = KauaiDecodeBlockHandle;
    s.allocate = [](void *, MMI32 bytes, int clear, void **out) -> int {
        return FAllocPv(out, bytes, clear ? fmemClear : fmemNil, mprNormal);
    };
    s.free_memory = [](void *, void **out) { FreePpv(out); };
    s.invalidate = [](void *, void *out, size_t bytes) { TrashPvCb(out, static_cast<LegacyLong>(bytes)); };
    s.diagnostic = [](void *, int code) {
        if (code == MM_MODEL_BAD_ORDER)
            Assert(false, "bad Model!");
    };
    s.save_failed = [](void *) { Warn("model save failed."); };
    s.put = [](void *, void *file, MMU32 type, MMU32 number, const void *data, MMI32 bytes) -> int {
        return static_cast<PChunkyFile>(file)->FPutPv(const_cast<void *>(data), bytes, type, number);
    };
    s.path = [](void *context, void *filename) -> const void * {
        auto path = static_cast<PString>(context);
        static_cast<PFilename>(filename)->GetStnPath(path);
        return path->Psz();
    };
    return s;
}

PModel Model::PmodlNew(LegacyLong nv, BRV *vertices, LegacyLong nf, BRF *faces)
{
    AssertIn(nv, 0, ksuMax);
    AssertIn(nf, 0, ksuMax);
    AssertPvCb(vertices, LwMul(nv, size(BRV)));
    AssertPvCb(faces, LwMul(nf, size(BRF)));
    auto s = _Services();
    return static_cast<PModel>(mm_model_new(nv, vertices, nf, faces, &s));
}
bool Model::FReadModl(PChunkyResourceFile cache, ChunkTagOrType type, ChunkNumber number, PDataBlock block,
                      PBaseCacheableObject *out, LegacyLong *bytes)
{
    AssertPo(cache, 0);
    AssertPo(block, 0);
    AssertNilOrVarMem(out);
    AssertVarMem(bytes);
    auto s = _Services();
    return mm_model_fetch(block->CoreBlock(), out, bytes, &s) != 0;
}
bool Model::_FInit(PDataBlock block)
{
    AssertBaseThis(0);
    AssertPo(block, 0);
    auto s = _Services();
    return mm_model_read(this, block->CoreBlock(), &s) != 0;
}
PModel Model::PmodlReadFromDat(Filename *filename)
{
    AssertPo(filename, ffniFile);
    String path;
    auto s = _Services(&path);
    return static_cast<PModel>(mm_model_dat(filename, &s));
}
PModel Model::PmodlFromBmdl(PBMDL handle)
{
    AssertVarMem(handle);
    auto owner = static_cast<PModel>(mm_brender_model_owner(handle));
    AssertPo(owner, 0);
    return owner;
}
Model::~Model(void)
{
    AssertBaseThis(0);
    auto s = _Services();
    mm_model_dispose(&_model, &s);
}
bool Model::FWrite(PChunkyFile file, ChunkTagOrType type, ChunkNumber number)
{
    AssertThis(0);
    AssertPo(file, 0);
    auto s = _Services();
    return mm_model_write(&_model, file, type, number, &s) != 0;
}
void Model::AdjustTdfCharacter(void)
{
    AssertThis(0);
    auto s = _Services();
    mm_model_adjust_glyph(&_model, &s);
}

bool Model::_FPrelight(LegacyLong count, BVEC3 *directions)
{
    AssertIn(count, 1, 10);
    AssertPvCb(directions, LwMul(count, size(BVEC3)));
    AssertBaseThis(0);
    return mm_brender_model_prelight(&_model, count, [](void *) {
        Assert(false, "code needs to get smarter...");
    }) != 0;
}
#ifdef DEBUG
void Model::AssertValid(LegacyUlong grf)
{
    Model_PAR::AssertValid(fobjAllocated);
    AssertVarMem(static_cast<PBMDL>(_model.handle));
    auto owner = static_cast<PModel>(mm_brender_model_owner(_model.handle));
    Assert(owner == this, "Bad Model identifier");
}
void Model::MarkMem(void)
{
    AssertThis(0);
    Model_PAR::MarkMem();
}
#endif
