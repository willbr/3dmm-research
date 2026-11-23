/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "bren_core.h"
#include "asset_resources.h"
#include "handle_services.h"
#include "model_renderer.h"
#include "socdef.h"
#include "template_renderer.h"
#include "texture_renderer.h"

using namespace BRender;

ASSERTNAME

static int native_big_endian()
{
    const unsigned short one = 1;
    return *reinterpret_cast<const unsigned char *>(&one) == 0;
}

const MMTemplateOwnerHost *mm_kauai_template_host(void)
{
    static const MMTemplateOwnerHost host = {
        KauaiResourceObjectAllocator(), sizeof(achar),
#ifdef DEBUG
        mm_kauai_template_mark
#else
        nullptr
#endif
    };
    return &host;
}

const MMActionResourceHost *mm_kauai_action_host(void)
{
    static const auto tables = Group::CollectionAllocator();
    static const MMActionResourceHost host = {
        KauaiResourceObjectAllocator(), &tables, KauaiDecodeBlockHandle,
#ifdef DEBUG
        [](void *memory) { if (memory) MarkPv(memory); },
        [](void *) { Assert(false, "bad ActionChunkOnFile"); },
        [](void *, void *out, size_t bytes) { FillPb(out, static_cast<LegacyLong>(bytes), kbGarbage); }
#else
        nullptr, nullptr, nullptr
#endif
    };
    return &host;
}

void mm_kauai_template_read_services(MMTemplateCreateServices *s)
{
    s->host_big_endian = native_big_endian();
    s->debug_checks = DebugShip(1, 0);
    s->decode = KauaiDecodeBlockHandle;
    s->table_allocator = Group::CollectionAllocator();
    s->read_name = [](void *cache, void *object, MMU32 type, MMU32 number) -> int {
        const auto storage = mm_template_owner_name(static_cast<MMTemplateOwner *>(object));
        return mm_kauai_resource_name(static_cast<MMResourceCache *>(cache), type, number, &storage);
    };
    s->object_size = [](void *, int text) -> MMI32 {
        // Preserve cache charges for the former native object layouts.
        const size_t prefix = sizeof(void *) == 8 ? 40 : 28 + DebugShip(4, 0);
        const size_t bytes = prefix + sizeof(MMTemplateState) + sizeof(String);
        const size_t alignment = sizeof(void *);
        const size_t base = (bytes + alignment - 1) & ~(alignment - 1);
        return static_cast<MMI32>(base + (text ? sizeof(MMTextDescriptor) + sizeof(MMTextGenerated) +
                                               2 * sizeof(void *) : 0));
    };
    s->invalidate = [](void *, void *out, size_t count) { TrashPvCb(out, static_cast<LegacyLong>(count)); };
    s->create_cache = [](void *cache) -> void * {
        return NewTypedSiblingCache(static_cast<MMResourceCache *>(cache), 0);
    };
    s->diagnostic = [](void *, int code) {
        switch (code)
        {
        case MM_TEMPLATE_BAD_ORDER: Assert(false, "freaky tmplf!"); break;
        case MM_TEMPLATE_OBSOLETE: Warn("Obsolete GLDC structure...get rid of it"); break;
        case MM_TEMPLATE_BUILD_COSTUMES: Warn("missing GGCM...building one on the fly"); break;
        case MM_TEMPLATE_COSTUME_LIMIT: Assert(false, "legacy costume table exceeds 50 entries per set"); break;
        case MM_TEMPLATE_WRONG_SET: Assert(false, "ibset's don't match"); break;
        }
    };
}

int mm_kauai_resource_name(const MMResourceCache *cache, MMU32 type, MMU32 number, const MMStringValue *out)
{
    return ReadResourceName(cache, type, number, *out);
}

void mm_kauai_text_action_name_missing(void *)
{
    Warn("action name not found");
}

void mm_kauai_text_read_services(MMTextResourceServices *s)
{
    s->host_big_endian = native_big_endian();
    s->payloads = KauaiChunkPayloadServices();
    s->bad_order = [](void *) { Assert(false, "bad ThreeDTextF"); };
}

