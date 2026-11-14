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
        CHECK(ThreeMM::Internal::CopyBodyActorTree(world, &source[0], &owners, 42));
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
