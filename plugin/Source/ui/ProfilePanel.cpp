#include "ui/ProfilePanel.h"

using namespace sdna;

namespace ui {

static constexpr int kRowH = 21;
static constexpr int kPad = 16;

ProfilePanel::ProfilePanel(SonicDNAProcessor& p) : proc(p) {
    viewport.setViewedComponent(&content, false);
    viewport.setScrollBarsShown(true, false);
    viewport.setScrollBarThickness(6);
    addAndMakeVisible(viewport);
    auto makeSlider = [&](std::function<void(float)> onChange, const juce::String& tip) {
        auto s = std::make_unique<juce::Slider>(juce::Slider::LinearBar, juce::Slider::NoTextBox);
        s->setRange(0.0, 1.0, 0.0);
        s->setTooltip(tip);
        s->setVelocityModeParameters(1.0, 1, 0.0, false);
        s->onValueChange = [sp = s.get(), onChange] { onChange((float)sp->getValue()); };
        content.addAndMakeVisible(*s);
        return s;
    };
    for (int i = 0; i < kNumDesc; ++i) {
        Desc d = (Desc)i;
        desc.push_back({ descLabel(d), makeSlider([this, d](float v) { proc.profile[d] = v; proc.profileEdited(); }, descHelp(d)) });
    }
    struct I { const char* name; float Influence::*m; const char* tip; };
    static const I inf[] = {
        { "Drums", &Influence::drums, "How closely drum & cymbal character follows the artist" },
        { "Tonal", &Influence::tonal, "How closely bass & melodic timbres follow the artist" },
        { "Texture", &Influence::texture, "How closely atmospheres, sound design and vocals follow the artist" },
        { "Processing", &Influence::processing, "How much of the artist's distortion / lo-fi / glitch / vintage processing is applied" },
        { "Groove", &Influence::groove, "How closely tempo, swing and rhythmic style follow the artist" },
        { "Variation", &Influence::variation, "How different the variations inside each category are" },
    };
    for (auto& x : inf) {
        auto m = x.m;
        infl.push_back({ x.name, makeSlider([this, m](float v) { proc.influence.*m = v; content.repaint(); }, x.tip) });
    }
    proc.addListener(this);
    refresh();
}

ProfilePanel::~ProfilePanel() { proc.removeListener(this); }

void ProfilePanel::refresh() {
    for (int i = 0; i < kNumDesc; ++i) desc[(size_t)i].slider->setValue(proc.profile.d[(size_t)i], juce::dontSendNotification);
    const float* iv[] = { &proc.influence.drums, &proc.influence.tonal, &proc.influence.texture, &proc.influence.processing, &proc.influence.groove, &proc.influence.variation };
    for (size_t i = 0; i < infl.size(); ++i) infl[i].slider->setValue(*iv[i], juce::dontSendNotification);
    resized();
    content.repaint();
}

void ProfilePanel::resized() {
    viewport.setBounds(getLocalBounds());
    int w = getWidth() - 8;
    content.setSize(w, content.layoutHeight(w));
}

void ProfilePanel::paint(juce::Graphics& g) { g.fillAll(col::panel); }

int ProfilePanel::Content::layoutHeight(int width) {
    auto& p = owner.proc.profile;
    int tw = width - 2 * kPad;
    juce::GlyphArrangement ga;
    ga.addJustifiedText(sans(12.5f), juce::String(p.summary.empty() ? std::string("Type an artist, producer or genre and press Generate.") : p.summary), 0, 0, (float)tw, juce::Justification::left);
    summaryH = juce::jmax(18, (int)ga.getBoundingBox(0, -1, true).getHeight() + 6);
    int y = 14 + 18 + 32 + 18;
    y += summaryH + 8;
    tagsY = y; y += 30;
    metaY = y; y += 20;
    promptY = y; y += owner.proc.prompt.isNotEmpty() ? 22 : 0;
    y += 14;
    descY = y; y += 22 + kNumDesc * kRowH;
    y += 14;
    infY = y; y += 22 + (int)owner.infl.size() * kRowH;
    y += 14;
    sigY = y; y += 22 + (int)juce::jmax<size_t>(1, p.signatures.size()) * 18 + 16;
    return y;
}

void ProfilePanel::Content::resized() {
    int labelW = 92, valueW = 30;
    for (int i = 0; i < (int)owner.desc.size(); ++i)
        owner.desc[(size_t)i].slider->setBounds(kPad + labelW, descY + 22 + i * kRowH + 3, getWidth() - 2 * kPad - labelW - valueW, kRowH - 6);
    for (int i = 0; i < (int)owner.infl.size(); ++i)
        owner.infl[(size_t)i].slider->setBounds(kPad + labelW, infY + 22 + i * kRowH + 3, getWidth() - 2 * kPad - labelW - valueW, kRowH - 6);
}

void ProfilePanel::Content::paint(juce::Graphics& g) {
    auto& proc = owner.proc;
    auto& p = proc.profile;
    const int W = getWidth();
    g.fillAll(col::panel);
    int y = 14;
    drawSectionLabel(g, "Sonic profile", { kPad, y, W - 2 * kPad, 14 });
    y += 18;
    g.setColour(col::text);
    g.setFont(sans(23.0f, 600));
    juce::String name = p.displayName.empty() ? U8("—") : juce::String::fromUTF8(p.displayName.c_str());
    g.drawFittedText(name, kPad, y, W - 2 * kPad, 30, juce::Justification::centredLeft, 1, 0.8f);
    y += 32;
    // Source badge
    juce::String src = juce::String(p.source);
    juce::String badge; juce::Colour bc = col::dim;
    if (src.startsWith("online")) { badge = "ONLINE RESEARCH"; bc = col::ok; }
    else if (src.startsWith("offline-db")) { badge = "OFFLINE DATABASE"; bc = col::text; }
    else if (src.startsWith("genre-blend")) { badge = "BLEND"; bc = col::text; }
    else if (src.startsWith("descriptor")) { badge = "FROM DESCRIPTION"; bc = col::warn; }
    else if (src.startsWith("heuristic")) { badge = "APPROXIMATION"; bc = col::warn; }
    else badge = src.toUpperCase();
    if (src.contains("edited")) badge << U8(" · EDITED");
    g.setFont(mono(10.0f, 500));
    juce::GlyphArrangement bga; bga.addLineOfText(mono(10.0f, 500), badge, 0, 0);
    int bw = (int)bga.getBoundingBox(0, -1, true).getWidth() + 12;
    g.setColour(bc.withAlpha(0.12f)); g.fillRoundedRectangle((float)kPad, (float)y, (float)bw, 16.0f, 2.0f);
    g.setColour(bc); g.drawText(badge, kPad, y, bw, 16, juce::Justification::centred);
    g.setColour(col::faint);
    g.drawText("confidence " + juce::String((int)std::round(p.confidence * 100)) + "%", kPad + bw + 8, y, W - bw - 2 * kPad, 16, juce::Justification::centredLeft);
    y += 18 + 6;
    g.setColour(col::dim);
    g.setFont(sans(12.5f));
    juce::String summary = p.summary.empty() ? juce::String("Type an artist, producer or genre and press Generate.") : juce::String::fromUTF8(p.summary.c_str());
    juce::GlyphArrangement ga;
    ga.addJustifiedText(sans(12.5f), summary, (float)kPad, (float)y + 11.0f, (float)(W - 2 * kPad), juce::Justification::left);
    ga.draw(g);
    // Tags
    int x = kPad;
    for (auto& t : p.topTags(5)) {
        juce::String s = juce::String(t.first).toUpperCase();
        juce::GlyphArrangement tg; tg.addLineOfText(mono(10.5f, 500), s, 0, 0);
        int tw = (int)tg.getBoundingBox(0, -1, true).getWidth() + 14;
        if (x + tw > W - kPad) break;
        g.setColour(col::raised); g.fillRoundedRectangle((float)x, (float)tagsY, (float)tw, 22.0f, 11.0f);
        g.setColour(col::text); g.setFont(mono(10.5f, 500));
        g.drawText(s, x, tagsY, tw, 22, juce::Justification::centred);
        x += tw + 6;
    }
    // Musical meta
    g.setFont(mono(11.0f));
    g.setColour(col::dim);
    juce::String meta;
    if (proc.kit) meta << noteName(proc.kit->dna.keyRoot) << " " << scaleName(proc.kit->dna.scale) << U8("  ·  ");
    else if (p.keyRoot >= 0) meta << noteName(p.keyRoot) << " " << scaleName(p.scale) << U8("  ·  ");
    else meta << scaleName(p.scale) << U8("  ·  ");
    meta << (int)std::round(proc.profileWithPrompt().bpm) << U8(" BPM  ·  ") << (p.grooves.empty() ? U8("—") : juce::String(grooveLabel(p.grooves[0])));
    g.drawText(meta.toUpperCase(), kPad, metaY, W - 2 * kPad, 18, juce::Justification::centredLeft);
    // Prompt preview
    if (proc.prompt.isNotEmpty()) {
        PromptResult pr; proc.profileWithPrompt(&pr);
        juce::String s = "DIRECTION ";
        if (pr.changes.empty() && pr.bpmDelta == 0) s << U8("· no recognised words");
        for (auto& c : pr.changes) s << U8("· ") << c.what << (c.delta > 0 ? " +" : " ") << (int)std::round(c.delta * 100) << " ";
        if (pr.bpmDelta != 0) s << U8("· BPM ") << (pr.bpmDelta > 0 ? "+" : "") << (int)pr.bpmDelta;
        g.setColour(col::accent);
        g.setFont(mono(10.5f, 500));
        g.drawFittedText(s.toUpperCase(), kPad, promptY, W - 2 * kPad, 18, juce::Justification::centredLeft, 1, 0.7f);
    }
    // Descriptors
    auto rows = [&](int top, const juce::String& title, std::vector<Row>& v, bool percent) {
        g.setColour(col::line); g.drawHorizontalLine(top - 6, (float)kPad, (float)(W - kPad));
        drawSectionLabel(g, title, { kPad, top + 2, W - 2 * kPad, 14 });
        for (size_t i = 0; i < v.size(); ++i) {
            int ry = top + 22 + (int)i * kRowH;
            g.setColour(col::dim); g.setFont(sans(12.0f));
            g.drawText(v[i].label, kPad, ry, 90, kRowH, juce::Justification::centredLeft);
            g.setColour(col::text); g.setFont(mono(10.5f));
            g.drawText(juce::String((int)std::round(v[i].slider->getValue() * 100)) + (percent ? "%" : ""), W - kPad - 30, ry, 30, kRowH, juce::Justification::centredRight);
        }
    };
    rows(descY, "Character", owner.desc, false);
    rows(infY, "Follow the artist", owner.infl, true);
    g.setColour(col::line); g.drawHorizontalLine(sigY - 6, (float)kPad, (float)(W - kPad));
    drawSectionLabel(g, "Signature elements", { kPad, sigY + 2, W - 2 * kPad, 14 });
    g.setFont(sans(12.0f));
    int sy = sigY + 22;
    if (p.signatures.empty()) { g.setColour(col::faint); g.drawText(U8("—"), kPad, sy, W, 18, juce::Justification::centredLeft); }
    for (auto& s : p.signatures) {
        g.setColour(col::accent); g.fillEllipse((float)kPad, (float)sy + 7.0f, 4.0f, 4.0f);
        g.setColour(col::dim);
        g.drawFittedText(juce::String::fromUTF8(s.c_str()), kPad + 12, sy, W - 2 * kPad - 12, 18, juce::Justification::centredLeft, 1, 0.8f);
        sy += 18;
    }
}

} // namespace ui
