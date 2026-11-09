// SONIC DNA — reference-audio analysis ("listen to the artist").
//
// The user supplies audio they own (finished tracks, stems, loops). We MEASURE
// it — tempo, swing, key, chords, groove grid, drum timbre, spectrum, dynamics,
// space — and use the measurements to steer synthesis. The reference audio is
// never copied into the kit, never sampled and never uploaded anywhere.
//
// All estimates are signal-processing heuristics (no machine-learning model):
// clean drum loops and stems give the most accurate drum measurements; on dense
// full mixes the drum timbre numbers are approximate.
#pragma once

#include "sdna/Dsp.h"
#include "sdna/Json.h"
#include "sdna/Kit.h"
#include "sdna/Profile.h"

#include <array>
#include <functional>
#include <string>
#include <vector>

namespace sdna {

struct DrumStats {
    int hits = 0;
    float pitchHz = 0;    // body / fundamental
    float pitchDrop = 1;  // early pitch / settled pitch (kick sweep)
    float decaySec = 0;   // approx. T60 of the hit
    float brightHz = 0;   // spectral centroid of the hit
};

struct ReferenceFeatures {
    std::string name;
    bool valid = false;
    double seconds = 0, analysedSeconds = 0;
    // Rhythm
    float bpm = 0, tempoConfidence = 0, swing = 0, onsetRate = 0, timingTightness = 0;
    std::array<std::array<float, 16>, 3> grid{}; // [kick, snare, hat][step] hit probability per bar
    int gridBars = 0;
    // Tonality
    int keyRoot = -1;
    Scale scale = Scale::Minor;
    float keyConfidence = 0;
    std::vector<int> progression; // scale degrees, one chord per bar
    float harmonicRatio = 0;
    // Spectrum & dynamics
    float centroidHz = 0, rolloffHz = 0, bandwidthHz = 0, tiltDbPerOct = 0, subRatio = 0, highRatio = 0, flatness = 0;
    float rmsDb = -120, crestDb = 0, clipRatio = 0, dynamicGapDb = 0, noiseFloorDb = -120, width = 0;
    // Drums
    DrumStats kick, snare, hat;
};

// Analyse one reference. `progress` (0..1) may return false to cancel.
ReferenceFeatures analyseReference(const Buffer& audio, const std::string& name, const std::function<bool(float)>& progress = {});

// Merge several references into one (medians / means, groove grids averaged).
ReferenceFeatures combineReferences(const std::vector<ReferenceFeatures>& refs);

// Turn measurements into a SonicProfile. `base` is the artist/genre profile (may be
// null); `weight` 0..1 is how much the audio overrides it. Measured facts (tempo,
// key, swing) are taken from the audio whenever weight > 0.
SonicProfile profileFromReference(const ReferenceFeatures& f, const SonicProfile* base, float weight);

// Write tempo/key/chords/drum targets/groove into a kit's DNA.
void applyReferenceToDNA(KitDNA& dna, const ReferenceFeatures& f, float weight);

// Human-readable facts for the UI / CLI.
std::vector<std::string> describeReference(const ReferenceFeatures& f);

Json featuresToJson(const ReferenceFeatures& f);
ReferenceFeatures featuresFromJson(const Json& j);

} // namespace sdna
