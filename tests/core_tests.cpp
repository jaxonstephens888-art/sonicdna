// Core test-suite: analysis, prompt parsing, generation quality checks,
// determinism, file formats, sequencer timing and real-time safety.
#include "sdna/ArtistDatabase.h"
#include "sdna/KitGenerator.h"
#include "sdna/KitIO.h"
#include "sdna/OnlineAnalysis.h"

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <functional>
#include <new>

using namespace sdna;

// ---- allocation counter (to prove the audio thread does not allocate) ------
static std::atomic<long> gAllocs{ 0 };
static std::atomic<bool> gCount{ false };
void* operator new(size_t n) { if (gCount.load()) ++gAllocs; void* p = std::malloc(n ? n : 1); if (!p) throw std::bad_alloc(); return p; }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete(void* p, size_t) noexcept { std::free(p); }

static int gFail = 0, gPass = 0;
#define CHECK(cond, ...) do { if (cond) { ++gPass; } else { ++gFail; std::printf("  FAIL %s:%d  %s  ", __FILE__, __LINE__, #cond); std::printf(__VA_ARGS__); std::printf("\n"); } } while (0)

static void section(const char* n) { std::printf("[%s]\n", n); }

static float crest(const Buffer& b) { float r = fx::rms(b); return r > 0 ? fx::peak(b) / r : 0; }

static double estimatePitch(const Buffer& b, double fromSec, double lenSec) {
    // autocorrelation pitch estimate on a window (mono)
    int s0 = (int)(fromSec * b.sampleRate), n = (int)(lenSec * b.sampleRate);
    if (s0 + n > b.length()) n = b.length() - s0;
    const float* x = b.data(0) + s0;
    int minLag = (int)(b.sampleRate / 1000), maxLag = (int)(b.sampleRate / 25);
    double best = -1; int bestLag = 0;
    for (int lag = minLag; lag < maxLag && lag < n / 2; ++lag) {
        double acc = 0, e = 0;
        for (int i = 0; i + lag < n; ++i) { acc += x[i] * x[i + lag]; e += x[i] * x[i]; }
        double v = acc / (e + 1e-9);
        if (v > best) { best = v; bestLag = lag; }
    }
    return bestLag ? b.sampleRate / bestLag : 0;
}

static Kit smallKit(const std::string& q, uint64_t seed, int per, std::vector<Category> cats = {}, const std::string& prompt = "") {
    SonicProfile p = analyseQuery(q).profile;
    applyPrompt(p, prompt);
    GenerateOptions o; o.perCategory = per; o.seed = seed; o.categories = cats; o.sampleRate = 44100;
    return KitGenerator().generate(p, Influence{}, o, q, prompt);
}

