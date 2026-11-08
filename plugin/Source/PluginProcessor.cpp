#include "PluginProcessor.h"
#include "PluginEditor.h"

using namespace sdna;

juce::AudioProcessorValueTreeState::ParameterLayout SonicDNAProcessor::createLayout() {
    juce::AudioProcessorValueTreeState::ParameterLayout l;
    l.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{ "master", 1 }, "Master", juce::NormalisableRange<float>(-36.0f, 6.0f, 0.1f), -6.0f, juce::AudioParameterFloatAttributes().withLabel("dB")));
    l.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{ "swing", 1 }, "Swing", juce::NormalisableRange<float>(0.0f, 1.0f, 0.001f), 0.0f));
    return l;
}

SonicDNAProcessor::SonicDNAProcessor()
    : juce::AudioProcessor(BusesProperties().withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      params(*this, nullptr, "SonicDNA", createLayout()) {
    masterParam = params.getRawParameterValue("master");
    swingParam = params.getRawParameterValue("swing");
    auto roles = defaultChannelRoles();
    for (int c = 0; c < kChannels; ++c) machine.params[c].role = (int)roles[(size_t)c];
    profile = neutralProfile();
    influence = Influence{};
    gc.dm = &machine;
    gc.startTimer(1000);
    machine.prepare(44100, 512);
}

SonicDNAProcessor::~SonicDNAProcessor() {
    alive->store(false);
    cancelFlag = true;
    gc.stopTimer();
    analyser.cancel();
    pool.removeAllJobs(true, 20000);
}

bool SonicDNAProcessor::isBusesLayoutSupported(const BusesLayout& l) const {
    auto out = l.getMainOutputChannelSet();
    return out == juce::AudioChannelSet::stereo() || out == juce::AudioChannelSet::mono();
}

void SonicDNAProcessor::prepareToPlay(double sr, int block) {
    currentRate = sr;
    machine.prepare(sr, juce::jmax(32, block));
    scratchR.assign((size_t)juce::jmax(32, block) * 2, 0.0f);
}

void SonicDNAProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) {
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    HostTransport ht;
    if (auto* ph = getPlayHead()) {
        if (auto pos = ph->getPosition()) {
            ht.valid = true;
            ht.playing = pos->getIsPlaying();
            ht.ppqPosition = pos->getPpqPosition().orFallback(0.0);
            ht.bpm = pos->getBpm().orFallback(120.0);
        }
    }
    hostPlaying.store(ht.valid && ht.playing);
    if (ht.valid) hostTempo.store(ht.bpm);

    MidiNote notes[256]; int nn = 0;
    for (const auto meta : midi) {
        auto m = meta.getMessage();
        if (m.isNoteOn() && nn < 256) notes[nn++] = { meta.samplePosition, m.getNoteNumber(), m.getFloatVelocity() };
    }
    machine.masterDb.store(masterParam->load());
    float sw = swingParam->load();
    if (sw != lastSwingParam) { machine.swing.store(sw); lastSwingParam = sw; }

    float* L = buffer.getWritePointer(0);
    float* R = buffer.getNumChannels() > 1 ? buffer.getWritePointer(1) : nullptr;
    if (!R) {
        if ((int)scratchR.size() < n) { buffer.clear(); return; } // host exceeded prepared size: stay silent rather than allocate
        R = scratchR.data();
    }
    machine.process(L, R, n, ht, notes, nn);
    if (buffer.getNumChannels() == 1) for (int i = 0; i < n; ++i) L[i] = 0.5f * (L[i] + R[i]);
    for (int c = 2; c < buffer.getNumChannels(); ++c) buffer.clear(c, 0, n);
    midi.clear();
}

juce::AudioProcessorEditor* SonicDNAProcessor::createEditor() { return new SonicDNAEditor(*this); }

// ---------------------------------------------------------------------------
void SonicDNAProcessor::postToMessageThread(std::function<void()> fn) {
    auto token = alive;
    juce::MessageManager::callAsync([token, fn = std::move(fn)] { if (token->load()) fn(); });
}

void SonicDNAProcessor::setStatus(const juce::String& s, bool error) {
    status = s; statusIsError = error;
    listeners.call([](Listener& l) { l.statusChanged(); });
}

