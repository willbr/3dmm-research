#include "playback.h"
#include "playback_audio.h"
#include "playback_scene.h"
#include "mm/playback.h"
#include "mm/frame_sound.h"
#include "mm/caption_layout.h"
#include "mm/caption_render.h"
#include "caption_quad.h"
#include "caption_faces.h"
#include "caption_raster.h"
#include "caption_runs.h"
#include "caption_scene.h"
#include "caption_catalog.h"
#include "sdl_audio.h"
#include "midi_synth.h"
#include "font_host.h"
#include "font_catalog.h"
#include "mm/graphics_transition.h"
#include "mm/text_box_runtime.h"
#include <SDL3/SDL.h>
#include <cmath>
#include <exception>
#include <new>
#include <stdexcept>
#include <utility>

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
MMGraphicsMoviePaletteSource TransitionPalettes(const Preview &preview)
{
    return {&preview,
        [](const void *p, unsigned char *colours) {
            return int(static_cast<const Preview *>(p)->CopyPresentationPalette(colours));
        },
        [](const void *p, unsigned char *colours) {
            static_cast<const Preview *>(p)->OverlayTransitionPalette(colours);
        }};
}
struct DecodedAudio
{
    KauaiAudio::Sample sample;
    std::vector<PreviewMidiEvent> midi;
};
static_assert(KauaiAudio::SampleRate == MARCUS_PLAYBACK_AUDIO_RATE, "Movie decoder and output sample rate");
struct AudioDecodeContext
{
    MarcusPlaybackAudio *audio;
    Preview &preview;
    const MMPreviewCue &cue;
    Error &error;
    std::exception_ptr failure;
};

int ReadAudioMedia(void *p, MMMovieAudioMedia *out)
{
    auto &ctx = *static_cast<AudioDecodeContext *>(p);
    try
    {
        PreviewAudio data;
        if (!ctx.preview.ReadSoundCue(ctx.cue, data, ctx.error))
            return 0;
        auto media = std::make_unique<DecodedAudio>();
        MMMovieAudioClip clip{data.source, data.asset.type, data.asset.number, nullptr, 0, 0};
        if (!data.silent)
        {
            if (data.midi)
            {
                media->midi = std::move(data.events);
                clip.midi_events = media->midi.size();
            }
            else
            {
                if (!KauaiAudio::DecodeWave(data.wave.data(), data.wave.size(), media->sample))
                    return Fail(ctx.error, std::string("Cannot decode movie WAVE audio: ") + SDL_GetError());
                clip.stereo = mm_audio_sample_data(media->sample.get());
                clip.sample_frames = mm_audio_sample_frames(media->sample.get());
            }
        }
        *out = {clip, media.release(),
            [](void *p) { delete static_cast<DecodedAudio *>(p); },
            [](void *p, std::size_t index) {
                const auto &event = static_cast<const DecodedAudio *>(p)->midi[index];
                return MMMovieAudioEvent{event.milliseconds, event.message};
            }};
        return 1;
    }
    catch (...) { ctx.failure = std::current_exception(); return 0; }
}

int CreateAudioSynth(void *p, MMMovieAudioSynth *out)
{
    auto &ctx = *static_cast<AudioDecodeContext *>(p);
    try
    {
        std::size_t bytes;
        const auto bank = marcus_playback_audio_bank(ctx.audio, &bytes);
        auto synth = std::make_unique<KauaiMidi::Synth>();
        if (!synth->Load(bank, bytes, MARCUS_PLAYBACK_AUDIO_RATE))
            return Fail(ctx.error, "Cannot load the General MIDI instrument bank (assets/GeneralUserGS.sf2).");
        *out = {synth.release(),
            [](void *p) { delete static_cast<KauaiMidi::Synth *>(p); },
            [](void *p, MMU32 message) { static_cast<KauaiMidi::Synth *>(p)->Message(message); },
            [](void *p, float *output, std::size_t frames) {
                static_cast<KauaiMidi::Synth *>(p)->Render(output, frames);
            },
            [](void *p) { static_cast<KauaiMidi::Synth *>(p)->Reset(); }};
        return 1;
    }
    catch (...) { ctx.failure = std::current_exception(); return 0; }
}

