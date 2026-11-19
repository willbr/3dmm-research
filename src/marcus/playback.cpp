#include "playback.h"
#include "mm/playback.h"
#include "mm/movie_audio.h"
#include "mm/frame_sound.h"
#include "mm/caption_layout.h"
#include "mm/caption_render.h"
#include "caption_quad.h"
#include "sdl_audio.h"
#include "midi_synth.h"
#include "font_host.h"
#include "font_catalog.h"
#include "mm/graphics_transition.h"
#include "mm/text_box_runtime.h"
#include <SDL3/SDL.h>
#include <algorithm>
#include <cmath>
#include <exception>
#include <fstream>
#include <map>
#include <mutex>
#include <new>
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
static_assert(static_cast<int>(MARCUS_TRANSITION_CUT) == MM_GRAPHICS_MOVIE_CUT &&
              static_cast<int>(MARCUS_TRANSITION_FADE_BLACK) == MM_GRAPHICS_MOVIE_FADE_BLACK &&
              static_cast<int>(MARCUS_TRANSITION_FADE_WHITE) == MM_GRAPHICS_MOVIE_FADE_WHITE &&
              static_cast<int>(MARCUS_TRANSITION_DISSOLVE) == MM_GRAPHICS_MOVIE_DISSOLVE &&
              static_cast<int>(MARCUS_TRANSITION_BLACK) == MM_GRAPHICS_MOVIE_BLACK, "Authored movie transition values");
MMPixelSurface TransitionSurface(PreviewImage &image)
{
    return {image.rgba.data(), static_cast<MMI32>(image.width), static_cast<MMI32>(image.height),
            static_cast<MMI32>(image.width * 4), 32};
}
struct AudioClip
{
    MMMovieAudioClip data = {};
    std::shared_ptr<const KauaiAudio::Sample> sample;
    std::vector<PreviewMidiEvent> midi;
};
struct AudioOwner
{
    std::shared_ptr<AudioClip> clip;
    std::unique_ptr<KauaiMidi::Synth> synth;
};
class Audio
{
    mutable std::mutex mutex;
    std::unique_ptr<MMMovieAudio, decltype(&mm_movie_audio_destroy)> mixer{
        mm_movie_audio_create(), mm_movie_audio_destroy};
    std::map<std::tuple<int, unsigned, unsigned, int, unsigned>, std::shared_ptr<AudioClip>> clips;
    std::vector<std::uint8_t> bank;
    std::size_t cachedBytes = 0;
    SDL_AudioStream *stream = nullptr;
    bool initialized = false;

    static void SDLCALL Feed(void *context, SDL_AudioStream *stream, int additional, int)
    {
        auto self = static_cast<Audio *>(context);
        float output[MM_MOVIE_AUDIO_BLOCK_FRAMES * 2];
        while (additional > 0)
        {
            const auto frames = std::min<std::size_t>(MM_MOVIE_AUDIO_BLOCK_FRAMES, (std::size_t(additional) + 7) / 8);
            {
                std::lock_guard<std::mutex> lock(self->mutex);
                mm_movie_audio_render(self->mixer.get(), output, frames);
            }
            const auto bytes = static_cast<int>(frames * 8);
            if (!SDL_PutAudioStreamData(stream, output, bytes))
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
        const SDL_AudioSpec spec = {SDL_AUDIO_F32, 2, KauaiAudio::SampleRate};
        std::unique_ptr<SDL_AudioStream, decltype(&SDL_DestroyAudioStream)> opened(
            SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, Feed, this), SDL_DestroyAudioStream);
        if (!opened || !SDL_ResumeAudioStreamDevice(opened.get()))
            return Fail(error, std::string("Cannot open movie audio output: ") + SDL_GetError());
        stream = opened.release();
        return true;
    }

    std::shared_ptr<AudioClip> LoadClip(Preview &preview, const PreviewSoundCue &cue, Error &error)
    {
        const auto key =
            std::make_tuple(cue.source, cue.sound.type, cue.sound.number,
                            cue.source == 0 ? static_cast<int>(cue.scene.type ? cue.scene.number : preview.Info().asset.number)
                                            : -1, cue.child);
        const auto found = clips.find(key);
        if (found != clips.end())
            return found->second;
        PreviewAudio data;
        if (!preview.ReadSound(cue, data, error))
            return {};
        auto clip = std::make_shared<AudioClip>();
        clip->data.source = data.source;
        clip->data.type = data.asset.type;
        clip->data.number = data.asset.number;
        if (!data.silent)
        {
            if (data.midi)
            {
                clip->midi = std::move(data.events);
                clip->data.midi_events = clip->midi.size();
            }
            else
            {
                if (!KauaiAudio::DecodeWave(data.wave.data(), data.wave.size(), clip->sample))
                {
                    Fail(error, std::string("Cannot decode movie WAVE audio: ") + SDL_GetError());
                    return {};
                }
                clip->data.stereo = clip->sample->stereo.data();
                clip->data.sample_frames = clip->sample->stereo.size() / 2;
            }
        }
        const auto bytes = clip->sample ? clip->sample->stereo.size() * sizeof(float)
                                        : clip->midi.size() * sizeof(PreviewMidiEvent);
        constexpr std::size_t budget = 256u * 1024 * 1024;
        if (bytes > budget)
        {
            Fail(error, "A decoded movie sound exceeds the 256 MiB audio budget.");
            return {};
        }
        if (cachedBytes + bytes > budget)
        {
            clips.clear();
            cachedBytes = 0;
        }
        clips.emplace(key, clip);
        cachedBytes += bytes;
        return clip;
    }

