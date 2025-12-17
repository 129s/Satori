#include "engine/StringSynthEngine.h"

#include <array>
#include <atomic>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <iterator>
#include <limits>
#include <mutex>
#include <thread>

#include "dsp/Filter.h"
#include "dsp/ConvolutionReverb.h"
#include "dsp/Denormals.h"
#include "dsp/PartitionedConvolver.h"
#include "dsp/RoomIrLibrary.h"

namespace engine {

namespace {
float Clamp01(float v) { return std::max(0.0f, std::min(1.0f, v)); }
float ComputeOnePoleAlpha(double sampleRate, double timeSeconds) {
    if (sampleRate <= 0.0 || timeSeconds <= 0.0) {
        return 1.0f;
    }
    const double a = 1.0 - std::exp(-1.0 / (sampleRate * timeSeconds));
    return static_cast<float>(std::clamp(a, 0.0, 1.0));
}
}  // namespace

class BodyFilter {
public:
    void setSampleRate(double sampleRate) {
        if (sampleRate <= 0.0) {
            return;
        }
        sampleRate_ = sampleRate;
        updateCoefficients();
    }

    void setParams(float tone, float size) {
        tone_ = std::clamp(tone, 0.0f, 1.0f);
        size_ = std::clamp(size, 0.0f, 1.0f);
        updateCoefficients();
    }

    void reset() {
        lowFilter_.reset();
    }

    float process(float input) {
        const float low = lowFilter_.process(input);
        const float high = input - low;
        return low * lowGain_ + high * highGain_;
    }

private:
    void updateCoefficients() {
        const float fc = 180.0f + 800.0f * size_;
        const float alpha = std::clamp(
            static_cast<float>((2.0 * 3.141592653589793 * fc) / sampleRate_), 0.001f, 0.99f);
        lowFilter_.setAlpha(alpha);
        const float tilt = (tone_ - 0.5f) * 0.6f;
        lowGain_ = std::clamp(1.0f + (-tilt), 0.6f, 1.6f);
        highGain_ = std::clamp(1.0f + tilt, 0.6f, 1.6f);
    }

    dsp::OnePoleLowPass lowFilter_{0.1f};
    double sampleRate_ = 44100.0;
    float tone_ = 0.5f;
    float size_ = 0.5f;
    float lowGain_ = 1.0f;
    float highGain_ = 1.0f;
};

class ExpressiveMapping {
public:
    static float Apply(float velocity, double frequency,
                       const synthesis::StringConfig& base,
                       synthesis::StringConfig& out) {
        out = base;
        const float v = std::clamp(velocity, 0.0f, 1.0f);
        const double refFreq = 440.0;
        const double ratio = (frequency > 0.0) ? (frequency / refFreq) : 1.0;
        const float keyTrack =
            static_cast<float>(std::clamp(std::log2(ratio), -3.0, 3.0));

        const float amp = 0.45f + 0.65f * v;

        auto clampParam = [](ParamId id, float value) {
            if (const auto* info = GetParamInfo(id)) {
                return ClampToRange(*info, value);
            }
            return value;
        };

        const float brightnessDelta = 0.28f * (v - 0.5f) + 0.12f * keyTrack;
        out.brightness =
            clampParam(ParamId::Brightness, out.brightness + brightnessDelta);

        const float decayDelta = -0.022f * (v - 0.5f) - 0.012f * keyTrack;
        out.decay = clampParam(ParamId::Decay, out.decay + decayDelta);

        return amp;
    }
};

class RoomProcessor {
public:
    RoomProcessor() { startWorker(); }

    ~RoomProcessor() { stopWorker(); }

    void setSampleRate(double sampleRate) {
        if (sampleRate <= 0.0) {
            return;
        }
        const double rounded = std::lround(sampleRate);
        const double prior = std::lround(requestedSampleRate_.load(std::memory_order_relaxed));
        if (rounded == prior && builtOnce_.load(std::memory_order_acquire)) {
            return;
        }
        requestedSampleRate_.store(sampleRate, std::memory_order_release);
        mixSmoothingAlpha_ = ComputeOnePoleAlpha(sampleRate, 0.01);
        sampleRateSeq_.fetch_add(1, std::memory_order_acq_rel);
        dataReady_.notify_one();
    }

    void setMix(float mix) {
        requestedMix_.store(std::clamp(mix, 0.0f, 1.0f), std::memory_order_relaxed);
    }

    void setIrIndex(int index) {
        requestedIrIndex_.store(std::max(0, index), std::memory_order_relaxed);
    }

    void reset() {
        resetSeq_.fetch_add(1, std::memory_order_acq_rel);
        // Clear local (audio-thread) state so old wet blocks don't leak through.
        blockPos_ = 0;
        haveWetBlock_ = false;
        havePlayDryBlock_ = false;
        haveOutputSeq_ = false;
        bufferedWet_.reset();
        for (auto& block : dryHistory_) {
            block.seq = std::numeric_limits<std::uint64_t>::max();
        }
        syncReverb_.reset();
        syncBuiltOnce_ = false;
        currentMix_ = Clamp01(requestedMix_.load(std::memory_order_relaxed));
        lastTargetMix_ = 0.0f;
    }

