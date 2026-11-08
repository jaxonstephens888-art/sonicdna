// Vocal elements via formant synthesis (glottal source -> vowel formant filters).
// These are *synthetic* voices: no words, no recordings, no real singer.
#include "Synth.h"

namespace sdna {
using namespace synth;

namespace {

class VocalEngine : public SoundEngine {
public:
    std::string id() const override { return "procedural.vocal"; }
    std::string description() const override { return "Formant (source-filter) voice synthesis with granular and spectral processing"; }
    bool handles(Category c) const override {
        return c == Category::VocalTexture || c == Category::VocalChop || c == Category::Chant || c == Category::FormantTexture || c == Category::VocalFx;
    }

    GenResult generate(Category cat, const GenContext& c) const override {
        Rng r(c.seed);
        double sr = c.sampleRate;
        const KitDNA& dna = c.dna;
        float dark = pv(c, r, Desc::Dark), hyp = pv(c, r, Desc::Hyper), atmo = pv(c, r, Desc::Atmospheric), lofi = pv(c, r, Desc::Lofi);
        float dist = pv(c, r, Desc::Distorted), glitch = pv(c, r, Desc::Glitch), exp_ = pv(c, r, Desc::Experimental), ind = pv(c, r, Desc::Industrial), energy = pv(c, r, Desc::Energy);
        double shift = 1.0 + (hyp - 0.3) * 0.35 - (dark - 0.5) * 0.12; // formant shift
        GenResult g;
        Buffer b;
        switch (cat) {
            case Category::VocalTexture: {
                std::vector<double> w = { 0.5 + atmo * 0.5, 0.3 + dark * 0.5, 0.3 + hyp * 0.6 };
                static const char* names[] = { "Choir Aah", "Hum", "Airy Ooh" };
                size_t rec = pickRecipe(c, r, w);
                int degree = dna.progression[(size_t)c.variant % dna.progression.size()];
                auto notes = chord(dna, degree, rec == 1 ? 2 : 3, 3, true);
                g.recipe = std::string(names[rec]) + " " + chordName(dna, degree);
                g.rootNote = notes[0];
                double len = 4.0;
                b = Buffer(2, (int)(len * sr), sr);
                int vowel = rec == 0 ? 0 : rec == 1 ? 4 : 3;
                for (size_t k = 0; k < notes.size(); ++k) for (int u = 0; u < 2; ++u) {
                    VoiceParams vp; double fr = midiToHz(notes[k]) * std::pow(2.0, r.bipolar() * 8 / 1200.0);
                    vp.pitchHz = [fr](double) { return fr; };
                    vp.vowels = { vowel, rec == 2 ? 4 : vowel }; vp.vowelPos = [](double t) { return std::min(1.0, t / 3.0); };
                    vp.breath = rec == 2 ? 0.5 : 0.2; vp.formantShift = shift * (fr > 300 ? 1.1 : 1.0); vp.vibratoCents = 15; vp.vibratoHz = r.range(4.5, 6);
                    vp.env.a = r.range(0.3, 0.8); vp.env.d = 0.5; vp.env.s = 0.9; vp.env.r = 0.8; vp.gate = 3.0;
                    Buffer x = voice(sr, len, vp, r.next());
                    Buffer st(2, x.length(), sr);
                    float pan = u ? 0.8f : 0.2f;
                    for (int i = 0; i < x.length(); ++i) { st.ch[0][(size_t)i] = x.ch[0][(size_t)i] * (1 - pan); st.ch[1][(size_t)i] = x.ch[0][(size_t)i] * pan; }
                    fx::mixInto(b, st, 0, 0.6f);
                }
                fx::chorus(b, 2.5f, 0.4f, 0.4f);
                kitRoom(b, dna, 1.5f, 2.0, 0.1f);
                break;
            }
            case Category::VocalChop: {
                std::vector<double> w = { 0.6, 0.3 + energy * 0.5 + glitch * 0.3, 0.3 + hyp * 0.7 };
                static const char* names[] = { "Syllable", "Chop Phrase", "Pitched Stab" };
                size_t rec = pickRecipe(c, r, w);
                g.recipe = names[rec];
                static const int vowelSeq[][3] = { { 0, 2, 3 }, { 1, 0, 4 }, { 3, 0, 2 }, { 5, 3, 0 }, { 2, 6, 0 } };
                const int* vs = vowelSeq[(size_t)r.irange(0, 4)];
                auto syl = [&](int note, double dur, int v0, int v1, bool consonant) {
                    VoiceParams vp; double fr = midiToHz(note);
                    double from = fr * std::pow(2.0, r.irange(-2, 2) / 12.0);
                    vp.pitchHz = [fr, from](double t) { return fr * std::pow(from / fr, std::exp(-t / 0.03)); };
                    vp.vowels = { v0, v1 }; vp.vowelPos = [dur](double t) { return std::min(1.0, t / std::max(0.02, dur * 0.7)); };
                    vp.breath = 0.15; vp.formantShift = shift * (fr > 330 ? 1.15 : 1.05); vp.vibratoCents = 6;
                    vp.env.a = 0.008; vp.env.d = 0.08; vp.env.s = 0.8; vp.env.r = 0.05; vp.gate = dur;
                    Buffer x = voice(sr, dur + 0.08, vp, r.next());
                    if (consonant) { Buffer cn = noiseBurst(sr, 0.04, 0.001, 0.015, 0, r.chance(0.5) ? 3000 : 1200, 12000, r.next(), r.range(2500, 6000), 1.5); fx::mixInto(x, cn, 0, 0.5f); }
                    return x;
                };
                int base = dna.noteInKey(r.irange(0, 4), 4);
                g.rootNote = base;
                if (rec == 0) b = syl(base, r.range(0.18, 0.4), vs[0], vs[1], r.chance(0.6));
                else if (rec == 2) { b = syl(base + 12, 0.15, vs[0], vs[0], true); fx::saturate(b, SatType::Soft, 0.3f); }
                else {
                    g.isLoop = true; g.bpm = dna.bpm; g.rootNote = dna.noteInKey(0, 4);
                    double six = 60.0 / dna.bpm / 4.0;
                    b = Buffer(1, (int)(16 * six * sr), sr);
                    for (int s = 0; s < 16; ++s) {
                        if (!r.chance(0.45 + energy * 0.25)) continue;
                        int note = dna.noteInKey(r.irange(0, 5), 4);
                        double dur = six * (r.chance(0.3) ? 2 : 1) * 0.9;
                        Buffer x = syl(note, dur, vs[(size_t)r.irange(0, 2)], vs[(size_t)r.irange(0, 2)], r.chance(0.5));
                        x.resize(std::min(x.length(), (int)((dur + 0.02) * sr))); fx::fadeOut(x, 0.01);
                        fx::mixInto(b, x, (int)(s * six * sr), 0.8f);
                    }
                    b.resize((int)(16 * six * sr));
                }
                b.makeStereo();
                if (!g.isLoop) kitRoom(b, dna, 0.8f, 0.8);
                break;
            }
            case Category::Chant: {
                std::vector<double> w = { 0.5 + dark * 0.4, 0.4 + energy * 0.5, 0.3 + atmo * 0.5 };
                static const char* names[] = { "Monk Drone", "Hey Shout", "Ritual" };
                size_t rec = pickRecipe(c, r, w);
                g.recipe = names[rec];
                int root = dna.noteInKey(0, 2) + (rec == 1 ? 12 : 0);
                g.rootNote = root;
                double len = rec == 1 ? 0.7 : 4.0;
                b = Buffer(2, (int)(len * sr), sr);
                int voices = rec == 1 ? 6 : 4;
                for (int k = 0; k < voices; ++k) {
                    VoiceParams vp; double fr = midiToHz(root + (k % 3 == 2 ? 7 : 0) + (k == 3 ? 12 : 0)) * std::pow(2.0, r.bipolar() * 15 / 1200.0);
                    vp.pitchHz = [fr, rec](double t) { return rec == 1 ? fr * (1.0 + 0.08 * std::exp(-t / 0.05)) : fr; };
                    vp.vowels = rec == 1 ? std::vector<int>{ 1, 0 } : std::vector<int>{ 3, 4, 3 };
                    vp.vowelPos = [rec, len](double t) { return rec == 1 ? std::min(1.0, t / 0.08) : 2.0 * t / len; };
                    vp.formantShift = 0.88 * shift; vp.breath = 0.15; vp.vibratoCents = 10; vp.jitter = 0.015;
                    vp.env.a = rec == 1 ? 0.01 : 0.4; vp.env.d = 0.2; vp.env.s = 0.85; vp.env.r = rec == 1 ? 0.15 : 0.8; vp.gate = rec == 1 ? 0.35 : len - 0.8;
                    Buffer x = voice(sr, len, vp, r.next());
                    Buffer st(2, x.length(), sr);
                    float pan = (float)k / (float)(voices - 1);
                    for (int i = 0; i < x.length(); ++i) { st.ch[0][(size_t)i] = x.ch[0][(size_t)i] * (1 - pan); st.ch[1][(size_t)i] = x.ch[0][(size_t)i] * pan; }
                    fx::mixInto(b, st, (int)(r.range(0, 0.015) * sr), 0.5f);
                }
                if (rec == 2) fx::delay(b, 60.0 / dna.bpm, 0.4f, 0.3f, true, 3000, 1.0);
                kitRoom(b, dna, 1.6f, 2.5, 0.15f);
                break;
            }
            case Category::FormantTexture: {
                std::vector<double> w = { 0.5 + atmo * 0.4, 0.4 + exp_ * 0.5, 0.3 + dark * 0.5 };
                static const char* names[] = { "Whisper Morph", "Vowel Sweep", "Throat Drone" };
                size_t rec = pickRecipe(c, r, w);
                g.recipe = names[rec];
                double len = 4.0;
                VoiceParams vp;
                int note = dna.noteInKey(0, rec == 2 ? 2 : 3); g.rootNote = rec == 0 ? -1 : note;
                double fr = midiToHz(note);
                vp.pitchHz = [fr](double) { return fr; };
                vp.vowels = { 0, 1, 2, 3, 4, 0 };
                double speed = r.range(0.6, 1.8);
                vp.vowelPos = [speed](double t) { return 2.5 + 2.5 * std::sin(t * speed); };
                vp.noiseOnly = rec == 0 ? 1.0 : 0.0; vp.breath = rec == 0 ? 0.8 : 0.2;
                vp.formantShift = shift * (rec == 2 ? 0.8 : 1.0); vp.vibratoCents = rec == 2 ? 3 : 8;
                vp.env.a = 0.5; vp.env.d = 0.5; vp.env.s = 0.9; vp.env.r = 0.8; vp.gate = len - 0.8;
                b = voice(sr, len, vp, r.next());
                if (rec == 2) { Buffer sub = b; fx::resample(sub, 0.5); sub.resize(b.length()); fx::fadeOut(sub, 0.8); fx::mixInto(b, sub, 0, 0.6f); fx::saturate(b, SatType::Tube, 0.3f); }
                b.makeStereo();
                fx::chorus(b, 3.0f, 0.2f, 0.4f);
                kitRoom(b, dna, 1.3f, 2.0);
                break;
            }
            default: { // VocalFx
                std::vector<double> w = { 0.4 + ind * 0.4 + exp_ * 0.2, 0.3 + hyp * 0.9, 0.3 + dark * 0.6, 0.3 + glitch * 0.6, 0.3 + lofi * 0.6, 0.3 + hyp * 0.4 + energy * 0.2 };
                static const char* names[] = { "Robot", "Chipmunk", "Demon", "Frozen Grain", "Radio", "Stepped Tune" };
                size_t rec = pickRecipe(c, r, w);
                g.recipe = names[rec];
                int note = dna.noteInKey(r.irange(0, 4), 3); g.rootNote = note;
                double fr = midiToHz(note);
                double len = 2.0;
                VoiceParams vp; vp.vowels = { 0, 2, 3, 1 }; vp.vowelPos = [](double t) { return std::fmod(t * 2.0, 3.0); };
                vp.formantShift = shift; vp.breath = 0.1; vp.env.a = 0.01; vp.env.d = 0.1; vp.env.s = 0.9; vp.env.r = 0.1; vp.gate = len - 0.15;
                if (rec == 5) {
                    auto& iv = scaleIntervals(dna.scale);
                    std::vector<double> steps; for (int i = 0; i < 6; ++i) steps.push_back(midiToHz(note + iv[(size_t)r.irange(0, (int)iv.size() - 1)]));
                    vp.pitchHz = [steps](double t) { return steps[(size_t)std::min(5.0, t * 3.0)]; };
                    vp.vibratoCents = 0;
                } else vp.pitchHz = [fr](double) { return fr; };
                if (rec == 0) vp.vibratoCents = 0, vp.jitter = 0;
                b = voice(sr, len, vp, r.next());
                switch (rec) {
                    case 0: fx::ringMod(b, fr * 2, 0.5f); fx::bitcrush(b, 8, 1, 0.4f); break;
                    case 1: fx::resample(b, 1.9); break;
                    case 2: fx::resample(b, 0.55); fx::saturate(b, SatType::Tube, 0.4f + dist * 0.4f); fx::lowpass(b, 3000, 0.7); break;
                    case 3: { fx::GrainParams gp; gp.grainMs = 90; gp.density = 50; gp.position = 0.4; gp.positionJitter = 0.03; gp.pitchJitter = 0.05; b = fx::granulate(b, 3.0, gp, r.next()); break; }
                    case 4: fx::bandpass(b, 1500, 1.2); fx::saturate(b, SatType::Hard, 0.5f); fx::bitcrush(b, 7, 3, 0.6f); break;
                    default: fx::saturate(b, SatType::Soft, 0.2f); break;
                }
                b.makeStereo();
                kitRoom(b, dna, 0.9f, 1.0);
                break;
            }
        }
        g.audio = std::move(b);
        return g;
    }
};

} // namespace

EnginePtr makeVocalEngine() { return std::make_shared<VocalEngine>(); }

} // namespace sdna
