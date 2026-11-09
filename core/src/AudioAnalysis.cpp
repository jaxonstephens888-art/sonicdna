#include "sdna/AudioAnalysis.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <numeric>

namespace sdna {

namespace {

constexpr double kSR = 22050.0;  // analysis rate
constexpr int kN = 2048, kH = 512, kBins = kN / 2 + 1;
constexpr double kFps = kSR / kH;
constexpr double kBinHz = kSR / kN;

float percentile(std::vector<float> v, float p) {
    if (v.empty()) return 0.0f;
    size_t k = (size_t)std::clamp((double)p * (double)(v.size() - 1), 0.0, (double)(v.size() - 1));
    std::nth_element(v.begin(), v.begin() + (long)k, v.end());
    return v[k];
}
float median(std::vector<float> v) { return percentile(std::move(v), 0.5f); }
float mapRange(float x, float a, float b) { return clampf((x - a) / (b - a), 0.0f, 1.0f); }
// Sample index where an onset detected at flux frame `fr` begins (window centre, one hop earlier).
int onsetSample(int fr) { return std::max(0, (fr - 1) * 512 + 1024 - 256); }
int binOf(double hz) { return std::clamp((int)std::lround(hz / kBinHz), 0, kBins - 1); }

// Interpolated read of an envelope at a fractional frame.
float envAt(const std::vector<float>& e, double f) {
    if (f < 0 || f >= (double)e.size() - 1) return 0.0f;
    int i = (int)f; float t = (float)(f - i);
    return e[(size_t)i] * (1 - t) + e[(size_t)i + 1] * t;
}
float envMax(const std::vector<float>& e, double from, double to) {
    float m = 0;
    for (int i = std::max(0, (int)std::floor(from)); i <= std::min((int)e.size() - 1, (int)std::ceil(to)); ++i) m = std::max(m, e[(size_t)i]);
    return m;
}

// Remove slow trends and keep rises only.
void normaliseEnvelope(std::vector<float>& e) {
    int w = (int)(kFps * 0.4);
    std::vector<float> out(e.size());
    double acc = 0; int n = 0;
    std::vector<double> pref(e.size() + 1, 0.0);
    for (size_t i = 0; i < e.size(); ++i) pref[i + 1] = pref[i] + e[i];
    for (size_t i = 0; i < e.size(); ++i) {
        int a = std::max(0, (int)i - w), b = std::min((int)e.size(), (int)i + w + 1);
        float mean = (float)((pref[(size_t)b] - pref[(size_t)a]) / (b - a));
        out[i] = std::max(0.0f, e[i] - mean);
    }
    (void)acc; (void)n;
    float p = percentile(out, 0.98f);
    if (p > 0) for (auto& x : out) x = std::min(1.5f, x / p);
    e.swap(out);
}

// Local maxima above a threshold, at least `minGap` frames apart.
std::vector<int> peaks(const std::vector<float>& e, float thr, int minGap) {
    std::vector<int> p;
    for (int i = 1; i + 1 < (int)e.size(); ++i) {
        if (e[(size_t)i] < thr || e[(size_t)i] < e[(size_t)i - 1] || e[(size_t)i] < e[(size_t)i + 1]) continue;
        if (!p.empty() && i - p.back() < minGap) { if (e[(size_t)i] > e[(size_t)p.back()]) p.back() = i; continue; }
        p.push_back(i);
    }
    return p;
}

// Zero-crossing frequency of x[from..to).
float zcFreq(const std::vector<float>& x, int from, int to) {
    from = std::max(1, from); to = std::min((int)x.size(), to);
    if (to - from < 16) return 0.0f;
    int crossings = 0; int first = -1, last = -1;
    for (int i = from; i < to; ++i) if ((x[(size_t)i - 1] < 0) != (x[(size_t)i] < 0) && x[(size_t)i - 1] <= 0) { ++crossings; if (first < 0) first = i; last = i; }
    if (crossings < 2) return 0.0f;
    return (float)((crossings - 1) * kSR / (double)(last - first));
}

// Time for the envelope of x[start..end) to fall `dropDb` below its peak; returns seconds (or the window length).
// Estimated T60 of a hit in x[start..end): measures how fast the envelope falls from its peak,
// stopping before it reaches the background level (so overlapping bass/pads don't inflate it).
float decayTime(const std::vector<float>& x, int start, int end, float dropDb) {
    end = std::min(end, (int)x.size());
    const int win = (int)(0.005 * kSR);       // hop
    const int span = (int)(0.025 * kSR);      // RMS window longer than one cycle of a 40 Hz kick
    double pre = 0; int preN = 0;
    for (int i = std::max(0, start - (int)(0.04 * kSR)); i < start; ++i) { pre += (double)x[(size_t)i] * x[(size_t)i]; ++preN; }
    float background = preN ? (float)std::sqrt(pre / preN) : 0.0f;
    float peak = 0; int peakAt = start;
    std::vector<float> env;
    for (int i = start; i + span <= end; i += win) {
        double acc = 0; for (int k = 0; k < span; ++k) acc += (double)x[(size_t)(i + k)] * x[(size_t)(i + k)];
        env.push_back((float)std::sqrt(acc / span));
    }
    for (size_t i = 0; i < env.size(); ++i) if (env[i] > peak) { peak = env[i]; peakAt = (int)i; }
    if (peak <= 1e-6f) return 0.0f;
    // remove the energy that was already sounding before the hit
    for (auto& e : env) e = std::sqrt(std::max(0.0f, e * e - background * background));
    peak = 0; for (size_t i = 0; i < env.size(); ++i) if (env[i] > peak) { peak = env[i]; peakAt = (int)i; }
    if (peak <= 1e-6f) return 0.0f;
    float room = gainToDb(peak) - gainToDb(background * 0.6f + 1e-6f);
    float drop = std::clamp(std::min(dropDb, room), 6.0f, dropDb);
    float thr = peak * dbToGain(-drop);
    double t = (double)(env.size() - (size_t)peakAt) * win / kSR;
    for (size_t i = (size_t)peakAt; i < env.size(); ++i) if (env[i] < thr) { t = (double)(i - (size_t)peakAt) * win / kSR; break; }
    return (float)(t * 60.0 / drop); // extrapolate the measured slope to a T60
}

// Fundamental of a low drum hit: strongest 28–160 Hz peak of (hit spectrum − pre-hit spectrum).
// Subtracting what was already sounding removes sustained bass notes under the kick.
float hitFundamental(const std::vector<float>& x, int onset, int from, int to) {
    const int NF = 8192;
    int len = std::min(to - from, NF);
    if (from < 0 || from + len > (int)x.size() || len < 512) return 0.0f;
    auto spec = [&](int s, int n, std::vector<float>& out) {
        std::vector<float> re(NF, 0.0f), im(NF, 0.0f);
        for (int i = 0; i < n; ++i) re[(size_t)i] = x[(size_t)(s + i)] * (0.5f - 0.5f * (float)std::cos(kTwoPi * i / (n - 1)));
        fft(re, im, false);
        out.resize(NF / 2);
        for (int k = 0; k < NF / 2; ++k) out[(size_t)k] = std::sqrt(re[(size_t)k] * re[(size_t)k] + im[(size_t)k] * im[(size_t)k]);
    };
    std::vector<float> hit, bg;
    spec(from, len, hit);
    int preStart = onset - len;
    if (preStart >= 0) spec(preStart, len, bg); else bg.assign(hit.size(), 0.0f);
    double hz = kSR / NF;
    int k0 = (int)(28 / hz), k1 = (int)(160 / hz);
    int best = -1; float bv = 0;
    for (int k = k0; k <= k1; ++k) { float v = hit[(size_t)k] - 1.2f * bg[(size_t)k]; if (v > bv) { bv = v; best = k; } }
    if (best <= k0 || best >= k1) return 0.0f;
    float a = hit[(size_t)best - 1] - bg[(size_t)best - 1], b = hit[(size_t)best] - bg[(size_t)best], c = hit[(size_t)best + 1] - bg[(size_t)best + 1];
    float d = (a - 2 * b + c) != 0 ? 0.5f * (a - c) / (a - 2 * b + c) : 0.0f;
    return (float)((best + clampf(d, -0.5f, 0.5f)) * hz);
}

// Low-frequency spectrum (28–160 Hz) of a hit with part of the pre-hit spectrum removed, normalised to 1.
std::vector<float> hitLowSpectrum(const std::vector<float>& x, int onset, int from, int to) {
    const int NF = 8192;
    int len = std::min(to - from, NF);
    if (from < 0 || from + len > (int)x.size() || len < 512) return {};
    auto spec = [&](int s, int n) {
        std::vector<float> re(NF, 0.0f), im(NF, 0.0f), out(NF / 2);
        for (int i = 0; i < n; ++i) re[(size_t)i] = x[(size_t)(s + i)] * (0.5f - 0.5f * (float)std::cos(kTwoPi * i / (n - 1)));
        fft(re, im, false);
        for (int k = 0; k < NF / 2; ++k) out[(size_t)k] = std::sqrt(re[(size_t)k] * re[(size_t)k] + im[(size_t)k] * im[(size_t)k]);
        return out;
    };
    auto hit = spec(from, len);
    std::vector<float> bg = onset - len >= 0 ? spec(onset - len, len) : std::vector<float>(hit.size(), 0.0f);
    double hz = kSR / NF;
    int k0 = (int)(28 / hz), k1 = (int)(160 / hz);
    std::vector<float> v((size_t)(k1 - k0 + 1));
    float mx = 0;
    for (int k = k0; k <= k1; ++k) { v[(size_t)(k - k0)] = std::max(0.0f, hit[(size_t)k] - 0.6f * bg[(size_t)k]); mx = std::max(mx, v[(size_t)(k - k0)]); }
    if (mx <= 0) return {};
    for (auto& q : v) q /= mx;
    return v;
}
constexpr double kLowSpecHz = 22050.0 / 8192.0;
constexpr int kLowSpecK0 = (int)(28 / kLowSpecHz);

float segmentCentroid(const std::vector<float>& x, int start, int len) {
    Buffer b(1, std::max(0, std::min(len, (int)x.size() - start)), kSR);
    if (b.length() < 64) return 0.0f;
    std::copy(x.begin() + start, x.begin() + start + b.length(), b.ch[0].begin());
    return fx::spectralCentroid(b, 4);
}

std::vector<float> filtered(const std::vector<float>& x, int type, double hz, double q, int passes) {
    Buffer b(1, (int)x.size(), kSR); b.ch[0] = x;
    for (int p = 0; p < passes; ++p) { if (type == 0) fx::lowpass(b, hz, q); else if (type == 1) fx::highpass(b, hz, q); else fx::bandpass(b, hz, q); }
    return b.ch[0];
}

const float kMajor[12] = { 6.35f, 2.23f, 3.48f, 2.33f, 4.38f, 4.09f, 2.52f, 5.19f, 2.39f, 3.66f, 2.29f, 2.88f };
const float kMinor[12] = { 6.33f, 2.68f, 3.52f, 5.38f, 2.60f, 3.53f, 2.54f, 4.75f, 3.98f, 2.69f, 3.34f, 3.17f };

float pearson(const float* a, const float* b, int n) {
    double ma = 0, mb = 0; for (int i = 0; i < n; ++i) { ma += a[i]; mb += b[i]; } ma /= n; mb /= n;
    double num = 0, da = 0, db = 0;
    for (int i = 0; i < n; ++i) { num += (a[i] - ma) * (b[i] - mb); da += (a[i] - ma) * (a[i] - ma); db += (b[i] - mb) * (b[i] - mb); }
    return (da > 0 && db > 0) ? (float)(num / std::sqrt(da * db)) : 0.0f;
}

} // namespace

// ---------------------------------------------------------------------------
ReferenceFeatures analyseReference(const Buffer& input, const std::string& name, const std::function<bool(float)>& progress) {
    ReferenceFeatures f;
    f.name = name;
    f.seconds = input.seconds();
    auto report = [&](float p) { return !progress || progress(p); };
    if (input.empty() || input.seconds() < 3.0) return f;

    // ---- choose an analysis window (skip intros on long tracks) ----
    const double maxSec = 150.0;
    int len = input.length(), start = 0;
    if (input.seconds() > maxSec) { start = (int)(len * 0.15); len = (int)(maxSec * input.sampleRate); }
    Buffer win(input.numChannels(), len, input.sampleRate);
    for (int c = 0; c < input.numChannels(); ++c) std::copy(input.ch[(size_t)c].begin() + start, input.ch[(size_t)c].begin() + start + len, win.ch[(size_t)c].begin());

    // Stereo width (side / mid energy) on the original.
    if (win.numChannels() == 2) {
        double m = 0, s = 0;
        for (int i = 0; i < win.length(); ++i) { float a = win.ch[0][(size_t)i], b = win.ch[1][(size_t)i]; m += (a + b) * (a + b); s += (a - b) * (a - b); }
        f.width = m > 0 ? (float)std::sqrt(s / m) : 0.0f;
    }
    // Dynamics on the original
    {
        double acc = 0; float pk = 0; size_t clip = 0, n = 0;
        for (auto& c : win.ch) for (float x : c) { acc += (double)x * x; pk = std::max(pk, std::abs(x)); if (std::abs(x) > 0.985f) ++clip; ++n; }
        float rms = (float)std::sqrt(acc / std::max<size_t>(1, n));
        f.rmsDb = gainToDb(rms);
        f.crestDb = gainToDb(pk) - f.rmsDb;
        f.clipRatio = (float)clip / (float)std::max<size_t>(1, n);
    }
    Buffer mono = win; mono.makeMono();
    // Long-term spectrum at the file's own rate (the analysis rate below caps at 11 kHz).
    std::vector<double> fullLtas(kBins, 0.0);
    double fullBinHz = mono.sampleRate / kN;
    {
        std::vector<float> fr(kN), fi(kN);
        int nf = 0;
        const float* s = mono.data(0);
        for (int p0 = 0; p0 + kN <= mono.length(); p0 += kN * 2, ++nf) {
            for (int i = 0; i < kN; ++i) { fr[(size_t)i] = s[p0 + i] * (0.5f - 0.5f * (float)std::cos(kTwoPi * i / (kN - 1))); fi[(size_t)i] = 0; }
            fft(fr, fi, false);
            for (int k = 0; k < kBins; ++k) fullLtas[(size_t)k] += (double)fr[(size_t)k] * fr[(size_t)k] + (double)fi[(size_t)k] * fi[(size_t)k];
        }
        for (auto& v : fullLtas) v /= std::max(1, nf);
    }
    Buffer a = fx::resampleTo(mono, kSR);
    std::vector<float> x = a.ch[0];
    // Normalise analysis signal so thresholds are level-independent.
    { float pk = 0; for (float v : x) pk = std::max(pk, std::abs(v)); if (pk > 1e-6f) for (auto& v : x) v /= pk; }
    f.analysedSeconds = x.size() / kSR;
    if (!report(0.1f)) return f;

    // ---- STFT ----
    const int frames = ((int)x.size() - kN) / kH + 1;
    if (frames < 64) return f;
    std::vector<float> mag((size_t)frames * kBins), frameRms((size_t)frames);
    std::vector<float> hann(kN); for (int i = 0; i < kN; ++i) hann[(size_t)i] = 0.5f - 0.5f * (float)std::cos(kTwoPi * i / (kN - 1));
    std::vector<float> re(kN), im(kN);
    for (int fr = 0; fr < frames; ++fr) {
        const float* s = x.data() + (size_t)fr * kH;
        double e = 0;
        for (int i = 0; i < kN; ++i) { re[(size_t)i] = s[i] * hann[(size_t)i]; im[(size_t)i] = 0; e += (double)s[i] * s[i]; }
        frameRms[(size_t)fr] = (float)std::sqrt(e / kN);
        fft(re, im, false);
        float* m = mag.data() + (size_t)fr * kBins;
        for (int k = 0; k < kBins; ++k) m[k] = std::sqrt(re[(size_t)k] * re[(size_t)k] + im[(size_t)k] * im[(size_t)k]);
        if ((fr & 255) == 0 && !report(0.1f + 0.35f * fr / frames)) return f;
    }
    auto M = [&](int fr, int k) { return mag[(size_t)fr * kBins + (size_t)k]; };

    // ---- long-term spectrum (full rate) ----
    const std::vector<double>& ltas = fullLtas;
    auto binF = [&](double hz) { return std::clamp((int)std::lround(hz / fullBinHz), 0, kBins - 1); };
    const double binHz = fullBinHz;
    double total = 0, num = 0;
    for (int k = 1; k < kBins; ++k) { total += ltas[(size_t)k]; num += ltas[(size_t)k] * k * binHz; }
    f.centroidHz = total > 0 ? (float)(num / total) : 0.0f;
    { double acc = 0; for (int k = 1; k < kBins; ++k) { acc += ltas[(size_t)k]; if (acc >= total * 0.85) { f.rolloffHz = (float)(k * binHz); break; } } }
    {
        double sub = 0, high = 0; for (int k = 1; k < kBins; ++k) { double hz = k * binHz; if (hz < 120) sub += ltas[(size_t)k]; if (hz > 6000) high += ltas[(size_t)k]; }
        f.subRatio = total > 0 ? (float)(sub / total) : 0; f.highRatio = total > 0 ? (float)(high / total) : 0;
    }
    {
        // Smoothed LTAS in dB; bandwidth = highest frequency within 55 dB of the 200 Hz–4 kHz level.
        std::vector<float> db(kBins);
        for (int k = 0; k < kBins; ++k) db[(size_t)k] = 10.0f * (float)std::log10(ltas[(size_t)k] + 1e-14);
        double ref = 0; int cnt = 0; for (int k = binF(200); k <= binF(4000); ++k) { ref += db[(size_t)k]; ++cnt; } ref /= std::max(1, cnt);
        f.bandwidthHz = (float)binHz;
        for (int k = kBins - 1; k > binF(1000); --k) {
            double avg = 0; int c2 = 0; for (int j = std::max(0, k - 4); j <= k; ++j) { avg += db[(size_t)j]; ++c2; } avg /= c2;
            if (avg > ref - 55.0) { f.bandwidthHz = (float)(k * binHz); break; }
        }
        // Tilt: slope of dB against octaves, 100 Hz–8 kHz.
        double sx = 0, sy = 0, sxx = 0, sxy = 0; int n = 0;
        for (int k = binF(100); k <= binF(8000); ++k) { double ox = std::log2(k * binHz / 1000.0), y = db[(size_t)k]; sx += ox; sy += y; sxx += ox * ox; sxy += ox * y; ++n; }
        f.tiltDbPerOct = (float)((n * sxy - sx * sy) / std::max(1e-9, n * sxx - sx * sx));
    }
    // Spectral flatness 200 Hz–8 kHz (mean over frames)
    {
        double accF = 0; int n = 0;
        for (int fr = 0; fr < frames; fr += 2) {
            double lg = 0, ar = 0; int c = 0;
            for (int k = binOf(200); k <= binOf(8000); ++k) { double p = (double)M(fr, k) * M(fr, k) + 1e-12; lg += std::log(p); ar += p; ++c; }
            if (ar / c < 1e-8) continue;
            accF += std::exp(lg / c) / (ar / c); ++n;
        }
        f.flatness = n ? (float)(accF / n) : 0.0f;
    }
    // Loud vs quiet frames (space / density) and noise floor
    {
        std::vector<float> dbs; dbs.reserve(frameRms.size());
        for (float r : frameRms) dbs.push_back(gainToDb(r));
        float p10 = percentile(dbs, 0.10f), p90 = percentile(dbs, 0.90f), p02 = percentile(dbs, 0.02f);
        f.dynamicGapDb = p90 - p10;
        f.noiseFloorDb = p02 + f.rmsDb - p90; // relative to the original level
    }
    if (!report(0.5f)) return f;

    // ---- onset envelopes ----
    // Raw spectral flux per band (mean per bin, so bands are comparable).
    auto bandFlux = [&](std::vector<std::pair<double, double>> ranges) {
        std::vector<float> e((size_t)frames, 0.0f);
        int bins = 0; for (auto& r : ranges) bins += binOf(r.second) - binOf(r.first) + 1;
        for (int fr = 1; fr < frames; ++fr) {
            double s = 0;
            for (auto& r : ranges) for (int k = binOf(r.first); k <= binOf(r.second); ++k)
                s += std::max(0.0, std::log1p(100.0 * M(fr, k)) - std::log1p(100.0 * M(fr - 1, k)));
            e[(size_t)fr] = (float)(s / std::max(1, bins));
        }
        return e;
    };
    std::vector<float> full = bandFlux({ { 30, 10000 } });
    std::vector<float> low = bandFlux({ { 35, 140 } });
    std::vector<float> lowMid = bandFlux({ { 150, 400 } });
    std::vector<float> mid = bandFlux({ { 1200, 5000 } });
    std::vector<float> high = bandFlux({ { 7000, 10500 } });
    // Separate instruments by spectral shape: a snare rises at least as much at 1-5 kHz as at 7-10 kHz,
    // a hi-hat rises mostly above 7 kHz, a kick rises mostly below 150 Hz.
    std::vector<float> kickE(low), snareE(mid), hatE(high);
    for (size_t i = 0; i < mid.size(); ++i) {
        kickE[i] = std::max(0.0f, low[i] - 0.5f * mid[i]); // broadband noise (snares, crashes) is not a kick
        snareE[i] = std::max(0.0f, mid[i] - 0.7f * high[i] - 0.3f * low[i]) + 0.3f * std::max(0.0f, lowMid[i] - 0.8f * low[i]);
        hatE[i] = std::max(0.0f, high[i] - 0.6f * mid[i]);
    }
    normaliseEnvelope(full); normaliseEnvelope(kickE); normaliseEnvelope(snareE); normaliseEnvelope(hatE);
    low = kickE; mid = snareE; high = hatE;
    if (!report(0.6f)) return f;

    // ---- tempo: comb over the onset autocorrelation ----
    std::vector<float> pulse(full.size()); // the envelope that carries the beat (kick + snare when present)
    const int maxLag = (int)(kFps * 4.2);
    std::vector<float> ac((size_t)maxLag + 2, 0.0f);
    {
        // Kick + snare carry the pulse; hi-hats (often every 16th) would make every grid multiple look like a tempo.
        std::vector<float>& te = pulse;
        double lm = 0, fe = 0; for (size_t i = 0; i < full.size(); ++i) { lm += low[i] + mid[i]; fe += full[i]; }
        bool drums = lm > 0.25 * fe; // clear kick/snare activity -> ignore hats and pads for the pulse
        // Only clear hits count; low-level bleed (hats in the snare band, bass in the kick band) is gated out.
        auto gate = [](float v) { return std::max(0.0f, v - 0.3f); };
        for (size_t i = 0; i < full.size(); ++i) te[i] = drums ? gate(low[i]) + gate(mid[i]) : gate(full[i]);
        double mean = std::accumulate(te.begin(), te.end(), 0.0) / te.size();
        std::vector<float> z(te.size()); for (size_t i = 0; i < te.size(); ++i) z[i] = (float)(te[i] - mean);
        double e0 = 0; for (float v : z) e0 += (double)v * v;
        for (int lag = 1; lag <= maxLag + 1 && lag < (int)z.size(); ++lag) {
            double s = 0; for (size_t i = (size_t)lag; i < z.size(); ++i) s += (double)z[i] * z[i - (size_t)lag];
            // unbiased: normalise by the number of overlapping frames
            ac[(size_t)lag] = (float)((s / (double)(z.size() - (size_t)lag)) / std::max(1e-12, e0 / (double)z.size()));
        }
    }
    auto acAt = [&](double lag) { if (lag < 1 || lag >= maxLag) return 0.0f; int i = (int)lag; float t = (float)(lag - i); return ac[(size_t)i] * (1 - t) + ac[(size_t)i + 1] * t; };
    auto score = [&](double bpm) {
        double beat = 60.0 * kFps / bpm, s = 0;
        const double w[4] = { 1.0, 1.0, 0.3, 1.5 };
        for (int k = 1; k <= 4; ++k) s += w[k - 1] * acAt(beat * k);
        double oct = std::log2(bpm / 115.0);
        return s * std::exp(-0.5 * oct * oct / (0.9 * 0.9)); // mild prior around 115 BPM
    };
    double bestBpm = 120, best = -1e9; std::vector<double> scores;
    for (double b = 60.0; b <= 200.0; b += 0.1) { double s = score(b); scores.push_back(s); if (s > best) { best = s; bestBpm = b; } }
    {
        double m = 0, sd = 0; for (double s : scores) m += s; m /= scores.size(); for (double s : scores) sd += (s - m) * (s - m); sd = std::sqrt(sd / scores.size());
        f.tempoConfidence = (float)clampd((best - m) / std::max(1e-9, sd) / 4.0, 0.0, 1.0);
    }
    // Half/double-time ambiguity (e.g. trap at 70 vs 140): producers label tempos in roughly 80–180 BPM.
    if (bestBpm < 80.0 && bestBpm * 2 <= 185.0 && score(bestBpm * 2) > 0.4 * best) bestBpm *= 2;
    else if (bestBpm > 185.0 && score(bestBpm / 2) > 0.4 * best) bestBpm /= 2;
    f.bpm = (float)std::round(bestBpm); // productions are almost always at integer tempos

    // ---- beat phase & bar alignment ----
    const double P = 60.0 * kFps / f.bpm, step = P / 4.0;
    double phase = 0; { double bs = -1; for (double ph = 0; ph < P; ph += 0.25) { double s = 0; for (double t = ph; t < frames; t += P) s += envAt(pulse, t) + 0.25f * envAt(full, t); if (s > bs) { bs = s; phase = ph; } } }
    int bars = (int)((frames - phase) / (4 * P));
    double barStart = phase;
    {
        // Try the 4 possible downbeats; score the resulting kick/snare pattern like a drummer would:
        // kick on 1, no snare on 1, snare on the backbeat (2 & 4, or 3 in half-time).
        double bs = -1e9;
        int nb = std::max(1, bars - 1);
        for (int r = 0; r < 4; ++r) {
            double o = phase + r * P;
            auto stepHit = [&](const std::vector<float>& e, int s) {
                float thr = 0.35f; int hits = 0;
                for (int b = 0; b < nb; ++b) { double t = o + b * 4 * P + s * step; if (envMax(e, t - step * 0.35, t + step * 0.45) > thr) ++hits; }
                return (double)hits / nb;
            };
            double k0 = stepHit(low, 0), s0 = stepHit(mid, 0), s4 = stepHit(mid, 4), s8 = stepHit(mid, 8), s12 = stepHit(mid, 12);
            double sc = 2.0 * k0 - 1.5 * s0 + 0.6 * (s4 + s12) + 0.4 * s8;
            if (sc > bs + 1e-9) { bs = sc; barStart = o; }
        }
        while (barStart - 4 * P >= -step) barStart -= 4 * P; // start at the first bar of the file
        bars = (int)((frames - barStart) / (4 * P));
    }
    // ---- groove grid + swing + timing ----
    if (bars >= 1) {
        const std::vector<float>* env[3] = { &low, &mid, &high };
        std::vector<float> vals[3]; float thr[3];
        for (int band = 0; band < 3; ++band) {
            for (int b = 0; b < bars; ++b) for (int s = 0; s < 16; ++s) { double t = barStart + b * 4 * P + s * step; vals[band].push_back(envMax(*env[band], t - step * 0.35, t + step * 0.45)); }
            thr[band] = std::max(0.12f, 0.4f * percentile(vals[band], 0.95f));
        }
        float snareStrong = 0.7f * percentile(vals[1], 0.95f);
        for (int band = 0; band < 3; ++band)
            for (int s = 0; s < 16; ++s) {
                int hits = 0;
                for (int b = 0; b < bars; ++b) {
                    size_t i = (size_t)(b * 16 + s);
                    bool hit = vals[band][i] > thr[band];
                    // On a kick, the snare detector also sees the kick's click: only count clear snares there.
                    if (band == 1 && hit && vals[0][i] > thr[0] && vals[1][i] < snareStrong) hit = false;
                    if (hit) ++hits;
                }
                f.grid[(size_t)band][(size_t)s] = (float)hits / (float)bars;
            }
        f.gridBars = bars;
        // Offsets of onset peaks relative to the straight grid (fractions of a 16th).
        std::vector<float> even, odd;
        const float peakThr = 0.3f;
        for (int b = 0; b < bars; ++b) for (int s = 0; s < 16; ++s) {
            double t = barStart + b * 4 * P + s * step;
            int lo = (int)std::floor(t - step * 0.3), hi = (int)std::ceil(t + step * 0.5);
            int bi = -1; float bv = peakThr;
            for (int i = std::max(1, lo); i <= std::min(frames - 2, hi); ++i) if (full[(size_t)i] > bv) { bv = full[(size_t)i]; bi = i; }
            if (bi < 0) continue;
            float y0 = full[(size_t)bi - 1], y1 = full[(size_t)bi], y2 = full[(size_t)bi + 1];
            float d = (y0 - 2 * y1 + y2) != 0 ? 0.5f * (y0 - y2) / (y0 - 2 * y1 + y2) : 0.0f;
            float off = (float)((bi + d - t) / step);
            (s % 2 ? odd : even).push_back(off);
        }
        if (even.size() > 4 && odd.size() > 4) {
            float delay = median(odd) - median(even);
            f.swing = clampf(delay / 0.33f, 0.0f, 1.0f);
            if (f.swing < 0.08f) f.swing = 0.0f;
            std::vector<float> dev; float me = median(even); for (float v : even) dev.push_back(std::abs(v - me));
            f.timingTightness = clampf(1.0f - median(dev) / 0.25f, 0.0f, 1.0f);
        }
    }
    { auto pk = peaks(full, 0.3f, (int)(kFps * 0.05)); f.onsetRate = (float)(pk.size() / f.analysedSeconds); }
    if (!report(0.7f)) return f;

    // ---- drum timbre ----
    {
        auto lp = filtered(x, 0, 180, 0.7, 2);
        auto kp = peaks(low, 0.45f, (int)(kFps * 0.12));
        std::vector<float> pitch, drop, dec, br, earlyF;
        std::vector<std::vector<float>> specs;
        std::sort(kp.begin(), kp.end(), [&](int i, int j) { return low[(size_t)i] > low[(size_t)j]; });
        if (kp.size() > 48) kp.resize(48);
        std::sort(kp.begin(), kp.end());
        for (size_t i = 0; i < kp.size(); ++i) {
            int s0 = onsetSample(kp[i]);
            int next = i + 1 < kp.size() ? onsetSample(kp[i + 1]) : (int)x.size();
            int end = std::min({ (int)x.size(), s0 + (int)(0.7 * kSR), next });
            if (end - s0 < (int)(0.08 * kSR)) continue;
            // align to the actual transient
            int pkAt = s0; float pv = 0; for (int k = s0; k < std::min(end, s0 + (int)(0.06 * kSR)); ++k) if (std::abs(lp[(size_t)k]) > pv) { pv = std::abs(lp[(size_t)k]); pkAt = k; }
            auto sp = hitLowSpectrum(x, s0, pkAt + (int)(0.05 * kSR), std::min(end, pkAt + (int)(0.3 * kSR)));
            if (!sp.empty()) specs.push_back(std::move(sp));
            float late = zcFreq(lp, pkAt + (int)(0.06 * kSR), std::min(end, pkAt + (int)(0.2 * kSR)));
            float early = zcFreq(lp, pkAt, std::min(end, pkAt + (int)(0.025 * kSR)));
            if (late >= 28 && late <= 160) pitch.push_back(late);
            if (early > 0) earlyF.push_back(early);
            dec.push_back(decayTime(lp, s0, end, 24.0f));
            br.push_back(segmentCentroid(x, pkAt, (int)(0.06 * kSR)));
        }
        // Kick pitch: per-bin median of the hits' low spectra. The kick is the same on every hit,
        // while bass notes change from chord to chord, so the median keeps the kick and drops the bass.
        float kickHz = 0;
        if (specs.size() >= 3) {
            size_t nb = specs[0].size();
            std::vector<float> medSpec(nb);
            for (size_t k = 0; k < nb; ++k) { std::vector<float> col; for (auto& s2 : specs) if (s2.size() == nb) col.push_back(s2[k]); medSpec[k] = median(col); }
            size_t bk = (size_t)(std::max_element(medSpec.begin(), medSpec.end()) - medSpec.begin());
            float d = 0;
            if (bk > 0 && bk + 1 < nb) { float a = medSpec[bk - 1], b = medSpec[bk], c = medSpec[bk + 1]; if (a - 2 * b + c != 0) d = clampf(0.5f * (a - c) / (a - 2 * b + c), -0.5f, 0.5f); }
            kickHz = (float)((kLowSpecK0 + (double)bk + d) * kLowSpecHz);
        } else if (!pitch.empty()) kickHz = median(pitch);
        f.kick.hits = (int)std::max(specs.size(), pitch.size());
        if (kickHz > 0) {
            f.kick.pitchHz = kickHz;
            f.kick.decaySec = dec.empty() ? 0.3f : clampf(median(dec), 0.08f, 3.5f);
            f.kick.brightHz = br.empty() ? 0.0f : median(br);
            f.kick.pitchDrop = earlyF.empty() ? 1.0f : clampf(median(earlyF) / kickHz, 1.0f, 6.0f);
        }
    }
    {
        auto body = filtered(x, 2, 230, 0.8, 1);
        auto noise = filtered(x, 1, 2000, 0.7, 2);
        auto sp = peaks(mid, 0.45f, (int)(kFps * 0.1));
        auto kp = peaks(low, 0.45f, (int)(kFps * 0.1));
        std::vector<float> pitch, dec, br;
        for (int p : sp) {
            bool onKick = false; for (int k : kp) if (std::abs(k - p) <= 1) onKick = true;
            if (onKick) continue;
            int s0 = onsetSample(p), end = std::min((int)x.size(), s0 + (int)(0.45 * kSR));
            float fq = zcFreq(body, s0 + (int)(0.008 * kSR), s0 + (int)(0.07 * kSR));
            if (fq > 120 && fq < 400) pitch.push_back(fq);
            dec.push_back(decayTime(noise, s0, end, 24.0f));
            br.push_back(segmentCentroid(x, s0, (int)(0.08 * kSR)));
            if (dec.size() >= 48) break;
        }
        f.snare.hits = (int)dec.size();
        if (!dec.empty()) { f.snare.decaySec = clampf(median(dec), 0.05f, 2.0f); f.snare.brightHz = median(br); f.snare.pitchHz = pitch.empty() ? 0.0f : median(pitch); }
    }
    {
        auto hp = filtered(x, 1, 6000, 0.7, 2);
        auto hpk = peaks(high, 0.4f, (int)(kFps * 0.05));
        std::vector<float> dec, br;
        for (int p : hpk) {
            int s0 = onsetSample(p), end = std::min((int)x.size(), s0 + (int)(0.35 * kSR));
            dec.push_back(decayTime(hp, s0, end, 24.0f));
            br.push_back(segmentCentroid(hp, s0, (int)(0.04 * kSR)));
            if (dec.size() >= 64) break;
        }
        f.hat.hits = (int)dec.size();
        if (!dec.empty()) { f.hat.decaySec = clampf(percentile(dec, 0.3f), 0.02f, 1.5f); f.hat.brightHz = median(br); }
    }
    if (!report(0.82f)) return f;

    // ---- harmonic / percussive balance (median-filter HPSS on 0–4 kHz) ----
    {
        int kMax = binOf(4000), step2 = 2;
        int nf = frames / step2;
        double eh = 0, ep = 0;
        std::vector<float> win9(9);
        for (int i = 4; i < nf - 4; ++i) {
            int fr = i * step2;
            for (int k = 4; k < kMax - 4; k += 2) {
                for (int j = -4; j <= 4; ++j) win9[(size_t)(j + 4)] = M((i + j) * step2, k);
                std::nth_element(win9.begin(), win9.begin() + 4, win9.end()); float h = win9[4];
                for (int j = -4; j <= 4; ++j) win9[(size_t)(j + 4)] = M(fr, k + j);
                std::nth_element(win9.begin(), win9.begin() + 4, win9.end()); float pc = win9[4];
                eh += (double)h * h; ep += (double)pc * pc;
            }
        }
        f.harmonicRatio = (eh + ep) > 0 ? (float)(eh / (eh + ep)) : 0.0f;
    }
    // ---- key & chords ----
    {
        // Harmonic part only: median over ~200 ms per bin removes drum hits (incl. tonal snare/tom bodies).
        const int kLo = binOf(60), kHi = binOf(2500);
        std::vector<float> H((size_t)frames * (size_t)(kHi + 1), 0.0f);
        {
            std::vector<float> w9(9);
            for (int fr = 0; fr < frames; ++fr) for (int k = kLo; k <= kHi; ++k) {
                for (int j = -4; j <= 4; ++j) w9[(size_t)(j + 4)] = M(std::clamp(fr + j, 0, frames - 1), k);
                std::nth_element(w9.begin(), w9.begin() + 4, w9.end());
                H[(size_t)fr * (size_t)(kHi + 1) + (size_t)k] = w9[4];
            }
        }
        auto HM = [&](int fr, int k) { return H[(size_t)fr * (size_t)(kHi + 1) + (size_t)k]; };
        std::vector<int> pcOfBin(kBins, -1);
        for (int k = binOf(180); k <= binOf(2500); ++k) { double midi = 69 + 12 * std::log2(k * kBinHz / 440.0); pcOfBin[(size_t)k] = (((int)std::lround(midi)) % 12 + 12) % 12; }
        std::array<float, 12> chroma{};
        std::vector<std::array<float, 12>> barChroma((size_t)std::max(0, bars));
        for (int fr = 0; fr < frames; ++fr) {
            std::array<float, 12> c{};
            for (int k = binOf(180); k <= binOf(2500); ++k) c[(size_t)pcOfBin[(size_t)k]] += HM(fr, k);
            for (int p = 0; p < 12; ++p) chroma[(size_t)p] += c[(size_t)p];
            if (bars > 0) { int b = (int)std::floor((fr - barStart) / (4 * P)); if (b >= 0 && b < bars) for (int p = 0; p < 12; ++p) barChroma[(size_t)b][(size_t)p] += c[(size_t)p]; }
        }
        // Bass chroma (60–260 Hz) from long FFT frames: at bass frequencies a semitone is only ~4 Hz,
        // so the short analysis frames are too coarse. The bass usually spells out the tonic.
        std::array<float, 12> bass{};
        {
            const int NF = 8192;
            std::vector<float> br(NF), bi(NF);
            for (size_t p0 = 0; p0 + NF <= x.size(); p0 += NF / 2) {
                for (int i = 0; i < NF; ++i) { br[(size_t)i] = x[p0 + (size_t)i] * (0.5f - 0.5f * (float)std::cos(kTwoPi * i / (NF - 1))); bi[(size_t)i] = 0; }
                fft(br, bi, false);
                double hzb = kSR / NF;
                for (int k = (int)(60 / hzb); k <= (int)(260 / hzb); ++k) {
                    double midi = 69 + 12 * std::log2(k * hzb / 440.0);
                    double frac = midi - std::floor(midi);
                    int pc = (((int)std::floor(midi)) % 12 + 12) % 12;
                    float m = std::sqrt(br[(size_t)k] * br[(size_t)k] + bi[(size_t)k] * bi[(size_t)k]);
                    bass[(size_t)pc] += m * (float)(1.0 - frac); bass[(size_t)((pc + 1) % 12)] += m * (float)frac;
                }
            }
        }
        float scores[24];
        for (int r = 0; r < 12; ++r) for (int mode = 0; mode < 2; ++mode) {
            float rot[12]; for (int p = 0; p < 12; ++p) rot[p] = chroma[(size_t)((p + r) % 12)];
            // Krumhansl profile correlation + how much of the energy fits the key's diatonic scale.
            static const int majorSet[7] = { 0, 2, 4, 5, 7, 9, 11 }, minorSet[7] = { 0, 2, 3, 5, 7, 8, 10 };
            float in = 0, all = 0; for (int p = 0; p < 12; ++p) all += rot[p];
            for (int q = 0; q < 7; ++q) in += rot[(mode ? minorSet : majorSet)[q]];
            scores[r * 2 + mode] = pearson(rot, mode ? kMinor : kMajor, 12) + 2.0f * (all > 0 ? in / all - 0.75f : 0.0f);
        }
        int bi = (int)(std::max_element(scores, scores + 24) - scores);
        int bestRoot = bi / 2; bool bestMinor = (bi % 2) == 1; float best = scores[bi];
        // Relative major/minor share the same notes; let the bass decide when they are close.
        int relRoot = bestMinor ? (bestRoot + 3) % 12 : (bestRoot + 9) % 12;
        float relScore = scores[relRoot * 2 + (bestMinor ? 0 : 1)];
        float bassSum = 0; for (float v : bass) bassSum += v;
        if (best - relScore < 0.3f && bassSum > 0) {
            auto triad = [&](int r, bool mi) { return bass[(size_t)r] + 0.5f * bass[(size_t)((r + 7) % 12)] + 0.3f * bass[(size_t)((r + (mi ? 3 : 4)) % 12)]; };
            if (triad(relRoot, !bestMinor) > triad(bestRoot, bestMinor) * 1.1f) { bestRoot = relRoot; bestMinor = !bestMinor; std::swap(best, relScore); }
        }
        float second = -2; for (int i = 0; i < 24; ++i) if (i != bestRoot * 2 + (bestMinor ? 1 : 0) && scores[i] > second) second = scores[i];
        f.keyRoot = bestRoot;
        f.keyConfidence = clampf((best - second) * 5.0f + (best - 0.5f), 0.0f, 1.0f);
        auto c = [&](int iv) { return chroma[(size_t)((bestRoot + iv) % 12)]; };
        if (bestMinor) {
            f.scale = Scale::Minor;
            if (c(1) > c(2) * 1.1f) f.scale = Scale::Phrygian;
            else if (c(9) > c(8) * 1.15f) f.scale = Scale::Dorian;
            else if (c(11) > c(10) * 1.2f) f.scale = Scale::HarmonicMinor;
        } else {
            f.scale = c(6) > c(5) * 1.15f ? Scale::Lydian : Scale::Major;
        }
        // One chord per bar -> scale degrees -> most common 4-bar cycle.
        const auto& iv = scaleIntervals(f.scale);
        std::vector<int> degs;
        for (auto& bc : barChroma) {
            float bs = -1; int root = -1;
            for (int r = 0; r < 12; ++r) for (int q = 0; q < 2; ++q) {
                float s = bc[(size_t)r] + bc[(size_t)((r + (q ? 3 : 4)) % 12)] * 0.8f + bc[(size_t)((r + 7) % 12)] * 0.9f;
                if (s > bs) { bs = s; root = r; }
            }
            int rel = ((root - f.keyRoot) % 12 + 12) % 12;
            auto it = std::find(iv.begin(), iv.end(), rel);
            if (it != iv.end()) degs.push_back((int)(it - iv.begin()));
        }
        if (degs.size() >= 4) {
            std::vector<std::pair<std::vector<int>, int>> counts;
            for (size_t i = 0; i + 4 <= degs.size(); ++i) {
                std::vector<int> w(degs.begin() + (long)i, degs.begin() + (long)i + 4);
                bool found = false; for (auto& cc : counts) if (cc.first == w) { ++cc.second; found = true; }
                if (!found) counts.push_back({ w, 1 });
            }
            auto bestW = std::max_element(counts.begin(), counts.end(), [](auto& p, auto& q) { return p.second < q.second; });
            f.progression = bestW->first;
            // rotate so the tonic (if present) comes first
            auto t = std::find(f.progression.begin(), f.progression.end(), 0);
            if (t != f.progression.end()) std::rotate(f.progression.begin(), t, f.progression.end());
        } else if (!degs.empty()) {
            f.progression = degs; while (f.progression.size() < 4) f.progression.push_back(degs[f.progression.size() % degs.size()]);
        }
    }
    report(1.0f);
    f.valid = true;
    return f;
}

// ---------------------------------------------------------------------------
ReferenceFeatures combineReferences(const std::vector<ReferenceFeatures>& in) {
    std::vector<const ReferenceFeatures*> refs;
    for (auto& r : in) if (r.valid) refs.push_back(&r);
    if (refs.empty()) return {};
    if (refs.size() == 1) return *refs[0];
    ReferenceFeatures o = *refs[0];
    o.name = std::to_string(refs.size()) + " references";
    auto med = [&](auto getter) { std::vector<float> v; for (auto* r : refs) v.push_back((float)getter(*r)); return median(v); };
    auto medNZ = [&](auto getter) { std::vector<float> v; for (auto* r : refs) { float x = (float)getter(*r); if (x > 0) v.push_back(x); } return v.empty() ? 0.0f : median(v); };
    o.seconds = 0; o.analysedSeconds = 0; for (auto* r : refs) { o.seconds += r->seconds; o.analysedSeconds += r->analysedSeconds; }
    o.bpm = med([](auto& r) { return r.bpm; }); o.tempoConfidence = med([](auto& r) { return r.tempoConfidence; });
    o.swing = med([](auto& r) { return r.swing; }); o.onsetRate = med([](auto& r) { return r.onsetRate; }); o.timingTightness = med([](auto& r) { return r.timingTightness; });
    o.centroidHz = med([](auto& r) { return r.centroidHz; }); o.rolloffHz = med([](auto& r) { return r.rolloffHz; }); o.bandwidthHz = med([](auto& r) { return r.bandwidthHz; });
    o.tiltDbPerOct = med([](auto& r) { return r.tiltDbPerOct; }); o.subRatio = med([](auto& r) { return r.subRatio; }); o.highRatio = med([](auto& r) { return r.highRatio; });
    o.flatness = med([](auto& r) { return r.flatness; }); o.rmsDb = med([](auto& r) { return r.rmsDb; }); o.crestDb = med([](auto& r) { return r.crestDb; });
    o.clipRatio = med([](auto& r) { return r.clipRatio; }); o.dynamicGapDb = med([](auto& r) { return r.dynamicGapDb; }); o.noiseFloorDb = med([](auto& r) { return r.noiseFloorDb; });
    o.width = med([](auto& r) { return r.width; }); o.harmonicRatio = med([](auto& r) { return r.harmonicRatio; });
    o.kick.pitchHz = medNZ([](auto& r) { return r.kick.pitchHz; }); o.kick.decaySec = medNZ([](auto& r) { return r.kick.decaySec; });
    o.kick.pitchDrop = medNZ([](auto& r) { return r.kick.pitchDrop; }); o.kick.brightHz = medNZ([](auto& r) { return r.kick.brightHz; });
    o.snare.pitchHz = medNZ([](auto& r) { return r.snare.pitchHz; }); o.snare.decaySec = medNZ([](auto& r) { return r.snare.decaySec; }); o.snare.brightHz = medNZ([](auto& r) { return r.snare.brightHz; });
    o.hat.decaySec = medNZ([](auto& r) { return r.hat.decaySec; }); o.hat.brightHz = medNZ([](auto& r) { return r.hat.brightHz; });
    o.kick.hits = o.snare.hits = o.hat.hits = 0;
    for (auto* r : refs) { o.kick.hits += r->kick.hits; o.snare.hits += r->snare.hits; o.hat.hits += r->hat.hits; }
    // Key: most confident reference wins; groove grids are averaged.
    auto* keyRef = *std::max_element(refs.begin(), refs.end(), [](auto* p, auto* q) { return p->keyConfidence < q->keyConfidence; });
    o.keyRoot = keyRef->keyRoot; o.scale = keyRef->scale; o.keyConfidence = keyRef->keyConfidence; o.progression = keyRef->progression;
    o.grid = {}; o.gridBars = 0;
    for (auto* r : refs) { for (int b = 0; b < 3; ++b) for (int s = 0; s < 16; ++s) o.grid[(size_t)b][(size_t)s] += r->grid[(size_t)b][(size_t)s] / (float)refs.size(); o.gridBars += r->gridBars; }
    o.valid = true;
    return o;
}

// ---------------------------------------------------------------------------
SonicProfile profileFromReference(const ReferenceFeatures& f, const SonicProfile* base, float weight) {
    SonicProfile p = base ? *base : neutralProfile();
    if (!f.valid) return p;
    weight = clampf(weight, 0, 1);
    float est[kNumDesc], conf[kNumDesc];
    for (int i = 0; i < kNumDesc; ++i) { est[i] = p.d[(size_t)i]; conf[i] = 0; }
    auto set = [&](Desc d, float v, float c) { est[(int)d] = clampf(v, 0, 1); conf[(int)d] = c; };
    float bright = mapRange(std::log2(std::max(1.0f, f.centroidHz) / 700.0f), 0.0f, std::log2(4200.0f / 700.0f));
    bool minorish = f.scale != Scale::Major && f.scale != Scale::Lydian;
    set(Desc::Dark, 1.0f - bright + (minorish ? 0.08f : -0.08f), 0.85f);
    set(Desc::Heavy, mapRange(f.subRatio, 0.04f, 0.35f), 0.8f);
    float lofi = 1.0f - mapRange(f.bandwidthHz, 8500.0f, 17500.0f);
    if (f.noiseFloorDb > -58.0f) lofi += 0.25f;
    set(Desc::Lofi, lofi, 0.8f);
    float squash = 1.0f - mapRange(f.crestDb, 8.0f, 19.0f);
    set(Desc::Distorted, 0.6f * squash + (f.clipRatio > 0.0005f ? 0.25f : 0.0f) + mapRange(f.flatness, 0.05f, 0.35f) * 0.3f, 0.6f);
    float loud = mapRange(f.rmsDb, -22.0f, -7.0f);
    set(Desc::Aggressive, 0.45f * loud + 0.35f * squash + 0.2f * mapRange(f.onsetRate, 2.0f, 8.0f), 0.55f);
    set(Desc::Atmospheric, 0.6f * (1.0f - mapRange(f.dynamicGapDb, 8.0f, 30.0f)) + 0.4f * mapRange(f.width, 0.08f, 0.6f), 0.6f);
    set(Desc::Energy, 0.5f * mapRange(f.bpm, 70.0f, 170.0f) + 0.5f * mapRange(f.onsetRate, 1.5f, 8.0f), 0.7f);
    set(Desc::Melodic, 0.7f * mapRange(f.harmonicRatio, 0.35f, 0.85f) + 0.3f * f.keyConfidence, 0.6f);
    set(Desc::Electronic, 0.3f + 0.65f * f.timingTightness, 0.4f);
    set(Desc::Industrial, mapRange(f.flatness, 0.06f, 0.4f) * 0.8f + mapRange(f.highRatio, 0.05f, 0.25f) * 0.2f, 0.4f);
    float bwMid = 1.0f - std::abs(f.bandwidthHz - 14000.0f) / 5000.0f;
    set(Desc::Vintage, clampf(bwMid, 0, 1) * 0.6f + (1.0f - mapRange(f.width, 0.05f, 0.5f)) * 0.4f, 0.3f);
    set(Desc::Hyper, bright * 0.5f + loud * 0.3f + mapRange(f.bpm, 120.0f, 170.0f) * 0.2f, 0.25f);
    for (int i = 0; i < kNumDesc; ++i) p.d[(size_t)i] = p.d[(size_t)i] + (est[i] - p.d[(size_t)i]) * weight * conf[i];
    if (weight > 0) {
        if (f.tempoConfidence > 0.15f && f.bpm > 0) p.bpm = f.bpm;
        p.swing = f.swing;
        if (f.keyConfidence > 0.1f) { p.keyRoot = f.keyRoot; p.scale = f.scale; }
        // Groove style guess from the measured grid.
        auto& k = f.grid[0]; auto& s = f.grid[1];
        std::vector<Groove> g;
        if (k[0] > 0.6f && k[4] > 0.6f && k[8] > 0.6f && k[12] > 0.6f) g.push_back(f.bpm >= 145 ? Groove::Hardstyle : Groove::FourFloor);
        else if (s[8] > 0.5f && s[8] > 1.8f * std::max(s[4], s[12])) {
            // Half-time backbeat: trap/drill when hats run in 8ths or faster at a fast tempo.
            float hatsPerBar = 0; for (float h : f.grid[2]) hatsPerBar += h;
            g.push_back(f.bpm > 125 && hatsPerBar >= 6 ? Groove::Trap : Groove::HalfTime);
        }
        else if (f.bpm >= 160) g.push_back(Groove::DnB);
        else if (f.swing > 0.35f) g.push_back(f.bpm >= 125 ? Groove::Garage : Groove::BoomBap);
        else g.push_back(Groove::Breakbeat);
        for (auto x : p.grooves) if (std::find(g.begin(), g.end(), x) == g.end()) g.push_back(x);
        p.grooves = g;
    }
    std::string facts;
    char buf[200];
    std::snprintf(buf, sizeof buf, "Measured from %s: %.0f BPM, %s %s", f.name.c_str(), f.bpm, noteName(std::max(0, f.keyRoot)), scaleName(f.scale));
    facts = buf;
    if (f.kick.pitchHz > 0) { std::snprintf(buf, sizeof buf, ", kick ~%.0f Hz", f.kick.pitchHz); facts += buf; }
    facts += ".";
    p.summary = base && !base->summary.empty() ? facts + " " + base->summary : facts;
    p.signatures.insert(p.signatures.begin(), "Drums, groove, key & tone measured from your reference audio");
    if (p.displayName.empty()) p.displayName = f.name;
    p.source = base && !base->source.empty() && base->source != "neutral" ? "audio+" + base->source : "audio";
    p.confidence = std::max(p.confidence, 0.4f + 0.5f * std::min(f.tempoConfidence, 1.0f));
    return p;
}

void applyReferenceToDNA(KitDNA& d, const ReferenceFeatures& f, float weight) {
    if (!f.valid || weight <= 0.0f) return;
    auto& t = d.ref;
    t.valid = true;
    t.weight = clampf(weight, 0, 1);
    if (f.kick.pitchHz > 0) { t.kickHz = f.kick.pitchHz; t.kickDecay = f.kick.decaySec; t.kickDrop = f.kick.pitchDrop; }
    if (f.snare.decaySec > 0) { t.snareHz = f.snare.pitchHz; t.snareDecay = f.snare.decaySec; t.snareBright = f.snare.brightHz; }
    if (f.hat.decaySec > 0) { t.hatDecay = f.hat.decaySec; t.hatBright = f.hat.brightHz; }
    if (f.bandwidthHz > 0 && f.bandwidthHz < 15500.0f) t.bandwidthHz = f.bandwidthHz;
    if (f.gridBars >= 2) { t.hasGrid = true; t.grid = f.grid; }
    if (f.tempoConfidence > 0.15f && f.bpm > 0) d.bpm = std::round(f.bpm);
    d.swing = f.swing;
    if (f.keyConfidence > 0.1f && f.keyRoot >= 0) {
        d.keyRoot = f.keyRoot; d.scale = f.scale;
        d.kickNote = 24 + d.keyRoot; if (d.kickNote < 28) d.kickNote += 12;
        if (f.progression.size() >= 2) d.progression = f.progression;
    }
    if (f.noiseFloorDb > -70.0f) d.noiseFloorDb = std::max(d.noiseFloorDb, std::min(-48.0f, f.noiseFloorDb));
}

std::vector<std::string> describeReference(const ReferenceFeatures& f) {
    std::vector<std::string> v;
    char b[240];
    if (!f.valid) { v.push_back("Could not analyse (too short or silent)."); return v; }
    std::snprintf(b, sizeof b, "%.1f BPM%s · %s %s%s", f.bpm, f.tempoConfidence < 0.25f ? " (unsure)" : "", noteName(f.keyRoot), scaleName(f.scale), f.keyConfidence < 0.2f ? " (unsure)" : ""); v.push_back(b);
    if (f.swing > 0) { std::snprintf(b, sizeof b, "Swing %.0f%%", f.swing * 100); v.push_back(b); }
    if (f.progression.size() >= 2) { std::string s = "Chords (degrees): "; for (size_t i = 0; i < f.progression.size(); ++i) s += (i ? "-" : "") + std::to_string(f.progression[i] + 1); v.push_back(s); }
    if (f.gridBars >= 1) {
        static const char* names[3] = { "Kick  ", "Snare ", "Hats  " };
        for (int band = 0; band < 3; ++band) {
            std::string row = names[band];
            for (int s = 0; s < 16; ++s) { float p = f.grid[(size_t)band][(size_t)s]; row += p >= 0.6f ? "x" : p >= 0.3f ? "o" : "."; if (s % 4 == 3 && s < 15) row += " "; }
            v.push_back(row);
        }
    }
    if (f.kick.pitchHz > 0) { std::snprintf(b, sizeof b, "Kick ~%.0f Hz, decay %.2f s%s", f.kick.pitchHz, f.kick.decaySec, f.kick.pitchDrop > 1.6f ? ", pitch sweep" : ""); v.push_back(b); }
    if (f.snare.decaySec > 0) { std::snprintf(b, sizeof b, "Snare decay %.2f s, brightness %.0f Hz", f.snare.decaySec, f.snare.brightHz); v.push_back(b); }
    if (f.hat.decaySec > 0) { std::snprintf(b, sizeof b, "Hats decay %.2f s", f.hat.decaySec); v.push_back(b); }
    std::snprintf(b, sizeof b, "Tone %.0f Hz centroid, top end to %.1f kHz, sub %.0f%%", f.centroidHz, f.bandwidthHz / 1000.0f, f.subRatio * 100); v.push_back(b);
    std::snprintf(b, sizeof b, "Loudness %.1f dBFS RMS, crest %.1f dB, width %.0f%%", f.rmsDb, f.crestDb, f.width * 100); v.push_back(b);
    return v;
}

// ---------------------------------------------------------------------------
static Json drumToJson(const DrumStats& d) {
    Json j = Json::object(); j["hits"] = d.hits; j["pitch_hz"] = d.pitchHz; j["pitch_drop"] = d.pitchDrop; j["decay_s"] = d.decaySec; j["bright_hz"] = d.brightHz; return j;
}
static DrumStats drumFromJson(const Json& j) {
    DrumStats d; d.hits = (int)j.get("hits").num(0); d.pitchHz = (float)j.get("pitch_hz").num(0); d.pitchDrop = (float)j.get("pitch_drop").num(1);
    d.decaySec = (float)j.get("decay_s").num(0); d.brightHz = (float)j.get("bright_hz").num(0); return d;
}

Json featuresToJson(const ReferenceFeatures& f) {
    Json j = Json::object();
    j["name"] = f.name; j["valid"] = f.valid; j["seconds"] = f.seconds; j["analysed_seconds"] = f.analysedSeconds;
    j["bpm"] = f.bpm; j["tempo_confidence"] = f.tempoConfidence; j["swing"] = f.swing; j["onset_rate"] = f.onsetRate; j["timing_tightness"] = f.timingTightness;
    Json g = Json::array(); for (auto& row : f.grid) { Json r = Json::array(); for (float v : row) r.push(std::round(v * 1000) / 1000.0); g.push(r); }
    j["grid"] = g; j["grid_bars"] = f.gridBars;
    j["key"] = f.keyRoot; j["scale"] = scaleName(f.scale); j["key_confidence"] = f.keyConfidence;
    Json pr = Json::array(); for (int x : f.progression) pr.push(x); j["progression"] = pr;
    j["harmonic_ratio"] = f.harmonicRatio; j["centroid_hz"] = f.centroidHz; j["rolloff_hz"] = f.rolloffHz; j["bandwidth_hz"] = f.bandwidthHz;
    j["tilt_db_oct"] = f.tiltDbPerOct; j["sub_ratio"] = f.subRatio; j["high_ratio"] = f.highRatio; j["flatness"] = f.flatness;
    j["rms_db"] = f.rmsDb; j["crest_db"] = f.crestDb; j["clip_ratio"] = f.clipRatio; j["dynamic_gap_db"] = f.dynamicGapDb; j["noise_floor_db"] = f.noiseFloorDb; j["width"] = f.width;
    j["kick"] = drumToJson(f.kick); j["snare"] = drumToJson(f.snare); j["hat"] = drumToJson(f.hat);
    return j;
}

ReferenceFeatures featuresFromJson(const Json& j) {
    ReferenceFeatures f;
    f.name = j.get("name").str(); f.valid = j.get("valid").boolean(); f.seconds = j.get("seconds").num(0); f.analysedSeconds = j.get("analysed_seconds").num(0);
    f.bpm = (float)j.get("bpm").num(0); f.tempoConfidence = (float)j.get("tempo_confidence").num(0); f.swing = (float)j.get("swing").num(0);
    f.onsetRate = (float)j.get("onset_rate").num(0); f.timingTightness = (float)j.get("timing_tightness").num(0);
    for (int b = 0; b < 3 && b < (int)j.get("grid").size(); ++b) for (int s = 0; s < 16; ++s) f.grid[(size_t)b][(size_t)s] = (float)j.get("grid")[(size_t)b][(size_t)s].num(0);
    f.gridBars = (int)j.get("grid_bars").num(0);
    f.keyRoot = (int)j.get("key").num(-1); scaleFromName(j.get("scale").str(), f.scale); f.keyConfidence = (float)j.get("key_confidence").num(0);
    for (auto& x : j.get("progression").items()) f.progression.push_back((int)x.num(0));
    f.harmonicRatio = (float)j.get("harmonic_ratio").num(0); f.centroidHz = (float)j.get("centroid_hz").num(0); f.rolloffHz = (float)j.get("rolloff_hz").num(0);
    f.bandwidthHz = (float)j.get("bandwidth_hz").num(0); f.tiltDbPerOct = (float)j.get("tilt_db_oct").num(0); f.subRatio = (float)j.get("sub_ratio").num(0);
    f.highRatio = (float)j.get("high_ratio").num(0); f.flatness = (float)j.get("flatness").num(0); f.rmsDb = (float)j.get("rms_db").num(-120);
    f.crestDb = (float)j.get("crest_db").num(0); f.clipRatio = (float)j.get("clip_ratio").num(0); f.dynamicGapDb = (float)j.get("dynamic_gap_db").num(0);
    f.noiseFloorDb = (float)j.get("noise_floor_db").num(-120); f.width = (float)j.get("width").num(0);
    f.kick = drumFromJson(j.get("kick")); f.snare = drumFromJson(j.get("snare")); f.hat = drumFromJson(j.get("hat"));
    return f;
}

} // namespace sdna