bool PlayAudio(MarcusPlaybackAudio *audio, Preview &preview, const MMPreviewCue &cue, MMAudioCounter offset,
               Error &error, bool audition = false)
{
    AudioDecodeContext ctx{audio, preview, cue, error, {}};
    const MMMovieAudioDecode decode{&ctx, ReadAudioMedia, CreateAudioSynth};
    char message[512];
    const int status = marcus_playback_audio_play(audio, &cue, preview.Info().asset.number, offset,
                                                  &decode, audition, message, sizeof(message));
    if (ctx.failure)
        std::rethrow_exception(ctx.failure);
    if (status == MM_MOVIE_AUDIO_CACHE_LIMIT)
        return Fail(error, "A decoded movie sound exceeds the 256 MiB audio budget.");
    if (status == MM_MOVIE_AUDIO_CACHE_MEMORY)
        throw std::bad_alloc();
    if (status == MARCUS_PLAYBACK_AUDIO_DEVICE)
        return Fail(error, message);
    return status == MM_MOVIE_AUDIO_CACHE_OK;
}
MarcusCaptionRaster CaptionMask(const KauaiFonts::Mask &mask)
{
    return {{mask.left, mask.top, mask.width, mask.height, mask.pitch},
            mask.coverage.empty() ? nullptr : mask.coverage.data(),
            mask.background.empty() ? nullptr : mask.background.data(), 0, 1.f};
}
MarcusCaptionSource CaptionSource(const PreviewCaption &caption)
{
    return {{ {caption.left, caption.top, caption.right, caption.bottom}, caption.text.data(), caption.text.size(),
              caption.fontSize, {caption.font.data(), caption.font.size()}, caption.properties.size(), &caption,
              [](const void *p, std::size_t index, MMCaptionProperty *out) {
                  const auto &property = static_cast<const PreviewCaption *>(p)->properties[index];
                  *out = {property.position, property.kind, property.value, {property.font.data(), property.font.size()}};
              }},
            caption.firstFrame, caption.endFrame, caption.chunk, caption.background, caption.story};
}
} // namespace
struct MarcusPlayback::Impl
{
    MarcusPlaybackScene scene;
    MarcusCaptionScene captionScene{};
    MarcusCaptionFaces faces{};
    std::shared_ptr<KauaiFonts::FontCatalog> fonts;
    MarcusCaptionRasterCache captionRasters{};
    MarcusCaptionSample captionSample{};
    std::unique_ptr<MarcusPlaybackAudio, decltype(&marcus_playback_audio_destroy)> audio{
        nullptr, marcus_playback_audio_destroy};
    MMPolledPlayback clock = {};
    MMGraphicsMovieFrames movieFrames;
    Impl()
    {
        if (!marcus_caption_scene_init(&captionScene))
            throw std::bad_alloc();
        marcus_playback_scene_init(&scene);
        mm_graphics_movie_frames_init(&movieFrames);
    }
    ~Impl()
    {
        marcus_caption_faces_clear(&faces);
        marcus_caption_raster_clear(&captionRasters);
        marcus_caption_sample_clear(&captionSample);
        marcus_caption_scene_dispose(&captionScene);
        marcus_playback_scene_clear(&scene);
        mm_graphics_movie_frames_dispose(&movieFrames);
    }
    void ClearTransition()
    {
        mm_graphics_movie_frames_clear(&movieFrames);
    }
    bool EnterFrame(Preview &preview, MMI32 frame, int restoreMusic, double fps, MMPolledFrameWait &wait,
                    Error &error);
    void ResetTiming()
    {
        mm_polled_playback_reset(&clock, clock.pacer.fps, clock.was_playing);
        ClearTransition();
        mm_caption_presentation_rewind(&captionScene.presentation);
    }
    KauaiFonts::Face *Face(const MMCaptionStyle &style)
    {
        struct Source
        {
            KauaiFonts::FontCatalog *bundle;
            std::exception_ptr failure;
        } source{fonts.get(), {}};
        const MarcusCaptionFaceServices services{
            &source,
            fonts ? +[](void *p, const MMCaptionFont *name, const MarcusCaptionFaceOptions *settings, void **out) -> int {
                auto &source = *static_cast<Source *>(p);
                try
                {
                    const std::string family(name->name, name->length);
                    KauaiFonts::FreeTypeOptions options;
                    options.pixels = settings->pixels;
                    options.bold = settings->bold;
                    options.italic = settings->italic;
                    options.underline = settings->underline;
                    options.legacyCellMetrics = true;
                    if (!source.bundle->Find(family, options))
                        return MARCUS_CAPTION_FACE_MISSING;
                    auto face = source.bundle->Open(family, options);
                    if (!face)
                        return MARCUS_CAPTION_FACE_FAILED;
                    *out = face.release();
                    return MARCUS_CAPTION_FACE_READY;
                }
                catch (...) { source.failure = std::current_exception(); return MARCUS_CAPTION_FACE_FAILED; }
            } : nullptr,
            [](void *p, const void *name, std::size_t length, int utf16,
               const MarcusCaptionFaceOptions *settings, void **out) -> int {
                auto &source = *static_cast<Source *>(p);
                try
                {
                    const auto encoding = utf16 ? KauaiFonts::Encoding::utf16 : KauaiFonts::Encoding::legacyBytes;
                    auto face = KauaiFonts::CreateStudioFace({name, length, encoding}, settings->pixels,
                                                            settings->bold, settings->italic, settings->underline);
                    if (!face)
                        return MARCUS_CAPTION_FACE_MISSING;
                    *out = face.release();
                    return MARCUS_CAPTION_FACE_READY;
                }
                catch (...) { source.failure = std::current_exception(); return MARCUS_CAPTION_FACE_FAILED; }
            },
            [](void *p) { delete static_cast<KauaiFonts::Face *>(p); }};
        void *face = nullptr;
        const int status = marcus_caption_face_get(&faces, &style, &services, &face);
        if (source.failure)
            std::rethrow_exception(source.failure);
        if (status == MARCUS_CAPTION_FACE_MEMORY)
            throw std::bad_alloc();
        return static_cast<KauaiFonts::Face *>(face);
    }
    struct CaptionFontHost
    {
        Impl &host;
        KauaiFonts::Face *face = nullptr;
        std::exception_ptr failure;

