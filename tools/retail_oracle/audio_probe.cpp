// Licensed under the MIT License. Owned-child calibration for process loopback.
#define NOMINMAX
#include <windows.h>
#include <mmsystem.h>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>
int main(int argc, char **argv)
{
    try
    {
        if (argc != 3)
            throw std::runtime_error("Usage: oracle-audio-probe wave|midi FREQUENCY");
        std::cout << "ready" << std::endl;
        char command;
        std::cin.get(command);
        if (command != 'p')
            return 1;
        if (std::string(argv[1]) == "midi")
        {
            HMIDIOUT device = nullptr;
            if (midiOutOpen(&device, MIDI_MAPPER, 0, 0, CALLBACK_NULL))
                throw std::runtime_error("MIDI mapper unavailable");
            midiOutShortMsg(device, 0x000000c0); // Acoustic piano.
            midiOutShortMsg(device, 0x00304590); // Note 69, velocity 48.
            Sleep(1000);
            midiOutShortMsg(device, 0x00004580);
            Sleep(500);
            midiOutReset(device);
            midiOutClose(device);
        }
        else
        {
            const double frequency = std::stod(argv[2]);
            if (frequency < 100 || frequency > 5000)
                throw std::runtime_error("Invalid calibration frequency");
            std::vector<std::int16_t> samples(48000 * 2 * 2);
            for (unsigned i = 0; i < samples.size() / 2; ++i)
                samples[2 * i] = samples[2 * i + 1] =
                    std::int16_t(1024 * std::sin(i * frequency * 6.283185307179586 / 48000));
            WAVEFORMATEX format{};
            format.wFormatTag = WAVE_FORMAT_PCM;
            format.nChannels = 2;
            format.nSamplesPerSec = 48000;
            format.wBitsPerSample = 16;
            format.nBlockAlign = 4;
            format.nAvgBytesPerSec = 48000 * 4;
            HANDLE done = CreateEventW(nullptr, FALSE, FALSE, nullptr);
            HWAVEOUT device;
            if (waveOutOpen(&device, WAVE_MAPPER, &format, reinterpret_cast<DWORD_PTR>(done), 0, CALLBACK_EVENT))
                throw std::runtime_error("Wave output unavailable");
            WAVEHDR header{};
            header.lpData = reinterpret_cast<char *>(samples.data());
            header.dwBufferLength = DWORD(samples.size() * 2);
            if (waveOutPrepareHeader(device, &header, sizeof(header)) || waveOutWrite(device, &header, sizeof(header)))
                throw std::runtime_error("Cannot play calibration tone");
            const auto start = GetTickCount64();
            while (!(header.dwFlags & WHDR_DONE) && GetTickCount64() - start < 10000)
                WaitForSingleObject(done, 100);
            if (!(header.dwFlags & WHDR_DONE))
                throw std::runtime_error("Calibration playback timed out");
            waveOutUnprepareHeader(device, &header, sizeof(header));
            waveOutClose(device);
            CloseHandle(done);
        }
        std::cout << "played" << std::endl;
        std::cin.get(command); // Remain alive until the capture has finished.
        return 0;
    }
    catch (const std::exception &error)
    {
        std::cerr << error.what() << std::endl;
        return 1;
    }
}
