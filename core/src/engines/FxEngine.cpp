// Sound design: risers, impacts, transitions, glitches, noise, atmospheres, textures, reverse.
// Several recipes *resample* other engines' output (granular clouds of pads,
// stutters of drum hits, reversed reverb tails) so textures share the kit's DNA.
#include "Synth.h"

namespace sdna {
using namespace synth;

namespace {

struct F {
    float dark, dist, elec, lofi, aggr, atmo, ind, glitch, vin, heavy, energy, mel, exp, hyp;
    F(const GenContext& c, Rng& r) {
        dark = pv(c, r, Desc::Dark); dist = pv(c, r, Desc::Distorted); elec = pv(c, r, Desc::Electronic); lofi = pv(c, r, Desc::Lofi);
        aggr = pv(c, r, Desc::Aggressive); atmo = pv(c, r, Desc::Atmospheric); ind = pv(c, r, Desc::Industrial); glitch = pv(c, r, Desc::Glitch);
        vin = pv(c, r, Desc::Vintage); heavy = pv(c, r, Desc::Heavy); energy = pv(c, r, Desc::Energy); mel = pv(c, r, Desc::Melodic);
        exp = pv(c, r, Desc::Experimental); hyp = pv(c, r, Desc::Hyper);
    }
    double cut() const { return std::pow(2.0, (0.45 - dark) * 1.6); }
};

// Source material borrowed from the other engines (resampling).
Buffer borrow(const GenContext& c, Category cat, uint64_t seed) {
    static EngineRegistry reg; // built-ins only; thread-safe after init (C++11 statics)
    GenContext sub{ c.dna, c.profile, 0.3f, 1, seed, c.sampleRate };
    return reg.engineFor(cat)->generate(cat, sub).audio;
}

class FxEngine : public SoundEngine {
public:
    std::string id() const override { return "procedural.fx"; }
    std::string description() const override { return "Noise sweeps, Shepard tones, granular clouds, resampling, stutter and tape effects"; }
    bool handles(Category c) const override {
        return c == Category::Riser || c == Category::Impact || c == Category::Transition || c == Category::Glitch || c == Category::Noise
            || c == Category::Atmosphere || c == Category::Texture || c == Category::Reverse;
    }

