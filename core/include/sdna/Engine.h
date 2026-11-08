// SONIC DNA — pluggable sound-generation engines.
//
// Every category is rendered by an engine. The built-in engines are pure DSP
// (procedural synthesis + processing). The interface is deliberately small so
// that other engines — e.g. a locally-hosted neural audio model or a remote
// generation API — can be registered later and selected per category.
#pragma once

#include "sdna/Kit.h"

#include <memory>
#include <string>
#include <vector>

namespace sdna {

struct GenContext {
    const KitDNA& dna;
    const SonicProfile& profile; // effective profile for the category's domain
    float variation;             // 0..1
    int variant;                 // index within category
    uint64_t seed;
    double sampleRate;
};

struct GenResult {
    Buffer audio;
    std::string recipe;    // e.g. "Hardstyle", "Rhodes"
    int rootNote = -1;
    bool isLoop = false;
    float bpm = 0.0f;
    bool skipFinish = false; // engine already applied its own final processing
};

class SoundEngine {
public:
    virtual ~SoundEngine() = default;
    virtual std::string id() const = 0;
    virtual std::string description() const = 0;
    virtual bool handles(Category c) const = 0;
    virtual bool requiresNetwork() const { return false; }
    // Must be thread-safe (called concurrently for different samples).
    virtual GenResult generate(Category c, const GenContext& ctx) const = 0;
};

using EnginePtr = std::shared_ptr<const SoundEngine>;

// Built-in procedural engines.
EnginePtr makeDrumEngine();
EnginePtr makeCymbalEngine();
EnginePtr makeBassEngine();
EnginePtr makeMelodicEngine();
EnginePtr makeFxEngine();
EnginePtr makeVocalEngine();

class EngineRegistry {
public:
    EngineRegistry(); // registers all built-in engines
    void add(EnginePtr e, bool preferred = false);
    EnginePtr engineFor(Category c) const;
    const std::vector<EnginePtr>& all() const { return engines; }
private:
    std::vector<EnginePtr> engines;
};

} // namespace sdna
