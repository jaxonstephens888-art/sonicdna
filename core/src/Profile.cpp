#include "sdna/Profile.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <map>
#include <set>
#include <sstream>

namespace sdna {

namespace {
struct DescInfo { const char* id; const char* label; const char* help; };
const DescInfo kDesc[kNumDesc] = {
    { "dark", "Dark", "Lower filter cutoffs, darker tuning, minor-leaning harmony" },
    { "distorted", "Distorted", "Saturation, clipping and wavefolding amount" },
    { "electronic", "Electronic", "Synthetic sources vs. organic / physically-modelled ones" },
    { "lofi", "Lo-fi", "Bit reduction, sample-rate reduction, tape hiss and vinyl" },
    { "aggressive", "Aggressive", "Transient punch, hard attacks, harsher timbres" },
    { "atmospheric", "Atmospheric", "Reverb size and send, long tails, space" },
    { "industrial", "Industrial", "Metallic, mechanical, inharmonic and ring-modulated layers" },
    { "glitch", "Glitch", "Stutters, buffer repeats, granular chops" },
    { "vintage", "Vintage", "Analog drift, tape wow/flutter, warm drum-machine tones" },
    { "heavy", "Heavy", "Low-end weight, sub content, long 808 tails" },
    { "energy", "Energy", "Tempo, rhythmic density and envelope speed" },
    { "melodic", "Melodic", "Harmonic richness and chordal content in tonal sounds" },
    { "experimental", "Experimental", "Unconventional synthesis: odd FM ratios, chaos, randomness" },
    { "hyper", "Hyper", "Glossy, plastic, pitched-up, hyper-processed pop textures" },
};
const char* kScaleNames[(int)Scale::Count] = { "minor", "major", "dorian", "phrygian", "harmonic minor", "minor pentatonic", "lydian" };
const char* kGrooveIds[(int)Groove::Count] = {
    "four_floor", "trap", "breakbeat", "halftime", "boom_bap", "garage", "hardstyle", "industrial",
    "electroclash", "drill", "jersey", "dnb", "ambient", "rnb", "hyperpop", "phonk", "reggaeton" };
const char* kGrooveLabels[(int)Groove::Count] = {
    "Four on the floor", "Trap", "Breakbeat", "Half-time", "Boom bap", "2-step garage", "Hardstyle", "Industrial",
    "Electroclash", "Drill", "Jersey club", "Drum & bass", "Ambient pulse", "R&B", "Hyperpop", "Phonk", "Dembow" };
}

const char* descId(Desc d) { return kDesc[(int)d].id; }
const char* descLabel(Desc d) { return kDesc[(int)d].label; }
const char* descHelp(Desc d) { return kDesc[(int)d].help; }
bool descFromId(const std::string& id, Desc& out) {
    std::string l = toLower(id);
    for (int i = 0; i < kNumDesc; ++i) if (l == kDesc[i].id || l == toLower(kDesc[i].label)) { out = (Desc)i; return true; }
    if (l == "lo-fi" || l == "lo_fi") { out = Desc::Lofi; return true; }
    return false;
}

const char* scaleName(Scale s) { return kScaleNames[(int)s]; }
bool scaleFromName(const std::string& n, Scale& out) {
    std::string l = toLower(n);
    for (int i = 0; i < (int)Scale::Count; ++i) if (l == kScaleNames[i]) { out = (Scale)i; return true; }
    if (l == "harmonic_minor") { out = Scale::HarmonicMinor; return true; }
    if (l == "minor_pentatonic" || l == "pentatonic") { out = Scale::MinorPentatonic; return true; }
    return false;
}
const std::vector<int>& scaleIntervals(Scale s) {
    static const std::vector<int> t[(int)Scale::Count] = {
        { 0, 2, 3, 5, 7, 8, 10 }, { 0, 2, 4, 5, 7, 9, 11 }, { 0, 2, 3, 5, 7, 9, 10 }, { 0, 1, 3, 5, 7, 8, 10 },
        { 0, 2, 3, 5, 7, 8, 11 }, { 0, 3, 5, 7, 10 }, { 0, 2, 4, 6, 7, 9, 11 } };
    return t[(int)s];
}
const char* noteName(int pc) {
    static const char* n[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    return n[((pc % 12) + 12) % 12];
}

const char* grooveId(Groove g) { return kGrooveIds[(int)g]; }
const char* grooveLabel(Groove g) { return kGrooveLabels[(int)g]; }
bool grooveFromId(const std::string& id, Groove& out) {
    std::string l = toLower(id);
    for (int i = 0; i < (int)Groove::Count; ++i) if (l == kGrooveIds[i] || l == toLower(kGrooveLabels[i])) { out = (Groove)i; return true; }
    return false;
}

std::string toLower(const std::string& s) {
    std::string o = s;
    for (auto& c : o) c = (char)std::tolower((unsigned char)c);
    return o;
}
std::string trim(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && std::isspace((unsigned char)s[a])) ++a;
    while (b > a && std::isspace((unsigned char)s[b - 1])) --b;
    return s.substr(a, b - a);
}

std::vector<std::pair<std::string, float>> SonicProfile::topTags(int n) const {
    std::vector<std::pair<std::string, float>> v;
    for (int i = 0; i < kNumDesc; ++i) if (d[(size_t)i] >= 0.55f) v.push_back({ kDesc[i].label, d[(size_t)i] });
    // Low values are characteristic too.
    if (d[(size_t)Desc::Electronic] <= 0.3f) v.push_back({ "Organic", 1.0f - d[(size_t)Desc::Electronic] });
    if (d[(size_t)Desc::Dark] <= 0.25f) v.push_back({ "Bright", 1.0f - d[(size_t)Desc::Dark] });
    if (d[(size_t)Desc::Energy] <= 0.25f) v.push_back({ "Laid-back", 1.0f - d[(size_t)Desc::Energy] });
    if (d[(size_t)Desc::Melodic] <= 0.2f) v.push_back({ "Minimal", 1.0f - d[(size_t)Desc::Melodic] });
    std::sort(v.begin(), v.end(), [](auto& a, auto& b) { return a.second > b.second; });
    if ((int)v.size() > n) v.resize((size_t)n);
    return v;
}

SonicProfile neutralProfile() {
    SonicProfile p;
    // A deliberately "generic modern production" starting point.
    const float vals[kNumDesc] = { 0.45f, 0.25f, 0.6f, 0.15f, 0.4f, 0.35f, 0.1f, 0.1f, 0.25f, 0.5f, 0.5f, 0.5f, 0.2f, 0.1f };
    for (int i = 0; i < kNumDesc; ++i) p.d[(size_t)i] = vals[i];
    p.bpm = 120; p.swing = 0.05f; p.scale = Scale::Minor;
    p.grooves = { Groove::FourFloor, Groove::Trap };
    p.source = "neutral";
    return p;
}

SonicProfile blendProfiles(const SonicProfile& a, const SonicProfile& b, float t) {
    SonicProfile o = a;
    for (int i = 0; i < kNumDesc; ++i) o.d[(size_t)i] = a.d[(size_t)i] + (b.d[(size_t)i] - a.d[(size_t)i]) * t;
    o.bpm = a.bpm + (b.bpm - a.bpm) * t;
    o.swing = a.swing + (b.swing - a.swing) * t;
    if (t >= 0.5f) { o.scale = b.scale; if (b.keyRoot >= 0) o.keyRoot = b.keyRoot; }
    // Merge grooves preserving order of the dominant profile.
    const auto& first = t >= 0.5f ? b.grooves : a.grooves;
    const auto& second = t >= 0.5f ? a.grooves : b.grooves;
    o.grooves.clear();
    for (auto g : first) if (std::find(o.grooves.begin(), o.grooves.end(), g) == o.grooves.end()) o.grooves.push_back(g);
    for (auto g : second) if (std::find(o.grooves.begin(), o.grooves.end(), g) == o.grooves.end()) o.grooves.push_back(g);
    return o;
}

SonicProfile effectiveProfile(const SonicProfile& p, const Influence& inf, Domain dom) {
    SonicProfile n = neutralProfile();
    SonicProfile o = p;
    float domainInf = dom == Domain::Drums ? inf.drums : dom == Domain::Tonal ? inf.tonal : inf.texture;
    for (int i = 0; i < kNumDesc; ++i) {
        Desc d = (Desc)i;
        bool processing = d == Desc::Distorted || d == Desc::Lofi || d == Desc::Glitch || d == Desc::Vintage;
        float t = processing ? inf.processing * (0.35f + 0.65f * domainInf) : domainInf;
        o.d[(size_t)i] = n.d[(size_t)i] + (p.d[(size_t)i] - n.d[(size_t)i]) * std::clamp(t, 0.0f, 1.0f);
    }
    o.bpm = n.bpm + (p.bpm - n.bpm) * inf.groove;
    o.swing = n.swing + (p.swing - n.swing) * inf.groove;
    if (inf.groove < 0.35f) o.grooves = n.grooves;
    return o;
}

// ---------------------------------------------------------------------------
// Prompt parsing
// ---------------------------------------------------------------------------
namespace {
struct KW { Desc d; float sign; float bpm; };
const std::map<std::string, std::vector<KW>>& keywordMap() {
    static std::map<std::string, std::vector<KW>> m;
    if (!m.empty()) return m;
    auto add = [&](std::initializer_list<const char*> words, std::vector<KW> kws) { for (auto w : words) m[w] = kws; };
    add({ "dark", "darker", "darkest", "moody", "sinister", "gloomy", "ominous", "evil", "haunting", "haunted", "eerie", "menacing", "gothic", "goth", "creepy", "bleak", "nocturnal" }, { { Desc::Dark, 1, 0 } });
    add({ "bright", "brighter", "airy", "shiny", "sparkly", "sparkling", "happy", "sunny", "uplifting", "crisp" }, { { Desc::Dark, -1, 0 } });
    add({ "distorted", "distortion", "dirty", "dirtier", "gritty", "grittier", "crunchy", "crunchier", "saturated", "fuzzy", "fuzz", "blown", "overdriven", "driven", "clipped", "clipping", "crushed", "grimy", "grimier", "filthy", "nasty" }, { { Desc::Distorted, 1, 0 } });
    add({ "clean", "cleaner", "pristine", "polished", "smooth", "smoother" }, { { Desc::Distorted, -1, 0 }, { Desc::Lofi, -0.6f, 0 } });
    add({ "electronic", "synthetic", "digital", "synthy", "robotic", "electro" }, { { Desc::Electronic, 1, 0 } });
    add({ "organic", "acoustic", "natural", "live", "human", "earthy", "woody" }, { { Desc::Electronic, -1, 0 } });
    add({ "lofi", "dusty", "tape", "vinyl", "cassette", "degraded", "grainy", "crackly", "worn", "bitcrushed", "8bit", "bitcrush" }, { { Desc::Lofi, 1, 0 } });
    add({ "hifi" }, { { Desc::Lofi, -1, 0 } });
    add({ "aggressive", "harder", "hard", "punchy", "punchier", "violent", "angry", "brutal", "rough", "raw", "savage", "slamming", "hard-hitting" }, { { Desc::Aggressive, 1, 0 } });
    add({ "soft", "softer", "gentle", "mellow", "chill", "calm", "relaxed", "tender", "delicate", "warm" }, { { Desc::Aggressive, -1, 0 } });
    add({ "atmospheric", "ambient", "spacious", "reverb", "reverby", "wet", "dreamy", "ethereal", "washed", "cinematic", "lush", "hazy", "foggy", "cavernous", "huge", "wide" }, { { Desc::Atmospheric, 1, 0 } });
    add({ "dry", "drier", "tight", "tighter", "upfront", "intimate", "close" }, { { Desc::Atmospheric, -1, 0 } });
    add({ "industrial", "metallic", "mechanical", "harsh", "factory", "clanky", "machine", "noisy", "noise", "steel", "rusty" }, { { Desc::Industrial, 1, 0 } });
    add({ "glitch", "glitchy", "glitched", "broken", "stutter", "stuttery", "chopped", "granular", "fractured", "corrupted", "datamosh" }, { { Desc::Glitch, 1, 0 } });
    add({ "vintage", "retro", "analog", "analogue", "80s", "90s", "70s", "oldschool", "old-school", "nostalgic", "classic" }, { { Desc::Vintage, 1, 0 } });
    add({ "modern", "futuristic", "contemporary", "fresh" }, { { Desc::Vintage, -1, 0 } });
    add({ "heavy", "heavier", "bassy", "boomy", "sub", "subby", "weight", "weighty", "fat", "fatter", "thick", "thicker", "massive", "deep", "deeper" }, { { Desc::Heavy, 1, 0 } });
    add({ "thin", "thinner", "light", "lighter", "small" }, { { Desc::Heavy, -1, 0 } });
    add({ "energetic", "faster", "uptempo", "driving", "intense", "frantic", "busy", "rave", "ravey", "hyperactive" }, { { Desc::Energy, 1, 8 } });
    add({ "slower", "downtempo", "laidback", "laid-back", "lazy", "slow", "sluggish", "sleepy" }, { { Desc::Energy, -1, -8 } });
    add({ "melodic", "musical", "harmonic", "chords", "chordal", "emotional", "sad", "melancholic", "melancholy", "romantic", "soulful" }, { { Desc::Melodic, 1, 0 } });
    add({ "minimal", "sparse", "stripped", "simple", "minimalist", "bare" }, { { Desc::Melodic, -1, 0 }, { Desc::Experimental, -0.3f, 0 } });
    add({ "experimental", "weird", "weirder", "strange", "abstract", "alien", "avant-garde", "avantgarde", "unusual", "otherworldly", "chaotic", "unpredictable", "psychedelic" }, { { Desc::Experimental, 1, 0 } });
    add({ "hyperpop", "plastic", "bubbly", "glossy", "pitched", "chipmunk", "squeaky", "latex", "candy", "bouncy" }, { { Desc::Hyper, 1, 0 } });
    return m;
}
const std::set<std::string>& stopwords() {
    static const std::set<std::string> s = { "make", "it", "more", "less", "a", "an", "the", "and", "with", "bit", "much", "very", "sound", "sounds",
        "like", "kit", "some", "add", "but", "keep", "way", "lot", "lots", "little", "slightly", "really", "super", "extra", "too", "please",
        "of", "to", "in", "on", "my", "me", "i", "want", "should", "be", "feel", "feels", "vibe", "vibes", "overall", "everything", "all",
        "drums", "drum", "samples", "sample", "pack", "just", "also", "so", "not", "no", "without", "fewer", "even", "most", "bit", "tad",
        "touch", "somewhat", "extremely", "insanely", "way", "less", "make", "them", "they", "is", "are", "for", "that", "this", "than", "kind",
        "sort", "tone", "tones", "texture", "textures", "go", "turn", "up", "down", "can", "you", "we", "our", "its", "i'd", "id", "maybe", "bass", "kicks",
        "snares", "hats", "melody", "melodies", "vocals", "style", "type", "ish", "-ish", "tempo", "bpm" };
    return s;
}
}

static std::string normalisePrompt(const std::string& in) {
    std::string s = toLower(in);
    auto repl = [&](const std::string& a, const std::string& b) {
        size_t pos = 0; while ((pos = s.find(a, pos)) != std::string::npos) { s.replace(pos, a.size(), b); pos += b.size(); }
    };
    repl("lo-fi", "lofi"); repl("lo fi", "lofi"); repl("hi-fi", "hifi"); repl("hi fi", "hifi");
    repl("laid back", "laidback"); repl("old school", "oldschool"); repl("hard hitting", "hard-hitting");
    repl("8-bit", "8bit"); repl("avant garde", "avantgarde"); repl("hyper pop", "hyperpop"); repl("80's", "80s"); repl("90's", "90s");
    return s;
}

PromptResult applyPrompt(SonicProfile& p, const std::string& prompt) {
    PromptResult res;
    std::string s = normalisePrompt(prompt);
    // Split into clauses.
    std::vector<std::string> clauses; std::string cur;
    for (char c : s) {
        if (c == ',' || c == '.' || c == ';' || c == '\n' || c == '!' || c == '+' || c == '&') { clauses.push_back(cur); cur.clear(); }
        else cur += c;
    }
    clauses.push_back(cur);
    // Also split on " and " / " but "
    std::vector<std::string> parts;
    for (auto& cl : clauses) {
        std::string x = " " + cl + " ";
        size_t pos;
        while ((pos = x.find(" and ")) != std::string::npos || (pos = x.find(" but ")) != std::string::npos) {
            parts.push_back(x.substr(0, pos)); x = " " + x.substr(pos + 5);
        }
        parts.push_back(x);
    }
    std::map<int, float> accum;
    for (auto& part : parts) {
        std::vector<std::string> toks; std::stringstream ss(part); std::string t;
        while (ss >> t) {
            std::string clean; for (char c : t) if (std::isalnum((unsigned char)c) || c == '-' || c == '\'') clean += c;
            if (!clean.empty()) toks.push_back(clean);
        }
        if (toks.empty()) continue;
        float intensity = 0.2f; bool negate = false;
        for (auto& w : toks) {
            if (w == "much" || w == "way" || w == "very" || w == "extremely" || w == "super" || w == "really" || w == "insanely" || w == "lot" || w == "massively" || w == "totally") intensity = 0.35f;
            if (w == "slightly" || w == "little" || w == "bit" || w == "tad" || w == "touch" || w == "somewhat" || w == "subtle" || w == "subtly" || w == "hint") intensity = 0.1f;
        }
        for (auto& w : toks) {
            if (w == "less" || w == "not" || w == "no" || w == "without" || w == "fewer" || w == "reduce" || w == "remove" || w == "lose") { negate = true; continue; }
            if (w == "more" || w == "add" || w == "increase" || w == "extra") { negate = false; continue; }
            auto it = keywordMap().find(w);
            if (it == keywordMap().end()) {
                if (!stopwords().count(w) && w.size() > 2) res.unrecognised.push_back(w);
                continue;
            }
            for (auto& kw : it->second) {
                float delta = kw.sign * intensity * (negate ? -1.0f : 1.0f);
                accum[(int)kw.d] += delta;
                res.bpmDelta += kw.bpm * (negate ? -1.0f : 1.0f) * (intensity / 0.2f);
            }
        }
    }
    for (auto& [di, delta] : accum) {
        float dclamped = std::clamp(delta, -0.6f, 0.6f);
        float& v = p.d[(size_t)di];
        float before = v;
        v = std::clamp(v + dclamped, 0.0f, 1.0f);
        if (std::abs(v - before) > 1e-4f) res.changes.push_back({ descLabel((Desc)di), v - before });
    }
    if (res.bpmDelta != 0.0f) p.bpm = std::clamp(p.bpm + res.bpmDelta, 60.0f, 200.0f);
    return res;
}

} // namespace sdna