    void process(float input, float& outL, float& outR) {
        const float targetMix = Clamp01(requestedMix_.load(std::memory_order_relaxed));
        currentMix_ += (targetMix - currentMix_) * mixSmoothingAlpha_;

        float wetL = 0.0f;
        float wetR = 0.0f;

        if (useSynchronous_) {
            syncProcess(input, wetL, wetR);  // wet-only
            outL = input * (1.0f - currentMix_) + wetL * currentMix_;
            outR = input * (1.0f - currentMix_) + wetR * currentMix_;
            return;
        }

        if (targetMix <= 0.0f) {
            if (lastTargetMix_ > 0.0f) {
                resetSeq_.fetch_add(1, std::memory_order_acq_rel);
                haveWetBlock_ = false;
                havePlayDryBlock_ = false;
                haveOutputSeq_ = false;
                bufferedWet_.reset();
                StereoBlock drained{};
                while (wetQueue_.pop(drained)) {
                }
            }
            lastTargetMix_ = targetMix;
            outL = input;
            outR = input;
            return;
        }

        if (lastTargetMix_ <= 0.0f) {
            // Freshly enabled: reset sequencing and clear any stale buffered blocks.
            resetSeq_.fetch_add(1, std::memory_order_acq_rel);
            nextSeq_ = 0;
            outputSeq_ = 0;
            haveOutputSeq_ = false;
            haveWetBlock_ = false;
            havePlayDryBlock_ = false;
            bufferedWet_.reset();
            for (auto& block : dryHistory_) {
                block.seq = std::numeric_limits<std::uint64_t>::max();
            }
            StereoBlock drained{};
            while (wetQueue_.pop(drained)) {
            }
        }
        lastTargetMix_ = targetMix;

        // Only swap wet blocks on boundaries to avoid mid-block discontinuities.
        if (blockPos_ == 0) {
            if (nextSeq_ >= kOutputDelayBlocks) {
                outputSeq_ = nextSeq_ - kOutputDelayBlocks;
                haveOutputSeq_ = true;
                playDryBlock_ =
                    dryHistory_[static_cast<std::size_t>(outputSeq_ & (kDryHistoryBlocks - 1))];
                havePlayDryBlock_ = (playDryBlock_.seq == outputSeq_);
            } else {
                haveOutputSeq_ = false;
                havePlayDryBlock_ = false;
            }

            const std::uint64_t expectedSeq =
                havePlayDryBlock_ ? playDryBlock_.seq : std::numeric_limits<std::uint64_t>::max();
            haveWetBlock_ = false;
            if (expectedSeq == std::numeric_limits<std::uint64_t>::max()) {
                // Not enough history yet; don't drain the queue.
            } else if (bufferedWet_ && bufferedWet_->seq == expectedSeq) {
                wetBlock_ = *bufferedWet_;
                bufferedWet_.reset();
                haveWetBlock_ = true;
            } else {
                StereoBlock candidate{};
                while (wetQueue_.pop(candidate)) {
                    if (candidate.seq < expectedSeq) {
                        continue;
                    }
                    if (candidate.seq == expectedSeq) {
                        wetBlock_ = candidate;
                        haveWetBlock_ = true;
                        break;
                    }
                    bufferedWet_ = candidate;
                    break;
                }
            }
        }
        if (haveWetBlock_) {
            wetL = wetBlock_.samples[blockPos_ * 2];
            wetR = wetBlock_.samples[blockPos_ * 2 + 1];
        }

        const float dryOut =
            havePlayDryBlock_ ? playDryBlock_.samples[blockPos_] : input;
        outL = dryOut * (1.0f - currentMix_) + wetL * currentMix_;
        outR = dryOut * (1.0f - currentMix_) + wetR * currentMix_;

        // Input path: accumulate dry into fixed blocks and enqueue for worker.
        dryAccum_.samples[blockPos_] = input;
        ++blockPos_;
        if (blockPos_ >= kBlockSize) {
            dryAccum_.seq = nextSeq_++;
            if (dryQueue_.push(dryAccum_)) {
                pendingDryBlocks_.fetch_add(1, std::memory_order_release);
                dataReady_.notify_one();
            }
            dryHistory_[static_cast<std::size_t>(dryAccum_.seq & (kDryHistoryBlocks - 1))] =
                dryAccum_;
            blockPos_ = 0;
            updateOfflineDetection();
        }
    }

private:
    static constexpr std::size_t kBlockSize = 256;
    static constexpr std::size_t kFftSize = 512;
    static constexpr std::size_t kLateBlockSize = 1024;
    static constexpr std::size_t kLateFftSize = 2048;
    static constexpr std::size_t kIrEarlySamples = kLateBlockSize;  // 1024
    // Strategy A (fixed latency): delay output by a few blocks to absorb worker jitter
    // and ensure wet blocks arrive in time.
    static constexpr std::size_t kOutputDelayBlocks = 6;
    static constexpr std::size_t kDryHistoryBlocks = 64;  // power-of-two
    static constexpr std::size_t kQueueCapacity = 256;  // blocks (power-of-two)

    template <typename T, std::size_t Capacity>
    class SpscRing {
    public:
        static_assert(Capacity >= 2, "Capacity too small");
        static_assert((Capacity & (Capacity - 1)) == 0, "Capacity must be power of two");

        bool push(const T& value) {
            const std::size_t head = head_.load(std::memory_order_relaxed);
            const std::size_t tail = tail_.load(std::memory_order_acquire);
            if ((head - tail) >= Capacity) {
                return false;
            }
            buffer_[head & (Capacity - 1)] = value;
            head_.store(head + 1, std::memory_order_release);
            return true;
        }

        bool pop(T& out) {
            const std::size_t tail = tail_.load(std::memory_order_relaxed);
            const std::size_t head = head_.load(std::memory_order_acquire);
            if (head == tail) {
                return false;
            }
            out = buffer_[tail & (Capacity - 1)];
            tail_.store(tail + 1, std::memory_order_release);
            return true;
        }

    private:
        std::array<T, Capacity> buffer_{};
        std::atomic<std::size_t> head_{0};
        std::atomic<std::size_t> tail_{0};
    };

    struct DryBlock {
        std::uint64_t seq = 0;
        std::array<float, kBlockSize> samples{};
    };

    struct StereoBlock {
        std::uint64_t seq = 0;
        std::array<float, kBlockSize * 2> samples{};
    };

    static_assert(kOutputDelayBlocks < kDryHistoryBlocks,
                  "Output delay must fit in dry history");

