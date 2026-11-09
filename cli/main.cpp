// sonicdna — command-line front end for the SONIC DNA core.
//
//   sonicdna analyze  "Burial" [--prompt "darker"]
//   sonicdna generate "Crystal Castles" [--prompt ...] [--per 8] [--sr 48000] [--bits 24]
//                     [--seed N] [--out DIR] [--zip FILE] [--kit FILE.sdnakit] [--categories kick,snare]
//   sonicdna list
#include "sdna/ArtistDatabase.h"
#include "sdna/AudioAnalysis.h"
#include "sdna/KitGenerator.h"
#include "sdna/KitIO.h"

#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <map>
#include <sstream>

using namespace sdna;
namespace fs = std::filesystem;

static void usage() {
    std::puts("SONIC DNA command line\n"
              "  sonicdna analyze \"<artist|genre>\" [--prompt \"...\"]\n"
              "  sonicdna generate \"<artist|genre>\" [--prompt \"...\"] [--per N] [--sr 44100|48000] [--bits 16|24|32]\n"
              "                    [--seed N] [--variation 0..1] [--out DIR] [--zip FILE.zip] [--kit FILE.sdnakit] [--categories a,b]\n"
              "                    [--ref track1.wav,track2.wav] [--ref-weight 0..1]   (learn from your own reference audio)\n"
              "  sonicdna list");
}

static void printProfile(const SonicProfile& p) {
    std::printf("  %-14s %s  (source: %s, confidence %.0f%%)\n", "Reference", p.displayName.c_str(), p.source.c_str(), p.confidence * 100);
    std::printf("  %-14s %s\n", "Summary", p.summary.c_str());
    std::string tags; for (auto& t : p.topTags(6)) tags += (tags.empty() ? "" : " · ") + t.first;
    std::printf("  %-14s %s\n", "Profile", tags.c_str());
    std::printf("  %-14s %.0f BPM, swing %.2f, %s\n", "Groove", p.bpm, p.swing, p.grooves.empty() ? "-" : grooveLabel(p.grooves[0]));
    for (int i = 0; i < kNumDesc; ++i) {
        int n = (int)std::round(p.d[(size_t)i] * 30);
        std::printf("    %-13s %s%s %3.0f\n", descLabel((Desc)i), std::string((size_t)n, '#').c_str(), std::string((size_t)(30 - n), '.').c_str(), p.d[(size_t)i] * 100);
    }
}

