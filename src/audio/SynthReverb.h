#pragma once

// The reverb of the Microsoft Synthesizer, the DirectMusic software synth MM2
// plays its music through. DMusicObject::InitPort opens that port with
// DMUS_EFFECT_REVERB (the object's reverb flag is set by its constructor and
// the synth's caps report the effect), and MM2 never sets the reverb's
// parameters (no IKsControl call on the port), so the synth's defaults apply:
// DMUS_WAVES_REVERB_PARAMS as the DirectX 7 documentation gives them
// (SynthReverbParams). Every DirectMusic sound MM2 makes goes through such a
// port: the soundtrack with its Big Air motif (a secondary segment on the same
// performance) and the city ambience segment (its own MMDMusicManager, made by
// mmGameMusicData::LoadAmbientSFX, Init'ed the same way).
//
// The reverb is Waves' "SimpleVerb" (TrueVerb licensed to Microsoft), which is
// proprietary. This is an approximation (inferred): its structure follows
// OpenMPT's emulation of the DirectX 8 Waves Reverb DMO (soundlib/plugins/dmo/
// WavesReverb.cpp, BSD-3-Clause, by the OpenMPT developers), which takes the
// same four parameters: four comb delays of 45 ms * 2^(n/4) cross-coupled in
// pairs by allpass lattices (golden-ratio coefficients), a two-tap low-pass in
// each loop for the high-frequency decay, and a dry/wet split that keeps the
// output power constant. That the DirectX 7 synth's reverb is the same
// algorithm as the DirectX 8 DMO is itself inferred (same vendor, parameters
// and documentation). See docs/music.md.

#include <array>
#include <cstddef>
#include <vector>

namespace mm2::audio {

// DMUS_WAVES_REVERB_PARAMS (dmusicc.h), with the Microsoft Synthesizer's
// documented defaults.
struct SynthReverbParams {
    // fInGain: input gain in dB, applied to the direct sound and the reverb.
    float inGain = 0.0f;
    // fReverbMix in dB: 0 is all reverb and no direct sound, lower is drier;
    // the coefficients keep the overall level about the same.
    float reverbMix = -10.0f;
    // fReverbTime: decay time to -60 dB, in milliseconds.
    float reverbTime = 1000.0f;
    // fHighFreqRTRatio: the high frequencies' decay time over fReverbTime.
    float highFreqRTRatio = 0.001f;
};

// Stereo reverb applied in place to interleaved stereo float frames. Not
// thread-safe; MusicEngine runs one per DirectMusic stream on its thread.
class SynthReverb {
public:
    // MM2's synth port runs at AudManager::GetSamplesPerSecond, which is
    // always 22050 Hz (the constructor's value; SetBitDepthAndSampleRate has
    // no caller). The reverb's loop filter acts at its own rate's Nyquist
    // frequency, so it runs at the output rate divided by the whole number
    // that comes closest to this (24 kHz at 48 kHz, 22050 Hz at 44.1 kHz).
    static constexpr int kPortRate = 22050;

    explicit SynthReverb(int sampleRate, const SynthReverbParams& params = {});

    // Values outside the DirectSound Waves reverb's ranges are clamped:
    // gains -96..0 dB, time 0.001..3000 ms, ratio 0.001..0.999.
    void setParams(const SynthReverbParams& params);
    const SynthReverbParams& params() const { return m_params; }

    // Off is a port without DMUS_EFFECT_REVERB: the input passes unchanged.
    void setEnabled(bool enabled) { m_enabled = enabled; }
    bool enabled() const { return m_enabled; }

    // Processes `frames` interleaved stereo frames in place.
    void process(float* stereo, int frames);
    // Forgets the reverb's tail.
    void reset();

    int sampleRate() const { return m_rate; }
    int internalRate() const { return m_rate / m_factor; }
    float dryGain() const { return m_dry; }
    float wetGain() const { return m_wet; }

private:
    void computeDelays();
    void computeCoefficients();
    // One sample at the internal rate: the delay lines' outputs through the
    // allpass lattices (the reverb's output, which does not depend on the
    // current input), then feed() writes the loops with the input.
    void tick();
    void feed(float left, float right);

    int m_rate;
    int m_factor = 1; // output frames per internal sample
    SynthReverbParams m_params;
    bool m_enabled = true;

    // Delays in internal samples: the four combs, then the two allpasses.
    std::array<std::size_t, 4> m_combDelay{};
    std::array<std::size_t, 2> m_allpassDelay{};
    // Coefficients: each comb's loop filter (current, previous sample), each
    // allpass's decay, and the direct and reverb input gains.
    std::array<std::array<float, 2>, 4> m_loop{};
    std::array<float, 2> m_allpassGain{};
    float m_dry = 1.0f;
    float m_wet = 0.0f;

    // State.
    std::vector<std::array<float, 4>> m_comb;
    std::vector<std::array<float, 2>> m_allpass1, m_allpass2;
    std::size_t m_combMask = 0, m_allpassMask = 0;
    std::size_t m_combPos = 0, m_allpassPos = 0;
    std::array<float, 4> m_tap{};     // this sample's loop outputs
    std::array<float, 4> m_tapPrev{}; // the previous sample's
    std::array<float, 2> m_out{};     // the reverb output of this internal sample
    std::array<float, 2> m_outPrev{}; // and of the previous one (for the interpolation)
    std::array<float, 2> m_acc{};     // input summed over the current internal sample
    int m_phase = 0;                  // output frames into the current internal sample
};

} // namespace mm2::audio