    GenResult generate(Category cat, const GenContext& c) const override {
        Rng r(c.seed);
        F f(c, r);
        double sr = c.sampleRate;
        const KitDNA& dna = c.dna;
        double bar = 240.0 / dna.bpm;
        GenResult g;
        Buffer b;
        switch (cat) {
            case Category::Riser: {
                std::vector<double> w = { 0.6 + f.elec * 0.3, 0.4 + f.mel * 0.4, 0.3 + f.exp * 0.5 + f.atmo * 0.2, 0.3 + f.dark * 0.4, f.glitch * 1.4 };
                static const char* names[] = { "Noise Sweep", "Synth Rise", "Shepard", "Tension", "Glitch Build" };
                size_t rec = pickRecipe(c, r, w);
                g.recipe = names[rec]; g.bpm = (float)dna.bpm;
                double len = std::min(8.0, bar * (dna.bpm > 110 ? 4 : 2));
                int n = (int)(len * sr);
                switch (rec) {
                    case 0: {
                        b = noiseBurst(sr, len, len, 100.0, 0, 20, 20000, r.next());
                        fx::sweep(b, 2, 300, 9000 * f.cut(), 2.5, 1.5);
                        break;
                    }
                    case 1: {
                        b = Buffer(1, n, sr);
                        Osc o1, o2(0.5); double base = midiToHz(dna.noteInKey(0, 3));
                        for (int i = 0; i < n; ++i) { double t = (double)i / n; double fr = base * std::pow(2.0, t * 2.0);
                            b.ch[0][(size_t)i] = (o1.saw(fr / sr) + o2.saw(fr * 1.007 / sr)) * 0.4f * (float)(t * t); }
                        fx::sweep(b, 0, 300, 12000 * f.cut(), 1.0, 1.3);
                        break;
                    }
                    case 2: {
                        b = Buffer(1, n, sr);
                        std::vector<double> ph(6, 0.0); double base = midiToHz(dna.noteInKey(0, 2));
                        for (int i = 0; i < n; ++i) { double t = (double)i / n; float s = 0;
                            for (int k = 0; k < 6; ++k) { double pos = std::fmod(k / 6.0 + t * 1.0, 1.0); double fr = base * std::pow(2.0, pos * 6.0);
                                double amp = std::sin(kPi * pos); ph[(size_t)k] += fr / sr; s += (float)(std::sin(kTwoPi * ph[(size_t)k]) * amp); }
                            b.ch[0][(size_t)i] = s * 0.25f * (float)(0.3 + 0.7 * t); }
                        break;
                    }
                    case 3: {
                        auto ch = chord(dna, 0, 3, 3); ch.push_back(ch[0] + 1); // add a semitone cluster for tension
                        b = Buffer(1, n, sr);
                        std::vector<Osc> os(ch.size());
                        for (int i = 0; i < n; ++i) { double t = (double)i / n; float s = 0;
                            for (size_t k = 0; k < ch.size(); ++k) s += os[k].saw(midiToHz(ch[k]) * std::pow(2.0, t * 1.0 / 12.0 * 12.0 * 0.25) / sr);
                            b.ch[0][(size_t)i] = s * 0.2f * (float)(t * t) * (1.0f + 0.3f * (float)std::sin(kTwoPi * (4 + 12 * t) * t)); }
                        fx::sweep(b, 0, 400, 6000 * f.cut(), 1.0, 1.0);
                        break;
                    }
                    default: {
                        Buffer hit = borrow(c, Category::Snare, r.next());
                        b = Buffer(1, n, sr);
                        double pos = 0; double step = bar / 4.0; int k = 0;
                        while (pos < len && k < 400) { Buffer h = hit; fx::resample(h, std::pow(2.0, pos / len)); fx::mixInto(b, h, (int)(pos * sr), (float)(0.3 + 0.7 * pos / len)); pos += step; step = std::max(bar / 64.0, step * 0.85); ++k; }
                        b.resize(n);
                        fx::sweep(b, 1, 60, 2000, 0.7, 2.0);
                        break;
                    }
                }
                b.makeStereo();
                fx::widen(b, 0.3f);
                kitRoom(b, dna, 1.2f, 1.0);
                b.resize(n);
                fx::fadeOut(b, 0.03);
                break;
            }
            case Category::Impact: {
                std::vector<double> w = { 0.5 + f.heavy * 0.6, 0.3 + f.atmo * 0.5 + f.mel * 0.2, f.ind * 1.3 + f.dist * 0.4, 0.4 + f.dark * 0.3, f.hyp * 1.3 + f.exp * 0.3 };
                static const char* names[] = { "Sub Boom", "Cinematic", "Industrial Slam", "Downer", "Snap" };
                size_t rec = pickRecipe(c, r, w);
                g.recipe = names[rec];
                PitchedDrum pd; pd.f0 = midiToHz(dna.kickNote - 12 + 12 * (rec == 4)); pd.startMul = rec == 3 ? 6 : 3; pd.pitchDecay = rec == 3 ? 0.6 : 0.15;
                pd.ampT60 = r.range(2.0, 4.0); pd.length = 4.0; pd.clickAmt = 0.5;
                b = pitchedDrum(sr, pd, r.next());
                Buffer n = noiseBurst(sr, 2.5, 0.001, r.range(0.4, 1.2), 1, 80, 9000 * f.cut(), r.next());
                fx::mixInto(b, n, 0, 0.5f);
                if (rec == 1) { Buffer t = borrow(c, Category::Tom, r.next()); fx::mixInto(b, t, 0, 0.6f); Buffer cr = borrow(c, Category::Crash, r.next()); fx::mixInto(b, cr, 0, 0.3f); }
                if (rec == 2) { Buffer m = metalCluster(sr, 1.5, 0.8, 0.8, r.next(), 9, true); fx::bandpass(m, 1500, 0.8); fx::mixInto(b, m, 0, 0.6f); fx::saturate(b, SatType::Hard, 0.7f); }
                if (rec == 4) { Buffer s = borrow(c, Category::Clap, r.next()); fx::mixInto(b, s, 0, 1.0f); fx::saturate(b, SatType::Fold, 0.4f); }
                kitRoom(b, dna, 2.0f, 3.0, 0.2f);
                break;
            }
            case Category::Transition: {
                std::vector<double> w = { 0.6, 0.6 + f.atmo * 0.3, 0.3 + f.vin * 0.6 + f.lofi * 0.3, 0.3 + f.glitch * 0.5, 0.3 + f.elec * 0.3 };
                static const char* names[] = { "Downlifter", "Whoosh", "Tape Stop", "Reverse Swell", "Filter Sweep" };
                size_t rec = pickRecipe(c, r, w);
                g.recipe = names[rec];
                double len = std::min(4.0, bar * (rec == 2 ? 1 : 2));
                int n = (int)(len * sr);
                switch (rec) {
                    case 0: b = noiseBurst(sr, len, 0.005, len * 0.9, 0, 20, 20000, r.next()); fx::sweep(b, 2, 9000 * f.cut(), 150, 2.0, 0.6); break;
                    case 1: { b = noiseBurst(sr, len, len * 0.5, len * 1.2, 1, 20, 20000, r.next()); for (int i = 0; i < n; ++i) { double t = (double)i / n; b.ch[0][(size_t)i] *= (float)std::sin(kPi * t); } fx::sweep(b, 2, 400, 5000, 3.0, 1.0); break; }
                    case 2: {
                        Buffer src = borrow(c, Category::Pad, r.next());
                        src.resize(n);
                        b = Buffer(src.numChannels(), n, sr);
                        double pos = 0;
                        for (int i = 0; i < n; ++i) { double t = (double)i / n; double speed = std::max(0.0, 1.0 - t); speed *= speed;
                            int p0 = (int)pos; float fr = (float)(pos - p0);
                            for (int ch = 0; ch < b.numChannels(); ++ch) { const auto& sc = src.ch[(size_t)ch];
                                float a0 = p0 < src.length() ? sc[(size_t)p0] : 0.0f, a1 = p0 + 1 < src.length() ? sc[(size_t)p0 + 1] : 0.0f;
                                b.ch[(size_t)ch][(size_t)i] = (a0 + (a1 - a0) * fr) * (float)std::min(1.0, speed * 4.0 + 0.05); }
                            pos += speed; }
                        fx::lowpass(b, 6000, 0.7);
                        fx::fadeOut(b, 0.02);
                        break;
                    }
                    case 3: { Buffer src = borrow(c, Category::Keys, r.next()); fx::reverb(src, 0.9f, 0.4f, 1.0f, 1.0f, 20, 2.0, true); fx::reverse(src); fx::trimLead(src, -50); b = src; break; }
                    default: { Buffer src = borrow(c, Category::Strings, r.next()); src.resize(n); fx::sweep(src, 0, 18000, 200, 1.5, 0.7); b = src; break; }
                }
                b.makeStereo();
                fx::widen(b, 0.4f);
                break;
            }
            case Category::Glitch: {
                std::vector<double> w = { 0.6 + f.glitch * 0.5, 0.4 + f.lofi * 0.6, 0.3 + f.exp * 0.6, 0.3 + f.glitch * 0.6, 0.3 + f.hyp * 0.6 };
                static const char* names[] = { "Stutter", "Bit Error", "Data", "Granular Chop", "Pitch Warp" };
                size_t rec = pickRecipe(c, r, w);
                g.recipe = names[rec]; g.bpm = (float)dna.bpm;
                double len = std::min(2.5, bar * 0.5);
                int n = (int)(len * sr);
                switch (rec) {
                    case 0: {
                        b = Buffer(1, n, sr);
                        Buffer k = borrow(c, Category::Kick, r.next()), s = borrow(c, Category::Snare, r.next()), h = borrow(c, Category::ClosedHat, r.next());
                        double six = bar / 16.0;
                        for (int i = 0; i < 8; ++i) { const Buffer& src = i % 4 == 0 ? k : (i % 4 == 2 ? s : h); fx::mixInto(b, src, (int)(i * six * sr), 0.8f); }
                        b.resize(n);
                        fx::stutter(b, r.next(), 0.7f, dna.bpm);
                        break;
                    }
                    case 1: {
                        b = Buffer(1, n, sr);
                        int pos = 0;
                        while (pos < n) { int seg = (int)(r.range(0.005, 0.06) * sr); float v = r.chance(0.3) ? 0.0f : r.bipolar(); double fr = r.range(100, 3000); Osc o;
                            for (int i = 0; i < seg && pos < n; ++i, ++pos) b.ch[0][(size_t)pos] = r.chance(0.5) ? v * o.square(fr / sr) : v; }
                        fx::lowpass(b, 9000, 0.7);
                        break;
                    }
                    case 2: {
                        b = Buffer(1, n, sr);
                        uint64_t x = r.next(); int hold = 1;
                        float cur = 0;
                        for (int i = 0; i < n; ++i) { if (--hold <= 0) { x ^= x << 13; x ^= x >> 7; x ^= x << 17; cur = (float)((x & 0xFF) / 127.5 - 1.0); hold = 1 + (int)((x >> 8) % 24); } b.ch[0][(size_t)i] = cur; }
                        fx::bandpass(b, r.range(800, 5000), 0.8);
                        fx::gain(b, 0.6f);
                        break;
                    }
                    case 3: {
                        Buffer src = borrow(c, r.chance(0.5) ? Category::VocalChop : Category::Pluck, r.next());
                        fx::GrainParams gp; gp.grainMs = r.range(15, 50); gp.density = r.range(40, 90); gp.position = 0.2; gp.positionJitter = 0.2; gp.pitchSet = { 0, 12, -12, 7 }; gp.reverseProb = 0.5;
                        b = fx::granulate(src, len, gp, r.next());
                        fx::stutter(b, r.next(), 0.4f, dna.bpm);
                        break;
                    }
                    default: {
                        Buffer src = borrow(c, Category::Lead, r.next()); src.resize(n);
                        b = src;
                        double pos = 0;
                        for (int i = 0; i < n; ++i) { double t = (double)i / n; double rate = std::pow(2.0, std::sin(t * kTwoPi * r.range(1, 3)) * 1.5);
                            int p = (int)pos; float fr = (float)(pos - p);
                            for (int ch = 0; ch < b.numChannels(); ++ch) { const auto& sc = src.ch[(size_t)ch]; float a0 = sc[(size_t)(p % src.length())], a1 = sc[(size_t)((p + 1) % src.length())]; b.ch[(size_t)ch][(size_t)i] = a0 + (a1 - a0) * fr; }
                            pos += rate; }
                        break;
                    }
                }
                fx::bitcrush(b, r.irange(6, 12), r.irange(1, 4), 0.3f + f.lofi * 0.5f);
                b.makeStereo();
                break;
            }
            case Category::Noise: {
                std::vector<double> w = { f.lofi * 1.2 + f.vin * 0.5, 0.4 + f.vin * 0.5, 0.3 + f.exp * 0.4, f.atmo * f.dark * 1.5 + 0.1, 0.3 + f.ind * 0.6, 0.4 };
                static const char* names[] = { "Vinyl Crackle", "Tape Hiss", "Radio Static", "Rain", "Electrical Hum", "Coloured Noise" };
                size_t rec = pickRecipe(c, r, w);
                g.recipe = names[rec]; g.isLoop = true; g.bpm = (float)dna.bpm;
                double len = bar * 2; if (len > 8) len = bar;
                int n = (int)(len * sr);
                b = Buffer(1, n, sr);
                switch (rec) {
                    case 0: fx::addNoise(b, -42, 1, r.next(), true); fx::bandpass(b, 2500, 0.4); fx::gain(b, 2.0f); break;
                    case 1: fx::addNoise(b, -30, 0, r.next()); fx::highpass(b, 3000, 0.5); fx::tapeWow(b, 0.3f, 0.5f, r.next()); break;
                    case 2: { fx::addNoise(b, -20, 0, r.next(), true); double ph = 0; for (int i = 0; i < n; ++i) { ph += 3.7 / sr; b.ch[0][(size_t)i] *= 0.5f + 0.5f * (float)std::sin(kTwoPi * ph) * (float)std::sin(kTwoPi * ph * 0.31); } fx::bandpass(b, 1800, 1.0); break; }
                    case 3: { fx::addNoise(b, -30, 1, r.next()); fx::lowpass(b, 6000, 0.6); for (int k = 0; k < (int)(len * 60); ++k) { Buffer drop = noiseBurst(sr, 0.03, 0.0005, 0.01, 0, 2000, 12000, r.next(), r.range(2000, 7000), 4); fx::mixInto(b, drop, r.irange(0, n - 1), (float)r.range(0.05, 0.25)); } b.resize(n); break; }
                    case 4: { double f0 = r.chance(0.5) ? 50 : 60; for (int i = 0; i < n; ++i) { double t = i / sr; double s = 0; for (int h = 1; h < 12; h += 1) s += std::sin(kTwoPi * f0 * h * t) / h * (h % 2 ? 1.0 : 0.5); b.ch[0][(size_t)i] = (float)(std::tanh(s * 2.0) * 0.3); } fx::addNoise(b, -40, 0, r.next(), true); break; }
                    default: fx::addNoise(b, -22, r.irange(0, 2), r.next()); fx::sweep(b, 0, 8000 * f.cut(), 1500, 1.5, 1.0); break;
                }
                b.makeStereo();
                fx::widen(b, 0.5f);
                break;
            }
            case Category::Atmosphere: {
                std::vector<double> w = { 0.5 + f.atmo * 0.5, 0.3 + f.dark * 0.5, 0.3 + f.mel * 0.5, 0.3 + f.ind * 0.6, 0.3 + f.lofi * 0.5 };
                static const char* names[] = { "Cloud", "Abyss", "Choir Haze", "Machine Room", "Memory" };
                size_t rec = pickRecipe(c, r, w);
                g.recipe = names[rec];
                double len = 7.0;
                Category srcCat = rec == 2 ? Category::VocalTexture : rec == 3 ? Category::Texture : Category::Pad;
                Buffer src = rec == 3 ? metalCluster(sr, 3.0, 0.4, 3.0, r.next(), 9, true) : borrow(c, srcCat, r.next());
                fx::GrainParams gp; gp.grainMs = r.range(120, 400); gp.density = r.range(10, 30); gp.position = 0.5; gp.positionJitter = 0.45;
                gp.pitchSet = rec == 1 ? std::vector<double>{ -12, -24, -12, 0 } : std::vector<double>{ 0, 0, 12, -12, 7 };
                gp.reverseProb = 0.4;
                b = fx::granulate(src, len, gp, r.next());
                if (rec == 1) fx::lowpass(b, 1200, 0.8);
                if (rec == 4) { fx::tapeWow(b, 0.8f, 0.4f, r.next()); fx::addNoise(b, -45, 1, r.next(), true); fx::lowpass(b, 5000, 0.6); }
                fx::fadeIn(b, 1.0);
                fx::reverb(b, 0.95f, dna.roomDamp, 0.6f, 1.0f, 40, 2.0);
                b.resize((int)(len * sr));
                fx::fadeOut(b, 1.0);
                break;
            }
            case Category::Texture: {
                std::vector<double> w = { 0.5, 0.3 + f.ind * 0.8, 0.3 + f.dark * 0.4 + f.atmo * 0.2, 0.3 + f.exp * 0.6, 0.3 + f.mel * 0.4 };
                static const char* names[] = { "Resonator", "Metallic", "Underwater", "Grain Swarm", "Tonal Wash" };
                size_t rec = pickRecipe(c, r, w);
                g.recipe = names[rec];
                double len = 4.0;
                int n = (int)(len * sr);
                switch (rec) {
                    case 0: {
                        Buffer src = noiseBurst(sr, len, 0.5, 10, 1, 20, 20000, r.next());
                        b = Buffer(1, n, sr);
                        auto ch = chord(dna, 0, 3, 4);
                        for (int nn : ch) { Buffer x = noiseBurst(sr, len, 0.5, 10, 1, 20, 20000, r.next()); fx::bandpass(x, midiToHz(nn), 60); fx::bandpass(x, midiToHz(nn), 30); fx::mixInto(b, x, 0, 1.0f); }
                        (void)src;
                        fx::normalize(b, -6);
                        break;
                    }
                    case 1: b = metalCluster(sr, len, r.range(0.2, 0.7), 6.0, r.next(), 9, true); fx::ringMod(b, r.range(30, 200), 0.4f); fx::lowpass(b, 6000, 0.7); break;
                    case 2: { Buffer src = borrow(c, Category::Keys, r.next()); src.resize(n); fx::lowpass(src, 700, 1.5); fx::chorus(src, 6, 0.3f, 0.8f); b = src; break; }
                    case 3: { Buffer src = borrow(c, Category::Perc, r.next()); fx::GrainParams gp; gp.grainMs = 20; gp.density = 120; gp.position = 0.05; gp.positionJitter = 0.05; gp.pitchJitter = 7; b = fx::granulate(src, len, gp, r.next()); break; }
                    default: { Buffer src = borrow(c, Category::Bell, r.next()); fx::GrainParams gp; gp.grainMs = 250; gp.density = 18; gp.position = 0.3; gp.positionJitter = 0.3; gp.pitchSet = { 0, 12, 7 }; b = fx::granulate(src, len, gp, r.next()); break; }
                }
                b.makeStereo();
                fx::fadeIn(b, 0.3);
                kitRoom(b, dna, 1.5f, 2.0);
                b.resize(n);
                fx::fadeOut(b, 0.5);
                break;
            }
            default: { // Reverse
                std::vector<double> w = { 0.6, 0.5 + f.atmo * 0.4, 0.3 + f.mel * 0.5, 0.3 + f.atmo * 0.3, 0.3 };
                static const char* names[] = { "Rev Cymbal", "Rev Snare Tail", "Rev Piano", "Rev Vocal", "Rev Pad" };
                Category src[] = { Category::Crash, Category::Snare, Category::Piano, Category::VocalTexture, Category::Pad };
                size_t rec = pickRecipe(c, r, w);
                g.recipe = names[rec];
                b = borrow(c, src[rec], r.next());
                if (rec == 1) fx::reverb(b, 0.9f, 0.5f, 0.8f, 1.0f, 10, 2.0);
                fx::reverse(b);
                fx::trimLead(b, -55);
                b.makeStereo();
                break;
            }
        }
        g.audio = std::move(b);
        return g;
    }
};

} // namespace

EnginePtr makeFxEngine() { return std::make_shared<FxEngine>(); }

} // namespace sdna
