// SONIC DNA — 16-channel drum machine / sampler with a 16-step sequencer.
//
// Real-time safety contract:
//   * process() never allocates, locks, or frees memory.
//   * Samples are handed to the audio thread as raw pointers through atomics;
//     ownership stays on the message thread (shared_ptr) and retired samples are
//     only freed by collectGarbage() once no voice references them.
//   * All parameters are std::atomic and may be written from any thread.
#pragma once

#include "sdna/Dsp.h"
#include "sdna/Kit.h"

#include <array>
#include <atomic>
#include <chrono>
#include <memory>
#include <vector>

namespace sdna {

constexpr int kChannels = 16;
constexpr int kSteps = 16;
constexpr int kVoices = 48;
constexpr int kBaseMidiNote = 36; // C1 -> pad 1 (drum-rack convention)

struct PlaybackSample {
    std::vector<float> L, R;
    double sampleRate = 44100;
    int rootNote = -1;
    std::string sampleId;
    std::string name;
    int length() const { return (int)L.size(); }
};
using PlaybackPtr = std::shared_ptr<const PlaybackSample>;
PlaybackPtr makePlayback(const Sample& s);

// Plain (non-atomic) pattern + mixer state, used for save/load/export.
struct ChannelState {
    std::string sampleId;
    Category role = Category::Kick;
    float volumeDb = 0, pan = 0, pitch = 0, tone = 0, drive = 0, reverb = 0, delay = 0, decay = 1;
    bool mute = false, solo = false;
    int choke = 0;
};
struct PatternState {
    std::array<std::array<uint8_t, kSteps>, kChannels> vel{}; // 0 = off, 1..127
    int length = 16;
    float bpm = 120, swing = 0, masterDb = 0;
    std::array<ChannelState, kChannels> ch{};
};

struct MidiNote { int offset; int note; float velocity; };

struct HostTransport {
    bool valid = false;      // host info available
    bool playing = false;
    double ppqPosition = 0;  // quarter notes
    double bpm = 120;
};

class DrumMachine {
public:
    DrumMachine();
    ~DrumMachine();

    void prepare(double sampleRate, int maxBlockSize);

    // ---- message thread -------------------------------------------------
    void setChannelSample(int ch, PlaybackPtr s);
    PlaybackPtr channelSample(int ch) const { return owned[(size_t)ch]; }
    void previewSample(PlaybackPtr s, float gain = 0.9f);
    void stopPreview();
    void collectGarbage(bool force = false);
    void setState(const PatternState& s);  // copies pattern+mixer into atomics
    PatternState getState() const;         // snapshot (sampleIds from loaded samples)

    // ---- any thread ------------------------------------------------------
    std::atomic<uint8_t> steps[kChannels][kSteps];
    struct Params {
        std::atomic<float> volumeDb{ 0 }, pan{ 0 }, pitch{ 0 }, tone{ 0 }, drive{ 0 }, reverb{ 0 }, delay{ 0 }, decay{ 1 };
        std::atomic<bool> mute{ false }, solo{ false };
        std::atomic<int> choke{ 0 };
        std::atomic<int> role{ 0 };
    } params[kChannels];
    std::atomic<float> bpm{ 120 }, swing{ 0 }, masterDb{ 0 };
    std::atomic<int> length{ 16 };
    std::atomic<bool> playing{ false };       // internal transport (standalone)
    std::atomic<bool> followHost{ true };      // plugin: lock to host transport when host plays
    std::atomic<int> currentStep{ -1 };        // for UI
    std::atomic<float> channelMeter[kChannels];
    std::atomic<float> masterMeter{ 0 };

    // Queue a pad hit from the UI (lock-free, single producer).
    void triggerFromUI(int ch, float velocity);

    // ---- audio thread ------------------------------------------------------
    void process(float* L, float* R, int numSamples, const HostTransport& host, const MidiNote* notes, int numNotes);
    void allNotesOff();

    // Offline render of N bars of the current state (uses a private instance).
    static Buffer renderLoop(const PatternState& st, const std::array<PlaybackPtr, kChannels>& samples, int bars, double sampleRate, bool wrapTail = true);

private:
    struct Voice {
        const PlaybackSample* s = nullptr;
        double pos = 0, rate = 1;
        float gain = 0, env = 1, envMul = 1, fade = 1, fadeStep = 0;
        int ch = 0;
        bool active = false;
        bool preview = false;
    };
    void startVoice(int ch, const PlaybackSample* s, float vel, int offset, bool preview, float gainOverride = 1.0f);
    void renderVoices(int start, int end);
    void runSequencer(int numSamples, const HostTransport& host, int& firstTrigOffset);

    double sr = 44100;
    int maxBlock = 0;
    Voice voices[kVoices];
    std::atomic<const PlaybackSample*> slot[kChannels];
    std::atomic<const PlaybackSample*> voiceRefs[kVoices];
    std::atomic<const PlaybackSample*> pendingPreview{ nullptr };
    std::atomic<float> pendingPreviewGain{ 0.9f };
    std::atomic<bool> stopPreviewFlag{ false };
    // UI trigger ring buffer
    static constexpr int kQ = 64;
    std::atomic<int> qWrite{ 0 }, qRead{ 0 };
    std::pair<int, float> queue[kQ];

    std::array<PlaybackPtr, kChannels> owned{};
    PlaybackPtr ownedPreview;
    struct Retired { PlaybackPtr p; std::chrono::steady_clock::time_point when; };
    std::vector<Retired> retired;

    // Buses (preallocated)
    std::vector<float> chL[kChannels], chR[kChannels], revInL, revInR, dlyInL, dlyInR, prevL, prevR;
    SVF toneL[kChannels], toneR[kChannels];
    float lastTone[kChannels];
    // Real-time reverb (Freeverb-ish) and stereo delay
    struct RtComb { std::vector<float> b; size_t i = 0; float st = 0; };
    struct RtAp { std::vector<float> b; size_t i = 0; };
    RtComb cl[8], cr[8]; RtAp al[4], ar[4];
    std::vector<float> dlBufL, dlBufR; size_t dlIdx = 0; float dlLP_L = 0, dlLP_R = 0;
    // Sequencer state
    double internalSteps = 0;   // position in 16th steps
    double lastHostSteps = -1;
    bool wasPlaying = false;
};

// Default channel roles and sample assignment from a kit.
std::array<Category, kChannels> defaultChannelRoles();
void assignKitToChannels(const Kit& kit, PatternState& st);

// Groove-aware pattern randomiser.
void randomizePattern(PatternState& st, const SonicProfile& p, Groove g, uint64_t seed, float density, float humanize);
Groove pickGroove(const SonicProfile& p, uint64_t seed);

// Standard MIDI File (type 0) for the pattern; pad n -> note kBaseMidiNote + n.
std::vector<uint8_t> patternToMidi(const PatternState& st, int bars);

} // namespace sdna
