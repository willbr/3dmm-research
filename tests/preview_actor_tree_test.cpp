#include <algorithm>
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <map>
#include <vector>
#include "soc_core.h"
#undef size
#include "preview_actor_tree.h"
ASSERTNAME

#define CHECK(value) do { if (!(value)) { std::fprintf(stderr, "Actor tree check failed at %d\n", __LINE__); std::exit(1); } } while (0)

struct Pixels
{
    std::vector<unsigned char> colour = std::vector<unsigned char>(128 * 96);
    std::vector<unsigned short> depth = std::vector<unsigned short>(128 * 96, 0xffff);
    Pixels(br_actor *world, br_actor *eye)
    {
        auto c = BrPixelmapAllocate(BR_PMT_INDEX_8, 128, 96, colour.data(), BR_PMAF_NORMAL);
        auto z = BrPixelmapAllocate(BR_PMT_DEPTH_16, 128, 96, depth.data(), BR_PMAF_NORMAL);
        CHECK(c && z);
        BrZbSceneRender(world, eye, c, z);
        BrPixelmapFree(z);
        BrPixelmapFree(c);
    }
    bool operator==(const Pixels &other) const { return colour == other.colour && depth == other.depth; }
};

static int BR_CALLBACK CountPick(br_actor *, br_model *, br_material *, br_vector3 *, br_vector3 *,
                                 br_scalar, br_scalar, void *context)
{
    ++*static_cast<int *>(context);
    return 0;
}
struct FaceHit
{
    int count, face;
    br_scalar depth;
};

static int BR_CALLBACK RecordFaceHit(br_model *, br_material *, br_vector3 *, br_vector3 *, br_scalar depth,
                                      int face, int, int, br_vector3 *, br_vector2 *, void *context)
{
    auto hit = static_cast<FaceHit *>(context);
    ++hit->count;
    hit->face = face;
    hit->depth = depth;
    return 0;
}

static void TestPickPlaneDepth()
{
    br_vector3 origin = {{0, 0, BR_SCALAR(-2)}}, direction = {{0, 0, 2}};
    br_vertex vertices[6] = {};
    br_face faces[2] = {};
    br_model model = {};
    br_material material = {};
    material.flags = BR_MATF_TWO_SIDED;
    model.vertices = vertices;
    model.faces = faces;
    model.nvertices = 6;
    model.nfaces = 2;
    vertices[0].p = {{BR_SCALAR(-1), BR_SCALAR(-1), 0}};
    vertices[1].p = {{BR_SCALAR(1), BR_SCALAR(-1), 0}};
    vertices[2].p = {{0, BR_SCALAR(1), 0}};
    for (int i = 0; i < 3; ++i)
    {
        faces[0].vertices[i] = i;
        faces[1].vertices[i] = i + 3;
        vertices[i + 3].p = vertices[i].p;
        vertices[i + 3].p.v[2] = origin.v[2] + 4;
    }
    faces[0].n.v[2] = faces[1].n.v[2] = BR_FRACTION(-1);
    faces[1].d = -vertices[3].p.v[2];

    // Face 0 intersects at 65536.0, beyond the fixed range. A wrap can
    // report it at zero. Face 1 intersects the same ray at 2.0.
    FaceHit hit = {};
    BrModelPick2D(&model, &material, &origin, &direction, 0, BR_SCALAR_MAX, RecordFaceHit, &hit);
    CHECK(hit.count == 1 && hit.face == 1 && hit.depth == BR_SCALAR(2));

    // The plane-origin difference exceeds 32 signed bits, but the depth fits.
    hit = {};
    model.nfaces = 1;
    faces[0].d = BR_SCALAR(30000);
    origin.v[2] = BR_SCALAR(30000);
    direction.v[2] = BR_SCALAR(-4);
    for (int i = 0; i < 3; ++i)
        vertices[i].p.v[2] = BR_SCALAR(-30000);
    BrModelPick2D(&model, &material, &origin, &direction, 0, BR_SCALAR(20000), RecordFaceHit, &hit);
    CHECK(hit.count == 1 && hit.face == 0 && hit.depth == BR_SCALAR(15000));
}

