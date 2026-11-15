#ifndef MM_PREVIEW_PICK_HOST_H
#define MM_PREVIEW_PICK_HOST_H
// Typed SDK primitives for the C89 picking owner. Included in MM.
struct Preview::Impl::PickHost
{
    static int BR_CALLBACK Face(br_model *, br_material *material, br_vector3 *, br_vector3 *, br_scalar distance, int,
                                int, int, br_vector3 *, br_vector2 *map, void *context)
    {
        mm_preview_pick_face(static_cast<MMPreviewPickState *>(context), BrScalarToFloat(distance), material, map);
        return 0;
    }
    static int BR_CALLBACK Model(br_actor *actor, br_model *model, br_material *material, br_vector3 *position,
                                 br_vector3 *direction, br_scalar nearClip, br_scalar farClip, void *context)
    {
        mm_preview_pick_model(static_cast<MMPreviewPickState *>(context), actor, model, material, position, direction,
                              nearClip, farClip);
        return 0;
    }
    static MMPreviewPickServices Services(const MMPreviewRenderServices &render)
    {
        MMPreviewPickServices s = {};
        s.render = &render;
        s.invertible = [](void *, void *v) -> int {
            br_matrix34 inverse;
            return BrMatrix34Inverse(&inverse, &static_cast<br_actor *>(v)->t.t.mat) != 0;
        };
        s.perspective = [](void *, const MMPreviewCamera *c, MMI32 out[16]) {
            br_matrix4 matrix;
            BrMatrix4Perspective(&matrix, c->fov, c->aspect, -c->near_z, -c->far_z);
            static_assert(sizeof(matrix) == 16 * sizeof(*out), "Pick projection layout");
            std::memcpy(out, &matrix, sizeof(matrix));
        };
        s.scene_pick = [](void *, void *world, void *camera, MMU32 w, MMU32 h, MMI32 x, MMI32 y,
                          MMPreviewPickState *state) -> int {
            br_pixelmap viewport = {};
            viewport.width = static_cast<br_uint_16>(w);
            viewport.height = static_cast<br_uint_16>(h);
            BrScenePick2D(static_cast<br_actor *>(world), static_cast<br_actor *>(camera), &viewport, x, y, Model,
                          state);
            return !Internal::AssertionFailed();
        };
        s.face_bytes = sizeof(br_face);
        s.face_count = [](void *, void *v) -> MMU32 { return static_cast<br_model *>(v)->nprepared_faces; };
        s.prepare_face = [](void *, void *v, MMU32 index, void *out) {
            auto model = static_cast<br_model *>(v);
            auto face = static_cast<br_face *>(out);
            *face = model->prepared_faces[index];
            br_vector3 normal;
            for (int axis = 0; axis < 3; ++axis)
                normal.v[axis] = BrFractionToScalar(face->n.v[axis]);
            face->d = BrVector3Dot(&normal, &model->prepared_vertices[face->vertices[0]].p);
        };
        s.model_pick = [](void *, void *v, void *faces, void *material, void *position, void *direction, MMI32 nearClip,
                          MMI32 farClip, MMPreviewPickState *state) {
            auto model = static_cast<br_model *>(v);
            auto prepared = *model;
            prepared.faces = static_cast<br_face *>(faces);
            prepared.vertices = model->prepared_vertices;
            prepared.nfaces = model->nprepared_faces;
            prepared.nvertices = model->nprepared_vertices;
            BrModelPick2D(&prepared, static_cast<br_material *>(material), static_cast<br_vector3 *>(position),
                          static_cast<br_vector3 *>(direction), nearClip, farClip, Face, state);
        };
        s.texture = [](void *, void *v, MMPreviewPickTexture *out) -> int {
            auto material = static_cast<br_material *>(v);
            auto texture = material->colour_map;
            if (!texture)
                return 0;
            out->opaque = (material->flags & BR_MATF_MAP_OPAQUE) != 0;
            out->indexed = texture->type == BR_PMT_INDEX_8;
            out->pixels = static_cast<const unsigned char *>(texture->pixels);
            out->width = texture->width;
            out->height = texture->height;
            out->stride = texture->row_bytes;
            out->base_x = texture->base_x;
            out->base_y = texture->base_y;
            return 1;
        };
        s.map = [](void *, void *v, void *map, float out[2]) {
            br_vector2 uv;
            BrMatrix23ApplyP(&uv, static_cast<br_vector2 *>(map), &static_cast<br_material *>(v)->map_transform);
            out[0] = BrScalarToFloat(uv.v[0]);
            out[1] = BrScalarToFloat(uv.v[1]);
        };
        return s;
    }
};
#endif