    static std::vector<float> ResampleLinear(const float* src,
                                             std::size_t srcCount,
                                             int srcRate,
                                             int dstRate) {
        if (!src || srcCount == 0 || srcRate <= 0 || dstRate <= 0) {
            return {};
        }
        if (srcRate == dstRate) {
            return std::vector<float>(src, src + srcCount);
        }
        const double ratio = static_cast<double>(dstRate) / static_cast<double>(srcRate);
        const std::size_t dstCount =
            std::max<std::size_t>(1, static_cast<std::size_t>(std::llround(srcCount * ratio)));
        std::vector<float> dst(dstCount, 0.0f);
        for (std::size_t i = 0; i < dstCount; ++i) {
            const double srcPos = static_cast<double>(i) / ratio;
            const std::size_t idx = static_cast<std::size_t>(std::floor(srcPos));
            const std::size_t idx1 = std::min(srcCount - 1, idx + 1);
            const float t = static_cast<float>(srcPos - static_cast<double>(idx));
            const float a = src[std::min(idx, srcCount - 1)];
            const float b = src[idx1];
            dst[i] = a + (b - a) * t;
        }
        return dst;
    }

    void rebuildKernels(double sampleRate,
                        std::vector<dsp::StereoConvolutionKernel>& kernelsOut) {
        kernelsOut.clear();
        const auto& list = dsp::RoomIrLibrary::list();
        kernelsOut.reserve(list.size());

        for (std::size_t i = 0; i < list.size(); ++i) {
            const auto ir = dsp::RoomIrLibrary::samples(static_cast<int>(i));
            const bool stereo = (ir.channels == 2 && ir.right);
            std::vector<float> resampledL = ResampleLinear(ir.left, ir.frameCount, ir.sampleRate,
                                                          static_cast<int>(std::lround(sampleRate)));
            std::vector<float> resampledR;
            if (stereo) {
                resampledR = ResampleLinear(ir.right, ir.frameCount, ir.sampleRate,
                                            static_cast<int>(std::lround(sampleRate)));
            }
            if (stereo && resampledR.size() != resampledL.size()) {
                const std::size_t n = std::min(resampledL.size(), resampledR.size());
                resampledL.resize(n);
                resampledR.resize(n);
            }

            bool treatStereoAsMono = false;
            if (stereo && !resampledR.empty()) {
                // Some "stereo" IRs are dual-mono (L==R). In that case, treat it as mono
                // to keep the classic stereo decorrelation path and cut CPU in half.
                const std::size_t frames = std::min(resampledL.size(), resampledR.size());
                double energy = 0.0;
                double diffEnergy = 0.0;
                for (std::size_t f = 0; f < frames; ++f) {
                    const double l = static_cast<double>(resampledL[f]);
                    const double r = static_cast<double>(resampledR[f]);
                    energy += 0.5 * (l * l + r * r);
                    const double d = l - r;
                    diffEnergy += d * d;
                }
                if (energy <= std::numeric_limits<double>::min()) {
                    treatStereoAsMono = true;
                } else {
                    const double rms = std::sqrt(energy / static_cast<double>(frames));
                    const double diffRms = std::sqrt(diffEnergy / static_cast<double>(frames));
                    treatStereoAsMono = (diffRms / std::max(1e-12, rms)) < 1e-3;  // ~ -60dB
                }
            }

            dsp::StereoConvolutionKernel kernel;
            const std::size_t earlyCount = std::min(kIrEarlySamples, resampledL.size());
            std::vector<float> earlyL(resampledL.begin(), resampledL.begin() + static_cast<std::ptrdiff_t>(earlyCount));
            kernel.left = dsp::PartitionedConvolver::buildKernelFromIr(
                earlyL, kBlockSize, kFftSize);
            if (resampledL.size() > earlyCount) {
                std::vector<float> lateL(resampledL.begin() + static_cast<std::ptrdiff_t>(earlyCount), resampledL.end());
                kernel.leftLate = dsp::PartitionedConvolver::buildKernelFromIr(
                    lateL, kLateBlockSize, kLateFftSize);
                kernel.hasLate = !kernel.leftLate.partitions.empty();
            }
            if (stereo && !treatStereoAsMono) {
                const std::size_t earlyCountR = std::min(kIrEarlySamples, resampledR.size());
                std::vector<float> earlyR(resampledR.begin(), resampledR.begin() + static_cast<std::ptrdiff_t>(earlyCountR));
                kernel.right = dsp::PartitionedConvolver::buildKernelFromIr(
                    earlyR, kBlockSize, kFftSize);
                if (resampledR.size() > earlyCountR) {
                    std::vector<float> lateR(resampledR.begin() + static_cast<std::ptrdiff_t>(earlyCountR), resampledR.end());
                    kernel.rightLate = dsp::PartitionedConvolver::buildKernelFromIr(
                        lateR, kLateBlockSize, kLateFftSize);
                    kernel.hasLate = kernel.hasLate || !kernel.rightLate.partitions.empty();
                }
                kernel.isStereo = true;
            }
            kernelsOut.push_back(std::move(kernel));
        }
    }

    void updateOfflineDetection() {
        if (offlineDetected_) {
            useSynchronous_ = true;
            return;
        }

        processedFramesForTiming_ += kBlockSize;
        const auto now = std::chrono::steady_clock::now();
        const auto elapsed = now - timingStart_;
        const auto elapsedSeconds =
            std::chrono::duration_cast<std::chrono::duration<double>>(elapsed).count();
        if (elapsedSeconds <= 0.0) {
            return;
        }
        const double framesPerSecond =
            static_cast<double>(processedFramesForTiming_) / elapsedSeconds;
        const double sr =
            std::max(1.0, requestedSampleRate_.load(std::memory_order_relaxed));
        const double speedRatio = framesPerSecond / sr;
        if (speedRatio > 8.0) {
            ++fastBlockStreak_;
        } else {
            fastBlockStreak_ = 0;
        }
        if (fastBlockStreak_ >= 2) {
            offlineDetected_ = true;
            useSynchronous_ = true;
        }
    }

