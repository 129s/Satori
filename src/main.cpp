#include <cctype>
#include <filesystem>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

#include "audio/WaveWriter.h"
#include "synthesis/KarplusStrongString.h"
#include "synthesis/KarplusStrongSynth.h"

namespace {

struct AppConfig {
    double frequency = 440.0;
    std::vector<double> notes;
    double duration = 2.0;
    double sampleRate = 44100.0;
    float decay = 0.996f;
    float brightness = 0.5f;
    float pickPosition = 0.5f;
    bool enableLowpass = true;
    synthesis::NoiseType noiseType = synthesis::NoiseType::White;
    unsigned int seed = 0;
    std::filesystem::path output = "satori_demo.wav";
};

void printUsage() {
    std::cout << "用法: Satori [--freq 440] [--notes 440,660] [--duration 2.0] "
                 "[--samplerate 44100] [--decay 0.996] [--brightness 0.5] "
                 "[--pickpos 0.5] [--noise white|binary] [--filter lowpass|none] "
                 "[--seed 1234] [--output out.wav]\n";
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

std::vector<double> parseFrequencyList(const std::string& csv) {
    std::vector<double> frequencies;
    std::stringstream ss(csv);
    std::string token;
    while (std::getline(ss, token, ',')) {
        if (token.empty()) {
            continue;
        }
        try {
            frequencies.push_back(std::stod(token));
        } catch (const std::exception&) {
            // 忽略非法项
        }
    }
    return frequencies;
}

std::string toLower(std::string value) {
    for (auto& ch : value) {
        ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    }
    return value;
}

void parseNoise(const std::string& value, synthesis::NoiseType& dest) {
    const auto lower = toLower(value);
    if (lower == "binary") {
        dest = synthesis::NoiseType::Binary;
    } else {
        dest = synthesis::NoiseType::White;
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
    if (auto it = kv.find("notes"); it != kv.end()) {
        config.notes = parseFrequencyList(it->second);
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
    if (auto it = kv.find("pickpos"); it != kv.end()) {
        parseFloat(it->second, config.pickPosition);
    }
    if (auto it = kv.find("noise"); it != kv.end()) {
        parseNoise(it->second, config.noiseType);
    }
    if (auto it = kv.find("filter"); it != kv.end()) {
        config.enableLowpass = toLower(it->second) != "none";
    }
    if (auto it = kv.find("seed"); it != kv.end()) {
        double tmp = 0.0;
        if (parseDouble(it->second, tmp)) {
            config.seed = static_cast<unsigned int>(tmp);
        }
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
    synthConfig.pickPosition = appConfig.pickPosition;
    synthConfig.enableLowpass = appConfig.enableLowpass;
    synthConfig.noiseType = appConfig.noiseType;
    synthConfig.seed = appConfig.seed;

    std::vector<float> samples;
    if (!appConfig.notes.empty()) {
        synthesis::KarplusStrongSynth synth(synthConfig);
        std::vector<synthesis::NoteEvent> notes;
        notes.reserve(appConfig.notes.size());
        for (double freq : appConfig.notes) {
            notes.push_back({freq, appConfig.duration, 0.0});
        }
        samples = synth.renderNotes(notes);
    } else {
        synthesis::KarplusStrongString ks(synthConfig);
        samples = ks.pluck(appConfig.frequency, appConfig.duration);
    }
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
