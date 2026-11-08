// SONIC DNA — kit generation orchestration.
#pragma once

#include "sdna/Engine.h"

#include <atomic>
#include <functional>

namespace sdna {

struct GenerateOptions {
    int perCategory = 8;            // 1..20 (5–20 recommended)
    double sampleRate = 44100.0;
    uint64_t seed = 0;              // 0 = pick a fresh random seed
    std::vector<Category> categories; // empty = all categories
    int threads = 0;                // 0 = hardware concurrency - 1
    // progress(done, total, label) — return false to cancel. Called from worker threads.
    std::function<bool(int, int, const std::string&)> progress;
};

class KitGenerator {
public:
    KitGenerator();
    explicit KitGenerator(std::shared_ptr<EngineRegistry> registry);

    // `profile` should already include prompt modifiers.
    Kit generate(const SonicProfile& profile, const Influence& influence, const GenerateOptions& opts,
                 const std::string& kitName, const std::string& prompt) const;

    // Render a single sample (used for "regenerate" on one slot).
    SamplePtr generateOne(const Kit& kit, Category c, int variant, uint64_t seed) const;

    const EngineRegistry& registry() const { return *reg; }

private:
    std::shared_ptr<EngineRegistry> reg;
};

Domain domainFor(Category c);
uint64_t freshSeed();
std::string nowIso8601();

// The kit-wide finishing chain (exposed for tests).
void finishSample(Buffer& b, Category c, const KitDNA& dna, bool isLoop, uint64_t seed);

} // namespace sdna
