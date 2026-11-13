#include "playback.h"
#include "retail_scroll.h"
#include "caption_quad.h"
#include "sdl_audio.h"
#include "midi_synth.h"
#include "font_host.h"
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

namespace
{
using namespace ThreeMM;
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
class Audio
{
    std::mutex mutex;
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
        const auto lane = (std::uint64_t(cue.actorSound ? 1 : 0) << 63) |
                          (std::uint64_t(std::uint32_t(cue.actor)) << 16) | std::uint32_t(cue.style);
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
    int justify = 0, indent = 0, tab = 18, line = 256, extra = 0, after = 256, extraAfter = 0;
};
struct Glyph
{
    KauaiFonts::Mask mask;
    Style style;
    int x = 0, y = 0, advance = 0, height = 0, ascent = 0;
    std::uint16_t character = 0;
    std::uint16_t following = 0;
};
struct CaptionRaster
{
    KauaiFonts::Mask mask;
    int ascent = 0;
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
    std::map<FaceKey, std::unique_ptr<KauaiFonts::Face>> faces;
    std::size_t captionBytes = 0;
    std::map<std::tuple<std::string, int, int, std::uint32_t>, CaptionRaster> captionRasters;
    std::size_t rasterBytes = 0;
    Audio audio;
    RetailScrollPlayback scrollPlayback;
    bool entered = false, wasPlaying = false, scrolling = false, restoreMusic = true;
    int wait = 0, transition = 0;
    double frameTime = 0, waitTime = 0, transitionTime = 0, frameSeconds = 0;
    PreviewImage previous;
    void ResetTiming()
    {
        entered = false;
        restoreMusic = true;
        scrolling = false;
        scrollPlayback.Reset();
        wait = 0;
        frameTime = waitTime = transitionTime = 0;
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
        auto face = KauaiFonts::CreateStudioFace(text, style.pixels, (style.flags & 1) != 0, (style.flags & 2) != 0,
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
        const std::uint16_t units[] = {glyph.character == '\t' ? std::uint16_t(' ') : glyph.character,
                                       glyph.following};
        const auto key = std::make_tuple(style.font, style.pixels, style.flags,
                                        std::uint32_t(units[0]) | (std::uint32_t(units[1]) << 16));
        auto found = captionRasters.find(key);
        if (found != captionRasters.end())
            return &found->second;
        auto face = Face(style);
        KauaiFonts::Metrics metrics;
        KauaiFonts::Mask mask;
        const KauaiFonts::Text text{units, glyph.following ? 2u : 1u, KauaiFonts::Encoding::utf16};
        if (!face || !face->Measure(text, metrics) || !face->Rasterize(text, mask, true))
        {
            Fail(error, "Cannot render a high-resolution caption glyph.");
            return nullptr;
        }
        CaptionRaster raster;
        raster.mask = ReduceCaptionMask(mask);
        raster.ascent = metrics.ascent;
        const auto bytes = raster.mask.coverage.size() + raster.mask.background.size();
        // Share repeated characters and bound the cache across camera sizes.
        if (rasterBytes + bytes > 64u * 1024 * 1024)
        {
            captionRasters.clear();
            rasterBytes = 0;
        }
        rasterBytes += bytes;
        return &captionRasters.emplace(key, std::move(raster)).first->second;
    }
    bool Layout(CaptionImage &caption, Error &error)
    {
        if (caption.ready)
            return true;
        captionBytes -= caption.maskBytes;
        caption.maskBytes = 0;
        caption.glyphs.clear();
        auto &data = caption.data;
        if (data.right <= data.left || data.bottom <= data.top || data.right - std::int64_t(data.left) > 4096 ||
            data.bottom - std::int64_t(data.top) > 4096 || data.text.size() > 65536)
            return Fail(error, "Caption bounds or text exceed the playback layout limits.");
        Style style;
        style.font = data.font;
        style.pixels = data.fontSize;
        std::size_t property = 0, lineStart = 0;
        int x = 0, y = 0, lineHeight = 0, ascent = 0, descent = 0;
        // TextBoxBase reserves a five-pixel border; TextBoxGobject has the
        // rich-text view's nine-pixel horizontal inset on each side.
        int width = std::max(1, data.right - data.left - 28);
        bool firstLine = true;
        auto finishLine = [&](bool paragraph) {
            int used = 0;
            for (auto i = lineStart; i < caption.glyphs.size(); ++i)
                if (caption.glyphs[i].character != ' ' && caption.glyphs[i].character != '\t')
                    used = caption.glyphs[i].x + caption.glyphs[i].advance;
            const int shift = style.justify == 2 ? (width - used) / 2 : style.justify == 1 ? width - used : 0;
            for (auto i = lineStart; i < caption.glyphs.size(); ++i)
            {
                auto &glyph = caption.glyphs[i];
                glyph.x += std::max(0, shift);
                glyph.y = y + ascent - glyph.ascent + glyph.style.offset;
            }
            const auto natural = std::max({lineHeight, ascent + descent, style.pixels});
            auto height = natural * style.line / 256 + style.extra;
            if (paragraph)
                height = height * style.after / 256 + style.extraAfter;
            y += std::max(1, height);
            lineStart = caption.glyphs.size();
            x = 0;
            firstLine = paragraph;
            lineHeight = ascent = descent = 0;
        };
        for (std::size_t index = 0; index < data.text.size(); ++index)
        {
            while (property < data.properties.size() && data.properties[property].position <= index)
            {
                const auto &p = data.properties[property++];
                switch (p.kind)
                {
                case 1:
                    style.flags = p.value & 255;
                    style.offset = static_cast<std::int8_t>((p.value >> 8) & 255);
                    style.pixels = std::max(4, std::min(255, data.fontSize + static_cast<std::int16_t>(p.value >> 16)));
                    break;
                case 2:
                    style.font = p.font.empty() ? data.font : p.font;
                    break;
                case 3:
                    style.fore = 0x01000000 + p.value;
                    break;
                case 4:
                    style.back = 0xffffffff + p.value;
                    break;
                case 128:
                    style.justify = p.value >> 24;
                    style.indent = (p.value >> 16) & 255;
                    style.tab = std::max(1, 18 + static_cast<std::int16_t>(p.value));
                    break;
                case 129:
                    style.line = 256 + static_cast<std::int16_t>(p.value >> 16);
                    style.extra = static_cast<std::int16_t>(p.value);
                    break;
                case 130:
                    style.after = 256 + static_cast<std::int16_t>(p.value >> 16);
                    style.extraAfter = static_cast<std::int16_t>(p.value);
                    break;
                }
            }
            auto c = data.text[index];
            if (c == '\r' || c == '\n')
            {
                finishLine(true);
                if (c == '\r' && index + 1 < data.text.size() && data.text[index + 1] == '\n')
                    ++index;
                continue;
            }
            if (caption.glyphs.size() == lineStart && x == 0 &&
                (style.indent == 3 || (style.indent == 1 && firstLine) || (style.indent == 2 && !firstLine)))
                x = style.tab;
            auto face = Face(style);
            if (!face)
                return Fail(error, "Cannot open a font for movie captions. Install Arial or a system font.");
            Glyph glyph;
            glyph.style = style;
            glyph.character = c;
            KauaiFonts::Metrics metrics;
            const std::size_t units = c >= 0xd800 && c <= 0xdbff && index + 1 < data.text.size() &&
                                              data.text[index + 1] >= 0xdc00 && data.text[index + 1] <= 0xdfff
                                          ? 2
                                          : 1;
            auto text = KauaiFonts::Text{&data.text[index], units, KauaiFonts::Encoding::utf16};
            if (units == 2)
                glyph.following = data.text[index + 1];
            std::uint16_t space = ' ';
            if (c == '\t')
                text = {&space, 1, KauaiFonts::Encoding::utf16};
            if (!face->Measure(text, metrics) || !face->Rasterize(text, glyph.mask, true))
                return Fail(error, "Cannot render a movie caption glyph.");
            const auto bytes = glyph.mask.coverage.size() + glyph.mask.background.size();
            if (captionBytes + bytes > 128u * 1024 * 1024)
                return Fail(error, "Movie caption glyphs exceed the 128 MiB scene budget.");
            captionBytes += bytes;
            caption.maskBytes += bytes;
            glyph.advance = c == '\t' ? style.tab - x % style.tab : metrics.advance;
            glyph.height = metrics.cellHeight + metrics.externalLeading;
            glyph.ascent = metrics.ascent;
            // Wrap complete words where possible, falling back to character
            // wrapping only for a word wider than the caption itself.
            if (x && x + glyph.advance > width - (style.indent == 3 ? style.tab : 0))
            {
                std::size_t word = caption.glyphs.size();
                while (word > lineStart && caption.glyphs[word - 1].character != ' ' &&
                       caption.glyphs[word - 1].character != '\t')
                    --word;
                if (word > lineStart && word < caption.glyphs.size() && c != ' ')
                {
                    std::vector<Glyph> tail;
                    for (auto i = word; i < caption.glyphs.size(); ++i)
                        tail.push_back(std::move(caption.glyphs[i]));
                    caption.glyphs.resize(word);
                    finishLine(false);
                    for (auto &item : tail)
                    {
                        item.x = x;
                        x += item.advance;
                        ascent = std::max(ascent, item.ascent - item.style.offset);
                        descent = std::max(descent, item.height - item.ascent + item.style.offset);
                        lineHeight = std::max(lineHeight, item.height);
                        caption.glyphs.push_back(std::move(item));
                    }
                }
                else
                    finishLine(false);
            }
            glyph.x = x;
            x += glyph.advance;
            ascent = std::max(ascent, glyph.ascent - glyph.style.offset);
            descent = std::max(descent, glyph.height - glyph.ascent + glyph.style.offset);
            lineHeight = std::max(lineHeight, glyph.height);
            caption.glyphs.push_back(std::move(glyph));
            index += units - 1;
        }
        finishLine(false);
        caption.height = y;
        caption.ready = true;
        return true;
    }
};
MarcusPlayback::MarcusPlayback() : impl(new Impl)
{
}
MarcusPlayback::~MarcusPlayback() = default;
bool MarcusPlayback::ReadTiming(const PreviewPresentation &presentation, MarcusSoundTiming &timing, Error &error)
{
    timing = {};
    timing.transition = TransitionSeconds(presentation.transition);
    timing.pauses = presentation.pauses;
    // Use the playback layout itself, including font metrics and rich-text
    // wrapping, so restart offsets use the same scrolling distance and speed.
    Impl calculator;
    for (const auto &data : presentation.captions)
        if (!data.story)
        {
            CaptionImage caption;
            caption.data = data;
            if (!calculator.Layout(caption, error))
                return false;
            const int overflow = std::max(0, caption.height - (data.bottom - data.top - 10));
            timing.scrolling[data.firstFrame] = std::max(timing.scrolling[data.firstFrame], RetailScrollPlayback::NominalSeconds(overflow));
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
bool MarcusPlayback::Select(ThreeMM::Preview &preview, ThreeMM::Error &error, bool preserveAudio)
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
    return true;
}
void MarcusPlayback::Stop()
{
    impl->audio.Suspend(true);
    Seek();
    impl->wasPlaying = false;
}
void MarcusPlayback::Statistics(MarcusAudioStats &stats)
{
    impl->audio.Statistics(stats);
}
void MarcusPlayback::Mute(bool muted)
{
    impl->audio.Mute(muted);
}
void MarcusPlayback::Seek()
{
    impl->audio.Clear();
    impl->ResetTiming();
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
void MarcusPlayback::BeginTransition(int kind, const ThreeMM::PreviewImage &previous)
{
    if (kind < 1 || kind > 4 || previous.rgba.empty())
        return;
    impl->transition = kind;
    impl->wasPlaying = true;
    impl->transitionTime = 0;
    impl->previous = previous;
}
bool MarcusPlayback::Update(ThreeMM::Preview &preview, bool playing, double elapsed, bool click, bool &advance,
                            ThreeMM::Error &error)
{
    advance = false;
    impl->audio.Suspend(!playing);
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
                                          : static_cast<std::uint64_t>(std::max(0.0, (double(frame) - musicFrame) * 8000)),
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
        if (!caption.data.story && frame == caption.data.firstFrame)
        {
            if (!impl->Layout(caption, error))
                return false;
            const auto overflow = std::max(0, caption.height - (caption.data.bottom - caption.data.top - 10));
            needsScroll = needsScroll || caption.scroll < overflow;
        }
    const auto scrollUpdate = impl->scrollPlayback.Update(elapsed, needsScroll);
    if (scrollUpdate.step)
        for (auto &caption : impl->captions)
            if (!caption.data.story && frame == caption.data.firstFrame)
            {
                const auto overflow = std::max(0, caption.height - (caption.data.bottom - caption.data.top - 10));
                if (caption.scroll < overflow)
                    ++caption.scroll;
            }
    impl->scrolling = scrollUpdate.holding;
    if (impl->scrolling)
        return true;
    if (wasScrolling)
        elapsed = 0;
    if (impl->wait == 1)
    {
        if (!click)
            return true;
        impl->wait = 0;
        elapsed = 0;
    }
    else if (impl->wait == 2)
    {
        if (impl->audio.Pending())
            return true;
        impl->wait = 0;
        elapsed = 0;
    }
    else if (impl->wait == 3)
    {
        impl->waitTime -= elapsed;
        if (impl->waitTime > 0)
            return true;
        impl->wait = 0;
        elapsed = 0;
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
bool MarcusPlayback::Composite(ThreeMM::Preview &preview, bool authored, ThreeMM::PreviewImage &image,
                               ThreeMM::Error &error)
{
    const auto frame = preview.Info().currentFrame;
    if (!authored && !std::any_of(impl->captions.begin(), impl->captions.end(), [&](const CaptionImage &caption) {
            return frame >= caption.data.firstFrame && frame < caption.data.endFrame;
        }))
        return true;
    if (authored || image.sceneCamera.valid)
    {
        ThreeMM::PreviewImage layer;
        auto canvas = &image;
        if (!authored)
        {
            // Give the perspective caption plane enough texels for the work
            // viewport, including angled views and moderate close inspection.
            layer.width = std::min(4096u, std::max({480u, image.width * 2, image.height * 8 / 3}));
            layer.height = layer.width * 3 / 4;
            layer.rgba.assign(std::size_t(layer.width) * layer.height * 4, 0);
            canvas = &layer;
        }
        const float aspect = authored && image.sceneCamera.valid ? image.sceneCamera.aspect : 4.f / 3;
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
        // Caption coordinates are authored in the original 480x360 movie plane.
        for (auto &caption : impl->captions)
        {
            const auto &data = caption.data;
            if (frame < data.firstFrame || frame >= data.endFrame)
                continue;
            if (data.right <= 0 || data.left >= 480 || data.bottom <= 0 || data.top >= 360)
                continue;
            if (!impl->Layout(caption, error))
                return false;
            const float sx = width / 480.f, sy = height / 360.f;
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
                const float density = raster ? float(scale) : 1.f;
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