void SonicDNAProcessor::analyse(const juce::String& q, bool online) {
    query = q.trim();
    if (online) {
        if (services::Settings::get().apiKey().isEmpty()) { setStatus("Online analysis needs an Anthropic API key (Settings). Using offline analysis.", true); online = false; }
        else if (analyser.busy()) return;
    }
    // Offline analysis is instant and always runs first so the UI is never empty.
    auto r = analyseQuery(query.toStdString());
    profile = r.profile;
    analysisMessage = r.message;
    listeners.call([](Listener& l) { l.profileChanged(); });
    if (!online) { setStatus(r.message); return; }
    setStatus("Researching \"" + query + U8("\" online… (requires internet)"));
    auto token = alive;
    analyser.start(query, [this, token](bool ok, SonicProfile p, juce::String msg) {
        if (!token->load()) return;
        if (ok) {
            profile = p;
            analysisMessage = msg;
            listeners.call([](Listener& l) { l.profileChanged(); });
            setStatus(msg + U8(" Profile updated — press Generate."));
        } else {
            setStatus("Online analysis failed: " + msg + " Kept the offline profile.", true);
        }
        listeners.call([](Listener& l) { l.statusChanged(); });
    });
    listeners.call([](Listener& l) { l.statusChanged(); });
}

void SonicDNAProcessor::profileEdited() {
    if (profile.source != "custom" && profile.source.find("edited") == std::string::npos) profile.source += "+edited";
    listeners.call([](Listener& l) { l.profileChanged(); });
}

SonicProfile SonicDNAProcessor::profileWithPrompt(PromptResult* res) const {
    SonicProfile p = profile;
    auto r = applyPrompt(p, prompt.toStdString());
    if (res) *res = r;
    return p;
}

juce::String SonicDNAProcessor::generationLabel() const { const juce::ScopedLock sl(labelLock); return genLabel; }

void SonicDNAProcessor::generate() {
    if (generating.exchange(true)) return;
    cancelFlag = false;
    genProgress = 0;
    PromptResult pr;
    SonicProfile p = profileWithPrompt(&pr);
    if (query.isEmpty() && profile.displayName.empty()) p.displayName = "Untitled";
    Influence inf = influence;
    GenerateOptions opts;
    opts.perCategory = services::Settings::get().perCategory();
    opts.sampleRate = services::Settings::get().sampleRate();
    std::string name = p.displayName.empty() ? query.toStdString() : p.displayName;
    std::string pr_ = prompt.toStdString();
    setStatus("Generating " + juce::String(opts.perCategory * kNumCategories) + U8(" samples…"));
    auto token = alive;
    pool.addJob([this, token, p, inf, opts, name, pr_]() mutable {
        opts.progress = [this](int done, int total, const std::string& label) {
            genProgress.store((float)done / (float)total);
            { const juce::ScopedLock sl(labelLock); genLabel = juce::String(label); }
            return !cancelFlag.load();
        };
        auto t0 = juce::Time::getMillisecondCounterHiRes();
        auto k = std::make_shared<Kit>(generator.generate(p, inf, opts, name, pr_));
        double secs = (juce::Time::getMillisecondCounterHiRes() - t0) / 1000.0;
        bool cancelled = cancelFlag.load() || k->samples.empty();
        // Auto-save (in the background, before handing over).
        juce::File saved;
        if (!cancelled && services::Settings::get().autoSave()) {
            auto dir = services::Settings::get().kitsFolder();
            dir.createDirectory();
            saved = dir.getNonexistentChildFile(juce::String(k->slug()) + "_" + juce::String::toHexString((juce::int64)(k->seed & 0xFFFFFF)), ".sdnakit");
            std::string err;
            if (!saveKitFile(*k, nullptr, saved.getFullPathName().toStdString(), &err)) saved = juce::File();
        }
        postToMessageThread([this, k, cancelled, secs, saved] {
            generating = false;
            if (cancelled) { setStatus("Generation cancelled."); return; }
            kitFile = saved;
            setKit(k, nullptr);
            juce::String msg = "Generated " + juce::String((int)k->samples.size()) + " original samples in " + juce::String(secs, 1) + U8(" s · ")
                + juce::String(noteName(k->dna.keyRoot)) + " " + scaleName(k->dna.scale) + U8(" · ") + juce::String((int)k->dna.bpm) + " BPM";
            if (saved.exists()) msg << U8(" · saved to ") << saved.getFileName();
            setStatus(msg);
        });
        (void)token;
    });
    listeners.call([](Listener& l) { l.statusChanged(); });
}

