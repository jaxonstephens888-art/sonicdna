// SONIC DNA — core DSP primitives.
// Pure C++17, no framework dependencies. Everything here is used for *offline*
// sample generation (not the real-time audio thread), so clarity wins over
// micro-optimisation, but nothing allocates per-sample.
#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

namespace sdna {

constexpr double kPi = 3.14159265358979323846;
constexpr double kTwoPi = 2.0 * kPi;

inline float clampf(float x, float lo, float hi) { return x < lo ? lo : (x > hi ? hi : x); }
inline double clampd(double x, double lo, double hi) { return x < lo ? lo : (x > hi ? hi : x); }
inline float lerpf(float a, float b, float t) { return a + (b - a) * t; }
inline double midiToHz(double note) { return 440.0 * std::pow(2.0, (note - 69.0) / 12.0); }
inline float dbToGain(float db) { return db <= -120.0f ? 0.0f : std::pow(10.0f, db / 20.0f); }
inline float gainToDb(float g) { return g <= 1.0e-6f ? -120.0f : 20.0f * std::log10(g); }

// ---------------------------------------------------------------------------
// Deterministic RNG (splitmix64). Every sample is reproducible from its seed.
// ---------------------------------------------------------------------------
class Rng {
public:
    explicit Rng(uint64_t seed = 0x5D11A5EEDull) : state(seed ? seed : 0x9E3779B97F4A7C15ull) {}
    uint64_t next() {
        uint64_t z = (state += 0x9E3779B97F4A7C15ull);
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
        return z ^ (z >> 31);
    }
    double uni() { return (double)(next() >> 11) * (1.0 / 9007199254740992.0); } // [0,1)
    double range(double lo, double hi) { return lo + (hi - lo) * uni(); }
    float rangef(float lo, float hi) { return (float)range(lo, hi); }
    int irange(int lo, int hiInclusive) { return lo + (int)(uni() * (double)(hiInclusive - lo + 1)); }
    bool chance(double p) { return uni() < p; }
    float bipolar() { return (float)(uni() * 2.0 - 1.0); }
    double gauss() {
        double u1 = std::max(1e-12, uni()), u2 = uni();
        return std::sqrt(-2.0 * std::log(u1)) * std::cos(kTwoPi * u2);
    }
    // Pick an index according to (non-normalised) weights.
    size_t weighted(const std::vector<double>& w) {
        double total = 0; for (double x : w) total += std::max(0.0, x);
        if (total <= 0) return (size_t)irange(0, (int)w.size() - 1);
        double r = uni() * total;
        for (size_t i = 0; i < w.size(); ++i) { r -= std::max(0.0, w[i]); if (r <= 0) return i; }
        return w.size() - 1;
    }
    uint64_t fork() { return next() ^ 0xA5A5A5A5DEADBEEFull; }
private:
    uint64_t state;
};

uint64_t hashString(const std::string& s);

// ---------------------------------------------------------------------------
// Audio buffer (1 or 2 channels, float, non-interleaved)
// ---------------------------------------------------------------------------
struct Buffer {
    double sampleRate = 44100.0;
    std::vector<std::vector<float>> ch;

    Buffer() = default;
    Buffer(int numChannels, int length, double sr) : sampleRate(sr), ch((size_t)numChannels, std::vector<float>((size_t)length, 0.0f)) {}

