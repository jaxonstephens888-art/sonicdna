#include "sdna/Dsp.h"

#include <complex>
#include <numeric>

namespace sdna {

uint64_t hashString(const std::string& s) {
    uint64_t h = 1469598103934665603ull; // FNV-1a
    for (unsigned char c : s) { h ^= c; h *= 1099511628211ull; }
    return h;
}

const char* satTypeName(SatType t) {
    switch (t) {
        case SatType::Soft: return "soft";
        case SatType::Tube: return "tube";
        case SatType::Hard: return "hard";
        case SatType::Fold: return "fold";
        case SatType::Rectify: return "rectify";
        case SatType::Bitcrush: return "crush";
    }
    return "soft";
}

const std::vector<Vowel>& vowelTable() {
    // Approximate adult formant data (Peterson & Barney style averages)
    static const std::vector<Vowel> t = {
        { "a", { 800, 1150, 2900, 3900 }, { 80, 90, 120, 130 }, { 0, -4, -20, -36 } },
        { "e", { 400, 1600, 2700, 3300 }, { 60, 80, 120, 150 }, { 0, -24, -30, -35 } },
        { "i", { 280, 2250, 2900, 3700 }, { 50, 90, 100, 120 }, { 0, -20, -26, -32 } },
        { "o", { 450, 800, 2830, 3500 }, { 70, 80, 100, 130 }, { 0, -9, -16, -28 } },
        { "u", { 325, 700, 2530, 3500 }, { 50, 60, 170, 180 }, { 0, -12, -30, -40 } },
        { "ae", { 660, 1720, 2410, 3500 }, { 80, 90, 120, 130 }, { 0, -6, -24, -32 } },
        { "uh", { 600, 1050, 2550, 3500 }, { 70, 80, 120, 130 }, { 0, -6, -22, -32 } },
    };
    return t;
}

Vowel lerpVowel(const Vowel& a, const Vowel& b, float t, float formantShift) {
    Vowel v = a;
    for (int i = 0; i < 4; ++i) {
        v.f[i] = lerpf(a.f[i], b.f[i], t) * formantShift;
        v.bw[i] = lerpf(a.bw[i], b.bw[i], t) * std::sqrt(formantShift);
        v.gainDb[i] = lerpf(a.gainDb[i], b.gainDb[i], t);
    }
    return v;
}

void fft(std::vector<float>& re, std::vector<float>& im, bool inverse) {
    const size_t n = re.size();
    for (size_t i = 1, j = 0; i < n; ++i) {
        size_t bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) { std::swap(re[i], re[j]); std::swap(im[i], im[j]); }
    }
    for (size_t len = 2; len <= n; len <<= 1) {
        double ang = kTwoPi / (double)len * (inverse ? 1 : -1);
        float wr = (float)std::cos(ang), wi = (float)std::sin(ang);
        for (size_t i = 0; i < n; i += len) {
            float cr = 1, ci = 0;
            for (size_t j = 0; j < len / 2; ++j) {
                float ur = re[i + j], ui = im[i + j];
                float vr = re[i + j + len / 2] * cr - im[i + j + len / 2] * ci;
                float vi = re[i + j + len / 2] * ci + im[i + j + len / 2] * cr;
                re[i + j] = ur + vr; im[i + j] = ui + vi;
                re[i + j + len / 2] = ur - vr; im[i + j + len / 2] = ui - vi;
                float ncr = cr * wr - ci * wi; ci = cr * wi + ci * wr; cr = ncr;
            }
        }
    }
    if (inverse) for (size_t i = 0; i < n; ++i) { re[i] /= (float)n; im[i] /= (float)n; }
}