void SonicDNAProcessor::cancelGeneration() { cancelFlag = true; }

void SonicDNAProcessor::setKit(std::shared_ptr<Kit> k, const PatternState* keepPattern, bool notify) {
    kit = std::move(k);
    PatternState st;
    if (keepPattern) {
        st = *keepPattern;
        // Any pad whose sample is not in this kit falls back to the default for its role.
        PatternState fallback = st;
        assignKitToChannels(*kit, fallback);
        for (int c = 0; c < kChannels; ++c)
            if (!kit->find(st.ch[(size_t)c].sampleId)) st.ch[(size_t)c].sampleId = fallback.ch[(size_t)c].sampleId;
    }
    else {
        st = machine.getState();
        auto roles = defaultChannelRoles();
        for (int c = 0; c < kChannels; ++c) { st.ch[(size_t)c].role = roles[(size_t)c]; }
        assignKitToChannels(*kit, st);
        Groove g = pickGroove(kit->profile, kit->seed);
        sdna::randomizePattern(st, kit->profile, g, kit->seed, 0.5f, kit->influence.variation);
        st.bpm = kit->dna.bpm; st.swing = kit->dna.swing;
        st.ch[5].choke = st.ch[6].choke = st.ch[7].choke = 1;
    }
    machine.setState(st);
    if (auto* p = params.getParameter("swing")) p->setValueNotifyingHost(p->convertTo0to1(st.swing));
    applyPadsFromState(st);
    if (notify) {
        listeners.call([](Listener& l) { l.kitChanged(); });
        listeners.call([](Listener& l) { l.padsChanged(); });
    }
}

void SonicDNAProcessor::applyPadsFromState(const PatternState& st) {
    for (int c = 0; c < kChannels; ++c) {
        SamplePtr s = kit ? kit->find(st.ch[(size_t)c].sampleId) : nullptr;
        machine.setChannelSample(c, s ? makePlayback(*s) : nullptr);
    }
}

void SonicDNAProcessor::regenerateSample(const std::string& id) {
    if (!kit) return;
    auto old = kit->find(id);
    if (!old) return;
    auto k = kit;
    Category cat = old->category; int variant = old->variant;
    uint64_t seed = freshSeed();
    pool.addJob([this, k, cat, variant, seed, id] {
        auto s = generator.generateOne(*k, cat, variant, seed);
        postToMessageThread([this, k, s, id] {
            if (kit != k || !s) return; // kit changed meanwhile
            for (auto& x : kit->samples) if (x->id == id) { x = s; break; }
            // Update pads that used the old sample.
            for (int c = 0; c < kChannels; ++c) {
                auto cur = machine.channelSample(c);
                if (cur && cur->sampleId == id) machine.setChannelSample(c, makePlayback(*s));
            }
            listeners.call([&](Listener& l) { l.sampleReplaced(id, s); });
            listeners.call([](Listener& l) { l.padsChanged(); });
            setStatus("Regenerated " + juce::String(s->name));
        });
    });
}

void SonicDNAProcessor::regenerateCategory(Category c) {
    if (!kit) return;
    for (auto& s : kit->inCategory(c)) regenerateSample(s->id);
}

void SonicDNAProcessor::previewSample(const SamplePtr& s) { if (s) machine.previewSample(makePlayback(*s)); }

void SonicDNAProcessor::assignPad(int ch, const SamplePtr& s) {
    if (ch < 0 || ch >= kChannels || !s) return;
    machine.setChannelSample(ch, makePlayback(*s));
    machine.params[ch].role = (int)s->category;
    listeners.call([](Listener& l) { l.padsChanged(); });
    setStatus(juce::String(s->name) + U8(" → pad ") + juce::String(ch + 1));
}

