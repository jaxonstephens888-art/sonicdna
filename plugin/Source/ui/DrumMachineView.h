#pragma once

#include "PluginProcessor.h"
#include "ui/Theme.h"

namespace ui {

// Compact numeric field (drag horizontally / double-click to type).
class ValueBox : public juce::Slider {
public:
    ValueBox(const juce::String& label, double min, double max, double def, int decimals, const juce::String& suffix);
    void paint(juce::Graphics&) override;
    juce::String getTextFromValue(double v) override { return juce::String(v * scale, decimals) + suffix; }
    juce::String label, suffix;
    int decimals;
    double scale = 1.0;
};

class DrumMachineView : public juce::Component, public juce::DragAndDropTarget, private SonicDNAProcessor::Listener, private juce::Timer {
public:
    explicit DrumMachineView(SonicDNAProcessor& p);
    ~DrumMachineView() override;
    void paint(juce::Graphics&) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;

    bool isInterestedInDragSource(const SourceDetails&) override;
    void itemDragEnter(const SourceDetails&) override { repaint(); }
    void itemDragMove(const SourceDetails& d) override { dropRow = rowAt(d.localPosition.y); repaint(); }
    void itemDragExit(const SourceDetails&) override { dropRow = -1; repaint(); }
    void itemDropped(const SourceDetails&) override;

private:
    void padsChanged() override { syncFromMachine(); }
    void kitChanged() override { syncFromMachine(); }
    void timerCallback() override;
    void syncFromMachine();
    void sampleMenu(int ch);
    int rowAt(int y) const;
    int stepAt(int x) const;
    juce::Rectangle<int> gridArea() const;
    juce::Rectangle<int> rowBounds(int ch) const;
    juce::Rectangle<int> padArea(int ch) const;
    juce::Rectangle<int> muteArea(int ch) const;
    juce::Rectangle<int> soloArea(int ch) const;
    juce::Rectangle<int> stepArea(int ch, int step) const;
    juce::Rectangle<int> stepsArea(int ch) const;

    SonicDNAProcessor& proc;
    FlatButton playBtn{ "PLAY" }, randomBtn{ "RANDOMISE", false }, clearBtn{ "CLEAR", false }, midiBtn{ "EXPORT MIDI", false }, loopBtn{ "EXPORT LOOP", false };
    ValueBox bpm{ "BPM", 40, 220, 120, 1, "" }, swing{ "SWING", 0, 1, 0, 0, "%" }, master{ "MASTER", -36, 6, -6, 1, " dB" }, density{ "DENSITY", 0, 1, 0.5, 0, "%" };
    juce::ComboBox groove;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> swingAtt, masterAtt;
    struct Strip { std::unique_ptr<Knob> k[8]; };
    Strip strips[sdna::kChannels];
    std::unique_ptr<juce::FileChooser> chooser;
    // interaction
    enum class Drag { None, Velocity, Paint } dragMode = Drag::None;
    int dragCh = -1, dragStep = -1, dragStartVel = 0; uint8_t paintValue = 0; bool moved = false;
    int lastStep = -2; int dropRow = -1;
    float meters[sdna::kChannels] = {};
    static constexpr int kKnobs = 8;
};

} // namespace ui
