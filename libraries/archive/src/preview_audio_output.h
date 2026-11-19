#ifndef MM_PREVIEW_AUDIO_OUTPUT_H
#define MM_PREVIEW_AUDIO_OUTPUT_H

#include <exception>
#include <new>
#include "mm/preview.h"
#include "mm/preview_media.h"

namespace MM
{
namespace Internal
{
struct AudioOutput
{
    PreviewAudio &output;
    std::exception_ptr exception;
    template <class F> int Guard(F f)
    {
        try
        {
            return f();
        }
        catch (const std::bad_alloc &)
        {
            exception = std::current_exception();
            return -1;
        }
        catch (...)
        {
            exception = std::current_exception();
            return -2;
        }
    }
    MMPreviewAudioServices Services()
    {
        MMPreviewAudioServices s = {};
        s.context = this;
        s.reset = [](void *v) { static_cast<AudioOutput *>(v)->output = {}; };
        s.silent = [](void *v) { static_cast<AudioOutput *>(v)->output.silent = true; };
        s.publish = [](void *v, MMU32 type, MMU32 number, int midi) {
            auto &o = static_cast<AudioOutput *>(v)->output;
            o.asset = {type, number};
            o.midi = midi != 0;
        };
        s.wave_buffer = [](void *v, MMU32 n, unsigned char **out) -> int {
            auto &h = *static_cast<AudioOutput *>(v);
            return h.Guard([&] {
                h.output.wave.resize(n);
                *out = h.output.wave.data();
                return 1;
            });
        };
        s.event = [](void *v, MMU32 timestamp, MMU32 message) -> int {
            auto &h = *static_cast<AudioOutput *>(v);
            return h.Guard([&] {
                h.output.events.push_back({timestamp, message});
                return 1;
            });
        };
        return s;
    }
};
} // namespace Internal
} // namespace MM
#endif
