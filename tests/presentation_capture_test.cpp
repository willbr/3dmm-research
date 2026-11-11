#include "../tools/presentation_capture.h"
#include <filesystem>
#include <iostream>
#define CHECK(x)                                                                                                       \
    do                                                                                                                 \
    {                                                                                                                  \
        if (!(x))                                                                                                      \
        {                                                                                                              \
            std::cerr << "Failed line " << __LINE__ << '\n';                                                           \
            return 1;                                                                                                  \
        }                                                                                                              \
    } while (0)
int main()
{
    const std::string directory = "presentation-capture-test-data";
    std::filesystem::create_directories(directory);
    PlaybackCapture::Writer writer;
    unsigned char pixel[] = {10, 20, 30, 255};
    writer.Begin(directory);
    writer.Add(pixel, 1, 1, 4, true, 0, 3, 100);
    writer.Add(pixel, 1, 1, 4, true, 0, 3, 200);
    pixel[0] = 11;
    writer.Add(pixel, 1, 1, 4, true, 0, 3, 300);
    writer.End();
    CHECK(!writer.failed && writer.events.size() == 3);
    CHECK(writer.events[0].path == writer.events[1].path);
    CHECK(writer.events[1].nanoseconds == 200);
    CHECK(writer.events[2].path != writer.events[0].path);
    std::ifstream stream(writer.events[0].path, std::ios::binary);
    std::string content((std::istreambuf_iterator<char>(stream)), {});
    CHECK(content == std::string("P6\n1 1\n255\n") + char(30) + char(20) + char(10));
    stream.close();
    for (const auto &e : writer.events)
        std::filesystem::remove(e.path);
    writer.Begin(directory + "/nonexistent/subfolder");
    writer.Add(pixel, 1, 1, 4, true, 0, 3, 400);
    writer.End();
    CHECK(writer.failed);
    writer.Begin(directory);
    writer.Add(pixel, 1, 1, 4, true, 0, 3, 500);
    writer.End();
    CHECK(!writer.failed && writer.events.size() == 1);
    std::filesystem::remove(writer.events[0].path);
    // Marcus supplies RGBA. Identical RGB bytes with a different shape must
    // retain their own PPM dimensions; repeated samples keep their timestamps.
    unsigned char rgba[] = {10, 20, 30, 255, 40, 50, 60, 255};
    writer.Begin(directory);
    writer.Add(rgba, 2, 1, 8, false, 0, 3, 100);
    writer.Add(rgba, 2, 1, 8, false, 0, 3, 200);
    writer.Add(rgba, 1, 2, 4, false, 0, 3, 300);
    writer.End();
    CHECK(!writer.failed && writer.events.size() == 3);
    CHECK(writer.events[0].path == writer.events[1].path && writer.events[1].nanoseconds == 200);
    CHECK(writer.events[2].path != writer.events[0].path);
    std::ifstream rgbaFile(writer.events[0].path, std::ios::binary);
    content.assign((std::istreambuf_iterator<char>(rgbaFile)), {});
    CHECK(content == std::string("P6\n2 1\n255\n") + char(10) + char(20) + char(30) + char(40) + char(50) + char(60));
    rgbaFile.close();
    for (const auto &event : writer.events)
        std::filesystem::remove(event.path);
    writer.eventLimit = 1;
    writer.Begin(directory);
    writer.Add(pixel, 1, 1, 4, false, 0, 3, 100);
    writer.Add(pixel, 1, 1, 4, false, 0, 3, 200);
    writer.End();
    CHECK(writer.failed && writer.events.size() == 1);
    std::filesystem::remove(writer.events[0].path);
    return 0;
}
