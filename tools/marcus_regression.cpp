// Headless observation of the real Marcus player. No UI or synthetic clock.
#include "player.h"
#include "presentation_capture.h"
#include "marcus_capture_audio.h"
#include <SDL3/SDL.h>
#include <condition_variable>
#include <cstdlib>
#include <cstdint>
#include <deque>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>
#ifdef _WIN32
#include <windows.h>
#endif

namespace
{
std::uint64_t Now()
{
#ifdef _WIN32
    LARGE_INTEGER counter, frequency;
    QueryPerformanceCounter(&counter);
    QueryPerformanceFrequency(&frequency);
    return std::uint64_t(counter.QuadPart / frequency.QuadPart) * 1000000000 +
           std::uint64_t(counter.QuadPart % frequency.QuadPart) * 1000000000 / frequency.QuadPart;
#else
    return SDL_GetTicksNS();
#endif
}
std::string Quote(const std::string &text)
{
    std::ostringstream out;
    out << '"';
    for (unsigned char c : text)
        if (c == '"' || c == '\\')
            out << '\\' << c;
        else if (c < 32)
            out << "\\u" << std::hex << std::setw(4) << std::setfill('0') << unsigned(c);
        else
            out << c;
    return out.str() + '"';
}
void Check(MarcusPlayer *player, int success)
{
    if (!success)
        throw std::runtime_error(marcus_error(player));
}
// Absolute-seek capture is synchronous; playback uses the shared bounded
// writer, which reuses exact duplicate pixels without losing observations.
void SaveImage(const std::string &path, const MarcusImage &image)
{
    std::vector<char> pixels(std::size_t(image.width) * image.height * 3);
    for (int y = 0; y < image.height; ++y)
        for (int x = 0; x < image.width; ++x)
            std::copy_n(image.rgba + y * image.pitch + x * 4, 3,
                        pixels.data() + (std::size_t(y) * image.width + x) * 3);
    std::ofstream file(path, std::ios::binary);
    file << "P6\n" << image.width << ' ' << image.height << "\n255\n";
    file.write(pixels.data(), pixels.size());
    file.close();
    if (!file)
        throw std::runtime_error("Cannot write captured image: " + path);
}
std::string State(MarcusPlayer *player, bool playing)
{
    MarcusInfo info;
    Check(player, marcus_info(player, &info));
    std::ostringstream out;
    out << "{\"movie_scene_count\":" << info.scene_count << ",\"movie_scene\":" << info.scene
        << ",\"scene_frame\":" << info.frame << ",\"scene_first_frame\":" << info.first_frame
        << ",\"scene_last_frame\":" << info.last_frame << ",\"scene_camera\":" << info.camera
        << ",\"movie_playing\":" << (playing ? "true" : "false")
        << ",\"movie_sound_playing\":null,\"error_stack_depth\":0}";
    return out.str();
}
} // namespace

