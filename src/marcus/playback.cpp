#include "playback.h"
#include "retail_scroll.h"
#include "caption_quad.h"
#include "sdl_audio.h"
#include "midi_synth.h"
#include "font_host.h"
#include "font_catalog.h"
#include "rich_text_layout.h"
#include "mm/text_box_runtime.h"
#include <SDL3/SDL.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <deque>
#include <fstream>
#include <map>
#include <mutex>
#include <tuple>
#include <stdexcept>

namespace
{
using namespace MM;
bool Fail(Error &error, const std::string &message)
{
    error.code = ErrorCode::decode_failed;
    error.message = message;
    return false;
}
double TransitionSeconds(int kind)
{
    return kind == 4 ? 1.0 / 6 : kind > 0 && kind < 4 ? 4 : 0;
}
struct AudioClip
{
    std::tuple<int, unsigned, unsigned> identity;
    std::shared_ptr<const KauaiAudio::Sample> sample;
    std::vector<PreviewMidiEvent> midi;
    std::uint64_t frames = 0;
};
struct AudioVoice
{
    std::shared_ptr<AudioClip> clip;
    std::unique_ptr<KauaiMidi::Synth> synth;
    std::uint64_t position = 0;
    std::size_t event = 0;
    float volume = 1;
    bool loop = false;
};
std::uint64_t SoundLane(bool actorSound, long actor, long style)
{
    return (std::uint64_t(actorSound ? 1 : 0) << 63) |
           (std::uint64_t(std::uint32_t(actorSound ? actor : 0)) << 16) | std::uint32_t(style);
}
class Audio
{
    mutable std::mutex mutex;
    struct LayerMix
    {
        bool muted, solo;
    };
    // Movie-wide owner/style identities survive scene changes, seeks and queue cleanup.
    std::map<std::uint64_t, LayerMix> mix;
    bool anySolo = false;
    std::map<std::uint64_t, std::deque<std::unique_ptr<AudioVoice>>> queues;
    std::map<std::tuple<int, unsigned, unsigned, int, unsigned>, std::shared_ptr<AudioClip>> clips;
    std::vector<std::uint8_t> bank;
    std::size_t cachedBytes = 0;
    SDL_AudioStream *stream = nullptr;
    bool paused = true, muted = false, initialized = false;
    MarcusAudioStats stats = {};
    static void SDLCALL Feed(void *context, SDL_AudioStream *stream, int additional, int)
    {
        auto self = static_cast<Audio *>(context);
        std::array<float, 1024> output, scratch;
        while (additional > 0)
        {
            const auto frames = std::min<std::size_t>(512, (std::size_t(additional) + 7) / 8);
            std::fill(output.begin(), output.end(), 0.f);
            {
                std::lock_guard<std::mutex> lock(self->mutex);
                if (!self->paused)
                    for (auto &queue : self->queues)
                    {
                        const bool audible = self->Audible(queue.first);
                        std::size_t written = 0;
                        for (auto &owner : queue.second)
                        {
                            auto &voice = *owner;
                            while (written < frames && voice.position < voice.clip->frames)
                            {
                                auto count = static_cast<std::size_t>(
                                    std::min<std::uint64_t>(frames - written, voice.clip->frames - voice.position));
                                if (voice.synth)
                                {
                                    while (voice.event < voice.clip->midi.size() &&
                                           std::uint64_t(voice.clip->midi[voice.event].milliseconds) * 48 <=
                                               voice.position)
                                        voice.synth->Message(voice.clip->midi[voice.event++].message);
                                    if (voice.event < voice.clip->midi.size())
                                        count = std::min<std::size_t>(
                                            count, std::uint64_t(voice.clip->midi[voice.event].milliseconds) * 48 -
                                                       voice.position);
                                    voice.synth->Render(scratch.data(), count);
                                }
                                else
                                    std::copy_n(voice.clip->sample->stereo.data() + voice.position * 2, count * 2,
                                                scratch.data());
                                if (audible)
                                    for (std::size_t i = 0; i < count * 2; ++i)
                                        output[written * 2 + i] += scratch[i] * voice.volume;
                                written += count;
                                voice.position += count;
                                if (voice.position == voice.clip->frames && voice.loop)
                                {
                                    voice.position = 0;
                                    voice.event = 0;
                                    if (voice.synth)
                                        voice.synth->Reset();
                                }
                            }
                            if (written == frames)
                                break;
                        }
                    }
                if (self->muted)
                    std::fill(output.begin(), output.end(), 0.f);
                self->stats.rendered_frames += static_cast<unsigned long>(frames);
                for (std::size_t frame = 0; frame < frames; ++frame)
                    if (std::abs(output[frame * 2]) > .00001f || std::abs(output[frame * 2 + 1]) > .00001f)
                        ++self->stats.audible_frames;
            }
            for (std::size_t i = 0; i < frames * 2; ++i)
                output[i] = std::max(-1.f, std::min(1.f, output[i]));
            auto bytes = static_cast<int>(frames * 8);
            if (!SDL_PutAudioStreamData(stream, output.data(), bytes))
                break;
            additional -= bytes;
        }
    }
    bool Open(Error &error)
    {
        if (stream)
            return true;
        if (!initialized)
        {
            if (!SDL_InitSubSystem(SDL_INIT_AUDIO))
                return Fail(error, std::string("Cannot initialize movie audio: ") + SDL_GetError());
            initialized = true;
        }
        const SDL_AudioSpec spec = {SDL_AUDIO_F32, 2, 48000};
        stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, Feed, this);
        if (!stream || !SDL_ResumeAudioStreamDevice(stream))
            return Fail(error, std::string("Cannot open movie audio output: ") + SDL_GetError());
        return true;
    }