namespace fx {

void gain(Buffer& b, float g) { for (auto& c : b.ch) for (auto& s : c) s *= g; }

float peak(const Buffer& b) {
    float p = 0; for (auto& c : b.ch) for (float s : c) p = std::max(p, std::abs(s)); return p;
}

float rms(const Buffer& b) {
    double acc = 0; size_t n = 0;
    for (auto& c : b.ch) { for (float s : c) acc += (double)s * s; n += c.size(); }
    return n ? (float)std::sqrt(acc / (double)n) : 0.0f;
}

void normalize(Buffer& b, float peakDb) {
    float p = peak(b);
    if (p > 1e-9f) gain(b, dbToGain(peakDb) / p);
}

void fadeIn(Buffer& b, double seconds) {
    int n = std::min(b.length(), (int)(seconds * b.sampleRate));
    for (auto& c : b.ch) for (int i = 0; i < n; ++i) c[(size_t)i] *= (float)i / (float)std::max(1, n);
}

void fadeOut(Buffer& b, double seconds) {
    int len = b.length();
    int n = std::min(len, (int)(seconds * b.sampleRate));
    for (auto& c : b.ch) for (int i = 0; i < n; ++i) {
        float t = (float)i / (float)std::max(1, n);
        c[(size_t)(len - n + i)] *= (1.0f - t) * (1.0f - t);
    }
}

void removeDC(Buffer& b) {
    float R = 1.0f - (float)(kTwoPi * 10.0 / b.sampleRate);
    for (auto& c : b.ch) {
        float x1 = 0, y1 = 0;
        for (auto& s : c) { float y = s - x1 + R * y1; x1 = s; y1 = y; s = y; }
    }
}

void trimTail(Buffer& b, float thresholdDb, double minSeconds, double fadeSeconds) {
    float thr = dbToGain(thresholdDb);
    int len = b.length(), last = 0;
    for (auto& c : b.ch) for (int i = len - 1; i >= 0; --i) if (std::abs(c[(size_t)i]) > thr) { last = std::max(last, i); break; }
    int newLen = std::max((int)(minSeconds * b.sampleRate), last + (int)(fadeSeconds * b.sampleRate));
    newLen = std::min(newLen, len);
    b.resize(newLen);
    fadeOut(b, fadeSeconds);
}

void trimLead(Buffer& b, float thresholdDb) {
    float thr = dbToGain(thresholdDb);
    int len = b.length(), first = len;
    for (auto& c : b.ch) for (int i = 0; i < len; ++i) if (std::abs(c[(size_t)i]) > thr) { first = std::min(first, i); break; }
    if (first <= 0 || first >= len) return;
    for (auto& c : b.ch) c.erase(c.begin(), c.begin() + first);
}

void saturate(Buffer& b, SatType type, float drive, float mix) {
    if (drive <= 0.0f || mix <= 0.0f) return;
    float pre = 1.0f + drive * 14.0f;
    float post = 1.0f / std::tanh(std::min(pre, 3.0f)) / (1.0f + drive * 0.6f);
    for (auto& c : b.ch) for (auto& s : c) {
        float y = shape(type, s * pre) * post;
        s = s * (1 - mix) + y * mix;
    }
}

void bitcrush(Buffer& b, int bits, int downsample, float mix) {
    if (mix <= 0) return;
    float q = std::pow(2.0f, (float)std::max(2, bits) - 1.0f);
    downsample = std::max(1, downsample);
    for (auto& c : b.ch) {
        float held = 0;
        for (size_t i = 0; i < c.size(); ++i) {
            if (i % (size_t)downsample == 0) held = std::round(c[i] * q) / q;
            c[i] = c[i] * (1 - mix) + held * mix;
        }
    }
}

static void svfProcess(Buffer& b, int type, double hz, double q) {
    for (auto& c : b.ch) {
        SVF f; f.set(b.sampleRate, hz, q);
        for (auto& s : c) {
            auto o = f.tick(s);
            s = type == 0 ? o.lp : type == 1 ? o.hp : o.bp * f.k;
        }
    }
}
void lowpass(Buffer& b, double hz, double q) { svfProcess(b, 0, hz, q); }
void highpass(Buffer& b, double hz, double q) { svfProcess(b, 1, hz, q); }
void bandpass(Buffer& b, double hz, double q) { svfProcess(b, 2, hz, q); }

void sweep(Buffer& b, int type, double startHz, double endHz, double q, double curve) {
    int len = b.length();
    for (auto& c : b.ch) {
        SVF f;
        for (int i = 0; i < len; ++i) {
            if ((i & 15) == 0) {
                double t = std::pow((double)i / std::max(1, len - 1), curve);
                double hz = startHz * std::pow(endHz / startHz, t);
                f.set(b.sampleRate, hz, q);
            }
            auto o = f.tick(c[(size_t)i]);
            c[(size_t)i] = type == 0 ? o.lp : type == 1 ? o.hp : o.bp * f.k;
        }
    }
}

void tilt(Buffer& b, float amount) {
    // Simple tilt EQ around ~800 Hz using a one-pole split.
    amount = clampf(amount, -1, 1);
    float lowG = dbToGain(-amount * 6.0f), highG = dbToGain(amount * 6.0f);
    for (auto& c : b.ch) {
        OnePole op; op.setLP(b.sampleRate, 800.0);
        for (auto& s : c) { float lo = op.lp(s); s = lo * lowG + (s - lo) * highG; }
    }
}

namespace {
struct Comb {
    std::vector<float> buf; size_t idx = 0; float store = 0;
    float process(float in, float fb, float damp) {
        float out = buf[idx];
        store = out * (1 - damp) + store * damp;
        buf[idx] = in + store * fb;
        if (++idx >= buf.size()) idx = 0;
        return out;
    }
};
struct Allpass {
    std::vector<float> buf; size_t idx = 0;
    float process(float in) {
        float bo = buf[idx]; float out = -in + bo;
        buf[idx] = in + bo * 0.5f;
        if (++idx >= buf.size()) idx = 0;
        return out;
    }
};
}

void reverb(Buffer& b, float size, float damp, float wet, float width, double predelayMs, double tailSec, bool wetOnly) {
    if (wet <= 0.0f && !wetOnly) return;
    static const int combT[8] = { 1116, 1188, 1277, 1356, 1422, 1491, 1557, 1617 };
    static const int apT[4] = { 556, 441, 341, 225 };
    const double scale = b.sampleRate / 44100.0;
    const int spread = (int)(23 * scale);
    const int len = b.length();
    const int tail = (int)(tailSec * b.sampleRate);
    const int pre = (int)(predelayMs * 0.001 * b.sampleRate);
    b.makeStereo();
    b.resize(len + tail);
    Comb cl[8], cr[8]; Allpass al[4], ar[4];
    for (int i = 0; i < 8; ++i) {
        cl[i].buf.assign((size_t)(combT[i] * scale), 0.0f);
        cr[i].buf.assign((size_t)((combT[i] + 23) * scale), 0.0f);
    }
    for (int i = 0; i < 4; ++i) {
        al[i].buf.assign((size_t)(apT[i] * scale), 0.0f);
        ar[i].buf.assign((size_t)(apT[i] * scale) + (size_t)spread, 0.0f);
    }
    float fb = 0.7f + clampf(size, 0, 1) * 0.28f;
    float dmp = clampf(damp, 0, 1) * 0.4f;
    float w1 = (width * 0.5f + 0.5f), w2 = (1 - width) * 0.5f;
    std::vector<float> preBuf((size_t)std::max(1, pre), 0.0f); size_t pIdx = 0;
    const int total = b.length();
    float* L = b.data(0); float* R = b.data(1);
    for (int i = 0; i < total; ++i) {
        float in = (L[i] + R[i]) * 0.5f * 0.03f;
        if (pre > 0) { float d = preBuf[pIdx]; preBuf[pIdx] = in; pIdx = (pIdx + 1) % preBuf.size(); in = d; }
        float ol = 0, orr = 0;
        for (int c = 0; c < 8; ++c) { ol += cl[c].process(in, fb, dmp); orr += cr[c].process(in, fb, dmp); }
        for (int a = 0; a < 4; ++a) { ol = al[a].process(ol); orr = ar[a].process(orr); }
        float wl = ol * w1 + orr * w2, wr = orr * w1 + ol * w2;
        float dry = wetOnly ? 0.0f : 1.0f;
        L[i] = L[i] * dry + wl * wet * 3.0f;
        R[i] = R[i] * dry + wr * wet * 3.0f;
    }
}

void delay(Buffer& b, double timeSec, float feedback, float wet, bool pingPong, double lpHz, double tailSec) {
    if (wet <= 0) return;
    int len = b.length();
    int d = std::max(1, (int)(timeSec * b.sampleRate));
    b.makeStereo();
    b.resize(len + (int)(tailSec * b.sampleRate));
    std::vector<float> bl((size_t)d, 0.0f), br((size_t)d, 0.0f);
    OnePole fl, fr; fl.setLP(b.sampleRate, lpHz); fr.setLP(b.sampleRate, lpHz);
    float* L = b.data(0); float* R = b.data(1);
    size_t idx = 0;
    for (int i = 0; i < b.length(); ++i) {
        float dl = bl[idx], dr = br[idx];
        float inL = L[i], inR = R[i];
        if (pingPong) { bl[idx] = fl.lp((inL + inR) * 0.5f + dr * feedback); br[idx] = fr.lp(dl * feedback); }
        else { bl[idx] = fl.lp(inL + dl * feedback); br[idx] = fr.lp(inR + dr * feedback); }
        L[i] = inL + dl * wet; R[i] = inR + dr * wet;
        if (++idx >= (size_t)d) idx = 0;
    }
}

void chorus(Buffer& b, float depthMs, float rateHz, float mix) {
    b.makeStereo();
    int len = b.length();
    int maxD = (int)((depthMs * 2 + 25) * 0.001 * b.sampleRate) + 4;
    std::vector<float> src = b.ch[0];
    for (size_t i = 0; i < src.size(); ++i) src[i] = 0.5f * (b.ch[0][i] + b.ch[1][i]);
    for (int c = 0; c < 2; ++c) {
        float* out = b.data(c);
        double ph = c * 0.25;
        for (int i = 0; i < len; ++i) {
            double mod = (15.0 + depthMs * (1.0 + std::sin(kTwoPi * ph))) * 0.001 * b.sampleRate;
            ph += rateHz / b.sampleRate;
            double rp = i - mod;
            int i0 = (int)std::floor(rp); float fr = (float)(rp - i0);
            float s0 = (i0 >= 0 && i0 < len) ? src[(size_t)i0] : 0.0f;
            float s1 = (i0 + 1 >= 0 && i0 + 1 < len) ? src[(size_t)i0 + 1] : 0.0f;
            float wetS = s0 + (s1 - s0) * fr;
            out[i] = out[i] * (1 - mix * 0.5f) + wetS * mix;
        }
        (void)maxD;
    }
}

void widen(Buffer& b, float amount) {
    if (b.numChannels() < 2) return;
    for (int i = 0; i < b.length(); ++i) {
        float m = 0.5f * (b.ch[0][(size_t)i] + b.ch[1][(size_t)i]);
        float s = 0.5f * (b.ch[0][(size_t)i] - b.ch[1][(size_t)i]) * (1.0f + amount);
        b.ch[0][(size_t)i] = m + s; b.ch[1][(size_t)i] = m - s;
    }
}

void reverse(Buffer& b) { for (auto& c : b.ch) std::reverse(c.begin(), c.end()); }

static inline float hermite(const std::vector<float>& x, double pos) {
    int i = (int)std::floor(pos); float t = (float)(pos - i);
    auto at = [&](int k) -> float { return (k >= 0 && k < (int)x.size()) ? x[(size_t)k] : 0.0f; };
    float xm1 = at(i - 1), x0 = at(i), x1 = at(i + 1), x2 = at(i + 2);
    float c0 = x0, c1 = 0.5f * (x1 - xm1), c2 = xm1 - 2.5f * x0 + 2 * x1 - 0.5f * x2, c3 = 0.5f * (x2 - xm1) + 1.5f * (x0 - x1);
    return ((c3 * t + c2) * t + c1) * t + c0;
}

void resample(Buffer& b, double ratio) {
    if (std::abs(ratio - 1.0) < 1e-6 || ratio <= 0) return;
    if (ratio > 1.0) lowpass(b, std::min(0.45 * b.sampleRate / ratio, 20000.0), 0.6);
    int newLen = std::max(1, (int)(b.length() / ratio));
    for (auto& c : b.ch) {
        std::vector<float> out((size_t)newLen);
        for (int i = 0; i < newLen; ++i) out[(size_t)i] = hermite(c, i * ratio);
        c.swap(out);
    }
}

Buffer resampleTo(const Buffer& b, double newRate) {
    Buffer out = b;
    if (std::abs(newRate - b.sampleRate) < 0.5) return out;
    double ratio = b.sampleRate / newRate;
    if (ratio > 1.0) { lowpass(out, 0.46 * newRate, 0.55); lowpass(out, 0.46 * newRate, 0.55); }
    int newLen = std::max(1, (int)std::llround(b.length() / ratio));
    for (auto& c : out.ch) {
        std::vector<float> o((size_t)newLen);
        for (int i = 0; i < newLen; ++i) o[(size_t)i] = hermite(c, i * ratio);
        c.swap(o);
    }
    out.sampleRate = newRate;
    return out;
}

void compress(Buffer& b, float thresholdDb, float ratio, double attackMs, double releaseMs, float makeupDb) {
    float aA = (float)std::exp(-1.0 / (attackMs * 0.001 * b.sampleRate));
    float aR = (float)std::exp(-1.0 / (releaseMs * 0.001 * b.sampleRate));
    float env = 0, mk = dbToGain(makeupDb);
    int len = b.length();
    for (int i = 0; i < len; ++i) {
        float lvl = 0; for (auto& c : b.ch) lvl = std::max(lvl, std::abs(c[(size_t)i]));
        env = lvl > env ? aA * env + (1 - aA) * lvl : aR * env + (1 - aR) * lvl;
        float db = gainToDb(env);
        float gr = db > thresholdDb ? (thresholdDb + (db - thresholdDb) / ratio) - db : 0.0f;
        float g = dbToGain(gr) * mk;
        for (auto& c : b.ch) c[(size_t)i] *= g;
    }
}

void softLimit(Buffer& b, float ceilingDb) {
    float c = dbToGain(ceilingDb);
    for (auto& ch : b.ch) for (auto& s : ch) {
        float x = s / c;
        if (std::abs(x) > 0.6f) { float sgn = x < 0 ? -1.0f : 1.0f; float a = std::abs(x); x = sgn * (0.6f + 0.4f * std::tanh((a - 0.6f) / 0.4f)); }
        s = x * c;
    }
}

void tapeWow(Buffer& b, float depth, float rateHz, uint64_t seed) {
    if (depth <= 0) return;
    Rng r(seed);
    int len = b.length();
    double maxDev = depth * 0.004 * b.sampleRate; // up to ~4 ms
    double ph = r.uni(), ph2 = r.uni();
    for (auto& c : b.ch) {
        std::vector<float> src = c;
        double p1 = ph, p2 = ph2;
        for (int i = 0; i < len; ++i) {
            double mod = maxDev * (0.5 + 0.35 * std::sin(kTwoPi * p1) + 0.15 * std::sin(kTwoPi * p2));
            p1 += rateHz / b.sampleRate; p2 += rateHz * 7.3 / b.sampleRate;
            c[(size_t)i] = hermite(src, i - mod);
        }
    }
}

void addNoise(Buffer& b, float levelDb, int type, uint64_t seed, bool crackle) {
    Noise n(seed); Rng r(seed ^ 77);
    float g = dbToGain(levelDb);
    for (int i = 0; i < b.length(); ++i) {
        float v = type == 0 ? n.white() : type == 1 ? n.pink() : n.brown();
        float v2 = 0;
        if (crackle && r.chance(0.00025 * 44100.0 / b.sampleRate)) v2 = r.bipolar() * 8.0f;
        for (auto& c : b.ch) c[(size_t)i] += (v + v2) * g;
    }
}

void transient(Buffer& b, float attackDb, float sustainDb) {
    float fastA = (float)std::exp(-1.0 / (0.0007 * b.sampleRate)), fastR = (float)std::exp(-1.0 / (0.02 * b.sampleRate));
    float slowA = (float)std::exp(-1.0 / (0.015 * b.sampleRate)), slowR = (float)std::exp(-1.0 / (0.08 * b.sampleRate));
    for (auto& c : b.ch) {
        float ef = 0, es = 0;
        for (auto& s : c) {
            float a = std::abs(s);
            ef = a > ef ? fastA * ef + (1 - fastA) * a : fastR * ef + (1 - fastR) * a;
            es = a > es ? slowA * es + (1 - slowA) * a : slowR * es + (1 - slowR) * a;
            float diff = clampf((ef - es) / (es + 1e-4f), 0.0f, 1.0f); // ~1 during attacks
            float gDb = diff * attackDb + (1 - diff) * sustainDb;
            s *= dbToGain(gDb);
        }
    }
}

void ringMod(Buffer& b, double hz, float mix) {
    for (auto& c : b.ch) {
        double ph = 0;
        for (auto& s : c) { float m = (float)std::sin(kTwoPi * ph); ph += hz / b.sampleRate; s = s * (1 - mix) + s * m * mix; }
    }
}

void stutter(Buffer& b, uint64_t seed, float amount, double bpm) {
    // Buffer-repeat style glitch: copy short slices (1/32..1/8) and repeat them.
    Rng r(seed);
    int len = b.length();
    double sixteenth = 60.0 / bpm / 4.0 * b.sampleRate;
    int pos = 0;
    Buffer src = b;
    while (pos < len) {
        int divIdx = r.irange(0, 3);
        int slice = std::max(64, (int)(sixteenth * (divIdx == 0 ? 0.5 : divIdx == 1 ? 1.0 : divIdx == 2 ? 0.25 : 2.0)));
        bool rep = r.chance(amount);
        int reps = rep ? r.irange(2, 6) : 1;
        int from = rep ? std::min(len - 1, r.irange(0, std::max(0, len - slice))) : pos;
        float pitchMul = rep && r.chance(0.3) ? (float)std::pow(2.0, r.irange(-12, 12) / 12.0) : 1.0f;
        for (int k = 0; k < reps && pos < len; ++k) {
            for (int i = 0; i < slice && pos < len; ++i, ++pos) {
                double sp = from + i * (double)pitchMul;
                float env = std::min(1.0f, std::min((float)i / 32.0f, (float)(slice - i) / 32.0f));
                for (int c = 0; c < b.numChannels(); ++c) {
                    float v = hermite(src.ch[(size_t)c], std::fmod(sp, (double)len));
                    b.ch[(size_t)c][(size_t)pos] = v * env;
                }
            }
        }
    }
}

void mixInto(Buffer& dst, const Buffer& src, int offset, float g) {
    if (src.numChannels() == 2) dst.makeStereo();
    int need = offset + src.length();
    if (dst.length() < need) dst.resize(need);
    for (int c = 0; c < dst.numChannels(); ++c) {
        const auto& s = src.ch[(size_t)std::min(c, src.numChannels() - 1)];
        for (int i = 0; i < src.length(); ++i) if (offset + i >= 0) dst.ch[(size_t)c][(size_t)(offset + i)] += s[(size_t)i] * g;
    }
}

float spectralCentroid(const Buffer& b, int maxFrames) {
    const int N = 2048;
    int len = b.length();
    if (len < N) {
        Buffer p = b; p.resize(N);
        return spectralCentroid(p, 1);
    }
    int hop = std::max(N, (len - N) / std::max(1, maxFrames));
    double num = 0, den = 0;
    std::vector<float> re(N), im(N);
    for (int start = 0; start + N <= len; start += hop) {
        for (int i = 0; i < N; ++i) {
            float w = 0.5f - 0.5f * (float)std::cos(kTwoPi * i / (N - 1));
            float s = 0; for (auto& c : b.ch) s += c[(size_t)(start + i)];
            re[(size_t)i] = s * w; im[(size_t)i] = 0;
        }
        fft(re, im, false);
        for (int k = 1; k < N / 2; ++k) {
            double mag = std::sqrt((double)re[(size_t)k] * re[(size_t)k] + (double)im[(size_t)k] * im[(size_t)k]);
            double hz = k * b.sampleRate / N;
            num += hz * mag; den += mag;
        }
    }
    return den > 0 ? (float)(num / den) : 0.0f;
}

Buffer granulate(const Buffer& src, double outSeconds, const GrainParams& gp, uint64_t seed) {
    Rng r(seed);
    Buffer out(2, (int)(outSeconds * src.sampleRate), src.sampleRate);
    if (src.empty()) return out;
    int outLen = out.length(), srcLen = src.length();
    double grainLen = gp.grainMs * 0.001 * src.sampleRate;
    int numGrains = std::max(1, (int)(gp.density * outSeconds));
    for (int g = 0; g < numGrains; ++g) {
        int start = (int)(r.uni() * std::max(1.0, outLen - grainLen * 0.5));
        double gl = grainLen * r.range(0.6, 1.4);
        double pos = clampd(gp.position + r.bipolar() * gp.positionJitter, 0.0, 1.0) * std::max(0.0, srcLen - gl);
        double semis = gp.pitchSemis + r.gauss() * gp.pitchJitter;
        if (!gp.pitchSet.empty()) semis = gp.pitchSemis + gp.pitchSet[(size_t)r.irange(0, (int)gp.pitchSet.size() - 1)];
        double rate = std::pow(2.0, semis / 12.0);
        bool rev = r.chance(gp.reverseProb);
        float pan = (float)(r.bipolar() * gp.spread);
        float gl_ = std::cos((pan + 1) * 0.25f * (float)kPi), gr_ = std::sin((pan + 1) * 0.25f * (float)kPi);
        float amp = (float)r.range(0.5, 1.0);
        int n = (int)gl;
        for (int i = 0; i < n && start + i < outLen; ++i) {
            float w = 0.5f - 0.5f * (float)std::cos(kTwoPi * i / std::max(1, n - 1));
            double sp = rev ? pos + (n - i) * rate : pos + i * rate;
            float s = 0;
            for (int c = 0; c < src.numChannels(); ++c) s += hermite(src.ch[(size_t)c], std::fmod(sp, (double)srcLen));
            s /= (float)src.numChannels();
            out.ch[0][(size_t)(start + i)] += s * w * gl_ * amp;
            out.ch[1][(size_t)(start + i)] += s * w * gr_ * amp;
        }
    }
    float norm = 1.0f / std::sqrt(std::max(1.0f, (float)(gp.density * gp.grainMs * 0.001)));
    gain(out, norm);
    return out;
}

} // namespace fx
} // namespace sdna
