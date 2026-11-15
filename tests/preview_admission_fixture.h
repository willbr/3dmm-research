/* Headless asset metadata, pose and selection-error compatibility trace. */
#ifndef PREVIEW_ADMISSION_FIXTURE_H
#define PREVIEW_ADMISSION_FIXTURE_H
struct AdmissionTrace
{
    FILE *file = nullptr;
    unsigned bytes = 0, fingerprint = 2166136261U;
    AdmissionTrace(const char *variable = "MM_PREVIEW_ADMISSION_SNAPSHOT")
    {
        auto path = std::getenv(variable);
        file = path ? std::fopen(path, "wb") : std::tmpfile();
    }
    bool Record(const void *value, std::size_t size)
    {
        auto p = static_cast<const unsigned char *>(value);
        for (std::size_t i = 0; i < size; ++i)
            fingerprint = (fingerprint ^ p[i]) * 16777619U;
        bytes += static_cast<unsigned>(size);
        return file && std::fwrite(value, 1, size, file) == size;
    }
    bool Info(const MM::Preview &preview)
    {
        const auto &i = preview.Info();
        const std::uint32_t fields[] = {i.asset.type,
                                        i.asset.number,
                                        i.prop,
                                        i.scene,
                                        i.timeline,
                                        std::uint32_t(i.firstFrame),
                                        std::uint32_t(i.lastFrame),
                                        std::uint32_t(i.currentFrame),
                                        i.actors,
                                        i.cameraCount,
                                        i.currentCamera,
                                        i.parts,
                                        i.triangles,
                                        unsigned(i.actions.size())};
        if (!Record(fields, sizeof(fields)))
            return false;
        for (const auto &a : i.actions)
        {
            unsigned count = static_cast<unsigned>(a.name.size());
            if (!Record(&a.celCount, sizeof(a.celCount)) || !Record(&count, sizeof(count)) ||
                !Record(a.name.data(), count))
                return false;
        }
        return true;
    }
    bool Error(bool result, const MM::Error &e, const MM::Preview &p)
    {
        const std::uint32_t fields[] = {result,       unsigned(e.code), e.hasChunk,
                                        e.chunk.type, e.chunk.number,   unsigned(e.message.size())};
        return Record(fields, sizeof(fields)) && Record(e.message.data(), e.message.size()) && Info(p);
    }
    bool Image(const MM::PreviewImage &i)
    {
        const unsigned fields[] = {i.width, i.height, i.coveredPixels};
        return Record(fields, sizeof(fields)) && Record(i.rgba.data(), i.rgba.size());
    }
    bool Finish(const char *label = "admission", unsigned expectedBytes = 17862716,
                unsigned expectedFingerprint = 0xD363793CU)
    {
        bool ok = file && std::fclose(file) == 0;
        file = nullptr;
#ifdef _WIN64
        ok = ok && (!expectedBytes || (bytes == expectedBytes && fingerprint == expectedFingerprint));
#endif
        std::printf("Preview %s trace: bytes=%u fingerprint=%08X\n", label, bytes, fingerprint);
        return ok;
    }
    ~AdmissionTrace()
    {
        if (file)
            std::fclose(file);
    }
};
#endif
