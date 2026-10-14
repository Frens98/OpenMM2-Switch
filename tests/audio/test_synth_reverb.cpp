// The Microsoft Synthesizer's reverb on the music (DMusicObject::InitPort's
// DMUS_EFFECT_REVERB, DMUS_WAVES_REVERB_PARAMS defaults) and the mono music
// of STEREO FX mono (InitPort's AudManagerBase::IsStereo() + 1 channels).
#include "TestData.h"
#include "audio/Music.h"
#include "audio/SynthReverb.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>
#include <numbers>
#include <random>
#include <thread>
#include <vector>

using namespace mm2;
using namespace mm2::audio;

namespace {

// The reverb's response to a unit impulse in the left channel, without the
// direct sound.
std::vector<float> impulseResponse(int rate, double seconds, const SynthReverbParams& params = {}) {
    SynthReverb reverb(rate, params);
    std::vector<float> buf(static_cast<std::size_t>(seconds * rate) * 2, 0.0f);
    buf[0] = 1.0f;
    reverb.process(buf.data(), static_cast<int>(buf.size() / 2));
    buf[0] -= reverb.dryGain();
    return buf;
}

// Four RBJ band-passes (0 dB peak) in a row over one channel of interleaved
// stereo: steep enough that a fast-decaying band is not swamped by its
// neighbours' slower tails.
std::vector<double> bandPass(const std::vector<float>& stereo, int channel, int rate, double freq, double q) {
    const double w = 2.0 * std::numbers::pi * freq / rate;
    const double alpha = std::sin(w) / (2.0 * q);
    const double a0 = 1.0 + alpha;
    const double b0 = alpha / a0, b2 = -alpha / a0, a1 = -2.0 * std::cos(w) / a0, a2 = (1.0 - alpha) / a0;
    std::vector<double> out(stereo.size() / 2);
    for (std::size_t i = 0; i < out.size(); ++i)
        out[i] = stereo[i * 2 + static_cast<std::size_t>(channel)];
    for (int stage = 0; stage < 4; ++stage) {
        double x1 = 0, x2 = 0, y1 = 0, y2 = 0;
        for (double& v : out) {
            const double x = v;
            const double y = b0 * x + b2 * x2 - a1 * y1 - a2 * y2;
            x2 = x1;
            x1 = x;
            y2 = y1;
            y1 = y;
            v = y;
        }
    }
    return out;
}

// Decay time to -60 dB in a band, from the Schroeder integral of both
// channels' energy, fitted between -5 and -25 dB (T20).
double decayTime(const std::vector<float>& ir, int rate, double freq) {
    const auto l = bandPass(ir, 0, rate, freq, 1.4);
    const auto r = bandPass(ir, 1, rate, freq, 1.4);
    std::vector<double> edc(l.size());
    double acc = 0;
    for (std::size_t i = l.size(); i-- > 0;) {
        acc += l[i] * l[i] + r[i] * r[i];
        edc[i] = acc;
    }
    const double total = edc[0];
    double sx = 0, sy = 0, sxx = 0, sxy = 0;
    int n = 0;
    for (std::size_t i = 0; i < edc.size(); ++i) {
        const double db = 10.0 * std::log10(edc[i] / total);
        if (db > -5.0)
            continue;
        if (db < -25.0)
            break;
        const double t = static_cast<double>(i) / rate;
        sx += t;
        sy += db;
        sxx += t * t;
        sxy += t * db;
        ++n;
    }
    const double slope = (n * sxy - sx * sy) / (n * sxx - sx * sx); // dB per second
    return -60.0 / slope;
}

std::vector<float> noise(std::size_t frames, float amplitude, unsigned seed) {
    std::mt19937 rng(seed);
    std::uniform_real_distribution<float> dist(-amplitude, amplitude);
    std::vector<float> v(frames * 2);
    for (float& f : v)
        f = dist(rng);
    return v;
}

double correlation(const std::vector<float>& stereo) {
    double ll = 0, rr = 0, lr = 0;
    for (std::size_t i = 0; i + 1 < stereo.size(); i += 2) {
        ll += static_cast<double>(stereo[i]) * stereo[i];
        rr += static_cast<double>(stereo[i + 1]) * stereo[i + 1];
        lr += static_cast<double>(stereo[i]) * stereo[i + 1];
    }
    return lr / std::sqrt(ll * rr);
}

} // namespace

TEST(SynthReverb, DefaultsAreTheDirectX7SynthsWavesReverbParams) {
    const SynthReverbParams p;
    EXPECT_EQ(p.inGain, 0.0f);
    EXPECT_EQ(p.reverbMix, -10.0f);
    EXPECT_EQ(p.reverbTime, 1000.0f);
    EXPECT_EQ(p.highFreqRTRatio, 0.001f);
    // -10 dB of reverb mix leaves 90% of the power to the direct sound.
    SynthReverb reverb(48000);
    EXPECT_NEAR(reverb.dryGain(), std::sqrt(0.9), 1e-6);
    EXPECT_GT(reverb.wetGain(), 0.0f);
    EXPECT_LT(reverb.wetGain(), std::sqrt(0.1f));
}

