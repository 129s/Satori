#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

#include "audio/WaveWriter.h"
#include "engine/StringSynthEngine.h"
#include "synthesis/KarplusStrongSynth.h"

namespace {

struct AppConfig {
    double frequency = 440.0;
    std::vector<synthesis::NoteEvent> notes;
    double duration = 2.0;
    double sampleRate = 44100.0;
    float decay = 0.996f;
    float brightness = 0.5f;
    float pickPosition = 0.5f;
    bool enableLowpass = true;
    synthesis::NoiseType noiseType = synthesis::NoiseType::White;
    unsigned int seed = 0;
    float ampRelease = 0.35f;
    std::filesystem::path output = "satori_demo.wav";
};

void printUsage() {
    std::cout << "用法: Satori [--freq 440] [--notes 440[:start[:dur]],660] [--duration 2.0] "
                 "[--samplerate 44100] [--decay 0.996] [--brightness 0.5] "
                 "[--pickpos 0.5] [--noise white|binary] [--filter lowpass|none] "
                 "[--release 0.35] [--seed 1234] [--output out.wav]\n";
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

std::string toLower(std::string value) {
    for (auto& ch : value) {
        ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    }
    return value;
}

std::vector<synthesis::NoteEvent> parseNoteList(const std::string& csv,
                                                double defaultDuration) {
    std::vector<synthesis::NoteEvent> notes;
    std::stringstream ss(csv);
    std::string token;
    while (std::getline(ss, token, ',')) {
        if (token.empty()) {
            continue;
        }
        std::stringstream parts(token);
        std::string piece;
        std::vector<std::string> segments;
        while (std::getline(parts, piece, ':')) {
            segments.push_back(piece);
        }
        if (segments.empty()) {
            continue;
        }

        double freq = 0.0;
        double start = 0.0;
        double duration = defaultDuration;
        if (!parseDouble(segments[0], freq)) {
            continue;
        }
        if (segments.size() >= 2 && !segments[1].empty() &&
            !parseDouble(segments[1], start)) {
            continue;
        }
        if (segments.size() >= 3 && !segments[2].empty() &&
            !parseDouble(segments[2], duration)) {
            continue;
        }

        if (freq <= 0.0 || duration <= 0.0) {
            continue;
        }
        notes.push_back({freq, duration, std::max(0.0, start)});
    }
    return notes;
}

void parseNoise(const std::string& value, synthesis::NoiseType& dest) {
    const auto lower = toLower(value);
    if (lower == "binary") {
        dest = synthesis::NoiseType::Binary;
    } else {
        dest = synthesis::NoiseType::White;
    }
}

float defaultAmpRelease() {
    if (const auto* info = engine::GetParamInfo(engine::ParamId::AmpRelease)) {
        return info->defaultValue;
    }
    return 0.35f;
}

AppConfig parseArgs(int argc, char** argv, bool& showHelp) {
    AppConfig config;
    config.ampRelease = defaultAmpRelease();
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

    if (auto it = kv.find("duration"); it != kv.end()) {
        parseDouble(it->second, config.duration);
    }
    if (auto it = kv.find("samplerate"); it != kv.end()) {
        parseDouble(it->second, config.sampleRate);
    }
    if (auto it = kv.find("freq"); it != kv.end()) {
        parseDouble(it->second, config.frequency);
    }
    if (auto it = kv.find("notes"); it != kv.end()) {
        config.notes = parseNoteList(it->second, config.duration);
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
    if (auto it = kv.find("release"); it != kv.end()) {
        parseFloat(it->second, config.ampRelease);
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

float findPeak(const std::vector<float>& buffer) {
    float peak = 0.0f;
    for (float sample : buffer) {
        peak = std::max(peak, std::abs(sample));
    }
    return peak;
}

void normalize(std::vector<float>& buffer, float peak) {
    if (peak <= 1.0f || peak == 0.0f) {
        return;
    }
    const float invPeak = 1.0f / peak;
    for (auto& sample : buffer) {
        sample *= invPeak;
    }
}

std::vector<float> renderWithEngine(engine::StringSynthEngine& engine,
                                    const std::vector<synthesis::NoteEvent>& notes,
                                    double sampleRate,
                                    double tailSeconds) {
    if (notes.empty() || sampleRate <= 0.0) {
        return {};
    }

    double maxTime = 0.0;
    for (const auto& note : notes) {
        maxTime = std::max(maxTime, note.startTime + note.duration);
    }
    const double totalSeconds = maxTime + std::max(0.0, tailSeconds);
    const std::size_t totalFrames = static_cast<std::size_t>(
        std::max(0.0, std::ceil(totalSeconds * sampleRate)));
    if (totalFrames == 0) {
        return {};
    }

    const uint16_t channels = 1;
    std::vector<float> buffer(totalFrames * channels, 0.0f);

    int noteId = 1;
    for (const auto& note : notes) {
        const auto startFrame = static_cast<std::uint64_t>(
            std::max(0.0, std::round(note.startTime * sampleRate)));
        const auto durationFrames = static_cast<std::uint64_t>(
            std::max(0.0, std::round(note.duration * sampleRate)));

        engine::Event on{};
        on.type = engine::EventType::NoteOn;
        on.noteId = noteId;
        on.frequency = note.frequency;
        on.velocity = 1.0f;
        engine.enqueueEventAt(on, startFrame);

        engine::Event off{};
        off.type = engine::EventType::NoteOff;
        off.noteId = noteId;
        engine.enqueueEventAt(off, startFrame + durationFrames);

        ++noteId;
    }

    const std::size_t blockFrames = 512;
    std::size_t cursor = 0;
    while (cursor < totalFrames) {
        const std::size_t framesThisBlock =
            std::min(blockFrames, totalFrames - cursor);
        engine::ProcessBlock block{
            buffer.data() + cursor * channels, framesThisBlock, channels};
        engine.process(block);
        cursor += framesThisBlock;
    }

    return buffer;
}

}  // namespace

int main(int argc, char** argv) {
    bool showHelp = false;
    AppConfig appConfig = parseArgs(argc, argv, showHelp);
    if (showHelp) {
        printUsage();
        return 0;
    }

    engine::StringSynthEngine synthEngine;
    synthEngine.setSampleRate(appConfig.sampleRate);
    synthEngine.setParam(engine::ParamId::Decay, appConfig.decay);
    synthEngine.setParam(engine::ParamId::Brightness, appConfig.brightness);
    synthEngine.setParam(engine::ParamId::PickPosition, appConfig.pickPosition);
    synthEngine.setParam(engine::ParamId::EnableLowpass, appConfig.enableLowpass ? 1.0f : 0.0f);
    synthEngine.setParam(engine::ParamId::NoiseType,
                         appConfig.noiseType == synthesis::NoiseType::Binary ? 1.0f : 0.0f);
    synthEngine.setParam(engine::ParamId::MasterGain, 1.0f);
    synthEngine.setParam(engine::ParamId::AmpRelease, appConfig.ampRelease);

    synthesis::StringConfig synthConfig = synthEngine.stringConfig();
    synthConfig.seed = appConfig.seed;
    synthEngine.setConfig(synthConfig);

    std::vector<synthesis::NoteEvent> noteSequence = appConfig.notes;
    if (noteSequence.empty()) {
        noteSequence.push_back({appConfig.frequency, appConfig.duration, 0.0});
    }

    const double tailSeconds =
        std::max(0.5, static_cast<double>(synthEngine.getParam(engine::ParamId::AmpRelease)) * 4.0);
    auto samples = renderWithEngine(synthEngine, noteSequence, appConfig.sampleRate, tailSeconds);
    const float peak = findPeak(samples);
    normalize(samples, peak);

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