    void syncApplyPendingState() {
        const std::uint64_t srSeq = sampleRateSeq_.load(std::memory_order_acquire);
        if (srSeq != syncSampleRateSeq_) {
            const double requested = requestedSampleRate_.load(std::memory_order_acquire);
            if (requested > 0.0) {
                syncSampleRate_ = requested;
                rebuildKernels(syncSampleRate_, syncKernels_);
                syncReverb_.setMix(1.0f);  // wet-only; mix is applied on the audio thread.
                syncReverb_.setSampleRate(syncSampleRate_);
                syncReverb_.setIrKernels(syncKernels_);
                syncBuiltOnce_ = true;
                builtOnce_.store(true, std::memory_order_release);
            }
            syncSampleRateSeq_ = srSeq;
            syncIrIndex_ = -1;
        }

        const std::uint64_t rstSeq = resetSeq_.load(std::memory_order_acquire);
        if (rstSeq != syncResetSeq_) {
            syncReverb_.reset();
            syncResetSeq_ = rstSeq;
            syncIrIndex_ = -1;
        }
        const int irIndex = requestedIrIndex_.load(std::memory_order_relaxed);
        if (irIndex != syncIrIndex_) {
            syncReverb_.setIrIndex(irIndex);
            syncIrIndex_ = irIndex;
        }
    }

    void syncProcess(float input, float& outL, float& outR) {
        syncApplyPendingState();
        // Output wet-only; dry/mix is applied by the caller.
        syncReverb_.processSample(input, outL, outR);
    }

    void startWorker() {
        if (running_.exchange(true, std::memory_order_acq_rel)) {
            return;
        }
        worker_ = std::thread(&RoomProcessor::workerLoop, this);
    }

    void stopWorker() {
        if (!running_.exchange(false, std::memory_order_acq_rel)) {
            return;
        }
        dataReady_.notify_one();
        if (worker_.joinable()) {
            worker_.join();
        }
    }

    void workerLoop() {
        dsp::ScopedDenormalsDisable denormalsGuard;

        double currentSampleRate = 44100.0;
        std::uint64_t currentSampleRateSeq = 0;
        std::uint64_t currentResetSeq = 0;
        int currentIrIndex = -1;

        // Worker produces wet-only blocks.
        reverb_.setMix(1.0f);

        auto applyPendingState = [&] {
            const std::uint64_t srSeq = sampleRateSeq_.load(std::memory_order_acquire);
            if (srSeq != currentSampleRateSeq) {
                const double requested =
                    requestedSampleRate_.load(std::memory_order_acquire);
                if (requested > 0.0) {
                    currentSampleRate = requested;
                    rebuildKernels(currentSampleRate, kernels_);
                    reverb_.setMix(1.0f);
                    reverb_.setSampleRate(currentSampleRate);
                    reverb_.setIrKernels(kernels_);
                    builtOnce_.store(true, std::memory_order_release);
                }
                currentSampleRateSeq = srSeq;
                // Force re-apply for the new instance/state.
                currentIrIndex = -1;
            }

            const std::uint64_t rstSeq = resetSeq_.load(std::memory_order_acquire);
            if (rstSeq != currentResetSeq) {
                reverb_.reset();
                currentResetSeq = rstSeq;
                // Re-apply params after reset.
                currentIrIndex = -1;
            }
            const int irIndex = requestedIrIndex_.load(std::memory_order_relaxed);
            if (irIndex != currentIrIndex) {
                reverb_.setIrIndex(irIndex);
                currentIrIndex = irIndex;
            }
        };

        DryBlock dry{};
        std::array<float, kBlockSize> wetL{};
        std::array<float, kBlockSize> wetR{};
        while (running_.load(std::memory_order_acquire)) {
            if (!dryQueue_.pop(dry)) {
                std::unique_lock<std::mutex> lock(cvMutex_);
                dataReady_.wait(lock, [&] {
                    return !running_.load(std::memory_order_acquire) ||
                           pendingDryBlocks_.load(std::memory_order_acquire) > 0;
                });
                continue;
            }
            pendingDryBlocks_.fetch_sub(1, std::memory_order_acq_rel);

            applyPendingState();

            StereoBlock wet{};
            wet.seq = dry.seq;
            reverb_.processBlockWet(dry.samples.data(), wetL.data(), wetR.data());
            for (std::size_t i = 0; i < kBlockSize; ++i) {
                wet.samples[i * 2] = wetL[i];
                wet.samples[i * 2 + 1] = wetR[i];
            }
            (void)wetQueue_.push(wet);
        }
    }

    // Audio-thread state.
    DryBlock dryAccum_{};
    std::size_t blockPos_ = 0;
    StereoBlock wetBlock_{};
    bool haveWetBlock_ = false;
    DryBlock playDryBlock_{};
    bool havePlayDryBlock_ = false;
    std::array<DryBlock, kDryHistoryBlocks> dryHistory_{};
    std::uint64_t outputSeq_ = 0;
    bool haveOutputSeq_ = false;
    std::optional<StereoBlock> bufferedWet_{};
    std::uint64_t nextSeq_ = 0;
    float mixSmoothingAlpha_ = 1.0f;
    float currentMix_ = 0.0f;
    float lastTargetMix_ = 0.0f;

    // Auto-detect offline rendering (e.g. unit tests) and fall back to synchronous
    // processing so the DSP remains deterministic when processed faster than realtime.
    bool offlineDetected_ = false;
    bool useSynchronous_ = false;
    std::chrono::steady_clock::time_point timingStart_ = std::chrono::steady_clock::now();
    std::size_t processedFramesForTiming_ = 0;
    int fastBlockStreak_ = 0;

    // Synchronous fallback state (audio thread).
    bool syncBuiltOnce_ = false;
    double syncSampleRate_ = 44100.0;
    std::uint64_t syncSampleRateSeq_ = 0;
    std::uint64_t syncResetSeq_ = 0;
    int syncIrIndex_ = -1;
    std::vector<dsp::StereoConvolutionKernel> syncKernels_{};
    dsp::ConvolutionReverb syncReverb_{};

    // Cross-thread queues (audio thread <-> reverb worker).
    SpscRing<DryBlock, kQueueCapacity> dryQueue_{};
    SpscRing<StereoBlock, kQueueCapacity> wetQueue_{};