  public:
    // Caller holds mutex. Silenced lanes still render MIDI and advance all voices.
    bool Audible(std::uint64_t lane) const
    {
        const auto found = mix.find(lane);
        return found == mix.end() ? !anySolo : !found->second.muted && (!anySolo || found->second.solo);
    }
    void SetTrackMix(std::uint64_t lane, bool muted, bool solo)
    {
        std::lock_guard<std::mutex> lock(mutex);
        if (muted || solo)
            mix[lane] = {muted, solo};
        else
            mix.erase(lane);
        anySolo = std::any_of(mix.begin(), mix.end(), [](const auto &entry) { return entry.second.solo; });
    }
    void TrackMix(std::uint64_t lane, int &muted, int &solo, int &audible) const
    {
        std::lock_guard<std::mutex> lock(mutex);
        const auto found = mix.find(lane);
        muted = found != mix.end() && found->second.muted;
        solo = found != mix.end() && found->second.solo;
        audible = Audible(lane);
    }
    void ResetMix()
    {
        std::lock_guard<std::mutex> lock(mutex);
        mix.clear();
        anySolo = false;
    }
    bool MixActive() const
    {
        std::lock_guard<std::mutex> lock(mutex);
        return !mix.empty();
    }
    void Mute(bool value)
    {
        std::lock_guard<std::mutex> lock(mutex);
        muted = value;
    }
    void Statistics(MarcusAudioStats &output)
    {
        std::lock_guard<std::mutex> lock(mutex);
        output = stats;
    }
    ~Audio()
    {
        SDL_DestroyAudioStream(stream);
        if (initialized)
            SDL_QuitSubSystem(SDL_INIT_AUDIO);
    }
    void Suspend(bool value)
    {
        std::lock_guard<std::mutex> lock(mutex);
        paused = value;
    }
    void Collect()
    {
        std::lock_guard<std::mutex> lock(mutex);
        for (auto lane = queues.begin(); lane != queues.end();)
        {
            auto &queue = lane->second;
            while (!queue.empty() && queue.front()->position >= queue.front()->clip->frames)
                queue.pop_front();
            if (queue.empty())
                lane = queues.erase(lane);
            else
                ++lane;
        }
    }
    void Clear()
    {
        {
            std::lock_guard<std::mutex> lock(mutex);
            queues.clear();
        }
        if (stream)
            SDL_ClearAudioStream(stream);
        clips.clear();
        cachedBytes = 0;
    }
    void SceneBoundary()
    {
        // Studio stops looping sampled effects when changing scenes, while
        // unfinished non-looping sounds and MIDI continue through the cut.
        std::lock_guard<std::mutex> lock(mutex);
        for (auto &lane : queues)
        {
            auto &queue = lane.second;
            queue.erase(
                std::remove_if(queue.begin(), queue.end(),
                               [](const std::unique_ptr<AudioVoice> &voice) { return voice->loop && !voice->synth; }),
                queue.end());
        }
    }
    bool Pending()
    {
        std::lock_guard<std::mutex> lock(mutex);
        for (const auto &queue : queues)
            for (const auto &voice : queue.second)
                if (!voice->loop && voice->position < voice->clip->frames)
                    return true;
        return false;
    }
    bool Play(Preview &preview, const PreviewSoundCue &cue, std::uint64_t offsetFrames, Error &error)
    {
        const auto key =
            std::make_tuple(cue.source, cue.sound.type, cue.sound.number,
                            cue.source == 0 ? static_cast<int>(cue.scene.type ? cue.scene.number : preview.Info().asset.number)
                                            : -1, cue.child);
        std::shared_ptr<AudioClip> clip;
        auto found = clips.find(key);
        if (!cue.silent && found != clips.end())
            clip = found->second;
        if (!clip && !cue.silent)
        {
            PreviewAudio data;
            if (!preview.ReadSound(cue, data, error))
                return false;
            clip = std::make_shared<AudioClip>();
            clip->identity = std::make_tuple(data.source, data.asset.type, data.asset.number);
            if (!data.silent)
            {
                if (data.midi)
                {
                    clip->midi = std::move(data.events);
                    if (!clip->midi.empty())
                        clip->frames = std::uint64_t(clip->midi.back().milliseconds) * 48 + 1;
                }
                else
                {
                    if (!KauaiAudio::DecodeWave(data.wave.data(), data.wave.size(), clip->sample))
                        return Fail(error, std::string("Cannot decode movie WAVE audio: ") + SDL_GetError());
                    clip->frames = clip->sample->stereo.size() / 2;
                }
            }
            auto bytes = clip->sample ? clip->sample->stereo.size() * sizeof(float)
                                      : clip->midi.size() * sizeof(PreviewMidiEvent);
            if (bytes > 256u * 1024 * 1024)
                return Fail(error, "A decoded movie sound exceeds the 256 MiB audio budget.");
            if (cachedBytes + bytes > 256u * 1024 * 1024)
            {
                clips.clear();
                cachedBytes = 0;
            }
            cachedBytes += bytes;
            clips[key] = clip;
        }
        const auto lane = SoundLane(cue.actorSound, cue.actor, cue.style);
        std::unique_ptr<AudioVoice> voice;
        if (clip && clip->frames)
        {
            voice.reset(new AudioVoice);
            voice->clip = clip;
            voice->volume = std::max(0.f, std::min(1.f, cue.volume / 65536.f));
            voice->loop = cue.loop;
            voice->position = offsetFrames;
            if (voice->loop)
                voice->position %= clip->frames;
            else
                voice->position = std::min(voice->position, clip->frames);
            if (!clip->midi.empty())
            {
                if (bank.empty())
                {
                    const auto base = SDL_GetBasePath();
                    std::ifstream input(std::string(base ? base : "") + "assets/GeneralUserGS.sf2", std::ios::binary);
                    if (!input)
                        input.open(MARCUS_MIDI_BANK, std::ios::binary);
                    bank.assign(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
                }
                voice->synth.reset(new KauaiMidi::Synth);
                if (!voice->synth->Load(bank.data(), bank.size(), 48000))
                    return Fail(error, "Cannot load the General MIDI instrument bank (assets/GeneralUserGS.sf2).");
                // Reconstruct controllers, programs and sounding notes on a mid-song seek.
                std::array<float, 1024> scratch;
                std::uint64_t position = 0;
                while (position < voice->position)
                {
                    while (voice->event < clip->midi.size() &&
                           std::uint64_t(clip->midi[voice->event].milliseconds) * 48 <= position)
                        voice->synth->Message(clip->midi[voice->event++].message);
                    auto count = std::min<std::uint64_t>(512, voice->position - position);
                    if (voice->event < clip->midi.size())
                        count = std::min(count, std::uint64_t(clip->midi[voice->event].milliseconds) * 48 - position);
                    voice->synth->Render(scratch.data(), static_cast<std::size_t>(count));
                    position += count;
                }
            }
            if (!Open(error))
                return false;
        }
        std::lock_guard<std::mutex> lock(mutex);
        auto &queue = queues[lane];
        while (!queue.empty() && queue.front()->position >= queue.front()->clip->frames)
            queue.pop_front();
        if (voice && voice->synth)
            for (const auto &existing : queue)
                if (existing->clip->identity == clip->identity && existing->position < existing->clip->frames)
                    return true;
        if (!cue.queue || !clip || !clip->frames)
            queue.clear();
        if (voice)
        {
            if (voice->synth)
                ++stats.midi_sounds;
            else
                ++stats.wave_sounds;
            queue.push_back(std::move(voice));
        }
        return true;
    }
};
struct Style
{
    std::string font;
    int pixels = 12, flags = 0, offset = 0;
    std::uint32_t fore = 0x01000000, back = 0xffffffff;
};
struct Glyph
{
    KauaiFonts::Mask mask;
    Style style;
    int x = 0, y = 0, height = 0, ascent = 0;
    std::u16string text;
};
struct CaptionRaster
{
    KauaiFonts::Mask mask;
    int ascent = 0;
    float density = 1;
};
// Average supersampled coverage rather than magnifying monochrome font pixels.
KauaiFonts::Mask ReduceCaptionMask(const KauaiFonts::Mask &source)
{
    KauaiFonts::Mask result;
    result.left = static_cast<int>(std::floor(source.left / 2.0));
    result.top = static_cast<int>(std::floor(source.top / 2.0));
    result.width = static_cast<int>(std::ceil((source.left + source.width) / 2.0)) - result.left;
    result.height = static_cast<int>(std::ceil((source.top + source.height) / 2.0)) - result.top;
    result.pitch = result.width;
    const auto reduce = [&](const std::vector<std::uint8_t> &input, std::vector<std::uint8_t> &output) {
        if (input.empty())
            return;
        output.resize(std::size_t(result.width) * result.height);
        for (int y = 0; y < result.height; ++y)
            for (int x = 0; x < result.width; ++x)
            {
                unsigned sum = 0;
                for (int dy = 0; dy < 2; ++dy)
                    for (int dx = 0; dx < 2; ++dx)
                    {
                        const int sx = (result.left + x) * 2 + dx - source.left;
                        const int sy = (result.top + y) * 2 + dy - source.top;
                        if (sx >= 0 && sy >= 0 && sx < source.width && sy < source.height)
                            sum += input[std::size_t(sy) * source.pitch + sx];
                    }
                output[std::size_t(y) * result.pitch + x] = static_cast<std::uint8_t>((sum + 2) / 4);
            }
    };
    reduce(source.coverage, result.coverage);
    reduce(source.background, result.background);
    return result;
}
struct CaptionImage
{
    PreviewCaption data;
    std::vector<Glyph> glyphs;
    int height = 0;
    int scroll = 0;
    bool ready = false;
    bool visible = false;
    std::uint64_t paintOrder = 0;
    std::size_t maskBytes = 0;
};
using FaceKey = std::tuple<std::string, int, int>;
KauaiFonts::Text FontText(const std::string &text, std::vector<std::uint16_t> &units)
{
    // Font family names are UTF-8 in the public preview API.
    for (std::size_t i = 0; i < text.size();)
    {
        std::uint32_t c = static_cast<unsigned char>(text[i++]);
        int follow = c < 128 ? 0 : c < 224 ? 1 : c < 240 ? 2 : 3;
        c &= follow ? (1u << (6 - follow)) - 1 : 127;
        while (follow-- && i < text.size())
            c = (c << 6) | (static_cast<unsigned char>(text[i++]) & 63);
        if (c > 0xffff)
        {
            units.push_back(static_cast<std::uint16_t>(0xd800 + ((c - 0x10000) >> 10)));
            c = 0xdc00 + ((c - 0x10000) & 1023);
        }
        units.push_back(static_cast<std::uint16_t>(c));
    }
    return {units.data(), units.size(), KauaiFonts::Encoding::utf16};
}
} // namespace
struct MarcusPlayback::Impl
{
    PreviewPresentation scene;
    std::vector<CaptionImage> captions;
    std::uint64_t nextPaintOrder = 0;
    void ObserveCaptions(std::int32_t frame)
    {
        // SDL/retail TextBox::FGotoFrame creates a new frontmost GOB when a
        // box becomes visible. Surviving boxes keep their order across seeks.
        for (auto &caption : captions)
        {
            const bool visible = mm_text_box_visible_range(caption.data.firstFrame, caption.data.endFrame, frame) != 0;
            if (visible && !caption.visible) caption.paintOrder = ++nextPaintOrder;
            caption.visible = visible;
        }
    }
    std::map<FaceKey, std::unique_ptr<KauaiFonts::Face>> faces;
    std::shared_ptr<KauaiFonts::FontCatalog> fonts;
    std::size_t captionBytes = 0;
    std::map<std::tuple<std::string, int, int, std::u16string>, CaptionRaster> captionRasters;
    std::size_t rasterBytes = 0;
    Audio audio;
    RetailScrollPlayback scrollPlayback;
    bool entered = false, wasPlaying = false, scrolling = false, restoreMusic = true;
    bool auditioning = false;
    int wait = 0, transition = 0;
    double frameTime = 0, waitTime = 0, transitionTime = 0, frameSeconds = 0;
    MMFramePacer pacer = {};
    double fps = -1, pacingTime = 0, frameCarry = 0;
    PreviewImage previous;
    void ResetTiming()
    {
        entered = false;
        restoreMusic = true;
        scrolling = false;
        scrollPlayback.Reset();
        wait = 0;
        frameTime = waitTime = transitionTime = 0;
        pacingTime = frameCarry = 0;
        mm_frame_pacer_reset(&pacer, fps);
        frameSeconds = 0;
        transition = 0;
        previous = {};
        for (auto &caption : captions)
            caption.scroll = 0;
    }
    KauaiFonts::Face *Face(const Style &style)
    {
        auto key = std::make_tuple(style.font, style.pixels, style.flags);
        auto found = faces.find(key);
        if (found != faces.end())
            return found->second.get();
        if (faces.size() >= 128)
            faces.clear();
        std::vector<std::uint16_t> family;
        auto text = FontText(style.font.empty() ? "Arial" : style.font, family);
        std::unique_ptr<KauaiFonts::Face> face;
        if (fonts)
        {
            KauaiFonts::FreeTypeOptions options;
            options.pixels = style.pixels;
            options.bold = (style.flags & 1) != 0;
            options.italic = (style.flags & 2) != 0;
            options.underline = (style.flags & 4) != 0;
            options.legacyCellMetrics = true;
            const auto familyName = style.font.empty() ? "Arial" : style.font;
            if (fonts->Find(familyName, options))
            {
                face = fonts->Open(familyName, options);
                if (!face) return nullptr; // A bundled face must not silently become an installed substitute.
            }
        }
        if (!face)
            face = KauaiFonts::CreateStudioFace(text, style.pixels, (style.flags & 1) != 0, (style.flags & 2) != 0,
                                               (style.flags & 4) != 0);
        if (!face)
            face = KauaiFonts::CreateStudioFace({"System", 6}, style.pixels, (style.flags & 1) != 0,
                                                (style.flags & 2) != 0, (style.flags & 4) != 0);
        auto result = face.get();
        faces.emplace(key, std::move(face));
        return result;
    }
    const CaptionRaster *Raster(const Glyph &glyph, int scale, Error &error)
    {
        Style style = glyph.style;
        style.pixels *= scale * 2;
        const auto key = std::make_tuple(style.font, style.pixels, style.flags, glyph.text);
        auto found = captionRasters.find(key);
        if (found != captionRasters.end())
            return &found->second;
        auto face = Face(style);
        KauaiFonts::Metrics metrics;
        KauaiFonts::Mask mask;
        const KauaiFonts::Text text{glyph.text.data(), glyph.text.size(), KauaiFonts::Encoding::utf16};
        if (!face || !face->Measure(text, metrics))
        {
            Fail(error, "Cannot render a high-resolution caption glyph.");
            return nullptr;
        }
        CaptionRaster raster;
        raster.density = float(scale);
        const auto estimate = (std::int64_t(metrics.advance) + style.pixels * 2) *
                              (metrics.cellHeight + style.pixels * 2) * 2;
        if (estimate > 64u * 1024 * 1024)
        {
            // A whole run can be much wider than a single glyph. Keep a large
            // inspection view within the raster cache's allocation budget.
            raster.mask = glyph.mask;
            raster.ascent = glyph.ascent * 2;
            raster.density = 1;
        }
        else if (!face->Rasterize(text, mask, true))
        {
            Fail(error, "Cannot render a high-resolution caption run.");
            return nullptr;
        }
        else if (metrics.cellHeight < glyph.height * scale)
        {
            // A legacy bitmap fallback has no larger strike. Preserve
            // its native pixels rather than shrinking it after supersampling.
            raster.mask = glyph.mask;
            auto &scaled = raster.mask;
            scaled.left *= scale;
            scaled.top *= scale;
            scaled.width *= scale;
            scaled.height *= scale;
            scaled.pitch = scaled.width;
            scaled.coverage.resize(std::size_t(scaled.width) * scaled.height);
            if (!glyph.mask.background.empty())
                scaled.background.resize(scaled.coverage.size());
            for (int y = 0; y < scaled.height; ++y)
                for (int x = 0; x < scaled.width; ++x)
                {
                    const auto from = (y / scale) * glyph.mask.pitch + x / scale;
                    const auto to = std::size_t(y) * scaled.pitch + x;
                    scaled.coverage[to] = glyph.mask.coverage[from];
                    if (!scaled.background.empty())
                        scaled.background[to] = glyph.mask.background[from];
                }
            raster.ascent = glyph.ascent * scale * 2;
        }
        else
        {
            raster.mask = ReduceCaptionMask(mask);
            raster.ascent = metrics.ascent;
        }
        const auto bytes = raster.mask.coverage.size() + raster.mask.background.size();
        // Share repeated runs and bound the cache across camera sizes.
        if (rasterBytes + bytes > 64u * 1024 * 1024)
        {
            captionRasters.clear();
            rasterBytes = 0;
        }
        rasterBytes += bytes;
        return &captionRasters.emplace(key, std::move(raster)).first->second;
    }
    struct CaptionHost
    {
        using Character = std::uint16_t;
        using CharacterProperties = Style;
        struct ParagraphProperties
        {
            int jc = 0, nd = 0, dxpTab = 18;
            int numLine = 256, dypExtraLine = 0, numAfter = 256, dypExtraAfter = 0;
        };
        struct Rectangle
        {
            long xpLeft = 0, xpRight = 0, ypTop = 0, ypBottom = 0;
            long Dxp() const
            {
                return xpRight - xpLeft;
            }
            void Offset(long x, long y)
            {
                xpLeft += x;
                xpRight += x;
                ypTop += y;
                ypBottom += y;
            }
        };
        Impl &owner;
        const PreviewCaption &data;
        std::vector<Character> text;
        Style style;
        KauaiFonts::Face *face = nullptr;
        CaptionHost(Impl &owner, const PreviewCaption &data) : owner(owner), data(data), text(data.text)
        {
            // RichTextDocument keeps a final EOP beyond the stored text bytes.
            text.push_back('\r');
        }
        long CpMac() const
        {
            return static_cast<long>(text.size());
        }
        unsigned long Flags(Character ch) const
        {
            return KauaiTextLayout::Flags(ch);
        }
        long Offset(const Style &value) const
        {
            return value.offset;
        }
        bool FMinPara(long cp) const
        {
            if (cp <= 0)
                return cp == 0;
            if (cp >= CpMac() || (Flags(text[cp]) & KauaiTextLayout::fchIgnore))
                return false;
            while (cp-- > 0)
            {
                const auto flags = Flags(text[cp]);
                if (flags & KauaiTextLayout::fchBreak)
                    return true;
                if (!(flags & KauaiTextLayout::fchIgnore))
                    return false;
            }
            return true;
        }
        void FetchRgch(long cp, long count, Character *out) const
        {
            std::copy_n(text.data() + cp, count, out);
        }
        void FetchChp(long cp, Style *out, long *limit) const
        {
            *out = {};
            out->font = data.font;
            out->pixels = data.fontSize;
            *limit = CpMac();
            for (const auto &property : data.properties)
            {
                if (property.kind < 1 || property.kind > 4)
                    continue;
                if (property.position > cp)
                {
                    *limit = std::min(*limit, static_cast<long>(property.position));
                    continue;
                }
                switch (property.kind)
                {
                case 1:
                    out->flags = property.value & 255;
                    out->offset = static_cast<std::int8_t>((property.value >> 8) & 255);
                    out->pixels = std::clamp(data.fontSize + static_cast<std::int16_t>(property.value >> 16), 4, 255);
                    break;
                case 2:
                    out->font = property.font.empty() ? data.font : property.font;
                    break;
                case 3:
                    out->fore = 0x01000000 + property.value;
                    break;
                case 4:
                    out->back = 0xffffffff + property.value;
                    break;
                }
            }
        }
        void FetchPap(long cp, ParagraphProperties *out, long *limit) const
        {
            *out = {};
            *limit = CpMac();
            for (const auto &property : data.properties)
            {
                if (property.kind < 128 || property.kind > 130)
                    continue;
                if (property.position > cp)
                {
                    *limit = std::min(*limit, static_cast<long>(property.position));
                    continue;
                }
                switch (property.kind)
                {
                case 128:
                    out->jc = property.value >> 24;
                    out->nd = (property.value >> 16) & 255;
                    out->dxpTab = std::max(1, 18 + static_cast<std::int16_t>(property.value));
                    break;
                case 129:
                    out->numLine = 256 + static_cast<std::int16_t>(property.value >> 16);
                    out->dypExtraLine = static_cast<std::int16_t>(property.value);
                    break;
                case 130:
                    out->numAfter = 256 + static_cast<std::int16_t>(property.value >> 16);
                    out->dypExtraAfter = static_cast<std::int16_t>(property.value);
                    break;
                }
            }
        }
        void SetFont(const Style &value)
        {
            style = value;
            face = owner.Face(style);
            if (!face)
                throw std::runtime_error("Cannot open a movie caption font.");
        }
        void WarmFont() const
        {
        }
        void GetRcFromRgch(Rectangle *rect, Character *characters, long count)
        {
            KauaiFonts::Metrics metrics;
            if (!face->Measure({characters, static_cast<std::size_t>(count), KauaiFonts::Encoding::utf16}, metrics))
                throw std::runtime_error("Cannot measure a movie caption run.");
            rect->xpLeft = 0;
            rect->xpRight = metrics.advance;
            rect->ypTop = -metrics.ascent;
            rect->ypBottom = metrics.cellHeight + metrics.externalLeading - metrics.ascent;
        }
        bool FGetObjectRc(long, Style *, Rectangle *) const
        {
            return false;
        }
    };
    bool Layout(CaptionImage &caption, Error &error)
    {
        if (caption.ready)
            return true;
        captionBytes -= caption.maskBytes;
        caption.maskBytes = 0;
        caption.glyphs.clear();
        const auto &data = caption.data;
        if (data.right <= data.left || data.bottom <= data.top || data.right - std::int64_t(data.left) > 4096 ||
            data.bottom - std::int64_t(data.top) > 4096 || data.text.size() > 65536)
            return Fail(error, "Caption bounds or text exceed the playback layout limits.");
        CaptionHost host(*this, data);
        const long width = std::max(1, data.right - data.left - 28);
        long cp = 0, y = 0;
        while (cp < host.CpMac())
        {
            KauaiTextLayout::Line line;
            KauaiTextLayout::CalculateLine(host, width, cp, y, &line);
            if (line.ccp <= 0)
                return Fail(error, "Caption layout did not advance.");
            CaptionHost::ParagraphProperties paragraph;
            long paragraphLimit;
            host.FetchPap(cp, &paragraph, &paragraphLimit);
            long current = cp, styleLimit = cp, x = 0;
            const long lineLimit = cp + line.ccp;
            Style style;
            KauaiTextLayout::CharacterRun<CaptionHost> run;
            for (;;)
            {
                if (current >= styleLimit)
                {
                    host.FetchChp(current, &style, &styleLimit);
                    styleLimit = std::min(styleLimit, lineLimit);
                    run.Init(&style, &paragraph, &host, current, styleLimit, x, width, width);
                }
                run.GetNextRun(true);
                KauaiTextLayout::RunData position;
                run.GetChrd(&position);
                if (position.cpLimDraw > current)
                {
                    Glyph glyph;
                    glyph.style = style;
                    glyph.text.assign(run.Prgch(), run.Prgch() + position.cpLimDraw - run.CpMin());
                    const KauaiFonts::Text text{glyph.text.data(), glyph.text.size(), KauaiFonts::Encoding::utf16};
                    KauaiFonts::Metrics metrics;
                    auto face = Face(style);
                    if (!face || !face->Measure(text, metrics) || !face->Rasterize(text, glyph.mask, true))
                        return Fail(error, "Cannot rasterize a movie caption run.");
                    glyph.x = static_cast<int>(line.xpLeft + run.XpMin());
                    glyph.y = static_cast<int>(y + line.dypAscent + style.offset - metrics.ascent);
                    glyph.ascent = metrics.ascent;
                    glyph.height = metrics.cellHeight + metrics.externalLeading;
                    const auto bytes = glyph.mask.coverage.size() + glyph.mask.background.size();
                    if (captionBytes + bytes > 128u * 1024 * 1024)
                        return Fail(error, "Movie caption runs exceed the 128 MiB scene budget.");
                    captionBytes += bytes;
                    caption.maskBytes += bytes;
                    caption.glyphs.push_back(std::move(glyph));
                }
                if (position.cpLim >= lineLimit || run.FBreak())
                    break;
                current = position.cpLim;
                x = position.xpLim;
            }
            cp = lineLimit;
            y += line.dyp;
        }
        caption.height = static_cast<int>(y);
        caption.ready = true;
        return true;
    }
};
MarcusPlayback::MarcusPlayback() : impl(new Impl)
{
}
MarcusPlayback::~MarcusPlayback() = default;
void MarcusPlayback::SetFonts(std::shared_ptr<KauaiFonts::FontCatalog> fonts)
{
    impl->faces.clear();
    impl->captionRasters.clear();
    impl->rasterBytes = 0;
    impl->fonts = std::move(fonts);
}
bool MarcusPlayback::ReadTiming(const PreviewPresentation &presentation, MarcusSoundTiming &timing, Error &error,
                                const std::shared_ptr<KauaiFonts::FontCatalog> &fonts)
{
    timing = {};
    timing.transition = TransitionSeconds(presentation.transition);
    timing.pauses = presentation.pauses;
    // Use the playback layout itself, including font metrics and rich-text
    // wrapping, so restart offsets use the same scrolling distance and speed.
    Impl calculator;
    calculator.fonts = fonts;
    for (const auto &data : presentation.captions)
        if (!data.story)
        {
            CaptionImage caption;
            caption.data = data;
            if (!calculator.Layout(caption, error))
                return false;
            const int overflow = std::max(0, caption.height - (data.bottom - data.top - 10));
            timing.scrolling[data.firstFrame] =
                std::max(timing.scrolling[data.firstFrame], RetailScrollPlayback::NominalSeconds(overflow));
            calculator.captionBytes -= caption.maskBytes;
        }
    return true;
}
bool MarcusPlayback::RestoreAudio(Preview &preview, const std::vector<MarcusResumeSound> &sounds, Error &error)
{
    impl->audio.Suspend(true);
    for (const auto &sound : sounds)
        if (!impl->audio.Play(preview, sound.cue,
                             static_cast<std::uint64_t>(std::floor(sound.seconds * 48000 + .5)), error))
            return false;
    impl->restoreMusic = false;
    return true;
}
bool MarcusPlayback::Select(MM::Preview &preview, MM::Error &error, bool preserveAudio)
{
    if (preserveAudio)
    {
        impl->audio.SceneBoundary();
        impl->ResetTiming();
        impl->wasPlaying = true;
    }
    else
        Stop();
    impl->captions.clear();
    impl->nextPaintOrder = 0;
    impl->captionBytes = 0;
    impl->captionRasters.clear();
    impl->rasterBytes = 0;
    if (!preview.ReadPresentation(impl->scene, error))
        return false;
    for (auto &caption : impl->scene.captions)
    {
        CaptionImage value;
        value.data = std::move(caption);
        impl->captions.push_back(std::move(value));
    }
    impl->scene.captions.clear();
    impl->ObserveCaptions(preview.Info().currentFrame);
    return true;
}
void MarcusPlayback::Stop()
{
    impl->auditioning = false;
    impl->audio.Suspend(true);
    Seek();
    impl->wasPlaying = false;
}
bool MarcusPlayback::Audition(Preview &preview, PreviewSoundCue cue, Error &error)
{
    Stop(); cue.loop = false; cue.queue = false;
    if (!impl->audio.Play(preview, cue, 0, error)) return false;
    impl->auditioning = true; impl->audio.Suspend(false); return true;
}
void MarcusPlayback::Statistics(MarcusAudioStats &stats)
{
    impl->audio.Statistics(stats);
}
void MarcusPlayback::Mute(bool muted)
{
    impl->audio.Mute(muted);
}
void MarcusPlayback::SetTrackMix(bool actorSound, long actor, long style, bool muted, bool solo)
{
    impl->audio.SetTrackMix(SoundLane(actorSound, actor, style), muted, solo);
}
void MarcusPlayback::TrackMix(bool actorSound, long actor, long style, int &muted, int &solo, int &audible) const
{
    impl->audio.TrackMix(SoundLane(actorSound, actor, style), muted, solo, audible);
}
void MarcusPlayback::ResetMix()
{
    impl->audio.ResetMix();
}
bool MarcusPlayback::MixActive() const
{
    return impl->audio.MixActive();
}
void MarcusPlayback::SetRate(double fps)
{
    impl->fps = fps;
    impl->frameTime = impl->pacingTime = impl->frameCarry = 0;
    mm_frame_pacer_reset(&impl->pacer, fps);
}
void MarcusPlayback::Seek(MM::Preview *preview)
{
    impl->audio.Clear();
    impl->ResetTiming();
    if (preview) impl->ObserveCaptions(preview->Info().currentFrame);
}
int MarcusPlayback::Wait() const
{
    return impl->wait;
}
bool MarcusPlayback::Scrolling() const
{
    return impl->scrolling;
}
int MarcusPlayback::Transition() const
{
    return impl->scene.transition;
}
bool MarcusPlayback::Transitioning() const
{
    return impl->transition != 0;
}
double MarcusPlayback::FrameSeconds() const
{
    return impl->transition ? std::min(0.0, impl->transitionTime / 1000 - TransitionSeconds(impl->transition))
                            : impl->frameSeconds;
}
void MarcusPlayback::BeginTransition(int kind, const MM::PreviewImage &previous)
{
    if (kind < 1 || kind > 4 || previous.rgba.empty())
        return;
    impl->transition = kind;
    impl->wasPlaying = true;
    impl->transitionTime = 0;
    impl->previous = previous;
}
bool MarcusPlayback::Update(MM::Preview &preview, bool playing, double elapsed, bool click, bool &advance,
                            MM::Error &error)
{
    impl->ObserveCaptions(preview.Info().currentFrame);
    advance = false;
    if (playing && impl->auditioning) { impl->audio.Clear(); impl->auditioning = false; }
    impl->audio.Suspend(!playing && !impl->auditioning);
    impl->audio.Collect();
    if (!playing)
    {
        impl->wasPlaying = false;
        return true;
    }
    // The first update after Play starts this frame; time spent paused belongs
    // to neither the authored wait nor the frame clock.
    if (!impl->wasPlaying)
        elapsed = 0;
    impl->wasPlaying = true;
    if (impl->transition)
    {
        impl->transitionTime += elapsed;
        if (impl->transitionTime < TransitionSeconds(impl->transition) * 1000)
            return true;
        impl->transition = 0;
        impl->previous = {};
        elapsed = 0;
    }
    const auto frame = preview.Info().currentFrame;
    if (!impl->entered)
    {
        impl->scrollPlayback.Reset();
        impl->frameSeconds = 0;
        // Restore scene music on a seek; speech and effects only fire at their
        // authored frame, so scrubbing never produces a burst of old sounds.
        const PreviewSoundCue *music = nullptr;
        for (const auto &cue : impl->scene.sounds)
            if (cue.style == 4 && cue.frame < frame && (!music || cue.frame >= music->frame))
                music = &cue;
        if (music && impl->restoreMusic)
        {
            const auto musicFrame = music->frame;
            for (const auto &cue : impl->scene.sounds)
                if (cue.style == 4 && cue.frame == musicFrame &&
                    !impl->audio.Play(preview, cue,
                                      cue.queue
                                          ? 0
                                          : static_cast<std::uint64_t>(std::max(0.0, (double(frame) - musicFrame) *
                                                (impl->fps > 0 ? 48000 / impl->fps : 8000))),
                                      error))
                    return false;
        }
        for (const auto &cue : impl->scene.sounds)
            if (cue.frame == frame && !impl->audio.Play(preview, cue, 0, error))
                return false;
        const auto sounds = preview.FrameSounds();
        // Explicit actor sounds take precedence over motion-match sounds in
        // the same actor/style lane, following the native dispatch policy.
        for (const auto &cue : sounds)
        {
            if (cue.motion && std::any_of(sounds.begin(), sounds.end(), [&](const PreviewSoundCue &other) {
                    return !other.motion && other.actor == cue.actor && other.style == cue.style;
                }))
                continue;
            if (!impl->audio.Play(preview, cue, 0, error))
                return false;
        }
        impl->wait = 0;
        for (const auto &pause : impl->scene.pauses)
            if (pause.frame == frame)
            {
                impl->wait = pause.kind;
                impl->waitTime = pause.milliseconds;
            }
        impl->entered = true;
        impl->restoreMusic = false;
        elapsed = 0;
    }
    if (impl->scrolling || impl->wait != 1)
        impl->frameSeconds += elapsed / 1000;
    const bool wasScrolling = impl->scrolling;
    bool needsScroll = false;
    for (auto &caption : impl->captions)
        if (mm_text_box_scroll_frame(caption.data.story, caption.data.firstFrame, frame))
        {
            if (!impl->Layout(caption, error))
                return false;
            const auto overflow = std::max(0, caption.height - (caption.data.bottom - caption.data.top - 10));
            needsScroll = needsScroll || caption.scroll < overflow;
        }
    const auto scrollUpdate = impl->scrollPlayback.Update(elapsed, needsScroll);
    if (scrollUpdate.step)
        for (auto &caption : impl->captions)
            if (mm_text_box_scroll_frame(caption.data.story, caption.data.firstFrame, frame))
            {
                const auto overflow = std::max(0, caption.height - (caption.data.bottom - caption.data.top - 10));
                if (caption.scroll < overflow)
                    ++caption.scroll;
            }
    impl->scrolling = scrollUpdate.holding;
    if (impl->scrolling)
    {
        impl->frameCarry = 0;
        return true;
    }
    if (wasScrolling)
        elapsed = 0;
    if (impl->wait == 1)
    {
        impl->frameCarry = 0;
        if (!click)
            return true;
        impl->wait = 0;
        elapsed = 0;
    }
    else if (impl->wait == 2)
    {
        impl->frameCarry = 0;
        if (impl->audio.Pending())
            return true;
        impl->wait = 0;
        elapsed = 0;
    }
    else if (impl->wait == 3)
    {
        impl->frameCarry = 0;
        impl->waitTime -= elapsed;
        if (impl->waitTime > 0)
            return true;
        impl->wait = 0;
        elapsed = 0;
    }
    if (impl->fps >= 0)
    {
        // Count only ordinary frame time: pause/resume and authored holds keep
        // the remaining interval. Shared millisecond pacing carries fractions,
        // retains bounded display jitter and never skips authored frames.
        impl->pacingTime += elapsed;
        const auto now = static_cast<MMPlaybackU32>(static_cast<std::uint64_t>(impl->pacingTime));
        if (!impl->pacer.pending)
        {
            const auto start = static_cast<MMPlaybackU32>(
                static_cast<std::uint64_t>(std::max(0.0, impl->pacingTime - impl->frameCarry)));
            mm_frame_pacer_schedule(&impl->pacer, start);
            impl->frameCarry = 0;
            return true; // Also makes Uncapped advance once per host update.
        }
        if (mm_frame_pacer_take_due(&impl->pacer, now))
        {
            // A 60 Hz display should still reach 24 FPS rather than rounding
            // every interval up to three refreshes. Discard lateness of a full
            // frame or more so a stall cannot build up catch-up debt.
            const double late = static_cast<MMPlaybackU32>(now - impl->pacer.due);
            impl->frameCarry = impl->fps > 0 && late < 1000.0 / impl->fps ? late : 0;
            advance = true;
            impl->entered = false;
        }
        return true;
    }
    impl->frameTime += elapsed;
    if (impl->frameTime >= 1000.0 / 6)
    {
        advance = true;
        impl->entered = false;
        // Carry ordinary host jitter into the next frame without skipping
        // authored frames after an expensive decode or a stalled window.
        impl->frameTime = std::min(impl->frameTime - 1000.0 / 6, 1000.0 / 12);
    }
    return true;
}
bool MarcusPlayback::Composite(MM::Preview &preview, bool authored, MM::PreviewImage &image,
                               MM::Error &error)
{
    const auto frame = preview.Info().currentFrame;
    impl->ObserveCaptions(frame);
    if (!authored && !std::any_of(impl->captions.begin(), impl->captions.end(), [&](const CaptionImage &caption) {
            return mm_text_box_visible_range(caption.data.firstFrame, caption.data.endFrame, frame) != 0;
        }))
        return true;
    if (authored || image.sceneCamera.valid)
    {
        MM::PreviewImage layer;
        auto canvas = &image;
        if (!authored)
        {
            // Give the perspective caption plane enough texels for the work
            // viewport, including angled views and moderate close inspection.
            layer.width = std::min(4096u, std::max({std::uint32_t(PreviewCaptionWidth), image.width * 2,
                image.height * 2 * PreviewCaptionWidth / PreviewCaptionHeight}));
            layer.height = layer.width * PreviewCaptionHeight / PreviewCaptionWidth;
            layer.rgba.assign(std::size_t(layer.width) * layer.height * 4, 0);
            canvas = &layer;
        }
        const float aspect = authored && image.sceneCamera.valid ? image.sceneCamera.aspect
            : float(PreviewCaptionWidth) / PreviewCaptionHeight;
        const auto width = std::min(int(canvas->width), int(canvas->height * aspect));
        const auto height = std::min(int(canvas->height), int(canvas->width / aspect));
        const auto left = (int(canvas->width) - width) / 2, top = (int(canvas->height) - height) / 2;
        auto paint = [&](int x, int y, std::uint32_t colour, int alpha) {
            if (x < left || x >= left + width || y < top || y >= top + height || colour == 0xffffffff)
                return;
            auto pixel = &canvas->rgba[(std::size_t(y) * canvas->width + x) * 4];
            colour = preview.PresentationColour(colour);
            const auto coverage = alpha * 255 + pixel[3] * (255 - alpha);
            if (!coverage)
                return;
            for (int channel = 0; channel < 3; ++channel)
            {
                const auto value = colour == 0xff000000 ? 255 - pixel[channel] : (colour >> (16 - channel * 8)) & 255;
                pixel[channel] = static_cast<std::uint8_t>(
                    (pixel[channel] * pixel[3] * (255 - alpha) + value * alpha * 255) / coverage);
            }
            pixel[3] = static_cast<std::uint8_t>(coverage / 255);
        };
        // Saved captions use the retail 544x306 workspace, independently of
        // the background bitmap's size or the host's chosen render resolution.
        std::vector<CaptionImage *> paintOrder;
        for (auto &caption : impl->captions)
            if (caption.visible) paintOrder.push_back(&caption);
        std::sort(paintOrder.begin(), paintOrder.end(), [](const CaptionImage *a, const CaptionImage *b) {
            return a->paintOrder < b->paintOrder;
        });
        for (auto *item : paintOrder)
        {
            auto &caption = *item;
            const auto &data = caption.data;
            if (!mm_text_box_visible_range(data.firstFrame, data.endFrame, frame))
                continue;
            if (data.right <= 0 || data.left >= PreviewCaptionWidth || data.bottom <= 0 ||
                data.top >= PreviewCaptionHeight)
                continue;
            if (!impl->Layout(caption, error))
                return false;
            const float sx = float(width) / PreviewCaptionWidth, sy = float(height) / PreviewCaptionHeight;
            const int x0 = left + int((data.left + 5) * sx), y0 = top + int((data.top + 5) * sy);
            const int x1 = left + int((data.right - 5) * sx), y1 = top + int((data.bottom - 5) * sy);
            for (int y = std::max(top, y0); y < std::min(top + height, y1); ++y)
                for (int x = std::max(left, x0); x < std::min(left + width, x1); ++x)
                    paint(x, y, data.background, 255);
            for (const auto &glyph : caption.glyphs)
            {
                // Keep native metrics for layout and scrolling; only the ink
                // is rerasterized. Align new masks to the original baseline.
                const int scale = std::min(std::max(1, 1024 / glyph.style.pixels),
                                           static_cast<int>(std::ceil(std::max(sx, sy))));
                const CaptionRaster *raster = scale > 1 ? impl->Raster(glyph, scale, error) : nullptr;
                if (scale > 1 && !raster)
                    return false;
                const auto &mask = raster ? raster->mask : glyph.mask;
                const float density = raster ? raster->density : 1.f;
                const float gx = glyph.x + mask.left / density + 14;
                const float gy = glyph.y + mask.top / density - int(caption.scroll) + 5 +
                                 (raster ? glyph.ascent - raster->ascent / (density * 2) : 0);
                const int right = left + int((data.left + gx + mask.width / density) * sx);
                const int bottom = top + int((data.top + gy + mask.height / density) * sy);
                const int startX = left + int((data.left + gx) * sx), startY = top + int((data.top + gy) * sy);
                for (int y = std::max(y0, startY); y < std::min(y1, bottom); ++y)
                    for (int x = std::max(x0, startX); x < std::min(x1, right); ++x)
                    {
                        const auto px = std::min(mask.width - 1, int((x - startX) * density / sx));
                        const auto py = std::min(mask.height - 1, int((y - startY) * density / sy));
                        const auto index = py * mask.pitch + px;
                        if (!mask.background.empty())
                            paint(x, y, glyph.style.back, mask.background[index]);
                        paint(x, y, glyph.style.fore, mask.coverage[index]);
                    }
            }
        }
        if (!authored)
            marcus_caption_quad(layer, image.sceneCamera, image);
    }
    if (authored && impl->transition && impl->previous.rgba.size() == image.rgba.size())
    {
        const auto progress = std::min(1.0, impl->transitionTime / (impl->transition == 4 ? 1000.0 / 6 : 4000.0));
        for (std::size_t pixel = 0; pixel < image.rgba.size() / 4; ++pixel)
        {
            // Deterministic per-pixel reveal, stable across window redraws.
            auto hash = static_cast<std::uint32_t>(pixel);
            hash ^= hash >> 16;
            hash *= 0x7feb352d;
            hash ^= hash >> 15;
            hash *= 0x846ca68b;
            hash ^= hash >> 16;
            const auto threshold = hash / 4294967296.0;
            const std::uint8_t *source = nullptr;
            std::uint8_t fill[4] = {0, 0, 0, 255};
            if (impl->transition == 2)
                fill[0] = fill[1] = fill[2] = 255;
            if (impl->transition == 3)
                source = threshold >= progress ? &impl->previous.rgba[pixel * 4] : &image.rgba[pixel * 4];
            else if (impl->transition == 4)
                source = fill;
            else if (progress < .5)
                source = threshold >= progress * 2 ? &impl->previous.rgba[pixel * 4] : fill;
            else
                source = threshold >= (progress - .5) * 2 ? fill : &image.rgba[pixel * 4];
            if (source != &image.rgba[pixel * 4])
                std::copy_n(source, 4, &image.rgba[pixel * 4]);
        }
    }
    return true;
}
