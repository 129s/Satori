#include "win/audio/UnifiedAudioEngine.h"

#include <algorithm>

namespace winaudio {

UnifiedAudioEngine::UnifiedAudioEngine(AudioEngineConfig config) {
    backend_ = config.backend;
    if (backend_ == AudioBackendType::Asio) {
        asio_ = std::make_unique<AsioAudioEngine>(std::move(config));
    } else {
        config.backend = AudioBackendType::WasapiShared;
        wasapi_ = std::make_unique<WASAPIAudioEngine>(std::move(config));
    }
}

UnifiedAudioEngine::~UnifiedAudioEngine() {
    shutdown();
}

bool UnifiedAudioEngine::initialize(RenderCallback callback) {
    if (backend_ == AudioBackendType::Asio) {
        if (!asio_) {
            AudioEngineConfig cfg;
            cfg.backend = AudioBackendType::Asio;
            asio_ = std::make_unique<AsioAudioEngine>(std::move(cfg));
        }
        return asio_->initialize(std::move(callback));
    }
    if (!wasapi_) {
        AudioEngineConfig cfg;
        cfg.backend = AudioBackendType::WasapiShared;
        wasapi_ = std::make_unique<WASAPIAudioEngine>(std::move(cfg));
    }
    return wasapi_->initialize(std::move(callback));
}

bool UnifiedAudioEngine::reinitialize(AudioEngineConfig config, RenderCallback callback) {
    shutdown();
    backend_ = config.backend;
    if (backend_ == AudioBackendType::Asio) {
        asio_ = std::make_unique<AsioAudioEngine>(std::move(config));
        return asio_->initialize(std::move(callback));
    }
    config.backend = AudioBackendType::WasapiShared;
    wasapi_ = std::make_unique<WASAPIAudioEngine>(std::move(config));
    return wasapi_->initialize(std::move(callback));
}

void UnifiedAudioEngine::shutdown() {
    if (wasapi_) {
        wasapi_->shutdown();
    }
    if (asio_) {
        asio_->shutdown();
    }
}

bool UnifiedAudioEngine::start() {
    if (backend_ == AudioBackendType::Asio) {
        return asio_ ? asio_->start() : false;
    }
    return wasapi_ ? wasapi_->start() : false;
}

void UnifiedAudioEngine::stop() {
    if (backend_ == AudioBackendType::Asio) {
        if (asio_) asio_->stop();
        return;
    }
    if (wasapi_) wasapi_->stop();
}

bool UnifiedAudioEngine::isRunning() const {
    if (backend_ == AudioBackendType::Asio) {
        return asio_ ? asio_->isRunning() : false;
    }
    return wasapi_ ? wasapi_->isRunning() : false;
}

const AudioEngineConfig& UnifiedAudioEngine::config() const {
    if (backend_ == AudioBackendType::Asio && asio_) {
        return asio_->config();
    }
    if (wasapi_) {
        return wasapi_->config();
    }
    static AudioEngineConfig empty;
    return empty;
}

const std::string& UnifiedAudioEngine::lastError() const {
    if (backend_ == AudioBackendType::Asio && asio_) {
        return asio_->lastError();
    }
    if (wasapi_) {
        return wasapi_->lastError();
    }
    static const std::string empty;
    return empty;
}

std::vector<AudioDeviceInfo> UnifiedAudioEngine::EnumerateDevices() {
    std::vector<AudioDeviceInfo> devices;
    // WASAPI shared endpoints.
    {
        AudioDeviceInfo def;
        def.backend = AudioBackendType::WasapiShared;
        def.id = L"";
        def.name = L"Default";
        devices.push_back(def);
        auto wasapi = WASAPIAudioEngine::EnumerateOutputDevices();
        devices.insert(devices.end(), wasapi.begin(), wasapi.end());
    }
    // ASIO drivers (if present in registry).
#if defined(SATORI_ENABLE_ASIO) && (SATORI_ENABLE_ASIO != 0) && defined(SATORI_HAS_ASIO_SDK) && (SATORI_HAS_ASIO_SDK != 0)
    {
        auto asio = AsioAudioEngine::EnumerateAsioDevices();
        devices.insert(devices.end(), asio.begin(), asio.end());
    }
#endif
    return devices;
}

}  // namespace winaudio