    int numChannels() const { return (int)ch.size(); }
    int length() const { return ch.empty() ? 0 : (int)ch[0].size(); }
    double seconds() const { return sampleRate > 0 ? length() / sampleRate : 0.0; }
    float* data(int c) { return ch[(size_t)c].data(); }
    const float* data(int c) const { return ch[(size_t)c].data(); }
    bool empty() const { return length() == 0; }
    void resize(int len) { for (auto& c : ch) c.resize((size_t)len, 0.0f); }
    void makeStereo() { if (ch.size() == 1) ch.push_back(ch[0]); }
    void makeMono() {
        if (ch.size() < 2) return;
        for (size_t i = 0; i < ch[0].size(); ++i) ch[0][i] = 0.5f * (ch[0][i] + ch[1][i]);
        ch.resize(1);
    }
};

// ---------------------------------------------------------------------------
// Oscillators
// ---------------------------------------------------------------------------
inline double polyBlep(double t, double dt) {
    if (t < dt) { t /= dt; return t + t - t * t - 1.0; }
    if (t > 1.0 - dt) { t = (t - 1.0) / dt; return t * t + t + t + 1.0; }
    return 0.0;
}

struct Osc {
    double phase = 0.0; // 0..1
    explicit Osc(double startPhase = 0.0) : phase(startPhase) {}
    void advance(double inc) { phase += inc; phase -= std::floor(phase); }
    float sine(double inc) { float v = (float)std::sin(kTwoPi * phase); advance(inc); return v; }
    float saw(double inc) {
        double dt = std::min(0.5, std::abs(inc));
        float v = (float)(2.0 * phase - 1.0 - polyBlep(phase, dt)); advance(inc); return v;
    }
    float square(double inc, double pw = 0.5) {
        double dt = std::min(0.5, std::abs(inc));
        double v = phase < pw ? 1.0 : -1.0;
        v += polyBlep(phase, dt);
        double t2 = phase - pw; t2 -= std::floor(t2);
        v -= polyBlep(t2, dt);
        advance(inc); return (float)v;
    }
    float tri(double inc) { float v = (float)(phase < 0.5 ? 4.0 * phase - 1.0 : 3.0 - 4.0 * phase); advance(inc); return v; }
};

struct Noise {
    Rng rng;
    float b0 = 0, b1 = 0, b2 = 0, b3 = 0, b4 = 0, b5 = 0, b6 = 0, brownState = 0;
    explicit Noise(uint64_t seed) : rng(seed) {}
    float white() { return rng.bipolar(); }
    float pink() { // Paul Kellet's refined method
        float w = white();
        b0 = 0.99886f * b0 + w * 0.0555179f; b1 = 0.99332f * b1 + w * 0.0750759f;
        b2 = 0.96900f * b2 + w * 0.1538520f; b3 = 0.86650f * b3 + w * 0.3104856f;
        b4 = 0.55000f * b4 + w * 0.5329522f; b5 = -0.7616f * b5 - w * 0.0168980f;
        float out = b0 + b1 + b2 + b3 + b4 + b5 + b6 + w * 0.5362f; b6 = w * 0.115926f;
        return out * 0.11f;
    }
    float brown() { brownState = clampf(brownState + white() * 0.02f, -1.0f, 1.0f); brownState *= 0.998f; return brownState * 3.0f; }
};

// ---------------------------------------------------------------------------
// Topology-preserving state variable filter (Simper / Zavalishin)
// ---------------------------------------------------------------------------
struct SVF {
    float ic1 = 0, ic2 = 0, a1 = 0, a2 = 0, a3 = 0, k = 1;
    struct Out { float lp, bp, hp; };
    void set(double sr, double fc, double q) {
        fc = clampd(fc, 10.0, sr * 0.49);
        double g = std::tan(kPi * fc / sr);
        k = (float)(1.0 / std::max(0.05, q));
        a1 = (float)(1.0 / (1.0 + g * (g + k)));
        a2 = (float)(g * a1);
        a3 = (float)(g * a2);
    }
    Out tick(float v0) {
        float v3 = v0 - ic2;
        float v1 = a1 * ic1 + a2 * v3;
        float v2 = ic2 + a2 * ic1 + a3 * v3;
        ic1 = 2 * v1 - ic1; ic2 = 2 * v2 - ic2;
        return { v2, v1, v0 - k * v1 - v2 };
    }
    float lp(float x) { return tick(x).lp; }
    float hp(float x) { return tick(x).hp; }
    float bp(float x) { return tick(x).bp * k; } // unity gain at centre
    void reset() { ic1 = ic2 = 0; }
};

struct OnePole {
    float z = 0, a = 0;
    void setLP(double sr, double fc) { a = (float)std::exp(-kTwoPi * clampd(fc, 1.0, sr * 0.49) / sr); }
    float lp(float x) { z = x * (1 - a) + z * a; return z; }
    float hp(float x) { return x - lp(x); }
};

// ---------------------------------------------------------------------------
// Envelopes
// ---------------------------------------------------------------------------
// Exponential decay reaching -60 dB after `t60` seconds.
inline float expDecay(double tSec, double t60) { return t60 <= 0 ? 0.0f : (float)std::exp(-6.907755 * tSec / t60); }

struct ADSR {
    double a = 0.005, d = 0.1, s = 0.7, r = 0.2; // seconds / level
    // gateLen: how long the note is held (seconds). Returns amplitude at time t.
    float at(double t, double gateLen) const {
        auto held = [&](double tt) -> double {
            if (tt < a) return a > 0 ? tt / a : 1.0;
            tt -= a;
            if (tt < d) return 1.0 - (1.0 - s) * (1.0 - std::exp(-5.0 * tt / std::max(1e-4, d)));
            return s;
        };
        if (t < gateLen) return (float)held(t);
        double lvl = held(gateLen);
        double tr = t - gateLen;
        return (float)(lvl * std::exp(-6.9 * tr / std::max(1e-4, r)));
    }
};

// ---------------------------------------------------------------------------
// Waveshapers
// ---------------------------------------------------------------------------
enum class SatType { Soft, Tube, Hard, Fold, Rectify, Bitcrush };
inline float satSoft(float x) { return std::tanh(x); }
inline float satTube(float x) { return x >= 0 ? std::tanh(x) : std::tanh(x * 0.7f) / 0.7f * 0.85f; }
inline float satHard(float x) { return clampf(x, -1.0f, 1.0f); }
inline float satFold(float x) { return (float)std::sin(x * 1.5707963); }
inline float shape(SatType t, float x) {
    switch (t) {
        case SatType::Soft: return satSoft(x);
        case SatType::Tube: return satTube(x);
        case SatType::Hard: return satHard(x);
        case SatType::Fold: return satFold(x);
        case SatType::Rectify: return std::tanh(0.6f * x + 0.4f * std::abs(x));
        case SatType::Bitcrush: return std::tanh(x);
    }
    return x;
}
const char* satTypeName(SatType t);

// ---------------------------------------------------------------------------
// Formant data for vocal synthesis (F1..F3 Hz, bandwidths, relative gains dB)
// ---------------------------------------------------------------------------
struct Vowel { const char* name; float f[4]; float bw[4]; float gainDb[4]; };
const std::vector<Vowel>& vowelTable();
Vowel lerpVowel(const Vowel& a, const Vowel& b, float t, float formantShift);

// ---------------------------------------------------------------------------
// Whole-buffer processors (offline)
// ---------------------------------------------------------------------------
namespace fx {
    void gain(Buffer& b, float g);
    float peak(const Buffer& b);
    float rms(const Buffer& b);
    void normalize(Buffer& b, float peakDb = -1.0f);
    void fadeIn(Buffer& b, double seconds);
    void fadeOut(Buffer& b, double seconds);
    void removeDC(Buffer& b);
    void trimTail(Buffer& b, float thresholdDb = -70.0f, double minSeconds = 0.05, double fadeSeconds = 0.01);
    void trimLead(Buffer& b, float thresholdDb = -60.0f);
    void saturate(Buffer& b, SatType type, float drive, float mix = 1.0f);
    void bitcrush(Buffer& b, int bits, int downsample, float mix = 1.0f);
    void lowpass(Buffer& b, double hz, double q = 0.707);
    void highpass(Buffer& b, double hz, double q = 0.707);
    void bandpass(Buffer& b, double hz, double q = 1.0);
    // Exponential sweep of cutoff from startHz to endHz over the buffer (type 0=LP 1=HP 2=BP)
    void sweep(Buffer& b, int type, double startHz, double endHz, double q, double curve = 1.0);
    void tilt(Buffer& b, float amount); // -1 dark .. +1 bright
    // Freeverb-style stereo reverb; extends the buffer by `tailSec` and returns stereo.
    void reverb(Buffer& b, float size, float damp, float wet, float width = 1.0f, double predelayMs = 10.0, double tailSec = 1.5, bool wetOnly = false);
    void delay(Buffer& b, double timeSec, float feedback, float wet, bool pingPong, double lpHz = 6000.0, double tailSec = 1.0);
    void chorus(Buffer& b, float depthMs, float rateHz, float mix);
    void widen(Buffer& b, float amount);
    void reverse(Buffer& b);
    void resample(Buffer& b, double ratio); // ratio > 1 = higher pitch/shorter
    Buffer resampleTo(const Buffer& b, double newRate);
    void compress(Buffer& b, float thresholdDb, float ratio, double attackMs, double releaseMs, float makeupDb = 0.0f);
    void softLimit(Buffer& b, float ceilingDb = -0.3f);
    void tapeWow(Buffer& b, float depth, float rateHz, uint64_t seed);
    void addNoise(Buffer& b, float levelDb, int type, uint64_t seed, bool crackle = false);
    void transient(Buffer& b, float attackDb, float sustainDb);
    void ringMod(Buffer& b, double hz, float mix);
    void stutter(Buffer& b, uint64_t seed, float amount, double bpm);
    void mixInto(Buffer& dst, const Buffer& src, int offset, float gain);
    float spectralCentroid(const Buffer& b, int maxFrames = 64);

    struct GrainParams {
        double grainMs = 80, density = 30; // grains per second
        double position = 0.5, positionJitter = 0.4; // 0..1 of source
        double pitchSemis = 0, pitchJitter = 0.1; // semitones
        double reverseProb = 0.1, spread = 0.8;
        std::vector<double> pitchSet; // optional quantised pitch offsets (semitones)
    };
    Buffer granulate(const Buffer& src, double outSeconds, const GrainParams& gp, uint64_t seed);
}

void fft(std::vector<float>& re, std::vector<float>& im, bool inverse);

} // namespace sdna