    std::atomic<bool> running_{false};
    std::thread worker_{};
    std::condition_variable dataReady_{};
    std::mutex cvMutex_{};
    std::atomic<std::uint32_t> pendingDryBlocks_{0};

    // Control parameters set from any thread; applied on worker thread.
    std::atomic<double> requestedSampleRate_{44100.0};
    std::atomic<std::uint64_t> sampleRateSeq_{0};
    std::atomic<float> requestedMix_{0.0f};
    std::atomic<int> requestedIrIndex_{0};
    std::atomic<std::uint64_t> resetSeq_{0};
    std::atomic<bool> builtOnce_{false};

    std::vector<dsp::StereoConvolutionKernel> kernels_;
    dsp::ConvolutionReverb reverb_;
};

namespace {

constexpr float kVoiceSilenceThreshold = 1e-5f;
constexpr float kEnergyDecay = 0.995f;
constexpr float kEnvelopeFloor = 1e-5f;
constexpr double kDefaultAttackSecondsValue = 0.004;

class AmpEnvelope {
public:
    AmpEnvelope() = default;

    void setSampleRate(double sampleRate) {
        sampleRate_ = sampleRate > 0.0 ? sampleRate : sampleRate_;
        updateAttackSamples();
        updateReleaseSamples();
    }

    void setAttackSeconds(double seconds) {
        attackSeconds_ = std::max(0.0, seconds);
        updateAttackSamples();
    }

    void setReleaseSeconds(double seconds) {
        releaseSeconds_ = std::max(0.0, seconds);
        updateReleaseSamples();
    }

    void noteOn(float targetLevel) {
        targetLevel_ = std::max(0.0f, targetLevel);
        stageCursor_ = 0;
        if (attackSamples_ == 0) {
            level_ = targetLevel_;
            stage_ = Stage::Sustain;
        } else {
            level_ = 0.0f;
            stage_ = Stage::Attack;
        }
    }

    void noteOff() {
        if (stage_ == Stage::Idle) {
            return;
        }
        stage_ = Stage::Release;
        stageCursor_ = 0;
        updateReleaseSamples();
        releaseStartLevel_ = level_;
        if (releaseSamples_ == 0) {
            level_ = 0.0f;
            stage_ = Stage::Idle;
        }
    }

    float next() {
        switch (stage_) {
            case Stage::Idle:
                level_ = 0.0f;
                return level_;
            case Stage::Attack: {
                if (attackSamples_ == 0) {
                    level_ = targetLevel_;
                    stage_ = Stage::Sustain;
                    return level_;
                }
                const float t = static_cast<float>(stageCursor_ + 1) /
                                static_cast<float>(attackSamples_);
                level_ = targetLevel_ * std::min(1.0f, t);
                if (++stageCursor_ >= attackSamples_) {
                    stage_ = Stage::Sustain;
                    stageCursor_ = 0;
                }
                return level_;
            }
            case Stage::Sustain:
                level_ = targetLevel_;
                return level_;
            case Stage::Release: {
                if (releaseSamples_ == 0) {
                    level_ = 0.0f;
                    stage_ = Stage::Idle;
                    return level_;
                }
                const float t = static_cast<float>(stageCursor_) /
                                static_cast<float>(releaseSamples_);
                const float factor = std::max(0.0f, 1.0f - t);
                level_ = releaseStartLevel_ * factor;
                if (++stageCursor_ >= releaseSamples_ || level_ < kEnvelopeFloor) {
                    level_ = 0.0f;
                    stage_ = Stage::Idle;
                }
                return level_;
            }
        }
        return 0.0f;
    }

    bool isIdle() const { return stage_ == Stage::Idle; }
    bool isReleasing() const { return stage_ == Stage::Release; }
    float level() const { return level_; }

private:
    enum class Stage { Idle, Attack, Sustain, Release };

    void updateAttackSamples() {
        attackSamples_ = static_cast<std::size_t>(
            std::max(0.0, std::round(attackSeconds_ * sampleRate_)));
    }

    void updateReleaseSamples() {
        releaseSamples_ = static_cast<std::size_t>(
            std::max(0.0, std::round(releaseSeconds_ * sampleRate_)));
    }

    Stage stage_ = Stage::Idle;
    double sampleRate_ = 44100.0;
    double attackSeconds_ = kDefaultAttackSecondsValue;
    double releaseSeconds_ = 0.35;
    float level_ = 0.0f;
    float targetLevel_ = 1.0f;
    float releaseStartLevel_ = 0.0f;
    std::size_t stageCursor_ = 0;
    std::size_t attackSamples_ = 0;
    std::size_t releaseSamples_ = 0;
};

struct Voice {
    synthesis::KarplusStrongString string;
    AmpEnvelope envelope;
    int noteId = -1;
    double frequency = 0.0;
    float velocity = 1.0f;
    std::uint64_t age = 0;
    float energy = 0.0f;
};

}  // namespace

class StringSynthEngine::VoiceManager {
public:
    VoiceManager(std::size_t maxVoices, double sampleRate, double attackSeconds,
                 double releaseSeconds)
        : maxVoices_(maxVoices),
          sampleRate_(sampleRate > 0.0 ? sampleRate : 44100.0),
          attackSeconds_(attackSeconds),
          releaseSeconds_(releaseSeconds) {}

    void setSampleRate(double sampleRate) {
        if (sampleRate <= 0.0) {
            return;
        }
        sampleRate_ = sampleRate;
        for (auto& voice : voices_) {
            voice.envelope.setSampleRate(sampleRate_);
        }
    }

    void setAttackSeconds(double seconds) {
        attackSeconds_ = std::max(0.0, seconds);
        for (auto& voice : voices_) {
            voice.envelope.setAttackSeconds(attackSeconds_);
        }
    }

    void setReleaseSeconds(double seconds) {
        releaseSeconds_ = std::max(0.0, seconds);
        for (auto& voice : voices_) {
            voice.envelope.setReleaseSeconds(releaseSeconds_);
        }
    }

