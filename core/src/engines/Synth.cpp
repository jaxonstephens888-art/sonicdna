#include "Synth.h"

namespace sdna::synth {

float pv(const GenContext& ctx, Rng& r, Desc d, float jitter) {
    return clampf(ctx.profile[d] + (float)r.gauss() * jitter * (0.3f + ctx.variation), 0.0f, 1.0f);
}

size_t pickRecipe(const GenContext& ctx, Rng& r, std::vector<double> w) {
    double ex = 2.0 - 1.5 * ctx.variation;
    for (auto& x : w) x = std::pow(std::max(0.0, x), ex);
    if (ctx.variant == 0) { // the most characteristic recipe first
        return (size_t)(std::max_element(w.begin(), w.end()) - w.begin());
    }
    return r.weighted(w);
}

bool hasGroove(const SonicProfile& p, Groove g) {
    for (size_t i = 0; i < p.grooves.size() && i < 3; ++i) if (p.grooves[i] == g) return true;
    return false;
}

Buffer pitchedDrum(double sr, const PitchedDrum& p, uint64_t seed) {
    int len = (int)(p.length * sr);
    Buffer b(1, len, sr);
    Noise n(seed);
    OnePole clickLP; clickLP.setLP(sr, p.clickLP);
    double ph = 0;
    std::vector<double> mph(p.modes.size(), 0.0);
    float* o = b.data(0);
    double att = std::max(1e-5, p.attackMs * 0.001);
    for (int i = 0; i < len; ++i) {
        double t = i / sr;
        double f = p.f0 * (1.0 + (p.startMul - 1.0) * std::exp(-t / std::max(1e-4, p.pitchDecay)));
        ph += f / sr; ph -= std::floor(ph);
        double s = std::sin(kTwoPi * ph);
        double tri = ph < 0.5 ? 4 * ph - 1 : 3 - 4 * ph;
        double body = s * (1 - p.triMix) + tri * p.triMix;
        if (p.harmonicMix > 0) body += p.harmonicMix * (0.5 * std::sin(2 * kTwoPi * ph) + 0.25 * std::sin(3 * kTwoPi * ph));
        double env = expDecay(t, p.ampT60) * std::min(1.0, t / att);
        double v = body * env;
        for (size_t m = 0; m < p.modes.size(); ++m) {
            mph[m] += f * p.modes[m].first / sr; mph[m] -= std::floor(mph[m]);
            v += std::sin(kTwoPi * mph[m]) * p.modes[m].second * expDecay(t, p.ampT60 * 0.45 / p.modes[m].first);
        }
        double click = clickLP.lp(n.white()) * p.clickAmt * expDecay(t, 0.012);
        o[i] = (float)(v + click);
    }
    return b;
}

Buffer noiseBurst(double sr, double length, double attackSec, double t60, int colour, double hpHz, double lpHz, uint64_t seed, double bpHz, double bpQ) {
    int len = (int)(length * sr);
    Buffer b(1, len, sr);
    Noise n(seed);
    SVF hp, lp, bp; hp.set(sr, hpHz, 0.707); lp.set(sr, lpHz, 0.707); if (bpHz > 0) bp.set(sr, bpHz, bpQ);
    float* o = b.data(0);
    for (int i = 0; i < len; ++i) {
        double t = i / sr;
        float v = colour == 0 ? n.white() : colour == 1 ? n.pink() : n.brown();
        if (hpHz > 20) v = hp.hp(v);
        if (lpHz < sr * 0.45) v = lp.lp(v);
        if (bpHz > 0) v = bp.bp(v);
        double env = expDecay(t, t60) * (attackSec > 0 ? std::min(1.0, t / attackSec) : 1.0);
        o[i] = (float)(v * env);
    }
    return b;
}

Buffer metalCluster(double sr, double length, double tune, double t60, uint64_t seed, int voices, bool fm) {
    static const double f808[6] = { 205.3, 304.4, 369.6, 522.7, 540.0, 800.0 };
    int len = (int)(length * sr);
    Buffer b(1, len, sr);
    Rng r(seed);
    std::vector<Osc> osc; std::vector<double> fr;
    for (int i = 0; i < voices; ++i) {
        osc.emplace_back(r.uni());
        fr.push_back(f808[i % 6] * tune * (i >= 6 ? 1.37 + 0.11 * i : 1.0) * (1.0 + r.bipolar() * 0.004));
    }
    float* o = b.data(0);
    double mph = 0;
    for (int i = 0; i < len; ++i) {
        double t = i / sr;
        float s = 0;
        if (!fm) { for (int k = 0; k < voices; ++k) s += osc[(size_t)k].square(fr[(size_t)k] / sr); s /= (float)voices; }
        else {
            mph += fr[3] * 2.83 / sr;
            double mod = std::sin(kTwoPi * mph) * 4.0;
            for (int k = 0; k < 3; ++k) { osc[(size_t)k].phase += fr[(size_t)k] * 1.5 / sr + 0.0; s += (float)std::sin(kTwoPi * osc[(size_t)k].phase + mod); }
            s /= 3.0f;
        }
        o[i] = s * expDecay(t, t60);
    }
    return b;
}

Buffer clapBursts(double sr, int bursts, double spacingMs, double tailT60, double bpHz, double q, uint64_t seed, double length) {
    int len = (int)(length * sr);
    Buffer b(1, len, sr);
    Noise n(seed); Rng r(seed ^ 3);
    SVF bp, hp; bp.set(sr, bpHz, q); hp.set(sr, 500, 0.707);
    float* o = b.data(0);
    std::vector<double> starts;
    double acc = 0;
    for (int k = 0; k < bursts; ++k) { starts.push_back(acc); acc += spacingMs * 0.001 * r.range(0.75, 1.25); }
    for (int i = 0; i < len; ++i) {
        double t = i / sr;
        double env = 0;
        for (int k = 0; k < bursts; ++k) {
            double dt = t - starts[(size_t)k];
            if (dt < 0) continue;
            bool last = k == bursts - 1;
            env += (last ? expDecay(dt, tailT60) : expDecay(dt, 0.02)) * std::min(1.0, dt / 0.0005) * (last ? 1.0 : 0.85);
        }
        o[i] = hp.hp(bp.bp(n.white())) * (float)env;
    }
    return b;
}

Buffer fm2(double sr, double length, double hz, double ratio, double index, double indexT60, double ampT60, double attack, double feedback) {
    int len = (int)(length * sr);
    Buffer b(1, len, sr);
    float* o = b.data(0);
    double pc = 0, pm = 0, prev = 0;
    for (int i = 0; i < len; ++i) {
        double t = i / sr;
        pm += hz * ratio / sr; pm -= std::floor(pm);
        double I = index * expDecay(t, indexT60);
        double mod = std::sin(kTwoPi * pm + feedback * prev);
        prev = mod;
        pc += hz / sr; pc -= std::floor(pc);
        double env = expDecay(t, ampT60) * (attack > 0 ? std::min(1.0, t / attack) : 1.0);
        o[i] = (float)(std::sin(kTwoPi * pc + I * mod) * env);
    }
    return b;
}

Buffer supersaw(double sr, double length, const std::vector<double>& freqs, double detuneCents, int voices, const ADSR& env, double gate, uint64_t seed, bool stereo) {
    int len = (int)(length * sr);
    Buffer b(stereo ? 2 : 1, len, sr);
    Rng r(seed);
    struct V { Osc o; double inc; float gl, gr; };
    std::vector<V> vs;
    for (double f : freqs) {
        for (int k = 0; k < voices; ++k) {
            double spread = voices > 1 ? (2.0 * k / (voices - 1) - 1.0) : 0.0;
            double cents = spread * detuneCents + r.bipolar() * detuneCents * 0.1;
            double pan = stereo ? spread * 0.9 : 0.0;
            vs.push_back({ Osc(r.uni()), f * std::pow(2.0, cents / 1200.0) / sr,
                (float)std::cos((pan + 1) * kPi / 4), (float)std::sin((pan + 1) * kPi / 4) });
        }
    }
    float norm = 1.0f / std::sqrt((float)std::max<size_t>(1, vs.size()));
    for (int i = 0; i < len; ++i) {
        float e = env.at(i / sr, gate) * norm;
        float l = 0, rr = 0;
        for (auto& v : vs) { float s = v.o.saw(v.inc); l += s * v.gl; rr += s * v.gr; }
        b.ch[0][(size_t)i] = l * e;
        if (stereo) b.ch[1][(size_t)i] = rr * e;
    }
    return b;
}

Buffer subtractive(double sr, double length, double hz, int wave, double cutoffStart, double cutoffEnd, double filterT, double q, const ADSR& env, double gate, double glideFrom, double glideT) {
    int len = (int)(length * sr);
    Buffer b(1, len, sr);
    Osc o1, o2(0.37);
    SVF f;
    float* out = b.data(0);
    for (int i = 0; i < len; ++i) {
        double t = i / sr;
        double fr = glideFrom > 0 ? hz * std::pow(glideFrom / hz, std::exp(-t / std::max(1e-3, glideT))) : hz;
        double inc = fr / sr;
        float s = wave == 0 ? o1.saw(inc) : wave == 1 ? o1.square(inc) : wave == 2 ? o1.tri(inc) : 0.5f * (o1.saw(inc) + o2.saw(inc * 1.006));
        if ((i & 7) == 0) f.set(sr, cutoffEnd + (cutoffStart - cutoffEnd) * std::exp(-t / std::max(1e-3, filterT)), q);
        out[i] = f.lp(s) * env.at(t, gate);
    }
    return b;
}

Buffer karplus(double sr, double length, double hz, double decay, double brightness, uint64_t seed) {
    int len = (int)(length * sr);
    Buffer b(1, len, sr);
    Noise n(seed);
    double period = sr / hz;
    int N = (int)period;
    size_t M = (size_t)period + 4;
    std::vector<float> line(M, 0.0f);
    OnePole init; init.setLP(sr, 600 + brightness * 9000);
    double g = std::pow(10.0, -3.0 / (std::max(0.05, decay) * hz));
    float bright = (float)clampd(0.25 + brightness * 0.7, 0.05, 0.98);
    float* o = b.data(0);
    size_t w = 0; float lp = 0;
    for (int i = 0; i < len; ++i) {
        float x = i < N ? init.lp(n.white()) : 0.0f;
        double rp = (double)w - period; while (rp < 0) rp += (double)M;
        size_t i0 = (size_t)rp % M, i1 = (i0 + 1) % M; float fr = (float)(rp - std::floor(rp));
        float d = line[i0] + (line[i1] - line[i0]) * fr;
        lp += bright * (d - lp);
        float y = x + (float)g * lp;
        line[w] = y; w = (w + 1) % M;
        o[i] = y;
    }
    return b;
}

Buffer additive(double sr, double length, double hz, const std::vector<double>& ratios, const std::vector<double>& amps, const std::vector<double>& t60s, double attack, uint64_t seed) {
    int len = (int)(length * sr);
    Buffer b(1, len, sr);
    Rng r(seed);
    std::vector<double> ph(ratios.size()); for (auto& p : ph) p = r.uni();
    float* o = b.data(0);
    for (int i = 0; i < len; ++i) {
        double t = i / sr, s = 0;
        double a = attack > 0 ? std::min(1.0, t / attack) : 1.0;
        for (size_t k = 0; k < ratios.size(); ++k) {
            double f = hz * ratios[k];
            if (f > sr * 0.45) continue;
            ph[k] += f / sr; ph[k] -= std::floor(ph[k]);
            s += std::sin(kTwoPi * ph[k]) * amps[k] * expDecay(t, t60s[k]);
        }
        o[i] = (float)(s * a);
    }
    return b;
}

Buffer piano(double sr, double length, const std::vector<int>& notes, double brightness, double inharm, double hammer, uint64_t seed) {
    int len = (int)(length * sr);
    Buffer out(1, len, sr);
    Rng r(seed);
    for (int note : notes) {
        double f = midiToHz(note);
        double B = inharm * 0.0004 * std::pow(f / 261.0, 0.6);
        double baseT60 = clampd(9.0 * std::pow(110.0 / f, 0.55), 1.0, 12.0);
        std::vector<double> ra, am, tt;
        int partials = std::min(24, (int)(sr * 0.45 / f));
        for (int n = 1; n <= partials; ++n) {
            double fn = n * std::sqrt(1 + B * n * n);
            for (int s = 0; s < 2; ++s) {
                ra.push_back(fn * (1.0 + (s ? 1 : -1) * 0.00035));
                am.push_back(std::pow((double)n, -(1.25 - brightness * 0.65)) * (n == 1 ? 1.0 : 0.9) * 0.5);
                tt.push_back(baseT60 / (1.0 + 0.35 * (n - 1)) * (s ? 0.9 : 1.0));
            }
        }
        Buffer nb = additive(sr, length, f, ra, am, tt, 0.002, r.next());
        Buffer h = noiseBurst(sr, 0.06, 0.0005, 0.03, 1, 60, 1200 + brightness * 2500, r.next());
        fx::mixInto(nb, h, 0, (float)hammer * 0.6f);
        fx::mixInto(out, nb, 0, 1.0f / std::sqrt((float)notes.size()));
    }
    return out;
}

Buffer electricPiano(double sr, double length, const std::vector<int>& notes, double bark, uint64_t seed) {
    int len = (int)(length * sr);
    Buffer out(1, len, sr);
    Rng r(seed);
    for (int note : notes) {
        double f = midiToHz(note);
        Buffer body = fm2(sr, length, f, 1.0, 1.2 + bark * 1.5, 0.9, clampd(4.0 * std::pow(220.0 / f, 0.5), 1.2, 6), 0.002);
        Buffer tine = fm2(sr, length, f * 1.0, 14.0, 0.9, 0.08, 0.35, 0.0005);
        fx::mixInto(body, tine, 0, 0.25f);
        if (bark > 0.05) for (auto& s : body.ch[0]) s = satTube(s * (1.0f + (float)bark * 3.0f)) / (1.0f + (float)bark);
        fx::mixInto(out, body, 0, 0.7f / std::sqrt((float)notes.size()));
        (void)r;
    }
    return out;
}

Buffer organ(double sr, double length, const std::vector<int>& notes, const std::vector<double>& drawbars, double leslie) {
    static const double ratios[9] = { 0.5, 1.5, 1, 2, 3, 4, 5, 6, 8 };
    int len = (int)(length * sr);
    Buffer b(2, len, sr);
    std::vector<double> ph(notes.size() * 9, 0.0);
    for (int i = 0; i < len; ++i) {
        double t = i / sr;
        double lfo = std::sin(kTwoPi * 6.2 * t);
        double s = 0;
        for (size_t n = 0; n < notes.size(); ++n) {
            double f = midiToHz(notes[n]) * (1.0 + leslie * 0.0015 * lfo);
            for (int k = 0; k < 9; ++k) {
                double& p = ph[n * 9 + (size_t)k];
                p += f * ratios[k] / sr; p -= std::floor(p);
                s += std::sin(kTwoPi * p) * drawbars[(size_t)k];
            }
        }
        s /= (double)notes.size() * 3.0;
        double env = std::min(1.0, t / 0.01) * (t > length - 0.08 ? std::max(0.0, (length - t) / 0.08) : 1.0);
        double am = 1.0 - leslie * 0.25 * (0.5 + 0.5 * lfo);
        b.ch[0][(size_t)i] = (float)(s * env * am);
        b.ch[1][(size_t)i] = (float)(s * env * (1.0 - leslie * 0.25 * (0.5 - 0.5 * lfo)));
    }
    return b;
}

Buffer voice(double sr, double length, const VoiceParams& vp, uint64_t seed) {
    int len = (int)(length * sr);
    Buffer b(1, len, sr);
    Noise n(seed); Rng r(seed ^ 99);
    Osc src;
    OnePole glottalLP; glottalLP.setLP(sr, 3500);
    OnePole breathHP; breathHP.setLP(sr, 1500);
    SVF f[4];
    const auto& vt = vowelTable();
    double jit = 0, vibPh = r.uni();
    float gains[4] = { 1, 1, 1, 1 };
    float* o = b.data(0);
    for (int i = 0; i < len; ++i) {
        double t = i / sr;
        if ((i & 31) == 0) {
            double pos = vp.vowelPos ? vp.vowelPos(t) : 0.0;
            pos = clampd(pos, 0.0, (double)vp.vowels.size() - 1.0);
            int a = (int)std::floor(pos); int c = std::min((int)vp.vowels.size() - 1, a + 1);
            const Vowel& va = vt[(size_t)vp.vowels[(size_t)a] % vt.size()];
            const Vowel& vc = vt[(size_t)vp.vowels[(size_t)c] % vt.size()];
            Vowel v = lerpVowel(va, vc, (float)(pos - a), (float)vp.formantShift);
            for (int k = 0; k < 4; ++k) { f[k].set(sr, v.f[k], v.f[k] / std::max(30.0f, v.bw[k])); gains[k] = dbToGain(v.gainDb[k]); }
            jit = jit * 0.9 + r.gauss() * vp.jitter;
        }
        vibPh += vp.vibratoHz / sr;
        double hz = (vp.pitchHz ? vp.pitchHz(t) : 150.0) * std::pow(2.0, (vp.vibratoCents * std::sin(kTwoPi * vibPh)) / 1200.0) * (1.0 + jit);
        float glottal = glottalLP.lp(src.saw(hz / sr));
        float breath = breathHP.hp(n.white()) * (float)vp.breath * (0.6f + 0.4f * (float)(src.phase < 0.5));
        float exc = glottal * (float)(1.0 - vp.noiseOnly) + (breath + n.white() * 0.5f * (float)vp.noiseOnly);
        float s = 0;
        for (int k = 0; k < 4; ++k) s += f[k].bp(exc) * gains[k];
        o[i] = s * vp.env.at(t, vp.gate);
    }
    return b;
}

std::vector<int> chord(const KitDNA& dna, int degree, int octave, int size, bool spread) {
    std::vector<int> v;
    for (int i = 0; i < size; ++i) v.push_back(dna.noteInKey(degree + 2 * i, octave));
    if (spread && v.size() >= 3) v[1] += 12;
    return v;
}

std::string chordName(const KitDNA& dna, int degree) {
    auto c = chord(dna, degree, 3, 3);
    int third = c[1] - c[0], fifth = c[2] - c[0];
    std::string q = third == 3 ? (fifth == 6 ? "dim" : "m") : (fifth == 8 ? "aug" : "");
    return std::string(noteName(c[0] % 12)) + q;
}

void kitRoom(Buffer& b, const KitDNA& dna, float wetScale, double tailSec, float sizeOffset) {
    float wet = dna.roomWet * wetScale;
    if (wet <= 0.002f) return;
    fx::reverb(b, clampf(dna.roomSize + sizeOffset, 0, 0.98f), dna.roomDamp, wet, 1.0f, dna.predelayMs, tailSec);
}

} // namespace sdna::synth
