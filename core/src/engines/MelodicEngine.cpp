// Melodic instruments. Everything is in the kit's key/scale; chordal material
// follows the kit's progression so that keys, pads, strings and arps agree.
#include "Synth.h"

namespace sdna {
using namespace synth;

namespace {

struct M {
    float dark, dist, elec, lofi, aggr, atmo, ind, vin, heavy, energy, mel, exp, hyp, glitch;
    M(const GenContext& c, Rng& r) {
        dark = pv(c, r, Desc::Dark); dist = pv(c, r, Desc::Distorted); elec = pv(c, r, Desc::Electronic); lofi = pv(c, r, Desc::Lofi);
        aggr = pv(c, r, Desc::Aggressive); atmo = pv(c, r, Desc::Atmospheric); ind = pv(c, r, Desc::Industrial); vin = pv(c, r, Desc::Vintage);
        heavy = pv(c, r, Desc::Heavy); energy = pv(c, r, Desc::Energy); mel = pv(c, r, Desc::Melodic); exp = pv(c, r, Desc::Experimental);
        hyp = pv(c, r, Desc::Hyper); glitch = pv(c, r, Desc::Glitch);
    }
    double cut() const { return std::pow(2.0, (0.45 - dark) * 1.6); }
};

std::vector<double> hz(const std::vector<int>& notes) { std::vector<double> v; for (int n : notes) v.push_back(midiToHz(n)); return v; }

Buffer pluckVoice(const GenContext& c, Rng& r, const M& m, int recipe, double f, double len) {
    double sr = c.sampleRate;
    ADSR env; env.a = 0.001; env.d = r.range(0.15, 0.4); env.s = 0.0; env.r = 0.2;
    switch (recipe) {
        case 0: return karplus(sr, len, f, r.range(0.8, 2.5), 0.4 + (1 - m.dark) * 0.5, r.next());
        case 1: return subtractive(sr, len, f, 0, 6000 * m.cut(), 300, r.range(0.05, 0.12), 1.4 + m.exp * 3, env, 0.05);
        case 2: return fm2(sr, len, f, r.chance(0.5) ? 2.0 : 3.0, r.range(2, 5), 0.08, r.range(0.4, 1.0), 0.001);
        case 3: { ADSR e2 = env; e2.d = 0.12; e2.s = 0.2; return subtractive(sr, len, f, 1, 20000, 20000, 1, 0.5, e2, 0.12); }
        default: return additive(sr, len, f, { 1, 3.99, 9.9, 0.5 }, { 1, 0.4, 0.12, 0.2 }, { 0.5, 0.15, 0.05, 0.6 }, 0.001, r.next());
    }
}

class MelodicEngine : public SoundEngine {
public:
    std::string id() const override { return "procedural.melodic"; }
    std::string description() const override { return "Supersaw, subtractive, FM, physical-model (Karplus-Strong), additive piano & organ synthesis"; }
    bool handles(Category c) const override {
        return c == Category::Lead || c == Category::Pluck || c == Category::Keys || c == Category::Piano || c == Category::Bell
            || c == Category::Strings || c == Category::Pad || c == Category::Arp;
    }

