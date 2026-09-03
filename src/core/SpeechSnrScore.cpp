#include "SpeechSnrScore.h"
#include "Biquad.h"

#include <algorithm>
#include <cmath>

namespace AetherSDR {

namespace {

constexpr double kBandLowHz = 300.0;
constexpr double kBandHighHz = 2700.0;
constexpr double kBandQ = 0.70710678;  // Butterworth-ish corner, matches Biquad's Q convention
constexpr double kFrameMs = 20.0;
constexpr int kMinFrames = 5;
constexpr float kFloorDb = -60.0f;     // Reported when speech+noise never exceeds the noise floor
constexpr double kEnergyEpsilon = 1.0e-12;

std::vector<float> bandpassSpeechBand(const float* samples, int n, int sampleRateHz)
{
    Biquad highPass;
    Biquad lowPass;
    highPass.setCoefficients(Biquad::Type::HighPass, sampleRateHz, kBandLowHz, kBandQ);
    lowPass.setCoefficients(Biquad::Type::LowPass, sampleRateHz, kBandHighHz, kBandQ);
    std::vector<float> filtered(static_cast<std::size_t>(n));
    highPass.process(samples, filtered.data(), n);
    lowPass.process(filtered.data(), filtered.data(), n);
    return filtered;
}

} // namespace

float SpeechSnrScore::estimateSnrDb(const float* samples, int numSamples, int sampleRateHz)
{
    if (!samples || numSamples <= 0 || sampleRateHz <= 0) {
        return 0.0f;
    }

    const int frameSamples = std::max(
        1, static_cast<int>(std::lround(kFrameMs * sampleRateHz / 1000.0)));
    const int frameCount = numSamples / frameSamples;
    if (frameCount < kMinFrames) {
        return 0.0f;
    }

    const std::vector<float> filtered = bandpassSpeechBand(samples, numSamples, sampleRateHz);

    std::vector<double> frameEnergies(static_cast<std::size_t>(frameCount));
    for (int frame = 0; frame < frameCount; ++frame) {
        double sum = 0.0;
        const int start = frame * frameSamples;
        for (int i = 0; i < frameSamples; ++i) {
            const double sample = filtered[static_cast<std::size_t>(start + i)];
            sum += sample * sample;
        }
        frameEnergies[static_cast<std::size_t>(frame)] = sum / frameSamples;
    }

    std::vector<double> sorted = frameEnergies;
    std::sort(sorted.begin(), sorted.end());

    const int noiseFrames = std::max(1, static_cast<int>(frameCount * 0.20));
    const int speechFrames = std::max(1, static_cast<int>(frameCount * 0.50));

    double noiseSum = 0.0;
    for (int i = 0; i < noiseFrames; ++i) {
        noiseSum += sorted[static_cast<std::size_t>(i)];
    }
    const double noisePower = std::max(noiseSum / noiseFrames, kEnergyEpsilon);

    double speechSum = 0.0;
    for (int i = frameCount - speechFrames; i < frameCount; ++i) {
        speechSum += sorted[static_cast<std::size_t>(i)];
    }
    const double speechPlusNoisePower = speechSum / speechFrames;

    // Power adds for uncorrelated signals: speechPower = (speech+noise) - noise.
    const double ratio = speechPlusNoisePower / noisePower - 1.0;
    if (ratio <= 0.0) {
        return kFloorDb;
    }
    return static_cast<float>(10.0 * std::log10(ratio));
}

float SpeechSnrScore::estimateSnrDb(const std::vector<float>& samples, int sampleRateHz)
{
    return estimateSnrDb(samples.data(), static_cast<int>(samples.size()), sampleRateHz);
}

} // namespace AetherSDR