    bool LoadSynth(AudioOwner &owner, Error &error)
    {
        if (bank.empty())
        {
            const auto base = SDL_GetBasePath();
            std::ifstream input(std::string(base ? base : "") + "assets/GeneralUserGS.sf2", std::ios::binary);
            if (!input)
                input.open(MARCUS_MIDI_BANK, std::ios::binary);
            bank.assign(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
        }
        owner.synth = std::make_unique<KauaiMidi::Synth>();
        if (!owner.synth->Load(bank.data(), bank.size(), KauaiAudio::SampleRate))
            return Fail(error, "Cannot load the General MIDI instrument bank (assets/GeneralUserGS.sf2).");
        return true;
    }

  public:
    Audio()
    {
        if (!mixer)
            throw std::bad_alloc();
    }
    ~Audio()
    {
        SDL_DestroyAudioStream(stream);
        if (initialized)
            SDL_QuitSubSystem(SDL_INIT_AUDIO);
    }
    void SetTrackMix(MMAudioCounter lane, bool muted, bool solo)
    {
        std::lock_guard<std::mutex> lock(mutex);
        if (!mm_movie_audio_set_mix(mixer.get(), lane, muted, solo))
            throw std::bad_alloc();
    }
    void TrackMix(MMAudioCounter lane, int &muted, int &solo, int &audible) const
    {
        std::lock_guard<std::mutex> lock(mutex);
        mm_movie_audio_track_mix(mixer.get(), lane, &muted, &solo, &audible);
    }
    void ResetMix()
    {
        std::lock_guard<std::mutex> lock(mutex);
        mm_movie_audio_reset_mix(mixer.get());
    }
    bool MixActive() const
    {
        std::lock_guard<std::mutex> lock(mutex);
        return mm_movie_audio_mix_active(mixer.get()) != 0;
    }
    void Mute(bool value)
    {
        std::lock_guard<std::mutex> lock(mutex);
        mm_movie_audio_mute(mixer.get(), value);
    }
    void Statistics(MarcusAudioStats &output)
    {
        std::lock_guard<std::mutex> lock(mutex);
        MMMovieAudioStats stats;
        mm_movie_audio_stats(mixer.get(), &stats);
        output = {stats.wave_sounds, stats.midi_sounds, stats.rendered_frames, stats.audible_frames};
    }
    void Suspend(bool value)
    {
        std::lock_guard<std::mutex> lock(mutex);
        mm_movie_audio_suspend(mixer.get(), value);
    }
    void Collect()
    {
        std::lock_guard<std::mutex> lock(mutex);
        mm_movie_audio_collect(mixer.get());
    }
    void Clear()
    {
        {
            std::lock_guard<std::mutex> lock(mutex);
            mm_movie_audio_clear(mixer.get());
        }
        if (stream)
            SDL_ClearAudioStream(stream);
        clips.clear();
        cachedBytes = 0;
    }
    void SceneBoundary()
    {
        std::lock_guard<std::mutex> lock(mutex);
        mm_movie_audio_cut(mixer.get());
    }
    bool Pending() const
    {
        std::lock_guard<std::mutex> lock(mutex);
        return mm_movie_audio_pending(mixer.get()) != 0;
    }

    bool Play(Preview &preview, const PreviewSoundCue &cue, MMAudioCounter offset, Error &error)
    {
        std::unique_ptr<MMMovieAudioVoice, decltype(&mm_movie_audio_voice_destroy)> voice(
            nullptr, mm_movie_audio_voice_destroy);
        if (!cue.silent)
        {
            auto clip = LoadClip(preview, cue, error);
            if (!clip)
                return false;
            if (clip->data.sample_frames || clip->data.midi_events)
            {
                auto owner = std::make_unique<AudioOwner>();
                owner->clip = clip;
                if (clip->data.midi_events && !LoadSynth(*owner, error))
                    return false;
                const MMMovieAudioServices services = {
                    owner.get(),
                    [](void *p) { delete static_cast<AudioOwner *>(p); },
                    [](void *p, std::size_t index) {
                        const auto &event = static_cast<AudioOwner *>(p)->clip->midi[index];
                        return MMMovieAudioEvent{event.milliseconds, event.message};
                    },
                    [](void *p, MMU32 message) { static_cast<AudioOwner *>(p)->synth->Message(message); },
                    [](void *p, float *output, std::size_t frames) {
                        static_cast<AudioOwner *>(p)->synth->Render(output, frames);
                    },
                    [](void *p) { static_cast<AudioOwner *>(p)->synth->Reset(); }};
                voice.reset(mm_movie_audio_prepare(&clip->data, &services, offset, cue.volume, cue.loop));
                if (!voice)
                    throw std::bad_alloc();
                owner.release();
                if (!Open(error))
                    return false;
            }
        }
        const auto lane = mm_movie_audio_lane(cue.actorSound, cue.actor, cue.style);
        std::lock_guard<std::mutex> lock(mutex);
        if (!mm_movie_audio_submit(mixer.get(), lane, cue.queue, voice.release()))
            throw std::bad_alloc();
        return true;
    }
};
struct Style
{
    std::string font;
    int pixels = 12, flags = 0;
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
MMCaptionMask MaskGeometry(const KauaiFonts::Mask &mask)
{
    return {mask.left, mask.top, mask.width, mask.height, mask.pitch};
}
// Average supersampled coverage rather than magnifying monochrome font pixels.
KauaiFonts::Mask ReduceCaptionMask(const KauaiFonts::Mask &source)
{
    const auto from = MaskGeometry(source);
    MMCaptionMask to;
    mm_caption_mask_half_size(&from, &to);
    KauaiFonts::Mask result;
    result.left = to.left;
    result.top = to.top;
    result.width = to.width;
    result.height = to.height;
    result.pitch = to.pitch;
    const auto reduce = [&](const std::vector<std::uint8_t> &input, std::vector<std::uint8_t> &output) {
        if (input.empty())
            return;
        output.resize(std::size_t(result.width) * result.height);
        mm_caption_mask_half(&from, input.data(), &to, output.data());
    };
    reduce(source.coverage, result.coverage);
    reduce(source.background, result.background);
    return result;
}
struct CaptionImage
{
    PreviewCaption data;
    std::vector<Glyph> glyphs;
    std::vector<MarcusCaptionStop> stops;
    int height = 0;
    bool ready = false;
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
    std::unique_ptr<CaptionImage> draft;
    int draftScroll = 0;
    std::vector<MMCaptionRow> captionRows;
    MMCaptionStack captionStack{};
    void ObserveCaptions(std::int32_t frame)
    {
        mm_caption_observe(&captionStack, captionRows.data(), captionRows.size(), frame);
    }
    std::map<FaceKey, std::unique_ptr<KauaiFonts::Face>> faces;
    std::shared_ptr<KauaiFonts::FontCatalog> fonts;
    std::size_t captionBytes = 0;
    std::map<std::tuple<std::string, int, int, std::u16string>, CaptionRaster> captionRasters;
    std::size_t rasterBytes = 0;
    Audio audio;
    MMPolledPlayback clock = {};
    bool auditioning = false;
    MMU32 transitionSeed = 0;
    PreviewImage previous;
    MMGraphicsMovieSnapshot transitionPalette{};
    ~Impl()
    {
        mm_graphics_movie_snapshot_dispose(&transitionPalette);
    }
    void ClearTransition()
    {
        previous = {};
        mm_graphics_movie_snapshot_dispose(&transitionPalette);
    }
    bool EnterFrame(Preview &preview, MMI32 frame, int restoreMusic, double fps, MMPolledFrameWait &wait,
                    Error &error);
    bool MeasureScroll(MMI32 frame, int &needsScroll, Error &error);
    void ScrollStep(MMI32 frame);
    void ResetTiming()
    {
        mm_polled_playback_reset(&clock, clock.pacer.fps, clock.was_playing);
        ClearTransition();
        for (auto &row : captionRows)
            row.scroll = 0;
    }
    KauaiFonts::Face *Face(const std::string &font, int pixels, int flags)
    {
        auto key = std::make_tuple(font, pixels, flags);
        auto found = faces.find(key);
        if (found != faces.end())
            return found->second.get();
        if (faces.size() >= 128)
            faces.clear();
        std::vector<std::uint16_t> family;
        auto text = FontText(font.empty() ? "Arial" : font, family);
        std::unique_ptr<KauaiFonts::Face> face;
        if (fonts)
        {
            KauaiFonts::FreeTypeOptions options;
            options.pixels = pixels;
            options.bold = (flags & 1) != 0;
            options.italic = (flags & 2) != 0;
            options.underline = (flags & 4) != 0;
            options.legacyCellMetrics = true;
            const auto familyName = font.empty() ? "Arial" : font;
            if (fonts->Find(familyName, options))
            {
                face = fonts->Open(familyName, options);
                if (!face) return nullptr; // A bundled face must not silently become an installed substitute.
            }
        }
        if (!face)
            face = KauaiFonts::CreateStudioFace(text, pixels, (flags & 1) != 0, (flags & 2) != 0,
                                               (flags & 4) != 0);
        if (!face)
            face = KauaiFonts::CreateStudioFace({"System", 6}, pixels, (flags & 1) != 0,
                                                (flags & 2) != 0, (flags & 4) != 0);
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
        auto face = Face(style.font, style.pixels, style.flags);
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
            const auto from = MaskGeometry(glyph.mask);
            const auto to = MaskGeometry(scaled);
            mm_caption_mask_expand(&from, glyph.mask.coverage.data(), &to, scaled.coverage.data(), scale);
            if (!scaled.background.empty())
                mm_caption_mask_expand(&from, glyph.mask.background.data(), &to, scaled.background.data(), scale);
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
    bool Layout(CaptionImage &caption, Error &error, bool editing = false)
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

        // Match the editor's UTF-8 buffer, which normalizes paragraph breaks to LF.
        std::vector<int> offsets;
        if (editing)
        {
            offsets.resize(data.text.size() + 1, -1);
            int offset = 0;
            for (std::size_t i = 0; i < data.text.size(); ++i)
            {
                offsets[i] = offset;
                const auto unit = data.text[i];
                if (unit == '\r' && i + 1 < data.text.size() && data.text[i + 1] == '\n')
                    ++i;
                if (unit >= 0xd800 && unit <= 0xdbff && i + 1 < data.text.size() &&
                    data.text[i + 1] >= 0xdc00 && data.text[i + 1] <= 0xdfff)
                {
                    ++i;
                    offset += 4;
                }
                else
                    offset += unit < 0x80 ? 1 : unit < 0x800 ? 2 : 3;
            }
            offsets.back() = offset;
        }
        struct Context
        {
            Impl &owner;
            CaptionImage &caption;
            Error &error;
            const std::vector<int> &offsets;
            KauaiFonts::Face *face = nullptr;
            std::exception_ptr failure;
        } context{*this, caption, error, offsets, nullptr, {}};
        const MMCaptionLayoutInput input{
            data.text.data(), static_cast<MMI32>(data.text.size()), std::max(1, data.right - data.left - 2 * MM_CAPTION_INDENT),
            data.fontSize, {data.font.data(), data.font.size()}, data.properties.size()};
        const MMCaptionLayoutServices services{
            &context,
            [](void *p, std::size_t index, MMCaptionProperty *out) {
                const auto &property = static_cast<Context *>(p)->caption.data.properties[index];
                *out = {property.position, property.kind, property.value, {property.font.data(), property.font.size()}};
            },
            [](void *p, const MMCaptionStyle *style) -> int {
                auto &ctx = *static_cast<Context *>(p);
                try
                {
                    ctx.face = ctx.owner.Face({style->font.name, style->font.length}, style->pixels, style->flags);
                    if (!ctx.face)
                        throw std::runtime_error("Cannot open a movie caption font.");
                    return 0;
                }
                catch (...) { ctx.failure = std::current_exception(); return MM_CAPTION_LAYOUT_FAILED; }
            },
            [](void *p, const unsigned short *chars, MMI32 count, MMI32 *width, MMI32 *top, MMI32 *bottom) -> int {
                auto &ctx = *static_cast<Context *>(p);
                try
                {
                    KauaiFonts::Metrics metrics;
                    if (!ctx.face->Measure({chars, static_cast<std::size_t>(count), KauaiFonts::Encoding::utf16}, metrics))
                        throw std::runtime_error("Cannot measure a movie caption run.");
                    *width = metrics.advance;
                    *top = -metrics.ascent;
                    *bottom = metrics.cellHeight + metrics.externalLeading - metrics.ascent;
                    return 0;
                }
                catch (...) { ctx.failure = std::current_exception(); return MM_CAPTION_LAYOUT_FAILED; }
            },
            [](void *p, const MMCaptionStyle *style, const unsigned short *chars, MMI32 count,
               MMI32 x, MMI32 baseline) -> int {
                auto &ctx = *static_cast<Context *>(p);
                try
                {
                    Glyph glyph;
                    glyph.style = {{style->font.name, style->font.length}, style->pixels, style->flags,
                                   style->foreground, style->background};
                    glyph.text.assign(chars, chars + count);
                    const KauaiFonts::Text text{glyph.text.data(), glyph.text.size(), KauaiFonts::Encoding::utf16};
                    KauaiFonts::Metrics metrics;
                    auto face = ctx.owner.Face(glyph.style.font, glyph.style.pixels, glyph.style.flags);
                    if (!face || !face->Measure(text, metrics) || !face->Rasterize(text, glyph.mask, true))
                    {
                        Fail(ctx.error, "Cannot rasterize a movie caption run.");
                        return MM_CAPTION_LAYOUT_FAILED;
                    }
                    glyph.x = x;
                    glyph.y = baseline - metrics.ascent;
                    glyph.ascent = metrics.ascent;
                    glyph.height = metrics.cellHeight + metrics.externalLeading;
                    const auto bytes = glyph.mask.coverage.size() + glyph.mask.background.size();
                    if (ctx.owner.captionBytes + bytes > 128u * 1024 * 1024)
                    {
                        Fail(ctx.error, "Movie caption runs exceed the 128 MiB scene budget.");
                        return MM_CAPTION_LAYOUT_FAILED;
                    }
                    ctx.owner.captionBytes += bytes;
                    ctx.caption.maskBytes += bytes;
                    ctx.caption.glyphs.push_back(std::move(glyph));
                    return 0;
                }
                catch (...) { ctx.failure = std::current_exception(); return MM_CAPTION_LAYOUT_FAILED; }
            },
            editing ? +[](void *p, const MMCaptionLayoutRun *run) -> int {
                auto &ctx = *static_cast<Context *>(p);
                try
                {
                    auto &stops = ctx.caption.stops;
                    const auto &box = ctx.caption.data;
                    const auto limit = std::min(run->end, static_cast<MMI32>(box.text.size()));
                    const float y = float(box.top) + MM_CAPTION_BORDER + run->top;
                    for (auto i = run->first; i <= limit; ++i)
                    {
                        const int offset = ctx.offsets[i];
                        if (offset < 0)
                            continue;
                        MMI32 x = run->x;
                        const auto drawn = i - run->first;
                        if (drawn && drawn < run->drawn)
                        {
                            KauaiFonts::Metrics metrics;
                            if (!ctx.face->Measure({run->text, static_cast<std::size_t>(drawn),
                                                   KauaiFonts::Encoding::utf16}, metrics))
                                throw std::runtime_error("Cannot measure a movie caption caret.");
                            x += metrics.advance;
                        }
                        else if (drawn)
                            x = run->right;
                        const MarcusCaptionStop stop{offset, float(box.left) + MM_CAPTION_INDENT + x,
                                                     y, float(run->height)};
                        if (!stops.empty() && stops.back().offset == offset && stops.back().y == y)
                            stops.back() = stop;
                        else
                            stops.push_back(stop);
                    }
                    return 0;
                }
                catch (...) { ctx.failure = std::current_exception(); return MM_CAPTION_LAYOUT_FAILED; }
            } : nullptr};
        const int status = mm_caption_layout(&input, &services, &caption.height);
        if (context.failure)
            std::rethrow_exception(context.failure);
        if (status == MM_CAPTION_LAYOUT_STALLED)
            return Fail(error, "Caption layout did not advance.");
        caption.ready = status == 0;
        return caption.ready;
    }
};
MarcusPlayback::MarcusPlayback() : impl(new Impl)
{
    mm_polled_playback_reset(&impl->clock, -1, 0);
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
    timing.transition = mm_graphics_movie_transition(presentation.transition).duration_ms / 1000;
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
            const int overflow = mm_caption_scroll_overflow(caption.height, data.top, data.bottom);
            timing.scrolling[data.firstFrame] =
                std::max(timing.scrolling[data.firstFrame], mm_scroll_nominal_seconds(overflow));
            calculator.captionBytes -= caption.maskBytes;
        }
    return true;
}
bool MarcusPlayback::RestoreAudio(Preview &preview, const std::vector<MarcusResumeSound> &sounds, Error &error)
{
    impl->audio.Suspend(true);
    for (const auto &sound : sounds)
        if (!impl->audio.Play(preview, sound.cue, mm_frame_sound_resume_samples(sound.seconds), error))
            return false;
    impl->clock.restore_music = 0;
    return true;
}
bool MarcusPlayback::Select(MM::Preview &preview, MM::Error &error, bool preserveAudio)
{
    if (preserveAudio)
    {
        impl->audio.SceneBoundary();
        impl->ResetTiming();
        impl->clock.was_playing = 1;
    }
    else
        Stop();
    impl->captions.clear();
    impl->captionRows.clear();
    impl->captionStack = {};
    ClearCaptionDraft();
    impl->captionBytes = 0;
    impl->captionRasters.clear();
    impl->rasterBytes = 0;
    if (!preview.ReadPresentation(impl->scene, error))
        return false;
    impl->captions.reserve(impl->scene.captions.size());
    impl->captionRows.reserve(impl->scene.captions.size());
    for (auto &caption : impl->scene.captions)
    {
        impl->captionRows.push_back({caption.firstFrame, caption.endFrame, 0, 0, 0, 0});
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
    impl->clock.was_playing = 0;
}
bool MarcusPlayback::Audition(Preview &preview, PreviewSoundCue cue, Error &error)
{
    Stop(); cue.loop = false; cue.queue = false;
    if (!impl->audio.Play(preview, cue, {}, error)) return false;
    impl->auditioning = true; impl->audio.Suspend(false); return true;
}
bool MarcusPlayback::Auditioning() const
{
    return impl->auditioning && impl->audio.Pending();
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
    impl->audio.SetTrackMix(mm_movie_audio_lane(actorSound, static_cast<MMI32>(actor), static_cast<MMI32>(style)), muted, solo);
}
void MarcusPlayback::TrackMix(bool actorSound, long actor, long style, int &muted, int &solo, int &audible) const
{
    impl->audio.TrackMix(mm_movie_audio_lane(actorSound, static_cast<MMI32>(actor), static_cast<MMI32>(style)), muted, solo, audible);
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
    mm_polled_playback_set_rate(&impl->clock, fps);
}
void MarcusPlayback::Seek(MM::Preview *preview)
{
    impl->audio.Clear();
    impl->ResetTiming();
    if (preview) impl->ObserveCaptions(preview->Info().currentFrame);
}
const MMPolledPlayback &MarcusPlayback::Clock() const
{
    return impl->clock;
}
int MarcusPlayback::Transition() const
{
    return impl->scene.transition;
}
bool MarcusPlayback::Transitioning() const
{
    return impl->clock.transition != 0;
}
bool MarcusPlayback::PaletteAnimating() const
{
    return impl->transitionPalette.indices != nullptr;
}
double MarcusPlayback::FrameSeconds() const
{
    return mm_polled_playback_frame_seconds(&impl->clock);
}
void MarcusPlayback::BeginTransition(int kind, MM::PreviewImage previous,
                                     const unsigned char *outgoingPalette, const MM::Preview &incoming)
{
    if (previous.rgba.empty())
        return;
    const auto surface = TransitionSurface(previous);
    const MMGraphicsMoviePaletteSource palettes{
        &incoming,
        [](const void *p, unsigned char *colours) {
            return int(static_cast<const Preview *>(p)->CopyPresentationPalette(colours));
        },
        [](const void *p, unsigned char *colours) {
            static_cast<const Preview *>(p)->OverlayTransitionPalette(colours);
        }};
    const int prepared = mm_graphics_movie_snapshot_prepare(&impl->transitionPalette, kind, &surface,
                                                            outgoingPalette, &palettes);
    if (prepared < 0)
        throw std::bad_alloc();
    if (!prepared)
        return;
    mm_polled_playback_begin_transition(&impl->clock, kind);
    impl->transitionSeed = static_cast<MMU32>(SDL_GetTicks());
    impl->previous = std::move(previous);
}
bool MarcusPlayback::Impl::EnterFrame(Preview &preview, MMI32 frame, int restoreMusic, double fps,
                                      MMPolledFrameWait &wait, Error &error)
{
    struct Context
    {
        Preview &preview;
        Audio &audio;
        Error &error;
        const PreviewPresentation &scene;
        std::vector<PreviewSoundCue> actors;
        std::exception_ptr failure;

        const PreviewSoundCue &Cue(int actor, std::size_t index) const
        {
            return actor ? actors[index] : scene.sounds[index];
        }
    } context{preview, audio, error, scene, {}, {}};
    const MMFrameSoundServices services{
        &context, scene.sounds.size(), scene.pauses.size(),
        [](void *p, std::size_t *count) -> int {
            auto &ctx = *static_cast<Context *>(p);
            try
            {
                ctx.actors = ctx.preview.FrameSounds();
                *count = ctx.actors.size();
                return 1;
            }
            catch (...) { ctx.failure = std::current_exception(); return 0; }
        },
        [](void *p, int actor, std::size_t index, MMFrameSoundCue *out) {
            const auto &cue = static_cast<Context *>(p)->Cue(actor, index);
            *out = {cue.frame, cue.style, cue.actor, cue.queue, cue.motion};
        },
        [](void *p, int actor, std::size_t index, MMAudioCounter offset) -> int {
            auto &ctx = *static_cast<Context *>(p);
            try { return ctx.audio.Play(ctx.preview, ctx.Cue(actor, index), offset, ctx.error); }
            catch (...) { ctx.failure = std::current_exception(); return 0; }
        },
        [](void *p, std::size_t index, MMI32 *frame, MMPolledFrameWait *out) {
            const auto &pause = static_cast<Context *>(p)->scene.pauses[index];
            *frame = pause.frame;
            *out = {pause.kind, static_cast<double>(pause.milliseconds)};
        }};
    const bool ok = mm_frame_sound_enter(frame, restoreMusic, fps, &services, &wait) != 0;
    if (context.failure)
        std::rethrow_exception(context.failure);
    return ok;
}
bool MarcusPlayback::Impl::MeasureScroll(MMI32 frame, int &needsScroll, Error &error)
{
    needsScroll = 0;
    for (std::size_t i = 0; i < captions.size(); ++i)
    {
        auto &caption = captions[i];
        if (mm_text_box_scroll_frame(caption.data.story, caption.data.firstFrame, frame))
        {
            if (!Layout(caption, error))
                return false;
            const auto overflow = mm_caption_scroll_overflow(caption.height, caption.data.top, caption.data.bottom);
            needsScroll = needsScroll || captionRows[i].scroll < overflow;
        }
    }
    return true;
}
void MarcusPlayback::Impl::ScrollStep(MMI32 frame)
{
    for (std::size_t i = 0; i < captions.size(); ++i)
    {
        const auto &caption = captions[i];
        if (mm_text_box_scroll_frame(caption.data.story, caption.data.firstFrame, frame))
        {
            const auto overflow = mm_caption_scroll_overflow(caption.height, caption.data.top, caption.data.bottom);
            if (captionRows[i].scroll < overflow)
                ++captionRows[i].scroll;
        }
    }
}
bool MarcusPlayback::Update(MM::Preview &preview, bool playing, double elapsed, bool click, bool &advance,
                            MM::Error &error)
{
    const auto frame = preview.Info().currentFrame;
    impl->ObserveCaptions(frame);
    advance = false;
    if (playing && impl->auditioning) { impl->audio.Clear(); impl->auditioning = false; }
    impl->audio.Collect();
    if (impl->auditioning && !impl->audio.Pending()) impl->auditioning = false;
    impl->audio.Suspend(!playing && !impl->auditioning);

    struct Services
    {
        Impl &host;
        Preview &preview;
        Error &error;
        MMI32 frame;
        std::exception_ptr failure;
    } context{*impl, preview, error, frame, {}};
    const MMPolledPlaybackServices services{
        &context,
        [](void *data, int restoreMusic, double fps, MMPolledFrameWait *wait) -> int {
            auto &ctx = *static_cast<Services *>(data);
            try { return ctx.host.EnterFrame(ctx.preview, ctx.frame, restoreMusic, fps, *wait, ctx.error); }
            catch (...) { ctx.failure = std::current_exception(); return 0; }
        },
        [](void *data, int *needsScroll) -> int {
            auto &ctx = *static_cast<Services *>(data);
            try { return ctx.host.MeasureScroll(ctx.frame, *needsScroll, ctx.error); }
            catch (...) { ctx.failure = std::current_exception(); return 0; }
        },
        [](void *data, int *pending) -> int {
            auto &ctx = *static_cast<Services *>(data);
            try { *pending = ctx.host.audio.Pending(); return 1; }
            catch (...) { ctx.failure = std::current_exception(); return 0; }
        },
        [](void *data) { static_cast<Services *>(data)->host.ClearTransition(); }
    };
    MMPolledPlaybackUpdate update;
    if (!mm_polled_playback_update(&impl->clock, playing, elapsed, click, &services, &update))
    {
        if (context.failure)
            std::rethrow_exception(context.failure);
        return false;
    }
    if (update.scroll_step)
        impl->ScrollStep(frame);
    advance = update.advance != 0;
    return true;
}
bool MarcusPlayback::CaptionAt(int x, int y, std::uint32_t &chunk, const std::uint32_t *hiddenCaption) const
{
    for (auto row = impl->captionStack.last; row; row = impl->captionRows[row - 1].previous)
    {
        const auto &box = impl->captions[row - 1].data;
        if (hiddenCaption && box.chunk == *hiddenCaption)
            continue;
        if (x >= box.left && x < box.right && y >= box.top && y < box.bottom)
        {
            chunk = box.chunk;
            return true;
        }
    }
    return false;
}

void MarcusPlayback::ClearCaptionDraft()
{
    if (impl->draft)
        impl->captionBytes -= impl->draft->maskBytes;
    impl->draft.reset();
    impl->draftScroll = 0;
}

bool MarcusPlayback::CaptionDraft(PreviewCaption data, Error &error)
{
    const auto scroll = impl->draftScroll;
    ClearCaptionDraft();
    impl->draft = std::make_unique<CaptionImage>();
    impl->draft->data = std::move(data);
    try
    {
        if (impl->Layout(*impl->draft, error, true))
        {
            const auto &box = impl->draft->data;
            impl->draftScroll = std::min(scroll, mm_caption_scroll_overflow(impl->draft->height, box.top, box.bottom));
            return true;
        }
    }
    catch (...)
    {
        ClearCaptionDraft();
        throw;
    }
    ClearCaptionDraft();
    return false;
}

const std::vector<MarcusCaptionStop> &MarcusPlayback::CaptionPositions(int &scroll) const
{
    static const std::vector<MarcusCaptionStop> empty;
    scroll = impl->draftScroll;
    return impl->draft ? impl->draft->stops : empty;
}

bool MarcusPlayback::CaptionCaret(int offset)
{
    if (!impl->draft)
        return false;
    const auto &draft = *impl->draft;
    const auto &box = draft.data;
    const auto before = impl->draftScroll;
    auto scroll = before;
    const auto caret = std::find_if(draft.stops.rbegin(), draft.stops.rend(),
                                    [offset](const auto &stop) { return stop.offset == offset; });
    if (offset < 0)
        scroll = 0;
    else if (caret != draft.stops.rend())
    {
        const auto top = caret->y - float(box.top) - MM_CAPTION_BORDER;
        const auto bottom = top + caret->height - (box.bottom - box.top - 2 * MM_CAPTION_BORDER);
        scroll = std::min(std::max(scroll, static_cast<int>(bottom)), static_cast<int>(top));
        scroll = std::clamp(scroll, 0, mm_caption_scroll_overflow(draft.height, box.top, box.bottom));
    }
    impl->draftScroll = scroll;
    return before != scroll;
}

bool MarcusPlayback::CaptionFontSample(const std::string &family, PreviewImage &image)
{
    auto face = impl->Face(family, 20, 0);
    KauaiFonts::Mask mask;
    if (!face || !face->Rasterize({"Aa Bb 123", 9}, mask) || mask.width <= 0 || mask.height <= 0 ||
        mask.width > 1024 || mask.height > 128 || mask.pitch < mask.width ||
        mask.coverage.size() < std::size_t(mask.pitch) * mask.height)
        return false;
    image.width = mask.width;
    image.height = mask.height;
    image.rgba.assign(std::size_t(mask.width) * mask.height * 4, 255);
    for (int y = 0; y < mask.height; ++y)
        for (int x = 0; x < mask.width; ++x)
            image.rgba[(std::size_t(y) * mask.width + x) * 4 + 3] = mask.coverage[std::size_t(y) * mask.pitch + x];
    return true;
}

bool MarcusPlayback::Composite(MM::Preview &preview, bool authored, MM::PreviewImage &image,
                               MM::Error &error, const std::uint32_t *hiddenCaption, bool editDraft)
{
    impl->ObserveCaptions(preview.Info().currentFrame);
    auto *draft = editDraft ? impl->draft.get() : nullptr;
    if ((!impl->captionStack.first && !draft) || (!authored && !image.sceneCamera.valid))
        return true;
    static_assert(PreviewCaptionWidth == MM_CAPTION_WIDTH && PreviewCaptionHeight == MM_CAPTION_HEIGHT,
                  "Caption workspace dimensions must agree.");
    MM::PreviewImage layer;
    auto surface = &image;
    if (!authored)
    {
        MMU32 width, height;
        mm_caption_layer_size(image.width, image.height, &width, &height);
        layer.width = width;
        layer.height = height;
        layer.rgba.resize(std::size_t(layer.width) * layer.height * 4);
        surface = &layer;
    }
    const float aspect = authored && image.sceneCamera.valid ? image.sceneCamera.aspect
        : float(PreviewCaptionWidth) / PreviewCaptionHeight;
    MMCaptionCanvas canvas;
    if (!mm_caption_canvas(&canvas, surface->rgba.data(), surface->rgba.size(), surface->width, surface->height, aspect))
        return true;
    const auto draw = [&](CaptionImage &caption, int scroll) {
        const auto &data = caption.data;
        const MMTextBoxBounds box = {data.left, data.top, data.right, data.bottom};
        if (!mm_caption_box_visible(&box))
            return true;
        if (!impl->Layout(caption, error))
            return false;
        mm_caption_box(&canvas, &box, preview.PresentationColour(data.background));
        for (const auto &glyph : caption.glyphs)
        {
            const auto scale = mm_caption_raster_scale(&canvas, glyph.style.pixels);
            const CaptionRaster *raster = scale > 1 ? impl->Raster(glyph, scale, error) : nullptr;
            if (scale > 1 && !raster)
                return false;
            const auto &mask = raster ? raster->mask : glyph.mask;
            const MMCaptionGlyph run = {
                MaskGeometry(mask), mask.coverage.empty() ? nullptr : mask.coverage.data(),
                mask.background.empty() ? nullptr : mask.background.data(), glyph.x, glyph.y,
                glyph.ascent, raster ? raster->ascent : 0, raster ? raster->density : 1.f, raster != nullptr,
                preview.PresentationColour(glyph.style.fore), preview.PresentationColour(glyph.style.back)};
            mm_caption_glyph(&canvas, &box, scroll, &run);
        }
        return true;
    };
    for (auto row = impl->captionStack.first; row; row = impl->captionRows[row - 1].next)
    {
        auto &caption = impl->captions[row - 1];
        if (hiddenCaption && caption.data.chunk == *hiddenCaption)
        {
            if (draft && !draw(*draft, impl->draftScroll))
                return false;
            draft = nullptr;
        }
        else if (!draw(caption, impl->captionRows[row - 1].scroll))
            return false;
    }
    if (draft && !draw(*draft, impl->draftScroll))
        return false;
    if (!authored)
        marcus_caption_quad(layer, image.sceneCamera, image);
    return true;
}

void MarcusPlayback::CompositeTransition(MM::PreviewImage &image) const
{
    if (!impl->clock.transition)
        return;
    const auto outgoing = TransitionSurface(impl->previous);
    const auto incoming = TransitionSurface(image);
    mm_graphics_movie_snapshot_render(&impl->transitionPalette, impl->clock.transition, impl->clock.transition_ms,
                                     impl->transitionSeed, &outgoing, &incoming,
                                     image.paletteIndices.empty() ? nullptr : image.paletteIndices.data());
}