    GenResult generate(Category cat, const GenContext& c) const override {
        Rng r(c.seed);
        M m(c, r);
        double sr = c.sampleRate;
        const KitDNA& dna = c.dna;
        int degree = dna.progression[(size_t)c.variant % dna.progression.size()];
        GenResult g;
        Buffer b;
        switch (cat) {
            case Category::Lead: {
                std::vector<double> w = { 0.3 + m.energy * 0.6 + (hasGroove(c.profile, Groove::Hardstyle) ? 1.2 : 0.0) + m.hyp * 0.4,
                    m.lofi * 0.8 + m.vin * 0.4 + (m.elec > 0.8f ? 0.4 : 0.0), 0.3 + m.exp * 0.7, m.vin * 0.7 + (hasGroove(c.profile, Groove::BoomBap) ? 0.4 : 0.0),
                    m.dist * m.aggr * 1.6, m.hyp * 1.6, 0.2 + m.exp * 0.4 + m.vin * 0.3 };
                static const char* names[] = { "Supersaw", "Chip Square", "FM", "Whine", "Screech", "Glass", "Acid" };
                size_t rec = pickRecipe(c, r, w);
                g.recipe = names[rec];
                int note = dna.noteInKey(c.variant % 3 == 2 ? 4 : 0, 4); g.rootNote = note;
                double f = midiToHz(note);
                double len = r.range(1.2, 2.0);
                ADSR env; env.a = 0.01; env.d = 0.3; env.s = 0.75; env.r = 0.25;
                switch (rec) {
                    case 0: b = supersaw(sr, len, { f, f * 2 }, r.range(15, 35), 7, env, len - 0.25, r.next(), true); fx::lowpass(b, 7000 * m.cut(), 0.8); break;
                    case 1: { b = subtractive(sr, len, f, 1, 20000, 20000, 1, 0.5, env, len - 0.25); fx::bitcrush(b, 6, 1, 0.6f); break; }
                    case 2: b = fm2(sr, len, f, r.chance(0.5) ? 1.0 : 2.0, r.range(1.0, 3.5), 1.5, 6.0, 0.01, r.range(0.0, 0.6)); break;
                    case 3: {
                        b = Buffer(1, (int)(len * sr), sr);
                        Osc o; double ph = 0; double from = f * std::pow(2.0, -r.irange(2, 5) / 12.0);
                        for (int i = 0; i < b.length(); ++i) { double t = i / sr; ph += 5.5 / sr;
                            double fr = f * std::pow(from / f, std::exp(-t / 0.08)) * std::pow(2.0, 0.25 * std::sin(kTwoPi * ph) * std::min(1.0, t / 0.4) / 12.0);
                            b.ch[0][(size_t)i] = (0.6f * o.sine(fr / sr) + 0.15f * (float)std::sin(2 * kTwoPi * o.phase)) * env.at(t, len - 0.25); }
                        break;
                    }
                    case 4: b = supersaw(sr, len, { f }, 20, 5, env, len - 0.25, r.next(), true); fx::saturate(b, SatType::Hard, 0.85f); fx::bandpass(b, 1800, 0.5); break;
                    case 5: { b = fm2(sr, len, f * 2, 3.0, 2.0, 0.3, 2.5, 0.005); Buffer s = supersaw(sr, len, { f * 2 }, 8, 3, env, len - 0.25, r.next(), true); fx::mixInto(b, s, 0, 0.4f); break; }
                    default: b = subtractive(sr, len, f, 0, 5000 * m.cut(), 400, 0.15, 10, env, len - 0.25); fx::saturate(b, SatType::Tube, 0.4f); break;
                }
                if (b.numChannels() == 1) b.makeStereo();
                fx::delay(b, 60.0 / dna.bpm * 0.75, 0.3f, 0.18f + m.atmo * 0.15f, true, 4000, 0.8);
                kitRoom(b, dna, 0.8f, 1.2);
                break;
            }
            case Category::Pluck: {
                std::vector<double> w = { 0.3 + (1 - m.elec) * 1.0, 0.5 + m.elec * 0.5, 0.3 + m.exp * 0.5 + m.hyp * 0.4, m.lofi * 0.8 + m.vin * 0.3, (1 - m.elec) * 0.4 + m.mel * 0.4 };
                static const char* names[] = { "String", "Saw", "FM", "Chip", "Mallet" };
                size_t rec = pickRecipe(c, r, w);
                g.recipe = names[rec];
                int note = dna.noteInKey(c.variant % 4 == 3 ? 2 : 0, c.variant % 2 ? 5 : 4); g.rootNote = note;
                b = pluckVoice(c, r, m, (int)rec, midiToHz(note), 1.4);
                b.makeStereo();
                fx::delay(b, 60.0 / dna.bpm * 0.5, 0.25f, 0.15f, true, 5000, 0.6);
                kitRoom(b, dna, 0.8f, 1.0);
                break;
            }
            case Category::Keys: {
                std::vector<double> w = { 0.5 + m.mel * 0.5 + m.vin * 0.3, m.vin * 0.6 + m.dist * 0.3, 0.2 + m.vin * 0.4 + (1 - m.elec) * 0.3, 0.2 + m.energy * 0.3 + (hasGroove(c.profile, Groove::RnB) ? 0.3 : 0.0), 0.2 + m.elec * 0.5 + m.energy * 0.3 };
                static const char* names[] = { "Rhodes", "Wurli", "Organ", "Clav", "Synth Stab" };
                size_t rec = pickRecipe(c, r, w);
                auto notes = chord(dna, degree, 3, r.chance(0.5 + m.mel * 0.4) ? 4 : 3, r.chance(0.4));
                g.recipe = std::string(names[rec]) + " " + chordName(dna, degree);
                g.rootNote = notes[0];
                double len = 2.0;
                switch (rec) {
                    case 0: b = electricPiano(sr, len, notes, 0.1, r.next()); break;
                    case 1: b = electricPiano(sr, len, notes, 0.7, r.next()); break;
                    case 2: b = organ(sr, 1.6, notes, { 0.8, 0.0, 0.8, 0.6, 0.2, 0.3, 0.0, 0.1, 0.2 }, r.range(0.3, 1.0)); break;
                    case 3: { b = Buffer(1, (int)(0.6 * sr), sr); for (int n : notes) { ADSR e; e.a = 0.001; e.d = 0.15; e.s = 0.2; e.r = 0.05; Buffer x = subtractive(sr, 0.6, midiToHz(n), 1, 6000, 1200, 0.06, 2.0, e, 0.25); fx::mixInto(b, x, 0, 0.5f); } break; }
                    default: { ADSR e; e.a = 0.003; e.d = 0.3; e.s = 0.0; e.r = 0.2; b = supersaw(sr, 1.2, hz(notes), 18, 5, e, 0.3, r.next(), true); fx::lowpass(b, 6000 * m.cut(), 0.8); break; }
                }
                b.makeStereo();
                fx::chorus(b, 1.5f, 0.6f, 0.25f);
                kitRoom(b, dna, 1.0f, 1.2);
                break;
            }
            case Category::Piano: {
                std::vector<double> w = { 0.5 + (1 - m.lofi) * 0.4, (1 - m.aggr) * 0.8 + m.atmo * 0.3, m.lofi * 1.2 + m.vin * 0.4, m.exp * 1.2 + m.ind * 0.3, m.hyp * 0.8 + m.exp * 0.3 };
                static const char* names[] = { "Grand", "Felt", "Upright", "Prepared", "Toy" };
                size_t rec = pickRecipe(c, r, w);
                bool chordal = c.variant % 2 == 1;
                std::vector<int> notes = chordal ? chord(dna, degree, 3, 4) : std::vector<int>{ dna.noteInKey(c.variant % 4 == 2 ? 4 : 0, 4) };
                g.recipe = std::string(names[rec]) + (chordal ? " " + chordName(dna, degree) : "");
                g.rootNote = notes[0];
                double bright = rec == 1 ? 0.1 : rec == 4 ? 0.95 : 0.55 - m.dark * 0.3;
                double inh = rec == 3 ? 40.0 : rec == 4 ? 15.0 : 1.0;
                b = piano(sr, rec == 4 ? 1.5 : 3.0, notes, bright, inh, rec == 1 ? 0.6 : 0.3, r.next());
                if (rec == 1) { fx::lowpass(b, 1800, 0.6); Buffer mech = noiseBurst(sr, 0.08, 0.002, 0.03, 2, 100, 900, r.next()); fx::mixInto(b, mech, 0, 0.15f); }
                if (rec == 2) { fx::lowpass(b, 4500, 0.7); fx::tapeWow(b, 0.5f, 0.7f, r.next()); }
                if (rec == 3) { fx::ringMod(b, midiToHz(notes[0]) * 2.71, 0.2f); }
                b.makeStereo();
                kitRoom(b, dna, 1.0f, 1.5);
                break;
            }
            case Category::Bell: {
                std::vector<double> w = { 0.5 + m.elec * 0.4, 0.3 + (1 - m.elec) * 0.4 + m.dark * 0.3, 0.3 + m.mel * 0.4 + m.hyp * 0.3, m.hyp * 0.8 + m.atmo * 0.3, m.exp * 0.8 + m.ind * 0.4 };
                static const char* names[] = { "FM Bell", "Tubular", "Music Box", "Glass", "Gamelan" };
                size_t rec = pickRecipe(c, r, w);
                g.recipe = names[rec];
                int note = dna.noteInKey(c.variant % 3 == 1 ? 4 : c.variant % 3 == 2 ? 2 : 0, 5); g.rootNote = note;
                double f = midiToHz(note);
                switch (rec) {
                    case 0: b = fm2(sr, 3.0, f, 3.5, r.range(2.0, 4.0), 1.2, 2.8, 0.001); break;
                    case 1: b = additive(sr, 3.5, f * 0.5, { 1, 2, 3, 4.2, 5.4, 6.8 }, { 0.6, 1.0, 0.8, 0.5, 0.3, 0.2 }, { 3.5, 3.0, 2.5, 1.8, 1.2, 0.9 }, 0.001, r.next()); break;
                    case 2: b = additive(sr, 2.0, f, { 1, 3.0, 5.2, 7.9 }, { 1, 0.3, 0.12, 0.05 }, { 1.6, 0.6, 0.3, 0.15 }, 0.0005, r.next()); break;
                    case 3: b = fm2(sr, 2.5, f * 2, 1.0, 0.8, 0.2, 1.8, 0.0008); break;
                    default: b = additive(sr, 3.0, f, { 1, 1.52, 2.43, 3.97, 5.1 }, { 1, 0.6, 0.5, 0.25, 0.15 }, { 2.5, 2.0, 1.4, 0.9, 0.6 }, 0.001, r.next()); break;
                }
                b.makeStereo();
                kitRoom(b, dna, 1.2f, 2.0);
                break;
            }
            case Category::Strings: {
                std::vector<double> w = { 0.6 + m.mel * 0.4, 0.3 + m.energy * 0.5, 0.2 + m.dark * 0.4 + m.exp * 0.3, m.dark * 0.6 + m.heavy * 0.3, m.vin * 0.9 + m.lofi * 0.3 };
                static const char* names[] = { "Ensemble", "Staccato", "Tremolo", "Low Cello", "String Machine" };
                size_t rec = pickRecipe(c, r, w);
                auto notes = chord(dna, degree, rec == 3 ? 2 : 3, 3, true);
                g.recipe = std::string(names[rec]) + " " + chordName(dna, degree);
                g.rootNote = notes[0];
                ADSR env; env.a = rec == 1 ? 0.015 : r.range(0.25, 0.7); env.d = 0.5; env.s = 0.85; env.r = rec == 1 ? 0.15 : 0.8;
                double len = rec == 1 ? 0.6 : 4.0;
                double gate = rec == 1 ? 0.25 : 3.0;
                b = supersaw(sr, len, hz(notes), rec == 4 ? 20 : 10, 5, env, gate, r.next(), true);
                // vibrato-ish movement through chorus, formant-ish body through bandpass
                fx::lowpass(b, (rec == 3 ? 1800 : 4500) * m.cut(), 0.6);
                fx::highpass(b, rec == 3 ? 50 : 150);
                if (rec == 2) { double ph = 0, rate = r.range(7, 11); for (int i = 0; i < b.length(); ++i) { ph += rate / sr; float am = 0.6f + 0.4f * (float)std::sin(kTwoPi * ph); b.ch[0][(size_t)i] *= am; b.ch[1][(size_t)i] *= am; } }
                fx::chorus(b, rec == 4 ? 4.0f : 2.5f, rec == 4 ? 0.8f : 0.4f, 0.5f);
                kitRoom(b, dna, 1.3f, 2.0, 0.1f);
                break;
            }
            case Category::Pad: {
                std::vector<double> w = { 0.5 + m.vin * 0.4 + m.mel * 0.2, 0.2 + m.elec * 0.4 + m.hyp * 0.4, 0.3 + m.atmo * 0.5 + m.dark * 0.2, m.dark * 0.9 + m.ind * 0.3, m.glitch * 0.5 + m.exp * 0.6 + m.atmo * 0.3, m.atmo * 0.6 + m.hyp * 0.3 + m.mel * 0.2 };
                static const char* names[] = { "Warm", "Glass", "Choir", "Dark Drone", "Granular", "Shimmer" };
                size_t rec = pickRecipe(c, r, w);
                auto notes = chord(dna, degree, 3, 4, rec != 3);
                g.recipe = std::string(names[rec]) + " " + chordName(dna, degree);
                g.rootNote = notes[0];
                double len = 5.0;
                ADSR env; env.a = r.range(0.6, 1.5); env.d = 1.0; env.s = 0.9; env.r = 1.2;
                switch (rec) {
                    case 0: b = supersaw(sr, len, hz(notes), 14, 5, env, 3.6, r.next(), true); fx::sweep(b, 0, 600 * m.cut(), 3000 * m.cut(), 0.9, 0.7); break;
                    case 1: { b = Buffer(2, (int)(len * sr), sr); for (int n : notes) { Buffer x = fm2(sr, len, midiToHz(n), 2.0, 1.2, 3.0, 8.0, env.a); for (int i = 0; i < x.length(); ++i) x.ch[0][(size_t)i] *= env.at(i / sr, 3.6); fx::mixInto(b, x, 0, 0.35f); } fx::chorus(b, 2, 0.3f, 0.5f); break; }
                    case 2: {
                        b = Buffer(2, (int)(len * sr), sr);
                        for (size_t k = 0; k < notes.size(); ++k) {
                            VoiceParams vp; double fr = midiToHz(notes[k]); vp.pitchHz = [fr](double) { return fr; };
                            vp.vowels = { 0, 3, 4 }; double speed = r.range(0.2, 0.5); vp.vowelPos = [speed](double t) { return 1.0 + std::sin(t * speed) ; };
                            vp.breath = 0.25; vp.vibratoCents = 12; vp.env = env; vp.gate = 3.6; vp.formantShift = fr > 300 ? 1.15 : 1.0;
                            Buffer x = voice(sr, len, vp, r.next()); fx::mixInto(b, x, 0, 0.5f);
                        }
                        fx::chorus(b, 3, 0.5f, 0.6f);
                        break;
                    }
                    case 3: {
                        std::vector<int> drone = { notes[0] - 12, notes[0] - 5, notes[0] };
                        b = supersaw(sr, len, hz(drone), 8, 3, env, 3.6, r.next(), true);
                        fx::lowpass(b, 700 * m.cut(), 1.2);
                        fx::addNoise(b, -36, 2, r.next());
                        if (m.ind > 0.4f) fx::ringMod(b, midiToHz(notes[0]) * 1.41, 0.15f);
                        break;
                    }
                    case 4: {
                        Buffer src = supersaw(sr, 2.0, hz(notes), 10, 3, env, 1.5, r.next(), true);
                        fx::GrainParams gp; gp.grainMs = r.range(60, 160); gp.density = r.range(25, 60); gp.position = 0.6; gp.positionJitter = 0.3;
                        gp.pitchSet = { 0, 0, 12, 7, -12 }; gp.reverseProb = 0.3;
                        b = fx::granulate(src, len, gp, r.next());
                        fx::fadeIn(b, 0.8);
                        break;
                    }
                    default: {
                        b = supersaw(sr, len, hz(notes), 9, 3, env, 3.6, r.next(), true);
                        Buffer oct = b; fx::resample(oct, 2.0); fx::reverb(oct, 0.95f, 0.2f, 1.0f, 1.0f, 30, 2.0, true); oct.resize(b.length());
                        fx::mixInto(b, oct, 0, 0.5f);
                        break;
                    }
                }
                fx::fadeOut(b, 0.8);
                kitRoom(b, dna, 1.6f, 3.0, 0.15f);
                break;
            }
            default: { // Arp loop
                std::vector<double> w = { 0.3 + (1 - m.elec) * 0.6, 0.5 + m.elec * 0.4, 0.3 + m.exp * 0.4, m.lofi * 0.6 + m.vin * 0.4, 0.3 + m.hyp * 0.6 };
                static const char* pnames[] = { "String", "Saw", "FM", "Chip", "Mallet" };
                static const char* orders[] = { "Up", "Down", "UpDown", "Random", "Octaves" };
                size_t rec = pickRecipe(c, r, w);
                int order = r.irange(0, 4);
                g.recipe = std::string(pnames[rec]) + " " + orders[order];
                double bpm = dna.bpm;
                int bars = bpm < 100 ? 1 : 2;
                double sixteenth = 60.0 / bpm / 4.0;
                int steps = 16 * bars;
                double rate = (m.energy > 0.7f && bpm < 130) ? 1.0 : (m.energy < 0.3f ? 2.0 : 1.0); // in sixteenths
                b = Buffer(2, (int)((steps * sixteenth) * sr), sr);
                int stepsPerChord = steps / (int)std::min<size_t>(4, dna.progression.size());
                std::vector<Buffer> cache;
                for (int s = 0; s < steps; s += (int)rate) {
                    int deg = dna.progression[(size_t)(s / std::max(1, stepsPerChord)) % dna.progression.size()];
                    auto ch = chord(dna, deg, 4, 4);
                    ch.push_back(ch[0] + 12);
                    int idx = s / (int)rate;
                    int n;
                    switch (order) {
                        case 0: n = ch[(size_t)idx % ch.size()]; break;
                        case 1: n = ch[ch.size() - 1 - (size_t)idx % ch.size()]; break;
                        case 2: { int period = (int)ch.size() * 2 - 2; int k = idx % period; n = ch[(size_t)(k < (int)ch.size() ? k : period - k)]; break; }
                        case 3: n = ch[(size_t)r.irange(0, (int)ch.size() - 1)]; break;
                        default: n = ch[0] + 12 * (idx % 2); break;
                    }
                    Rng nr(c.seed + (uint64_t)n);
                    Buffer v = pluckVoice(c, nr, m, (int)rec, midiToHz(n), sixteenth * rate * 2.5);
                    fx::fadeOut(v, 0.01);
                    float vel = (s % 4 == 0) ? 1.0f : 0.7f + 0.2f * (float)r.uni();
                    fx::mixInto(b, v, (int)(s * sixteenth * sr), vel * 0.6f);
                }
                b.resize((int)(steps * sixteenth * sr));
                // Tempo-synced dotted-8th delay, then wrap the tail around so the loop is seamless.
                int loopLen = b.length();
                fx::delay(b, sixteenth * 3, 0.35f, 0.25f, true, 4500, 1.5);
                for (int ch = 0; ch < 2; ++ch) for (int i = loopLen; i < b.length(); ++i) b.ch[(size_t)ch][(size_t)((i - loopLen) % loopLen)] += b.ch[(size_t)ch][(size_t)i];
                b.resize(loopLen);
                g.isLoop = true; g.bpm = (float)bpm; g.rootNote = dna.noteInKey(0, 4);
                break;
            }
        }
        g.audio = std::move(b);
        return g;
    }
};

} // namespace

EnginePtr makeMelodicEngine() { return std::make_shared<MelodicEngine>(); }

} // namespace sdna
