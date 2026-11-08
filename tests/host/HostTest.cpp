// Loads the built SONIC DNA VST3 exactly like a DAW would (JUCE VST3 hosting)
// and exercises it: MIDI, host transport sync, state recall, block sizes,
// sample rates, real-time performance and editor lifecycle.
#include <JuceHeader.h>

static int fails = 0, passes = 0;
#define CHECK(c, msg) do { if (c) ++passes; else { ++fails; std::cout << "  FAIL: " << msg << "\n"; } } while (0)

struct TestPlayHead : juce::AudioPlayHead {
    bool playing = false; double ppq = 0, bpm = 120;
    juce::Optional<PositionInfo> getPosition() const override {
        PositionInfo p; p.setIsPlaying(playing); p.setPpqPosition(ppq); p.setBpm(bpm); p.setTimeInSeconds(ppq * 60.0 / bpm);
        return p;
    }
};

static float peakOf(const juce::AudioBuffer<float>& b) { float p = 0; for (int c = 0; c < b.getNumChannels(); ++c) p = std::max(p, b.getMagnitude(c, 0, b.getNumSamples())); return p; }
static bool finite(const juce::AudioBuffer<float>& b) { for (int c = 0; c < b.getNumChannels(); ++c) for (int i = 0; i < b.getNumSamples(); ++i) if (!std::isfinite(b.getSample(c, i))) return false; return true; }

