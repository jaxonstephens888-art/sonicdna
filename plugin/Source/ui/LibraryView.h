#pragma once

#include "PluginProcessor.h"
#include "ui/Theme.h"

namespace ui {

class LibraryView;

class SampleRow : public juce::Component {
public:
    SampleRow(LibraryView& owner, sdna::SamplePtr s);
    void paint(juce::Graphics&) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseEnter(const juce::MouseEvent&) override { repaint(); }
    void mouseExit(const juce::MouseEvent&) override { repaint(); }
    void setSample(sdna::SamplePtr s);
    sdna::SamplePtr sample;
    bool pending = false, playing = false;
private:
    void computePeaks();
    juce::Rectangle<int> waveArea() const;
    LibraryView& owner;
    std::vector<float> peaks; // max abs per column
    IconButton play{ "Play", icon::play() }, regen{ "Regenerate", icon::regen(), true }, save{ "Export WAV", icon::download(), true };
    FlatButton pad{ "PAD", false };
    bool dragStarted = false;
};

class LibraryView : public juce::Component, private SonicDNAProcessor::Listener {
public:
    explicit LibraryView(SonicDNAProcessor& p);
    ~LibraryView() override;
    void paint(juce::Graphics&) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseMove(const juce::MouseEvent&) override;
    void mouseExit(const juce::MouseEvent&) override { hoverCat = -1; repaint(); }
    void select(sdna::Category c);

    SonicDNAProcessor& proc;
    void preview(SampleRow* r);
    void regenerate(SampleRow* r);
    void exportSample(SampleRow* r);
    void padMenu(SampleRow* r);

private:
    void kitChanged() override;
    void sampleReplaced(const std::string& oldId, sdna::SamplePtr s) override;
    void rebuildRows();
    juce::Rectangle<int> listArea() const { return getLocalBounds().removeFromLeft(196); }
    std::vector<std::pair<int, int>> listItems() const; // (y, category or -1-group)

    sdna::Category current = sdna::Category::Kick;
    int hoverCat = -1;
    juce::Viewport viewport;
    juce::Component rowsHolder;
    std::vector<std::unique_ptr<SampleRow>> rows;
    FlatButton regenAll{ "REGENERATE ALL", false }, exportFolder{ "EXPORT CATEGORY", false };
    std::unique_ptr<juce::FileChooser> chooser;
};

} // namespace ui