void mm_kauai_font_check_index(MMI32 index, MMI32 count)
{
    AssertIn(index, 0, count);
}

void mm_kauai_template_validate(MMTemplateOwner *owner)
{
#ifdef DEBUG
    if (!owner)
        return;
    const auto &state = *mm_template_owner_state(owner);
    Assert(state.parents && state.sets, "missing Template part lists");
    Assert(state.costumes && state.costumes->rows == static_cast<MMU32>(state.set_count),
           "Bad template costume table");
    if (mm_template_owner_is_text(owner))
    {
        const auto &descriptor = *mm_template_owner_descriptor(owner);
        const auto &generated = *mm_template_owner_generated(owner);
        Assert(descriptor.font.type != ctgNil, "ThreeDText has bad _tagTdf");
        mm_kauai_material_validate(static_cast<MMMaterialOwner *>(generated.material));
        if (generated.action_id != -1)
        {
            AssertIn(generated.action_id, 0, MM_TEXT_ACTION_COUNT);
            Assert(mm_action_resource_references(generated.action) > 0, "missing generated action");
        }
        else
            Assert(generated.action == nullptr, "generated.action_id is wrong");
        AssertIn(descriptor.shape, 0, MM_TEXT_SHAPE_COUNT);
    }
#else
    (void)owner;
#endif
}

void mm_kauai_template_mark(MMTemplateOwner *owner)
{
#ifdef DEBUG
    if (!owner)
        return;
    mm_kauai_template_validate(owner);
    MarkPv(owner);
    const auto binding = mm_template_owner_binding(owner);
    if (binding->cache && !binding->attached)
        mm_resource_cache_mark(binding->cache);
    const auto &state = *mm_template_owner_state(owner);
    MMPartIndexList *indices[] = {state.parents, state.sets};
    for (auto list : indices)
        if (list)
        {
            MarkPv(list);
            MarkPv(list->storage.buffers[0].data);
        }
    if (state.costumes)
    {
        MarkPv(state.costumes);
        MarkPv(state.costumes->storage.buffers[0].data);
        MarkPv(state.costumes->storage.buffers[1].data);
    }
    if (mm_template_owner_is_text(owner))
    {
        const auto &generated = *mm_template_owner_generated(owner);
        mm_brender_material_mark(static_cast<MMMaterialOwner *>(generated.material));
        if (generated.action_id != -1)
            mm_action_resource_services()->mark_memory(generated.action);
    }
#else
    (void)owner;
#endif
}

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

void mm_kauai_model_validate(const MMModelOwner *owner)
{
#ifdef DEBUG
    Assert(owner, "Bad Model owner");
    if (owner)
    {
        const auto handle = mm_brender_model_handle(owner);
        AssertVarMem(handle);
        Assert(handle && mm_brender_model_owner(handle) == owner, "Bad Model identifier");
    }
#else
    (void)owner;
#endif
}

