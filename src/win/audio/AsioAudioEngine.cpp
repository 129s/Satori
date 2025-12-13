#include "win/audio/AsioAudioEngine.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <cmath>
#include <sstream>
#include <utility>

#include <windows.h>
#include <objbase.h>
#include <unknwn.h>

#ifndef interface
#define interface struct
#endif

#if defined(SATORI_ENABLE_ASIO) && (SATORI_ENABLE_ASIO != 0) && defined(SATORI_HAS_ASIO_SDK) && (SATORI_HAS_ASIO_SDK != 0)
#include "asiosys.h"
#include "asio.h"
#include "iasiodrv.h"
#endif

namespace winaudio {

namespace {

std::wstring ReadRegStringValue(HKEY key, const wchar_t* name) {
    DWORD type = 0;
    DWORD bytes = 0;
    if (RegQueryValueExW(key, name, nullptr, &type, nullptr, &bytes) != ERROR_SUCCESS) {
        return L"";
    }
    if (type != REG_SZ && type != REG_EXPAND_SZ) {
        return L"";
    }
    std::wstring value;
    value.resize(bytes / sizeof(wchar_t));
    if (value.empty()) {
        return L"";
    }
    if (RegQueryValueExW(key, name, nullptr, nullptr,
                         reinterpret_cast<LPBYTE>(value.data()),
                         &bytes) != ERROR_SUCCESS) {
        return L"";
    }
    while (!value.empty() && (value.back() == L'\0' || value.back() == L'\n' || value.back() == L'\r')) {
        value.pop_back();
    }
    return value;
}

void EnumerateAsioRegistryRoot(HKEY root, const wchar_t* path, std::vector<AudioDeviceInfo>& out) {
    HKEY base = nullptr;
    if (RegOpenKeyExW(root, path, 0, KEY_READ, &base) != ERROR_SUCCESS) {
        return;
    }

    DWORD index = 0;
    wchar_t subKeyName[256];
    DWORD subKeyLen = static_cast<DWORD>(std::size(subKeyName));
    while (true) {
        subKeyLen = static_cast<DWORD>(std::size(subKeyName));
        const LONG res = RegEnumKeyExW(base, index, subKeyName, &subKeyLen,
                                       nullptr, nullptr, nullptr, nullptr);
        if (res != ERROR_SUCCESS) {
            break;
        }
        ++index;

        HKEY sub = nullptr;
        if (RegOpenKeyExW(base, subKeyName, 0, KEY_READ, &sub) != ERROR_SUCCESS) {
            continue;
        }

        std::wstring clsid = ReadRegStringValue(sub, L"CLSID");
        std::wstring desc = ReadRegStringValue(sub, L"Description");
        RegCloseKey(sub);

        if (clsid.empty()) {
            continue;
        }
        if (desc.empty()) {
            desc = subKeyName;
        }

        AudioDeviceInfo info;
        info.backend = AudioBackendType::Asio;
        info.id = std::move(clsid);
        info.name = std::move(desc);
        out.push_back(std::move(info));
    }

    RegCloseKey(base);
}

#if defined(SATORI_ENABLE_ASIO) && (SATORI_ENABLE_ASIO != 0) && defined(SATORI_HAS_ASIO_SDK) && (SATORI_HAS_ASIO_SDK != 0)
std::string AsioErrorCodeString(ASIOError e) {
    switch (e) {
        case ASE_OK:
            return "ASE_OK";
        case ASE_SUCCESS:
            return "ASE_SUCCESS";
        case ASE_NotPresent:
            return "ASE_NotPresent";
        case ASE_HWMalfunction:
            return "ASE_HWMalfunction";
        case ASE_InvalidParameter:
            return "ASE_InvalidParameter";
        case ASE_InvalidMode:
            return "ASE_InvalidMode";
        case ASE_SPNotAdvancing:
            return "ASE_SPNotAdvancing";
        case ASE_NoClock:
            return "ASE_NoClock";
        case ASE_NoMemory:
            return "ASE_NoMemory";
        default:
            break;
    }
    std::ostringstream oss;
    oss << "ASIOError(" << static_cast<long>(e) << ")";
    return oss.str();
}

std::string AsioDriverErrorMessage(IASIO* driver) {
    if (!driver) {
        return {};
    }
    char buf[256]{};
    driver->getErrorMessage(buf);
    if (buf[0] == '\0') {
        return {};
    }
    return std::string(buf);
}

std::string AsioDriverName(IASIO* driver) {
    if (!driver) {
        return {};
    }
    char buf[64]{};
    driver->getDriverName(buf);
    if (buf[0] == '\0') {
        return {};
    }
    return std::string(buf);
}
#endif

}  // namespace

struct AsioAudioEngine::Impl {
#if defined(SATORI_ENABLE_ASIO) && (SATORI_ENABLE_ASIO != 0) && defined(SATORI_HAS_ASIO_SDK) && (SATORI_HAS_ASIO_SDK != 0)
    IASIO* driver = nullptr;
    ASIOCallbacks callbacks{};
    std::vector<ASIOBufferInfo> bufferInfos;
    long inputChannels = 0;
    long outputChannels = 0;
    long bufferSize = 0;
    bool supportsOutputReady = false;
    bool comInitialized = false;
    std::vector<ASIOSampleType> outputTypes;

