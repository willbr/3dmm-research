#ifndef MM_PREVIEW_RENDER_HOST_H
#define MM_PREVIEW_RENDER_HOST_H
// Preview freezes host data for the C89 frame owner. Included in MM.
struct Preview::Impl::RenderHost
{
    Preview *preview;
    Impl *impl;
    const PreviewView *view;
    Error *error;
    PreviewImage image;
    br_camera camera = {};
    MMBackgroundMesh *mesh = nullptr;
    const MMBackgroundLight *lights = nullptr;
    MMU32 lightCount = 0;
    std::exception_ptr exception;
    bool readBodyBounds = true;
    int onlyObject = view ? view->onlyObject : -1;
    const PreviewBounds *orbitBounds = view ? view->orbitBounds : nullptr;
    static PreviewProjection Projection(const MMPreviewProjection &p)
    {
        PreviewProjection out;
        out.valid = p.valid != 0;
        out.orthographic = p.orthographic != 0;
        std::copy(p.transform, p.transform + 12, out.transform);
        out.verticalFov = p.vertical_fov;
        out.aspect = p.aspect;
        out.nearClip = p.near_clip;
        out.farClip = p.far_clip;
        out.displayDepth = p.display_depth;
        out.orthoHeight = p.ortho_height;
        return out;
    }
    MMPreviewRenderServices Services()
    {
        MMPreviewRenderServices s = {};
        mm_brender_preview_services(&s);
        s.context = this;
        s.movie = impl->assets.MovieSession();
        if (impl->live.publication.phase && impl->live.publication.frame == impl->live.status.frame)
            s.position = &impl->live.publication;
        s.math.sin = [](float x) { return std::sin(x); };
        s.math.cos = [](float x) { return std::cos(x); };
        s.math.tan = [](float x) { return std::tan(x); };
        s.math.sqrt = [](float x) { return std::sqrt(x); };
        s.math.asin = [](float x) { return std::asin(x); };
        s.math.atan2 = [](float y, float x) { return std::atan2(y, x); };
        s.math.fmod = [](float x, float y) { return std::fmod(x, y); };
        s.math.scalar_float = [](MMI32 x) { return BrScalarToFloat(x); };
        s.math.scalar = [](float x) -> MMI32 { return BrFloatToScalar(x); };
        s.math.scalar_double = [](double x) -> MMI32 { return BrFloatToScalar(x); };
        s.math.angle = [](float x) -> unsigned short { return BR_ANGLE_DEG(x); };
        s.ready = [](void *v) -> int { return static_cast<RenderHost *>(v)->impl->live.status.ready; };
        s.describe = [](void *v, MMPreviewData *d) {
            auto &h = *static_cast<RenderHost *>(v);
            auto &i = *h.impl;
            d->scene = i.live.status.scene;
            d->background = bool(i.background) && h.onlyObject < 0;
            d->model = i.model ? mm_model_owner_resource(i.model)->handle : nullptr;
            const auto *model = static_cast<const br_model *>(d->model);
            d->model_resource = i.model;
            if (i.model)
                for (unsigned a = 0; a < 3; ++a)
                {
                    d->model_low[a] = BrScalarToFloat(model->bounds.min.v[a]);
                    d->model_high[a] = BrScalarToFloat(model->bounds.max.v[a]);
                }
            const auto *palette = i.Palette();
            for (unsigned n = 0; n < 256; ++n)
            {
                d->palette[n * 4] = palette[n * 4 + 2];
                d->palette[n * 4 + 1] = palette[n * 4 + 1];
                d->palette[n * 4 + 2] = palette[n * 4];
                d->palette[n * 4 + 3] = 255;
            }
            if (i.background && d->background)
            {
                const auto &b = *mm_background_preview_state(i.background.get());
                d->width = b.width;
                d->height = b.height;
                d->light_count = h.lights ? h.lightCount : b.light_count;
                d->lights = h.lights ? h.lights :
                    reinterpret_cast<const MMBackgroundLight *>(b.metadata.buffers[1].data);
                d->colours = b.images.buffers[0].data;
                d->depths = reinterpret_cast<const unsigned short *>(b.images.buffers[1].data);
                const auto &camera = b.camera.data;
                static_assert(sizeof(d->camera.matrix) == sizeof(camera.matrix.m), "Camera matrix layout");
                std::memcpy(d->camera.matrix, camera.matrix.m, sizeof(d->camera.matrix));
                d->camera.fov = camera.fov;
                d->camera.near_z = camera.near_z;
                d->camera.far_z = camera.far_z;
                d->camera.aspect = BrFloatToScalar(float(b.width) / b.height);
            }
        };
        s.mesh = [](void *v, MMPreviewMesh *out) -> int {
            auto &h = *static_cast<RenderHost *>(v);
            try
            {
                h.mesh = mm_background_preview_owner_mesh(h.impl->background.get());
                if (!h.mesh)
                    throw std::bad_alloc();
                out->empty = h.mesh->triangle_count == 0;
                std::copy(h.mesh->low, h.mesh->low + 3, out->low);
                std::copy(h.mesh->high, h.mesh->high + 3, out->high);
                out->infinite = h.mesh->infinite_count ? h.mesh->pixels.buffers[1].data : nullptr;
                return 1;
            }
            catch (...)
            {
                h.exception = std::current_exception();
                return 0;
            }
        };
        s.body_count = [](void *v) -> MMU32 {
            return static_cast<MMU32>(static_cast<RenderHost *>(v)->impl->actors.size() + 1);
        };
        s.body = [](void *v, MMU32 index, MMPreviewBody *out) -> int {
            auto &h = *static_cast<RenderHost *>(v);
            auto &i = *h.impl;
            PBody body = nullptr;
            if (h.onlyObject >= 0 && index != MMU32(h.onlyObject + 1)) return 0;
            std::uint64_t order = 0;
            if (!index)
                body = i.body;
            else if (i.actors[index - 1]->Visible())
            {
                body = i.actors[index - 1]->Pbody();
                order = i.actors[index - 1]->DrawOrder();
            }
            if (!body)
                return 0;
            const auto &shape = mm_body_owner_state(body)->shape;
            out->draw = {shape.bindings, shape.part_count};
            out->resource = body;
            out->owner = static_cast<MMI32>(index) - 1;
            out->order_low = static_cast<MMU32>(order);
            out->order_high = static_cast<MMU32>(order >> 32);
            if (!h.readBodyBounds)
                return 1;
            MMBodyBounds bounds;
            mm_brender_body_bounds(mm_kauai_body_renderer(), body, 1, &bounds);
            for (int axis = 0; axis < 3; ++axis)
            {
                out->low[axis] = BrScalarToFloat(bounds.min[axis]);
                out->high[axis] = BrScalarToFloat(bounds.max[axis]);
            }
            return 1;
        };
        s.retain_pose_resource = [](void *, void *object) { mm_body_owner_retain(static_cast<PBody>(object)); };
        s.release_pose_resource = [](void *, void *object) { mm_body_owner_release(static_cast<PBody>(object)); };
        s.retain_model = [](void *, void *model) { mm_model_owner_retain(static_cast<PModel>(model)); };
        s.release_model = [](void *, void *model) { mm_model_owner_release(static_cast<PModel>(model)); };
        s.object_bounds = [](void *v, MMI32 object, float corners[24], int *valid) -> int {
            auto &h = *static_cast<RenderHost *>(v);
            try
            {
                PreviewBounds b;
                if (h.orbitBounds && h.onlyObject == object) b = *h.orbitBounds;
                else if (!h.preview->ObjectBounds(object, b, *h.error))
                    return 0;
                *valid = b.valid;
                std::copy(b.corners, b.corners + 24, corners);
                return 1;
            }
            catch (...)
            {
                h.exception = std::current_exception();
                return 0;
            }
        };
        s.camera = [](void *v, void *world, const MMPreviewCamera *c) -> void * {
            return mm_brender_preview_camera(world, &static_cast<RenderHost *>(v)->camera, c);
        };
        s.add_mesh = [](void *v, void *world) -> int {
            return mm_background_mesh_add(static_cast<RenderHost *>(v)->mesh, world,
                                          mm_brender_background_mesh_services());
        };
        s.raster_begin = [](void *v, const MMPreviewProjection *p, const unsigned char *palette, int opaque,
                            MMU32 w, MMU32 height) -> int {
            auto &h = *static_cast<RenderHost *>(v);
            try
            {
                h.view->rasterizer->Frame(Projection(*p), palette, opaque != 0);
                if (!h.view->rasterizer->Begin(w, height, *h.error))
                    return 0;
                BrZbSetTriangleSink(
                    [](void *context, const BrRasterTriangle *triangle) {
                        auto &host = *static_cast<RenderHost *>(context);
                        if (host.exception)
                            return;
                        try
                        {
                            host.view->rasterizer->Triangle(*triangle);
                        }
                        catch (...)
                        {
                            host.exception = std::current_exception();
                        }
                    },
                    &h);
                return 1;
            }
            catch (...)
            {
                h.exception = std::current_exception();
                return 0;
            }
        };
        s.sink_off = [](void *) { BrZbSetTriangleSink(nullptr, nullptr); };
        s.renderer_ok = [](void *v) -> int {
            return !Internal::AssertionFailed() && !static_cast<RenderHost *>(v)->exception;
        };
        s.raster_end = [](void *v, const MMPreviewBuffer *b) -> int {
            auto &h = *static_cast<RenderHost *>(v);
            try
            {
                return h.view->rasterizer->End(b->colour, b->colour_stride, b->depth, b->depth_stride, *h.error);
            }
            catch (...)
            {
                h.exception = std::current_exception();
                return 0;
            }
        };
        s.raster_rgba = [](void *v) -> const unsigned char * {
            auto &h = *static_cast<RenderHost *>(v);
            try
            {
                return h.view->rasterizer->RGBA();
            }
            catch (...)
            {
                h.exception = std::current_exception();
                return nullptr;
            }
        };
        s.output = [](void *v, MMU32 w, MMU32 height, int depth, unsigned char **rgba,
                      float **distances) -> int {
            auto &h = *static_cast<RenderHost *>(v);
            try
            {
                h.image.rgba.resize(std::size_t(w) * height * 4);
                if (depth)
                    h.image.depth.assign(std::size_t(w) * height, std::numeric_limits<float>::infinity());
                *rgba = h.image.rgba.data();
                *distances = h.image.depth.data();
                return 1;
            }
            catch (...)
            {
                h.exception = std::current_exception();
                return 0;
            }
        };
        if (view && view->includePaletteIndices)
            s.palette_indices = [](void *v, MMU32 w, MMU32 height) -> unsigned char * {
                auto &indices = static_cast<RenderHost *>(v)->image.paletteIndices;
                try
                {
                    indices.resize(std::size_t(w) * height);
                    return indices.data();
                }
                catch (const std::bad_alloc &)
                {
                    return nullptr;
                }
            };
        return s;
    }
};

#endif
