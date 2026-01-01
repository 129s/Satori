#include "win/midi/WinMidiInput.h"

#include <algorithm>
#include <sstream>

namespace winmidi {

namespace {

std::string MidiErrorText(MMRESULT result) {
    char buffer[256] = {};
    if (midiInGetErrorTextA(result, buffer, static_cast<UINT>(sizeof(buffer))) ==
        MMSYSERR_NOERROR) {
        return std::string(buffer);
    }
    std::ostringstream ss;
    ss << "MIDI error " << static_cast<unsigned int>(result);
    return ss.str();
}

}  // namespace

void CALLBACK MidiInProc(HMIDIIN, UINT msg, DWORD_PTR instance, DWORD_PTR param1,
                         DWORD_PTR) {
    auto* self = reinterpret_cast<WinMidiInput*>(instance);
    if (!self) {
        return;
    }
    if (msg != MIM_DATA) {
        return;
    }
    const auto message = static_cast<std::uint32_t>(param1);
    if (self->isOpen() && self->callback_) {
        self->callback_(message);
    }
}

std::vector<MidiInDevice> EnumerateMidiInDevices() {
    std::vector<MidiInDevice> devices;
    const UINT count = midiInGetNumDevs();
    devices.reserve(count);
    for (UINT i = 0; i < count; ++i) {
        MIDIINCAPSW caps{};
        if (midiInGetDevCapsW(i, &caps, sizeof(caps)) != MMSYSERR_NOERROR) {
            continue;
        }
        MidiInDevice dev;
        dev.id = i;
        dev.name = caps.szPname;
        devices.push_back(std::move(dev));
    }
    return devices;
}

WinMidiInput::WinMidiInput() = default;

WinMidiInput::~WinMidiInput() {
    close();
}

bool WinMidiInput::open(unsigned int deviceId, MessageCallback callback,
                        std::string& error) {
    close();
    callback_ = std::move(callback);
    deviceId_ = deviceId;

    HMIDIIN handle = nullptr;
    const MMRESULT result =
        midiInOpen(&handle, deviceId, reinterpret_cast<DWORD_PTR>(MidiInProc),
                   reinterpret_cast<DWORD_PTR>(this), CALLBACK_FUNCTION);
    if (result != MMSYSERR_NOERROR) {
        error = MidiErrorText(result);
        callback_ = {};
        return false;
    }
    handle_ = handle;

    const MMRESULT startResult = midiInStart(handle);
    if (startResult != MMSYSERR_NOERROR) {
        error = MidiErrorText(startResult);
        close();
        return false;
    }

    return true;
}

void WinMidiInput::close() {
    if (!handle_) {
        callback_ = {};
        return;
    }
    HMIDIIN handle = reinterpret_cast<HMIDIIN>(handle_);
    (void)midiInStop(handle);
    (void)midiInReset(handle);
    (void)midiInClose(handle);
    handle_ = nullptr;
    callback_ = {};
}

bool WinMidiInput::isOpen() const {
    return handle_ != nullptr;
}

}  // namespace winmidi
