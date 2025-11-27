#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

#include "audio/WaveWriter.h"
#include "synthesis/KarplusStrongString.h"

namespace {

struct AppConfig {
    double frequency = 440.0;
    double duration = 2.0;
    double sampleRate = 44100.0;
    float decay = 0.996f;
    float brightness = 0.5f;
    std::filesystem::path output = "satori_demo.wav";
};

void printUsage() {
    std::cout << "用法: Satori [--freq 440] [--duration 2.0] [--samplerate 44100] "
                 "[--decay 0.996] [--brightness 0.5] [--output out.wav]\n";
}

bool parseDouble(const std::string& value, double& dest) {
    try {
        dest = std::stod(value);
        return true;
    } catch (const std::exception&) {
        return false;
    }
}

bool parseFloat(const std::string& value, float& dest) {
    try {
        dest = std::stof(value);
        return true;
    } catch (const std::exception&) {
        return false;
    }
}

AppConfig parseArgs(int argc, char** argv, bool& showHelp) {
    AppConfig config;
    showHelp = false;

    std::unordered_map<std::string, std::string> kv;
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--help" || arg == "-h") {
            showHelp = true;
            return config;
        }
        if (arg.rfind("--", 0) == 0 && i + 1 < argc) {
            kv[arg.substr(2)] = argv[++i];
        }
    }

    if (auto it = kv.find("freq"); it != kv.end()) {
        parseDouble(it->second, config.frequency);
    }
    if (auto it = kv.find("duration"); it != kv.end()) {
        parseDouble(it->second, config.duration);
    }
    if (auto it = kv.find("samplerate"); it != kv.end()) {
        parseDouble(it->second, config.sampleRate);
    }
    if (auto it = kv.find("decay"); it != kv.end()) {
        parseFloat(it->second, config.decay);
    }
    if (auto it = kv.find("brightness"); it != kv.end()) {
        parseFloat(it->second, config.brightness);
    }
    if (auto it = kv.find("output"); it != kv.end()) {
        config.output = it->second;
    }

    return config;
}

}  // namespace

int main(int argc, char** argv) {
    bool showHelp = false;
    AppConfig appConfig = parseArgs(argc, argv, showHelp);
    if (showHelp) {
        printUsage();
        return 0;
    }

    synthesis::StringConfig synthConfig;
    synthConfig.sampleRate = appConfig.sampleRate;
    synthConfig.decay = appConfig.decay;
    synthConfig.brightness = appConfig.brightness;

    synthesis::KarplusStrongString ks(synthConfig);
    const auto samples = ks.pluck(appConfig.frequency, appConfig.duration);
    if (samples.empty()) {
        std::cerr << "生成样本失败，请检查输入参数。\n";
        return 1;
    }

    audio::WaveWriter writer;
    audio::WaveFormat format;
    format.sampleRate = static_cast<uint32_t>(appConfig.sampleRate);
    std::string errorMessage;
    if (!writer.write(appConfig.output, samples, format, errorMessage)) {
        std::cerr << errorMessage << "\n";
        return 1;
    }

    std::cout << "已生成 WAV 文件: " << std::filesystem::absolute(appConfig.output)
              << "\n";
    return 0;
}
