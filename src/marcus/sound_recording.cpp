// Narrow C bridge to the shared Studio microphone recorder and private files.
#include "sound_recording.h"
#include "scene_edit.h"
#include "sdl_recording.h"
#include "sdl_audio.h"
#include "waveform.h"
#include <SDL3/SDL.h>
#include <exception>
#include <string>

struct MarcusSoundRecording
{
    std::unique_ptr<KauaiAudio::Recorder> recorder;
    std::shared_ptr<MarcusMovieFile> file;
    std::string path, error;
};
namespace {
int Fail(MarcusSoundRecording *r, const char *message)
{ if (r) r->error = message; return 0; }
int Finish(MarcusSoundRecording *r)
{
    if (!r) return 0;
    if (!r->recorder) return !r->path.empty();
    if (!r->recorder->Stop())
    { r->recorder.reset(); return Fail(r, "Microphone recording failed. Check your input device and try again."); }
    if (!r->recorder->HaveSound())
    { r->recorder.reset(); return Fail(r, "No audio was captured. Record for longer and check your microphone."); }
    try
    {
        auto file = MarcusCreateEventFile(); file->path = file->directory / "Recording.wav";
        const auto path = file->path.u8string(); const auto &wave = r->recorder->Wave();
        if (!SDL_SaveFile(path.c_str(), wave.data(), wave.size()))
        { r->recorder.reset(); return Fail(r, SDL_GetError()); }
        r->recorder.reset(); r->file = std::move(file); r->path = path; r->error.clear(); return 1;
    }
    catch (const std::exception &e) { r->recorder.reset(); return Fail(r, e.what()); }
}
}
extern "C" {
MarcusSoundRecording *sound_recording_create()
{ try { return new MarcusSoundRecording; } catch (...) { return nullptr; } }
void sound_recording_destroy(MarcusSoundRecording *r) { delete r; }
int sound_recording_start(MarcusSoundRecording *r, unsigned long maxMs)
{
    if (!r || r->recorder || !maxMs || maxMs > 180000) return Fail(r, "Choose a recording length between 1 ms and 3 minutes.");
    try
    {
        SDL_ClearError(); auto next = std::make_unique<KauaiAudio::Recorder>();
        if (!next->Open(48000, 1, 16, static_cast<std::uint32_t>(maxMs)) || !next->Start())
            return Fail(r, *SDL_GetError() ? SDL_GetError() : "Cannot start the default microphone. Check your input device and microphone access.");
        r->recorder = std::move(next); r->file.reset(); r->path.clear(); r->error.clear(); return 1;
    }
    catch (const std::exception &e) { return Fail(r, e.what()); }
}
int sound_recording_poll(MarcusSoundRecording *r)
{
    if (!r) return -1;
    if (!r->recorder) return r->error.empty() ? 0 : -1;
    try { if (r->recorder->Recording()) return 1; return Finish(r) ? 0 : -1; }
    catch (const std::exception &e) { r->recorder.reset(); Fail(r, e.what()); return -1; }
}
int sound_recording_stop(MarcusSoundRecording *r) { return Finish(r); }
const char *sound_recording_error(const MarcusSoundRecording *r) { return r ? r->error.c_str() : "Cannot allocate the sound recorder."; }
const char *sound_recording_path(const MarcusSoundRecording *r) { return r ? r->path.c_str() : ""; }
int sound_recording_waveform(MarcusSoundRecording *r, int count, MarcusWaveformColumn *columns, double *seconds)
{
    if (!r || r->path.empty() || !columns || !seconds || count < 1 || count > 4096) return 0;
    try
    {
        std::size_t bytes; auto data = SDL_LoadFile(r->path.c_str(), &bytes);
        if (!data) return Fail(r, SDL_GetError());
        std::unique_ptr<void, decltype(&SDL_free)> owned(data, SDL_free);
        KauaiAudio::Sample sample;
        if (!KauaiAudio::DecodeWave(data, bytes, sample)) return Fail(r, "Cannot read the recorded audio.");
        MarcusWaveformEnvelope peaks;
        peaks.Build(mm_audio_sample_data(sample.get()), mm_audio_sample_frames(sample.get()));
        *seconds = peaks.Seconds(); peaks.Read(0, *seconds / count, count, columns); return 1;
    }
    catch (const std::exception &e) { return Fail(r, e.what()); }
}
}
