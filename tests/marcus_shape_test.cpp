// Headless transform, undo and native snapshot checks; no application input.
#include "player.h"
#include "asm_renderer.h"
#include <SDL3/SDL.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>
int main(int argc,char **argv)
{
    if (argc!=4 && argc!=5) return 2;
    using Clock=std::chrono::steady_clock;
    std::unique_ptr<MarcusPlayer,decltype(&marcus_destroy)> player(marcus_create(),marcus_destroy);
    MarcusObject object={}; MarcusMovieTime timing={},after={}; MarcusImage image={};
    auto check=[&](bool ok,const char *message) {
        if (!ok) throw std::runtime_error(std::string(message)+": "+marcus_error(player.get()));
    };
    std::string snapshotContext;
    try
    {
        auto checkNativeSnapshot = [&] {
            MarcusInfo info;
            check(marcus_info(player.get(),&info),"Read snapshot frame");
            // Retail actor attachment order depends on transport history. Compare
            // both renderers after the same replay from the scene's beginning.
            check(marcus_select_scene(player.get(),info.scene) && marcus_seek(player.get(),info.frame),"Replay snapshot frame");
            check(marcus_set_renderer(player.get(), MARCUS_RENDERER_BRENDER) &&
                  marcus_render(player.get(), 160, 90, 1, 0, 0, 1, &image), "Render live structural edits");
            const long frame = image.frame;
            const std::vector<unsigned char> expected(image.rgba, image.rgba + image.pitch * image.height);
            std::unique_ptr<MarcusFramePreviews, decltype(&marcus_frame_previews_destroy)> helper(
                marcus_frame_previews_create(player.get()), marcus_frame_previews_destroy);
            check(bool(helper), "Materialize live scene, prop and camera together");
            marcus_frame_previews_request(helper.get(), &frame, 1);
            char error[1024] = {};
            int ready = 0;
            const auto deadline = Clock::now() + std::chrono::seconds(20);
            while (!(ready = marcus_frame_previews_take(helper.get(), &image, error, sizeof(error))) && Clock::now() < deadline)
                SDL_Delay(5);
            check(ready == 1 && image.width == 160 && image.height == 90 &&
                  std::equal(expected.begin(), expected.end(), image.rgba), (snapshotContext+" frame "+std::to_string(frame)+" native snapshot matches live render").c_str());
        };
        const char *shapeMovie=argv[1];
        auto exerciseShape = [&](int objectIndex,int renderer) {
            snapshotContext=std::string(shapeMovie)+" object "+std::to_string(objectIndex);
            check(marcus_open(player.get(),shapeMovie,argv[2],argv[3]) && marcus_seek(player.get(),10),"Open shape fixture");
            check(marcus_set_renderer(player.get(),renderer) && marcus_movie_time(player.get(),&timing),"Prime shape editor");
            const long frames[]={1,10,20,22,25,30}; MarcusObject originalPoses[6];
            for (int i=0;i<6;++i) check(marcus_seek(player.get(),frames[i]) && marcus_object(player.get(),objectIndex,&originalPoses[i]),"Read authored shape path");
            check(marcus_seek(player.get(),10),"Select shape frame");
            MarcusObjectShape original,target;
            float left[9],right[9];
            check(marcus_object_shape(player.get(),objectIndex,&original,left,right),"Read object shape and gizmo frame");
            target=original;
            const float rotation[9]={0,1,0,-1,0,0,0,0,1};
            std::copy(rotation,rotation+9,target.rotation);
            target.scale[0]=1.25f; target.scale[1]=.75f; target.scale[2]=1.5f;
            check(marcus_set_orbit_target(player.get(),objectIndex) && marcus_render(player.get(),160,90,0,25,-10,1.2f,&image),"Render original shape orbit");
            const auto projection=image.projection;
            const std::vector<unsigned char> pixels(image.rgba,image.rgba+image.pitch*image.height);
            auto stationary=[&] {
                for (int i=0;i<12;++i) check(std::abs(projection.transform[i]-image.projection.transform[i])<.0001f,"Shape editing holds orbit camera");
            };
            check(marcus_preview_object_shape(player.get(),objectIndex,&target) &&
                  marcus_render(player.get(),160,90,0,25,-10,1.2f,&image),"Preview rotation and size");
            stationary();
            check(marcus_movie_time_async(player.get(),&after)==1 && after.duration_seconds==timing.duration_seconds,"Shape preview retains duration");
            check(marcus_preview_object_shape(player.get(),objectIndex,&original) &&
                  marcus_render(player.get(),160,90,0,25,-10,1.2f,&image) &&
                  std::equal(pixels.begin(),pixels.end(),image.rgba),"Cancelled shape restores exact pixels");
            stationary();
            MarcusObjectShape invalid=target;
            invalid.scale[0]=0;
            check(!marcus_preview_object_shape(player.get(),objectIndex,&invalid),"Reject collapsed size");
            invalid=target; invalid.rotation[0]=1;
            check(!marcus_set_object_shape(player.get(),objectIndex,&invalid),"Reject malformed rotation");
            check(marcus_preview_object_shape(player.get(),objectIndex,&target) &&
                  marcus_set_object_shape(player.get(),objectIndex,&target) &&
                  marcus_render(player.get(),160,90,0,25,-10,1.2f,&image),"Commit rotation and size");
            stationary();
            check(marcus_undo(player.get()) && marcus_render(player.get(),160,90,0,25,-10,1.2f,&image) &&
                  std::equal(pixels.begin(),pixels.end(),image.rgba),"Shape undo restores exact pixels");
            stationary();
            check(marcus_set_object_shape(player.get(),objectIndex,&target),"Restore shape for replay checks");
            for (int i=0;i<6;++i)
            {
                check(marcus_seek(player.get(),frames[i]) && marcus_object(player.get(),objectIndex,&object),"Read transformed object path");
                const auto &a=originalPoses[i];
                check(object.x==a.x && object.y==a.y && object.z==a.z && object.action==a.action && object.cel==a.cel &&
                      object.first_frame==a.first_frame && object.last_frame==a.last_frame,"Rotation and size retain path, animation and lifetime");
                if (renderer==MARCUS_RENDERER_BRENDER) checkNativeSnapshot();
            }
            check(marcus_select_scene(player.get(),1) && marcus_seek(player.get(),10) &&
                  marcus_object_shape(player.get(),objectIndex,&invalid,nullptr,nullptr) && invalid.scale[0]==1,"Shape edits stay scene-local");
            check(marcus_select_scene(player.get(),0) && marcus_reload(player.get(),argv[2],argv[3]) &&
                  marcus_seek(player.get(),10) && marcus_object_shape(player.get(),objectIndex,&invalid,nullptr,nullptr) &&
                  invalid.scale[0]==target.scale[0],"Reload retains object shape");
            check(marcus_undo(player.get()) && !marcus_modified(player.get()) &&
                  marcus_object_shape(player.get(),objectIndex,&invalid,nullptr,nullptr) && invalid.scale[0]==1,"Undo restores object shape");
        };
        check(marcus_open(player.get(), shapeMovie, argv[2], argv[3]), "Open moving actor for camera copy");
        MarcusObject poses[2] = {};
        check(marcus_seek(player.get(), 20) && marcus_object(player.get(), 0, &poses[1]) &&
              marcus_seek(player.get(), 10) && marcus_object(player.get(), 0, &poses[0]) &&
              (poses[0].x != poses[1].x || poses[0].y != poses[1].y || poses[0].z != poses[1].z),
              "Fixture has a changing route position");
        MarcusCamera fly;
        MarcusBounds oldBounds, newBounds;
        check(marcus_match_camera(player.get(), 320, 180, &fly) && marcus_object_bounds(player.get(), 0, &oldBounds) &&
              oldBounds.valid, "Read the moving actor in the original shot");
        fly.x += 1; fly.y += 2; fly.yaw += 11; fly.pitch += 9;
        MarcusImage work;
        check(marcus_render_fly(player.get(), 320, 180, &fly, &work), "Look at the moving actor from Fly");
        auto viewPoint = [](const MarcusProjection &view, const float *p, int axis) {
            double out = 0;
            for (int j = 0; j < 3; ++j) out += (p[j] - view.transform[9 + j]) * view.transform[axis * 3 + j];
            return out;
        };
        MarcusInfo copyInfo;
        MarcusProjection fixed;
        check(marcus_info(player.get(), &copyInfo) && marcus_camera_projection(player.get(), copyInfo.camera, &fixed), "Read the fixed camera");
        const double lens = std::tan(fixed.vertical_fov * .00872664626) / std::tan(work.projection.vertical_fov * .00872664626);
        const double scale[] = {lens * fixed.aspect / work.projection.aspect, lens, 1};
        auto samePoint = [&](const float *before, const float *after, const char *kind) {
            for (int axis = 0; axis < 3; ++axis)
            {
                const auto expected = viewPoint(work.projection, before, axis) * scale[axis];
                const auto actual = viewPoint(fixed, after, axis);
                check(std::abs(expected - actual) < .03, (std::string(kind) + " matches in the fixed camera, axis " +
                    std::to_string(axis) + ": expected " + std::to_string(expected) + ", got " + std::to_string(actual)).c_str());
            }
        };
        check(marcus_copy_to_camera(player.get(), &work) &&
              marcus_object_bounds(player.get(), 0, &newBounds) && newBounds.valid, "Reframe a moving actor");
        for (int corner = 0; corner < 8; ++corner)
            samePoint(oldBounds.corners + corner * 3, newBounds.corners + corner * 3, "Actor corner");
        for (int i = 0; i < 2; ++i)
        {
            check(marcus_seek(player.get(), i ? 20 : 10) && marcus_object(player.get(), 0, &object), "Read the reframed route");
            const float before[] = {poses[i].x, poses[i].y, poses[i].z}, after[] = {object.x, object.y, object.z};
            samePoint(before, after, i ? "Later position" : "Copied frame position");
            check(object.first_frame == poses[i].first_frame && object.last_frame == poses[i].last_frame &&
                  object.action == poses[i].action && object.cel == poses[i].cel, "Reframing preserves route timing");
        }
        check(marcus_undo(player.get()) && marcus_object(player.get(), 0, &object) && object.x == poses[0].x &&
              object.y == poses[0].y && object.z == poses[0].z, "Camera copy is one undo step");

        for (int index : {0,1,3}) exerciseShape(index,MARCUS_RENDERER_BRENDER);
        if (argc==5)
        {
            shapeMovie=argv[4];
            exerciseShape(0,MARCUS_RENDERER_BRENDER);
            shapeMovie=argv[1];
        }

        MM::Error error;
        if (MarcusASMRenderer::Available(error))
            for (int index : {0,1,3}) exerciseShape(index,MARCUS_RENDERER_BRENDER_ASM);
        std::puts("Rotation and size: actor/prop/text preview, cancellation, path/timing, undo, reload, native snapshots and ASM passed");
        return 0;
    }
    catch (const std::exception &error) { std::fprintf(stderr,"%s\n",error.what()); return 1; }
}
