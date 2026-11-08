// SONIC DNA — kit model: categories, samples, kit-wide "DNA" (shared character).
#pragma once

#include "sdna/Dsp.h"
#include "sdna/Profile.h"

#include <memory>
#include <string>
#include <vector>

namespace sdna {

enum class Group : int { Drums, Cymbals, Bass, Melodic, SoundDesign, Vocal, Count };

enum class Category : int {
    Kick, Snare, Clap, Rim, Tom, Perc, Fill,
    ClosedHat, OpenHat, PedalHat, Ride, Crash, Shaker,
    Bass808, SubBass, BassShot, DistBass, SynthBass,
    Lead, Pluck, Keys, Piano, Bell, Strings, Pad, Arp,
    Riser, Impact, Transition, Glitch, Noise, Atmosphere, Texture, Reverse,
    VocalTexture, VocalChop, Chant, FormantTexture, VocalFx,
    Count
};
constexpr int kNumCategories = (int)Category::Count;
constexpr int kNumGroups = (int)Group::Count;

struct CategoryInfo {
    Category cat;
    Group group;
    const char* id;      // "kick"
    const char* label;   // "Kicks"
    const char* single;  // "Kick"
};
const CategoryInfo& categoryInfo(Category c);
const char* groupLabel(Group g);
const char* groupFolder(Group g);
bool categoryFromId(const std::string& id, Category& out);
std::vector<Category> categoriesInGroup(Group g);

// Kit-wide shared character. Derived once from the profile so every sample in
// a kit shares tuning, space, saturation flavour and degradation — this is what
// makes a generated kit feel like *one* record rather than random sounds.
struct KitDNA {
    uint64_t seed = 1;
    int keyRoot = 9;            // pitch class
    Scale scale = Scale::Minor;
    float bpm = 120;
    float swing = 0;
    SatType satType = SatType::Soft;
    float satAmount = 0.2f;     // 0..1
    int crushBits = 16;
    int crushDownsample = 1;
    float crushMix = 0.0f;
    float roomSize = 0.5f, roomDamp = 0.5f, roomWet = 0.15f;
    float predelayMs = 12.0f;
    float tapeWow = 0.0f;
    float noiseFloorDb = -90.0f;
    bool vinyl = false;
    float tone = 0.0f;          // -1 dark .. +1 bright
    float drumTune = 0.0f;      // semitones
    float transientDb = 0.0f;
    float fmBias = 0.0f;        // 0 harmonic .. 1 inharmonic
    int kickNote = 33;          // MIDI note of kick / 808 fundamental
    std::vector<int> progression; // scale degrees for chordal material

    int noteInKey(int degree, int octave) const; // MIDI note of scale degree (can be negative/over 7)
};

KitDNA deriveDNA(const SonicProfile& p, uint64_t seed);

struct Sample {
    std::string id;           // stable unique id within a kit
    std::string name;         // "Kick 03 · Hardstyle"
    Category category = Category::Kick;
    int variant = 0;
    uint64_t seed = 0;
    std::string engine;       // engine id that produced it
    std::string recipe;       // recipe within engine
    int rootNote = -1;        // MIDI root for tonal samples, -1 = unpitched
    bool isLoop = false;
    float bpm = 0.0f;
    Buffer audio;

    std::string fileStem(const std::string& kitSlug) const;
};
using SamplePtr = std::shared_ptr<Sample>;

struct Kit {
    std::string name;
    std::string prompt;
    SonicProfile profile;     // the profile after prompt modifiers
    Influence influence;
    KitDNA dna;
    uint64_t seed = 1;
    double sampleRate = 44100.0;
    int perCategory = 8;
    std::string createdAt;
    std::vector<SamplePtr> samples;

    std::vector<SamplePtr> inCategory(Category c) const;
    SamplePtr find(const std::string& id) const;
    std::string slug() const;
};

std::string slugify(const std::string& s);

} // namespace sdna
