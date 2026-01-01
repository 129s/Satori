#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include <windows.h>
#include <mmsystem.h>

namespace winmidi {

struct MidiInDevice {
    unsigned int id = 0;
    std::wstring name;
};

std::vector<MidiInDevice> EnumerateMidiInDevices();

class WinMidiInput {
public:
    using MessageCallback = std::function<void(std::uint32_t message)>;

    WinMidiInput();
    ~WinMidiInput();

    WinMidiInput(const WinMidiInput&) = delete;
    WinMidiInput& operator=(const WinMidiInput&) = delete;

    bool open(unsigned int deviceId, MessageCallback callback, std::string& error);
    void close();
    bool isOpen() const;
    unsigned int deviceId() const { return deviceId_; }

private:
    friend void CALLBACK MidiInProc(HMIDIIN, UINT, DWORD_PTR, DWORD_PTR, DWORD_PTR);

    void* handle_ = nullptr;
    unsigned int deviceId_ = 0;
    MessageCallback callback_;
};

}  // namespace winmidi
