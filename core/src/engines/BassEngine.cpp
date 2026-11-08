// Bass: 808s, sub, one-shots, distorted and synth basses. All tuned to the kit key.
#include "Synth.h"

namespace sdna {
using namespace synth;

namespace {

class BassEngine : public SoundEngine {
public:
    std::string id() const override { return "procedural.bass"; }
    std::string description() const override { return "Tuned 808, sub, subtractive, FM, wavefold and Reese bass synthesis"; }
    bool handles(Category c) const override {
        return c == Category::Bass808 || c == Category::SubBass || c == Category::BassShot || c == Category::DistBass || c == Category::SynthBass;
    }

    GenResult generate(Category cat, const GenContext& c) const override {
        Rng r(c.seed);
        double sr = c.sampleRate;
        float dark = pv(c, r, Desc::Dark), dist = pv(c, r, Desc::Distorted), heavy = pv(c, r, Desc::Heavy), aggr = pv(c, r, Desc::Aggressive);
        float vin = pv(c, r, Desc::Vintage), exp_ = pv(c, r, Desc::Experimental), hyp = pv(c, r, Desc::Hyper), ind = pv(c, r, Desc::Industrial), energy = pv(c, r, Desc::Energy);
        // Root: kit key, around G1–E2 for 808/sub, an octave up for mid basses.
        static const int degs[] = { 0, 0, 4, 3, 5, 0, 6, 2 };
        int deg = c.variant < 2 ? 0 : degs[c.variant % 8];
        int base = c.dna.noteInKey(deg, 1);
        while (base < 30) base += 12;
        while (base > 41) base -= 12;
        GenResult g; g.rootNote = base;
        double f = midiToHz(base);
        double cutMul = std::pow(2.0, (0.45 - dark) * 1.5);
        Buffer b;

        switch (cat) {
            case Category::Bass808: {
                std::vector<double> w = { 0.6 + (1 - dist) * 0.6, dist * 1.4 + aggr * 0.3, 0.4 + energy * 0.4, hyp * 1.2 + exp_ * 0.4, heavy * 0.8 };
                static const char* names[] = { "Clean", "Distorted", "Glide", "Pitch Dive", "Long" };
                size_t rec = pickRecipe(c, r, w);
                g.recipe = names[rec];
                PitchedDrum pd; pd.f0 = f; pd.startMul = std::pow(2.0, r.range(7, 12) / 12.0); pd.pitchDecay = r.range(0.03, 0.07);
                pd.ampT60 = (rec == 4 ? 3.0 : 1.4 + heavy * 1.2) * r.range(0.8, 1.1); pd.length = std::min(3.5, pd.ampT60 * 0.75 + 0.2);
                pd.harmonicMix = 0.05; pd.clickAmt = 0.15 + aggr * 0.2; pd.clickLP = 3000; pd.attackMs = 1;
                if (rec == 2) { pd.startMul = std::pow(2.0, r.chance(0.5) ? 12.0 / 12.0 : 7.0 / 12.0); pd.pitchDecay = r.range(0.08, 0.18); }
                if (rec == 3) { pd.startMul = std::pow(2.0, 24.0 / 12.0); pd.pitchDecay = 0.12; }
                b = pitchedDrum(sr, pd, r.next());
                if (rec == 1) { fx::saturate(b, c.dna.satType == SatType::Bitcrush ? SatType::Hard : c.dna.satType, 0.55f + dist * 0.45f); fx::lowpass(b, 2500 * cutMul, 0.7); }
                else fx::saturate(b, SatType::Soft, 0.15f + dist * 0.25f);
                fx::fadeOut(b, 0.05);
                break;
            }
            case Category::SubBass: {
                std::vector<double> w = { 0.8, 0.5 + vin * 0.4, 0.3 + dist * 0.6, 0.4 };
                static const char* names[] = { "Pure Sine", "Triangle", "Warm", "Wobble" };
                size_t rec = pickRecipe(c, r, w);
                g.recipe = names[rec];
                double len = r.range(1.2, 2.0);
                b = Buffer(1, (int)(len * sr), sr);
                ADSR env; env.a = 0.006; env.d = 0.2; env.s = 0.85; env.r = 0.15;
                Osc o; double wph = 0;
                double wobRate = c.dna.bpm / 60.0 * (r.chance(0.5) ? 2.0 : 1.0);
                for (int i = 0; i < b.length(); ++i) {
                    double t = i / sr;
                    float s = rec == 1 ? o.tri(f / sr) : o.sine(f / sr);
                    if (rec == 2) s = std::tanh(s * 1.8f) * 0.8f;
                    float am = 1.0f;
                    if (rec == 3) { wph += wobRate / sr; am = 0.55f + 0.45f * (float)std::cos(kTwoPi * wph); }
                    b.ch[0][(size_t)i] = s * env.at(t, len - 0.15) * am;
                }
                if (rec == 1) fx::lowpass(b, 400, 0.7);
                break;
            }
            case Category::BassShot: {
                std::vector<double> w = { 0.6 + vin * 0.4, 0.4 + (1 - dark) * 0.3, vin * 0.6 + exp_ * 0.4 + (hasGroove(c.profile, Groove::Breakbeat) ? 0.3 : 0.0), 0.4 + ind * 0.3, 0.3 };
                static const char* names[] = { "Saw Pluck", "Square", "Acid", "Stab", "Rubber" };
                size_t rec = pickRecipe(c, r, w);
                g.recipe = names[rec];
                int note = base + 12; g.rootNote = note;
                double hz = midiToHz(note);
                ADSR env; env.a = 0.002; env.d = r.range(0.15, 0.4); env.s = rec == 3 ? 0.0 : 0.3; env.r = 0.1;
                double gate = r.range(0.25, 0.5);
                switch (rec) {
                    case 0: b = subtractive(sr, 0.9, hz, 0, 3500 * cutMul, 180, 0.12, 0.9, env, gate); break;
                    case 1: b = subtractive(sr, 0.9, hz, 1, 2500 * cutMul, 250, 0.15, 0.8, env, gate); break;
                    case 2: b = subtractive(sr, 0.9, hz, 0, 4000 * cutMul, 200, 0.09, 8.0 + r.range(0, 6), env, gate, r.chance(0.5) ? hz * 0.5 : 0, 0.04);
                        fx::saturate(b, SatType::Tube, 0.3f + dist * 0.4f); break;
                    case 3: b = subtractive(sr, 0.6, hz, 3, 5000 * cutMul, 300, 0.05, 1.5, env, 0.12); break;
                    default: { b = fm2(sr, 0.8, hz, 1.0, 3.0, 0.08, 0.35, 0.002, 0.3); break; }
                }
                Buffer sub = Buffer(1, b.length(), sr);
                Osc o; for (int i = 0; i < sub.length(); ++i) sub.ch[0][(size_t)i] = o.sine(hz * 0.5 / sr) * env.at(i / sr, gate) * 0.5f;
                fx::mixInto(b, sub, 0, heavy);
                break;
            }
            case Category::DistBass: {
                std::vector<double> w = { 0.5 + exp_ * 0.5, 0.5 + dist * 0.5, 0.4 + vin * 0.4, 0.3 + aggr * 0.6 + (hasGroove(c.profile, Groove::HalfTime) ? 0.5 : 0.0), ind * 1.4 };
                static const char* names[] = { "Growl", "Fold", "Fuzz", "Riddim", "Industrial" };
                size_t rec = pickRecipe(c, r, w);
                g.recipe = names[rec];
                int note = base + 12; g.rootNote = note;
                double hz = midiToHz(note);
                double len = r.range(0.9, 1.6);
                b = Buffer(1, (int)(len * sr), sr);
                Osc o1, o2(0.3), sub; double mph = 0, lfo = 0;
                double lfoRate = c.dna.bpm / 60.0 * (double)(1 << r.irange(1, 3));
                SVF fl; SVF form;
                ADSR env; env.a = 0.004; env.d = 0.2; env.s = 0.9; env.r = 0.1;
                for (int i = 0; i < b.length(); ++i) {
                    double t = i / sr;
                    lfo += lfoRate / sr;
                    float l = 0.5f + 0.5f * (float)std::sin(kTwoPi * lfo);
                    float s = 0;
                    switch (rec) {
                        case 0: { mph += hz * 2.0 / sr; double I = 2.0 + 6.0 * l; o1.phase += hz / sr; o1.phase -= std::floor(o1.phase);
                            s = (float)std::sin(kTwoPi * o1.phase + I * std::sin(kTwoPi * mph)); break; }
                        case 1: s = satFold(o1.saw(hz / sr) * (1.5f + 3.0f * l)); break;
                        case 2: s = std::tanh((o1.square(hz / sr) + o2.saw(hz * 1.01 / sr)) * 4.0f); break;
                        case 3: s = o1.square(hz / sr, 0.25 + 0.2 * l); break;
                        default: s = std::tanh(o1.saw(hz / sr) * 3.0f) * (float)std::sin(kTwoPi * mph); mph += hz * 1.414 / sr; break;
                    }
                    if ((i & 15) == 0) { fl.set(sr, (300 + 3500 * l) * cutMul, rec == 3 ? 4.0 : 1.2); form.set(sr, 500 + 1500 * l, 5); }
                    float y = fl.lp(s);
                    if (rec == 0 || rec == 3) y = y * 0.7f + form.bp(s) * 0.6f;
                    y += sub.sine(hz * 0.5 / sr) * 0.45f;
                    b.ch[0][(size_t)i] = std::tanh(y * (1.5f + dist * 2.0f)) * env.at(t, len - 0.1);
                }
                break;
            }
            default: { // SynthBass
                std::vector<double> w = { 0.5 + (hasGroove(c.profile, Groove::DnB) ? 1.2 : 0.0) + dark * 0.3, 0.5 + vin * 0.6, 0.4 + exp_ * 0.3 + hyp * 0.4, 0.3 + energy * 0.5, vin * 0.8 + (hasGroove(c.profile, Groove::BoomBap) ? 0.4 : 0.0) };
                static const char* names[] = { "Reese", "Analog", "FM", "Wobble", "Portamento" };
                size_t rec = pickRecipe(c, r, w);
                g.recipe = names[rec];
                int note = base + 12; g.rootNote = note;
                double hz = midiToHz(note);
                double len = r.range(1.0, 1.8);
                ADSR env; env.a = 0.005; env.d = 0.3; env.s = 0.8; env.r = 0.15;
                switch (rec) {
                    case 0: {
                        b = supersaw(sr, len, { hz, hz * 0.5 }, r.range(10, 25), 3, env, len - 0.15, r.next(), true);
                        fx::lowpass(b, 900 * cutMul, 1.0);
                        fx::saturate(b, SatType::Soft, 0.3f + dist * 0.3f);
                        break;
                    }
                    case 1: b = subtractive(sr, len, hz, 3, 2500 * cutMul, 450 * cutMul, 0.25, 2.5, env, len - 0.15); break;
                    case 2: b = fm2(sr, len, hz, r.chance(0.5) ? 1.0 : 2.0, r.range(1.5, 4.0), 0.4, 3.0, 0.004, 0.2); break;
                    case 3: {
                        b = subtractive(sr, len, hz, 0, 800, 800, 1, 3.0, env, len - 0.15);
                        Buffer src = b; b.ch[0].assign(b.ch[0].size(), 0.0f);
                        SVF fl; double ph = 0; double rate = c.dna.bpm / 60.0 * 2.0;
                        Osc o;
                        for (int i = 0; i < b.length(); ++i) { ph += rate / sr; if ((i & 15) == 0) fl.set(sr, (200 + 2500 * (0.5 + 0.5 * std::sin(kTwoPi * ph))) * cutMul, 4); b.ch[0][(size_t)i] = fl.lp(o.saw(hz / sr)) * env.at(i / sr, len - 0.15); }
                        break;
                    }
                    default: b = subtractive(sr, len, hz, 0, 1800 * cutMul, 600, 0.3, 1.5, env, len - 0.15, hz * std::pow(2.0, r.chance(0.5) ? 1.0 : -1.0), 0.08); break;
                }
                break;
            }
        }
        fx::highpass(b, 20);
        g.audio = std::move(b);
        return g;
    }
};

} // namespace

EnginePtr makeBassEngine() { return std::make_shared<BassEngine>(); }

} // namespace sdna