    void noteOn(int noteId, double frequency, float velocity,
                const synthesis::StringConfig& config) {
        if (frequency <= 0.0) {
            return;
        }
        Voice* voice = findVoiceByNote(noteId);
        if (!voice) {
            voice = allocateVoice();
        }
        if (!voice) {
            return;
        }
        voice->noteId = noteId;
        voice->frequency = frequency;
        voice->velocity = velocity;
        voice->age = ++ageCounter_;
        voice->energy = 0.0f;

        synthesis::StringConfig voiceConfig = config;
        const float amp = ExpressiveMapping::Apply(velocity, frequency, config, voiceConfig);
        voiceConfig.sampleRate = sampleRate_;
        voice->string.updateConfig(voiceConfig);
        voice->string.start(frequency, velocity);

        voice->envelope.setSampleRate(sampleRate_);
        voice->envelope.setAttackSeconds(attackSeconds_);
        voice->envelope.setReleaseSeconds(releaseSeconds_);
        voice->envelope.noteOn(amp);
    }

    void noteOff(int noteId) {
        if (noteId < 0) {
            return;
        }
        for (auto& voice : voices_) {
            if (voice.noteId == noteId) {
                voice.envelope.setReleaseSeconds(releaseSeconds_);
                voice.envelope.noteOff();
            }
        }
    }

    float renderFrame(float masterGain) {
        float mixed = 0.0f;
        for (auto& voice : voices_) {
            if (voice.envelope.isIdle()) {
                continue;
            }
            const float env = voice.envelope.next();
            const float sample = voice.string.processSample() * env * voice.velocity;
            voice.energy = kEnergyDecay * voice.energy +
                           (1.0f - kEnergyDecay) * std::abs(sample);
            mixed += sample;
        }

        cleanupSilentVoices();
        return mixed * masterGain;
    }

    std::size_t activeVoices() const { return voices_.size(); }

private:
    Voice* findVoiceByNote(int noteId) {
        auto it = std::find_if(
            voices_.begin(), voices_.end(),
            [noteId](const Voice& voice) { return voice.noteId == noteId; });
        if (it == voices_.end()) {
            return nullptr;
        }
        return &(*it);
    }

    Voice* allocateVoice() {
        if (voices_.size() < maxVoices_) {
            voices_.emplace_back();
            voices_.back().envelope.setSampleRate(sampleRate_);
            voices_.back().envelope.setAttackSeconds(attackSeconds_);
            voices_.back().envelope.setReleaseSeconds(releaseSeconds_);
            return &voices_.back();
        }
        // Voice stealing: prefer releasing voices, otherwise lowest energy, then oldest.
        auto candidate = std::min_element(
            voices_.begin(), voices_.end(),
            [](const Voice& a, const Voice& b) {
                if (a.envelope.isReleasing() != b.envelope.isReleasing()) {
                    return a.envelope.isReleasing();
                }
                if (std::abs(a.energy - b.energy) > std::numeric_limits<float>::epsilon()) {
                    return a.energy < b.energy;
                }
                return a.age < b.age;
            });
        if (candidate == voices_.end()) {
            return nullptr;
        }
        return &(*candidate);
    }

    void cleanupSilentVoices() {
        voices_.erase(
            std::remove_if(voices_.begin(), voices_.end(),
                           [](const Voice& voice) {
                               return voice.envelope.isIdle() ||
                                      (voice.envelope.isReleasing() &&
                                       voice.energy < kVoiceSilenceThreshold);
                           }),
            voices_.end());
    }

    std::vector<Voice> voices_;
    const std::size_t maxVoices_;
    double sampleRate_ = 44100.0;
    double attackSeconds_ = kDefaultAttackSecondsValue;
    double releaseSeconds_ = 0.35;
    std::uint64_t ageCounter_ = 0;
};

StringSynthEngine::StringSynthEngine(synthesis::StringConfig config)
    : config_(config) {
    if (const auto* info = GetParamInfo(ParamId::AmpRelease)) {
        ampReleaseSeconds_ = info->defaultValue;
    }
    voiceManager_ = std::make_unique<VoiceManager>(
        kMaxVoices, config_.sampleRate, kDefaultAttackSeconds, ampReleaseSeconds_);
    voiceManager_->setReleaseSeconds(ampReleaseSeconds_);
    bodyFilter_ = std::make_unique<BodyFilter>();
    bodyFilter_->setSampleRate(config_.sampleRate);
    bodyFilter_->setParams(config_.bodyTone, config_.bodySize);
    roomProcessor_ = std::make_unique<RoomProcessor>();
    roomProcessor_->setSampleRate(config_.sampleRate);
    roomProcessor_->setMix(config_.roomAmount);
    roomProcessor_->setIrIndex(config_.roomIrIndex);
}

StringSynthEngine::~StringSynthEngine() = default;

void StringSynthEngine::setConfig(const synthesis::StringConfig& config) {
    std::lock_guard<std::mutex> lock(mutex_);
    config_.sampleRate = config.sampleRate;
    config_.seed = config.seed;
    config_.excitationMode = config.excitationMode;
    config_.excitationType = config.excitationType;
    applyParamUnlocked(ParamId::Decay, static_cast<float>(config.decay), config_,
                       masterGain_);
    applyParamUnlocked(ParamId::Brightness, config.brightness, config_, masterGain_);
    applyParamUnlocked(ParamId::DispersionAmount, config.dispersionAmount, config_,
                       masterGain_);
    applyParamUnlocked(ParamId::ExcitationBrightness, config.excitationBrightness, config_,
                       masterGain_);
    applyParamUnlocked(ParamId::ExcitationVelocity, config.excitationVelocity, config_,
                       masterGain_);
    applyParamUnlocked(ParamId::ExcitationMix, config.excitationMix, config_,
                       masterGain_);
    applyParamUnlocked(ParamId::BodyTone, config.bodyTone, config_, masterGain_);
    applyParamUnlocked(ParamId::BodySize, config.bodySize, config_, masterGain_);
    applyParamUnlocked(ParamId::RoomAmount, config.roomAmount, config_, masterGain_);
    applyParamUnlocked(ParamId::RoomIR, static_cast<float>(config.roomIrIndex), config_,
                       masterGain_);
    applyParamUnlocked(ParamId::PickPosition, config.pickPosition, config_, masterGain_);
    applyParamUnlocked(ParamId::EnableLowpass, config.enableLowpass ? 1.0f : 0.0f,
                       config_, masterGain_);
    applyParamUnlocked(ParamId::NoiseType,
                       config.noiseType == synthesis::NoiseType::Binary ? 1.0f : 0.0f,
                       config_, masterGain_);
    voiceManager_->setSampleRate(config_.sampleRate);
    if (bodyFilter_) {
        bodyFilter_->setSampleRate(config_.sampleRate);
    }
    if (roomProcessor_) {
        roomProcessor_->setSampleRate(config_.sampleRate);
        roomProcessor_->setIrIndex(config_.roomIrIndex);
        roomProcessor_->setMix(config_.roomAmount);
    }
}

synthesis::StringConfig StringSynthEngine::stringConfig() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return config_;
}

