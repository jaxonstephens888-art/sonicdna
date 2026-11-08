// SONIC DNA — sonic profile model.
// A SonicProfile is the bridge between "analysis" (what an artist sounds like)
// and "synthesis" (parameters for the DSP engines). It is a small, explicit,
// human-editable vector of descriptors plus musical context.
#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace sdna {

enum class Desc : int {
    Dark, Distorted, Electronic, Lofi, Aggressive, Atmospheric, Industrial,
    Glitch, Vintage, Heavy, Energy, Melodic, Experimental, Hyper, Count
};
constexpr int kNumDesc = (int)Desc::Count;
const char* descId(Desc d);     // "dark"
const char* descLabel(Desc d);  // "Dark"
const char* descHelp(Desc d);   // one-line explanation of what it changes
bool descFromId(const std::string& id, Desc& out);

enum class Scale : int { Minor, Major, Dorian, Phrygian, HarmonicMinor, MinorPentatonic, Lydian, Count };
const char* scaleName(Scale s);
bool scaleFromName(const std::string& n, Scale& out);
const std::vector<int>& scaleIntervals(Scale s);
const char* noteName(int pc);

enum class Groove : int {
    FourFloor, Trap, Breakbeat, HalfTime, BoomBap, Garage, Hardstyle, Industrial,
    Electroclash, Drill, Jersey, DnB, Ambient, RnB, Hyperpop, Phonk, Reggaeton, Count
};
const char* grooveId(Groove g);
const char* grooveLabel(Groove g);
bool grooveFromId(const std::string& id, Groove& out);

struct SonicProfile {
    std::array<float, kNumDesc> d{};   // each 0..1
    float bpm = 120.0f;
    float swing = 0.0f;               // 0..1 (0 = straight, 1 = heavy shuffle)
    int keyRoot = -1;                 // pitch class 0..11, -1 = let generator choose
    Scale scale = Scale::Minor;
    std::vector<Groove> grooves;      // preferred rhythmic styles, most typical first

    // Descriptive (analysis output, shown in UI / exported in kit.json)
    std::string query;                // what the user typed
    std::string displayName;          // matched artist / genre name
    std::string source;               // "offline-db", "genre-blend", "heuristic", "online", "custom"
    float confidence = 0.0f;          // 0..1, how well the query was understood
    std::string summary;              // short prose about the sound
    std::vector<std::string> genres;
    std::vector<std::string> signatures; // characteristic elements / techniques

    float& operator[](Desc x) { return d[(size_t)x]; }
    float operator[](Desc x) const { return d[(size_t)x]; }

    // The n most pronounced descriptors (value-weighted), as labels.
    std::vector<std::pair<std::string, float>> topTags(int n) const;
};

SonicProfile neutralProfile();
SonicProfile blendProfiles(const SonicProfile& a, const SonicProfile& b, float t);

// "How closely should the kit follow the artist?" — per aspect, 0..1.
struct Influence {
    float drums = 1.0f;      // drum & cymbal character
    float tonal = 1.0f;      // bass + melodic timbre, key & scale
    float texture = 1.0f;    // atmospheres, sound design, vocals
    float processing = 1.0f; // distortion / lo-fi / glitch / vintage processing
    float groove = 1.0f;     // tempo, swing, rhythmic style
    float variation = 0.5f;  // how different variations within a category are
};

enum class Domain { Drums, Tonal, Texture };
// The effective profile for a domain after applying influence (lerp from neutral).
SonicProfile effectiveProfile(const SonicProfile& p, const Influence& inf, Domain dom);

// Natural-language direction ("make it darker, more industrial, less reverb").
struct PromptChange { std::string what; float delta; };
struct PromptResult {
    std::vector<PromptChange> changes;
    std::vector<std::string> unrecognised; // words we could not map (shown to user honestly)
    float bpmDelta = 0.0f;
    bool any() const { return !changes.empty() || bpmDelta != 0.0f; }
};
PromptResult applyPrompt(SonicProfile& p, const std::string& prompt);

std::string toLower(const std::string& s);
std::string trim(const std::string& s);

} // namespace sdna
