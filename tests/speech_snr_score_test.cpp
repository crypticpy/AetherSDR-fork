// Standalone test harness for SpeechSnrScore. CMake target
// `speech_snr_score_test`. Exit 0 = pass.
//
// Ground truth is built independently of SpeechSnrScore's internals: a
// synthetic speech-shaped burst-and-gap signal is generated, then the tone
// (speech-shaped bursts) and noise are also measured in isolation through
// the same 300-2700 Hz bandpass shape SpeechSnrScore itself uses, giving a
// "true" SNR to compare the blind estimator against. Acceptance: the blind
// estimate on the combined signal is within 2 dB of that reference.

#include "core/Biquad.h"
#include "core/SpeechSnrScore.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

using AetherSDR::Biquad;
using AetherSDR::SpeechSnrScore;

namespace {

constexpr double kSampleRate = 24000.0;
constexpr float kPi = 3.14159265358979323846f;
constexpr int kMaxHarmonic = 20;  // 20 * 140 Hz = 2800 Hz, spans most of the band

// Same band SpeechSnrScore.cpp bandpasses to (300-2700 Hz, Q ~ 0.7071) —
// duplicated here deliberately so this test's "ground truth" measures the
// same passband the estimator does, rather than an idealized full-band
// power ratio the estimator was never going to reproduce.
constexpr double kBandLowHz = 300.0;
constexpr double kBandHighHz = 2700.0;
constexpr double kBandQ = 0.70710678;

// ~2.5 s: 6 voiced bursts of 300 ms separated by 120 ms noise-only gaps,
// aligned to 20 ms so the duty cycle matches SpeechSnrScore's own framing
// exactly (gap fraction ~29% > the 20% noise percentile; speech fraction
// ~71% > the 50% speech percentile — no percentile bucket can be
// contaminated by the wrong region).
constexpr int kFrameSamples20ms = static_cast<int>(kSampleRate) / 50;  // 480
constexpr int kBurstFrames20ms = 15;  // 300 ms
constexpr int kGapFrames20ms = 6;     // 120 ms
constexpr int kRepeats = 6;
constexpr int kTotalFrames20ms = kRepeats * (kBurstFrames20ms + kGapFrames20ms);
constexpr int kTotalSamples = kTotalFrames20ms * kFrameSamples20ms;

int g_failed = 0;

void report(const char* name, bool ok, const std::string& detail)
{
    std::printf("%s %-60s %s\n", ok ? "[ OK ]" : "[FAIL]", name, detail.c_str());
    if (!ok) {
        ++g_failed;
    }
}

uint32_t xorshift32(uint32_t& state)
{
    state ^= state << 13;
    state ^= state >> 17;
    state ^= state << 5;
    return state;
}

float voicedSample(int sampleIndex, double sampleRate)
{
    constexpr float kF0 = 140.0f;
    const float seconds = static_cast<float>(sampleIndex / sampleRate);
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

float scaledVoicedSample(float toneGain, int sampleIndex)
{
    static const float kPeak = harmonicPeakSum();
    return (toneGain / kPeak) * voicedSample(sampleIndex, kSampleRate);
}

// Bandpass a signal with the same corner frequencies/Q SpeechSnrScore uses,
// for computing this test's independent ground truth.
std::vector<float> bandpassSpeechBand(const std::vector<float>& samples)
{
    Biquad highPass;
    Biquad lowPass;
    highPass.setCoefficients(Biquad::Type::HighPass, kSampleRate, kBandLowHz, kBandQ);
    lowPass.setCoefficients(Biquad::Type::LowPass, kSampleRate, kBandHighHz, kBandQ);
    std::vector<float> filtered(samples.size());
    highPass.process(samples.data(), filtered.data(), static_cast<int>(samples.size()));
    lowPass.process(filtered.data(), filtered.data(), static_cast<int>(samples.size()));
    return filtered;
}

// Mean power of `filtered` restricted to burst frames (wantBurst=true) or gap
// frames (wantBurst=false), using the same 20 ms frame grid the signal was
// built from. Restricting to the matching region — rather than averaging
// over the whole timeline — mirrors what the blind pause-based estimator
// itself measures (its "speech+noise" bucket is built from high-energy
// frames, i.e. bursts only; its "noise" bucket from low-energy frames, i.e.
// gaps only), so this is the correct reference to compare it against.
double meanPowerInRegion(const std::vector<float>& filtered, bool wantBurst)
{
    double sum = 0.0;
    long count = 0;
    for (int frame20 = 0; frame20 < kTotalFrames20ms; ++frame20) {
        const int cyclePos = frame20 % (kBurstFrames20ms + kGapFrames20ms);
        const bool inBurst = cyclePos < kBurstFrames20ms;
        if (inBurst != wantBurst) {
            continue;
        }
        const int start = frame20 * kFrameSamples20ms;
        for (int i = 0; i < kFrameSamples20ms; ++i) {
            const double s = filtered[static_cast<std::size_t>(start + i)];
            sum += s * s;
            ++count;
        }
    }
    return count > 0 ? sum / static_cast<double>(count) : 0.0;
}

// Builds the combined (tone-in-bursts + continuous noise) signal, and
// separately the tone-only and noise-only components at matching sample
// positions so a reference SNR can be measured independently of
// SpeechSnrScore's internals.
struct SyntheticSignal {
    std::vector<float> combined;
    std::vector<float> toneOnly;   // zero during gaps, matching combined's burst placement
    std::vector<float> noiseOnly;  // the same noise realization used in combined
};

SyntheticSignal makeSignal(float toneGain, float noiseAmplitude)
{
    SyntheticSignal sig;
    sig.combined.resize(kTotalSamples);
    sig.toneOnly.resize(kTotalSamples);
    sig.noiseOnly.resize(kTotalSamples);

    uint32_t rngState = 0x9e3779b9U;
    for (int frame20 = 0; frame20 < kTotalFrames20ms; ++frame20) {
        const int cyclePos = frame20 % (kBurstFrames20ms + kGapFrames20ms);
        const bool inBurst = cyclePos < kBurstFrames20ms;
        for (int i = 0; i < kFrameSamples20ms; ++i) {
            const int idx = frame20 * kFrameSamples20ms + i;
            const uint32_t rnd = xorshift32(rngState);
            const float noise =
                (static_cast<float>(rnd & 0xffffU) / 32767.5f - 1.0f) * noiseAmplitude;
            const float voice = inBurst ? scaledVoicedSample(toneGain, idx) : 0.0f;
            sig.toneOnly[static_cast<std::size_t>(idx)] = voice;
            sig.noiseOnly[static_cast<std::size_t>(idx)] = noise;
            sig.combined[static_cast<std::size_t>(idx)] =
                std::clamp(voice + noise, -1.0f, 1.0f);
        }
    }
    return sig;
}

bool testEstimateMatchesKnownSnrWithinTolerance()
{
    constexpr float kToneGain = 0.30f;
    constexpr float kNoiseAmplitude = 0.06f;
    constexpr double kToleranceDb = 2.0;

    const SyntheticSignal sig = makeSignal(kToneGain, kNoiseAmplitude);

    const double tonePower = meanPowerInRegion(bandpassSpeechBand(sig.toneOnly), true);
    const double noisePower = meanPowerInRegion(bandpassSpeechBand(sig.noiseOnly), false);
    const double trueSnrDb = 10.0 * std::log10(tonePower / noisePower);

    const float estimatedSnrDb =
        SpeechSnrScore::estimateSnrDb(sig.combined, static_cast<int>(kSampleRate));
    const double error = std::abs(estimatedSnrDb - trueSnrDb);

    char detail[160];
    std::snprintf(detail, sizeof(detail),
                  "true=%.2f dB estimated=%.2f dB error=%.2f dB (limit %.1f)",
                  trueSnrDb, estimatedSnrDb, error, kToleranceDb);
    const bool ok = error <= kToleranceDb;
    report("estimateSnrDb matches independently-measured SNR", ok, detail);
    return ok;
}

bool testTooShortBlockReturnsZero()
{
    const std::vector<float> tooShort(200, 0.1f);  // well under kMinFrames * 480
    const float result =
        SpeechSnrScore::estimateSnrDb(tooShort, static_cast<int>(kSampleRate));
    const bool ok = result == 0.0f;
    report("too-short block returns 0 dB rather than garbage", ok,
          ok ? std::string() : "result=" + std::to_string(result));
    return ok;
}

bool testSilenceDoesNotCrashOrReportPositiveSnr()
{
    const std::vector<float> silence(static_cast<std::size_t>(kTotalSamples), 0.0f);
    const float result =
        SpeechSnrScore::estimateSnrDb(silence, static_cast<int>(kSampleRate));
    // All frames tie at zero energy: speech+noise cannot exceed the noise
    // floor, so this must hit the fixed floor, never a positive "SNR".
    const bool ok = result <= 0.0f;
    report("pure silence never reports a positive SNR", ok,
          ok ? std::string() : "result=" + std::to_string(result));
    return ok;
}

} // namespace

int main()
{
    testEstimateMatchesKnownSnrWithinTolerance();
    testTooShortBlockReturnsZero();
    testSilenceDoesNotCrashOrReportPositiveSnr();

    if (g_failed > 0) {
        std::printf("%d speech_snr_score_test check(s) failed\n", g_failed);
        return 1;
    }
    std::printf("speech_snr_score_test passed\n");
    return 0;
}
