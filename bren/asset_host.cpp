/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "bren_core.h"
#include "asset_resources.h"
#include "handle_services.h"
#include "model_renderer.h"
#include "socdef.h"

using namespace BRender;

ASSERTNAME

const MMFontOwnerHost *mm_kauai_font_host(void)
{
    static const MMFontOwnerHost host = {
        KauaiResourceObjectAllocator(), KauaiCopyAllocator(), KauaiDecodeBlockHandle,
#ifdef DEBUG
        [](void *memory) { MarkPv(memory); },
#else
        nullptr,
#endif
        [](void *) { PushErc(ercSocBadTdf); },
        [](void *) { Assert(false, "bad ThreeDFontF"); },
        [](void *, void *out, size_t bytes) { TrashPvCb(out, static_cast<LegacyLong>(bytes)); }
    };
    return &host;
}

void mm_kauai_font_missing_model(void *, MMU32 id)
{
    String message;
    message.FFormatSz(PszLit("Couldn't find BMDL for 3-D Font with chid %d."), id);
    Warn(message.Psz());
    PushErc(ercSocNoModlForChar);
}

const MMModelOwnerHost *mm_kauai_model_host(void)
{
    static const auto renderer = [] {
        MMModelServices s = {};
        mm_brender_asset_model_bind(&s);
        s.system = koskCur;
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
    return &host;
}

const MMMaterialOwnerHost *mm_kauai_material_host(void)
{
    static const auto services = [] {
        MMMaterialServices s = {};
        mm_brender_asset_material_bind(&s, DebugShip(1, 0));
        s.system = koskCur;
        s.windows_system = koskSbWin;
        s.invalidate = [](void *, void *out, size_t bytes) { TrashPvCb(out, static_cast<LegacyLong>(bytes)); };
        s.diagnostic = [](void *, int code) {
            switch (code)
            {
            case MM_MATERIAL_BAD_FILE:
                PushErc(ercSocBadFile);
                break;
            case MM_MATERIAL_BAD_CUSTOM_SIZE:
                Bug("bad CustomMaterialOnFile...you may need to update tmpls.chk");
                break;
            case MM_MATERIAL_BAD_CUSTOM_ORDER:
                Assert(false, "bad CustomMaterialOnFile");
                break;
            case MM_MATERIAL_BAD_TRANSFORM_ORDER:
                Assert(false, "bad TXXFF");
                break;
            }
        };
        s.decode = KauaiDecodeBlockHandle;
        s.texture_reader = EngineTexture::Read;
        s.allocate = [](void *, MMI32 bytes, void **out) -> int { return FAllocPv(out, bytes, fmemClear, mprNormal); };
        s.free_memory = [](void *, void **p) { FreePpv(p); };
        s.write_texture = [](void *, void *texture, const MMChunkEdit *edit, const MMBlockFileServices *files,
                             MMU32 type, MMU32 *number) -> int {
            return EngineTexture::Write(static_cast<PTextureMap>(texture), *edit, files, type, number);
        };
        s.read_bitmap = [](void *, void *file, void *palette) -> void * {
            return EngineTexture::FromBitmap(static_cast<PFilename>(file), static_cast<PDynamicArray>(palette));
        };
        s.wrap_pixelmap = [](void *, void *p) -> void * { return EngineTexture::Wrap(static_cast<PBPMP>(p)); };
        return s;
    }();
    static const MMMaterialOwnerHost host = {
        &services, mm_brender_material_shade(), KauaiResourceObjectAllocator(),
#ifdef DEBUG
        [](void *p) { MarkPv(p); },
        mm_resource_cache_mark,
        [](void *p) { EngineTexture::Mark(static_cast<PTextureMap>(p)); },
        [](void *p) {
            const auto owner = static_cast<MMModelOwner *>(p);
            const auto handle = mm_model_owner_resource(owner)->handle;
            Assert(handle && mm_brender_model_owner(handle) == owner, "Bad Model identifier");
            mm_brender_model_mark(owner);
        }
#else
        nullptr, nullptr, nullptr, nullptr
#endif
    };
    return &host;
}

MMTextureOwner *mm_kauai_asset_texture_stream(const MMReadStream *stream)
{
    return EngineTexture::FromStream(*stream);
}

void mm_kauai_material_validate(MMMaterialOwner *owner)
{
#ifdef DEBUG
    const bool valid = owner && mm_material_owner_kind(owner) == MM_MATERIAL;
    Assert(valid, "Expected a material owner");
    if (valid)
        if (auto texture = mm_material_texture(mm_material_owner_resource(owner), mm_kauai_material_host()->services))
        {
            EngineTexture::Validate(static_cast<PTextureMap>(texture));
            Assert(mm_brender_material_shade()->texture != nullptr, "Textured material needs a shade table");
        }
#else
    (void)owner;
#endif
}