    std::vector<float> interleaved;

    static AsioAudioEngine* activeEngine;

    static void bufferSwitch(long index, ASIOBool processNow);
    static ASIOTime* bufferSwitchTimeInfo(ASIOTime* params, long index, ASIOBool processNow);
    static void sampleRateDidChange(ASIOSampleRate sRate);
    static long asioMessage(long selector, long value, void* message, double* opt);

    static std::size_t bytesPerSample(ASIOSampleType type);
    static void writeSample(ASIOSampleType type, void* dst, float v);
#endif
};

#if defined(SATORI_ENABLE_ASIO) && (SATORI_ENABLE_ASIO != 0) && defined(SATORI_HAS_ASIO_SDK) && (SATORI_HAS_ASIO_SDK != 0)
AsioAudioEngine* AsioAudioEngine::Impl::activeEngine = nullptr;

void AsioAudioEngine::Impl::bufferSwitch(long index, ASIOBool processNow) {
    (void)bufferSwitchTimeInfo(nullptr, index, processNow);
}

ASIOTime* AsioAudioEngine::Impl::bufferSwitchTimeInfo(ASIOTime*, long index, ASIOBool) {
    AsioAudioEngine* self = activeEngine;
    if (!self || !self->impl_ || !self->impl_->driver || !self->renderCallback_) {
        return nullptr;
    }
    const std::size_t frames = static_cast<std::size_t>(self->impl_->bufferSize);
    const std::size_t channels = static_cast<std::size_t>(self->config_.channels);
    if (channels == 0 || frames == 0) {
        return nullptr;
    }

    self->impl_->interleaved.resize(frames * channels);
    self->renderCallback_(self->impl_->interleaved.data(), frames);

    const long bufferIndex = index ? 1 : 0;
    for (long ch = 0; ch < self->impl_->outputChannels; ++ch) {
        const ASIOSampleType type =
            (static_cast<std::size_t>(ch) < self->impl_->outputTypes.size())
                ? self->impl_->outputTypes[static_cast<std::size_t>(ch)]
                : ASIOSTFloat32LSB;
        void* dst =
            self->impl_->bufferInfos[static_cast<std::size_t>(ch)].buffers[bufferIndex];
        if (!dst) {
            continue;
        }

        const bool active = static_cast<std::size_t>(ch) < channels;
        const std::size_t stride = bytesPerSample(type);
        std::uint8_t* outBytes = reinterpret_cast<std::uint8_t*>(dst);
        for (std::size_t f = 0; f < frames; ++f) {
            const float v = active
                                ? self->impl_->interleaved[f * channels +
                                                          static_cast<std::size_t>(ch)]
                                : 0.0f;
            writeSample(type, outBytes + f * stride, v);
        }
    }

    if (self->impl_->supportsOutputReady) {
        (void)self->impl_->driver->outputReady();
    }
    return nullptr;
}

void AsioAudioEngine::Impl::sampleRateDidChange(ASIOSampleRate) {}

long AsioAudioEngine::Impl::asioMessage(long selector, long value, void*, double*) {
    AsioAudioEngine* self = activeEngine;
    if (!self || !self->impl_) return 0;
    switch (selector) {
        case kAsioSelectorSupported:
            switch (value) {
                case kAsioEngineVersion:
                case kAsioSupportsTimeInfo:
                    return 1;
                default:
                    return 0;
            }
        case kAsioEngineVersion:
            return 2;
        case kAsioSupportsTimeInfo:
            return 1;
        case kAsioSupportsTimeCode:
            return 0;
        default:
            break;
    }
    return 0;
}

std::size_t AsioAudioEngine::Impl::bytesPerSample(ASIOSampleType type) {
    switch (type) {
        case ASIOSTInt16LSB:
        case ASIOSTInt16MSB:
            return 2;
        case ASIOSTInt24LSB:
        case ASIOSTInt24MSB:
            return 3;
        case ASIOSTFloat64LSB:
        case ASIOSTFloat64MSB:
            return 8;
        default:
            return 4;
    }
}

void AsioAudioEngine::Impl::writeSample(ASIOSampleType type, void* dst, float v) {
    v = std::max(-1.0f, std::min(1.0f, v));
    switch (type) {
        case ASIOSTFloat32LSB: {
            *reinterpret_cast<float*>(dst) = v;
            break;
        }
        case ASIOSTFloat32MSB: {
            const float vf = v;
            const auto* src = reinterpret_cast<const std::uint8_t*>(&vf);
            auto* b = reinterpret_cast<std::uint8_t*>(dst);
            b[0] = src[3];
            b[1] = src[2];
            b[2] = src[1];
            b[3] = src[0];
            break;
        }
        case ASIOSTFloat64LSB: {
            *reinterpret_cast<double*>(dst) = static_cast<double>(v);
            break;
        }
        case ASIOSTFloat64MSB: {
            const double vd = static_cast<double>(v);
            const auto* src = reinterpret_cast<const std::uint8_t*>(&vd);
            auto* b = reinterpret_cast<std::uint8_t*>(dst);
            for (int i = 0; i < 8; ++i) {
                b[i] = src[7 - i];
            }
            break;
        }
        case ASIOSTInt32LSB: {
            const int32_t s = static_cast<int32_t>(std::llround(v * 2147483647.0));
            *reinterpret_cast<int32_t*>(dst) = s;
            break;
        }
        case ASIOSTInt32MSB: {
            const int32_t s = static_cast<int32_t>(std::llround(v * 2147483647.0));
            const auto* src = reinterpret_cast<const std::uint8_t*>(&s);
            auto* b = reinterpret_cast<std::uint8_t*>(dst);
            b[0] = src[3];
            b[1] = src[2];
            b[2] = src[1];
            b[3] = src[0];
            break;
        }
        case ASIOSTInt16LSB: {
            const int16_t s = static_cast<int16_t>(std::llround(v * 32767.0));
            *reinterpret_cast<int16_t*>(dst) = s;
            break;
        }
        case ASIOSTInt16MSB: {
            const int16_t s = static_cast<int16_t>(std::llround(v * 32767.0));
            const auto* src = reinterpret_cast<const std::uint8_t*>(&s);
            auto* b = reinterpret_cast<std::uint8_t*>(dst);
            b[0] = src[1];
            b[1] = src[0];
            break;
        }
        case ASIOSTInt24LSB: {
            const int32_t s = static_cast<int32_t>(std::llround(v * 8388607.0));
            uint8_t* b = reinterpret_cast<uint8_t*>(dst);
            b[0] = static_cast<uint8_t>(s & 0xFF);
            b[1] = static_cast<uint8_t>((s >> 8) & 0xFF);
            b[2] = static_cast<uint8_t>((s >> 16) & 0xFF);
            break;
        }
        case ASIOSTInt24MSB: {
            const int32_t s = static_cast<int32_t>(std::llround(v * 8388607.0));
            uint8_t* b = reinterpret_cast<uint8_t*>(dst);
            b[0] = static_cast<uint8_t>((s >> 16) & 0xFF);
            b[1] = static_cast<uint8_t>((s >> 8) & 0xFF);
            b[2] = static_cast<uint8_t>(s & 0xFF);
            break;
        }
        case ASIOSTInt32LSB24: {
            const int32_t s = static_cast<int32_t>(std::llround(v * 8388607.0));
            *reinterpret_cast<int32_t*>(dst) = (s << 8);
            break;
        }
        case ASIOSTInt32MSB24: {
            const int32_t s = static_cast<int32_t>(std::llround(v * 8388607.0));
            const int32_t packed = (s << 8);
            const auto* src = reinterpret_cast<const std::uint8_t*>(&packed);
            auto* b = reinterpret_cast<std::uint8_t*>(dst);
            b[0] = src[3];
            b[1] = src[2];
            b[2] = src[1];
            b[3] = src[0];
            break;
        }
        default:
            std::memset(dst, 0, bytesPerSample(type));
            break;
    }
}
#endif

AsioAudioEngine::AsioAudioEngine(AudioEngineConfig config)
    : config_(std::move(config)) {
    config_.backend = AudioBackendType::Asio;
}

AsioAudioEngine::~AsioAudioEngine() {
    shutdown();
}

bool AsioAudioEngine::initialize(RenderCallback callback) {
    if (initialized_) {
        return true;
    }
    lastError_.clear();
    renderCallback_ = std::move(callback);
    initialized_ = false;
    running_ = false;
    impl_ = std::make_unique<Impl>();

#if !defined(SATORI_ENABLE_ASIO) || (SATORI_ENABLE_ASIO == 0)
    setLastError("[ASIO] 未启用：请在 CMake 中打开 SATORI_ENABLE_ASIO。");
    return false;
#elif !defined(SATORI_HAS_ASIO_SDK) || (SATORI_HAS_ASIO_SDK == 0)
    setLastError("[ASIO] 未找到 ASIO SDK：请设置 SATORI_ASIO_SDK_DIR 并重新生成工程。");
    return false;
#else
    if (!renderCallback_) {
        setLastError("[ASIO] 缺少渲染回调。");
        return false;
    }
    if (Impl::activeEngine && Impl::activeEngine != this) {
        setLastError("[ASIO] 当前实现仅支持单实例 ASIO 引擎。");
        return false;
    }

    HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    impl_->comInitialized = (hr == S_OK || hr == S_FALSE);

    CLSID clsid{};
    if (config_.deviceId.empty() || FAILED(CLSIDFromString(config_.deviceId.c_str(), &clsid))) {
        setLastError("[ASIO] 无效的 ASIO CLSID。");
        return false;
    }

    impl_->driver = nullptr;
    // Steinberg host samples instantiate with the driver's CLSID as both CLSID and IID.
    hr = CoCreateInstance(clsid, nullptr, CLSCTX_INPROC_SERVER, clsid,
                          reinterpret_cast<void**>(&impl_->driver));
    if (FAILED(hr) || !impl_->driver) {
        std::ostringstream oss;
        oss << "[ASIO] CoCreateInstance 失败: 0x" << std::hex << hr;
        setLastError(oss.str());
        return false;
    }

    void* sysHandle = nullptr;
    if (config_.sysHandle != 0) {
        sysHandle = reinterpret_cast<void*>(config_.sysHandle);
    } else {
        sysHandle = GetDesktopWindow();
    }
    if (impl_->driver->init(sysHandle) == ASIOFalse) {
        std::ostringstream oss;
        oss << "[ASIO] driver->init 失败";
        const auto name = AsioDriverName(impl_->driver);
        if (!name.empty()) {
            oss << " (" << name << ")";
        }
        const auto msg = AsioDriverErrorMessage(impl_->driver);
        if (!msg.empty()) {
            oss << ": " << msg;
        }
        oss << ".";
        setLastError(oss.str());
        impl_->driver->Release();
        impl_->driver = nullptr;
        return false;
    }

    const ASIOError chRes = impl_->driver->getChannels(&impl_->inputChannels, &impl_->outputChannels);
    if (chRes != ASE_OK ||
        impl_->outputChannels <= 0) {
        std::ostringstream oss;
        oss << "[ASIO] getChannels 失败: " << AsioErrorCodeString(chRes);
        const auto msg = AsioDriverErrorMessage(impl_->driver);
        if (!msg.empty()) {
            oss << " (" << msg << ")";
        }
        oss << ".";
        setLastError(oss.str());
        impl_->driver->Release();
        impl_->driver = nullptr;
        return false;
    }

    ASIOSampleRate currentRate = 0;
    if (impl_->driver->getSampleRate(&currentRate) != ASE_OK || currentRate <= 0) {
        currentRate = 44100.0;
    }
    if (config_.sampleRate > 0 &&
        std::abs(static_cast<double>(currentRate) - static_cast<double>(config_.sampleRate)) > 1e-6) {
        const ASIOError setRes = impl_->driver->setSampleRate(static_cast<ASIOSampleRate>(config_.sampleRate));
        if (setRes == ASE_OK) {
            (void)impl_->driver->getSampleRate(&currentRate);
        }
    }
    config_.sampleRate = static_cast<std::uint32_t>(currentRate);

    long minSize = 0;
    long maxSize = 0;
    long preferredSize = 0;
    long granularity = 0;
    const ASIOError bsRes = impl_->driver->getBufferSize(&minSize, &maxSize, &preferredSize, &granularity);
    if (bsRes != ASE_OK) {
        std::ostringstream oss;
        oss << "[ASIO] getBufferSize 失败: " << AsioErrorCodeString(bsRes);
        const auto msg = AsioDriverErrorMessage(impl_->driver);
        if (!msg.empty()) {
            oss << " (" << msg << ")";
        }
        oss << ".";
        setLastError(oss.str());
        impl_->driver->Release();
        impl_->driver = nullptr;
        return false;
    }

    auto clampToRange = [&](long v) { return std::max(minSize, std::min(maxSize, v)); };
    long target = (config_.bufferFrames > 0) ? static_cast<long>(config_.bufferFrames) : preferredSize;
    target = clampToRange(target);
    if (granularity == -1) {
        // Power-of-two.
        long pow2 = 1;
        while (pow2 < target && pow2 < maxSize) pow2 <<= 1;
        const long lower = std::max(minSize, pow2 >> 1);
        const long upper = std::min(maxSize, pow2);
        target = (std::abs(target - lower) <= std::abs(upper - target)) ? lower : upper;
        target = clampToRange(target);
    } else if (granularity > 0) {
        const long steps = (target - minSize + granularity / 2) / granularity;
        target = clampToRange(minSize + steps * granularity);
    } else if (granularity == 0) {
        // Some drivers require preferred size.
        target = clampToRange(preferredSize);
    }

    impl_->bufferSize = target;
    config_.bufferFrames = static_cast<std::uint32_t>(impl_->bufferSize);

    if (config_.channels == 0) {
        config_.channels = 2;
    }
    config_.channels = static_cast<std::uint16_t>(
        std::max<long>(1, std::min<long>(static_cast<long>(config_.channels), impl_->outputChannels)));

    impl_->bufferInfos.clear();
    impl_->bufferInfos.resize(static_cast<std::size_t>(impl_->outputChannels));
    for (long ch = 0; ch < impl_->outputChannels; ++ch) {
        ASIOBufferInfo& bi = impl_->bufferInfos[static_cast<std::size_t>(ch)];
        bi.isInput = ASIOFalse;
        bi.channelNum = ch;
        bi.buffers[0] = nullptr;
        bi.buffers[1] = nullptr;
    }

    Impl::activeEngine = this;
    impl_->callbacks.bufferSwitch = &Impl::bufferSwitch;
    impl_->callbacks.sampleRateDidChange = &Impl::sampleRateDidChange;
    impl_->callbacks.asioMessage = &Impl::asioMessage;
    impl_->callbacks.bufferSwitchTimeInfo = &Impl::bufferSwitchTimeInfo;

    impl_->outputTypes.assign(static_cast<std::size_t>(impl_->outputChannels), ASIOSTFloat32LSB);
    for (long ch = 0; ch < impl_->outputChannels; ++ch) {
        ASIOChannelInfo ci{};
        ci.channel = ch;
        ci.isInput = ASIOFalse;
        if (impl_->driver->getChannelInfo(&ci) == ASE_OK) {
            impl_->outputTypes[static_cast<std::size_t>(ch)] = ci.type;
        }
    }

    const ASIOError createRes =
        impl_->driver->createBuffers(impl_->bufferInfos.data(), impl_->outputChannels,
                                     impl_->bufferSize, &impl_->callbacks);
    if (createRes != ASE_OK) {
        std::ostringstream oss;
        oss << "[ASIO] createBuffers 失败: " << AsioErrorCodeString(createRes);
        const auto msg = AsioDriverErrorMessage(impl_->driver);
        if (!msg.empty()) {
            oss << " (" << msg << ")";
        }
        setLastError(oss.str());
        impl_->driver->Release();
        impl_->driver = nullptr;
        Impl::activeEngine = nullptr;
        return false;
    }

    initialized_ = true;
    return true;
#endif
}

bool AsioAudioEngine::reinitialize(AudioEngineConfig config, RenderCallback callback) {
    shutdown();
    config_ = std::move(config);
    config_.backend = AudioBackendType::Asio;
    return initialize(std::move(callback));
}

void AsioAudioEngine::shutdown() {
    stop();
    initialized_ = false;
    running_ = false;
#if defined(SATORI_ENABLE_ASIO) && (SATORI_ENABLE_ASIO != 0) && defined(SATORI_HAS_ASIO_SDK) && (SATORI_HAS_ASIO_SDK != 0)
    if (impl_ && impl_->driver) {
        (void)impl_->driver->disposeBuffers();
        impl_->driver->Release();
        impl_->driver = nullptr;
    }
    if (Impl::activeEngine == this) {
        Impl::activeEngine = nullptr;
    }
    if (impl_ && impl_->comInitialized) {
        CoUninitialize();
    }
#endif
    impl_.reset();
}

bool AsioAudioEngine::start() {
    if (!initialized_ || running_) {
        return false;
    }
#if defined(SATORI_ENABLE_ASIO) && (SATORI_ENABLE_ASIO != 0) && defined(SATORI_HAS_ASIO_SDK) && (SATORI_HAS_ASIO_SDK != 0)
    if (!impl_ || !impl_->driver) {
        return false;
    }
    const ASIOError res = impl_->driver->start();
    if (res != ASE_OK) {
        std::ostringstream oss;
        oss << "[ASIO] start 失败: " << AsioErrorCodeString(res);
        const auto msg = AsioDriverErrorMessage(impl_->driver);
        if (!msg.empty()) {
            oss << " (" << msg << ")";
        }
        setLastError(oss.str());
        return false;
    }
    running_ = true;
    return true;
#else
    return false;
#endif
}

void AsioAudioEngine::stop() {
#if defined(SATORI_ENABLE_ASIO) && (SATORI_ENABLE_ASIO != 0) && defined(SATORI_HAS_ASIO_SDK) && (SATORI_HAS_ASIO_SDK != 0)
    if (impl_ && impl_->driver && running_) {
        (void)impl_->driver->stop();
    }
#endif
    running_ = false;
}

void AsioAudioEngine::setLastError(const std::string& message) {
    lastError_ = message;
}

std::vector<AudioDeviceInfo> AsioAudioEngine::EnumerateAsioDevices() {
    std::vector<AudioDeviceInfo> out;
    EnumerateAsioRegistryRoot(HKEY_LOCAL_MACHINE, L"SOFTWARE\\ASIO", out);
    EnumerateAsioRegistryRoot(HKEY_LOCAL_MACHINE, L"SOFTWARE\\WOW6432Node\\ASIO", out);
    std::sort(out.begin(), out.end(),
              [](const AudioDeviceInfo& a, const AudioDeviceInfo& b) { return a.name < b.name; });
    return out;
}

}  // namespace winaudio