int main(int argc, char** argv) {
    juce::ScopedJuceInitialiser_GUI init;
    if (argc < 3) { std::cout << "usage: host_test <plugin.vst3> <kit.sdnakit>\n"; return 2; }
    juce::File pluginFile{ juce::String(argv[1]) }, kitFile{ juce::String(argv[2]) };
    juce::AudioPluginFormatManager fm; fm.addFormat(new juce::VST3PluginFormat());
    juce::OwnedArray<juce::PluginDescription> descs;
    juce::VST3PluginFormat vst3;
    vst3.findAllTypesForFile(descs, pluginFile.getFullPathName());
    CHECK(descs.size() == 1, "found plugin description");
    if (descs.isEmpty()) return 1;
    auto& d = *descs[0];
    std::cout << "Plugin: " << d.name << " by " << d.manufacturerName << "  instrument=" << d.isInstrument << "  category=" << d.category << "\n";
    CHECK(d.name == "SONIC DNA" && d.isInstrument, "name / instrument flag");
    juce::String err;
    auto inst = fm.createPluginInstance(d, 48000, 512, err);
    CHECK(inst != nullptr, "instantiate: " + err);
    if (!inst) return 1;
    CHECK(inst->acceptsMidi(), "accepts MIDI");
    CHECK(inst->getTotalNumOutputChannels() == 2, "stereo out");
    int named = 0; for (auto* p : inst->getParameters()) if (!p->getName(32).startsWith("MIDI CC")) { std::cout << "  param: " << p->getName(32) << "\n"; ++named; }
    CHECK(named >= 2, "automatable parameters exposed");

    TestPlayHead ph; inst->setPlayHead(&ph);
    inst->prepareToPlay(48000, 512);
    juce::AudioBuffer<float> buf(2, 512); juce::MidiBuffer midi;
    buf.clear(); inst->processBlock(buf, midi);
    CHECK(peakOf(buf) == 0.0f, "silent with no kit");

    // State that points at a generated kit + a four-on-the-floor kick pattern.
    juce::String state = R"({"format":"sonic-dna-state","version":1,"query":"test","prompt":"","master":-6,"kit_file":")" + kitFile.getFullPathName().replace("\\", "\\\\") + R"(","pattern":{"length":16,"bpm":120,"swing":0,"channels":[)";
    // Use the kit's own pattern sample ids by loading the kit's json via the plugin: here we only set steps; sample ids come from the kit file pattern.
    state << R"({"role":"kick","steps":[110,0,0,0,110,0,0,0,110,0,0,0,110,0,0,0]})";
    state << "]}}";
    // A host stores VST3 state in JUCE's envelope: <VST3PluginState><IComponent>base64(plugin data)</IComponent></...>
    {
        juce::XmlElement xml("VST3PluginState");
        juce::MemoryBlock raw(state.toRawUTF8(), state.getNumBytesAsUTF8());
        xml.createNewChildElement("IComponent")->addTextElement(raw.toBase64Encoding());
        juce::MemoryBlock env; juce::AudioProcessor::copyXmlToBinary(xml, env);
        inst->setStateInformation(env.getData(), (int)env.getSize());
    }
    // The kit loads on a worker thread, then hops to the message thread: pump messages until it is in.
    juce::String saved;
    auto tStart = juce::Time::getMillisecondCounterHiRes();
    for (int i = 0; i < 120; ++i) {
        juce::MessageManager::getInstance()->runDispatchLoopUntil(250);
        midi.addEvent(juce::MidiMessage::noteOn(10, 36, (juce::uint8)120), 0);
        buf.clear(); inst->processBlock(buf, midi); midi.clear();
        if (peakOf(buf) > 0.001f) break;
    }
    std::cout << "  kit recalled in " << (juce::Time::getMillisecondCounterHiRes() - tStart) / 1000.0 << " s\n";
    {
        juce::MemoryBlock mb; inst->getStateInformation(mb);
        if (auto xml = juce::AudioProcessor::getXmlFromBinary(mb.getData(), (int)mb.getSize()))
            if (auto* c = xml->getChildByName("IComponent")) { juce::MemoryBlock d; d.fromBase64Encoding(c->getAllSubText()); saved = d.toString(); }
    }
    CHECK(saved.contains(kitFile.getFileName()), "state references the kit file after recall");

    // MIDI note C1 (36) -> pad 1
    midi.addEvent(juce::MidiMessage::noteOn(10, 36, (juce::uint8)120), 100);
    buf.clear(); inst->processBlock(buf, midi); midi.clear();
    float pk = 0; for (int i = 0; i < 20; ++i) { pk = std::max(pk, peakOf(buf)); buf.clear(); inst->processBlock(buf, midi); }
    CHECK(pk > 0.01f, "MIDI note produces audio (peak " + juce::String(pk, 3) + ")");

    // Host transport drives the sequencer (needs steps; kit json default pattern was empty -> program via MIDI-less test of silence then playing state)
    ph.playing = true; ph.ppq = 0; ph.bpm = 120;
    double ppqPerBlock = 512.0 / 48000.0 * 2.0;
    int blocks = (int)(4.0 / ppqPerBlock); // one bar
    float seqPeak = 0; bool fin = true;
    for (int b = 0; b < blocks; ++b) { buf.clear(); inst->processBlock(buf, midi); seqPeak = std::max(seqPeak, peakOf(buf)); fin = fin && finite(buf); ph.ppq += ppqPerBlock; }
    ph.playing = false;
    std::cout << "  host-synced bar peak: " << seqPeak << "\n";
    CHECK(fin, "finite output while host plays");

    // Block sizes and sample rates
    for (double sr : { 44100.0, 48000.0, 96000.0 }) {
        for (int bs : { 1, 17, 64, 333, 1024, 2048 }) {
            inst->releaseResources(); inst->prepareToPlay(sr, bs);
            juce::AudioBuffer<float> b(2, bs); bool ok = true;
            for (int k = 0; k < 50; ++k) { if (k % 7 == 0) midi.addEvent(juce::MidiMessage::noteOn(10, 36 + (k % 16), (juce::uint8)100), 0); b.clear(); inst->processBlock(b, midi); midi.clear(); ok = ok && finite(b) && peakOf(b) <= 1.0f; }
            CHECK(ok, "finite & <=0 dBFS at " + juce::String(sr) + " Hz / block " + juce::String(bs));
        }
    }
    // Real-time performance: 16 pads hammering + sequencer
    inst->releaseResources(); inst->prepareToPlay(48000, 256);
    juce::AudioBuffer<float> b(2, 256);
    ph.playing = true; ph.ppq = 0;
    auto t0 = juce::Time::getMillisecondCounterHiRes();
    int nBlocks = (int)(10.0 * 48000 / 256);
    for (int k = 0; k < nBlocks; ++k) { for (int n = 0; n < 4; ++n) midi.addEvent(juce::MidiMessage::noteOn(10, 36 + ((k + n) % 16), (juce::uint8)110), n * 50); b.clear(); inst->processBlock(b, midi); midi.clear(); ph.ppq += 256.0 / 48000.0 * 2.0; }
    double ms = juce::Time::getMillisecondCounterHiRes() - t0;
    std::cout << "  10 s of audio (4 hits/block, 48 kHz, 256 frames) processed in " << ms << " ms  -> " << (10000.0 / ms) << "x real time\n";
    CHECK(ms < 2000, "real-time performance");

    // Editor lifecycle
    for (int i = 0; i < 2; ++i) {
        std::unique_ptr<juce::AudioProcessorEditor> ed(inst->createEditorIfNeeded());
        CHECK(ed != nullptr, "editor created");
        if (ed) { ed->setSize(1300, 850); juce::MessageManager::getInstance()->runDispatchLoopUntil(400); inst->editorBeingDeleted(ed.get()); }
    }
    inst->releaseResources();
    inst.reset();
    std::cout << passes << " passed, " << fails << " failed\n";
    return fails ? 1 : 0;
}