        template<class Callback> int Read(Callback callback)
        {
            try { return callback(); }
            catch (...) { failure = std::current_exception(); return 0; }
        }

        MarcusCaptionFonts Services()
        {
            return {this,
                [](void *p, const MMCaptionStyle *style) -> int {
                    auto &ctx = *static_cast<CaptionFontHost *>(p);
                    return ctx.Read([&] {
                        ctx.face = ctx.host.Face(*style);
                        return ctx.face != nullptr;
                    });
                },
                [](void *p, const unsigned short *chars, MMI32 count, MarcusCaptionMetrics *out) -> int {
                    auto &ctx = *static_cast<CaptionFontHost *>(p);
                    return ctx.Read([&] {
                        KauaiFonts::Metrics metrics;
                        const KauaiFonts::Text text{chars, static_cast<std::size_t>(count), KauaiFonts::Encoding::utf16};
                        if (!ctx.face->Measure(text, metrics))
                            return false;
                        *out = {metrics.advance, metrics.ascent, metrics.cellHeight, metrics.externalLeading};
                        return true;
                    });
                },
                [](void *p, const unsigned short *chars, MMI32 count, MarcusCaptionMaskOwner *out) -> int {
                    auto &ctx = *static_cast<CaptionFontHost *>(p);
                    return ctx.Read([&] {
                        auto mask = std::make_unique<KauaiFonts::Mask>();
                        const KauaiFonts::Text text{chars, static_cast<std::size_t>(count), KauaiFonts::Encoding::utf16};
                        if (!ctx.face->Rasterize(text, *mask, true))
                            return false;
                        *out = {CaptionMask(*mask), mask->coverage.size() + mask->background.size(), mask.get(),
                                [](void *value) { delete static_cast<KauaiFonts::Mask *>(value); }};
                        mask.release();
                        return true;
                    });
                }};
        }