static void TestPickInverses(br_model *model)
{
    auto world = BrActorAllocate(BR_ACTOR_NONE, nullptr);
    auto eye = BrActorAdd(world, BrActorAllocate(BR_ACTOR_CAMERA, nullptr));
    auto actor = BrActorAdd(world, BrActorAllocate(BR_ACTOR_MODEL, nullptr));
    auto camera = static_cast<br_camera *>(eye->type_data);
    actor->model = model;
    actor->t.type = eye->t.type = BR_TRANSFORM_MATRIX34;
    BrMatrix34Translate(&actor->t.t.mat, 0, 0, BR_SCALAR(-5));
    camera->aspect = BR_SCALAR(1);
    camera->hither_z = BR_SCALAR(1);
    camera->yon_z = BR_SCALAR(20);
    br_pixelmap viewport = {};
    viewport.width = viewport.height = 128;
    auto count = [&]() {
        int hits = 0;
        BrScenePick2D(world, eye, &viewport, 0, 0, CountPick, &hits);
        return hits;
    };
    CHECK(count() == 1);
    std::memset(&actor->t.t.mat, 0, sizeof(actor->t.t.mat));
    CHECK(count() == 0);
    BrMatrix34Translate(&actor->t.t.mat, 0, 0, BR_SCALAR(-5));
    CHECK(count() == 1);
    std::memset(&eye->t.t.mat, 0, sizeof(eye->t.t.mat));
    CHECK(count() == 0);
    BrMatrix34Identity(&eye->t.t.mat);
    // The camera is invertible, but normalized orthographic world-to-model
    // scaling underflows the fixed-point determinant. No unwritten inverse
    // may reach the bounds test or callback, regardless of stack contents.
    camera->type = BR_CAMERA_PARALLEL;
    camera->width = camera->height = BR_SCALAR(100);
    camera->yon_z = BR_SCALAR(30000);
    CHECK(count() == 0);
    camera->width = camera->height = BR_SCALAR(1);
    camera->yon_z = BR_SCALAR(20);
    CHECK(count() == 1);
    auto bounds = BrActorAdd(world, BrActorAllocate(BR_ACTOR_BOUNDS, &model->bounds));
    bounds->t.type = BR_TRANSFORM_MATRIX34;
    std::memset(&bounds->t.t.mat, 0, sizeof(bounds->t.t.mat));
    BrActorRemove(actor);
    BrActorAdd(bounds, actor);
    CHECK(count() == 0);
    BrActorFree(world);
}
void SameTree(const br_actor *source, const br_actor *copy, const std::map<br_actor *, int> &owners)
{
    CHECK(source != copy && source->type == copy->type && source->model == copy->model);
    CHECK(source->material == copy->material && source->render_style == copy->render_style);
    CHECK(!std::memcmp(&source->t, &copy->t, sizeof(source->t)));
    if (source->type == BR_ACTOR_MODEL && source->model) CHECK(owners.at(const_cast<br_actor *>(copy)) == 42);
    auto a = source->children;
    auto b = copy->children;
    while (a && b)
    {
        CHECK(a->parent == source && b->parent == copy && b->depth == copy->depth + 1);
        SameTree(a, b, owners);
        a = a->next;
        b = b->next;
    }
    CHECK(!a && !b);
}

