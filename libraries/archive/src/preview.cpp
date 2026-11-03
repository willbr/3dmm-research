/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "three_mm/preview.h"
#include "diagnostics.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <functional>
#include "soc_core.h"
#include "preview_actor.h"
#include "preview_background.h"
#undef size
ASSERTNAME

namespace ThreeMM
{
namespace
{
bool runtimeOwned = false;
bool Fail(Error &error, ErrorCode code, const char *message)
{
    error.code = code;
    error.message = message;
    if (code == ErrorCode::decode_failed && Internal::AssertionFailed())
        error.message += std::string(" ") + Internal::DiagnosticMessage();
    return false;
}
struct Frame
{
    br_pixelmap *colour = nullptr, *depth = nullptr;
    br_actor *world = nullptr, *light = nullptr;
    std::vector<br_actor *> lights;
    ~Frame()
    {
        if (light)
            BrLightDisable(light);
        for (auto value : lights)
            BrLightDisable(value);
        if (world)
            BrActorFree(world);
        if (depth)
            BrPixelmapFree(depth);
        if (colour)
            BrPixelmapFree(colour);
    }
};
bool WithinBudget(const Archive &archive, PChunkyFile file, Error &error)
{
    std::uint64_t total = 0;
    for (const auto &chunk : archive.Chunks())
    {
        std::uint32_t decoded = chunk.storedBytes;
        if (chunk.packed)
        {
            DataBlock block;
            byte header[8];
            if (chunk.storedBytes < sizeof(header) || !file->FFind(chunk.id.type, chunk.id.number, &block) ||
                !block.FReadRgb(header, sizeof(header), 0, fTrue))
                return Fail(error, ErrorCode::decode_failed, "Invalid compressed preview dependency.");
            decoded = (std::uint32_t(header[4]) << 24) | (std::uint32_t(header[5]) << 16) |
                      (std::uint32_t(header[6]) << 8) | header[7];
        }
        total += decoded;
        if (chunk.storedBytes > 64u * 1024 * 1024 || decoded > 64u * 1024 * 1024 || total > 512u * 1024 * 1024)
        {
            error.hasChunk = true;
            error.chunk = chunk.id;
            return Fail(error, ErrorCode::resource_limit,
                        "Preview archives are limited to 64 MiB per chunk and 512 MiB of decoded dependencies.");
        }
    }
    return true;
}
} // namespace
struct Preview::Impl
{
    struct Source
    {
        std::int32_t id = 0;
        Archive archive;
        PChunkyResourceFile cache = nullptr;
        ~Source()
        {
            ReleasePpo(&cache);
        }
    };
    Archive assets, resources;
    PChunkyResourceFile cache = nullptr;
    PTemplate tmpl = nullptr;
    PBody body = nullptr;
    PModel model = nullptr;
    PreviewInfo info;
    Color palette[256] = {};
    bool running = false, ready = false;
    std::vector<std::unique_ptr<Source>> sources;
    std::vector<std::unique_ptr<Internal::PreviewActor>> actors;
    std::string resolveError;
    void BindTemplate(PTemplate value)
    {
        if (!value->FIsTdt())
            return;
        static_cast<PThreeDText>(value)->SetFontResolver(this, [](void *context, const TAG &tag) -> PThreeDFont {
            auto self = static_cast<Impl *>(context);
            TAGOnFile wire;
            wire.From(tag);
            auto source = self->Resolve(wire);
            return source ? static_cast<PThreeDFont>(source->PbacoFetch(tag.ctg, tag.cno, ThreeDFont::FReadTdf))
                          : nullptr;
        });
    }
    std::unique_ptr<Internal::PreviewBackground> background;
    struct CameraChange
    {
        std::int32_t frame;
        std::uint32_t camera;
    };
    std::vector<CameraChange> cameraChanges;
    std::uint32_t initialCamera = 0;
    bool LoadBackground(PChunkyResourceFile source, ChunkNumber number)
    {
        std::unique_ptr<Internal::PreviewBackground> loaded(new Internal::PreviewBackground);
        if (!loaded->Load(source, number, palette))
            return false;
        info.cameraCount = static_cast<std::uint32_t>(loaded->cameras.size());
        background.swap(loaded);
        return true;
    }
    PChunkyResourceFile Resolve(const TAGOnFile &tag)
    {
        if (tag.sid == 0 && cache->Pcfl()->FFind(tag.ctg, tag.cno))
            return cache;
        for (const auto &source : sources)
            if (source->id == tag.sid && source->cache->Pcfl()->FFind(tag.ctg, tag.cno))
                return source->cache;
        resolveError = "Missing source " + std::to_string(tag.sid) + ": " + Identity({tag.ctg, tag.cno}) +
                       ". Add the content archive for this source.";
        return nullptr;
    }
    std::vector<PBody> Bodies() const
    {
        std::vector<PBody> bodies;
        if (body)
            bodies.push_back(body);
        for (const auto &actor : actors)
            if (actor->Visible())
                bodies.push_back(actor->body);
        return bodies;
    }
    void RefreshStats()
    {
        info.parts = info.triangles = 0;
        const auto count = [&](PBody value) {
            for (LegacyLong i = 0; i < value->Cbact() - 2; ++i)
                if (auto model = value->PbactPart(i)->model)
                {
                    ++info.parts;
                    info.triangles += model->nprepared_faces;
                }
        };
        if (body)
            count(body);
        for (const auto &actor : actors)
            if (actor->Visible())
                count(actor->body);
        if (model)
        {
            info.parts = 1;
            info.triangles = model->Pbmdl()->nprepared_faces;
        }
    }
    void ClearAsset()
    {
        actors.clear();
        background.reset();
        cameraChanges.clear();
        initialCamera = 0;
        resolveError.clear();
        ReleasePpo(&body);
        ReleasePpo(&tmpl);
        ReleasePpo(&model);
        info = {};
        ready = false;
    }
    ~Impl()
    {
        ClearAsset();
        sources.clear();
        ReleasePpo(&cache);
        if (running)
        {
            Body::ReleaseSharedMaterial();
            Material_MTRL::ReleaseShadeTable();
            BrZbEnd();
            BrEnd();
            runtimeOwned = false;
        }
    }
};
Preview::Preview() : _impl(new Impl)
{
}
Preview::~Preview() = default;
void Preview::Close()
{
    _impl.reset(new Impl);
}
const PreviewInfo &Preview::Info() const
{
    return _impl->info;
}
bool Preview::Open(const std::string &archivePath, const std::string &resourcesPath, Error &error)
try
{
    error = {};
    Internal::ClearDiagnostic();
    // Validate both paths before touching the live renderer or session.
    std::unique_ptr<Impl> opened(new Impl);
    if (!opened->assets.Open(archivePath, error) || !opened->resources.Open(resourcesPath, error))
        return false;
    if (!WithinBudget(opened->assets, static_cast<PChunkyFile>(opened->assets.NativeFile()), error) ||
        !WithinBudget(opened->resources, static_cast<PChunkyFile>(opened->resources.NativeFile()), error))
        return false;
    DataBlock block;
    auto file = static_cast<PChunkyFile>(opened->resources.NativeFile());
    if (!file->FFind(kctgColorTable, 0, &block))
        return Fail(error, ErrorCode::unsupported_format,
                    "Resources must contain the GLCR/0 palette from 3dmovie.chk.");
    short order;
    PDynamicArray colours = DynamicArray::PglRead(&block, &order);
    const bool paletteOk = colours && colours->CbEntry() == sizeof(Color) && colours->IvMac() == 256;
    if (paletteOk)
    {
        if (order == kboOther)
            SwapBytesRglw(colours->QvGet(0), colours->IvMac());
        std::memcpy(opened->palette, colours->QvGet(0), sizeof(opened->palette));
    }
    ReleasePpo(&colours);
    if (!paletteOk || !file->FFind(kctgTmap, 0))
        return Fail(error, ErrorCode::unsupported_format,
                    "Resources require a 256-colour GLCR/0 palette and TMAP/0 shade table.");
    if (runtimeOwned && !_impl->running)
        return Fail(error, ErrorCode::invalid_argument, "Another preview owns the BRender runtime. Close it first.");
    if (!_impl->running)
    {
        BrBegin();
        BrZbBegin(BR_PMT_INDEX_8, BR_PMT_DEPTH_16);
        opened->running = runtimeOwned = true;
    }
    opened->cache = ChunkyResourceFile::PcrfNew(static_cast<PChunkyFile>(opened->assets.NativeFile()), 32L << 20);
    if (!opened->cache || Internal::AssertionFailed())
        return Fail(error, ErrorCode::decode_failed, "Cannot initialize the asset resource reader.");
    if (!Material_MTRL::FSetShadeTable(file, kctgTmap, 0))
        return Fail(error, ErrorCode::decode_failed, "Cannot load the resource shade table.");
    // Transfer the runtime only after replacement resources are ready. Old
    // models/materials are destroyed immediately, without rendering them again.
    _impl->running = false;
    opened->running = true;
    _impl.swap(opened);
    return true;
}
catch (const std::bad_alloc &)
{
    return Fail(error, ErrorCode::out_of_memory, "Insufficient memory to open preview resources.");
}
bool Preview::AddContent(std::int32_t sourceId, const std::string &path, Error &error)
try
{
    error = {};
    Internal::ClearDiagnostic();
    if (!_impl->running || sourceId <= 0)
        return Fail(error, ErrorCode::invalid_argument,
                    "Open preview resources and specify a positive content source ID.");
    for (const auto &source : _impl->sources)
        if (source->id == sourceId && source->archive.Path() == path)
            return true;
    std::unique_ptr<Impl::Source> source(new Impl::Source);
    source->id = sourceId;
    if (!source->archive.Open(path, error))
        return false;
    if (!WithinBudget(source->archive, static_cast<PChunkyFile>(source->archive.NativeFile()), error))
        return false;
    source->cache = ChunkyResourceFile::PcrfNew(static_cast<PChunkyFile>(source->archive.NativeFile()), 32L << 20);
    if (!source->cache || Internal::AssertionFailed())
        return Fail(error, ErrorCode::decode_failed, "Cannot initialize the content resource reader.");
    _impl->sources.push_back(std::move(source));
    return true;
}
catch (const std::bad_alloc &)
{
    return Fail(error, ErrorCode::out_of_memory, "Insufficient memory to load content archive.");
}
bool Preview::Select(Id asset, Error &error)
try
{
    error = {};
    Internal::ClearDiagnostic();
    _impl->ClearAsset();
    if (!_impl->cache)
        return Fail(error, ErrorCode::invalid_argument, "Open preview resources first.");
    bool ok = false;
    if (asset.type == kctgTmpl)
    {
        _impl->tmpl = static_cast<PTemplate>(_impl->cache->PbacoFetch(asset.type, asset.number, Template::FReadTmpl));
        if (_impl->tmpl)
        {
            _impl->BindTemplate(_impl->tmpl);
            _impl->body = _impl->tmpl->PbodyCreate();
            auto &info = _impl->info;
            info.prop = _impl->tmpl->FIsProp();
            for (LegacyLong i = 0; i < _impl->tmpl->Cactn(); ++i)
            {
                LegacyLong count = 0;
                if (!_impl->tmpl->FGetCcelActn(i, &count) || count <= 0)
                    break;
                PreviewAction action;
                action.name = "Action " + std::to_string(i);
                ChildChunkIdentification child;
                if (_impl->cache->Pcfl()->FGetKidChidCtg(asset.type, asset.number, i, kctgActn, &child))
                    for (const auto &chunk : _impl->assets.Chunks())
                        if (chunk.id == Id{child.cki.ctg, child.cki.cno} && !chunk.name.empty())
                        {
                            action.name = std::to_string(i) + ": " + chunk.name;
                            break;
                        }
                action.celCount = static_cast<std::uint32_t>(count);
                info.actions.push_back(action);
            }
            ok = _impl->body && !info.actions.empty() && _impl->tmpl->FSetActnCel(_impl->body, 0, 0);
            if (ok)
            {
                BRA x, y, z;
                _impl->tmpl->GetRestOrien(&x, &y, &z);
                BMAT34 rest;
                BrMatrix34RotateX(&rest, x);
                BrMatrix34PostRotateY(&rest, y);
                BrMatrix34PostRotateZ(&rest, z);
                _impl->body->LocateOrient(rZero, rZero, rZero, &rest);
            }
        }
    }
    else if (asset.type == kctgBmdl)
    {
        _impl->model = static_cast<PModel>(_impl->cache->PbacoFetch(asset.type, asset.number, Model::FReadModl));
        ok = _impl->model != nullptr;
    }
    else if (asset.type == kctgBkgd)
        ok = _impl->LoadBackground(_impl->cache, asset.number);
    else if (asset.type == kctgScen || asset.type == kctgActr)
    {
        auto file = _impl->cache->Pcfl();
        std::vector<ChunkNumber> actorChunks;
        if (asset.type == kctgScen)
        {
            SceneData scene;
            if (!scene.FRead(file, asset.number))
                return Fail(error, ErrorCode::decode_failed, "Cannot read the scene header.");
            _impl->info.scene = true;
            _impl->info.firstFrame = scene.firstFrame;
            _impl->info.lastFrame = scene.lastFrame;
            ChildChunkIdentification child;
            DataBlock block;
            if (!file->FGetKidChidCtg(asset.type, asset.number, 1, kctgStartGg, &child) ||
                !file->FFind(child.cki.ctg, child.cki.cno, &block))
                return Fail(error, ErrorCode::decode_failed, "Scene start events are missing.");
            short order;
            auto starts = SceneData::PggReadEvents(&block, &order, true);
            if (!starts)
                return Fail(error, ErrorCode::decode_failed, "Cannot read scene start events.");
            bool valid = true;
            TAGOnFile backgroundTag = {};
            bool hasBackground = false;
            for (LegacyLong i = 0; i < starts->IvMac(); ++i)
            {
                SceneEvent event;
                starts->GetFixed(i, &event);
                if (order == kboOther)
                    SwapBytesBom(&event, kbomSev);
                if (event.sevt == sevtAddActr)
                {
                    std::uint32_t slot;
                    starts->Get(i, &slot);
                    if (order == kboOther)
                        SwapBytesRglw(&slot, 1);
                    if (!file->FGetKidChidCtg(asset.type, asset.number, slot, kctgActr, &child))
                    {
                        valid = false;
                        break;
                    }
                    actorChunks.push_back(child.cki.cno);
                    if (actorChunks.size() > 256)
                    {
                        valid = false;
                        break;
                    }
                }
                else if (event.sevt == sevtSetBkgd)
                {
                    starts->Get(i, &backgroundTag);
                    if (order == kboOther)
                        SwapBytesRglw(&backgroundTag, sizeof(backgroundTag) / 4);
                    hasBackground = true;
                }
                else if (event.sevt == sevtChngCamera)
                {
                    starts->Get(i, &_impl->initialCamera);
                    if (order == kboOther)
                        SwapBytesRglw(&_impl->initialCamera, 1);
                }
            }
            ReleasePpo(&starts);
            if (!valid)
                return Fail(error, ErrorCode::decode_failed, "Scene references a missing actor.");
            if (hasBackground)
            {
                auto source = _impl->Resolve(backgroundTag);
                if (!source || !_impl->LoadBackground(source, backgroundTag.cno))
                    return Fail(error, ErrorCode::decode_failed,
                                source ? "Cannot load scene background." : _impl->resolveError.c_str());
            }
            if (!file->FGetKidChidCtg(asset.type, asset.number, 0, kctgFrmGg, &child) ||
                !file->FFind(child.cki.ctg, child.cki.cno, &block))
                return Fail(error, ErrorCode::decode_failed, "Scene frame events are missing.");
            auto frames = SceneData::PggReadEvents(&block, &order, false);
            if (!frames)
                return Fail(error, ErrorCode::decode_failed, "Cannot read scene frame events.");
            for (LegacyLong i = 0; i < frames->IvMac(); ++i)
            {
                SceneEvent event;
                frames->GetFixed(i, &event);
                if (order == kboOther)
                    SwapBytesBom(&event, kbomSev);
                if (event.sevt == sevtChngCamera)
                {
                    std::uint32_t camera;
                    frames->Get(i, &camera);
                    if (order == kboOther)
                        SwapBytesRglw(&camera, 1);
                    _impl->cameraChanges.push_back({event.nfrm, camera});
                }
            }
            ReleasePpo(&frames);
        }
        else
        {
            DataBlock block;
            ActorChunkOnFile header;
            if (!file->FFind(asset.type, asset.number, &block) || !ActorData::FReadHeader(&block, &header))
                return Fail(error, ErrorCode::decode_failed, "Cannot read the actor header.");
            if (header.nfrmFirst == knfrmInvalid)
                return Fail(error, ErrorCode::unsupported_format,
                            "This actor has no on-stage frames. Select its template to inspect its geometry.");
            _impl->info.firstFrame = header.nfrmFirst;
            _impl->info.lastFrame = header.nfrmLast;
            actorChunks.push_back(asset.number);
        }
        for (const auto number : actorChunks)
        {
            std::unique_ptr<Internal::PreviewActor> actor(new Internal::PreviewActor);
            if (!actor->Load(
                    _impl->cache, number, [&](const TAGOnFile &tag) { return _impl->Resolve(tag); },
                    [&](PTemplate value) { _impl->BindTemplate(value); }))
            {
                const auto message = _impl->resolveError.empty() ? "Cannot load the actor route, events or template."
                                                                 : _impl->resolveError;
                _impl->ClearAsset();
                return Fail(error, ErrorCode::decode_failed, message.c_str());
            }
            _impl->actors.push_back(std::move(actor));
        }
        if (asset.type == kctgActr)
        {
            _impl->info.firstFrame = _impl->actors[0]->FirstFrame();
            _impl->info.lastFrame = _impl->actors[0]->LastFrame();
        }
        _impl->info.timeline = true;
        _impl->info.actors = static_cast<std::uint32_t>(_impl->actors.size());
        _impl->ready = true;
        ok = SetFrame(_impl->info.firstFrame, error);
    }
    else
        return Fail(error, ErrorCode::unsupported_format, "This chunk does not yet have a 3D preview.");
    if (!ok || Internal::AssertionFailed())
    {
        _impl->ClearAsset();
        error.hasChunk = true;
        error.chunk = asset;
        if (error)
            return false;
        return Fail(error, ErrorCode::decode_failed, "The engine could not load this asset and its pose dependencies.");
    }
    _impl->info.asset = asset;
    _impl->RefreshStats();
    _impl->ready = true;
    return true;
}
catch (const std::bad_alloc &)
{
    _impl->ClearAsset();
    return Fail(error, ErrorCode::out_of_memory, "Insufficient memory to load preview asset.");
}
bool Preview::SetPose(std::uint32_t action, std::uint32_t cel, Error &error)
{
    error = {};
    Internal::ClearDiagnostic();
    if (!_impl->ready || !_impl->tmpl || action >= _impl->info.actions.size() ||
        cel >= _impl->info.actions[action].celCount)
        return Fail(error, ErrorCode::invalid_argument, "Action or cel is out of range.");
    if (!_impl->tmpl->FSetActnCel(_impl->body, action, cel) || Internal::AssertionFailed())
    {
        _impl->ready = false;
        return Fail(error, ErrorCode::decode_failed, "Cannot apply this action pose.");
    }
    _impl->RefreshStats();
    return true;
}
bool Preview::SetFrame(std::int32_t frame, Error &error)
try
{
    error = {};
    Internal::ClearDiagnostic();
    if (!_impl->ready || !_impl->info.timeline || frame < _impl->info.firstFrame || frame > _impl->info.lastFrame)
        return Fail(error, ErrorCode::invalid_argument, "Scene frame is out of range.");
    for (const auto &actor : _impl->actors)
        if (!actor->Seek(frame) || Internal::AssertionFailed())
        {
            _impl->ready = false;
            return Fail(error, ErrorCode::decode_failed,
                        _impl->resolveError.empty() ? "Cannot evaluate this scene frame."
                                                    : _impl->resolveError.c_str());
        }
    if (_impl->background)
    {
        auto camera = _impl->initialCamera;
        for (const auto &change : _impl->cameraChanges)
            if (change.frame <= frame)
                camera = change.camera;
        if (!SetCamera(camera, error))
            return false;
    }
    _impl->info.currentFrame = frame;
    _impl->RefreshStats();
    return true;
}
catch (const std::bad_alloc &)
{
    _impl->ready = false;
    return Fail(error, ErrorCode::out_of_memory, "Insufficient memory to evaluate scene frame.");
}
bool Preview::SetCamera(std::uint32_t camera, Error &error)
try
{
    error = {};
    Internal::ClearDiagnostic();
    if (!_impl->ready || !_impl->background || camera >= _impl->info.cameraCount)
        return Fail(error, ErrorCode::invalid_argument, "Background camera is out of range.");
    if (camera == _impl->info.currentCamera)
        return true;
    if (!_impl->background->SetCamera(camera) || Internal::AssertionFailed())
    {
        _impl->ready = false;
        return Fail(error, ErrorCode::decode_failed, "Cannot load the background camera and depth image.");
    }
    _impl->info.currentCamera = camera;
    return true;
}
catch (const std::bad_alloc &)
{
    return Fail(error, ErrorCode::out_of_memory, "Insufficient memory to load the background camera.");
}
bool Preview::Render(const PreviewView &view, PreviewImage &image, Error &error)
{
    image = {};
    error = {};
    Internal::ClearDiagnostic();
    if (!_impl->ready)
        return Fail(error, ErrorCode::invalid_argument, "Select an asset to render.");
    if (view.width < 64 || view.height < 64 || view.width > 1024 || view.height > 1024 || !std::isfinite(view.yaw) ||
        !std::isfinite(view.pitch) || !std::isfinite(view.zoom) || view.pitch < -85 || view.pitch > 85 ||
        view.zoom < .1f || view.zoom > 10)
        return Fail(error, ErrorCode::invalid_argument, "Preview dimensions or camera values are out of range.");
    try
    {
        Frame frame;
        const auto bg = view.authoredCamera ? _impl->background.get() : nullptr;
        const auto width = bg ? bg->width : view.width, height = bg ? bg->height : view.height;
        frame.colour = BrPixelmapAllocate(BR_PMT_INDEX_8, width, height, nullptr, BR_PMAF_NORMAL);
        frame.depth = frame.colour ? BrPixelmapMatch(frame.colour, BR_PMMATCH_DEPTH_16) : nullptr;
        frame.world = BrActorAllocate(BR_ACTOR_NONE, nullptr);
        if (!frame.colour || !frame.depth || !frame.world)
            return Fail(error, ErrorCode::out_of_memory, "Cannot allocate preview buffers.");
        BrPixelmapFill(frame.colour, 0);
        BrPixelmapFill(frame.depth, 0xffff);
        if (bg)
            for (std::uint32_t y = 0; y < height; ++y)
            {
                std::memcpy(static_cast<byte *>(frame.colour->pixels) + y * frame.colour->row_bytes,
                            bg->colours.data() + y * width, width);
                std::memcpy(static_cast<byte *>(frame.depth->pixels) + y * frame.depth->row_bytes,
                            bg->depths.data() + y * width, width * 2);
            }
        float low[3] = {0, 0, 0}, high[3] = {1, 1, 1};
        auto bodies = _impl->Bodies();
        bool first = true;
        for (auto body : bodies)
        {
            BCB bounds;
            body->GetBcbBounds(&bounds, fTrue);
            const float min[3] = {BrScalarToFloat(bounds.xrMin), BrScalarToFloat(bounds.yrMin),
                                  BrScalarToFloat(bounds.zrMin)};
            const float max[3] = {BrScalarToFloat(bounds.xrMax), BrScalarToFloat(bounds.yrMax),
                                  BrScalarToFloat(bounds.zrMax)};
            for (int axis = 0; axis < 3; ++axis)
            {
                low[axis] = first ? min[axis] : std::min(low[axis], min[axis]);
                high[axis] = first ? max[axis] : std::max(high[axis], max[axis]);
            }
            first = false;
        }
        if (_impl->model)
            for (int axis = 0; axis < 3; ++axis)
            {
                low[axis] = BrScalarToFloat(_impl->model->Pbmdl()->bounds.min.v[axis]);
                high[axis] = BrScalarToFloat(_impl->model->Pbmdl()->bounds.max.v[axis]);
            }
        float center[3], squared = 0;
        for (int axis = 0; axis < 3; ++axis)
        {
            center[axis] = (low[axis] + high[axis]) * .5f;
            squared += (high[axis] - low[axis]) * (high[axis] - low[axis]) * .25f;
        }
        const float radius = std::max(.1f, std::sqrt(squared));
        const float aspect = float(width) / height;
        const float distance = radius * 2.8f / std::min(1.f, aspect) / view.zoom;
        if (distance + radius * 4 > 30000)
            return Fail(error, ErrorCode::unsupported_format, "Asset exceeds the fixed-point preview camera range.");
        br_camera camera = {};
        camera.type = BR_CAMERA_PERSPECTIVE_FOV;
        camera.field_of_view = BR_ANGLE_DEG(45);
        camera.hither_z = BrFloatToScalar(std::max(.01f, distance / 1000));
        camera.yon_z = BrFloatToScalar(distance + radius * 4 + 1);
        camera.aspect = BrFloatToScalar(aspect);
        if (bg)
        {
            camera.hither_z = bg->camera.zrHither;
            camera.yon_z = bg->camera.zrYon;
            camera.field_of_view = bg->camera.aFov;
        }
        auto cam = BrActorAllocate(BR_ACTOR_CAMERA, &camera);
        if (!cam)
            return Fail(error, ErrorCode::out_of_memory, "Cannot allocate preview camera.");
        BrActorAdd(frame.world, cam);
        const float yaw = std::fmod(view.yaw, 360.f) * .01745329252f, pitch = view.pitch * .01745329252f;
        const float sy = std::sin(yaw), cy = std::cos(yaw), sp = std::sin(pitch), cp = std::cos(pitch);
        const float basis[3][3] = {{cy, 0, -sy}, {-sy * sp, cp, -cy * sp}, {sy * cp, sp, cy * cp}};
        cam->t.type = BR_TRANSFORM_MATRIX34;
        for (int i = 0; i < 3; ++i)
            for (int j = 0; j < 3; ++j)
                cam->t.t.mat.m[i][j] = BrFloatToScalar(basis[i][j]);
        for (int axis = 0; axis < 3; ++axis)
        {
            const float position = center[axis] + basis[2][axis] * distance;
            if (!bg && (position < -32767.f || position > 32767.f))
                return Fail(error, ErrorCode::unsupported_format,
                            "Camera position exceeds the fixed-point renderer range.");
            if (!bg)
                cam->t.t.mat.m[3][axis] = BrFloatToScalar(position);
        }
        if (bg)
            cam->t.t.mat = bg->camera.bmat34Cam;
        br_light light = {};
        light.type = BR_LIGHT_DIRECT;
        light.colour = BR_COLOUR_RGB(255, 255, 255);
        light.attenuation_c = BR_SCALAR(1);
        std::vector<br_light> lights(bg ? bg->lights.size() : 0);
        if (bg)
        {
            for (std::size_t i = 0; i < lights.size(); ++i)
            {
                lights[i].type = static_cast<br_uint_8>(bg->lights[i].lt);
                lights[i].colour = BR_COLOUR_RGB(255, 255, 255);
                lights[i].attenuation_c = bg->lights[i].rIntensity;
                auto actor = BrActorAllocate(BR_ACTOR_LIGHT, &lights[i]);
                if (!actor)
                    return Fail(error, ErrorCode::out_of_memory, "Cannot allocate scene lighting.");
                BrActorAdd(frame.world, actor);
                actor->t.type = BR_TRANSFORM_MATRIX34;
                actor->t.t.mat = bg->lights[i].bmat34;
                frame.lights.push_back(actor);
                BrLightEnable(actor);
            }
        }
        else
        {
            frame.light = BrActorAllocate(BR_ACTOR_LIGHT, &light);
            if (!frame.light)
                return Fail(error, ErrorCode::out_of_memory, "Cannot allocate preview lighting.");
            BrActorAdd(frame.world, frame.light);
            frame.light->t = cam->t;
            BrLightEnable(frame.light);
        }
        std::vector<PBACT> instances;
        for (auto body : bodies)
            for (LegacyLong i = 0; i < body->Cbact() - 2; ++i)
                if (body->PbactPart(i)->model)
                    instances.push_back(body->PbactPart(i));
        if (_impl->model)
            instances.push_back(nullptr);
        for (auto source : instances)
        {
            auto actor = BrActorAllocate(BR_ACTOR_MODEL, nullptr);
            if (!actor)
                return Fail(error, ErrorCode::out_of_memory, "Cannot allocate preview model instance.");
            BrActorAdd(frame.world, actor);
            actor->model = source ? source->model : _impl->model->Pbmdl();
            actor->material = source ? source->material : nullptr;
            actor->t.type = BR_TRANSFORM_MATRIX34;
            BrMatrix34Identity(&actor->t.t.mat);
            // Copy instances, never relink Body's embedded BACTs (not owned by BrRes).
            for (auto part = source; part; part = part->parent)
            {
                br_matrix34 composed;
                BrMatrix34Mul(&composed, &actor->t.t.mat, &part->t.t.mat);
                actor->t.t.mat = composed;
            }
        }
        BrZbSceneRender(frame.world, cam, frame.colour, frame.depth);
        if (Internal::AssertionFailed())
            return Fail(error, ErrorCode::decode_failed, "The renderer rejected this asset.");
        PreviewImage rendered;
        rendered.width = view.width;
        rendered.height = view.height;
        rendered.rgba.resize(std::size_t(view.width) * view.height * 4);
        // Fit the completed authored view into the requested image, preserving
        // its aspect ratio and background/actor depth relationship.
        const float scale = std::min(float(view.width) / width, float(view.height) / height);
        const auto fittedWidth = static_cast<std::uint32_t>(width * scale);
        const auto fittedHeight = static_cast<std::uint32_t>(height * scale);
        const auto left = (view.width - fittedWidth) / 2, top = (view.height - fittedHeight) / 2;
        for (std::uint32_t y = 0; y < fittedHeight; ++y)
        {
            const auto sourceY = std::min(height - 1, static_cast<std::uint32_t>(y / scale));
            const auto row = static_cast<const byte *>(frame.colour->pixels) + sourceY * frame.colour->row_bytes;
            const auto depths = reinterpret_cast<const std::uint16_t *>(static_cast<const byte *>(frame.depth->pixels) +
                                                                        sourceY * frame.depth->row_bytes);
            for (std::uint32_t x = 0; x < fittedWidth; ++x)
            {
                const auto sourceX = std::min(width - 1, static_cast<std::uint32_t>(x / scale));
                auto out = &rendered.rgba[(std::size_t(y + top) * view.width + x + left) * 4];
                const auto &colour = (bg ? bg->palette : _impl->palette)[row[sourceX]];
                out[0] = colour.bRed;
                out[1] = colour.bGreen;
                out[2] = colour.bBlue;
                out[3] = !bg && depths[sourceX] == 0xffff ? 0 : 255;
                if (out[3])
                    ++rendered.coveredPixels;
            }
        }
        image = std::move(rendered);
        return true;
    }
    catch (const std::bad_alloc &)
    {
        return Fail(error, ErrorCode::out_of_memory, "Insufficient memory for the preview image.");
    }
}
} // namespace ThreeMM