int main(int argc, char **argv)
{
    if (argc != 7)
    {
        std::cerr << "Usage: marcus-regression MOVIE RESOURCES CONTENT OUTPUT WIDTH HEIGHT\n";
        return 2;
    }
    const int width = std::atoi(argv[5]), height = std::atoi(argv[6]);
    if (width <= 0 || height <= 0 || width > 4096 || height > 4096)
        return 2;
    auto player = marcus_create();
    if (!player)
        return 2;
    try
    {
        Check(player, marcus_open(player, argv[1], argv[2], argv[3]));
        MarcusCaptureSilence silence;
        if (!silence.Open())
            throw std::runtime_error(std::string("Cannot prepare capture silence: ") + SDL_GetError());
        std::cout << "REGRESSION {\"ready\":true,\"capture_features\":{\"silence_carrier\":true,"
                     "\"duplicate_images_reused\":true}}"
                  << std::endl;
        // The reader owns no player state. All engine calls stay on this thread.
        std::mutex mutex;
        std::deque<std::string> commands;
        bool eof = false;
        std::thread reader([&]() {
            std::string line;
            while (std::getline(std::cin, line))
            {
                std::lock_guard<std::mutex> lock(mutex);
                commands.push_back(line);
            }
            std::lock_guard<std::mutex> lock(mutex);
            eof = true;
        });
        {
            PlaybackCapture::Writer images;
            images.eventLimit = 100000;
            bool playing = false, tracing = false, overflow = false;
            int previousScene = -1;
            long previousFrame = 0;
            std::uint64_t last = Now();
            std::vector<std::string> frames, presentations;
            std::string playbackError;
            for (;;)
            {
                std::string line;
                {
                    std::lock_guard<std::mutex> lock(mutex);
                    if (!commands.empty())
                    {
                        line = std::move(commands.front());
                        commands.pop_front();
                    }
                    else if (eof)
                        break;
                }
                if (!line.empty())
                {
                    try
                    {
                        std::istringstream input(line);
                        std::string command, result;
                        input >> command;
                        if (command == "state")
                            result = State(player, playing);
                        else if (command == "seek")
                        {
                            int scene;
                            long frame;
                            if (!(input >> scene >> frame) || scene < 1 || frame < 1)
                                throw std::runtime_error("Invalid scene/frame");
                            MarcusScene info;
                            Check(player, marcus_scene(player, scene - 1, &info));
                            if (frame > info.last_frame - info.first_frame + 1)
                                throw std::runtime_error("Frame outside scene");
                            playing = false;
                            Check(player, marcus_select_scene(player, scene - 1));
                            Check(player, marcus_seek(player, info.first_frame + frame - 1));
                            result = "{\"success\":true,\"state\":" + State(player, playing) + '}';
                        }
                        else if (command == "capture")
                        {
                            std::string path;
                            input >> std::quoted(path);
                            MarcusImage image;
                            Check(player, marcus_render(player, width, height, 1, 0, 0, 1, &image));
                            SaveImage(path, image);
                            result = "{\"captured\":true}";
                        }
                        else if (command == "begin")
                        {
                            images.Begin(argv[4]);
                            frames.clear();
                            presentations.clear();
                            overflow = false;
                            previousScene = -1;
                            playbackError.clear();
                            tracing = true;
                            result = "{\"armed\":true}";
                        }
                        else if (command == "end")
                        {
                            tracing = false;
                            images.End();
                            if (images.failed)
                                throw std::runtime_error("Presentation image capture failed");
                            std::ostringstream out;
                            out << "{\"overflow\":" << (overflow ? "true" : "false") << ",\"frames\":[";
                            for (std::size_t i = 0; i < frames.size(); ++i)
                                out << (i ? "," : "") << frames[i];
                            out << "],\"presentations\":[";
                            for (std::size_t i = 0; i < presentations.size(); ++i)
                                out << (i ? "," : "") << presentations[i];
                            out << "],\"sounds\":[],\"error\":" << Quote(playbackError) << '}';
                            result = out.str();
                        }
                        else if (command == "play")
                        {
                            playing = true;
                            last = Now();
                            result = "{\"queued\":true}";
                        }
                        else
                            throw std::runtime_error("Unknown command");
                        std::cout << "REGRESSION " << result << std::endl;
                    }
                    catch (const std::exception &error)
                    {
                        std::cout << "REGRESSION {\"error\":" << Quote(error.what()) << '}' << std::endl;
                    }
                }
                if (playing)
                {
                    try
                    {
                        const auto now = Now();
                        if (now - last < 10000000) // Observe animated presentation at up to 100 Hz.
                        {
                            SDL_Delay(1);
                            continue;
                        }
                        MarcusPlaybackState state;
                        Check(player, marcus_update(player, 1, 0, double(now - last) / 1000000, 0, &state));
                        last = now;
                        MarcusInfo info;
                        Check(player, marcus_info(player, &info));
                        const auto commit = Now();
                        const bool changed = info.scene != previousScene || info.frame != previousFrame;
                        if (tracing && (changed || state.changed))
                        {
                            if (presentations.size() >= 100000)
                                throw std::runtime_error("Presentation trace overflow");
                            std::ostringstream event;
                            event << "{\"scene\":" << info.scene << ",\"frame\":" << info.frame
                                  << ",\"first\":" << info.first_frame << ",\"last\":" << info.last_frame
                                  << ",\"monotonic_ns\":" << commit << '}';
                            if (changed)
                                frames.push_back(event.str());
                            MarcusImage image;
                            const auto renderStart = Now();
                            Check(player, marcus_render(player, width, height, 1, 0, 0, 1, &image));
                            const auto renderEnd = Now();
                            images.Add(image.rgba, image.width, image.height, image.pitch, false, info.scene,
                                       info.frame, renderEnd);
                            if (images.failed)
                                throw std::runtime_error("Presentation image capture failed or overflowed");
                            const auto &path = images.events.back().path;
                            std::ostringstream presentation;
                            presentation << "{\"scene\":" << info.scene << ",\"frame\":" << info.frame
                                         << ",\"monotonic_ns\":" << renderEnd
                                         << ",\"render_ns\":" << renderEnd - renderStart
                                         << ",\"capture_ns\":" << Now() - renderEnd << ",\"wait\":" << state.wait
                                         << ",\"scrolling\":" << state.scrolling
                                         << ",\"transition\":" << state.transition << ",\"path\":" << Quote(path)
                                         << '}';
                            presentations.push_back(presentation.str());
                        }
                        previousScene = info.scene;
                        previousFrame = info.frame;
                        playing = state.playing != 0;
                    }
                    catch (const std::exception &error)
                    {
                        playbackError = error.what();
                        overflow = true;
                        playing = false;
                        marcus_stop(player);
                    }
                }
                SDL_Delay(1);
            }
        } // Finish queued images before exiting.
        reader.join();
    }
    catch (const std::exception &error)
    {
        std::cerr << error.what() << '\n';
        marcus_destroy(player);
        return 1;
    }
    marcus_destroy(player);
    return 0;
}
