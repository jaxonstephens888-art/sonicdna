// SONIC DNA — app services: persistent settings and the online analysis client.
#pragma once

#include <JuceHeader.h>
#include "sdna/OnlineAnalysis.h"

#include <functional>

namespace services {

class Settings {
public:
    static Settings& get();
    juce::String apiKey() const;
    void setApiKey(const juce::String&);
    juce::String model() const;
    void setModel(const juce::String&);
    juce::File kitsFolder() const;
    void setKitsFolder(const juce::File&);
    bool autoSave() const;
    void setAutoSave(bool);
    int exportBits() const;
    void setExportBits(int);
    double sampleRate() const;
    void setSampleRate(double);
    int perCategory() const;
    void setPerCategory(int);
    juce::File settingsFile() const;
private:
    Settings();
    std::unique_ptr<juce::PropertiesFile> props;
};

// Runs on a background thread. Calls `done` on the MESSAGE thread.
// REQUIRES INTERNET and an API key; never touches the audio thread.
class OnlineAnalyser : private juce::Thread {
public:
    using Callback = std::function<void(bool ok, sdna::SonicProfile profile, juce::String message)>;
    OnlineAnalyser();
    ~OnlineAnalyser() override;
    bool start(const juce::String& query, Callback done); // false if busy or no key
    bool busy() const { return isThreadRunning(); }
    void cancel();
private:
    void run() override;
    juce::String query;
    Callback callback;
};

juce::File tempExportFolder();

} // namespace services