TEST(SynthReverb, RunsNearTheSynthPortsRate) {
    EXPECT_EQ(SynthReverb(48000).internalRate(), 24000);
    EXPECT_EQ(SynthReverb(44100).internalRate(), 22050);
    EXPECT_EQ(SynthReverb(22050).internalRate(), 22050);
    EXPECT_EQ(SynthReverb(96000).internalRate(), 24000);
    EXPECT_EQ(SynthReverb(16000).internalRate(), 16000);
}

TEST(SynthReverb, LowFrequenciesDecayInTheReverbTime) {
    for (int rate : {48000, 44100, 22050}) {
        const auto ir = impulseResponse(rate, 4.0);
        EXPECT_NEAR(decayTime(ir, rate, 250.0), 1.0, 0.1) << rate;
    }
    SynthReverbParams p;
    p.reverbTime = 2000.0f;
    EXPECT_NEAR(decayTime(impulseResponse(48000, 6.0, p), 48000, 250.0), 2.0, 0.2);
    p.reverbTime = 500.0f;
    EXPECT_NEAR(decayTime(impulseResponse(48000, 3.0, p), 48000, 250.0), 0.5, 0.05);
}

TEST(SynthReverb, HighFrequenciesDecayFaster) {
    const auto ir = impulseResponse(48000, 4.0);
    const double low = decayTime(ir, 48000, 250.0);
    const double mid = decayTime(ir, 48000, 4000.0);
    const double high = decayTime(ir, 48000, 8000.0);
    EXPECT_LT(mid, 0.85 * low);
    EXPECT_LT(high, 0.5 * low);
    EXPECT_LT(high, mid);
    // A ratio near 1 keeps the highs about as long as the lows.
    SynthReverbParams p;
    p.highFreqRTRatio = 0.999f;
    const auto bright = impulseResponse(48000, 4.0, p);
    EXPECT_NEAR(decayTime(bright, 48000, 4000.0), decayTime(bright, 48000, 250.0), 0.1);
}

TEST(SynthReverb, TheReverbIsStereoAndDecorrelated) {
    // A mono input comes out of the two channels differently.
    SynthReverb reverb(48000);
    auto buf = noise(48000, 0.5f, 1);
    for (std::size_t i = 0; i < buf.size(); i += 2)
        buf[i + 1] = buf[i];
    reverb.process(buf.data(), 48000);
    for (std::size_t i = 0; i < buf.size(); i += 2)
        buf[i] -= buf[i + 1]; // left - right: the reverb's difference only
    double side = 0;
    for (std::size_t i = 0; i < buf.size(); i += 2)
        side += static_cast<double>(buf[i]) * buf[i];
    EXPECT_GT(side, 1.0);
}

TEST(SynthReverb, OffOrWithoutMixTheDirectSoundIsUnchanged) {
    const auto input = noise(20000, 0.5f, 2);
    auto off = input;
    SynthReverb reverb(48000);
    reverb.setEnabled(false);
    reverb.process(off.data(), 20000);
    EXPECT_EQ(off, input); // a port without DMUS_EFFECT_REVERB

    SynthReverbParams p;
    p.reverbMix = -96.0f; // the driest mix
    SynthReverb dry(48000, p);
    auto out = input;
    dry.process(out.data(), 20000);
    for (std::size_t i = 0; i < out.size(); ++i)
        ASSERT_NEAR(out[i], input[i], 1e-4f) << i;
}

TEST(SynthReverb, InGainScalesBothPaths) {
    SynthReverbParams p;
    p.inGain = -6.0f;
    SynthReverb quiet(48000, p);
    SynthReverb normal(48000);
    const float g = std::pow(10.0f, -6.0f / 20.0f);
    EXPECT_NEAR(quiet.dryGain(), normal.dryGain() * g, 1e-6f);
    EXPECT_NEAR(quiet.wetGain(), normal.wetGain() * g, 1e-6f);
}

TEST(SynthReverb, BlockSizeDoesNotMatter) {
    const auto input = noise(9000, 0.5f, 3);
    auto whole = input;
    SynthReverb a(48000);
    a.process(whole.data(), 9000);
    auto pieces = input;
    SynthReverb b(48000);
    int done = 0;
    for (int n : {1, 7, 512, 3, 1000, 2477}) {
        b.process(pieces.data() + static_cast<std::size_t>(done) * 2, n);
        done += n;
    }
    b.process(pieces.data() + static_cast<std::size_t>(done) * 2, 9000 - done);
    EXPECT_EQ(whole, pieces);
}

