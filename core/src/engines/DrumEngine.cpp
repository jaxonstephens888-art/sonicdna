// Procedural drum synthesis: kicks, snares, claps, rims, toms, percussion, fills.
#include "Synth.h"

namespace sdna {
using namespace synth;

namespace {

struct D { // profile snapshot with per-variant jitter
    float dark, dist, elec, lofi, aggr, atmo, ind, glitch, vin, heavy, energy, mel, exp, hyp;
    D(const GenContext& c, Rng& r) {
        dark = pv(c, r, Desc::Dark); dist = pv(c, r, Desc::Distorted); elec = pv(c, r, Desc::Electronic); lofi = pv(c, r, Desc::Lofi);
        aggr = pv(c, r, Desc::Aggressive); atmo = pv(c, r, Desc::Atmospheric); ind = pv(c, r, Desc::Industrial); glitch = pv(c, r, Desc::Glitch);
        vin = pv(c, r, Desc::Vintage); heavy = pv(c, r, Desc::Heavy); energy = pv(c, r, Desc::Energy); mel = pv(c, r, Desc::Melodic);
        exp = pv(c, r, Desc::Experimental); hyp = pv(c, r, Desc::Hyper);
    }
};

double brightLP(const D& d, double base) { return base * std::pow(2.0, (0.45 - d.dark) * 2.0); }

// ------------------------------------------------------------------ KICK
GenResult kick(const GenContext& c, Rng& r, int forced = -1) {
    D d(c, r);
    const auto& p = c.profile;
    double sr = c.sampleRate;
    bool trapish = hasGroove(p, Groove::Trap) || hasGroove(p, Groove::Drill) || hasGroove(p, Groove::Phonk);
    std::vector<double> w = {
        0.3 + d.heavy + (trapish ? 1.0 : 0.0),
        0.6 + d.energy * 0.5 + d.elec * 0.3,
        d.dist * d.aggr * 2.0 + (hasGroove(p, Groove::Hardstyle) ? 2.5 : 0.0),
        d.ind * 1.5 + d.dist * 0.4,
        (1 - d.elec) * 1.5 + d.vin * 0.3,
        d.lofi * 1.5 + d.vin * 0.5,
        d.hyp * 1.6 + d.exp * 0.5,
        d.dark * d.atmo * 1.2 + ((hasGroove(p, Groove::FourFloor) || hasGroove(p, Groove::Industrial)) ? 0.6 : 0.0),
    };
    static const char* names[] = { "Sub Boom", "Punch", "Hardstyle", "Industrial", "Acoustic", "Dusty", "Plastic", "Rumble" };
    size_t rec = forced >= 0 ? (size_t)forced : pickRecipe(c, r, w);
    double f0 = midiToHz(c.dna.kickNote + c.dna.drumTune * 0.3 + r.bipolar() * c.variation * 1.5);
    double fPunch = f0 < 45 ? f0 * 1.5 : f0;
    const auto& ref = c.dna.ref;
    // Reference audio: tune kicks to the measured kick and follow its decay / sweep.
    auto refT60 = [&](double def) { return (ref.valid && ref.kickDecay > 0) ? def + (ref.kickDecay * r.range(0.85, 1.15) - def) * ref.weight : def; };
    if (ref.valid && ref.kickHz > 25.0f) {
        double target = ref.kickHz * std::pow(2.0, r.gauss() * c.variation * 1.2 / 12.0);
        f0 = f0 + (target - f0) * ref.weight;
        fPunch = f0;
    }
    GenResult g; g.recipe = names[rec];
    Buffer b;
    auto punch = [&](double t60) {
        PitchedDrum pd; pd.f0 = fPunch; pd.startMul = r.range(5, 8); pd.pitchDecay = r.range(0.010, 0.02);
        pd.ampT60 = t60; pd.length = t60 + 0.1; pd.triMix = 0.15; pd.clickAmt = 0.35 + d.aggr * 0.4; pd.harmonicMix = 0.1;
        pd.clickLP = brightLP(d, 7000);
        return pitchedDrum(sr, pd, r.next());
    };
    switch (rec) {
        case 0: {
            PitchedDrum pd; pd.f0 = f0; pd.startMul = r.range(3.0, 5.0); pd.pitchDecay = r.range(0.025, 0.05);
            pd.ampT60 = refT60(0.6 + d.heavy * 1.3 * r.range(0.7, 1.1)); pd.length = pd.ampT60 * 0.85 + 0.15;
            if (ref.valid && ref.kickDrop > 1.2f) pd.startMul = clampd(ref.kickDrop * 1.3, 1.5, 8.0);
            pd.clickAmt = 0.12 + d.aggr * 0.3; pd.clickLP = brightLP(d, 4500);
            b = pitchedDrum(sr, pd, r.next());
            fx::saturate(b, SatType::Soft, d.dist * 0.5f);
            break;
        }
        case 1: b = punch(refT60(r.range(0.25, 0.45))); fx::transient(b, 3.0f + d.aggr * 3.0f, 0.0f); break;
        case 2: {
            PitchedDrum pd; pd.f0 = f0 * 2; pd.startMul = r.range(4.0, 7.0); pd.pitchDecay = r.range(0.015, 0.03);
            pd.ampT60 = r.range(0.45, 0.9); pd.length = pd.ampT60 + 0.05; pd.clickAmt = 0.5;
            b = pitchedDrum(sr, pd, r.next());
            fx::saturate(b, r.chance(0.5) ? SatType::Hard : SatType::Fold, 0.75f + d.dist * 0.25f);
            fx::lowpass(b, brightLP(d, 3500), 0.9);
            fx::highpass(b, 35);
            Buffer click = punch(0.12);
            fx::mixInto(b, click, 0, 0.6f);
            break;
        }
        case 3: {
            b = punch(r.range(0.25, 0.4));
            Buffer m = metalCluster(sr, 0.25, r.range(1.0, 2.2), 0.08, r.next(), 6, r.chance(0.5));
            fx::bandpass(m, r.range(1800, 4000), 1.2);
            fx::mixInto(b, m, 0, 0.35f);
            fx::ringMod(b, r.range(70, 220), 0.25f + d.ind * 0.2f);
            fx::saturate(b, SatType::Hard, 0.5f + d.dist * 0.4f);
            fx::bitcrush(b, r.irange(8, 11), 1, 0.35f);
            break;
        }
        case 4: {
            PitchedDrum pd; pd.f0 = clampd(f0 * 1.5, 50, 75); pd.startMul = 1.6; pd.pitchDecay = 0.03;
            pd.ampT60 = r.range(0.25, 0.4); pd.length = 0.5; pd.modes = { { 1.59, 0.3 }, { 2.14, 0.15 }, { 2.65, 0.08 } };
            pd.clickAmt = 0.7; pd.clickLP = 2500; pd.attackMs = 1.0;
            b = pitchedDrum(sr, pd, r.next());
            fx::reverb(b, 0.3f, 0.6f, 0.12f, 0.6f, 4, 0.35);
            b.makeMono();
            break;
        }
        case 5: {
            b = punch(refT60(r.range(0.2, 0.35)));
            fx::saturate(b, SatType::Tube, 0.4f + d.dist * 0.3f);
            fx::lowpass(b, r.range(2200, 4500), 0.8);
            fx::bitcrush(b, r.irange(9, 12), r.irange(1, 3), 0.7f);
            break;
        }
        case 6: {
            PitchedDrum pd; pd.f0 = fPunch * r.range(1.5, 2.5); pd.startMul = r.range(10, 20); pd.pitchDecay = r.range(0.005, 0.01);
            pd.ampT60 = r.range(0.15, 0.3); pd.length = 0.35; pd.clickAmt = 0.6; pd.clickLP = 12000;
            b = pitchedDrum(sr, pd, r.next());
            Buffer f = fm2(sr, 0.15, pd.f0 * 3, 1.5, 3.0, 0.03, 0.06);
            fx::mixInto(b, f, 0, 0.3f);
            fx::saturate(b, SatType::Fold, 0.3f + d.dist * 0.3f);
            break;
        }
        default: {
            b = punch(r.range(0.25, 0.35));
            Buffer rum = b;
            fx::reverb(rum, 0.92f, 0.7f, 1.0f, 0.0f, 0, 0.9, true);
            rum.makeMono();
            fx::lowpass(rum, 180, 0.9); fx::lowpass(rum, 180, 0.7);
            fx::saturate(rum, SatType::Soft, 0.8f);
            fx::highpass(rum, 32);
            int ramp = (int)(0.06 * sr);
            for (int i = 0; i < rum.length(); ++i) rum.ch[0][(size_t)i] *= std::min(1.0f, (float)i / (float)ramp);
            fx::normalize(rum, -3);
            fx::mixInto(b, rum, 0, 0.55f + d.dark * 0.3f);
            break;
        }
    }
    fx::highpass(b, 22);
    g.audio = std::move(b);
    return g;
}

// ------------------------------------------------------------------ SNARE
Buffer crackSnare(const GenContext& c, Rng& r, const D& d, double bodyHz, double noiseT60) {
    double sr = c.sampleRate;
    PitchedDrum pd; pd.f0 = bodyHz; pd.startMul = 1.5; pd.pitchDecay = 0.02; pd.ampT60 = 0.15; pd.length = 0.5;
    pd.modes = { { 1.78, 0.5 } }; pd.clickAmt = 0.2;
    Buffer b = pitchedDrum(sr, pd, r.next());
    Buffer n = noiseBurst(sr, 0.5, 0.0, noiseT60, 0, r.range(1000, 2000), brightLP(d, 11000), r.next());
    fx::mixInto(b, n, 0, (float)r.range(0.6, 0.95));
    return b;
}

GenResult snare(const GenContext& c, Rng& r, int forced = -1) {
    D d(c, r);
    double sr = c.sampleRate;
    const auto& p = c.profile;
    bool trapish = hasGroove(p, Groove::Trap) || hasGroove(p, Groove::Drill);
    std::vector<double> w = {
        0.6 + (1 - d.elec) * 0.4,
        0.3 + (trapish ? 1.4 : 0.0) + d.elec * 0.3,
        d.lofi * 1.5 + d.vin * 0.3,
        d.ind * 1.6 + d.dist * 0.3,
        d.vin * d.atmo * 1.5 + (hasGroove(p, Groove::Electroclash) ? 0.6 : 0.0),
        (1 - d.elec) * 1.0 + (1 - d.aggr) * 0.3,
        d.hyp * 1.6 + d.exp * 0.4,
        d.aggr * d.dist * 1.5 + (hasGroove(p, Groove::Breakbeat) ? 0.7 : 0.0),
    };
    static const char* names[] = { "Crack", "Snap", "Dusty", "Metal", "Gated", "Brush", "Plastic", "Breakbeat" };
    size_t rec = forced >= 0 ? (size_t)forced : pickRecipe(c, r, w);
    double tune = std::pow(2.0, c.dna.drumTune / 12.0);
    const auto& ref = c.dna.ref;
    bool follow = ref.valid && ref.snareDecay > 0;
    auto body = [&](double def) { return (follow && ref.snareHz > 100) ? def + (ref.snareHz * r.range(0.95, 1.05) - def) * ref.weight : def; };
    auto ndec = [&](double def) { return follow ? def + (ref.snareDecay * r.range(0.85, 1.15) - def) * ref.weight : def; };
    GenResult g; g.recipe = names[rec];
    Buffer b;
    switch (rec) {
        case 0: b = crackSnare(c, r, d, body(r.range(170, 230) * tune), ndec(r.range(0.15, 0.3))); break;
        case 1: {
            b = crackSnare(c, r, d, body(r.range(200, 250) * tune), ndec(r.range(0.09, 0.14)));
            Buffer cl = clapBursts(sr, 2, 8, 0.08, 1500, 1.0, r.next(), 0.3);
            fx::mixInto(b, cl, 0, 0.45f);
            fx::transient(b, 4, -1);
            break;
        }
        case 2:
            b = crackSnare(c, r, d, body(r.range(160, 210) * tune), ndec(r.range(0.12, 0.22)));
            fx::saturate(b, SatType::Tube, 0.5f);
            fx::lowpass(b, r.range(3500, 6000), 0.8);
            fx::bitcrush(b, r.irange(8, 11), r.irange(1, 3), 0.7f);
            break;
        case 3: {
            b = noiseBurst(sr, 0.45, 0, r.range(0.15, 0.25), 0, 800, 12000, r.next());
            Buffer m = metalCluster(sr, 0.4, r.range(1.2, 2.2), 0.15, r.next(), 6, true);
            fx::bandpass(m, r.range(1800, 3500), 1.0);
            fx::mixInto(b, m, 0, 0.6f);
            fx::ringMod(b, r.range(400, 900), 0.4f);
            fx::saturate(b, SatType::Hard, 0.6f);
            fx::reverb(b, 0.4f, 0.4f, 0.15f, 0.8f, 3, 0.3);
            b.makeMono();
            break;
        }
        case 4: {
            b = crackSnare(c, r, d, r.range(180, 220) * tune, 0.2);
            fx::reverb(b, 0.85f, 0.3f, 0.55f, 1.0f, 2, 0.5);
            int gate = (int)(r.range(0.22, 0.32) * sr);
            b.resize(gate);
            fx::fadeOut(b, 0.015);
            break;
        }
        case 5: {
            b = noiseBurst(sr, 0.45, 0.012, r.range(0.2, 0.3), 1, 600, 9000, r.next(), r.range(2500, 3500), 0.7);
            PitchedDrum pd; pd.f0 = 160 * tune; pd.startMul = 1.2; pd.ampT60 = 0.1; pd.length = 0.3; pd.clickAmt = 0.05;
            Buffer body = pitchedDrum(sr, pd, r.next());
            fx::mixInto(b, body, 0, 0.4f);
            break;
        }
        case 6: {
            b = fm2(sr, 0.25, r.range(300, 600) * tune, 2.4, 4.0, 0.02, 0.08);
            Buffer n = noiseBurst(sr, 0.2, 0, 0.05, 0, 4000, 16000, r.next());
            fx::mixInto(b, n, 0, 0.7f);
            break;
        }
        default:
            b = crackSnare(c, r, d, body(r.range(180, 220) * tune), ndec(r.range(0.18, 0.28)));
            fx::compress(b, -22, 8, 1, 60, 10);
            fx::saturate(b, SatType::Soft, 0.5f);
            fx::reverb(b, 0.35f, 0.5f, 0.12f, 0.8f, 3, 0.3);
            b.makeMono();
            break;
    }
    fx::highpass(b, 90);
    if (follow && ref.snareBright > 500) fx::lowpass(b, std::min(16000.0, (double)ref.snareBright * (3.5 - 1.5 * ref.weight)), 0.6);
    g.audio = std::move(b);
    return g;
}

// ------------------------------------------------------------------ CLAP
GenResult clap(const GenContext& c, Rng& r) {
    D d(c, r);
    double sr = c.sampleRate;
    std::vector<double> w = { 0.7 + d.energy * 0.3, d.atmo * 1.2 + 0.2, d.vin * 1.0 + d.elec * 0.4, 0.5 + d.aggr * 0.3, d.lofi * 1.5, d.dist * d.aggr * 1.8, d.hyp * 1.5 };
    static const char* names[] = { "Tight", "Roomy", "808", "Layered", "Dusty", "Distorted", "Stacked" };
    size_t rec = pickRecipe(c, r, w);
    GenResult g; g.recipe = names[rec];
    double bp = r.range(900, 1600) * std::pow(2.0, (0.45 - d.dark) * 0.8);
    Buffer b;
    switch (rec) {
        case 0: b = clapBursts(sr, 3, r.range(7, 10), r.range(0.08, 0.13), bp, 1.2, r.next(), 0.4); break;
        case 1: b = clapBursts(sr, 4, r.range(8, 12), 0.2, bp, 1.0, r.next(), 0.6); kitRoom(b, c.dna, 1.5f, 0.8, 0.1f); break;
        case 2: b = clapBursts(sr, 4, 11, 0.25, 1000, 0.9, r.next(), 0.6); fx::reverb(b, 0.2f, 0.5f, 0.12f, 0.5f, 1, 0.2); b.makeMono(); break;
        case 3: {
            b = clapBursts(sr, 3, 8, 0.1, bp, 1.2, r.next(), 0.4);
            Buffer snap = noiseBurst(sr, 0.1, 0, 0.015, 0, 1000, 16000, r.next(), 2500, 4);
            fx::mixInto(b, snap, (int)(0.012 * sr), 0.8f);
            Buffer sn = crackSnare(c, r, d, 200, 0.1);
            fx::mixInto(b, sn, 0, 0.3f);
            break;
        }
        case 4: b = clapBursts(sr, 3, 9, 0.12, bp, 1.1, r.next(), 0.4); fx::lowpass(b, 5000, 0.7); fx::bitcrush(b, 10, 2, 0.7f); break;
        case 5: b = clapBursts(sr, 4, 10, 0.18, bp, 1.0, r.next(), 0.5); fx::saturate(b, SatType::Hard, 0.8f); fx::highpass(b, 300); break;
        default: {
            b = clapBursts(sr, 3, 8, 0.1, bp, 1.2, r.next(), 0.4);
            Buffer hi = b; fx::resample(hi, 1.35); Buffer lo = b; fx::resample(lo, 0.8);
            fx::mixInto(b, hi, 0, 0.7f); fx::mixInto(b, lo, 0, 0.5f);
            break;
        }
    }
    g.audio = std::move(b);
    return g;
}

// ------------------------------------------------------------------ RIM
GenResult rim(const GenContext& c, Rng& r) {
    D d(c, r);
    double sr = c.sampleRate;
    std::vector<double> w = { 0.5 + d.elec * 0.6 + d.vin * 0.3, (1 - d.elec) * 1.0 + 0.2, (1 - d.elec) * 0.8, d.ind * 1.3 + d.exp * 0.5, d.lofi * 1.4 };
    static const char* names[] = { "808 Rim", "Woodblock", "Sidestick", "Clink", "Dusty Rim" };
    size_t rec = pickRecipe(c, r, w);
    double tune = std::pow(2.0, c.dna.drumTune / 12.0) * r.range(0.9, 1.1);
    GenResult g; g.recipe = names[rec];
    Buffer b;
    auto rim808 = [&]() {
        Buffer x = additive(sr, 0.12, 1.0, { 470 * tune, 1680 * tune }, { 0.6, 0.4 }, { 0.04, 0.025 }, 0.0002, r.next());
        Buffer clk = noiseBurst(sr, 0.03, 0, 0.005, 0, 3000, 16000, r.next());
        fx::mixInto(x, clk, 0, 0.5f);
        fx::bandpass(x, 1200, 0.6);
        fx::highpass(x, 300);
        return x;
    };
    switch (rec) {
        case 0: b = rim808(); break;
        case 1: { PitchedDrum pd; pd.f0 = r.range(800, 1200) * tune; pd.startMul = 1.4; pd.pitchDecay = 0.004; pd.ampT60 = 0.05; pd.length = 0.1; pd.clickAmt = 0.1; b = pitchedDrum(sr, pd, r.next()); break; }
        case 2: {
            b = noiseBurst(sr, 0.12, 0, 0.03, 0, 500, 12000, r.next(), r.range(1800, 2600), 3);
            PitchedDrum pd; pd.f0 = 350 * tune; pd.startMul = 1.2; pd.ampT60 = 0.04; pd.length = 0.1; pd.clickAmt = 0.0;
            Buffer body = pitchedDrum(sr, pd, r.next()); fx::mixInto(b, body, 0, 0.6f);
            break;
        }
        case 3: b = fm2(sr, 0.2, r.range(900, 1600) * tune, 3.7, 3.0, 0.03, 0.08); break;
        default: b = rim808(); fx::lowpass(b, 4000, 0.8); fx::bitcrush(b, 9, 2, 0.8f); fx::saturate(b, SatType::Tube, 0.4f); break;
    }
    g.audio = std::move(b);
    return g;
}

// ------------------------------------------------------------------ TOM
GenResult tom(const GenContext& c, Rng& r, int forced = -1, int forcedDegree = -999) {
    D d(c, r);
    double sr = c.sampleRate;
    std::vector<double> w = { d.elec * 0.8 + d.heavy * 0.5, (1 - d.elec) * 1.3 + 0.2, d.vin * d.elec * 1.5, d.ind * 1.4 + d.dist * 0.4 };
    static const char* names[] = { "808 Tom", "Acoustic", "Simmons", "Industrial" };
    size_t rec = forced >= 0 ? (size_t)forced : pickRecipe(c, r, w);
    static const int degs[] = { 0, 2, 4, 5, 7, 9, -2, 11 };
    int deg = forcedDegree != -999 ? forcedDegree : degs[c.variant % 8];
    int note = c.dna.noteInKey(deg, 2);
    double f = midiToHz(note);
    GenResult g; g.recipe = names[rec]; g.rootNote = note;
    Buffer b;
    PitchedDrum pd; pd.f0 = f;
    switch (rec) {
        case 0: pd.startMul = 1.6; pd.pitchDecay = 0.06; pd.ampT60 = r.range(0.5, 0.9); pd.length = pd.ampT60 + 0.1; pd.clickAmt = 0.15; b = pitchedDrum(sr, pd, r.next()); break;
        case 1:
            pd.startMul = 1.3; pd.pitchDecay = 0.04; pd.ampT60 = r.range(0.35, 0.6); pd.length = pd.ampT60 + 0.1;
            pd.modes = { { 1.5, 0.35 }, { 1.98, 0.2 }, { 2.44, 0.1 } }; pd.clickAmt = 0.6; pd.clickLP = 3000; pd.attackMs = 1;
            b = pitchedDrum(sr, pd, r.next()); fx::reverb(b, 0.35f, 0.5f, 0.12f, 0.7f, 5, 0.4); b.makeMono();
            break;
        case 2: {
            pd.startMul = 2.2; pd.pitchDecay = 0.12; pd.ampT60 = 0.4; pd.length = 0.6; pd.clickAmt = 0.2;
            b = pitchedDrum(sr, pd, r.next());
            Buffer n = noiseBurst(sr, 0.5, 0, 0.2, 1, 200, 3000, r.next());
            fx::mixInto(b, n, 0, 0.35f);
            break;
        }
        default:
            pd.startMul = 1.4; pd.pitchDecay = 0.04; pd.ampT60 = 0.45; pd.length = 0.6; pd.modes = { { 1.5, 0.3 }, { 2.3, 0.2 } }; pd.clickAmt = 0.5;
            b = pitchedDrum(sr, pd, r.next());
            { Buffer m = metalCluster(sr, 0.3, 1.0, 0.1, r.next(), 6, true); fx::bandpass(m, 2500, 1.0); fx::mixInto(b, m, 0, 0.25f); }
            fx::saturate(b, SatType::Hard, 0.6f);
            fx::reverb(b, 0.5f, 0.4f, 0.15f, 0.7f, 5, 0.4); b.makeMono();
            break;
    }
    g.audio = std::move(b);
    return g;
}

// ------------------------------------------------------------------ PERCUSSION
GenResult perc(const GenContext& c, Rng& r) {
    D d(c, r);
    double sr = c.sampleRate;
    const auto& p = c.profile;
    std::vector<double> w = {
        0.3 + (hasGroove(p, Groove::Phonk) ? 2.5 : 0.0) + d.vin * 0.5 + (hasGroove(p, Groove::Trap) ? 0.4 : 0.0),
        (1 - d.elec) * 1.0 + ((hasGroove(p, Groove::Reggaeton) || hasGroove(p, Groove::RnB)) ? 0.8 : 0.0),
        0.3 + (1 - d.elec) * 0.5 + (hasGroove(p, Groove::Reggaeton) ? 0.6 : 0.0),
        d.ind * 1.5 + d.exp * 0.3,
        (1 - d.elec) * 0.5 + d.vin * 0.4,
        d.hyp * 1.7 + d.exp * 0.4,
        d.exp * 1.0 + d.glitch * 0.7,
        d.mel * 0.4 + 0.2,
    };
    static const char* names[] = { "Cowbell", "Conga", "Clave", "Metal Hit", "Tambourine", "Latex", "Found Click", "Agogo" };
    size_t rec = pickRecipe(c, r, w);
    double tune = std::pow(2.0, c.dna.drumTune / 12.0);
    GenResult g; g.recipe = names[rec];
    Buffer b;
    switch (rec) {
        case 0: {
            double t = r.range(0.95, 1.35) * tune;
            b = additive(sr, 0.6, 1.0, {}, {}, {}, 0, 1);
            Osc o1, o2; b = Buffer(1, (int)(0.6 * sr), sr);
            double t60 = r.range(0.25, 0.5);
            for (int i = 0; i < b.length(); ++i) { double tt = i / sr; b.ch[0][(size_t)i] = (o1.square(540 * t / sr) + o2.square(800 * t / sr)) * 0.5f * (expDecay(tt, 0.05) * 0.6f + expDecay(tt, t60) * 0.4f); }
            fx::bandpass(b, 900 * t, 1.6); fx::highpass(b, 300);
            break;
        }
        case 1: {
            PitchedDrum pd; pd.f0 = r.range(180, 350) * tune; pd.startMul = 1.3; pd.pitchDecay = 0.02; pd.ampT60 = r.range(0.2, 0.35); pd.length = 0.4;
            pd.modes = { { 1.6, 0.2 } }; pd.clickAmt = 0.1;
            b = pitchedDrum(sr, pd, r.next());
            Buffer slap = noiseBurst(sr, 0.05, 0, 0.012, 0, 800, 8000, r.next(), 1500, 1.5);
            fx::mixInto(b, slap, 0, r.range(0.3, 0.8) > 0 ? 0.6f : 0.0f);
            break;
        }
        case 2: { PitchedDrum pd; pd.f0 = r.range(2000, 2600) * tune; pd.startMul = 1.05; pd.ampT60 = 0.05; pd.length = 0.1; pd.clickAmt = 0.05; b = pitchedDrum(sr, pd, r.next()); break; }
        case 3: {
            b = fm2(sr, 0.5, r.range(300, 900), r.chance(0.5) ? 1.41 : 2.76, 6.0, 0.1, r.range(0.15, 0.35));
            Buffer n = noiseBurst(sr, 0.2, 0, 0.04, 0, 2000, 14000, r.next());
            fx::mixInto(b, n, 0, 0.4f);
            if (d.dist > 0.4f) fx::saturate(b, SatType::Hard, 0.5f);
            break;
        }
        case 4: {
            b = Buffer(1, (int)(0.35 * sr), sr);
            for (int k = 0; k < 3; ++k) { Buffer j = noiseBurst(sr, 0.3, 0.001, k == 2 ? 0.2 : 0.04, 0, 5000, 16000, r.next(), r.range(6500, 9000), 2.0); fx::mixInto(b, j, (int)(k * r.range(0.008, 0.016) * sr), 1.0f); }
            Buffer m = metalCluster(sr, 0.3, 3.0, 0.15, r.next()); fx::highpass(m, 7000); fx::mixInto(b, m, 0, 0.4f);
            break;
        }
        case 5: {
            int len = (int)(r.range(0.08, 0.2) * sr);
            b = Buffer(1, len, sr);
            double f0 = r.range(250, 700) * tune, f1 = f0 * r.range(0.4, 2.8);
            double ph = 0; SVF res; res.set(sr, f0 * 3, 6);
            for (int i = 0; i < len; ++i) {
                double t = (double)i / len;
                double f = f0 * std::pow(f1 / f0, std::pow(t, 0.5));
                ph += f / sr;
                float s = (float)std::sin(kTwoPi * ph) * (float)(1.0 - t) * (float)std::min(1.0, i / (0.002 * sr));
                b.ch[0][(size_t)i] = s + res.bp(s) * 0.5f;
            }
            fx::saturate(b, SatType::Soft, 0.3f);
            break;
        }
        case 6: {
            b = Buffer(1, (int)(0.25 * sr), sr);
            b.ch[0][0] = 1.0f; b.ch[0][1] = -0.6f;
            Buffer acc(1, b.length(), sr);
            for (int k = 0; k < 3; ++k) { Buffer x = b; fx::bandpass(x, r.range(400, 6000), r.range(8, 30)); fx::mixInto(acc, x, 0, 1.0f); }
            b = acc;
            Buffer n = noiseBurst(sr, 0.02, 0, 0.004, 0, 2000, 16000, r.next()); fx::mixInto(b, n, 0, 0.3f);
            break;
        }
        default: b = fm2(sr, 0.5, c.dna.noteInKey(r.irange(0, 4), 5) > 0 ? midiToHz(c.dna.noteInKey(r.irange(0, 4), 5)) : 1200, 2.0, 2.0, 0.05, 0.2); break;
    }
    g.audio = std::move(b);
    return g;
}

// ------------------------------------------------------------------ FILLS
GenResult fill(const GenContext& c, Rng& r) {
    D d(c, r);
    double sr = c.sampleRate;
    std::vector<double> w = { 0.6 + (1 - d.elec) * 0.4, 0.5 + d.energy * 0.5, d.glitch * 1.6 + d.exp * 0.4, 0.4 + (hasGroove(c.profile, Groove::Breakbeat) ? 1.0 : 0.0), (1 - d.elec) * 0.5 + d.ind * 0.4 };
    static const char* names[] = { "Tom Run", "Snare Roll", "Glitch Roll", "Break Fill", "Tribal" };
    size_t rec = pickRecipe(c, r, w);
    double bpm = c.dna.bpm;
    double sixteenth = 60.0 / bpm / 4.0;
    int bars = 1;
    Buffer out(1, (int)((16 * sixteenth * bars + 1.0) * sr), sr);
    GenResult g; g.recipe = names[rec]; g.bpm = (float)bpm;
    auto at = [&](double step) { return (int)(step * sixteenth * sr); };
    auto sub = [&](uint64_t s) { return GenContext{ c.dna, c.profile, 0.0f, 1, s, sr }; };
    int snRec = -1, tomRec = -1;
    switch (rec) {
        case 0: {
            int degsDown[] = { 9, 7, 5, 4, 2, 0, -2, -3 };
            for (int i = 0; i < 8; ++i) { Rng rr(c.seed + (uint64_t)i); auto cc = sub(c.seed + (uint64_t)i); auto t = tom(cc, rr, tomRec, degsDown[i]); tomRec = 1; fx::mixInto(out, t.audio, at(8 + i), 0.8f + 0.2f * (float)(i % 2 == 0)); }
            Rng rr(c.seed ^ 5); auto cc = sub(c.seed ^ 5); auto k = kick(cc, rr, -1); fx::mixInto(out, k.audio, at(0), 1.0f); fx::mixInto(out, k.audio, at(4), 0.9f);
            break;
        }
        case 1: {
            Rng rr(c.seed ^ 3); auto cc = sub(c.seed ^ 3); auto s = snare(cc, rr, snRec);
            for (int i = 0; i < 16; ++i) fx::mixInto(out, s.audio, at(i), 0.25f + 0.75f * (float)i / 15.0f);
            for (int i = 0; i < 8; ++i) fx::mixInto(out, s.audio, at(12 + i * 0.5), 0.6f + 0.4f * (float)i / 7.0f);
            break;
        }
        case 2: {
            Rng rr(c.seed ^ 7); auto cc = sub(c.seed ^ 7); auto s = snare(cc, rr, -1); auto k = kick(cc, rr, -1);
            double pos = 0; double stepLen = 2.0; int n = 0;
            while (pos < 16 && n < 64) {
                Buffer hit = (n % 3 == 0) ? k.audio : s.audio;
                Buffer h2 = hit; fx::resample(h2, std::pow(2.0, (pos / 16.0) * 12.0 / 12.0));
                fx::mixInto(out, h2, at(pos), 0.8f);
                pos += stepLen; stepLen = std::max(0.125, stepLen * 0.78); ++n;
            }
            fx::bitcrush(out, 10, 2, 0.5f);
            break;
        }
        case 3: {
            Rng rr(c.seed ^ 9); auto cc = sub(c.seed ^ 9); auto s = snare(cc, rr, -1); auto k = kick(cc, rr, -1);
            int ks[] = { 0, 3, 10 }; int ss[] = { 4, 7, 12, 14, 15 }; int ghosts[] = { 2, 6, 9, 11, 13 };
            for (int x : ks) fx::mixInto(out, k.audio, at(x), 1.0f);
            for (int x : ss) fx::mixInto(out, s.audio, at(x), 0.95f);
            for (int x : ghosts) fx::mixInto(out, s.audio, at(x), 0.25f);
            break;
        }
        default: {
            int pattern[] = { 0, 3, 6, 8, 10, 11, 13, 14, 15 };
            int i = 0;
            for (int x : pattern) { Rng rr(c.seed + (uint64_t)x); auto cc = sub(c.seed + (uint64_t)x); auto t = tom(cc, rr, -1, (x * 3) % 7 - 2); fx::mixInto(out, t.audio, at(x), (i++ % 2) ? 0.7f : 1.0f); }
            break;
        }
    }
    fx::normalize(out, -1);
    g.audio = std::move(out);
    return g;
}

class DrumEngine : public SoundEngine {
public:
    std::string id() const override { return "procedural.drums"; }
    std::string description() const override { return "Procedural drum synthesis (pitched membranes, noise bursts, modal resonators, FM)"; }
    bool handles(Category c) const override { return c == Category::Kick || c == Category::Snare || c == Category::Clap || c == Category::Rim || c == Category::Tom || c == Category::Perc || c == Category::Fill; }
    GenResult generate(Category cat, const GenContext& c) const override {
        Rng r(c.seed);
        switch (cat) {
            case Category::Kick: return kick(c, r);
            case Category::Snare: return snare(c, r);
            case Category::Clap: return clap(c, r);
            case Category::Rim: return rim(c, r);
            case Category::Tom: return tom(c, r);
            case Category::Perc: return perc(c, r);
            default: return fill(c, r);
        }
    }
};

} // namespace

EnginePtr makeDrumEngine() { return std::make_shared<DrumEngine>(); }

} // namespace sdna