        void Check() const
        {
            if (failure)
                std::rethrow_exception(failure);
        }
    };
    static bool CaptionStatus(int status, Error &error)
    {
        switch (status)
        {
        case 0: return true;
        case MARCUS_CAPTION_RUNS_MEMORY: throw std::bad_alloc();
        case MARCUS_CAPTION_RUNS_BOUNDS: return Fail(error, "Caption bounds or text exceed the playback layout limits.");
        case MARCUS_CAPTION_RUNS_FONT: return Fail(error, "Cannot open a movie caption font.");
        case MARCUS_CAPTION_RUNS_MEASURE: return Fail(error, "Cannot measure a movie caption run.");
        case MARCUS_CAPTION_RUNS_RASTER: return Fail(error, "Cannot rasterize a movie caption run.");
        case MARCUS_CAPTION_RUNS_CARET: return Fail(error, "Cannot measure a movie caption caret.");
        case MARCUS_CAPTION_RUNS_BUDGET: return Fail(error, "Movie caption runs exceed the 128 MiB scene budget.");
        case MARCUS_CAPTION_SCENE_RASTER_METRICS: return Fail(error, "Cannot render a high-resolution caption glyph.");
        case MARCUS_CAPTION_SCENE_RASTER_SOURCE: return Fail(error, "Cannot render a high-resolution caption run.");
        case MM_CAPTION_LAYOUT_STALLED: return Fail(error, "Caption layout did not advance.");
        default: return false;
        }
    }
};
MarcusPlayback::MarcusPlayback() : impl(new Impl)
{
    impl->audio.reset(marcus_playback_audio_create(MARCUS_MIDI_BANK));
    if (!impl->audio)
        throw std::bad_alloc();
    mm_polled_playback_reset(&impl->clock, -1, 0);
}
MarcusPlayback::~MarcusPlayback() = default;
void MarcusPlayback::SetFonts(std::shared_ptr<KauaiFonts::FontCatalog> fonts)
{
    marcus_caption_faces_clear(&impl->faces);
    marcus_caption_raster_clear(&impl->captionRasters);
    impl->fonts = std::move(fonts);
}
bool MarcusPlayback::ReadTiming(const PreviewPresentation &presentation, MMMovieTimingHolds &timing, Error &error,
                                const std::shared_ptr<KauaiFonts::FontCatalog> &fonts)
{
    Impl calculator;
    calculator.fonts = fonts;
    struct Context
    {
        const PreviewPresentation &presentation;
        Impl &calculator;
        Error &error;
        std::exception_ptr failure;
    } context{presentation, calculator, error, {}};
    // Supply the playback layout's actual font metrics and wrapping.
    const MMMovieTimingPresentation input{&context, presentation.transition,
        presentation.captions.size(), presentation.pauses.size(),
        [](void *p, std::size_t row, MMMovieTimingCaption *out) {
            const auto &caption = static_cast<Context *>(p)->presentation.captions[row];
            *out = {caption.firstFrame, caption.top, caption.bottom, caption.story};
        },
        [](void *p, std::size_t row, MMI32 *height) -> int {
            auto &h = *static_cast<Context *>(p);
            try
            {
                const auto source = CaptionSource(h.presentation.captions[row]);
                Impl::CaptionFontHost fonts{h.calculator, nullptr, {}};
                const auto services = fonts.Services();
                const auto status = marcus_caption_scene_measure(&source, &services, height);
                fonts.Check();
                return Impl::CaptionStatus(status, h.error);
            }
            catch (...) { h.failure = std::current_exception(); return 0; }
        },
        [](void *p, std::size_t row, MMMovieTimingPause *out) {
            const auto &pause = static_cast<Context *>(p)->presentation.pauses[row];
            *out = {pause.frame, pause.kind, pause.milliseconds};
        }};
    const int status = mm_movie_timing_holds_collect(&timing, &input);
    if (context.failure) std::rethrow_exception(context.failure);
    if (status == MM_SOUND_COLLECT_MEMORY) throw std::bad_alloc();
    return status == MM_SOUND_COLLECT_OK;
}