TEST(SynthReverb, SilenceStaysSilentAndTheTailEndsInZeros) {
    SynthReverb reverb(48000);
    std::vector<float> zeros(48000 * 2, 0.0f);
    reverb.process(zeros.data(), 48000);
    for (float f : zeros)
        ASSERT_EQ(f, 0.0f);

    auto burst = noise(4800, 1.0f, 4);
    reverb.process(burst.data(), 4800);
    // 60 dB a second: well under the flush level after 10 s.
    std::vector<float> tail(48000 * 2);
    for (int s = 0; s < 10; ++s) {
        std::ranges::fill(tail, 0.0f);
        reverb.process(tail.data(), 48000);
    }
    std::ranges::fill(tail, 0.0f);
    reverb.process(tail.data(), 48000);
    for (float f : tail) {
        ASSERT_EQ(f, 0.0f);
        ASSERT_FALSE(std::fpclassify(f) == FP_SUBNORMAL);
    }
}

TEST(SynthReverb, NoNaNFromBadInputOrExtremeParameters) {
    SynthReverb reverb(48000);
    auto buf = noise(48000, 0.5f, 5);
    buf[1000] = std::numeric_limits<float>::quiet_NaN();
    buf[2001] = std::numeric_limits<float>::infinity();
    buf[3000] = -std::numeric_limits<float>::infinity();
    reverb.process(buf.data(), 48000);
    for (std::size_t i = 0; i < buf.size(); ++i) {
        if (i != 1000 && i != 2001 && i != 3000) { // the direct sound passes them as they are
            ASSERT_TRUE(std::isfinite(buf[i])) << i;
        }
    }
    auto after = noise(48000, 0.5f, 6);
    reverb.process(after.data(), 48000);
    for (float f : after)
        ASSERT_TRUE(std::isfinite(f));

    for (const SynthReverbParams& p : {SynthReverbParams{0.0f, 0.0f, 3000.0f, 0.999f},
                                       SynthReverbParams{0.0f, 0.0f, 0.001f, 0.001f},
                                       SynthReverbParams{-96.0f, -96.0f, 1.0f, 0.5f},
                                       SynthReverbParams{20.0f, 10.0f, 1e9f, 5.0f}}) {
        SynthReverb r(48000, p);
        auto loud = noise(96000, 1.0f, 7);
        r.process(loud.data(), 96000);
        float peak = 0;
        for (float f : loud) {
            ASSERT_TRUE(std::isfinite(f));
            peak = std::max(peak, std::abs(f));
        }
        EXPECT_LT(peak, 10.0f); // bounded: the loops lose energy every pass
    }
}

// With retail data: the music through MusicEngine, with and without the
// reverb, and mono.
TEST(SynthReverb, RaceMusicWidensWithTheReverbAndIsMonoWhenAsked) {
    MM2_REQUIRE_GAME_DATA();
    auto lib = std::make_shared<MusicLibrary>(*test::gameData());
    MusicEngine engine(lib, MusicTables::load(*test::gameData()), 48000);
    EXPECT_TRUE(engine.reverb()); // on, as MM2's port has it
    engine.setReverb(false);
    engine.selectSong(0, false);
    engine.preload();
    engine.setState(MusicState::Racing);
    constexpr int kFrames = 48000 * 4;
    std::vector<float> dry(static_cast<std::size_t>(kFrames) * 2);
    for (int f = 0; f < kFrames; f += 512)
        engine.render(dry.data() + static_cast<std::size_t>(f) * 2, nullptr, std::min(512, kFrames - f));
    // The same notes through the reverb, as the engine applies it.
    auto wet = dry;
    SynthReverb(48000).process(wet.data(), kFrames);
    EXPECT_LT(correlation(wet), correlation(dry));

    engine.setReverb(true);
    engine.setStereo(false);
    std::vector<float> mono(48000 * 2);
    engine.render(mono.data(), nullptr, 48000);
    double energy = 0;
    for (std::size_t i = 0; i < mono.size(); i += 2) {
        ASSERT_EQ(mono[i], mono[i + 1]);
        energy += static_cast<double>(mono[i]) * mono[i];
    }
    EXPECT_GT(energy, 1.0);
}

TEST(SynthReverb, PlayerTakesTheStereoSettingWhenTheMusicStarts) {
    MM2_REQUIRE_GAME_DATA();
    MusicPlayer player(*test::gameData(), 48000);
    ASSERT_TRUE(player.ok());
    player.setStereo(false);
    player.playMenu();
    auto stream = player.musicStream();
    std::vector<float> block(2 * 480);
    bool heard = false;
    for (int i = 0; i < 300 && !heard; ++i) { // up to 3 s for the loader
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        stream->render(block.data(), 480);
        for (std::size_t j = 0; j < block.size(); j += 2) {
            ASSERT_EQ(block[j], block[j + 1]);
            heard = heard || block[j] != 0.0f;
        }
    }
    EXPECT_TRUE(heard);
}
