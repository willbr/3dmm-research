/* Headless camera interaction and metadata compatibility probe. */
#ifndef PREVIEW_CAMERA_FIXTURE_H
#define PREVIEW_CAMERA_FIXTURE_H
static bool TestPreviewCamera(MM::Preview &preview)
{
    const auto path = std::getenv("MM_PREVIEW_CAMERA_SNAPSHOT");
    FILE *trace = path ? std::fopen(path, "wb") : std::tmpfile();
    if (!trace)
        return false;
    std::uint32_t bytes = 0, fingerprint = 2166136261U;
    auto record = [&](const void *data, std::size_t size) {
        auto p = static_cast<const unsigned char *>(data);
        for (std::size_t i = 0; i < size; ++i)
            fingerprint = (fingerprint ^ p[i]) * 16777619U;
        bytes += static_cast<std::uint32_t>(size);
        return std::fwrite(data, 1, size, trace) == size;
    };
    auto projection = [&](const MM::PreviewProjection &p) {
        const int flags[] = {p.valid, p.orthographic};
        const float values[] = {p.verticalFov, p.aspect, p.nearClip, p.farClip, p.displayDepth, p.orthoHeight};
        return record(flags, sizeof(flags)) && record(values, sizeof(values)) &&
               record(p.transform, sizeof(p.transform));
    };
    auto camera = [&](const MM::PreviewCamera &c) {
        const float values[] = {c.x, c.y, c.z, c.yaw, c.pitch, c.roll, c.moveSpeed, c.verticalFov};
        return record(values, sizeof(values));
    };
    auto image = [&](const MM::PreviewImage &i) {
        return projection(i.projection) && record(&i.coveredPixels, sizeof(i.coveredPixels)) &&
               record(i.rgba.data(), i.rgba.size());
    };
    MM::Error error;
    auto result = [&](bool ok) {
        const int values[] = {ok, static_cast<int>(error.code)};
        return record(values, sizeof(values));
    };
    auto bounds = [&](int object) {
        MM::PreviewBounds b;
        bool ok = preview.ObjectBounds(object, b, error);
        const int valid = b.valid;
        return result(ok) && record(&valid, sizeof(valid)) && record(&b.colour, sizeof(b.colour)) &&
               record(b.corners, sizeof(b.corners));
    };
    bool ok = preview.Select({0x5343454e, 0}, error);
    for (int frame : {1, 5})
    {
        ok = ok && preview.SetFrame(frame, error);
        const auto active = preview.Info().currentCamera;
        for (unsigned slot = 0; ok && slot <= preview.Info().cameraCount; ++slot)
            for (int repeat = 0; ok && repeat < 2; ++repeat)
            {
                MM::PreviewProjection p;
                const bool read = preview.CameraProjection(slot, p, error);
                ok = read == (slot < preview.Info().cameraCount) && result(read) && projection(p) &&
                     preview.Info().currentCamera == active && preview.Info().currentFrame == frame;
            }
        float point[3] = {-1, -1, -1};
        const bool placed = preview.PlacementPoint(point);
        ok = ok && record(&placed, sizeof(placed)) && record(point, sizeof(point));
        for (int object = -1; ok && object <= int(preview.Info().actors); ++object)
            ok = bounds(object);
        for (float aspect : {.75f, 1.f, 16.f / 9, 2.5f})
            for (int mode = 0; ok && mode < 4; ++mode)
            {
                preview.ResetOrbitFraming();
                MM::PreviewCamera matched;
                ok = preview.MatchSceneCamera(aspect, matched, error) && result(true) && camera(matched);
                MM::PreviewView view;
                view.width = mode % 2 ? 128 : 96;
                view.height = 96;
                view.authoredCamera = false;
                view.stableOrbit = true;
                view.orthographic = mode >= 2;
                view.zoom = mode % 2 ? 2.5f : .5f;
                view.yaw = matched.yaw + 23.75f;
                view.pitch = -17.125f;
                view.roll = matched.roll + 12.25f;
                view.verticalFov = matched.verticalFov;
                MM::PreviewImage before, after;
                ok = ok && preview.Render(view, before, error) && image(before);
                for (int step = 0; ok && step < 3; ++step)
                {
                    const float right = step == 0 ? .125f : step == 1 ? -.25f : 100000;
                    const float up = step == 0 ? -.2f : step == 1 ? .1f : -100000;
                    const bool panned = preview.PanOrbit(view, right, up, error);
                    ok = panned && result(panned) && preview.Render(view, after, error) && image(after);
                }
                const auto prior = after.rgba;
                const bool rejected = preview.PanOrbit(view, float(HUGE_VAL), 0, error);
                ok = ok && !rejected && result(rejected) && preview.Render(view, after, error) && prior == after.rgba;
            }
        for (float aspect : {0.f, -1.f, float(HUGE_VAL)})
        {
            MM::PreviewCamera c;
            const bool matched = preview.MatchSceneCamera(aspect, c, error);
            ok = ok && !matched && result(matched) && camera(c);
        }
    }
    ok = ok && preview.SetFrame(1, error);
    MM::PreviewView view;
    view.width = 128;
    view.height = 96;
    view.authoredCamera = false;
    view.stableOrbit = true;
    view.orbitObject = 0;
    preview.ResetOrbitFraming();
    MM::PreviewImage rendered;
    ok = ok && preview.Render(view, rendered, error) && image(rendered);
    const float delta[3] = {.25f, -.5f, .75f}, zero[3] = {};
    ok = ok && preview.TranslateObject(0, delta, error) && bounds(0) && preview.Render(view, rendered, error) &&
         image(rendered) && preview.SetObjectOffset(0, zero, error) && bounds(0) &&
         preview.Render(view, rendered, error) && image(rendered);
    MM::PreviewObjectShape original, shape;
    float left[9], right[9];
    ok = ok && preview.ObjectShape(0, original, left, right, error);
    shape = original;
    shape.scale[0] *= 1.25f;
    shape.scale[1] *= .75f;
    ok = ok && preview.SetObjectShape(0, shape, error) && bounds(0) && preview.Render(view, rendered, error) &&
         image(rendered) && preview.SetObjectShape(0, original, error);
    ok = ok && preview.Select({0x41435452, 1}, error);
    MM::PreviewCamera c;
    const bool matched = preview.MatchSceneCamera(1, c, error);
    ok = ok && !matched && result(matched) && camera(c);
    ok = std::fclose(trace) == 0 && ok;
#ifdef _WIN64
    ok = ok && bytes == 5728850 && fingerprint == 0x5A0D32E5U;
#endif
    std::printf("Preview camera trace: bytes=%u fingerprint=%08X\n", bytes, fingerprint);
    if (!ok)
        std::fprintf(stderr, "Camera interaction probe: %s\n", error.message.c_str());
    return ok;
}
#endif
