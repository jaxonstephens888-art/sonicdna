#include "ui/Theme.h"
#include "BinaryData.h"

namespace ui {

namespace {
struct Faces {
    juce::Typeface::Ptr regular, medium, semibold, mono, monoMedium;
    Faces() {
        regular = juce::Typeface::createSystemTypefaceFor(BinaryData::InterRegular_otf, BinaryData::InterRegular_otfSize);
        medium = juce::Typeface::createSystemTypefaceFor(BinaryData::InterMedium_otf, BinaryData::InterMedium_otfSize);
        semibold = juce::Typeface::createSystemTypefaceFor(BinaryData::InterSemiBold_otf, BinaryData::InterSemiBold_otfSize);
        mono = juce::Typeface::createSystemTypefaceFor(BinaryData::JetBrainsMonoRegular_ttf, BinaryData::JetBrainsMonoRegular_ttfSize);
        monoMedium = juce::Typeface::createSystemTypefaceFor(BinaryData::JetBrainsMonoMedium_ttf, BinaryData::JetBrainsMonoMedium_ttfSize);
    }
};
Faces& faces() { static Faces f; return f; }
}

juce::Font sans(float size, int weight) {
    auto& f = faces();
    auto tf = weight >= 600 ? f.semibold : weight >= 500 ? f.medium : f.regular;
    return juce::Font(juce::FontOptions(tf).withHeight(size));
}
juce::Font mono(float size, int weight) {
    auto& f = faces();
    return juce::Font(juce::FontOptions(weight >= 500 ? f.monoMedium : f.mono).withHeight(size));
}

namespace icon {
juce::Path play() { juce::Path p; p.addTriangle(0.0f, 0.0f, 0.0f, 1.0f, 0.88f, 0.5f); return p; }
juce::Path stop() { juce::Path p; p.addRectangle(0.0f, 0.0f, 1.0f, 1.0f); return p; }
juce::Path regen() {
    juce::Path p;
    p.addCentredArc(0.5f, 0.5f, 0.38f, 0.38f, 0.0f, 0.5f, juce::MathConstants<float>::twoPi - 0.2f, true);
    p.startNewSubPath(0.5f + 0.38f * std::sin(-0.2f) + 0.0f, 0.12f - 0.12f);
    juce::Path a; a.addTriangle(0.5f, -0.02f, 0.5f, 0.26f, 0.72f, 0.12f);
    p.addPath(a);
    return p;
}
juce::Path download() {
    juce::Path p;
    p.startNewSubPath(0.5f, 0.05f); p.lineTo(0.5f, 0.68f);
    p.startNewSubPath(0.25f, 0.45f); p.lineTo(0.5f, 0.70f); p.lineTo(0.75f, 0.45f);
    p.startNewSubPath(0.1f, 0.95f); p.lineTo(0.9f, 0.95f);
    return p;
}
juce::Path drag() {
    juce::Path p;
    for (int r = 0; r < 3; ++r) for (int c = 0; c < 2; ++c) p.addEllipse(0.22f + c * 0.42f, 0.05f + r * 0.36f, 0.16f, 0.16f);
    return p;
}
juce::Path gear() {
    juce::Path p;
    const int teeth = 8;
    for (int i = 0; i < teeth * 2; ++i) {
        float a = (float)i / (teeth * 2) * juce::MathConstants<float>::twoPi;
        float r = (i % 2) ? 0.38f : 0.5f;
        juce::Point<float> pt(0.5f + r * std::cos(a), 0.5f + r * std::sin(a));
        if (i == 0) p.startNewSubPath(pt); else p.lineTo(pt);
    }
    p.closeSubPath();
    p.addEllipse(0.33f, 0.33f, 0.34f, 0.34f);
    p.setUsingNonZeroWinding(false);
    return p;
}
juce::Path chevron() { juce::Path p; p.startNewSubPath(0.15f, 0.35f); p.lineTo(0.5f, 0.7f); p.lineTo(0.85f, 0.35f); return p; }
}

void drawIcon(juce::Graphics& g, const juce::Path& p, juce::Rectangle<float> area, juce::Colour c, bool stroke, float thickness) {
    juce::Path q = p;
    q.applyTransform(juce::AffineTransform::scale(area.getWidth(), area.getHeight()).translated(area.getX(), area.getY()));
    g.setColour(c);
    if (stroke) g.strokePath(q, juce::PathStrokeType(thickness, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    else g.fillPath(q);
}

void drawSectionLabel(juce::Graphics& g, const juce::String& text, juce::Rectangle<int> area, juce::Colour c) {
    g.setColour(c);
    g.setFont(mono(10.5f, 500));
    g.drawText(text.toUpperCase(), area, juce::Justification::centredLeft, false);
}

// ---------------------------------------------------------------------------
LookAndFeel::LookAndFeel() {
    setColour(juce::ResizableWindow::backgroundColourId, col::bg);
    setColour(juce::DocumentWindow::backgroundColourId, col::bg);
    setColour(juce::TextButton::buttonColourId, col::panel2);
    setColour(juce::TextButton::buttonOnColourId, col::accent);
    setColour(juce::TextButton::textColourOffId, col::text);
    setColour(juce::TextButton::textColourOnId, col::bg);
    setColour(juce::Label::textColourId, col::text);
    setColour(juce::TextEditor::backgroundColourId, col::panel);
    setColour(juce::TextEditor::textColourId, col::text);
    setColour(juce::TextEditor::highlightColourId, col::accent.withAlpha(0.35f));
    setColour(juce::TextEditor::highlightedTextColourId, col::text);
    setColour(juce::TextEditor::outlineColourId, col::line);
    setColour(juce::TextEditor::focusedOutlineColourId, col::lineHi);
    setColour(juce::CaretComponent::caretColourId, col::accent);
    setColour(juce::ComboBox::backgroundColourId, col::panel2);
    setColour(juce::ComboBox::textColourId, col::text);
    setColour(juce::ComboBox::outlineColourId, col::line);
    setColour(juce::ComboBox::arrowColourId, col::dim);
    setColour(juce::PopupMenu::backgroundColourId, col::panel2);
    setColour(juce::PopupMenu::textColourId, col::text);
    setColour(juce::PopupMenu::highlightedBackgroundColourId, col::raised);
    setColour(juce::PopupMenu::highlightedTextColourId, col::text);
    setColour(juce::PopupMenu::headerTextColourId, col::faint);
    setColour(juce::Slider::textBoxTextColourId, col::text);
    setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour(juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
    setColour(juce::Slider::textBoxHighlightColourId, col::accent.withAlpha(0.35f));
    setColour(juce::ScrollBar::thumbColourId, col::lineHi);
    setColour(juce::ToggleButton::textColourId, col::text);
    setColour(juce::ToggleButton::tickColourId, col::accent);
    setColour(juce::TooltipWindow::backgroundColourId, col::raised);
    setColour(juce::TooltipWindow::textColourId, col::text);
    setColour(juce::AlertWindow::backgroundColourId, col::panel);
    setColour(juce::AlertWindow::textColourId, col::text);
    setColour(juce::AlertWindow::outlineColourId, col::line);
    setColour(juce::ListBox::backgroundColourId, col::panel);
    setColour(juce::ProgressBar::foregroundColourId, col::accent);
    setColour(juce::ProgressBar::backgroundColourId, col::panel2);
    setDefaultSansSerifTypeface(sans(13).getTypefacePtr());
}

juce::Typeface::Ptr LookAndFeel::getTypefaceForFont(const juce::Font& f) {
    if (f.getTypefaceName() == juce::Font::getDefaultSansSerifFontName()) return sans(f.getHeight(), f.isBold() ? 600 : 400).getTypefacePtr();
    if (f.getTypefaceName() == juce::Font::getDefaultMonospacedFontName()) return mono(f.getHeight()).getTypefacePtr();
    return juce::LookAndFeel_V4::getTypefaceForFont(f);
}

void LookAndFeel::drawButtonBackground(juce::Graphics& g, juce::Button& b, const juce::Colour&, bool over, bool down) {
    auto r = b.getLocalBounds().toFloat().reduced(0.5f);
    auto* fb = dynamic_cast<FlatButton*>(&b);
    bool on = b.getToggleState();
    bool primary = fb && fb->primary;
    juce::Colour fill = primary ? col::accent : (on && (!fb || fb->accentOn) ? col::accent.withAlpha(0.16f) : col::panel2);
    if (!b.isEnabled()) fill = primary ? col::accentDim : col::panel;
    else if (down) fill = primary ? col::accent.darker(0.15f) : fill.brighter(0.08f);
    else if (over) fill = primary ? col::accent.brighter(0.08f) : fill.brighter(0.05f);
    g.setColour(fill);
    g.fillRoundedRectangle(r, 3.0f);
    if (!primary) {
        g.setColour(on && (!fb || fb->accentOn) ? col::accent.withAlpha(0.7f) : (over ? col::lineHi : col::line));
        g.drawRoundedRectangle(r, 3.0f, 1.0f);
    }
}

juce::Font LookAndFeel::getTextButtonFont(juce::TextButton& b, int h) {
    auto* fb = dynamic_cast<FlatButton*>(&b);
    if (fb && fb->primary) return sans(juce::jmin(15.0f, h * 0.38f), 600);
    return mono(juce::jmin(11.0f, h * 0.5f), 500);
}

void LookAndFeel::drawButtonText(juce::Graphics& g, juce::TextButton& b, bool, bool) {
    auto* fb = dynamic_cast<FlatButton*>(&b);
    bool primary = fb && fb->primary;
    bool on = b.getToggleState();
    g.setFont(getTextButtonFont(b, b.getHeight()));
    juce::Colour c = primary ? juce::Colour(0xff0b0c0e) : (on && (!fb || fb->accentOn) ? col::accent : col::text);
    if (!b.isEnabled()) c = c.withAlpha(0.4f);
    g.setColour(c);
    g.drawText(b.getButtonText(), b.getLocalBounds().reduced(4, 0), juce::Justification::centred, false);
}

void LookAndFeel::drawToggleButton(juce::Graphics& g, juce::ToggleButton& b, bool over, bool) {
    auto r = b.getLocalBounds().toFloat();
    auto box = r.removeFromLeft(r.getHeight()).reduced(r.getHeight() * 0.25f);
    g.setColour(b.getToggleState() ? col::accent : col::panel2);
    g.fillRoundedRectangle(box, 2.0f);
    g.setColour(over ? col::lineHi : col::line);
    g.drawRoundedRectangle(box, 2.0f, 1.0f);
    if (b.getToggleState()) { juce::Path tick; tick.startNewSubPath(0.2f, 0.55f); tick.lineTo(0.42f, 0.75f); tick.lineTo(0.8f, 0.28f); drawIcon(g, tick, box, col::bg, true, 1.6f); }
    g.setColour(b.isEnabled() ? col::text : col::faint);
    g.setFont(mono(11.0f, 500));
    g.drawText(b.getButtonText(), r.withTrimmedLeft(4), juce::Justification::centredLeft, true);
}

void LookAndFeel::drawRotarySlider(juce::Graphics& g, int x, int y, int w, int h, float pos, float start, float end, juce::Slider& s) {
    auto r = juce::Rectangle<float>((float)x, (float)y, (float)w, (float)h).reduced(2.0f);
    float d = juce::jmin(r.getWidth(), r.getHeight());
    auto c = r.withSizeKeepingCentre(d, d);
    float radius = d * 0.5f - 1.5f;
    juce::Path track; track.addCentredArc(c.getCentreX(), c.getCentreY(), radius, radius, 0.0f, start, end, true);
    g.setColour(col::line); g.strokePath(track, juce::PathStrokeType(2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    bool bipolar = s.getMinimum() < 0 && s.getMaximum() > 0;
    float zeroAngle = bipolar ? start + (end - start) * (float)((0 - s.getMinimum()) / (s.getMaximum() - s.getMinimum())) : start;
    float a = start + pos * (end - start);
    juce::Path val; val.addCentredArc(c.getCentreX(), c.getCentreY(), radius, radius, 0.0f, juce::jmin(zeroAngle, a), juce::jmax(zeroAngle, a), true);
    g.setColour(s.isEnabled() ? col::accent : col::faint);
    g.strokePath(val, juce::PathStrokeType(2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    juce::Point<float> tip(c.getCentreX() + (radius - 4.0f) * std::sin(a), c.getCentreY() - (radius - 4.0f) * std::cos(a));
    g.setColour(col::text);
    g.drawLine(juce::Line<float>(c.getCentre(), tip), 1.4f);
}

void LookAndFeel::drawLinearSlider(juce::Graphics& g, int x, int y, int w, int h, float pos, float, float, juce::Slider::SliderStyle style, juce::Slider& s) {
    auto r = juce::Rectangle<float>((float)x, (float)y, (float)w, (float)h);
    if (style == juce::Slider::LinearBar) {
        auto track = r.withSizeKeepingCentre(r.getWidth(), 4.0f);
        g.setColour(col::panel2); g.fillRoundedRectangle(track, 2.0f);
        bool bipolar = s.getMinimum() < 0 && s.getMaximum() > 0;
        float zero = bipolar ? track.getX() + track.getWidth() * (float)((0 - s.getMinimum()) / (s.getMaximum() - s.getMinimum())) : track.getX();
        float px = pos;
        auto fill = juce::Rectangle<float>(juce::jmin(zero, px), track.getY(), std::abs(px - zero), track.getHeight());
        g.setColour(s.isMouseOverOrDragging() ? col::accent : col::accent.withAlpha(0.85f));
        g.fillRoundedRectangle(fill, 2.0f);
        g.setColour(col::text);
        g.fillRoundedRectangle(juce::Rectangle<float>(px - 1.0f, track.getY() - 3.0f, 2.0f, track.getHeight() + 6.0f), 1.0f);
        return;
    }
    juce::LookAndFeel_V4::drawLinearSlider(g, x, y, w, h, pos, 0, 0, style, s);
}

juce::Label* LookAndFeel::createSliderTextBox(juce::Slider& s) {
    auto* l = juce::LookAndFeel_V4::createSliderTextBox(s);
    l->setFont(mono(11.0f));
    l->setColour(juce::Label::textColourId, col::dim);
    l->setColour(juce::Label::outlineColourId, juce::Colours::transparentBlack);
    l->setColour(juce::Label::backgroundColourId, juce::Colours::transparentBlack);
    return l;
}

void LookAndFeel::fillTextEditorBackground(juce::Graphics& g, int w, int h, juce::TextEditor& e) {
    g.setColour(e.findColour(juce::TextEditor::backgroundColourId));
    g.fillRoundedRectangle(0.0f, 0.0f, (float)w, (float)h, 3.0f);
}
void LookAndFeel::drawTextEditorOutline(juce::Graphics& g, int w, int h, juce::TextEditor& e) {
    g.setColour(e.hasKeyboardFocus(true) ? col::accent.withAlpha(0.6f) : col::line);
    g.drawRoundedRectangle(0.5f, 0.5f, (float)w - 1.0f, (float)h - 1.0f, 3.0f, 1.0f);
}

void LookAndFeel::drawComboBox(juce::Graphics& g, int w, int h, bool, int, int, int, int, juce::ComboBox& b) {
    auto r = juce::Rectangle<float>(0, 0, (float)w, (float)h).reduced(0.5f);
    g.setColour(col::panel2); g.fillRoundedRectangle(r, 3.0f);
    g.setColour(b.isMouseOver(true) ? col::lineHi : col::line); g.drawRoundedRectangle(r, 3.0f, 1.0f);
    drawIcon(g, icon::chevron(), juce::Rectangle<float>((float)w - 16.0f, (float)h * 0.5f - 4.0f, 8.0f, 8.0f), col::dim, true, 1.3f);
}
juce::Font LookAndFeel::getComboBoxFont(juce::ComboBox&) { return mono(11.0f, 500); }
void LookAndFeel::positionComboBoxText(juce::ComboBox& b, juce::Label& l) {
    l.setBounds(6, 0, b.getWidth() - 22, b.getHeight());
    l.setFont(getComboBoxFont(b));
}
juce::Font LookAndFeel::getPopupMenuFont() { return sans(13.0f); }
void LookAndFeel::drawPopupMenuBackground(juce::Graphics& g, int w, int h) {
    g.fillAll(col::panel2);
    g.setColour(col::line); g.drawRect(0, 0, w, h);
}

void LookAndFeel::drawScrollbar(juce::Graphics& g, juce::ScrollBar&, int x, int y, int w, int h, bool vertical, int ts, int tsz, bool over, bool down) {
    juce::Rectangle<float> thumb = vertical ? juce::Rectangle<float>((float)x + 2, (float)ts, (float)w - 4, (float)tsz) : juce::Rectangle<float>((float)ts, (float)y + 2, (float)tsz, (float)h - 4);
    g.setColour(down ? col::dim : over ? col::faint : col::lineHi);
    g.fillRoundedRectangle(thumb, 2.0f);
}

juce::Font LookAndFeel::getLabelFont(juce::Label& l) { return l.getFont(); }

void LookAndFeel::drawTooltip(juce::Graphics& g, const juce::String& text, int w, int h) {
    g.fillAll(col::raised);
    g.setColour(col::lineHi); g.drawRect(0, 0, w, h);
    g.setColour(col::text); g.setFont(sans(12.0f));
    g.drawFittedText(text, juce::Rectangle<int>(w, h).reduced(8, 4), juce::Justification::centredLeft, 4);
}
juce::Rectangle<int> LookAndFeel::getTooltipBounds(const juce::String& tip, juce::Point<int> pos, juce::Rectangle<int> parent) {
    juce::GlyphArrangement ga; ga.addLineOfText(sans(12.0f), tip, 0, 0);
    int w = juce::jmin(360, (int)ga.getBoundingBox(0, -1, true).getWidth() + 18);
    int lines = 1 + (int)(ga.getBoundingBox(0, -1, true).getWidth() / 340.0f);
    int h = 10 + lines * 16;
    return juce::Rectangle<int>(pos.x > parent.getCentreX() ? pos.x - (w + 12) : pos.x + 12, pos.y > parent.getCentreY() ? pos.y - (h + 6) : pos.y + 6, w, h).constrainedWithin(parent);
}

// ---------------------------------------------------------------------------
Knob::Knob(const juce::String& name, double min, double max, double def, const juce::String& sfx)
    : juce::Slider(juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::NoTextBox), defaultValue(def), suffix(sfx) {
    setName(name);
    setRange(min, max, 0.0);
    setValue(def, juce::dontSendNotification);
    setDoubleClickReturnValue(true, def);
    setRotaryParameters(juce::MathConstants<float>::pi * 1.25f, juce::MathConstants<float>::pi * 2.75f, true);
    setMouseDragSensitivity(140);
    setPopupDisplayEnabled(true, false, nullptr);
    setTooltip(name + " (double-click to reset)");
}
juce::String Knob::getTextFromValue(double v) { return getName() + " " + juce::String(v, std::abs(v) < 10 ? 2 : 1) + suffix; }

void IconButton::paintButton(juce::Graphics& g, bool over, bool down) {
    auto r = getLocalBounds().toFloat();
    if (over || down) { g.setColour(col::raised); g.fillRoundedRectangle(r.reduced(1), 3.0f); }
    auto ic = r.withSizeKeepingCentre(juce::jmin(r.getWidth(), r.getHeight()) * 0.5f, juce::jmin(r.getWidth(), r.getHeight()) * 0.5f);
    drawIcon(g, path, ic, getToggleState() ? col::accent : (over ? col::text : tint), stroke, 1.4f);
}

} // namespace ui