bool MarcusPlayback::ReadTiming(Preview &preview, Id scene, MarcusPlaybackScene &sound, MMMovieTimingHolds &timing,
                                Error &error, const std::shared_ptr<KauaiFonts::FontCatalog> &fonts)
{
    Impl calculator;
    calculator.fonts = fonts;
    MarcusPlaybackPresentation input{&sound, &calculator.captionScene};
    if (!preview.ReadTimingPresentation(scene, marcus_playback_presentation_output(&input), error))
        return false;
    marcus_playback_scene_order_cues(&sound, scene.type, scene.number);
    Impl::CaptionFontHost fontHost{calculator, nullptr, {}};
    const auto services = fontHost.Services();
    const int status = marcus_playback_presentation_timing(&input, &services, &timing);
    fontHost.Check();
    return Impl::CaptionStatus(status, error);
}

bool MarcusPlayback::RestoreAudio(Preview &preview, const std::vector<MarcusResumeSound> &sounds, Error &error)
{
    marcus_playback_audio_suspend(impl->audio.get(), 1);
    for (const auto &sound : sounds)
        if (!PlayAudio(impl->audio.get(), preview, sound.cue.Native(), mm_frame_sound_resume_samples(sound.seconds), error))
            return false;
    impl->clock.restore_music = 0;
    return true;
}
bool MarcusPlayback::Select(MM::Preview &preview, MM::Error &error, bool preserveAudio)
{
    if (preserveAudio)
    {
        marcus_playback_audio_cut(impl->audio.get());
        impl->ResetTiming();
        impl->clock.was_playing = 1;
    }
    else
        Stop();
    marcus_caption_scene_clear(&impl->captionScene);
    struct SceneOutput
    {
        MarcusPlaybackPresentation target;
        bool committed = false;
        ~SceneOutput()
        {
            if (!committed)
                marcus_playback_presentation_clear(&target);
        }
    } output{{&impl->scene, &impl->captionScene}};
    marcus_caption_raster_clear(&impl->captionRasters);
    if (!preview.ReadPresentation(marcus_playback_presentation_output(&output.target), error))
        return false;
    const int status = marcus_caption_scene_publish(&impl->captionScene, preview.Info().currentFrame);
    if (status)
        return Impl::CaptionStatus(status, error);
    output.committed = true;
    return true;
}
void MarcusPlayback::Stop()
{
    marcus_playback_audio_stop(impl->audio.get());
    impl->ResetTiming();
    impl->clock.was_playing = 0;
}
bool MarcusPlayback::Audition(Preview &preview, PreviewSoundCue cue, Error &error)
{
    Stop();
    return PlayAudio(impl->audio.get(), preview, cue.Native(), {}, error, true);
}
bool MarcusPlayback::Auditioning() const
{
    return marcus_playback_audio_auditioning(impl->audio.get()) != 0;
}
void MarcusPlayback::Statistics(MarcusAudioStats &out)
{
    MMMovieAudioStats stats;
    marcus_playback_audio_stats(impl->audio.get(), &stats);
    out = {stats.wave_sounds, stats.midi_sounds, stats.rendered_frames, stats.audible_frames};
}
void MarcusPlayback::Mute(bool muted)
{
    marcus_playback_audio_mute(impl->audio.get(), muted);
}
void MarcusPlayback::SetTrackMix(bool actorSound, long actor, long style, bool muted, bool solo)
{
    const auto lane = mm_movie_audio_lane(actorSound, static_cast<MMI32>(actor), static_cast<MMI32>(style));
    if (!marcus_playback_audio_set_mix(impl->audio.get(), lane, muted, solo))
        throw std::bad_alloc();
}
void MarcusPlayback::TrackMix(bool actorSound, long actor, long style, int &muted, int &solo, int &audible) const
{
    const auto lane = mm_movie_audio_lane(actorSound, static_cast<MMI32>(actor), static_cast<MMI32>(style));
    marcus_playback_audio_track_mix(impl->audio.get(), lane, &muted, &solo, &audible);
}
void MarcusPlayback::ResetMix()
{
    marcus_playback_audio_reset_mix(impl->audio.get());
}
bool MarcusPlayback::MixActive() const
{
    return marcus_playback_audio_mix_active(impl->audio.get()) != 0;
}
void MarcusPlayback::SetRate(double fps)
{
    mm_polled_playback_set_rate(&impl->clock, fps);
}
void MarcusPlayback::Seek(MM::Preview *preview)
{
    marcus_playback_audio_clear(impl->audio.get());
    impl->ResetTiming();
    if (preview) mm_caption_presentation_observe(&impl->captionScene.presentation, preview->Info().currentFrame);
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
    return mm_graphics_movie_frames_has_palette(&impl->movieFrames) != 0;
}
double MarcusPlayback::FrameSeconds() const
{
    return mm_polled_playback_frame_seconds(&impl->clock);
}
void MarcusPlayback::PresentMovieFrame(int scene, MM::PreviewImage &image)
{
    const auto surface = TransitionSurface(image);
    if (!mm_graphics_movie_frames_present(&impl->movieFrames, &impl->clock, scene, &surface,
                                          image.paletteIndices.data(), image.paletteIndices.size()))
        throw std::bad_alloc();
}
void MarcusPlayback::InvalidateMovieFrame()
{
    mm_graphics_movie_frames_invalidate(&impl->movieFrames);
}
void MarcusPlayback::ForgetMovieFrame()
{
    mm_graphics_movie_frames_forget(&impl->movieFrames);
}
void MarcusPlayback::CaptureMovieFrame(int scene, MMGraphicsMovieCapture &capture, const MM::Preview &preview)
{
    const auto palette = TransitionPalettes(preview);
    mm_graphics_movie_frames_capture(&impl->movieFrames, scene, &capture, &palette);
}
void MarcusPlayback::BeginTransition(int kind, MMGraphicsMovieCapture &capture, const MM::Preview &incoming)
{
    const auto palette = TransitionPalettes(incoming);
    if (mm_graphics_movie_frames_begin(&impl->movieFrames, &impl->clock, kind, &capture, &palette,
                                      []() -> MMU32 { return static_cast<MMU32>(SDL_GetTicks()); }) < 0)
        throw std::bad_alloc();
}
bool MarcusPlayback::Impl::EnterFrame(Preview &preview, MMI32 frame, int restoreMusic, double fps,
                                      MMPolledFrameWait &wait, Error &error)
{
    struct Context
    {
        Preview &preview;
        MarcusPlaybackAudio *audio;
        Error &error;
        std::exception_ptr failure;
    } context{preview, audio.get(), error, {}};
    const MarcusPlaybackFrameServices services{
        &context,
        [](void *p, void *out, MarcusPlaybackCueSink *append) -> int {
            auto &ctx = *static_cast<Context *>(p);
            try
            {
                if (!ctx.preview.VisitFrameSounds(out, append))
                    throw std::bad_alloc();
                return 1;
            }
            catch (...) { ctx.failure = std::current_exception(); return 0; }
        },
        [](void *p, const MMPreviewCue *cue, MMAudioCounter offset) -> int {
            auto &ctx = *static_cast<Context *>(p);
            try { return PlayAudio(ctx.audio, ctx.preview, *cue, offset, ctx.error); }
            catch (...) { ctx.failure = std::current_exception(); return 0; }
        }};
    const bool ok = marcus_playback_scene_enter(&scene, frame, restoreMusic, fps, &services, &wait) != 0;
    if (context.failure)
        std::rethrow_exception(context.failure);
    return ok;
}
bool MarcusPlayback::Update(MM::Preview &preview, bool playing, double elapsed, bool click, bool &advance,
                            MM::Error &error)
{
    const auto frame = preview.Info().currentFrame;
    mm_caption_presentation_observe(&impl->captionScene.presentation, frame);
    advance = false;
    marcus_playback_audio_update(impl->audio.get(), playing);

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
            try
            {
                Impl::CaptionFontHost fonts{ctx.host, nullptr, {}};
                const auto services = fonts.Services();
                const int status = marcus_caption_scene_measure_scroll(&ctx.host.captionScene, ctx.frame, &services,
                                                                      needsScroll);
                fonts.Check();
                return Impl::CaptionStatus(status, ctx.error);
            }
            catch (...) { ctx.failure = std::current_exception(); return false; }
        },
        [](void *data, int *pending) -> int {
            auto &ctx = *static_cast<Services *>(data);
            *pending = marcus_playback_audio_pending(ctx.host.audio.get());
            return 1;
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
        mm_caption_presentation_scroll(&impl->captionScene.presentation, frame);
    advance = update.advance != 0;
    return true;
}
int MarcusPlayback::CaptionAt(const MarcusCaptionList *list, int x, int y, const std::uint32_t *hiddenCaption) const
{
    return marcus_caption_catalog_hit(list, &impl->captionScene.presentation, x, y, hiddenCaption);
}

