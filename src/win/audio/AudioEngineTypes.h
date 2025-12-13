#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace winaudio {

enum class AudioBackendType { WasapiShared, Asio };

/// Audio render callback. Caller fills interleaved float buffer.
using RenderCallback = std::function<void(float* output, std::size_t frames)>;

struct AudioDeviceInfo {
    AudioBackendType backend = AudioBackendType::WasapiShared;
    std::wstring id;    // WASAPI endpoint ID or ASIO CLSID string.
    std::wstring name;  // Display name.
};

struct AudioEngineConfig {
    AudioBackendType backend = AudioBackendType::WasapiShared;
    // Empty = system default output device (WASAPI). For ASIO, must be a CLSID string.
    std::wstring deviceId;
    // Optional platform handle for backends that require a host window handle (e.g. ASIO).
    std::uintptr_t sysHandle = 0;
    // Optional request. For WASAPI shared mode this is typically ignored or
    // coerced to the system mix format.
    uint32_t sampleRate = 0;
    uint16_t channels = 1;
    uint32_t bufferFrames = 512;
};

}  // namespace winaudio
