// Hi-hats & cymbals: square-cluster metal (808-style), FM metal, filtered noise, shakers.
#include "Synth.h"

namespace sdna {
using namespace synth;

namespace {

class CymbalEngine : public SoundEngine {
public:
    std::string id() const override { return "procedural.cymbals"; }
    std::string description() const override { return "Metallic cluster / FM / noise cymbal synthesis"; }
    bool handles(Category c) const override {
        return c == Category::ClosedHat || c == Category::OpenHat || c == Category::PedalHat || c == Category::Ride || c == Category::Crash || c == Category::Shaker;
    }

    GenResult generate(Category cat, const GenContext& c) const override {
        Rng r(c.seed);
        double sr = c.sampleRate;
        float dark = pv(c, r, Desc::Dark), elec = pv(c, r, Desc::Electronic), lofi = pv(c, r, Desc::Lofi), ind = pv(c, r, Desc::Industrial);
        float dist = pv(c, r, Desc::Distorted), vin = pv(c, r, Desc::Vintage), hyp = pv(c, r, Desc::Hyper), aggr = pv(c, r, Desc::Aggressive), exp_ = pv(c, r, Desc::Experimental);
        double brightMul = std::pow(2.0, (0.45 - dark) * 1.2);
        GenResult g;

        if (cat == Category::Shaker) {
            std::vector<double> w = { 0.5 + (1 - elec) * 0.8, 0.4 + elec * 0.6, lofi * 1.2, hyp * 1.2 + exp_ * 0.3 };
            static const char* names[] = { "Organic", "Electronic", "Dusty", "Glass" };
            size_t rec = pickRecipe(c, r, w);
            g.recipe = names[rec];
            Buffer b(1, (int)(0.3 * sr), sr);
            int grains = rec == 0 ? r.irange(1, 2) : 1;
            for (int k = 0; k < grains; ++k) {
                double att = rec == 1 ? 0.003 : r.range(0.012, 0.03);
                Buffer n = noiseBurst(sr, 0.25, att, r.range(0.06, 0.12), 0, 3000, 16000, r.next(), r.range(5500, 9500) * brightMul, rec == 3 ? 6.0 : 1.2);
                fx::mixInto(b, n, (int)(k * r.range(0.03, 0.06) * sr), k == 0 ? 0.7f : 1.0f);
            }
            if (rec == 2) { fx::lowpass(b, 7000, 0.7); fx::bitcrush(b, 9, 2, 0.6f); }
            if (rec == 3) { Buffer m = metalCluster(sr, 0.2, 4.0, 0.05, r.next()); fx::highpass(m, 8000); fx::mixInto(b, m, 0, 0.3f); }
            g.audio = std::move(b);
            return g;
        }

        // Recipe: 0 = 808 squares, 1 = FM metal, 2 = noise, 3 = dusty, 4 = industrial, 5 = tick (hyper)
        std::vector<double> w = { 0.4 + elec * 0.6 + vin * 0.5, 0.2 + exp_ * 0.8 + ind * 0.4, 0.3 + (1 - elec) * 0.9, lofi * 1.4, ind * 1.2 + dist * aggr * 0.8, hyp * 1.4 };
        static const char* names[] = { "808 Metal", "FM Metal", "Noise", "Dusty", "Industrial", "Tick" };
        size_t rec = pickRecipe(c, r, w);
        g.recipe = names[rec];

        double t60, length, hp, bpHz;
        switch (cat) {
            case Category::ClosedHat: t60 = r.range(0.03, 0.09); length = 0.2; hp = 6500; bpHz = 10000; break;
            case Category::OpenHat: t60 = r.range(0.3, 0.8); length = t60 + 0.2; hp = 6000; bpHz = 9500; break;
            case Category::PedalHat: t60 = r.range(0.1, 0.18); length = 0.35; hp = 5000; bpHz = 8000; break;
            case Category::Ride: t60 = r.range(1.5, 3.0); length = t60 * 0.9; hp = 3500; bpHz = 6000; break;
            default: t60 = r.range(1.6, 3.5); length = t60 * 0.9; hp = 2500; bpHz = 7000; break; // crash
        }
        if (rec == 5) { t60 *= 0.4; length = std::max(0.08, length * 0.5); }
        double tune = r.range(0.85, 1.6) * std::pow(2.0, c.dna.drumTune / 24.0);
        Buffer metal, noise;
        int len = (int)(length * sr);
        metal = metalCluster(sr, length, tune, t60, r.next(), cat == Category::Crash ? 9 : 6, rec == 1 || rec == 4);
        noise = noiseBurst(sr, length, cat == Category::Crash ? 0.003 : 0.0, t60 * (cat == Category::Ride ? 0.4 : 0.9), 0, hp, 18000, r.next());
        float metalMix = rec == 2 ? 0.2f : rec == 0 ? 0.85f : 0.6f;
        if (cat == Category::Crash) metalMix *= 0.6f;
        Buffer b(1, len, sr);
        fx::mixInto(b, metal, 0, metalMix);
        fx::mixInto(b, noise, 0, 1.0f - metalMix * 0.6f);
        fx::highpass(b, hp * brightMul * 0.8, 0.7);
        fx::bandpass(b, std::min(sr * 0.4, bpHz * brightMul), 0.35);
        if (cat == Category::Ride) {
            // bell partial
            Buffer bell = fm2(sr, length, r.range(2400, 3600) * tune, 1.41, 1.5, 0.5, t60 * 0.7, 0.0005);
            fx::mixInto(b, bell, 0, 0.18f);
        }
        if (cat == Category::PedalHat) {
            Buffer chick = noiseBurst(sr, 0.05, 0.001, 0.02, 0, 800, 6000, r.next(), 2200, 1.5);
            fx::mixInto(b, chick, 0, 0.5f);
        }
        if (cat == Category::Crash) {
            fx::sweep(b, 0, 16000 * brightMul, 5000 * brightMul, 0.5, 0.5);
            fx::chorus(b, 2.0f, 0.3f, 0.4f);
        }
        if (rec == 3) { fx::lowpass(b, 8000, 0.7); fx::bitcrush(b, r.irange(8, 11), r.irange(2, 3), 0.7f); }
        if (rec == 4) { fx::saturate(b, SatType::Hard, 0.6f); fx::ringMod(b, r.range(1500, 4000), 0.3f); }
        fx::fadeIn(b, 0.0003);
        g.audio = std::move(b);
        return g;
    }
};

} // namespace

EnginePtr makeCymbalEngine() { return std::make_shared<CymbalEngine>(); }

} // namespace sdna