void MarcusPlayback::ClearCaptionDraft()
{
    marcus_caption_scene_clear_draft(&impl->captionScene);
}

bool MarcusPlayback::CaptionDraft(MMPreviewOwnedCaption &data, Error &error)
{
    Impl::CaptionFontHost fonts{*impl, nullptr, {}};
    const auto services = fonts.Services();
    const int status = marcus_caption_scene_draft_owned(&impl->captionScene, &data, &services);
    fonts.Check();
    return Impl::CaptionStatus(status, error);
}

const MarcusCaptionStop *MarcusPlayback::CaptionPositions(int &count, int &scroll) const
{
    return marcus_caption_scene_stops(&impl->captionScene, &count, &scroll);
}

bool MarcusPlayback::CaptionCaret(int offset)
{
    return marcus_caption_scene_caret(&impl->captionScene, offset) != 0;
}

bool MarcusPlayback::CaptionFontSample(const char *family, MarcusImage &image)
{
    struct SampleHost
    {
        Impl &playback;
        KauaiFonts::Mask mask;
        std::exception_ptr failure;
    } host{*impl, {}, {}};
    const int status = marcus_caption_sample(&impl->captionSample, family, &host,
        [](void *p, const MMCaptionStyle *style, const char *text, std::size_t length, MarcusCaptionSampleMask *out) -> int {
            auto &host = *static_cast<SampleHost *>(p);
            try
            {
                auto *face = host.playback.Face(*style);
                auto &mask = host.mask;
                if (!face || !face->Rasterize({text, length}, mask))
                    return false;
                *out = {mask.coverage.data(), mask.coverage.size(), mask.width, mask.height, mask.pitch};
                return true;
            }
            catch (...) { host.failure = std::current_exception(); return false; }
        }, &image);
    if (host.failure)
        std::rethrow_exception(host.failure);
    if (status == MARCUS_CAPTION_RASTER_MEMORY)
        throw std::bad_alloc();
    return status == MARCUS_CAPTION_RASTER_OK;
}

