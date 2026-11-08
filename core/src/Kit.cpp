#include "sdna/Kit.h"

#include <cctype>
#include <cstdio>

namespace sdna {

namespace {
const CategoryInfo kCats[kNumCategories] = {
    { Category::Kick, Group::Drums, "kick", "Kicks", "Kick" },
    { Category::Snare, Group::Drums, "snare", "Snares", "Snare" },
    { Category::Clap, Group::Drums, "clap", "Claps", "Clap" },
    { Category::Rim, Group::Drums, "rim", "Rims", "Rim" },
    { Category::Tom, Group::Drums, "tom", "Toms", "Tom" },
    { Category::Perc, Group::Drums, "perc", "Percussion", "Perc" },
    { Category::Fill, Group::Drums, "fill", "Fills", "Fill" },
    { Category::ClosedHat, Group::Cymbals, "closed_hat", "Closed Hats", "Closed Hat" },
    { Category::OpenHat, Group::Cymbals, "open_hat", "Open Hats", "Open Hat" },
    { Category::PedalHat, Group::Cymbals, "pedal_hat", "Pedal Hats", "Pedal Hat" },
    { Category::Ride, Group::Cymbals, "ride", "Rides", "Ride" },
    { Category::Crash, Group::Cymbals, "crash", "Crashes", "Crash" },
    { Category::Shaker, Group::Cymbals, "shaker", "Shakers", "Shaker" },
    { Category::Bass808, Group::Bass, "808", "808s", "808" },
    { Category::SubBass, Group::Bass, "sub_bass", "Sub Bass", "Sub" },
    { Category::BassShot, Group::Bass, "bass_shot", "Bass One-Shots", "Bass" },
    { Category::DistBass, Group::Bass, "dist_bass", "Distorted Bass", "Dist Bass" },
    { Category::SynthBass, Group::Bass, "synth_bass", "Synth Bass", "Synth Bass" },
    { Category::Lead, Group::Melodic, "lead", "Synth Leads", "Lead" },
    { Category::Pluck, Group::Melodic, "pluck", "Synth Plucks", "Pluck" },
    { Category::Keys, Group::Melodic, "keys", "Keys", "Keys" },
    { Category::Piano, Group::Melodic, "piano", "Piano", "Piano" },
    { Category::Bell, Group::Melodic, "bell", "Bells", "Bell" },
    { Category::Strings, Group::Melodic, "strings", "Strings", "Strings" },
    { Category::Pad, Group::Melodic, "pad", "Pads", "Pad" },
    { Category::Arp, Group::Melodic, "arp", "Arpeggios", "Arp" },
    { Category::Riser, Group::SoundDesign, "riser", "Risers", "Riser" },
    { Category::Impact, Group::SoundDesign, "impact", "Impacts", "Impact" },
    { Category::Transition, Group::SoundDesign, "transition", "Transitions", "Transition" },
    { Category::Glitch, Group::SoundDesign, "glitch", "Glitches", "Glitch" },
    { Category::Noise, Group::SoundDesign, "noise", "Noise", "Noise" },
    { Category::Atmosphere, Group::SoundDesign, "atmosphere", "Atmospheres", "Atmos" },
    { Category::Texture, Group::SoundDesign, "texture", "Textures", "Texture" },
    { Category::Reverse, Group::SoundDesign, "reverse", "Reverse", "Reverse" },
    { Category::VocalTexture, Group::Vocal, "vocal_texture", "Vocal Textures", "Vox Texture" },
    { Category::VocalChop, Group::Vocal, "vocal_chop", "Vocal Chops", "Vox Chop" },
    { Category::Chant, Group::Vocal, "chant", "Chants", "Chant" },
    { Category::FormantTexture, Group::Vocal, "formant_texture", "Formant Textures", "Formant" },
    { Category::VocalFx, Group::Vocal, "vocal_fx", "Processed Vocal FX", "Vox FX" },
};
const char* kGroupLabels[kNumGroups] = { "Drums", "Hi-Hats & Cymbals", "Bass", "Melodic", "Sound Design", "Vocal" };
const char* kGroupFolders[kNumGroups] = { "Drums", "Cymbals", "Bass", "Melodic", "Sound Design", "Vocals" };
}

const CategoryInfo& categoryInfo(Category c) { return kCats[(int)c]; }
const char* groupLabel(Group g) { return kGroupLabels[(int)g]; }
const char* groupFolder(Group g) { return kGroupFolders[(int)g]; }
bool categoryFromId(const std::string& id, Category& out) {
    for (auto& c : kCats) if (id == c.id) { out = c.cat; return true; }
    return false;
}
std::vector<Category> categoriesInGroup(Group g) {
    std::vector<Category> v; for (auto& c : kCats) if (c.group == g) v.push_back(c.cat); return v;
}

int KitDNA::noteInKey(int degree, int octave) const {
    const auto& iv = scaleIntervals(scale);
    int n = (int)iv.size();
    int oct = degree >= 0 ? degree / n : -((-degree + n - 1) / n);
    int idx = degree - oct * n;
    return 12 * (octave + 1 + oct) + keyRoot + iv[(size_t)idx];
}

KitDNA deriveDNA(const SonicProfile& p, uint64_t seed) {
    KitDNA k;
    Rng r(seed ^ 0xD1A5ull);
    k.seed = seed;
    auto v = [&](Desc d) { return p[d]; };
    k.keyRoot = p.keyRoot >= 0 ? p.keyRoot : r.irange(0, 11);
    k.scale = p.scale;
    k.bpm = std::round(p.bpm);
    k.swing = p.swing;
    // Saturation flavour from the character of the profile.
    float dist = v(Desc::Distorted), ind = v(Desc::Industrial), vin = v(Desc::Vintage), hyp = v(Desc::Hyper), exp_ = v(Desc::Experimental);
    std::vector<double> w = { 0.6 + vin * 0.5, 0.3 + vin * 1.0, dist * v(Desc::Aggressive) * 1.5 + ind * 0.6, exp_ * 0.8 + hyp * 0.8 + ind * 0.4, ind * 0.6 + dist * 0.3, v(Desc::Lofi) * 0.6 };
    k.satType = (SatType)r.weighted(w);
    k.satAmount = clampf(0.05f + dist * 0.8f + v(Desc::Aggressive) * 0.1f, 0, 1);
    float lofi = v(Desc::Lofi);
    k.crushMix = lofi > 0.45f ? clampf((lofi - 0.45f) * 1.6f, 0, 0.8f) : 0.0f;
    k.crushBits = (int)std::round(16 - lofi * 9 - r.uni() * 2);
    k.crushDownsample = lofi > 0.6f ? 1 + (int)((lofi - 0.6f) * 6) : 1;
    k.roomSize = clampf(0.25f + v(Desc::Atmospheric) * 0.7f, 0, 0.97f);
    k.roomDamp = clampf(0.3f + v(Desc::Dark) * 0.5f + vin * 0.2f, 0, 1);
    k.roomWet = clampf(0.04f + v(Desc::Atmospheric) * 0.35f, 0, 0.5f);
    k.predelayMs = 5.0f + v(Desc::Atmospheric) * 30.0f;
    k.tapeWow = clampf((vin - 0.4f) * 0.8f + lofi * 0.3f, 0, 1);
    k.vinyl = lofi > 0.6f && vin > 0.4f;
    k.noiseFloorDb = lofi > 0.35f ? -62.0f + lofi * 22.0f : -120.0f;
    k.tone = clampf((0.45f - v(Desc::Dark)) * 1.4f + hyp * 0.3f, -1, 1);
    k.drumTune = (v(Desc::Hyper) - 0.3f) * 6.0f - (v(Desc::Heavy) - 0.5f) * 3.0f;
    k.transientDb = (v(Desc::Aggressive) - 0.4f) * 8.0f;
    k.fmBias = clampf(exp_ * 0.6f + ind * 0.5f, 0, 1);
    // Kick / 808 fundamental: key root in the 30–60 Hz octave.
    k.kickNote = 24 + k.keyRoot;
    if (k.kickNote < 28) k.kickNote += 12;
    // A short, characteristic progression in scale degrees.
    static const std::vector<std::vector<int>> minorProgs = { { 0, 5, 2, 6 }, { 0, 3, 4, 0 }, { 0, 6, 5, 4 }, { 0, 2, 5, 3 }, { 0, 0, 5, 5 }, { 0, 1, 0, 6 } };
    static const std::vector<std::vector<int>> majorProgs = { { 0, 4, 5, 3 }, { 0, 3, 4, 3 }, { 0, 5, 3, 4 }, { 3, 4, 2, 5 }, { 0, 2, 3, 4 } };
    bool majorish = p.scale == Scale::Major || p.scale == Scale::Lydian;
    const auto& progs = majorish ? majorProgs : minorProgs;
    k.progression = progs[(size_t)r.irange(0, (int)progs.size() - 1)];
    if (p.scale == Scale::Phrygian && r.chance(0.6)) k.progression = { 0, 1, 0, 6 };
    return k;
}

std::vector<SamplePtr> Kit::inCategory(Category c) const {
    std::vector<SamplePtr> v; for (auto& s : samples) if (s->category == c) v.push_back(s); return v;
}
SamplePtr Kit::find(const std::string& id) const {
    for (auto& s : samples) { if (s->id == id) return s; }
    return nullptr;
}
std::string Kit::slug() const { return slugify(name.empty() ? "SonicDNA" : name); }

std::string slugify(const std::string& s) {
    std::string o;
    bool up = true;
    for (unsigned char c : s) {
        if (std::isalnum(c)) { o += up ? (char)std::toupper(c) : (char)c; up = false; }
        else up = true;
    }
    if (o.empty()) o = "Kit";
    if (o.size() > 32) o.resize(32);
    return o;
}

std::string Sample::fileStem(const std::string& kitSlug) const {
    char buf[32]; std::snprintf(buf, sizeof buf, "%02d", variant + 1);
    std::string stem = kitSlug + "_" + slugify(categoryInfo(category).single) + "_" + buf;
    if (!recipe.empty()) stem += "_" + slugify(recipe);
    if (rootNote >= 0) stem += std::string("_") + noteName(rootNote % 12) + std::to_string(rootNote / 12 - 1);
    if (isLoop && bpm > 0) stem += "_" + std::to_string((int)std::round(bpm)) + "bpm";
    for (auto& c : stem) if (c == '#') c = 's';
    return stem;
}

} // namespace sdna
