#include "mm/preview.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <utility>
#include "preview_admission_fixture.h"
struct MetadataTrace : AdmissionTrace
{
    MetadataTrace() : AdmissionTrace("MM_PREVIEW_METADATA_SNAPSHOT")
    {
    }
    bool Text(const std::string &s)
    {
        const unsigned n = unsigned(s.size());
        return Record(&n, sizeof(n)) && Record(s.data(), n);
    }
    bool Cues(const std::vector<MM::PreviewSoundCue> &cues)
    {
        const unsigned n = unsigned(cues.size());
        if (!Record(&n, sizeof(n)))
            return false;
        for (const auto &c : cues)
        {
            const std::uint32_t values[] = {std::uint32_t(c.frame),
                                            std::uint32_t(c.source),
                                            std::uint32_t(c.actor),
                                            std::uint32_t(c.style),
                                            std::uint32_t(c.volume),
                                            c.sound.type,
                                            c.sound.number,
                                            c.scene.type,
                                            c.scene.number,
                                            c.child,
                                            c.actorSound,
                                            c.loop,
                                            c.queue,
                                            c.silent,
                                            c.motion,
                                            std::uint32_t(c.motionCel)};
            if (!Record(values, sizeof(values)))
                return false;
        }
        return true;
    }
    bool Presentation(const MM::PreviewPresentation &p)
    {
        const unsigned counts[] = {unsigned(p.transition), unsigned(p.captions.size()), unsigned(p.pauses.size())};
        if (!Record(counts, sizeof(counts)) || !Cues(p.sounds))
            return false;
        for (const auto &c : p.captions)
        {
            const unsigned fields[] = {unsigned(c.firstFrame),
                                       unsigned(c.endFrame),
                                       unsigned(c.left),
                                       unsigned(c.top),
                                       unsigned(c.right),
                                       unsigned(c.bottom),
                                       unsigned(c.fontSize),
                                       c.story,
                                       c.background,
                                       unsigned(c.text.size()),
                                       unsigned(c.properties.size())};
            if (!Record(fields, sizeof(fields)) || !Text(c.font) || !Record(c.text.data(), c.text.size() * 2))
                return false;
            for (const auto &property : c.properties)
            {
                const unsigned values[] = {property.position, property.kind, property.value};
                if (!Record(values, sizeof(values)) || !Text(property.font))
                    return false;
            }
        }
        for (const auto &pause : p.pauses)
        {
            const int values[] = {pause.frame, pause.kind, pause.milliseconds};
            if (!Record(values, sizeof(values)))
                return false;
        }
        return true;
    }
    bool Timeline(const MM::PreviewAnimationTimeline &t)
    {
        const unsigned counts[] = {unsigned(t.spans.size()), unsigned(t.events.size())};
        if (!Record(counts, sizeof(counts)))
            return false;
        for (const auto &s : t.spans)
        {
            const std::int64_t frames[] = {s.firstFrame, s.endFrame};
            const int values[] = {s.action, s.frozen};
            if (!Record(frames, sizeof(frames)) || !Record(values, sizeof(values)) || !Text(s.name))
                return false;
        }
        for (const auto &e : t.events)
        {
            const int values[] = {e.frame, int(e.kind)};
            if (!Record(values, sizeof(values)) || !Text(e.name) || !Text(e.detail))
                return false;
        }
        return true;
    }
    bool Objects(const std::vector<MM::PreviewObject> &objects)
    {
        const unsigned n = unsigned(objects.size());
        if (!Record(&n, sizeof(n)))
            return false;
        for (const auto &o : objects)
        {
            const unsigned values[] = {o.asset.type,
                                       o.asset.number,
                                       o.templateAsset.type,
                                       o.templateAsset.number,
                                       unsigned(o.actorId),
                                       unsigned(o.sourceId),
                                       unsigned(o.firstFrame),
                                       unsigned(o.lastFrame),
                                       o.hasLifetime,
                                       o.prop,
                                       o.text,
                                       o.visible,
                                       unsigned(o.action),
                                       unsigned(o.cel)};
            const float p[] = {o.x, o.y, o.z};
            if (!Record(values, sizeof(values)) || !Record(p, sizeof(p)))
                return false;
        }
        return true;
    }
};
int main(int argc, char **argv)
{
    if (argc < 6)
        return 2;
    MetadataTrace trace;
    if (!trace.file)
        return 1;
    MM::Preview preview;
    MM::Error error;
    const auto check = [&](bool ok) {
        if (!ok)
            std::fprintf(stderr, "Metadata probe: %s\n", error.message.c_str());
        return ok && trace.Error(ok, error, preview);
    };
    for (int file = 5; file < argc; ++file)
    {
        MM::Archive archive;
        if (!archive.OpenForPreview(argv[file], error))
            return 1;
        const auto chunks = archive.Chunks();
        if (!check(preview.Open(std::move(archive), argv[1], error)))
            return 1;
        for (int source = 1; source <= 2; ++source)
            for (int content = 2; content <= 4; ++content)
                if (!check(preview.AddContent(source, argv[content], error)))
                    return 1;
        for (const auto &chunk : chunks)
        {
            if (chunk.id.type != 0x5343454e)
                continue;
            if (!check(preview.Select(chunk.id, error)))
                return 1;
            MM::PreviewPresentation presentation, timing;
            std::vector<MM::PreviewSoundCue> cues;
            if (!check(preview.ReadPresentation(chunk.id, presentation, error)) || !trace.Presentation(presentation) ||
                !check(preview.ReadTimingPresentation(chunk.id, timing, error)) || !trace.Presentation(timing) ||
                !check(preview.ReadSoundCues(chunk.id, cues, error)) || !trace.Cues(cues))
                return 1;
            for (const auto &cue : cues)
            {
                MM::PreviewAudio audio;
                if (!check(preview.ReadSound(cue, audio, error)))
                    return 1;
                const unsigned fields[] = {
                    audio.asset.type, audio.asset.number,          unsigned(audio.source),       audio.midi,
                    audio.silent,     unsigned(audio.wave.size()), unsigned(audio.events.size())};
                if (!trace.Record(fields, sizeof(fields)) || !trace.Record(audio.wave.data(), audio.wave.size()))
                    return 1;
                for (const auto &event : audio.events)
                {
                    const unsigned values[] = {event.milliseconds, event.message};
                    if (!trace.Record(values, sizeof(values)))
                        return 1;
                }
            }
            const auto info = preview.Info();
            const int frames[] = {info.firstFrame, std::min(info.firstFrame + 2, info.lastFrame), info.lastFrame,
                                  info.firstFrame};
            for (int frame : frames)
            {
                if (!check(preview.SetFrame(frame, error)) || !trace.Cues(preview.FrameSounds()))
                    return 1;
                MM::PreviewView view;
                view.width = view.height = 64;
                MM::PreviewImage before, after;
                const auto objects = preview.Objects();
                if (!trace.Objects(objects) || !check(preview.Render(view, before, error)) || !trace.Image(before))
                    return 1;
                for (std::size_t actor = 0; actor < objects.size(); ++actor)
                {
                    MM::PreviewAnimationTimeline timeline;
                    if (!check(preview.ReadAnimationTimeline(int(actor), timeline, error)) || !trace.Timeline(timeline))
                        return 1;
                }
                if (!trace.Objects(preview.Objects()) || !check(preview.Render(view, after, error)) ||
                    before.rgba != after.rgba)
                    return 1;
            }
        }
    }
    if (!trace.Finish("metadata", 1045462, 0x0BD333B2U))
        return 1;
    std::puts(
        "Preview metadata: captions, sounds/audio, pauses, full timeline labels, seeks and live isolation passed.");
    return 0;
}
