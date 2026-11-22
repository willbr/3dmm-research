#ifndef MM_PREVIEW_RENDER_HOST_H
#define MM_PREVIEW_RENDER_HOST_H
// Preview freezes host data for the C89 frame owner. Included in MM.
struct Preview::Impl::RenderHost
{
    Impl *impl;
    const PreviewView *view;
    Error *error;
    PreviewImage image;
    br_camera camera = {};
    std::exception_ptr exception;
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
        s.base_palette = impl->palette;
        s.live = &impl->live;
        s.body_geometry = &mm_kauai_body_renderer()->geometry;
        s.only_object = view ? view->onlyObject : -1;
        if (view && view->orbitBounds)
        {
            s.object_corners = view->orbitBounds->corners;
            s.object_corners_valid = view->orbitBounds->valid;
        }
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
        s.camera = [](void *v, void *world, const MMPreviewCamera *c) -> void * {
            return mm_brender_preview_camera(world, &static_cast<RenderHost *>(v)->camera, c);
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
