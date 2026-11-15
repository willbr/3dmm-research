/* Headless Preview picking compatibility probe. */
#ifndef PREVIEW_PICK_FIXTURE_H
#define PREVIEW_PICK_FIXTURE_H
static bool TestPreviewPick(MM::Preview &preview)
{
    const auto path = std::getenv("MM_PREVIEW_PICK_SNAPSHOT");
    FILE *trace = path ? std::fopen(path, "wb") : std::tmpfile();
    if (!trace)
        return false;
    std::uint32_t fingerprint = 2166136261U, bytes = 0, hits = 0;
    auto record = [&](const void *value, std::size_t size) {
        auto data = static_cast<const unsigned char *>(value);
        for (std::size_t i = 0; i < size; ++i)
            fingerprint = (fingerprint ^ data[i]) * 16777619U;
        bytes += static_cast<std::uint32_t>(size);
        return std::fwrite(value, 1, size, trace) == size;
    };
    MM::Error error;
    bool ok = preview.Select({0x5343454e, 0}, error);
    for (int configuration = 0; ok && configuration < 8; ++configuration)
        for (int frame : {1, 5})
        {
            preview.ResetOrbitFraming();
            MM::PreviewView view;
            view.width = configuration % 2 ? 96 : 160;
            view.height = 128;
            view.authoredCamera = configuration < 2;
            view.renderAtOutputResolution = configuration == 1;
            view.orthographic = configuration == 3 || configuration == 5;
            view.axisView = configuration == 4 ? 2 : configuration == 5 ? 3 : 0;
            view.freeCamera = configuration >= 6;
            view.cameraX = 2;
            view.cameraY = 8;
            view.cameraZ = 25;
            view.yaw = 23.5f;
            view.pitch = -11.25f;
            MM::PreviewImage image, again;
            ok = preview.SetFrame(frame, error) && preview.Render(view, image, error);
            for (int gy = -1; ok && gy <= 9; ++gy)
                for (int gx = -1; ok && gx <= 11; ++gx)
                {
                    int object = 999;
                    float distance = -1;
                    const int x = gx * int(view.width) / 11, y = gy * int(view.height) / 9;
                    bool picked = preview.Pick(image.projection, view.authoredCamera, view.width, view.height, x, y,
                                               object, distance, error, view.renderAtOutputResolution);
                    std::int32_t result[] = {configuration, frame, x, y, picked, static_cast<std::int32_t>(error.code),
                                             object};
                    hits += object >= 0;
                    ok = picked && record(result, sizeof(result)) && record(&distance, sizeof(distance));
                }
            // Picking must leave shared prepared geometry and the live scene intact.
            ok = ok && preview.Render(view, again, error) && image.rgba == again.rgba;
            for (int invalid = 0; ok && invalid < 5; ++invalid)
            {
                auto projection = image.projection;
                if (invalid == 0)
                    projection.valid = false;
                if (invalid == 1)
                    projection.farClip = projection.nearClip;
                if (invalid == 2)
                    projection.transform[0] = 40000;
                if (invalid == 3)
                    std::memset(projection.transform, 0, sizeof(projection.transform));
                if (invalid == 4)
                {
                    projection.orthographic = true;
                    projection.orthoHeight = 60001;
                }
                int object = 999;
                float distance = -1;
                bool picked = preview.Pick(projection, view.authoredCamera, view.width, view.height, view.width / 2,
                                           view.height / 2, object, distance, error);
                std::int32_t result[] = {invalid, picked, static_cast<std::int32_t>(error.code), object};
                ok = !picked && object == -1 && std::isinf(distance) && record(result, sizeof(result)) &&
                     record(&distance, sizeof(distance));
            }
        }
    ok = std::fclose(trace) == 0 && ok && hits > 0;
#if defined(_WIN64) && defined(_MSC_VER)
    // Original and converted controllers agree with failed SDK inverses guarded.
    ok = ok && bytes == 74816 && fingerprint == 0x8F6FCC06U && hits == 6;
#endif
    std::printf("Preview pick trace: bytes=%u fingerprint=%08X hits=%u\n", bytes, fingerprint, hits);
    if (!ok)
        std::fprintf(stderr, "Pick probe: %s\n", error.message.c_str());
    return ok;
}
#endif