int main()
{
    BrBegin();
    BrZbBegin(BR_PMT_INDEX_8, BR_PMT_DEPTH_16);
    // Verified against the pinned retail EXE: BrMatrix34Mul (RVA 0x76ac4)
    // and BrMatrix4Pre34 (0x7c84a) shift each product before adding. Keep
    // this distinct from the standalone full-precision ASM primitive.
    CHECK(BR_MAC3(1, 32768, 1, 32768, 0, 0) == 0);
    CHECK(BR_MAC3(-1, 32768, 1, 32768, 0, 0) == -1);
    CHECK(BrFixedMac3(1, 32768, 1, 32768, 0, 0) == 1);
    br_matrix34 a, b, product;
    BrMatrix34Identity(&a);
    BrMatrix34Identity(&b);
    a.m[0][0] = a.m[0][1] = 1;
    a.m[0][2] = 0;
    b.m[0][0] = b.m[1][0] = 32768;
    BrMatrix34Mul(&product, &a, &b);
    CHECK(product.m[0][0] == 0);
    br_matrix4 projective;
    BrMatrix4Copy34(&projective, &b);
    BrMatrix4Pre34(&projective, &a);
    CHECK(projective.m[0][0] == 0);
    auto model = BrModelAllocate(nullptr, 3, 1);
    CHECK(model);
    model->vertices[0].p = {{BR_SCALAR(-1.25), BR_SCALAR(-1.1), 0}};
    model->vertices[1].p = {{BR_SCALAR(1.3), BR_SCALAR(-1.1), 0}};
    model->vertices[2].p = {{BR_SCALAR(.2), BR_SCALAR(1.2), 0}};
    model->faces[0].vertices[0] = 0;
    model->faces[0].vertices[1] = 1;
    model->faces[0].vertices[2] = 2;
    BrModelAdd(model);
    TestPickInverses(model);
    TestPickPlaneDepth();
    auto red = BrMaterialAllocate(nullptr);
    auto blue = BrMaterialAllocate(nullptr);
    CHECK(red && blue);
    red->flags = blue->flags = 0; // Exercise the one-sided assembly projection path.
    red->index_base = 70;
    blue->index_base = 150;
    red->index_range = blue->index_range = 0;
    BrMaterialAdd(red);
    BrMaterialAdd(blue);

    // Body-like embedded actors, including empty nodes, inherited materials,
    // sibling overlap, and nontrivial local transforms. They cannot be freed
    // or reparented by the preview's temporary render world.
    br_actor source[5] = {};
    for (auto &actor : source)
    {
        actor.t.type = BR_TRANSFORM_MATRIX34;
        BrMatrix34Identity(&actor.t.t.mat);
    }
    source[2].type = source[3].type = source[4].type = BR_ACTOR_MODEL;
    source[2].model = source[3].model = source[4].model = model;
    source[1].material = red;
    source[3].material = blue;
    source[4].render_style = BR_RSTYLE_NONE;
    BrActorAdd(&source[0], &source[1]);
    BrActorAdd(&source[1], &source[2]);
    BrActorAdd(&source[1], &source[3]);
    BrActorAdd(&source[2], &source[4]);
    bool detectsFlattening = false;
    std::uint64_t pixelHash = UINT64_C(14695981039346656037);
    for (int sample = 0; sample < 24; ++sample)
    {
        BrMatrix34RotateY(&source[0].t.t.mat, BR_ANGLE_DEG(13.1 + sample * 1.73));
        BrMatrix34PostTranslate(&source[0].t.t.mat, BR_SCALAR(.123), BR_SCALAR(-.271), BR_SCALAR(-5.31));
        BrMatrix34RotateZ(&source[1].t.t.mat, BR_ANGLE_DEG(7.39 + sample * .37));
        BrMatrix34RotateX(&source[2].t.t.mat, BR_ANGLE_DEG(3.17));
        source[3].t = source[2].t; // Equal-depth siblings expose draw-order changes.
        auto world = BrActorAllocate(BR_ACTOR_NONE, nullptr);
        auto eye = BrActorAdd(world, BrActorAllocate(BR_ACTOR_CAMERA, nullptr));
        auto camera = static_cast<br_camera *>(eye->type_data);
        camera->hither_z = BR_SCALAR(.1);
        camera->yon_z = BR_SCALAR(100);
        camera->aspect = BR_SCALAR(128.0 / 96);
        BrMatrix34RotateY(&eye->t.t.mat, BR_ANGLE_DEG(-4.13));
        BrActorAdd(world, &source[0]);
        Pixels original(world, eye);
        for (auto value : original.colour) pixelHash = (pixelHash ^ value) * UINT64_C(1099511628211);
        for (auto value : original.depth)
        {
            pixelHash = (pixelHash ^ (value & 255)) * UINT64_C(1099511628211);
            pixelHash = (pixelHash ^ (value >> 8)) * UINT64_C(1099511628211);
        }
        CHECK(std::count(original.colour.begin(), original.colour.end(), 150) > 10);
        BrActorRemove(&source[0]);
        std::map<br_actor *, int> owners;
        CHECK(MM::Internal::CopyBodyActorTree(world, &source[0], &owners, 42));
        auto copy = world->children;
        SameTree(&source[0], copy, owners);
        CHECK(owners.size() == 3 && !source[0].parent);
        CHECK(original == Pixels(world, eye));
        BrActorRemove(copy);
        BrActorFree(copy);

        // The former flattening must fail this fixture: it changes the order
        // in which fixed-point local and camera matrices are multiplied.
        auto flat = BrActorAdd(world, BrActorAllocate(BR_ACTOR_MODEL, nullptr));
        flat->model = model;
        flat->material = blue;
        BrMatrix34Identity(&flat->t.t.mat);
        for (auto part = &source[3]; part; part = part->parent)
        {
            br_matrix34 composed;
            BrMatrix34Mul(&composed, &flat->t.t.mat, &part->t.t.mat);
            flat->t.t.mat = composed;
        }
        detectsFlattening |= !(original == Pixels(world, eye));
        BrActorFree(world);
        CHECK(!source[0].parent && source[0].children == &source[1] && source[1].children == &source[3]);
    }
    CHECK(detectsFlattening);
    // Independent reference from the x86 mesh386.asm projector and scanner.
    // This also detects a return to the earlier-rounding generic C projector.
    CHECK(pixelHash == UINT64_C(12017058143299196623));
    BrMaterialRemove(blue); BrMaterialFree(blue);
    BrMaterialRemove(red); BrMaterialFree(red);
    BrModelRemove(model); BrModelFree(model);
    BrZbEnd();
    BrEnd();
    std::puts("Actor tree pixels, depth, sibling order, inheritance and embedded ownership passed");
}
