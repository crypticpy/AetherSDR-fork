#pragma once

#include <vector>

namespace AetherSDR {

// Blind, single-block speech-band SNR estimator (B18 design doc gap 3: "no
// objective A/B score" for the client NR modes). Pure DSP — no Qt, no I/O,
// no threading state — a stateless function of the samples handed to it.
//
// Method: bandpass the block to the speech band (300-2700 Hz), split it into
// non-overlapping 20 ms frames, and rank frames by energy. The lowest 20% of
// frame energies are treated as the noise floor (pauses/silence between
// speech); the top 50% are treated as speech-plus-noise. Speech power is
// recovered by subtracting the noise floor from the speech-plus-noise level,
// assuming the two are uncorrelated (power adds) — the same pause-based
// noise-floor idea rnnoise_filter_test.cpp already uses for its probe-tone
// measurements, generalized into a reusable estimator.
//
// This is a rough estimator meant for A/B comparisons ("did this NR mode
// measurably reduce the noise floor on this signal"), not a perceptual
// quality score (DNSMOS etc.) — no vendored model, no external dependency.
//
// Not wired into AudioEngine yet. See the B18 design doc, §5 item 3/4, for
// the intended call site: alongside the exclusive NR selection in
// AudioEngine::processMixedRxAudioData() (AudioEngine.cpp, the block that
// picks RN2/NR4/DFNR/BNR/MNR around :5043-5090), to compute a live
// before/after number the GUI or automation bridge could surface — that
// wiring is intentionally out of scope for this change.
class SpeechSnrScore {
public:
    // samples/numSamples: mono float32 PCM in [-1, 1]. sampleRateHz: the
    // block's sample rate. Returns the estimated speech-band SNR in dB.
    // Returns 0.0f if the block is too short to form at least a handful of
    // 20 ms frames. Returns a fixed floor value (see kFloorDb in the .cpp)
    // if speech-plus-noise never measurably exceeds the noise floor.
    static float estimateSnrDb(const float* samples, int numSamples, int sampleRateHz);
    static float estimateSnrDb(const std::vector<float>& samples, int sampleRateHz);
};

} // namespace AetherSDR