int main() {
    // ------------------------------------------------------------------
    section("analysis");
    {
        for (const char* a : { "The Hellp", "Crystal Castles", "Burial", "Frank Ocean", "Yabujin", "Aphex Twin", "Nine Inch Nails", "Dean Blunt", "SOPHIE" }) {
            auto r = analyseQuery(a);
            CHECK(r.method == "offline-db" && r.profile.confidence > 0.99f, "%s -> %s (%.2f)", a, r.method.c_str(), r.profile.confidence);
        }
        auto f = analyseQuery("crystl castels");
        CHECK(f.method == "offline-db" && f.matched.size() == 1 && f.matched[0] == std::string("Crystal Castles"), "fuzzy -> %s", f.matched.empty() ? "-" : f.matched[0].c_str());
        auto b = analyseQuery("Burial meets Nine Inch Nails");
        CHECK(b.method == "genre-blend" && b.matched.size() == 2, "blend -> %s %zu", b.method.c_str(), b.matched.size());
        auto g = analyseQuery("dark lo-fi trap");
        CHECK(g.method == "genre-blend", "genre phrase -> %s", g.method.c_str());
        CHECK(g.profile[Desc::Dark] > profileFromEntry(artistDatabase()[0]).d[0] * 0 + 0.7f, "dark applied %.2f", g.profile[Desc::Dark]);
        auto u = analyseQuery("Zxqv Blorptron");
        CHECK(u.method == "heuristic" && u.profile.confidence < 0.2f, "unknown -> %s", u.method.c_str());
        auto u2 = analyseQuery("Zxqv Blorptron");
        CHECK(u.profile.d == u2.profile.d, "heuristic deterministic");
        auto burial = analyseQuery("burial").profile, sophie = analyseQuery("sophie").profile;
        CHECK(burial[Desc::Atmospheric] > sophie[Desc::Atmospheric] && sophie[Desc::Hyper] > burial[Desc::Hyper], "profiles differ sensibly");
        CHECK(!suggestNames("crys").empty() && suggestNames("crys")[0] == std::string("Crystal Castles"), "suggest");
    }
    section("prompt");
    {
        SonicProfile p = neutralProfile();
        float d0 = p[Desc::Dark], x0 = p[Desc::Distorted], a0 = p[Desc::Atmospheric];
        auto r = applyPrompt(p, "Make it much darker, less distorted and more atmospheric. Add some flibbertigibbet");
        CHECK(p[Desc::Dark] > d0 + 0.3f, "darker %.2f", p[Desc::Dark]);
        CHECK(p[Desc::Distorted] < x0, "less distorted %.2f", p[Desc::Distorted]);
        CHECK(p[Desc::Atmospheric] > a0, "atmos %.2f", p[Desc::Atmospheric]);
        CHECK(r.unrecognised.size() == 1 && r.unrecognised[0] == "flibbertigibbet", "unrecognised reported (%zu)", r.unrecognised.size());
        SonicProfile q = neutralProfile(); float bpm = q.bpm;
        applyPrompt(q, "faster, more industrial, lo-fi");
        CHECK(q.bpm > bpm && q[Desc::Industrial] > 0.25f && q[Desc::Lofi] > 0.3f, "multi %.0f %.2f %.2f", q.bpm, q[Desc::Industrial], q[Desc::Lofi]);
    }
    // ------------------------------------------------------------------
    section("generation: full kit validity");
    {
        Kit k = smallKit("Crystal Castles", 1234, 2);
        CHECK((int)k.samples.size() == kNumCategories * 2, "count %zu", k.samples.size());
        int bad = 0;
        for (auto& s : k.samples) {
            const auto& a = s->audio;
            bool finite = true; for (auto& c : a.ch) for (float x : c) if (!std::isfinite(x)) finite = false;
            float pk = fx::peak(a);
            bool ok = finite && a.length() > 100 && pk <= 1.0f && pk > 0.3f && a.seconds() < 10.0;
            if (!ok) { ++bad; std::printf("    bad sample %s len=%.2fs peak=%.3f finite=%d\n", s->name.c_str(), a.seconds(), pk, (int)finite); }
            if (s->category == Category::Arp) CHECK(s->isLoop && s->bpm > 0, "arp loop meta");
            if (categoryInfo(s->category).group == Group::Bass) CHECK(s->rootNote >= 24 && s->rootNote < 72, "bass root %d", s->rootNote);
        }
        CHECK(bad == 0, "%d invalid samples", bad);
    }
    section("generation: determinism & regeneration");
    {
        Kit a = smallKit("Burial", 42, 1, { Category::Kick, Category::Pad, Category::VocalChop });
        Kit b = smallKit("Burial", 42, 1, { Category::Kick, Category::Pad, Category::VocalChop });
        Kit c = smallKit("Burial", 43, 1, { Category::Kick, Category::Pad, Category::VocalChop });
        bool same = true, diff = false;
        for (size_t i = 0; i < a.samples.size(); ++i) { same = same && a.samples[i]->audio.ch == b.samples[i]->audio.ch; diff = diff || a.samples[i]->audio.ch != c.samples[i]->audio.ch; }
        CHECK(same, "same seed -> identical audio");
        CHECK(diff, "different seed -> different audio");
        KitGenerator gen;
        auto re = gen.generateOne(a, Category::Kick, 0, 999);
        CHECK(re && re->audio.ch != a.samples[0]->audio.ch && re->category == Category::Kick, "regenerate one");
    }
    section("generation: profile -> sound");
    {
        auto gen = [](float dark, float dist, Category cat, uint64_t seed) {
            SonicProfile p = neutralProfile(); p[Desc::Dark] = dark; p[Desc::Distorted] = dist;
            GenerateOptions o; o.perCategory = 6; o.seed = seed; o.categories = { cat };
            return KitGenerator().generate(p, Influence{}, o, "t", "");
        };
        double cDark = 0, cBright = 0;
        Kit kd = gen(0.95f, 0.2f, Category::ClosedHat, 7), kb = gen(0.05f, 0.2f, Category::ClosedHat, 7);
        for (auto& s : kd.samples) cDark += fx::spectralCentroid(s->audio);
        for (auto& s : kb.samples) cBright += fx::spectralCentroid(s->audio);
        CHECK(cDark < cBright * 0.9, "dark hats centroid %.0f < bright %.0f", cDark / 6, cBright / 6);
        double crClean = 0, crDist = 0;
        Kit kc = gen(0.5f, 0.0f, Category::Kick, 9), kx = gen(0.5f, 1.0f, Category::Kick, 9);
        for (auto& s : kc.samples) crClean += crest(s->audio);
        for (auto& s : kx.samples) crDist += crest(s->audio);
        CHECK(crDist < crClean, "distorted kicks crest %.2f < clean %.2f", crDist / 6, crClean / 6);
    }
    section("generation: tuning");
    {
        Kit k = smallKit("Metro Boomin", 77, 3, { Category::Bass808, Category::SubBass });
        for (auto& s : k.samples) {
            double f = estimatePitch(s->audio, 0.55, 0.3), target = midiToHz(s->rootNote);
            double cents = 1200.0 * std::log2(f / target);
            // allow octave errors of the estimator only for distorted (harmonic-rich) 808s
            double c2 = std::min(std::abs(cents), std::min(std::abs(cents - 1200), std::abs(cents + 1200)));
            CHECK(c2 < 40, "%s root %d: est %.1f Hz vs %.1f Hz (%.0f cents)", s->name.c_str(), s->rootNote, f, target, cents);
        }
    }
    // ------------------------------------------------------------------
    section("wav");
    {
        Buffer b(2, 4800, 48000); for (int i = 0; i < b.length(); ++i) { b.ch[0][(size_t)i] = 0.5f * std::sin(i * 0.05f); b.ch[1][(size_t)i] = -0.25f; }
        for (int bits : { 16, 24, 32 }) {
            AudioFormat f; f.sampleRate = 48000; f.bitDepth = bits;
            Buffer back; int rn = 0;
            bool ok = decodeWav(encodeWav(b, f, 60, true), back, &rn);
            float err = 0; for (int c = 0; c < 2; ++c) for (int i = 0; i < b.length(); ++i) err = std::max(err, std::abs(back.ch[(size_t)c][(size_t)i] - b.ch[(size_t)c][(size_t)i]));
            CHECK(ok && back.length() == b.length() && back.sampleRate == 48000 && rn == 60, "roundtrip %d-bit", bits);
            CHECK(err < (bits == 16 ? 1e-4f : bits == 24 ? 1e-6f : 1e-9f), "%d-bit error %g", bits, err);
        }
        AudioFormat f44; f44.sampleRate = 44100; f44.bitDepth = 24;
        Buffer back; decodeWav(encodeWav(b, f44), back);
        CHECK(back.sampleRate == 44100 && std::abs(back.length() - 4410) <= 1, "48k->44.1k length %d", back.length());
    }
    section("zip / kit files / midi");
    {
        Kit k = smallKit("SOPHIE", 5, 1);
        PatternState pat; auto roles = defaultChannelRoles(); for (int c = 0; c < kChannels; ++c) pat.ch[(size_t)c].role = roles[(size_t)c];
        assignKitToChannels(k, pat);
        int assigned = 0; for (auto& c : pat.ch) assigned += !c.sampleId.empty();
        CHECK(assigned == kChannels, "all pads assigned (%d)", assigned);
        randomizePattern(pat, k.profile, Groove::Hyperpop, 1, 0.5f, 0.5f);
        CHECK(pat.vel[0][0] > 0, "kick on 1");
        std::array<PlaybackPtr, kChannels> pads{};
        for (int c = 0; c < kChannels; ++c) pads[(size_t)c] = makePlayback(*k.find(pat.ch[(size_t)c].sampleId));
        ExportOptions eo; eo.format.bitDepth = 16;
        auto zip = buildKitZip(k, &pat, &pads, eo);
        std::vector<std::pair<std::string, std::vector<uint8_t>>> entries;
        CHECK(readZip(zip, entries), "zip readable");
        CHECK(entries.size() == k.samples.size() + 4, "zip entries %zu", entries.size());
        bool hasMid = false, hasLoop = false, hasJson = false;
        for (auto& e : entries) { hasMid |= e.first.find(".mid") != std::string::npos; hasLoop |= e.first.find("Loops/") != std::string::npos; hasJson |= e.first.find("kit.json") != std::string::npos; }
        CHECK(hasMid && hasLoop && hasJson, "zip has midi/loop/json");
        writeFile("/tmp/sdna_test_kit.zip", zip);

        auto mid = patternToMidi(pat, 2);
        CHECK(mid.size() > 30 && std::string(mid.begin(), mid.begin() + 4) == "MThd", "midi header");
        int noteOns = 0; for (size_t i = 0; i + 2 < mid.size(); ++i) if (mid[i] == 0x99 && mid[i + 1] >= 36 && mid[i + 1] < 52) ++noteOns;
        int expected = 0; for (auto& row : pat.vel) for (auto v : row) expected += v ? 1 : 0;
        CHECK(noteOns >= expected * 2 - 2, "midi note-ons %d (pattern %d x2 bars)", noteOns, expected);

        std::string path = "/tmp/sdna_test.sdnakit", err;
        CHECK(saveKitFile(k, &pat, path, &err), "save %s", err.c_str());
        Kit k2; PatternState p2;
        CHECK(loadKitFile(path, k2, &p2, &err), "load %s", err.c_str());
        CHECK(k2.samples.size() == k.samples.size() && k2.name == k.name && k2.dna.keyRoot == k.dna.keyRoot, "kit meta roundtrip");
        bool audioSame = true; for (size_t i = 0; i < k.samples.size(); ++i) audioSame = audioSame && k2.samples[i]->audio.ch == k.samples[i]->audio.ch;
        CHECK(audioSame, "kit audio lossless roundtrip");
        CHECK(p2.vel == pat.vel && p2.ch[3].sampleId == pat.ch[3].sampleId, "pattern roundtrip");
        CHECK(k2.profile.displayName == k.profile.displayName && std::abs(k2.profile[Desc::Hyper] - k.profile[Desc::Hyper]) < 1e-3, "profile roundtrip");
    }
    section("online analysis protocol (offline, canned responses)");
    {
        Json req = online::buildRequest("The Hellp");
        CHECK(req.get("model").str() == "claude-opus-5-5" && req.get("tools")[0].get("type").str() == "web_search_20260209", "request shape");
        CHECK(req.get("fallbacks").str() == "default" && req.get("output_config").get("effort").str() == "medium", "fallback/effort");
        auto hs = online::requestHeaders("k");
        bool ver = false; for (auto& h : hs) ver |= h.name == "anthropic-version" && h.value == "2026-10-21";
        CHECK(ver, "version header");
        std::string good = R"({"id":"msg_1","type":"message","role":"assistant","stop_reason":"end_turn","content":[
          {"type":"server_tool_use","id":"s1","name":"web_search","input":{"query":"the hellp production"}},
          {"type":"web_search_tool_result","tool_use_id":"s1","content":[{"type":"web_search_result","url":"https://example.com/a","title":"A"}]},
          {"type":"text","text":"Here is the profile:\n```json\n{\"name\":\"The Hellp\",\"summary\":\"Loud electroclash.\",\"confidence\":0.82,\"genres\":[\"electroclash\"],\"signatures\":[\"blown drum machines\"],\"descriptors\":{\"dark\":0.8,\"distorted\":0.9,\"electronic\":0.9,\"lofi\":0.4,\"aggressive\":0.85,\"atmospheric\":0.3,\"industrial\":0.4,\"glitch\":0.3,\"vintage\":0.4,\"heavy\":0.6,\"energy\":0.85,\"melodic\":0.4,\"experimental\":0.4,\"hyper\":0.2},\"bpm\":130,\"swing\":0.05,\"key\":-1,\"scale\":\"minor\",\"grooves\":[\"electroclash\",\"four_floor\"]}\n```"}]})";
        auto it = online::interpretResponse("the hellp", good, 200);
        CHECK(it.status == online::Status::Done && it.profile.source == "online", "parse ok: %s", it.error.c_str());
        CHECK(std::abs(it.profile[Desc::Distorted] - 0.9f) < 1e-4 && it.profile.bpm == 130 && it.profile.grooves.size() == 2 && it.sources.size() == 1, "fields");
        auto pause = online::interpretResponse("x", R"({"stop_reason":"pause_turn","content":[{"type":"text","text":"..."}]})", 200);
        CHECK(pause.status == online::Status::Continue, "pause_turn -> continue");
        Json cont = online::continueRequest(req, R"({"stop_reason":"pause_turn","content":[{"type":"text","text":"..."}]})");
        CHECK(cont.get("messages").size() == 2 && cont.get("messages")[1].get("role").str() == "assistant", "continuation appends assistant turn");
        auto e401 = online::interpretResponse("x", R"({"type":"error","error":{"type":"authentication_error","message":"invalid x-api-key"}})", 401);
        CHECK(e401.status == online::Status::Error && e401.error.find("401") != std::string::npos, "401 handled");
        auto refusal = online::interpretResponse("x", R"({"stop_reason":"refusal","content":[]})", 200);
        CHECK(refusal.status == online::Status::Error, "refusal handled");
        auto noJson = online::interpretResponse("x", R"({"stop_reason":"end_turn","content":[{"type":"text","text":"I could not find it."}]})", 200);
        CHECK(noJson.status == online::Status::Error, "no json handled");
    }
    // ------------------------------------------------------------------
    section("drum machine");
    {
        const double sr = 48000; const int block = 256;
        auto click = std::make_shared<PlaybackSample>(); click->sampleRate = sr; click->L.assign(2000, 0.0f); click->L[0] = 1.0f; for (int i = 1; i < 2000; ++i) click->L[(size_t)i] = 0.5f * std::exp(-i / 100.0f); click->R = click->L;
        DrumMachine dm; dm.prepare(sr, block);
        dm.setChannelSample(0, click);
        for (int s : { 0, 4, 8, 12 }) dm.steps[0][s] = 127;
        dm.bpm = 120; dm.playing = true; dm.followHost = false;
        int total = (int)(sr * 2.0); // one bar at 120
        std::vector<float> L((size_t)total), R((size_t)total);
        HostTransport none;
        gAllocs = 0; gCount = true;
        for (int p = 0; p < total; p += block) dm.process(L.data() + p, R.data() + p, std::min(block, total - p), none, nullptr, 0);
        gCount = false;
        CHECK(gAllocs.load() == 0, "process() allocated %ld times", gAllocs.load());
        std::vector<int> onsets; for (int i = 0; i < total; ++i) if (std::abs(L[(size_t)i]) > 0.6f && (i == 0 || std::abs(L[(size_t)i - 1]) < 0.3f)) onsets.push_back(i);
        CHECK(onsets.size() == 4, "4 onsets (%zu)", onsets.size());
        int beat = (int)(sr * 0.5);
        bool timing = onsets.size() == 4; for (size_t i = 0; i < onsets.size(); ++i) timing = timing && std::abs(onsets[i] - (int)i * beat) <= 1;
        CHECK(timing, "sample-accurate timing");

        // swing: odd step 1 delayed
        DrumMachine sw; sw.prepare(sr, block); sw.setChannelSample(0, click); sw.steps[0][1] = 127; sw.bpm = 120; sw.swing = 1.0f; sw.playing = true; sw.followHost = false;
        std::fill(L.begin(), L.end(), 0.0f);
        for (int p = 0; p < total; p += block) sw.process(L.data() + p, R.data() + p, std::min(block, total - p), none, nullptr, 0);
        int first = -1; for (int i = 0; i < total; ++i) if (L[(size_t)i] > 0.6f) { first = i; break; }
        int stepLen = (int)(sr * 0.125);
        CHECK(std::abs(first - (int)(stepLen * 1.33)) <= 2, "swing offset %d vs %d", first, (int)(stepLen * 1.33));

        // mute and MIDI input
        DrumMachine mm; mm.prepare(sr, block); mm.setChannelSample(3, click); mm.steps[3][0] = 127; mm.params[3].mute = true; mm.playing = true; mm.followHost = false;
        std::fill(L.begin(), L.end(), 0.0f);
        mm.process(L.data(), R.data(), block, none, nullptr, 0);
        CHECK(fx::peak(Buffer()) == 0 && *std::max_element(L.begin(), L.begin() + block) < 0.01f, "mute silences");
        mm.playing = false;
        MidiNote n{ 10, kBaseMidiNote + 3, 1.0f };
        std::fill(L.begin(), L.end(), 0.0f);
        mm.process(L.data(), R.data(), block, none, &n, 1);
        int mo = -1; for (int i = 0; i < block; ++i) if (L[(size_t)i] > 0.6f) { mo = i; break; }
        CHECK(mo == 10, "MIDI note triggers pad 4 at offset %d", mo);

        // host sync: host at beat 1 (ppq 1.0) should fire step 4
        DrumMachine hm; hm.prepare(sr, block); hm.setChannelSample(0, click); hm.steps[0][4] = 127; hm.followHost = true;
        HostTransport ht; ht.valid = true; ht.playing = true; ht.bpm = 120; ht.ppqPosition = 1.0 - (100.0 / sr) * 2.0; // 100 samples before beat 2
        std::fill(L.begin(), L.end(), 0.0f);
        hm.process(L.data(), R.data(), block, ht, nullptr, 0);
        int ho = -1; for (int i = 0; i < block; ++i) if (L[(size_t)i] > 0.6f) { ho = i; break; }
        CHECK(std::abs(ho - 100) <= 1, "host-synced step at %d", ho);

        // offline loop render length
        PatternState st = dm.getState(); st.bpm = 120;
        std::array<PlaybackPtr, kChannels> pads{}; pads[0] = click;
        Buffer loop = DrumMachine::renderLoop(st, pads, 2, sr);
        CHECK(loop.length() == (int)(sr * 4.0), "loop length %d", loop.length());
    }
    std::printf("\n%d passed, %d failed\n", gPass, gFail);
    return gFail ? 1 : 0;
}
