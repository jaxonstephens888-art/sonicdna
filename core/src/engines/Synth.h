// Internal synthesis toolkit shared by the built-in engines.
#pragma once

#include "sdna/Engine.h"

#include <functional>

namespace sdna::synth {

// Profile value with per-variant jitter scaled by `variation`.
float pv(const GenContext& ctx, Rng& r, Desc d, float jitter = 0.12f);

// Choose a recipe index from weights, sharpened when variation is low.
size_t pickRecipe(const GenContext& ctx, Rng& r, std::vector<double> weights);

bool hasGroove(const SonicProfile& p, Groove g);

// --- Drum voices ----------------------------------------------------------
struct PitchedDrum {
    double f0 = 50, startMul = 4, pitchDecay = 0.04; // seconds (time constant)
    double ampT60 = 0.6, length = 0.8;
    double harmonicMix = 0.0;  // adds 2nd/3rd harmonic
    double triMix = 0.0;       // triangle vs sine body
    double clickAmt = 0.3, clickLP = 8000;
    double attackMs = 0.5;
    std::vector<std::pair<double, double>> modes; // extra membrane modes (ratio, gain)
};
Buffer pitchedDrum(double sr, const PitchedDrum& p, uint64_t seed);

Buffer noiseBurst(double sr, double length, double attackSec, double t60, int colour, double hpHz, double lpHz, uint64_t seed, double bpHz = 0, double bpQ = 1);
Buffer metalCluster(double sr, double length, double tune, double t60, uint64_t seed, int voices = 6, bool fm = false);
Buffer clapBursts(double sr, int bursts, double spacingMs, double tailT60, double bpHz, double q, uint64_t seed, double length);

// --- Tonal voices -----------------------------------------------------------
// FM operator pair. index envelope decays with indexT60.
Buffer fm2(double sr, double length, double hz, double ratio, double index, double indexT60, double ampT60, double attack = 0.002, double feedback = 0.0);
Buffer supersaw(double sr, double length, const std::vector<double>& freqs, double detuneCents, int voices, const ADSR& env, double gate, uint64_t seed, bool stereo = true);
Buffer subtractive(double sr, double length, double hz, int wave, double cutoffStart, double cutoffEnd, double filterT, double q, const ADSR& env, double gate, double glideFrom = 0, double glideT = 0.05);
Buffer karplus(double sr, double length, double hz, double decay, double brightness, uint64_t seed);
Buffer additive(double sr, double length, double hz, const std::vector<double>& ratios, const std::vector<double>& amps, const std::vector<double>& t60s, double attack, uint64_t seed);
Buffer piano(double sr, double length, const std::vector<int>& notes, double brightness, double inharm, double hammer, uint64_t seed);
Buffer electricPiano(double sr, double length, const std::vector<int>& notes, double bark, uint64_t seed);
Buffer organ(double sr, double length, const std::vector<int>& notes, const std::vector<double>& drawbars, double leslie);

// --- Voice (formant) synthesis ---------------------------------------------
struct VoiceParams {
    std::function<double(double)> pitchHz;     // pitch over time (seconds -> Hz)
    std::function<double(double)> vowelPos;    // 0..(n-1) index into vowel list over time
    std::vector<int> vowels = { 0 };           // indices into vowelTable()
    double formantShift = 1.0;                 // >1 smaller/brighter
    double breath = 0.1;                       // aspiration noise
    double vibratoHz = 5.0, vibratoCents = 20;
    double jitter = 0.01;
    double noiseOnly = 0.0;                    // 1 = whisper
    ADSR env;
    double gate = 1.0;
};
Buffer voice(double sr, double length, const VoiceParams& vp, uint64_t seed);

// --- Music helpers ------------------------------------------------------------
std::vector<int> chord(const KitDNA& dna, int degree, int octave, int size, bool spread = false);
std::string chordName(const KitDNA& dna, int degree);

// Glue that every engine may use: shared kit room.
void kitRoom(Buffer& b, const KitDNA& dna, float wetScale, double tailSec, float sizeOffset = 0.0f);

} // namespace sdna::synth