void SonicDNAProcessor::randomizePattern(Groove g, float density) {
    PatternState st = machine.getState();
    SonicProfile p = kit ? kit->profile : profileWithPrompt();
    uint64_t seed = freshSeed();
    float bpm = st.bpm;
    sdna::randomizePattern(st, p, g, seed, density, influence.variation);
    st.bpm = bpm; // keep user's tempo
    for (int c = 0; c < kChannels; ++c) for (int k = 0; k < kSteps; ++k) machine.steps[c][k].store(st.vel[(size_t)c][(size_t)k]);
    listeners.call([](Listener& l) { l.padsChanged(); });
}

void SonicDNAProcessor::clearPattern() {
    for (auto& row : machine.steps) for (auto& s : row) s.store(0);
    listeners.call([](Listener& l) { l.padsChanged(); });
}

// ---------------------------------------------------------------------------
AudioFormat SonicDNAProcessor::exportFormat() const {
    AudioFormat f;
    f.sampleRate = services::Settings::get().sampleRate();
    f.bitDepth = services::Settings::get().exportBits();
    return f;
}

bool SonicDNAProcessor::exportSampleTo(const SamplePtr& s, const juce::File& f) {
    if (!s) return false;
    bool ok = writeFile(f.getFullPathName().toStdString(), encodeWav(s->audio, exportFormat(), s->rootNote, s->isLoop));
    setStatus(ok ? "Exported " + f.getFileName() : "Could not write " + f.getFullPathName(), !ok);
    return ok;
}

juce::File SonicDNAProcessor::writeTempSample(const SamplePtr& s) {
    if (!s || !kit) return {};
    auto f = services::tempExportFolder().getChildFile(juce::String(s->fileStem(kit->slug())) + ".wav");
    if (!f.existsAsFile()) writeFile(f.getFullPathName().toStdString(), encodeWav(s->audio, exportFormat(), s->rootNote, s->isLoop));
    return f;
}

void SonicDNAProcessor::exportKitZipAsync(const juce::File& f) {
    if (!kit) return;
    Kit snapshot = *kit; // copies shared_ptrs only
    PatternState st = machine.getState();
    std::array<PlaybackPtr, kChannels> pads; for (int c = 0; c < kChannels; ++c) pads[(size_t)c] = machine.channelSample(c);
    ExportOptions eo; eo.format = exportFormat();
    setStatus(U8("Exporting kit…"));
    pool.addJob([this, snapshot, st, pads, eo, f] {
        bool ok = exportKitZip(snapshot, &st, &pads, eo, f.getFullPathName().toStdString());
        postToMessageThread([this, ok, f, n = snapshot.samples.size()] {
            setStatus(ok ? "Exported " + juce::String((int)n) + " samples + MIDI + loop to " + f.getFileName() : "Export failed: " + f.getFullPathName(), !ok);
        });
    });
}

bool SonicDNAProcessor::exportMidi(const juce::File& f, int bars) {
    auto data = patternToMidi(machine.getState(), bars);
    bool ok = writeFile(f.getFullPathName().toStdString(), data);
    setStatus(ok ? "Exported MIDI pattern " + f.getFileName() : "Could not write MIDI file", !ok);
    return ok;
}

void SonicDNAProcessor::exportLoopAsync(const juce::File& f, int bars) {
    PatternState st = machine.getState();
    std::array<PlaybackPtr, kChannels> pads; for (int c = 0; c < kChannels; ++c) pads[(size_t)c] = machine.channelSample(c);
    auto fmt = exportFormat();
    pool.addJob([this, st, pads, fmt, f, bars] {
        Buffer loop = DrumMachine::renderLoop(st, pads, bars, fmt.sampleRate, true);
        fx::normalize(loop, -1.0f);
        bool ok = writeFile(f.getFullPathName().toStdString(), encodeWav(loop, fmt, -1, true));
        postToMessageThread([this, ok, f] { setStatus(ok ? "Rendered loop to " + f.getFileName() : "Could not write loop", !ok); });
    });
}