void StringSynthEngine::setSampleRate(double sampleRate) {
    std::lock_guard<std::mutex> lock(mutex_);
    config_.sampleRate = sampleRate;
    voiceManager_->setSampleRate(config_.sampleRate);
    if (bodyFilter_) {
        bodyFilter_->setSampleRate(config_.sampleRate);
    }
    if (roomProcessor_) {
        roomProcessor_->setSampleRate(config_.sampleRate);
    }
}

double StringSynthEngine::sampleRate() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return config_.sampleRate;
}

void StringSynthEngine::enqueueEvent(const Event& event) {
    enqueueEventAt(event, frameCursor_.load(std::memory_order_relaxed));
}

void StringSynthEngine::enqueueEventAt(const Event& event,
                                       std::uint64_t frameOffset) {
    Event stamped = event;
    stamped.frameOffset = frameOffset;
    std::lock_guard<std::mutex> lock(mutex_);
    eventQueue_.push_back(stamped);
}

void StringSynthEngine::noteOn(int noteId, double frequency, float velocity,
                               double durationSeconds) {
    if (frequency <= 0.0) {
        return;
    }
    std::uint64_t startFrame = frameCursor_.load(std::memory_order_relaxed);
    double currentSampleRate = 0.0;
    int resolvedNoteId = noteId;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        currentSampleRate = config_.sampleRate;
        if (resolvedNoteId < 0) {
            resolvedNoteId = nextNoteId_++;
        }
    }

    Event event;
    event.type = EventType::NoteOn;
    event.noteId = resolvedNoteId;
    event.velocity = velocity;
    event.frequency = frequency;
    enqueueEventAt(event, startFrame);

    if (durationSeconds > 0.0 && currentSampleRate > 0.0) {
        const auto deltaFrames = static_cast<std::uint64_t>(
            std::max(0.0, std::round(durationSeconds * currentSampleRate)));
        Event off;
        off.type = EventType::NoteOff;
        off.noteId = resolvedNoteId;
        enqueueEventAt(off, startFrame + deltaFrames);
    }
}

void StringSynthEngine::noteOff(int noteId) {
    if (noteId < 0) {
        return;
    }
    Event event;
    event.type = EventType::NoteOff;
    event.noteId = noteId;
    enqueueEvent(event);
}

void StringSynthEngine::noteOn(double frequency, double durationSeconds) {
    noteOn(-1, frequency, 1.0f, durationSeconds);
}

void StringSynthEngine::setParam(ParamId id, float value) {
    std::lock_guard<std::mutex> lock(mutex_);
    applyParamUnlocked(id, value, config_, masterGain_);
}

float StringSynthEngine::getParam(ParamId id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    switch (id) {
        case ParamId::Decay:
            return static_cast<float>(config_.decay);
        case ParamId::Brightness:
            return config_.brightness;
        case ParamId::DispersionAmount:
            return config_.dispersionAmount;
        case ParamId::ExcitationBrightness:
            return config_.excitationBrightness;
        case ParamId::ExcitationVelocity:
            return config_.excitationVelocity;
        case ParamId::ExcitationMix:
            return config_.excitationMix;
        case ParamId::BodyTone:
            return config_.bodyTone;
        case ParamId::BodySize:
            return config_.bodySize;
        case ParamId::RoomAmount:
            return config_.roomAmount;
        case ParamId::RoomIR:
            return static_cast<float>(config_.roomIrIndex);
        case ParamId::PickPosition:
            return config_.pickPosition;
        case ParamId::EnableLowpass:
            return config_.enableLowpass ? 1.0f : 0.0f;
        case ParamId::NoiseType:
            return config_.noiseType == synthesis::NoiseType::Binary ? 1.0f : 0.0f;
        case ParamId::MasterGain:
            return masterGain_;
        case ParamId::AmpRelease:
            return static_cast<float>(ampReleaseSeconds_);
        default:
            return 0.0f;
    }
}

