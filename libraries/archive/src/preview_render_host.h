#ifndef MM_PREVIEW_RENDER_HOST_H
#define MM_PREVIEW_RENDER_HOST_H
// Typed BRender primitives for the C89 Preview frame owner. Included in MM.
struct Preview::Impl::RenderHost
{
    Preview *preview;
    Impl *impl;
    const PreviewView *view;
    Error *error;
    PreviewImage image;
    br_camera camera = {};
    Internal::PreviewBackgroundMesh *mesh = nullptr;
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
    void Camera(br_actor *actor, const MMPreviewCamera &c)
    {
        camera.type = c.parallel ? BR_CAMERA_PARALLEL : BR_CAMERA_PERSPECTIVE_FOV;
        camera.field_of_view = c.fov;
        camera.hither_z = c.near_z;
        camera.yon_z = c.far_z;
        camera.aspect = c.aspect;
        camera.width = c.width;
        camera.height = c.height;
        actor->t.type = BR_TRANSFORM_MATRIX34;
        static_assert(sizeof(actor->t.t.mat) == sizeof(c.matrix), "Camera matrix layout");
        std::memcpy(&actor->t.t.mat, c.matrix, sizeof(c.matrix));
    }
    MMPreviewRenderServices Services()
    {
        MMPreviewRenderServices s = {};
        s.context = this;
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
        s.tree = Internal::PreviewTreeServices();
        s.ready = [](void *v) -> int { return static_cast<RenderHost *>(v)->impl->ready; };
        s.describe = [](void *v, MMPreviewData *d) {
            auto &h = *static_cast<RenderHost *>(v);
            auto &i = *h.impl;
            d->scene = i.info.scene;
            d->background = bool(i.background);
            d->model = i.model ? i.model->Pbmdl() : nullptr;
            if (i.model)
                for (unsigned a = 0; a < 3; ++a)
                {
                    d->model_low[a] = BrScalarToFloat(i.model->Pbmdl()->bounds.min.v[a]);
                    d->model_high[a] = BrScalarToFloat(i.model->Pbmdl()->bounds.max.v[a]);
                }
            auto palette = i.background ? i.background->palette : i.palette;
            for (unsigned n = 0; n < 256; ++n)
            {
                d->palette[n * 4] = palette[n].bRed;
                d->palette[n * 4 + 1] = palette[n].bGreen;
                d->palette[n * 4 + 2] = palette[n].bBlue;
                d->palette[n * 4 + 3] = 255;
            }
            if (i.background)
            {
                auto &b = *i.background;
                d->width = b.width;
                d->height = b.height;
                d->light_count = static_cast<MMU32>(b.lights.size());
                d->lights = b.lights.data();
                d->colours = b.colours.data();
                d->depths = b.depths.data();
                std::memcpy(d->camera.matrix, &b.camera.bmat34Cam, sizeof(d->camera.matrix));
                d->camera.fov = b.camera.aFov;
                d->camera.near_z = b.camera.zrHither;
                d->camera.far_z = b.camera.zrYon;
                d->camera.aspect = BrFloatToScalar(float(b.width) / b.height);
            }
        };
        s.mesh = [](void *v, MMPreviewMesh *out) -> int {
            auto &h = *static_cast<RenderHost *>(v);
            try
            {
                h.mesh = &h.impl->background->Mesh();
                out->empty = h.mesh->Empty();
                std::copy(h.mesh->low.begin(), h.mesh->low.end(), out->low);
                std::copy(h.mesh->high.begin(), h.mesh->high.end(), out->high);
                out->infinite = h.mesh->InfiniteColours().empty() ? nullptr : h.mesh->InfiniteColours().data();
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
            auto &i = *static_cast<RenderHost *>(v)->impl;
            PBody body = nullptr;
            std::uint64_t order = 0;
            if (!index)
                body = i.body;
            else if (i.actors[index - 1]->Visible())
            {
                body = i.actors[index - 1]->body;
                order = i.actors[index - 1]->DrawOrder();
            }
            if (!body)
                return 0;
            out->tree = body->PbactRoot();
            out->owner = static_cast<MMI32>(index) - 1;
            out->order_low = static_cast<MMU32>(order);
            out->order_high = static_cast<MMU32>(order >> 32);
            BCB b;
            body->GetBcbBounds(&b, fTrue);
            out->low[0] = BrScalarToFloat(b.xrMin);
            out->low[1] = BrScalarToFloat(b.yrMin);
            out->low[2] = BrScalarToFloat(b.zrMin);
            out->high[0] = BrScalarToFloat(b.xrMax);
            out->high[1] = BrScalarToFloat(b.yrMax);
            out->high[2] = BrScalarToFloat(b.zrMax);
            return 1;
        };
        s.object_bounds = [](void *v, MMI32 object, float corners[24], int *valid) -> int {
            auto &h = *static_cast<RenderHost *>(v);
            try
            {
                PreviewBounds b;
                if (!h.preview->ObjectBounds(object, b, *h.error))
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
        s.colour = [](void *, MMU32 w, MMU32 h) -> void * {
            return BrPixelmapAllocate(BR_PMT_INDEX_8, w, h, nullptr, BR_PMAF_NORMAL);
        };
        s.depth = [](void *, void *c) -> void * {
            return BrPixelmapMatch(static_cast<br_pixelmap *>(c), BR_PMMATCH_DEPTH_16);
        };
        s.world = [](void *) -> void * { return BrActorAllocate(BR_ACTOR_NONE, nullptr); };
        s.free_pixel = [](void *, void *p) { BrPixelmapFree(static_cast<br_pixelmap *>(p)); };
        s.free_world = [](void *, void *p) { BrActorFree(static_cast<br_actor *>(p)); };
        s.fill = [](void *, void *p, MMU32 value) { BrPixelmapFill(static_cast<br_pixelmap *>(p), value); };
        s.buffers = [](void *, void *c, void *d, MMPreviewBuffer *out) {
            auto colour = static_cast<br_pixelmap *>(c), depth = static_cast<br_pixelmap *>(d);
            out->colour = static_cast<unsigned char *>(colour->pixels);
            out->depth = static_cast<unsigned char *>(depth->pixels);
            out->colour_stride = colour->row_bytes;
            out->depth_stride = depth->row_bytes;
        };
        s.camera = [](void *v, void *world, const MMPreviewCamera *c) -> void * {
            auto &h = *static_cast<RenderHost *>(v);
            auto actor = BrActorAllocate(BR_ACTOR_CAMERA, &h.camera);
            if (actor)
            {
                BrActorAdd(static_cast<br_actor *>(world), actor);
                h.Camera(actor, *c);
            }
            return actor;
        };
        s.update_camera = [](void *v, void *actor, const MMPreviewCamera *c) {
            static_cast<RenderHost *>(v)->Camera(static_cast<br_actor *>(actor), *c);
        };
        s.light_bytes = sizeof(br_light);
        s.direct_light_type = BR_LIGHT_DIRECT;
        s.light = [](void *, void *world, void *data, const MMBackgroundLight *source) -> void * {
            auto light = static_cast<br_light *>(data);
            *light = {};
            light->type = static_cast<br_uint_8>(source->type);
            light->colour = BR_COLOUR_RGB(255, 255, 255);
            light->attenuation_c = source->intensity;
            auto actor = BrActorAllocate(BR_ACTOR_LIGHT, light);
            if (actor)
            {
                BrActorAdd(static_cast<br_actor *>(world), actor);
                actor->t.type = BR_TRANSFORM_MATRIX34;
                std::memcpy(&actor->t.t.mat, &source->matrix, sizeof(actor->t.t.mat));
            }
            return actor;
        };
        s.enable_light = [](void *, void *a) { BrLightEnable(static_cast<br_actor *>(a)); };
        s.disable_light = [](void *, void *a) { BrLightDisable(static_cast<br_actor *>(a)); };
        s.add_model = [](void *, void *world, void *model) -> int {
            auto actor = BrActorAllocate(BR_ACTOR_MODEL, nullptr);
            if (!actor)
                return 0;
            BrActorAdd(static_cast<br_actor *>(world), actor);
            actor->model = static_cast<br_model *>(model);
            return 1;
        };
        s.add_mesh = [](void *v, void *world) -> int {
            return static_cast<RenderHost *>(v)->mesh->AddToScene(static_cast<br_actor *>(world));
        };
        s.material = [](void *, void *a) -> void * { return static_cast<br_actor *>(a)->material; };
        s.face_material = [](void *, void *m, MMU32 i) -> void * {
            return static_cast<br_model *>(m)->face_groups[i].material;
        };
        s.material_count = [](void *, void *m) -> MMU32 { return static_cast<br_model *>(m)->nface_groups; };
        s.read_material = [](void *, void *v, MMPreviewMaterial *out) {
            auto m = static_cast<br_material *>(v);
            out->material = m;
            out->flags = m->flags;
            out->map = m->colour_map;
            out->base = m->index_base;
            out->range = m->index_range;
        };
        s.write_material = [](void *, const MMPreviewMaterial *m) {
            auto out = static_cast<br_material *>(m->material);
            out->flags = m->flags;
            out->colour_map = static_cast<br_pixelmap *>(m->map);
            out->index_base = m->base;
            out->index_range = m->range;
            BrMaterialUpdate(out, BR_MATU_ALL);
        };
        s.light_flag = BR_MATF_LIGHT;
        s.prelit_flag = BR_MATF_PRELIT;
        s.smooth_flag = BR_MATF_SMOOTH;
        s.edges = [](void *, void *a) { static_cast<br_actor *>(a)->render_style = BR_RSTYLE_EDGES; };
        s.vertices = [](void *, void *m) -> MMU32 { return static_cast<br_model *>(m)->nprepared_vertices; };
        s.faces = [](void *, void *m) -> MMU32 { return static_cast<br_model *>(m)->nprepared_faces; };
        s.vertex = [](void *, void *m, MMU32 i, MMI32 out[3]) {
            auto v = &static_cast<br_model *>(m)->prepared_vertices[i];
            for (unsigned a = 0; a < 3; ++a)
                out[a] = v->p.v[a];
        };
        s.face = [](void *, void *m, MMU32 i, unsigned short out[3]) {
            auto f = &static_cast<br_model *>(m)->prepared_faces[i];
            std::copy(f->vertices, f->vertices + 3, out);
        };
        s.relative = [](void *, void *a, void *c, MMI32 out[12]) {
            br_matrix34 m;
            BrActorToActorMatrix34(&m, static_cast<br_actor *>(a), static_cast<br_actor *>(c));
            std::memcpy(out, &m, sizeof(m));
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
        s.render = [](void *, void *w, void *c, void *p, void *d) {
            BrZbSceneRender(static_cast<br_actor *>(w), static_cast<br_actor *>(c), static_cast<br_pixelmap *>(p),
                            static_cast<br_pixelmap *>(d));
        };
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
        s.depth_projection = [](void *, void *c, MMI32 out[16]) {
            br_matrix4 m;
            BrActorToScreenMatrix4(&m, static_cast<br_actor *>(c), static_cast<br_actor *>(c));
            std::memcpy(out, &m, sizeof(m));
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
        return s;
    }
};

#endif
