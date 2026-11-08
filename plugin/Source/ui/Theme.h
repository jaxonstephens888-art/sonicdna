// SONIC DNA — visual language: dark, restrained, one accent colour.
#pragma once

#include <JuceHeader.h>

// UTF-8 string literal helper (JUCE treats plain char* literals as ASCII).
#define U8(x) juce::String(juce::CharPointer_UTF8(x))

namespace ui {

namespace col {
    const juce::Colour bg        { 0xff0b0c0e };
    const juce::Colour panel     { 0xff111316 };
    const juce::Colour panel2    { 0xff171a1e };
    const juce::Colour raised    { 0xff1e2227 };
    const juce::Colour line      { 0xff23272d };
    const juce::Colour lineHi    { 0xff2f343b };
    const juce::Colour text      { 0xffe9e8e4 };
    const juce::Colour dim       { 0xff8b9199 };
    const juce::Colour faint     { 0xff565c64 };
    const juce::Colour accent    { 0xffff5a1f }; // signal orange
    const juce::Colour accentDim { 0xff7a3418 };
    const juce::Colour ok        { 0xff6fcf97 };
    const juce::Colour warn      { 0xffe8b04a };
}

juce::Font sans(float size, int weight = 400); // 400 regular, 500 medium, 600 semibold
juce::Font mono(float size, int weight = 400);

namespace icon {
    juce::Path play();
    juce::Path stop();
    juce::Path regen();
    juce::Path download();
    juce::Path drag();
    juce::Path gear();
    juce::Path chevron();
}

void drawIcon(juce::Graphics& g, const juce::Path& p, juce::Rectangle<float> area, juce::Colour c, bool stroke = false, float thickness = 1.4f);
void drawSectionLabel(juce::Graphics& g, const juce::String& text, juce::Rectangle<int> area, juce::Colour c = col::faint);

class LookAndFeel : public juce::LookAndFeel_V4 {
public:
    LookAndFeel();
    juce::Typeface::Ptr getTypefaceForFont(const juce::Font&) override;

    void drawButtonBackground(juce::Graphics&, juce::Button&, const juce::Colour&, bool over, bool down) override;
    void drawButtonText(juce::Graphics&, juce::TextButton&, bool over, bool down) override;
    juce::Font getTextButtonFont(juce::TextButton&, int buttonHeight) override;
    void drawToggleButton(juce::Graphics&, juce::ToggleButton&, bool over, bool down) override;

    void drawRotarySlider(juce::Graphics&, int x, int y, int w, int h, float pos, float start, float end, juce::Slider&) override;
    void drawLinearSlider(juce::Graphics&, int x, int y, int w, int h, float pos, float minPos, float maxPos, juce::Slider::SliderStyle, juce::Slider&) override;
    juce::Label* createSliderTextBox(juce::Slider&) override;

    void fillTextEditorBackground(juce::Graphics&, int w, int h, juce::TextEditor&) override;
    void drawTextEditorOutline(juce::Graphics&, int w, int h, juce::TextEditor&) override;

    void drawComboBox(juce::Graphics&, int w, int h, bool down, int bx, int by, int bw, int bh, juce::ComboBox&) override;
    juce::Font getComboBoxFont(juce::ComboBox&) override;
    void positionComboBoxText(juce::ComboBox&, juce::Label&) override;
    juce::Font getPopupMenuFont() override;
    void drawPopupMenuBackground(juce::Graphics&, int w, int h) override;

    void drawScrollbar(juce::Graphics&, juce::ScrollBar&, int x, int y, int w, int h, bool vertical, int thumbStart, int thumbSize, bool over, bool down) override;
    int getDefaultScrollbarWidth() override { return 8; }
    juce::Font getLabelFont(juce::Label&) override;
    void drawTooltip(juce::Graphics&, const juce::String& text, int w, int h) override;
    juce::Rectangle<int> getTooltipBounds(const juce::String& tipText, juce::Point<int> screenPos, juce::Rectangle<int> parentArea) override;
};

// Small rotary knob with a caption, used across the drum machine.
class Knob : public juce::Slider {
public:
    Knob(const juce::String& name, double min, double max, double def, const juce::String& suffix = {});
    juce::String getTextFromValue(double v) override;
    double defaultValue;
    juce::String suffix;
};

// Flat text button that can act as a toggle with an accent state.
class FlatButton : public juce::TextButton {
public:
    explicit FlatButton(const juce::String& text, bool accentWhenOn = true) : juce::TextButton(text) { accentOn = accentWhenOn; }
    bool accentOn = true;
    bool primary = false; // filled accent (Generate)
};

class IconButton : public juce::Button {
public:
    IconButton(const juce::String& name, juce::Path p, bool stroked = false) : juce::Button(name), path(std::move(p)), stroke(stroked) {}
    void paintButton(juce::Graphics&, bool over, bool down) override;
    juce::Path path;
    bool stroke;
    juce::Colour tint = col::dim;
};

} // namespace ui