void SonicDNAProcessor::saveKitAsync(const juce::File& f) {
    if (!kit) return;
    Kit snapshot = *kit;
    PatternState st = machine.getState();
    pool.addJob([this, snapshot, st, f] {
        std::string err;
        bool ok = saveKitFile(snapshot, &st, f.getFullPathName().toStdString(), &err);
        postToMessageThread([this, ok, f, err] { if (ok) kitFile = f; setStatus(ok ? "Saved kit " + f.getFileName() : "Save failed: " + juce::String(err), !ok); });
    });
}

void SonicDNAProcessor::loadKitAsync(const juce::File& f) {
    setStatus("Loading " + f.getFileName() + U8("…"));
    pool.addJob([this, f] {
        auto k = std::make_shared<Kit>();
        PatternState st; st.length = 0;
        std::string err;
        bool ok = loadKitFile(f.getFullPathName().toStdString(), *k, &st, &err);
        postToMessageThread([this, ok, k, st, f, err] {
            if (!ok) { setStatus("Could not load kit: " + juce::String(err), true); return; }
            kitFile = f;
            profile = k->profile;
            query = juce::String(k->profile.query.empty() ? k->profile.displayName : k->profile.query);
            prompt = juce::String(k->prompt);
            influence = k->influence;
            listeners.call([](Listener& l) { l.profileChanged(); });
            setKit(k, st.length > 0 ? &st : nullptr);
            setStatus("Loaded " + f.getFileName() + " (" + juce::String((int)k->samples.size()) + " samples)");
        });
    });
}

// ---------------------------------------------------------------------------
void SonicDNAProcessor::getStateInformation(juce::MemoryBlock& dest) {
    Json j = Json::object();
    j["format"] = "sonic-dna-state"; j["version"] = 1;
    j["query"] = query.toStdString(); j["prompt"] = prompt.toStdString();
    j["profile"] = profileToJson(profile);
    Json inf = Json::object();
    inf["drums"] = influence.drums; inf["tonal"] = influence.tonal; inf["texture"] = influence.texture;
    inf["processing"] = influence.processing; inf["groove"] = influence.groove; inf["variation"] = influence.variation;
    j["influence"] = inf;
    j["pattern"] = patternToJson(machine.getState());
    j["kit_file"] = kitFile.getFullPathName().toStdString();
    j["master"] = (double)masterParam->load();
    j["swing"] = (double)swingParam->load();
    auto s = j.dump(0);
    dest.replaceAll(s.data(), s.size());
}

void SonicDNAProcessor::setStateInformation(const void* data, int size) {
    std::string s((const char*)data, (size_t)size);
    Json j = Json::parse(s);
    if (j.get("format").str() != "sonic-dna-state") return;
    query = juce::String(j.get("query").str()); prompt = juce::String(j.get("prompt").str());
    profile = profileFromJson(j.get("profile"));
    const Json& inf = j.get("influence");
    influence.drums = (float)inf.get("drums").num(1); influence.tonal = (float)inf.get("tonal").num(1); influence.texture = (float)inf.get("texture").num(1);
    influence.processing = (float)inf.get("processing").num(1); influence.groove = (float)inf.get("groove").num(1); influence.variation = (float)inf.get("variation").num(0.5);
    PatternState st = patternFromJson(j.get("pattern"));
    if (auto* p = params.getParameter("master")) p->setValueNotifyingHost(p->convertTo0to1((float)j.get("master").num(-6)));
    machine.setState(st);
    juce::File f(juce::String(j.get("kit_file").str()));
    listeners.call([](Listener& l) { l.profileChanged(); });
    if (f.existsAsFile()) {
        pool.addJob([this, f, st] {
            auto k = std::make_shared<Kit>();
            PatternState ignore; std::string err;
            bool ok = loadKitFile(f.getFullPathName().toStdString(), *k, &ignore, &err);
            postToMessageThread([this, ok, k, st, f, err] {
                if (!ok) { setStatus("Could not reload kit " + f.getFileName() + ": " + juce::String(err), true); return; }
                kitFile = f;
                setKit(k, &st);
                setStatus("Restored " + f.getFileName());
            });
        });
    } else if (f.getFullPathName().isNotEmpty()) {
        setStatus("Saved kit file not found: " + f.getFullPathName(), true);
    }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new SonicDNAProcessor(); }
