// Offline, deterministic ctest for DFNR (DeepFilterFilter). Mirrors the shape
// of tests/rnnoise_filter_test.cpp: construct the filter, feed it synthetic
// audio, measure. Unlike RNNoise, DFNR's model is a large runtime-located
// asset (DeepFilterFilter.cpp's findModelPath()), not something this repo
// vendors as source — so if it isn't present at test time, that's a build/
// packaging fact, not a regression, and this test must SKIP cleanly rather
// than fail (see main()).

#ifdef HAVE_DFNR

#include "core/DeepFilterFilter.h"
#include "core/SpeechSnrScore.h"

#include <QByteArray>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <vector>

using AetherSDR::DeepFilterFilter;
using AetherSDR::SpeechSnrScore;

namespace {

constexpr int kSampleRate = 24000;
constexpr int kFrameSamples20ms = kSampleRate / 50;  // 480 samples = 20 ms
constexpr float kPi = 3.14159265358979323846f;
constexpr int kMaxHarmonic = 20;  // 20 * 120 Hz = 2400 Hz, inside the speech band

// ~840 ms: 3 voiced bursts of 300 ms separated by 120 ms gaps of noise-only
// audio, aligned to 20 ms boundaries so the duty cycle lines up exactly with
// SpeechSnrScore's own 20 ms framing (no partial-frame edge contamination).
constexpr int kBurstFrames20ms = 15;  // 300 ms
constexpr int kGapFrames20ms = 6;     // 120 ms
constexpr int kRepeats = 3;
constexpr int kTotalFrames20ms = kRepeats * (kBurstFrames20ms + kGapFrames20ms);
constexpr int kTotalSamples = kTotalFrames20ms * kFrameSamples20ms;

// Synthetic "voiced" tone: a fundamental plus a harmonic series with 1/k
// rolloff — the standard toy spectral model of voiced speech, not a
// recording. F0 = 120 Hz sits below the 300 Hz band-floor SpeechSnrScore
// measures, same as real radio audio: most of what a bandpass-limited
// receiver hears from a voice is harmonics, not the fundamental itself.
float voicedSample(int sampleIndex, int sampleRate)
{
    constexpr float kF0 = 120.0f;
    const float seconds = static_cast<float>(sampleIndex) / sampleRate;
    float sum = 0.0f;
    for (int k = 1; k <= kMaxHarmonic; ++k) {
        sum += (1.0f / static_cast<float>(k))
            * std::sin(2.0f * kPi * kF0 * static_cast<float>(k) * seconds);
    }
    return sum;
}

float harmonicPeakSum()
{
    float sum = 0.0f;
    for (int k = 1; k <= kMaxHarmonic; ++k) {
        sum += 1.0f / static_cast<float>(k);
    }
    return sum;
}

// toneGain is an approximate peak ceiling (harmonics rarely all align in
// phase, so the typical amplitude is well below toneGain).
float scaledVoicedSample(float toneGain, int sampleIndex, int sampleRate)
{
    static const float kPeak = harmonicPeakSum();
    return (toneGain / kPeak) * voicedSample(sampleIndex, sampleRate);
}

uint32_t xorshift32(uint32_t& state)
{
    state ^= state << 13;
    state ^= state >> 17;
    state ^= state << 5;
    return state;
}

// Interleaved stereo (L==R) float32 PCM: kRepeats cycles of a voiced burst
// (tone + white noise) followed by a noise-only gap.
QByteArray makeVoicedNoisyStereo(float toneGain, float noiseAmplitude)
{
    QByteArray block(kTotalSamples * 2 * static_cast<int>(sizeof(float)),
                     Qt::Uninitialized);
    auto* samples = reinterpret_cast<float*>(block.data());
    uint32_t rngState = 0x2545f491U;

    for (int frame20 = 0; frame20 < kTotalFrames20ms; ++frame20) {
        const int cyclePos = frame20 % (kBurstFrames20ms + kGapFrames20ms);
        const bool inBurst = cyclePos < kBurstFrames20ms;
        for (int i = 0; i < kFrameSamples20ms; ++i) {
            const int sampleIndex = frame20 * kFrameSamples20ms + i;
            const uint32_t rnd = xorshift32(rngState);
            const float noise =
                (static_cast<float>(rnd & 0xffffU) / 32767.5f - 1.0f) * noiseAmplitude;
            const float voice = inBurst
                ? scaledVoicedSample(toneGain, sampleIndex, kSampleRate)
                : 0.0f;
            const float s = std::clamp(voice + noise, -1.0f, 1.0f);
            samples[sampleIndex * 2] = s;
            samples[sampleIndex * 2 + 1] = s;
        }
    }
    return block;
}

std::vector<float> leftChannel(const QByteArray& stereo)
{
    const auto* samples = reinterpret_cast<const float*>(stereo.constData());
    const int frames = stereo.size() / (2 * static_cast<int>(sizeof(float)));
    std::vector<float> left(static_cast<std::size_t>(frames));
    for (int i = 0; i < frames; ++i) {
        left[static_cast<std::size_t>(i)] = samples[i * 2];
    }
    return left;
}

int firstAudibleLeftFrame(const QByteArray& stereo, float threshold)
{
    const auto* samples = reinterpret_cast<const float*>(stereo.constData());
    const int frames = stereo.size() / (2 * static_cast<int>(sizeof(float)));
    for (int i = 0; i < frames; ++i) {
        if (std::abs(samples[i * 2]) > threshold) {
            return i;
        }
    }
    return -1;
}

// Process a whole block through arbitrary chunk sizes, proving the filter is
// correct under any caller chunking (same intent as rnnoise_filter_test's
// processInBlocks) rather than just the app's own block size.
QByteArray processInBlocks(DeepFilterFilter& filter, const QByteArray& input,
                           const std::vector<int>& blockFrames)
{
    constexpr int kStereoFrameBytes = 2 * static_cast<int>(sizeof(float));
    const int totalFrames = input.size() / kStereoFrameBytes;
    QByteArray output;
    output.reserve(input.size());
    int offsetFrames = 0;
    int blockIndex = 0;
    while (offsetFrames < totalFrames) {
        const int requestedFrames = blockFrames[blockIndex % blockFrames.size()];
        const int frames = std::min(requestedFrames, totalFrames - offsetFrames);
        output.append(filter.process(input.mid(offsetFrames * kStereoFrameBytes,
                                               frames * kStereoFrameBytes)));
        offsetFrames += frames;
        ++blockIndex;
    }
    return output;
}

// Requirement (c): when the model isn't loaded, DeepFilterFilter::process()
// documents itself as an exact pass-through (DeepFilterFilter.cpp: "if
// (!m_state ...) return pcm24kStereo;"). That guard is the only "disabled"
// state the class itself exposes — there is no separate bypass flag; the
// app's AudioEngine decides whether to call process() at all. It's exercised
// naturally whenever the model file isn't found, which is also the SKIP
// condition in main() below, so this doubles as the bypass check.
bool verifyBypassPassthrough(DeepFilterFilter& filter)
{
    const QByteArray input = makeVoicedNoisyStereo(0.30f, 0.05f);
    const QByteArray output = processInBlocks(filter, input, {73, 480, 211, 17, 604});
    if (output != input) {
        std::printf("DFNR bypass (model unavailable) was not bit-identical "
                    "pass-through\n");
        return false;
    }
    return true;
}

bool testOutputLengthAndLatency(DeepFilterFilter& filter)
{
    // Silence, then a voiced burst — measures how many samples DFNR holds
    // back before the first audible output (resample/accumulator buffering
    // plus the model's own lookahead).
    constexpr int kLeadInFrames20ms = 20;  // 400 ms silence
    constexpr int kBurstOnlyFrames20ms = 40;  // 800 ms voiced
    constexpr int kTotalTestFrames20ms = kLeadInFrames20ms + kBurstOnlyFrames20ms;
    constexpr int kLeadInSamples = kLeadInFrames20ms * kFrameSamples20ms;
    constexpr int kTotalTestSamples = kTotalTestFrames20ms * kFrameSamples20ms;

    QByteArray input(kTotalTestSamples * 2 * static_cast<int>(sizeof(float)), '\0');
    auto* samples = reinterpret_cast<float*>(input.data());
    for (int i = kLeadInSamples; i < kTotalTestSamples; ++i) {
        const float s = scaledVoicedSample(0.4f, i, kSampleRate);
        samples[i * 2] = s;
        samples[i * 2 + 1] = s;
    }

    const QByteArray output = processInBlocks(filter, input, {137, 480, 91, 213});
    if (output.size() != input.size()) {
        std::printf("DFNR changed output length: got %lld expected %lld bytes\n",
                    static_cast<long long>(output.size()),
                    static_cast<long long>(input.size()));
        return false;
    }

    // Documented delay is a few DFNR hops (DeepFilterFilter.cpp comment: "3
    // hops at 48kHz" ~= 30 ms), plus this filter's own resample/accumulator
    // buffering (Resampler prewarm()s its own group delay at construction,
    // but the round trip through 24->48->24 kHz still costs some samples —
    // rnnoise_filter_test.cpp measures ~150-190 ms for a comparable resample
    // round trip). Bound generously as a regression guard against a broken
    // output FIFO or a latency blowup, not a pin to the exact hop count.
    constexpr int kMaxReasonableLatencySamples = kSampleRate;  // 1000 ms
    const int firstAudible = firstAudibleLeftFrame(output, 1.0e-4f);
    if (firstAudible < 0) {
        std::printf("DFNR output stayed silent through the entire voiced burst\n");
        return false;
    }
    const int delayFromBurstOnset = firstAudible - kLeadInSamples;
    if (delayFromBurstOnset > kMaxReasonableLatencySamples) {
        std::printf("DFNR latency exceeded the sanity bound: %d samples (limit %d)\n",
                    delayFromBurstOnset, kMaxReasonableLatencySamples);
        return false;
    }
    std::printf("DFNR output length OK (%lld bytes), latency from burst onset "
                "%d samples (%.1f ms)\n",
                static_cast<long long>(output.size()), delayFromBurstOnset,
                1000.0 * delayFromBurstOnset / kSampleRate);
    return true;
}

bool testNoiseReduction(DeepFilterFilter& filter)
{
    constexpr float kToneGain = 0.30f;
    constexpr float kNoiseAmplitude = 0.12f;
    constexpr float kMinImprovementDb = 3.0f;

    const QByteArray input = makeVoicedNoisyStereo(kToneGain, kNoiseAmplitude);
    const QByteArray output = processInBlocks(filter, input, {480});

    const float inputSnr = SpeechSnrScore::estimateSnrDb(leftChannel(input), kSampleRate);
    const float outputSnr = SpeechSnrScore::estimateSnrDb(leftChannel(output), kSampleRate);
    const float improvement = outputSnr - inputSnr;
    std::printf("DFNR speech-band SNR: input=%.2f dB output=%.2f dB (+%.2f dB)\n",
                inputSnr, outputSnr, improvement);
    if (improvement < kMinImprovementDb) {
        std::printf("DFNR did not improve speech-band SNR by at least %.1f dB\n",
                    kMinImprovementDb);
        return false;
    }
    return true;
}

} // namespace

int main()
{
    DeepFilterFilter filter;
    if (!filter.isValid()) {
        std::printf(
            "DFNR model not available in this build/test environment "
            "(DeepFilterFilter::isValid() == false) -- a build/packaging "
            "fact, not a regression signal. Verifying the bypass "
            "pass-through this state guarantees, then skipping the "
            "model-dependent checks.\n");
        if (!verifyBypassPassthrough(filter)) {
            return 1;
        }
        std::printf(
            "deep_filter_filter_test skipped (no model) -- bypass "
            "pass-through verified\n");
        return 0;
    }

    if (!testOutputLengthAndLatency(filter)) {
        return 1;
    }
    filter.reset();  // clean buffers/state between independent checks
    if (!testNoiseReduction(filter)) {
        return 1;
    }
    std::printf("deep_filter_filter_test passed\n");
    return 0;
}

#else  // !HAVE_DFNR

#include <cstdio>

int main()
{
    std::printf(
        "deep_filter_filter_test skipped: built without HAVE_DFNR "
        "(ENABLE_DFNR is off for this build)\n");
    return 0;
}

#endif  // HAVE_DFNR