void StringSynthEngine::process(const ProcessBlock& block) {
    if (!block.output || block.frames == 0 || block.channels == 0) {
        return;
    }
    const std::size_t totalSamples = block.frames * block.channels;
    std::fill(block.output, block.output + totalSamples, 0.0f);

    const std::uint64_t blockStartFrame =
        frameCursor_.load(std::memory_order_relaxed);
    const std::uint64_t blockEndFrame = blockStartFrame + block.frames;

    std::vector<Event> pendingEvents;
    synthesis::StringConfig currentConfig{};
    float currentMasterGain = 1.0f;
    double currentAmpRelease = 0.0;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        currentConfig = config_;
        currentMasterGain = masterGain_;
        currentAmpRelease = ampReleaseSeconds_;
        pendingEvents.swap(eventQueue_);
    }

    voiceManager_->setSampleRate(currentConfig.sampleRate);
    voiceManager_->setReleaseSeconds(currentAmpRelease);

    std::vector<Event> readyEvents;
    std::vector<Event> futureEvents;
    readyEvents.reserve(pendingEvents.size());
    futureEvents.reserve(pendingEvents.size());

    for (auto& event : pendingEvents) {
        if (event.frameOffset <= blockStartFrame) {
            event.frameOffset = blockStartFrame;
            readyEvents.push_back(event);
        } else if (event.frameOffset < blockEndFrame) {
            readyEvents.push_back(event);
        } else {
            futureEvents.push_back(event);
        }
    }

    std::stable_sort(
        readyEvents.begin(), readyEvents.end(),
        [](const Event& a, const Event& b) { return a.frameOffset < b.frameOffset; });

    std::size_t eventIndex = 0;
    for (std::size_t frame = 0; frame < block.frames; ++frame) {
        const std::uint64_t absoluteFrame = blockStartFrame + frame;
        while (eventIndex < readyEvents.size() &&
               readyEvents[eventIndex].frameOffset <= absoluteFrame) {
            handleEvent(readyEvents[eventIndex], currentConfig, currentMasterGain,
                        currentAmpRelease);
            ++eventIndex;
        }

        float sample = voiceManager_->renderFrame(currentMasterGain);
        if (bodyFilter_) {
            sample = bodyFilter_->process(sample);
        }
        float left = sample;
        float right = sample;
        if (roomProcessor_) {
            roomProcessor_->process(sample, left, right);
        }
        if (block.channels >= 2) {
            block.output[frame * block.channels] += left;
            block.output[frame * block.channels + 1] += right;
            for (uint16_t ch = 2; ch < block.channels; ++ch) {
                block.output[frame * block.channels + ch] += sample;
            }
        } else if (block.channels == 1) {
            block.output[frame] += 0.5f * (left + right);
        }
    }

    frameCursor_.fetch_add(block.frames, std::memory_order_relaxed);

    {
        std::lock_guard<std::mutex> lock(mutex_);
        config_ = currentConfig;
        masterGain_ = currentMasterGain;
        ampReleaseSeconds_ = currentAmpRelease;
        if (!futureEvents.empty()) {
            eventQueue_.insert(eventQueue_.end(),
                               std::make_move_iterator(futureEvents.begin()),
                               std::make_move_iterator(futureEvents.end()));
            std::stable_sort(
                eventQueue_.begin(), eventQueue_.end(),
                [](const Event& a, const Event& b) {
                    return a.frameOffset < b.frameOffset;
                });
        }
    }
}

std::size_t StringSynthEngine::activeVoiceCount() const {
    return voiceManager_ ? voiceManager_->activeVoices() : 0;
}

std::size_t StringSynthEngine::queuedEventCount() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return eventQueue_.size();
}

std::uint64_t StringSynthEngine::renderedFrames() const {
    return frameCursor_.load(std::memory_order_relaxed);
}

std::vector<std::uint64_t> StringSynthEngine::queuedEventFrames() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<std::uint64_t> frames;
    frames.reserve(eventQueue_.size());
    for (const auto& event : eventQueue_) {
        frames.push_back(event.frameOffset);
    }
    return frames;
}

void StringSynthEngine::handleEvent(const Event& event,
                                    synthesis::StringConfig& config,
                                    float& masterGain,
                                    double& ampRelease) {
    switch (event.type) {
        case EventType::NoteOn:
            voiceManager_->noteOn(event.noteId, event.frequency, event.velocity, config);
            break;
        case EventType::NoteOff:
            voiceManager_->noteOff(event.noteId);
            break;
        case EventType::ParamChange:
            applyParamUnlocked(event.param, event.paramValue, config, masterGain);
            ampRelease = ampReleaseSeconds_;
            voiceManager_->setReleaseSeconds(ampRelease);
            break;
        default:
            break;
    }
}

void StringSynthEngine::applyParamUnlocked(ParamId id, float value,
                                           synthesis::StringConfig& config,
                                           float& masterGain) {
    const auto* info = GetParamInfo(id);
    if (!info) {
        return;
    }
    const float clamped = ClampToRange(*info, value);
    switch (id) {
        case ParamId::Decay:
            config.decay = clamped;
            break;
        case ParamId::Brightness:
            config.brightness = clamped;
            break;
        case ParamId::DispersionAmount:
            config.dispersionAmount = clamped;
            break;
        case ParamId::ExcitationBrightness:
            config.excitationBrightness = clamped;
            break;
        case ParamId::ExcitationVelocity:
            config.excitationVelocity = clamped;
            break;
        case ParamId::ExcitationMix:
            config.excitationMix = clamped;
            break;
        case ParamId::BodyTone:
            config.bodyTone = clamped;
            if (bodyFilter_) {
                bodyFilter_->setParams(config.bodyTone, config.bodySize);
            }
            break;
        case ParamId::BodySize:
            config.bodySize = clamped;
            if (bodyFilter_) {
                bodyFilter_->setParams(config.bodyTone, config.bodySize);
            }
            break;
        case ParamId::RoomAmount:
            config.roomAmount = clamped;
            if (roomProcessor_) {
                roomProcessor_->setMix(config.roomAmount);
            }
            break;
        case ParamId::RoomIR:
            config.roomIrIndex = static_cast<int>(std::lround(clamped));
            if (roomProcessor_) {
                roomProcessor_->setIrIndex(config.roomIrIndex);
            }
            break;
        case ParamId::PickPosition:
            config.pickPosition = clamped;
            break;
        case ParamId::EnableLowpass:
            config.enableLowpass = clamped >= 0.5f;
            break;
        case ParamId::NoiseType:
            config.noiseType =
                (clamped >= 0.5f) ? synthesis::NoiseType::Binary : synthesis::NoiseType::White;
            break;
        case ParamId::MasterGain:
            masterGain = clamped;
            break;
        case ParamId::AmpRelease:
            ampReleaseSeconds_ = clamped;
            if (voiceManager_) {
                voiceManager_->setReleaseSeconds(ampReleaseSeconds_);
            }
            break;
        default:
            break;
    }
}

}  // namespace engine
