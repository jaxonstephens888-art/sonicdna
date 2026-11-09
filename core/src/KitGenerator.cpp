#include "sdna/KitGenerator.h"

#include <chrono>
#include <cstdio>
#include <ctime>
#include <random>
#include <thread>

namespace sdna {

EngineRegistry::EngineRegistry() {
    engines = { makeDrumEngine(), makeCymbalEngine(), makeBassEngine(), makeMelodicEngine(), makeFxEngine(), makeVocalEngine() };
}
void EngineRegistry::add(EnginePtr e, bool preferred) {
    if (preferred) engines.insert(engines.begin(), std::move(e));
    else engines.push_back(std::move(e));
}
EnginePtr EngineRegistry::engineFor(Category c) const {
    for (auto& e : engines) if (e->handles(c)) return e;
    return nullptr;
}

Domain domainFor(Category c) {
    switch (categoryInfo(c).group) {
        case Group::Drums: case Group::Cymbals: return Domain::Drums;
        case Group::Bass: case Group::Melodic: return Domain::Tonal;
        default: return Domain::Texture;
    }
}

uint64_t freshSeed() {
    std::random_device rd;
    uint64_t s = ((uint64_t)rd() << 32) ^ rd() ^ (uint64_t)std::chrono::high_resolution_clock::now().time_since_epoch().count();
    return s ? s : 1;
}

std::string nowIso8601() {
    std::time_t t = std::time(nullptr);
    char buf[32]; std::tm tmv{};
#if defined(_WIN32)
    gmtime_s(&tmv, &t);
#else
    gmtime_r(&t, &tmv);
#endif
    std::strftime(buf, sizeof buf, "%Y-%m-%dT%H:%M:%SZ", &tmv);
    return buf;
}

static double maxSeconds(Category c) {
    switch (categoryInfo(c).group) {
        case Group::Drums: return c == Category::Fill ? 4.5 : 2.5;
        case Group::Cymbals: return 4.0;
        case Group::Bass: return 4.0;
        case Group::Melodic: return 8.0;
        default: return 9.0;
    }
}

void finishSample(Buffer& b, Category c, const KitDNA& dna, bool isLoop, uint64_t seed) {
    if (b.empty()) return;
    // Sanitise
    for (auto& ch : b.ch) for (auto& s : ch) if (!std::isfinite(s)) s = 0.0f;
    Group g = categoryInfo(c).group;
    static const float satScale[kNumGroups] = { 1.0f, 0.55f, 0.85f, 0.45f, 0.6f, 0.5f };
    static const float crushScale[kNumGroups] = { 1.0f, 0.9f, 0.5f, 0.8f, 0.8f, 0.8f };
    fx::removeDC(b);
    if (g != Group::Bass && std::abs(dna.tone) > 0.02f) fx::tilt(b, dna.tone * 0.6f);
    fx::normalize(b, -3.0f);
    SatType st = dna.satType == SatType::Bitcrush ? SatType::Soft : dna.satType;
    fx::saturate(b, st, dna.satAmount * satScale[(int)g] * 0.6f, 0.85f);
    if (dna.crushMix > 0.0f) fx::bitcrush(b, dna.crushBits, dna.crushDownsample, dna.crushMix * crushScale[(int)g]);
    if (dna.tapeWow > 0.05f && g != Group::Drums && g != Group::Cymbals && !isLoop) fx::tapeWow(b, dna.tapeWow * 0.6f, 0.55f, seed ^ 0x7A9E);
    if ((g == Group::Drums || g == Group::Cymbals) && std::abs(dna.transientDb) > 0.5f) fx::transient(b, dna.transientDb, 0.0f);
    // Follow a band-limited (lo-fi) reference's top end.
    if (dna.ref.valid && dna.ref.bandwidthHz > 1000.0f) {
        double hz = dna.ref.bandwidthHz * 1.05 + (1.0 - dna.ref.weight) * 4000.0;
        if (hz < b.sampleRate * 0.45) fx::lowpass(b, hz, 0.6);
    }
    int cap = (int)(maxSeconds(c) * b.sampleRate);
    if (b.length() > cap) { b.resize(cap); fx::fadeOut(b, 0.05); }
    if (!isLoop) {
        fx::normalize(b, -1.0f);
        fx::trimTail(b, -66.0f, 0.04, g == Group::Drums || g == Group::Cymbals ? 0.004 : 0.02);
        fx::fadeIn(b, g == Group::Drums || g == Group::Cymbals ? 0.0002 : 0.002);
    }
    if (dna.noiseFloorDb > -100.0f) {
        float lvl = dna.noiseFloorDb + (g == Group::Drums || g == Group::Cymbals ? -8.0f : 0.0f);
        fx::normalize(b, -1.0f);
        fx::addNoise(b, lvl, 1, seed ^ 0x51, dna.vinyl);
        if (!isLoop) fx::fadeOut(b, 0.004);
    }
    fx::softLimit(b, -0.3f);
    fx::normalize(b, -1.0f);
}

KitGenerator::KitGenerator() : reg(std::make_shared<EngineRegistry>()) {}
KitGenerator::KitGenerator(std::shared_ptr<EngineRegistry> r) : reg(std::move(r)) {}

static std::string hex8(uint64_t v) { char b[17]; std::snprintf(b, sizeof b, "%08llx", (unsigned long long)(v & 0xFFFFFFFFull)); return b; }

SamplePtr KitGenerator::generateOne(const Kit& kit, Category c, int variant, uint64_t seed) const {
    auto eng = reg->engineFor(c);
    if (!eng) return nullptr;
    SonicProfile eff = effectiveProfile(kit.profile, kit.influence, domainFor(c));
    GenContext ctx{ kit.dna, eff, kit.influence.variation, variant, seed, kit.sampleRate };
    GenResult res = eng->generate(c, ctx);
    if (!res.skipFinish) finishSample(res.audio, c, kit.dna, res.isLoop, seed);
    auto s = std::make_shared<Sample>();
    s->category = c; s->variant = variant; s->seed = seed;
    s->engine = eng->id(); s->recipe = res.recipe; s->rootNote = res.rootNote;
    s->isLoop = res.isLoop; s->bpm = res.bpm;
    s->audio = std::move(res.audio);
    s->audio.sampleRate = kit.sampleRate;
    char num[16]; std::snprintf(num, sizeof num, "%02d", variant + 1);
    s->name = std::string(categoryInfo(c).single) + " " + num + (res.recipe.empty() ? "" : " \xC2\xB7 " + res.recipe);
    s->id = std::string(categoryInfo(c).id) + "-" + std::to_string(variant) + "-" + hex8(seed);
    return s;
}

Kit KitGenerator::generate(const SonicProfile& profile, const Influence& influence, const GenerateOptions& opts,
                           const std::string& kitName, const std::string& prompt) const {
    Kit kit;
    kit.seed = opts.seed ? opts.seed : freshSeed();
    kit.profile = profile;
    kit.influence = influence;
    kit.sampleRate = opts.sampleRate;
    kit.perCategory = std::clamp(opts.perCategory, 1, 20);
    kit.name = kitName.empty() ? (profile.displayName.empty() ? "Sonic DNA Kit" : profile.displayName) : kitName;
    kit.prompt = prompt;
    kit.createdAt = nowIso8601();
    // Groove influence & tonal influence affect tempo and key choice.
    SonicProfile grooveP = effectiveProfile(profile, influence, Domain::Tonal);
    kit.dna = deriveDNA(grooveP, kit.seed);
    if (opts.reference && opts.reference->valid) applyReferenceToDNA(kit.dna, *opts.reference, opts.referenceWeight);

    std::vector<Category> cats = opts.categories;
    if (cats.empty()) for (int i = 0; i < kNumCategories; ++i) cats.push_back((Category)i);

    struct Job { Category c; int v; uint64_t seed; };
    std::vector<Job> jobs;
    Rng seeds(kit.seed);
    for (auto c : cats) for (int v = 0; v < kit.perCategory; ++v) jobs.push_back({ c, v, seeds.next() });

    std::vector<SamplePtr> out(jobs.size());
    std::atomic<size_t> next{ 0 };
    std::atomic<int> done{ 0 };
    std::atomic<bool> cancelled{ false };
    unsigned hw = std::max(1u, std::thread::hardware_concurrency());
    int nThreads = opts.threads > 0 ? opts.threads : (int)std::max(1u, hw > 1 ? hw - 1 : 1);
    nThreads = std::min<int>(nThreads, (int)jobs.size());

    auto worker = [&]() {
        for (;;) {
            if (cancelled.load()) return;
            size_t i = next.fetch_add(1);
            if (i >= jobs.size()) return;
            const Job& j = jobs[i];
            out[i] = generateOne(kit, j.c, j.v, j.seed);
            int d = ++done;
            if (opts.progress && !opts.progress(d, (int)jobs.size(), out[i] ? out[i]->name : std::string())) cancelled = true;
        }
    };
    std::vector<std::thread> pool;
    for (int t = 0; t < nThreads; ++t) pool.emplace_back(worker);
    for (auto& t : pool) t.join();

    for (auto& s : out) if (s) kit.samples.push_back(s);
    if (cancelled.load()) kit.samples.clear();
    return kit;
}

} // namespace sdna
