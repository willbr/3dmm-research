// Licensed under the MIT License.
// Process-scoped WASAPI loopback. No microphone or system-wide loopback input.
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <audioclient.h>
#include <audioclientactivationparams.h>
#include <mmdeviceapi.h>
#include <wrl.h>
#include <algorithm>
#include <cstdint>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>
using Microsoft::WRL::ComPtr;
static void Check(HRESULT result, const char *operation)
{
    if (FAILED(result))
        throw std::runtime_error(std::string(operation) + ": HRESULT " + std::to_string(std::uint32_t(result)));
}
struct Handle
{
    HANDLE value;
    explicit Handle(HANDLE value) : value(value)
    {
        if (!value)
            throw std::runtime_error("Cannot create/open capture handle");
    }
    ~Handle()
    {
        CloseHandle(value);
    }
};
class Activation
    : public Microsoft::WRL::RuntimeClass<Microsoft::WRL::RuntimeClassFlags<Microsoft::WRL::ClassicCom>,
                                          Microsoft::WRL::FtmBase, IActivateAudioInterfaceCompletionHandler>
{
  public:
    Handle completed{CreateEventW(nullptr, TRUE, FALSE, nullptr)};
    HRESULT result = E_PENDING;
    ComPtr<IAudioClient> client;
    STDMETHOD(ActivateCompleted)(IActivateAudioInterfaceAsyncOperation *operation) override
    {
        ComPtr<IUnknown> object;
        HRESULT activated = E_FAIL;
        result = operation->GetActivateResult(&activated, &object);
        if (SUCCEEDED(result))
            result = activated;
        if (SUCCEEDED(result))
            result = object.As(&client);
        SetEvent(completed.value);
        return S_OK;
    }
};
static std::uint64_t Created(HANDLE process)
{
    FILETIME created, exited, kernel, user;
    if (!GetProcessTimes(process, &created, &exited, &kernel, &user))
        throw std::runtime_error("Cannot verify capture target identity");
    return (std::uint64_t(created.dwHighDateTime) << 32) | created.dwLowDateTime;
}
static void Integer(std::ostream &stream, std::uint32_t value, unsigned bytes)
{
    for (unsigned i = 0; i < bytes; ++i)
        stream.put(char(value >> (8 * i)));
}
static void Header(std::ostream &stream, std::uint32_t bytes)
{
    stream.seekp(0);
    stream.write("RIFF", 4);
    Integer(stream, bytes + 36, 4);
    stream.write("WAVEfmt ", 8);
    Integer(stream, 16, 4);
    Integer(stream, 1, 2);
    Integer(stream, 2, 2);
    Integer(stream, 48000, 4);
    Integer(stream, 48000 * 4, 4);
    Integer(stream, 4, 2);
    Integer(stream, 16, 2);
    stream.write("data", 4);
    Integer(stream, bytes, 4);
}
int wmain(int argc, wchar_t **argv)
{
    bool initialized = false;
    try
    {
        if (argc != 6)
            throw std::runtime_error("Usage: oracle-audio-capture PID CREATED WAV STOP_EVENT MAX_SECONDS");
        const auto pid = std::stoul(argv[1]);
        const auto created = std::stoull(argv[2]);
        const auto seconds = std::stoul(argv[5]);
        if (!seconds || seconds > 3600)
            throw std::runtime_error("Capture limit must be 1..3600 seconds");
        Handle process{OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | SYNCHRONIZE, FALSE, pid)};
        if (Created(process.value) != created)
            throw std::runtime_error("Capture target PID was reused");
        Handle stop{OpenEventW(SYNCHRONIZE, FALSE, argv[4])};
        const std::filesystem::path path(argv[3]);
        if (std::filesystem::exists(path) || std::filesystem::exists(path.wstring() + L".packets.jsonl"))
            throw std::runtime_error("Capture output already exists");
        Check(CoInitializeEx(nullptr, COINIT_MULTITHREADED), "Initialize COM");
        initialized = true;
        auto activation = Microsoft::WRL::Make<Activation>();
        AUDIOCLIENT_ACTIVATION_PARAMS params{};
        params.ActivationType = AUDIOCLIENT_ACTIVATION_TYPE_PROCESS_LOOPBACK;
        params.ProcessLoopbackParams.TargetProcessId = pid;
        params.ProcessLoopbackParams.ProcessLoopbackMode = PROCESS_LOOPBACK_MODE_INCLUDE_TARGET_PROCESS_TREE;
        PROPVARIANT variant{};
        variant.vt = VT_BLOB;
        variant.blob.cbSize = sizeof(params);
        variant.blob.pBlobData = reinterpret_cast<BYTE *>(&params);
        ComPtr<IActivateAudioInterfaceAsyncOperation> operation;
        Check(ActivateAudioInterfaceAsync(VIRTUAL_AUDIO_DEVICE_PROCESS_LOOPBACK, __uuidof(IAudioClient), &variant,
                                          activation.Get(), &operation),
              "Activate process loopback");
        if (WaitForSingleObject(activation->completed.value, 10000) != WAIT_OBJECT_0)
            throw std::runtime_error("Process loopback activation timed out");
        Check(activation->result, "Complete process loopback activation");
        auto client = activation->client;
        WAVEFORMATEX format{};
        // Capture the mixer in float, then quantize deterministically ourselves.
        // Windows' float-to-PCM conversion adds random one-LSB dither to silence.
        format.wFormatTag = WAVE_FORMAT_IEEE_FLOAT;
        format.nChannels = 2;
        format.nSamplesPerSec = 48000;
        format.wBitsPerSample = 32;
        format.nBlockAlign = 8;
        format.nAvgBytesPerSec = 48000 * 8;
        Check(client->Initialize(AUDCLNT_SHAREMODE_SHARED,
                                 AUDCLNT_STREAMFLAGS_LOOPBACK | AUDCLNT_STREAMFLAGS_EVENTCALLBACK |
                                     AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM,
                                 0, 0, &format, nullptr),
              "Initialize process capture");
        Handle ready{CreateEventW(nullptr, FALSE, FALSE, nullptr)};
        Check(client->SetEventHandle(ready.value), "Set capture event");
        ComPtr<IAudioCaptureClient> capture;
        Check(client->GetService(IID_PPV_ARGS(&capture)), "Get capture service");
        std::ofstream wave(path, std::ios::binary);
        std::ofstream packets(path.wstring() + L".packets.jsonl", std::ios::binary);
        if (!wave || !packets)
            throw std::runtime_error("Cannot open capture outputs");
        Header(wave, 0);
        LARGE_INTEGER frequency, start;
        QueryPerformanceFrequency(&frequency);
        QueryPerformanceCounter(&start);
        Check(client->Start(), "Start process capture");
        std::cout
            << "{\"ready\":true,\"pid\":" << pid << ",\"created\":" << created
            << ",\"rate\":48000,\"channels\":2,\"bits\":16,\"source_bits\":32,\"source_format\":\"float\",\"qpc\":"
            << start.QuadPart << ",\"qpc_frequency\":" << frequency.QuadPart << "}" << std::endl;
        std::uint32_t bytes = 0;
        unsigned discontinuities = 0, packetCount = 0, timestampsMissing = 0;
        std::uint64_t quietFrames = 0, lastMeter = 0;
        bool limited = false, exited = false;
        const auto wallStart = GetTickCount64();
        HANDLE events[] = {stop.value, ready.value, process.value};
        while (true)
        {
            const auto wait = WaitForMultipleObjects(3, events, FALSE, 1000);
            if (wait == WAIT_FAILED)
                throw std::runtime_error("Capture wait failed");
            if (wait == WAIT_OBJECT_0 || wait == WAIT_OBJECT_0 + 2)
            {
                exited = wait == WAIT_OBJECT_0 + 2;
                break;
            }
            if (GetTickCount64() - wallStart >= std::uint64_t(seconds) * 1000)
            {
                limited = true;
                break;
            }
            UINT32 count = 0;
            Check(capture->GetNextPacketSize(&count), "Query audio packet");
            while (count)
            {
                BYTE *data;
                DWORD flags;
                UINT32 frames;
                UINT64 position, timestamp;
                Check(capture->GetBuffer(&data, &frames, &flags, &position, &timestamp), "Read audio packet");
                if (frames > (std::uint64_t(seconds) * 48000 * 4 - bytes) / 4)
                {
                    capture->ReleaseBuffer(frames);
                    limited = true;
                    break;
                }
                packets << "{\"sample_offset\":" << bytes / 4 << ",\"frames\":" << frames << ",\"flags\":" << flags
                        << ",\"device_position\":" << position << ",\"qpc_100ns\":" << timestamp << "}\n";
                if (flags & AUDCLNT_BUFFERFLAGS_DATA_DISCONTINUITY)
                    ++discontinuities;
                if (flags & AUDCLNT_BUFFERFLAGS_TIMESTAMP_ERROR)
                    ++timestampsMissing;
                int peak = 0;
                if (flags & AUDCLNT_BUFFERFLAGS_SILENT)
                {
                    char silence[4096]{};
                    for (std::size_t remaining = std::size_t(frames) * 4; remaining;)
                    {
                        const auto amount = std::min(remaining, sizeof(silence));
                        wave.write(silence, amount);
                        remaining -= amount;
                    }
                }
                else
                {
                    const auto source = reinterpret_cast<const float *>(data);
                    std::vector<std::int16_t> samples(std::size_t(frames) * 2);
                    for (std::size_t i = 0; i < samples.size(); ++i)
                    {
                        if (!std::isfinite(source[i]))
                            throw std::runtime_error("Non-finite mixer sample");
                        const auto value = std::max(-1.0, std::min(1.0, double(source[i])));
                        const auto integer = std::max(-32768L, std::min(32767L, std::lround(value * 32768)));
                        samples[i] = static_cast<std::int16_t>(integer);
                        peak = std::max(peak, int(std::abs(integer)));
                    }
                    wave.write(reinterpret_cast<const char *>(samples.data()), std::size_t(frames) * 4);
                }
                quietFrames =
                    peak || (flags & (AUDCLNT_BUFFERFLAGS_DATA_DISCONTINUITY | AUDCLNT_BUFFERFLAGS_TIMESTAMP_ERROR))
                        ? 0
                        : quietFrames + frames;
                bytes += frames * 4;
                ++packetCount;
                Check(capture->ReleaseBuffer(frames), "Release audio packet");
                const auto meterTime = GetTickCount64();
                if (meterTime - lastMeter >= 100)
                {
                    std::cout << "{\"meter\":true,\"quiet_frames\":" << quietFrames << ",\"peak\":" << peak << "}"
                              << std::endl;
                    lastMeter = meterTime;
                }
                if (!wave || !packets)
                    throw std::runtime_error("Audio capture write failed");
                Check(capture->GetNextPacketSize(&count), "Query next audio packet");
            }
            if (limited)
                break;
        }
        Check(client->Stop(), "Stop process capture");
        Header(wave, bytes);
        wave.close();
        packets.close();
        if (!wave || !packets)
            throw std::runtime_error("Cannot finish audio capture");
        std::cout << "{\"complete\":true,\"frames\":" << bytes / 4 << ",\"packets\":" << packetCount
                  << ",\"discontinuities\":" << discontinuities << ",\"timestamp_errors\":" << timestampsMissing
                  << ",\"limit_reached\":" << (limited ? "true" : "false")
                  << ",\"target_exited\":" << (exited ? "true" : "false") << "}" << std::endl;
        client.Reset();
        operation.Reset();
        activation.Reset();
        capture.Reset();
        CoUninitialize();
        // The controller checks packet flags in the playback interval. WASAPI
        // may flag its initial pre-roll packet as discontinuous.
        return limited || exited || !packetCount ? 2 : 0;
    }
    catch (const std::exception &error)
    {
        std::cerr << error.what() << std::endl;
        if (initialized)
            CoUninitialize();
        return 1;
    }
}