int main(int argc, char** argv) {
    if (argc < 2) { usage(); return 1; }
    std::string cmd = argv[1];
    if (cmd == "list") {
        for (auto& e : artistDatabase()) std::printf("%s%s\n", e.isGenre ? "[genre] " : "", e.name);
        return 0;
    }
    if (argc < 3) { usage(); return 1; }
    std::string query = argv[2];
    std::map<std::string, std::string> opt;
    for (int i = 3; i + 1 < argc; i += 2) opt[argv[i]] = argv[i + 1];
    auto get = [&](const char* k, const std::string& d) { auto it = opt.find(k); return it == opt.end() ? d : it->second; };

    AnalysisResult ar = analyseQuery(query);
    SonicProfile prof = ar.profile;
    // Reference audio: measured locally, never copied into the kit.
    std::vector<ReferenceFeatures> refs;
    ReferenceFeatures combined;
    float refWeight = std::stof(get("--ref-weight", "0.7"));
    {
        std::string list = get("--ref", ""); std::stringstream ss(list); std::string path;
        while (std::getline(ss, path, ',')) {
            if (path.empty()) continue;
            std::vector<uint8_t> bytes; Buffer audio; std::string err;
            if (!readFile(path, bytes) || !decodeWav(bytes, audio, nullptr, &err)) { std::fprintf(stderr, "cannot read reference %s %s (WAV only in the CLI)\n", path.c_str(), err.c_str()); continue; }
            auto f = analyseReference(audio, fs::path(path).filename().string());
            std::printf("  Reference %s (%.0fs)\n", f.name.c_str(), f.seconds);
            for (auto& line : describeReference(f)) std::printf("      %s\n", line.c_str());
            refs.push_back(f);
        }
        if (!refs.empty()) {
            combined = combineReferences(refs);
            if (query == "-") prof = profileFromReference(combined, nullptr, 1.0f);
            else prof = profileFromReference(combined, &prof, refWeight);
        }
    }
    std::string prompt = get("--prompt", "");
    PromptResult pr = applyPrompt(prof, prompt);
    std::printf("SONIC DNA · %s\n  %s\n", query.c_str(), ar.message.c_str());
    if (!prompt.empty()) {
        std::printf("  Direction:     \"%s\"\n", prompt.c_str());
        for (auto& c : pr.changes) std::printf("                 %s %+.0f\n", c.what.c_str(), c.delta * 100);
        if (pr.bpmDelta != 0) std::printf("                 Tempo %+.0f BPM\n", pr.bpmDelta);
        if (!pr.unrecognised.empty()) { std::string u; for (auto& w : pr.unrecognised) u += w + " "; std::printf("                 (not understood: %s)\n", u.c_str()); }
    }
    printProfile(prof);
    if (cmd == "analyze") { std::puts(profileToJson(prof).dump(2).c_str()); return 0; }
    if (cmd != "generate") { usage(); return 1; }

    GenerateOptions go;
    go.perCategory = std::stoi(get("--per", "8"));
    go.sampleRate = std::stod(get("--sr", "44100"));
    go.seed = std::stoull(get("--seed", "0"));
    std::string cats = get("--categories", "");
    if (!cats.empty()) {
        std::stringstream ss(cats); std::string c;
        while (std::getline(ss, c, ',')) { Category cc; if (categoryFromId(c, cc)) go.categories.push_back(cc); else std::fprintf(stderr, "unknown category '%s'\n", c.c_str()); }
    }
    Influence inf; inf.variation = std::stof(get("--variation", "0.5"));
    if (combined.valid) { go.reference = &combined; go.referenceWeight = refWeight; }
    int lastPct = -1;
    go.progress = [&](int done, int total, const std::string&) {
        int pct = done * 100 / total;
        if (pct / 10 != lastPct / 10) { std::fprintf(stderr, "\r  generating %3d%% (%d/%d)", pct, done, total); lastPct = pct; }
        return true;
    };
    auto t0 = std::chrono::steady_clock::now();
    KitGenerator gen;
    Kit kit = gen.generate(prof, inf, go, prof.displayName, prompt);
    double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    std::fprintf(stderr, "\n");
    std::printf("  Generated %zu samples in %.1fs — key %s %s, %.0f BPM, seed %llu\n", kit.samples.size(), secs,
                noteName(kit.dna.keyRoot), scaleName(kit.dna.scale), kit.dna.bpm, (unsigned long long)kit.seed);

    PatternState pat;
    auto roles = defaultChannelRoles();
    for (int c = 0; c < kChannels; ++c) pat.ch[(size_t)c].role = roles[(size_t)c];
    assignKitToChannels(kit, pat);
    randomizePattern(pat, kit.profile, pickGroove(kit.profile, kit.seed), kit.seed, 0.5f, kit.influence.variation, &kit.dna.ref);
    pat.ch[6].choke = pat.ch[5].choke = pat.ch[7].choke = 1;
    std::array<PlaybackPtr, kChannels> pads{};
    for (int c = 0; c < kChannels; ++c) if (auto s = kit.find(pat.ch[(size_t)c].sampleId)) pads[(size_t)c] = makePlayback(*s);

    ExportOptions eo;
    eo.format.sampleRate = go.sampleRate;
    eo.format.bitDepth = std::stoi(get("--bits", "24"));
    std::string out = get("--out", ""), zip = get("--zip", ""), kitFile = get("--kit", "");
    if (!out.empty()) {
        for (auto& s : kit.samples) {
            fs::path p = fs::path(out) / samplePath(kit, *s);
            fs::create_directories(p.parent_path());
            writeFile(p.string(), encodeWav(s->audio, eo.format, s->rootNote, s->isLoop));
        }
        fs::create_directories(fs::path(out) / "Patterns"); fs::create_directories(fs::path(out) / "Loops");
        writeFile((fs::path(out) / "Patterns" / "pattern.mid").string(), patternToMidi(pat, 2));
        Buffer loop = DrumMachine::renderLoop(pat, pads, 2, eo.format.sampleRate);
        fx::normalize(loop, -1);
        writeFile((fs::path(out) / "Loops" / "drum_loop.wav").string(), encodeWav(loop, eo.format, -1, true));
        writeFile((fs::path(out) / "kit.json").string(), [&] { auto s = kitToJson(kit, &pat, "").dump(2); return std::vector<uint8_t>(s.begin(), s.end()); }());
        std::printf("  Wrote WAVs to %s\n", out.c_str());
    }
    if (!zip.empty()) { exportKitZip(kit, &pat, &pads, eo, zip); std::printf("  Wrote %s\n", zip.c_str()); }
    if (!kitFile.empty()) { std::string err; if (saveKitFile(kit, &pat, kitFile, &err)) std::printf("  Saved kit %s\n", kitFile.c_str()); else std::printf("  Save failed: %s\n", err.c_str()); }
    return 0;
}