namespace
{
MMTextureServices TextureEffects()
{
    MMTextureServices s = {};
    s.system = koskCur;
    s.packed_signature = klwSigPackedFile;
    s.unpacked_signature = klwSigUnpackedFile;
    s.release_aux = [](void *, void *p) { static_cast<BASE *>(p)->Release(); };
    s.allocate = [](void *, MMI32 n, int clear, void **out) -> int {
        return FAllocPv(out, n, clear ? fmemClear : fmemNil, mprNormal);
    };
    s.free_memory = [](void *, void **p) { FreePpv(p); };
    s.invalidate = [](void *, void *p, size_t n) { TrashPvCb(p, static_cast<LegacyLong>(n)); };
    s.diagnostic = [](void *, int code) {
        if (code == MM_TEXTURE_ORDER)
            Assert(false, "bad TextureMapFile");
        else if (code == MM_TEXTURE_PACK_SHRINK)
            Assert(false, "HQ shrink failed");
        else
            Assert(false, "upside down bitmap");
    };
    s.payloads = KauaiChunkPayloadServices();
    s.copy_allocator = KauaiCopyAllocator();
    s.encode = [](void *, void **handle) -> int { return vpcodmUtil->FCompressPhq(handle, cfmtNil); };
#ifndef MAC
    s.read_bitmap = [](void *, void *filename, MMTextureBitmap *out) -> int {
        byte *pixels = nullptr;
        PDynamicArray palette = nullptr;
        LegacyLong w, h;
        bool upside;
        if (!FReadBitmap(static_cast<PFilename>(filename), &pixels, &palette, &w, &h, &upside))
            return 0;
        *out = {pixels, palette, w, h, upside};
        return 1;
    };
#else
    s.read_bitmap = [](void *, void *, MMTextureBitmap *) -> int {
        RawRtn();
        return 0;
    };
#endif
    s.palette_count = [](void *, void *p) -> MMI32 { return static_cast<PDynamicArray>(p)->IvMac(); };
    s.palette_colour = [](void *, void *p, MMI32 i, MMTextureColour *out) {
        Color c;
        static_cast<PDynamicArray>(p)->Get(i, &c);
        *out = {c.bRed, c.bGreen, c.bBlue};
    };
    s.create_file = [](void *, void *p) -> void * { return FileObject::PfilCreate(static_cast<PFilename>(p)); };
    s.make_temporary = [](void *, void *p) { static_cast<PFileObject>(p)->SetTemp(); };
    s.report = [](void *, void *p, int error) {
        PZString message = error == MM_TEXTURE_FILE_CREATE   ? PszLit("Couldn't create destination file\n")
                           : error == MM_TEXTURE_FILE_MEMORY ? PszLit("allocation failure\n")
                                                                   : PszLit("writing to destination file failed\n");
        static_cast<PMSNK>(p)->ReportLine(message);
    };
    return s;
}
} // namespace

const MMTextureOwnerHost *mm_kauai_texture_host(void)
{
    struct Binding
    {
        MMBRenderTextureHost texture;
        Binding()
        {
            const auto services = TextureEffects();
            mm_brender_texture_host_init(&texture, &services, KauaiResourceObjectAllocator(),
#ifdef DEBUG
                                         [](void *memory) { MarkPv(memory); }
#else
                                         nullptr
#endif
            );
        }
    };
    static const Binding binding;
    return &binding.texture.owner;
}

void mm_kauai_texture_validate(MMTextureOwner *texture)
{
#ifdef DEBUG
    Assert(texture, "Bad texture owner");
    if (texture && !texture->texture.imported_owner)
    {
        const auto view = static_cast<PBPMP>(mm_brender_texture_view(texture));
        AssertPvCb(view->pixels, LwMul(view->row_bytes, view->height));
    }
#else
    (void)texture;
#endif
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
        s.allocate = [](void *, MMI32 bytes, void **out) -> int { return FAllocPv(out, bytes, fmemClear, mprNormal); };
        s.free_memory = [](void *, void **p) { FreePpv(p); };
        return s;
    }();
    static const MMMaterialOwnerHost host = {
        &services, mm_brender_material_shade(), KauaiResourceObjectAllocator(),
#ifdef DEBUG
        [](void *p) { MarkPv(p); },
        mm_resource_cache_mark,
        [](void *p) { mm_brender_texture_mark(static_cast<MMTextureOwner *>(p)); },
        [](void *p) { mm_brender_model_mark(static_cast<MMModelOwner *>(p)); }
#else
        nullptr, nullptr, nullptr, nullptr
#endif
    };
    return &host;
}

void mm_kauai_material_validate(MMMaterialOwner *owner)
{
#ifdef DEBUG
    const bool valid = owner && mm_material_owner_kind(owner) == MM_MATERIAL;
    Assert(valid, "Expected a material owner");
    if (valid)
        if (auto texture = mm_material_texture(mm_material_owner_resource(owner), mm_kauai_material_host()->services))
        {
            mm_kauai_texture_validate(static_cast<MMTextureOwner *>(texture));
            Assert(mm_brender_material_shade()->texture != nullptr, "Textured material needs a shade table");
        }
#else
    (void)owner;
#endif
}
