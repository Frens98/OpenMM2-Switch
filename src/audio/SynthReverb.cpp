#include "audio/SynthReverb.h"

#include <algorithm>
#include <bit>
#include <cmath>

namespace mm2::audio {
namespace {

// The allpass lattices' coefficient, the golden ratio's inverse.
constexpr float kPhi = 0.61803401f;
// The first comb's delay, the step between the delays (2^0.25) and the share
// of a pair's delays that goes to its allpass.
constexpr float kBaseDelay = 0.045f;
constexpr float kDelayStep = 1.18920707f;
constexpr float kAllpassShare = 0.11546667f;

// Loop values this small are set to zero, so a decaying tail ends in exact
// silence instead of denormal numbers (which are slow on x86).
constexpr float kTiny = 1e-20f;
float flush(float v) { return std::abs(v) < kTiny ? 0.0f : v; }

// Input the loops accept; anything else (NaN, infinity) is fed as silence so
// one bad sample cannot poison the reverb for good.
constexpr float kMaxInput = 1.0e4f;
float sane(float v) { return std::abs(v) <= kMaxInput ? v : 0.0f; }

std::size_t roundDelay(float v) { return static_cast<std::size_t>(std::lround(v)); }

} // namespace

SynthReverb::SynthReverb(int sampleRate, const SynthReverbParams& params) : m_rate(std::max(sampleRate, 1)) {
    m_factor = std::max(1, static_cast<int>(std::lround(static_cast<double>(m_rate) / kPortRate)));
    computeDelays();
    setParams(params);
}

void SynthReverb::computeDelays() {
    const float rate = static_cast<float>(internalRate());
    const std::size_t d0 = std::max<std::size_t>(roundDelay(rate * kBaseDelay), 8);
    const std::size_t d1 = roundDelay(static_cast<float>(d0) * kDelayStep);
    const std::size_t d2 = roundDelay(static_cast<float>(d1) * kDelayStep);
    const std::size_t d3 = roundDelay(static_cast<float>(d2) * kDelayStep);
    const std::size_t a0 = roundDelay(static_cast<float>(d0 + d2) * kAllpassShare);
    const std::size_t a1 = roundDelay(static_cast<float>(d1 + d3) * kAllpassShare);
    // Pair 0 (combs 0 and 1) shares allpass 0, pair 1 (combs 2 and 3)
    // allpass 1; each loop's total delay is its comb's plus its allpass's.
    m_combDelay = {d0 - a0, d2 - a0, d1 - a1, d3 - a1};
    m_allpassDelay = {a0, a1};
    const std::size_t combSize = std::bit_ceil(d3 + 1);
    const std::size_t allpassSize = std::bit_ceil(a1 + 1);
    m_comb.assign(combSize, {});
    m_allpass1.assign(allpassSize, {});
    m_allpass2.assign(allpassSize, {});
    m_combMask = combSize - 1;
    m_allpassMask = allpassSize - 1;
    reset();
}

void SynthReverb::setParams(const SynthReverbParams& p) {
    m_params.inGain = std::clamp(p.inGain, -96.0f, 0.0f);
    m_params.reverbMix = std::clamp(p.reverbMix, -96.0f, 0.0f);
    m_params.reverbTime = std::clamp(p.reverbTime, 0.001f, 3000.0f);
    m_params.highFreqRTRatio = std::clamp(p.highFreqRTRatio, 0.001f, 0.999f);
    computeCoefficients();
}

void SynthReverb::computeCoefficients() {
    // Decay per sample, as a power of ten: -3 (60 dB) over the reverb time.
    const double perSample = -3000.0 / (static_cast<double>(internalRate()) * m_params.reverbTime);
    // The extra decay of the highest frequencies, so that theirs takes
    // reverbTime * ratio.
    const double perSampleHf = perSample * (1.0 / m_params.highFreqRTRatio - 1.0);
    for (std::size_t i = 0; i < 2; ++i) {
        const double delay = static_cast<double>(m_allpassDelay[i]);
        m_allpassGain[i] = static_cast<float>(std::pow(10.0, delay * perSample));
    }
    double sum = 0.0;
    for (std::size_t c = 0; c < 4; ++c) {
        const std::size_t pair = c / 2;
        const double gain = std::pow(10.0, static_cast<double>(m_combDelay[c]) * perSample);
        // The loop filter averages this sample and the previous one with
        // weight `blend`, so the highest frequency is scaled by 1 - 2 * blend
        // per pass on top of `gain`.
        const double loop = static_cast<double>(m_combDelay[c] + m_allpassDelay[pair]);
        const double blend = (1.0 - std::pow(10.0, loop * perSampleHf)) * 0.5;
        m_loop[c] = {static_cast<float>(gain * (1.0 - blend)), static_cast<float>(gain * blend)};
        // The loop's gain at 0 Hz, summed over its first passes: the reverb
        // input is scaled so the four loops' energy comes out at a fixed level.
        const double g = gain * static_cast<double>(m_allpassGain[pair]);
        const double passes = g * (((g + 1.0) * g + 1.0) * g + 1.0) + 1.0;
        sum += passes * passes;
    }
    // fReverbMix is the reverb's share of the power: the direct sound keeps
    // the rest.
    const double inGain = std::pow(10.0, static_cast<double>(m_params.inGain) / 20.0);
    const double mix = std::pow(10.0, static_cast<double>(m_params.reverbMix) / 10.0);
    m_dry = static_cast<float>(std::sqrt(1.0 - mix) * inGain);
    m_wet = static_cast<float>(std::sqrt(mix) * (4.0 / std::sqrt(sum)) * inGain);
}

void SynthReverb::reset() {
    std::ranges::fill(m_comb, std::array<float, 4>{});
    std::ranges::fill(m_allpass1, std::array<float, 2>{});
    std::ranges::fill(m_allpass2, std::array<float, 2>{});
    m_combPos = m_allpassPos = 0;
    m_tap = m_tapPrev = {};
    m_out = m_outPrev = m_acc = {};
    m_phase = 0;
}

void SynthReverb::tick() {
    const std::size_t cp = m_combPos;
    const float x0 = m_comb[(cp - m_combDelay[0]) & m_combMask][0];
    const float x1 = m_comb[(cp - m_combDelay[1]) & m_combMask][1];
    const float x2 = m_comb[(cp - m_combDelay[2]) & m_combMask][2];
    const float x3 = m_comb[(cp - m_combDelay[3]) & m_combMask][3];
    const std::size_t ap = m_allpassPos & m_allpassMask;

    // Pair 0: combs 0 and 1 through allpass 1.
    const auto& a = m_allpass1[(m_allpassPos - m_allpassDelay[0]) & m_allpassMask];
    const float r1 = x1 * kPhi + a[0] * m_allpassGain[0];
    const float r2 = a[1] * m_allpassGain[0] - x0 * kPhi;
    m_allpass1[ap] = {flush(r2 * kPhi + x0), flush(x1 - r1 * kPhi)};
    m_tap[0] = r1;
    m_tap[1] = r2;

    // Pair 1: combs 2 and 3 through allpass 2.
    const auto& b = m_allpass2[(m_allpassPos - m_allpassDelay[1]) & m_allpassMask];
    const float r3 = x3 * kPhi + b[0] * m_allpassGain[1];
    const float r4 = b[1] * m_allpassGain[1] - x2 * kPhi;
    m_allpass2[ap] = {flush(r4 * kPhi + x2), flush(x3 - r3 * kPhi)};
    m_tap[2] = r3;
    m_tap[3] = r4;

    m_outPrev = m_out;
    m_out = {m_tap[0] + m_tap[2], m_tap[1] + m_tap[3]};
}

void SynthReverb::feed(float left, float right) {
    const float wl = sane(left) * m_wet;
    const float wr = sane(right) * m_wet;
    auto& slot = m_comb[m_combPos & m_combMask];
    // The third comb takes the right input inverted, which decorrelates the
    // pairs' outputs.
    const float in[4] = {wl, wr, -wr, wl};
    for (std::size_t c = 0; c < 4; ++c)
        slot[c] = flush(m_tap[c] * m_loop[c][0] + m_tapPrev[c] * m_loop[c][1] + in[c]);
    m_tapPrev = m_tap;
    ++m_combPos;
    ++m_allpassPos;
}

void SynthReverb::process(float* stereo, int frames) {
    if (!m_enabled || !stereo || frames <= 0)
        return;
    const float step = 1.0f / static_cast<float>(m_factor);
    for (int i = 0; i < frames; ++i) {
        if (m_phase == 0)
            tick();
        ++m_phase;
        // The reverb output between internal samples is interpolated
        // linearly, so it reaches this internal sample's value on its last frame.
        const float t = static_cast<float>(m_phase) * step;
        const float wetL = m_outPrev[0] + (m_out[0] - m_outPrev[0]) * t;
        const float wetR = m_outPrev[1] + (m_out[1] - m_outPrev[1]) * t;
        float& l = stereo[static_cast<std::size_t>(i) * 2];
        float& r = stereo[static_cast<std::size_t>(i) * 2 + 1];
        m_acc[0] += l;
        m_acc[1] += r;
        l = l * m_dry + wetL;
        r = r * m_dry + wetR;
        // The input is averaged over the internal sample's frames.
        if (m_phase == m_factor) {
            feed(m_acc[0] * step, m_acc[1] * step);
            m_acc = {};
            m_phase = 0;
        }
    }
}

} // namespace mm2::audio
