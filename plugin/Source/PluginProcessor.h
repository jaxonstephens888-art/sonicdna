// SONIC DNA — audio processor + application model.
//
// Threads:
//   * audio thread  : processBlock -> sdna::DrumMachine::process (lock-free)
//   * message thread: all model state (kit, profile, pattern edits, UI)
//   * worker pool   : kit generation, single-sample regeneration, file export
//   * analyser      : optional online analysis (HTTPS)
#pragma once

#include <JuceHeader.h>
#include "sdna/ArtistDatabase.h"
#include "sdna/AudioAnalysis.h"
#include "sdna/DrumMachine.h"
#include "sdna/KitGenerator.h"
#include "sdna/KitIO.h"
#include "services/Services.h"

class SonicDNAProcessor : public juce::AudioProcessor {
public:
    SonicDNAProcessor();
    ~SonicDNAProcessor() override;

    // ---- juce::AudioProcessor
    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported(const BusesLayout&) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "SONIC DNA"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 3.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}
    void getStateInformation(juce::MemoryBlock&) override;
    void setStateInformation(const void*, int) override;

    // ---- model (message thread) -------------------------------------------
    struct Listener {
        virtual ~Listener() = default;
        virtual void profileChanged() {}
        virtual void kitChanged() {}
        virtual void sampleReplaced(const std::string& /*oldId*/, sdna::SamplePtr) {}
        virtual void padsChanged() {}
        virtual void statusChanged() {}
    };
    void addListener(Listener* l) { listeners.add(l); }
    void removeListener(Listener* l) { listeners.remove(l); }

    juce::String query, prompt;
    sdna::SonicProfile profile;      // effective profile (artist + reference audio + user edits), before the prompt
    sdna::SonicProfile artistProfile; // from the artist/genre analysis alone

    // ---- reference audio ("listen to the artist") ---------------------------
    // Files the user owns are decoded and MEASURED locally; audio is never copied into kits or uploaded.
    std::vector<sdna::ReferenceFeatures> references;
    float referenceWeight = 0.7f;    // 0 = ignore audio, 1 = audio decides everything it can measure
    void addReferenceFiles(const juce::Array<juce::File>& files);
    void removeReference(int index);
    void clearReferences();
    void setReferenceWeight(float w);
    bool isListening() const { return refJobs.load() > 0; }
    float listeningProgress() const { return refProgress.load(); }
    sdna::ReferenceFeatures combinedReference() const { return sdna::combineReferences(references); }
    static bool isSupportedAudioFile(const juce::File& f);
    sdna::Influence influence;
    juce::String analysisMessage;
    std::shared_ptr<sdna::Kit> kit;  // current kit (may be null)
    juce::File kitFile;              // where the current kit is saved
    juce::String status;
    bool statusIsError = false;

    void analyse(const juce::String& q, bool online);
    void profileEdited();                // sliders changed the profile
    sdna::SonicProfile profileWithPrompt(sdna::PromptResult* res = nullptr) const;
    void generate();
    void cancelGeneration();
    bool isGenerating() const { return generating.load(); }
    float generationProgress() const { return genProgress.load(); }
    juce::String generationLabel() const;
    bool isAnalysing() const { return analyser.busy(); }

    void regenerateSample(const std::string& id);
    void regenerateCategory(sdna::Category c);
    void previewSample(const sdna::SamplePtr& s);
    void stopPreview() { machine.stopPreview(); }
    void assignPad(int ch, const sdna::SamplePtr& s);
    void randomizePattern(sdna::Groove g, float density);
    void clearPattern();
    sdna::PatternState patternState() const { return machine.getState(); }

    // exports / files (message thread; heavy work is done in the background)
    sdna::AudioFormat exportFormat() const;
    bool exportSampleTo(const sdna::SamplePtr& s, const juce::File& f);
    juce::File writeTempSample(const sdna::SamplePtr& s); // for drag & drop
    void exportKitZipAsync(const juce::File& f);
    bool exportMidi(const juce::File& f, int bars);
    void exportLoopAsync(const juce::File& f, int bars);
    void saveKitAsync(const juce::File& f);
    void loadKitAsync(const juce::File& f);

    void setStatus(const juce::String& s, bool error = false);

    sdna::DrumMachine machine;
    juce::AudioProcessorValueTreeState params;
    double hostSampleRate() const { return currentRate; }
    bool hostDrivesTransport() const { return hostPlaying.load(); }
    double hostBpm() const { return hostTempo.load(); }

private:
    void setKit(std::shared_ptr<sdna::Kit> k, const sdna::PatternState* keepPattern, bool notify = true);
    void applyPadsFromState(const sdna::PatternState& st);
    void postToMessageThread(std::function<void()> fn);
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

    juce::ListenerList<Listener> listeners;
    sdna::KitGenerator generator;
    void rebuildProfile();
    juce::ThreadPool pool{ 2 };
    juce::AudioFormatManager formats;
    std::atomic<int> refJobs{ 0 };
    std::atomic<float> refProgress{ 0 };
    services::OnlineAnalyser analyser;
    std::atomic<bool> generating{ false }, cancelFlag{ false };
    std::atomic<float> genProgress{ 0 };
    juce::CriticalSection labelLock;
    juce::String genLabel;
    std::shared_ptr<std::atomic<bool>> alive = std::make_shared<std::atomic<bool>>(true);
    double currentRate = 44100;
    std::atomic<bool> hostPlaying{ false };
    std::atomic<double> hostTempo{ 120.0 };
    std::vector<float> scratchR;
    std::atomic<float>* masterParam = nullptr;
    std::atomic<float>* swingParam = nullptr;
    float lastSwingParam = -1;
    sdna::PatternState pendingPattern; bool havePendingPattern = false;
    juce::Timer* gcTimer = nullptr;
    struct GcTimer : juce::Timer { sdna::DrumMachine* dm; void timerCallback() override { dm->collectGarbage(); } } gc;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SonicDNAProcessor)
};