bool MarcusPlayback::Composite(MM::Preview &preview, bool authored, MM::PreviewImage &image,
                               MM::Error &error, const std::uint32_t *hiddenCaption, bool editDraft)
{
    static_assert(PreviewCaptionWidth == MM_CAPTION_WIDTH && PreviewCaptionHeight == MM_CAPTION_HEIGHT,
                  "Caption workspace dimensions must agree.");
    const auto camera = marcus_caption_projection(image.sceneCamera);
    const auto view = marcus_caption_projection(image.projection);
    auto covered = static_cast<MMU32>(image.coveredPixels);
    const MMCaptionOutput output{image.rgba.data(), image.rgba.size(), image.width, image.height,
                                 &camera, &view, image.depth.data(), image.depth.size(), &covered, authored};
    Impl::CaptionFontHost fonts{*impl, nullptr, {}};
    const auto services = fonts.Services();
    const MarcusCaptionScenePaint paint{&services, &impl->captionRasters, &preview,
        [](const void *p, MMU32 colour) -> MMU32 { return static_cast<const Preview *>(p)->PresentationColour(colour); }};
    const int status = marcus_caption_scene_paint(&impl->captionScene, preview.Info().currentFrame, hiddenCaption,
                                                  editDraft, &output, &paint);
    fonts.Check();
    if (!authored && !status)
        image.coveredPixels = covered;
    return Impl::CaptionStatus(status, error);
}
