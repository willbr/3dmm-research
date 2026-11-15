/* Headless live definitions, camera editing and seek preservation probe. */
#ifndef PREVIEW_LIVE_FIXTURE_H
#define PREVIEW_LIVE_FIXTURE_H
static bool TestPreviewLive(MM::Preview &preview)
{
    const auto path = std::getenv("MM_PREVIEW_LIVE_SNAPSHOT");
    FILE *trace = path ? std::fopen(path, "wb") : std::tmpfile();
    if (!trace)
        return false;
    std::uint32_t bytes = 0, fingerprint = 2166136261U;
    auto record = [&](const void *value, std::size_t size) {
        auto data = static_cast<const unsigned char *>(value);
        for (std::size_t i = 0; i < size; ++i)
            fingerprint = (fingerprint ^ data[i]) * 16777619U;
        bytes += static_cast<std::uint32_t>(size);
        return std::fwrite(value, 1, size, trace) == size;
    };
    MM::Error error;
    auto result = [&](bool value) {
        int fields[] = {value, static_cast<int>(error.code)};
        return record(fields, sizeof(fields));
    };
    MM::PreviewView view;
    view.width = view.height = 96;
    view.authoredCamera = true;
    auto state = [&]() {
        const auto info = preview.Info();
        const std::int32_t fields[] = {info.scene,
                                       info.timeline,
                                       info.firstFrame,
                                       info.lastFrame,
                                       info.currentFrame,
                                       static_cast<std::int32_t>(info.currentCamera),
                                       static_cast<std::int32_t>(info.actors),
                                       static_cast<std::int32_t>(info.parts),
                                       static_cast<std::int32_t>(info.triangles)};
        if (!record(fields, sizeof(fields)))
            return false;
        for (const auto &o : preview.Objects())
        {
            const std::int32_t fields[] = {static_cast<std::int32_t>(o.asset.number),
                                           o.actorId,
                                           o.sourceId,
                                           o.firstFrame,
                                           o.lastFrame,
                                           o.action,
                                           o.cel,
                                           o.prop,
                                           o.text,
                                           o.visible};
            const float position[] = {o.x, o.y, o.z};
            if (!record(fields, sizeof(fields)) || !record(position, sizeof(position)))
                return false;
        }
        MM::PreviewImage image;
        bool rendered = preview.Render(view, image, error);
        return rendered && result(rendered) && record(image.rgba.data(), image.rgba.size()) &&
               record(image.projection.transform, sizeof(image.projection.transform));
    };
    bool ok = preview.Select({0x5343454e, 0}, error);
    MM::PreviewObject prop;
    for (auto object : preview.Objects())
        if (object.prop && !object.text)
        {
            prop = object;
            break;
        }
    ok = ok && prop.templateAsset.number && preview.SetSceneRange(-5, 15, error) && state();
    const auto camera = preview.Info().cameraCount - 1;
    std::vector<MM::PreviewCameraEdit> edits = {{8, 0}, {1, camera}, {8, camera}, {INT32_MIN, 0}, {-2, camera}};
    ok = ok && preview.SetCameraEdits(edits, error) && state();
    for (int frame : {-5, 0, 1, 5, 15, 5, -5})
        ok = ok && preview.SetFrame(frame, error) && state();
    bool changed = preview.SetCameraEdits({{0, preview.Info().cameraCount}}, error);
    ok = ok && !changed && result(changed) && state();
    changed = preview.SetSceneRange(2, 15, error);
    ok = ok && !changed && result(changed) && state();
    changed = preview.SetSceneRange(-5, 9, error);
    ok = ok && !changed && result(changed) && state();
    changed = preview.SetSceneRange(INT32_MIN, 15, error);
    ok = ok && !changed && result(changed) && state();
    ok = ok && preview.SetCameraEdits({}, error) && state() && preview.SetFrame(1, error);
    MM::PreviewPropDefinition a;
    a.scene = 0;
    a.number = 70000;
    a.templateNumber = prop.templateAsset.number;
    a.source = prop.sourceId;
    a.actorId = 100;
    a.first = 1;
    a.last = 10;
    a.point[0] = prop.x;
    a.point[1] = prop.y;
    a.point[2] = prop.z;
    ok = ok && preview.SetDefinitions({}, {a}, error) && state();
    // A matching number retains the existing actor; definition field changes
    // do not silently recreate or move that retained prefix.
    a.point[0] += 4;
    a.templateNumber = 0xfffffff0U;
    ok = ok && preview.SetDefinitions({}, {a}, error) && state();
    a.number = 70001;
    a.templateNumber = prop.templateAsset.number;
    auto b = a;
    b.number = 70002;
    b.actorId = 101;
    b.point[0] += 2;
    auto other = b;
    other.scene = 1;
    other.number = 70003;
    ok = ok && preview.SetDefinitions({}, {a, b, other}, error) && state();
    MM::PreviewSceneDefinition virtualScene;
    virtualScene.number = 60000;
    virtualScene.background = 0;
    virtualScene.source = 1;
    auto bad = b;
    bad.number = 70005;
    bad.templateNumber = 0xfffffff0U;
    auto pending = a;
    pending.number = 70004;
    changed = preview.SetDefinitions({virtualScene}, {pending, bad}, error);
    ok = ok && !changed && result(changed) && state();
    // Failed reconciliation restores both definition lists and the live actors.
    changed = preview.Select({0x5343454e, 60000}, error);
    ok = ok && !changed && result(changed) && preview.Select({0x5343454e, 0}, error) && state();
    ok = ok && preview.SetDefinitions({}, {b}, error) && state();
    b.scene = virtualScene.number;
    b.first = b.last = 1;
    ok = ok && preview.SetDefinitions({virtualScene}, {b}, error) && preview.Select({0x5343454e, 60000}, error) &&
         state();
    MM::PreviewPresentation presentation;
    ok = ok && preview.ReadPresentation(presentation, error) && presentation.captions.empty() &&
         presentation.sounds.empty();
    ok = ok && preview.SetDefinitions({}, {}, error);
    const auto cleared = preview.Info();
    const int clearedFields[] = {cleared.scene, cleared.timeline, int(cleared.actors)};
    ok = ok && record(clearedFields, sizeof(clearedFields));
    changed = preview.SetFrame(1, error);
    ok = ok && !changed && result(changed) && preview.Select({0x5343454e, 0}, error) && state();
    ok = std::fclose(trace) == 0 && ok;
#ifdef _WIN64
    ok = ok && bytes == 819080 && fingerprint == 0xC3B1C4A9U;
#endif
    std::printf("Preview live trace: bytes=%u fingerprint=%08X\n", bytes, fingerprint);
    if (!ok)
        std::fprintf(stderr, "Live Preview probe: %s\n", error.message.c_str());
    return ok;
}
#endif
