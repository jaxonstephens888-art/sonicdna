#include "ui/LibraryView.h"

using namespace sdna;

namespace ui {

static constexpr int kRowH = 58;
static constexpr int kItemH = 24, kGroupH = 30;

// ---------------------------------------------------------------------------
SampleRow::SampleRow(LibraryView& o, SamplePtr s) : owner(o) {
    for (auto* b : std::initializer_list<juce::Component*>{ &play, &regen, &save, &pad }) addAndMakeVisible(b);
    play.setTooltip("Audition");
    regen.setTooltip("Regenerate this sound (new seed, same profile)");
    save.setTooltip("Export as WAV");
    pad.setTooltip("Assign to a drum-machine pad");
    play.onClick = [this] { owner.preview(this); };
    regen.onClick = [this] { owner.regenerate(this); };
    save.onClick = [this] { owner.exportSample(this); };
    pad.onClick = [this] { owner.padMenu(this); };
    setSample(std::move(s));
}

void SampleRow::setSample(SamplePtr s) { sample = std::move(s); pending = false; computePeaks(); repaint(); }

void SampleRow::computePeaks() {
    peaks.assign(400, 0.0f);
    if (!sample || sample->audio.empty()) return;
    const auto& a = sample->audio;
    int len = a.length();
    for (int i = 0; i < len; ++i) {
        size_t bin = (size_t)((int64_t)i * 400 / len);
        float v = 0; for (auto& c : a.ch) v = std::max(v, std::abs(c[(size_t)i]));
        peaks[bin] = std::max(peaks[bin], v);
    }
}

juce::Rectangle<int> SampleRow::waveArea() const {
    auto r = getLocalBounds().reduced(8, 8);
    r.removeFromLeft(44 + 230);
    r.removeFromRight(4 * 34 + 52);
    return r;
}

void SampleRow::resized() {
    auto r = getLocalBounds().reduced(8, 0);
    play.setBounds(r.removeFromLeft(36).withSizeKeepingCentre(32, 32));
    auto right = r.removeFromRight(4 * 34 + 52);
    right.removeFromRight(8);
    pad.setBounds(right.removeFromLeft(48).withSizeKeepingCentre(46, 24));
    right.removeFromLeft(10);
    regen.setBounds(right.removeFromLeft(32).withSizeKeepingCentre(30, 30));
    save.setBounds(right.removeFromLeft(32).withSizeKeepingCentre(30, 30));
}

void SampleRow::paint(juce::Graphics& g) {
    auto r = getLocalBounds();
    bool over = isMouseOverOrDragging(true);
    g.setColour(playing ? col::raised : over ? col::panel2 : col::panel);
    g.fillRect(r.reduced(0, 1));
    if (playing) { g.setColour(col::accent); g.fillRect(r.getX(), r.getY() + 1, 2, r.getHeight() - 2); }
    g.setColour(col::line); g.drawHorizontalLine(r.getBottom() - 1, 0.0f, (float)getWidth());
    if (!sample) return;
    auto text = r.reduced(8, 0).withTrimmedLeft(44).withWidth(222);
    g.setColour(pending ? col::faint : col::text);
    g.setFont(sans(13.5f, 500));
    g.drawFittedText(juce::String::fromUTF8(sample->name.c_str()), text.withY(r.getCentreY() - 19).withHeight(20), juce::Justification::centredLeft, 1, 0.8f);
    juce::String meta;
    if (sample->rootNote >= 0) meta << noteName(sample->rootNote % 12) << (sample->rootNote / 12 - 1) << "  ";
    meta << juce::String(sample->audio.seconds(), 2) << "s";
    if (sample->isLoop) meta << "  LOOP " << (int)std::round(sample->bpm);
    if (sample->audio.numChannels() == 2) meta << "  ST";
    if (pending) meta = U8("regenerating…");
    g.setColour(col::faint);
    g.setFont(mono(10.5f));
    g.drawText(meta, text.withY(r.getCentreY() + 2).withHeight(16), juce::Justification::centredLeft);
    // Waveform
    auto w = waveArea().toFloat();
    g.setColour(col::bg.withAlpha(0.5f)); g.fillRoundedRectangle(w, 3.0f);
    juce::Path p;
    float mid = w.getCentreY(), half = w.getHeight() * 0.46f;
    int n = (int)peaks.size();
    p.startNewSubPath(w.getX(), mid);
    for (int i = 0; i < n; ++i) p.lineTo(w.getX() + w.getWidth() * i / (float)(n - 1), mid - peaks[(size_t)i] * half);
    for (int i = n - 1; i >= 0; --i) p.lineTo(w.getX() + w.getWidth() * i / (float)(n - 1), mid + peaks[(size_t)i] * half);
    p.closeSubPath();
    g.setColour(pending ? col::faint.withAlpha(0.4f) : (playing ? col::accent : col::dim.withAlpha(over ? 0.95f : 0.7f)));
    g.fillPath(p);
    // drag affordance
    drawIcon(g, icon::drag(), juce::Rectangle<float>(w.getRight() - 14.0f, w.getY() + 4.0f, 9.0f, 13.0f), over ? col::dim : col::faint);
}

void SampleRow::mouseDown(const juce::MouseEvent& e) {
    dragStarted = false;
    if (waveArea().contains(e.getPosition())) owner.preview(this);
}

void SampleRow::mouseDrag(const juce::MouseEvent& e) {
    if (dragStarted || !sample || e.getDistanceFromDragStart() < 6) return;
    dragStarted = true;
    if (auto* c = juce::DragAndDropContainer::findParentDragContainerFor(this)) {
        juce::Image img(juce::Image::ARGB, 220, 30, true);
        { juce::Graphics g(img); g.setColour(col::raised.withAlpha(0.95f)); g.fillRoundedRectangle(0, 0, 220, 30, 4); g.setColour(col::text); g.setFont(sans(13, 500)); g.drawText(juce::String::fromUTF8(sample->name.c_str()), 10, 0, 200, 30, juce::Justification::centredLeft); }
        c->startDragging(juce::var(juce::String("sample:") + juce::String(sample->id)), this, juce::ScaledImage(img), true);
    }
}

// ---------------------------------------------------------------------------
LibraryView::LibraryView(SonicDNAProcessor& p) : proc(p) {
    viewport.setViewedComponent(&rowsHolder, false);
    viewport.setScrollBarsShown(true, false);
    addAndMakeVisible(viewport);
    addAndMakeVisible(regenAll);
    addAndMakeVisible(exportFolder);
    regenAll.setTooltip("Regenerate every sound in this category with new seeds");
    exportFolder.setTooltip("Export every sound in this category as WAV files into a folder");
    regenAll.onClick = [this] { proc.regenerateCategory(current); for (auto& r : rows) r->pending = true; repaint(); for (auto& r : rows) r->repaint(); };
    exportFolder.onClick = [this] {
        chooser = std::make_unique<juce::FileChooser>("Export category to folder", juce::File::getSpecialLocation(juce::File::userMusicDirectory));
        chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories, [this](const juce::FileChooser& fc) {
            auto dir = fc.getResult();
            if (!dir.isDirectory() || !proc.kit) return;
            int n = 0;
            for (auto& s : proc.kit->inCategory(current)) if (writeFile(dir.getChildFile(juce::String(s->fileStem(proc.kit->slug())) + ".wav").getFullPathName().toStdString(), encodeWav(s->audio, proc.exportFormat(), s->rootNote, s->isLoop))) ++n;
            proc.setStatus("Exported " + juce::String(n) + " " + categoryInfo(current).label + " to " + dir.getFullPathName());
        });
    };
    proc.addListener(this);
    rebuildRows();
}

LibraryView::~LibraryView() { proc.removeListener(this); }

std::vector<std::pair<int, int>> LibraryView::listItems() const {
    std::vector<std::pair<int, int>> v;
    int y = 10;
    for (int g = 0; g < kNumGroups; ++g) {
        v.push_back({ y, -1 - g }); y += kGroupH;
        for (auto c : categoriesInGroup((Group)g)) { v.push_back({ y, (int)c }); y += kItemH; }
        y += 4;
    }
    return v;
}

void LibraryView::paint(juce::Graphics& g) {
    g.fillAll(col::bg);
    auto la = listArea();
    g.setColour(col::panel); g.fillRect(la);
    g.setColour(col::line); g.drawVerticalLine(la.getRight() - 1, 0.0f, (float)getHeight());
    for (auto& [y, id] : listItems()) {
        if (y > getHeight()) break;
        if (id < 0) { drawSectionLabel(g, groupLabel((Group)(-1 - id)), { 16, y + 10, la.getWidth() - 24, 14 }); continue; }
        auto c = (Category)id;
        juce::Rectangle<int> r(8, y, la.getWidth() - 16, kItemH);
        bool sel = c == current;
        if (sel) { g.setColour(col::raised); g.fillRoundedRectangle(r.toFloat(), 3.0f); g.setColour(col::accent); g.fillRect(r.getX(), r.getY() + 5, 2, r.getHeight() - 10); }
        else if (hoverCat == id) { g.setColour(col::panel2); g.fillRoundedRectangle(r.toFloat(), 3.0f); }
        g.setColour(sel ? col::text : col::dim);
        g.setFont(sans(13.0f, sel ? 500 : 400));
        g.drawText(categoryInfo(c).label, r.withTrimmedLeft(10), juce::Justification::centredLeft);
        int n = proc.kit ? (int)proc.kit->inCategory(c).size() : 0;
        g.setColour(col::faint); g.setFont(mono(10.5f));
        g.drawText(n ? juce::String(n) : U8("–"), r.withTrimmedRight(8), juce::Justification::centredRight);
    }
    // Header
    auto main = getLocalBounds().withTrimmedLeft(la.getWidth());
    auto head = main.removeFromTop(56).reduced(20, 0);
    g.setColour(col::text); g.setFont(sans(20.0f, 600));
    g.drawText(categoryInfo(current).label, head.removeFromLeft(300), juce::Justification::centredLeft);
    if (!proc.kit) {
        auto e = main.reduced(40);
        if (proc.isGenerating()) { g.setColour(col::dim); g.setFont(sans(15.0f, 500)); g.drawText(U8("Synthesising kit…"), e, juce::Justification::centred); return; }
        g.setColour(col::dim); g.setFont(sans(15.0f, 500));
        g.drawText("No kit yet", e.removeFromTop(e.getHeight() / 2).withTrimmedBottom(10), juce::Justification::centredBottom);
        g.setColour(col::faint); g.setFont(sans(13.0f));
        g.drawFittedText("Enter an artist, producer or genre above and press GENERATE.\nSONIC DNA synthesises 39 categories of original drums, bass, melodic, sound-design and vocal sounds.",
                         e.removeFromTop(60), juce::Justification::centredTop, 3);
    }
}

void LibraryView::resized() {
    auto main = getLocalBounds().withTrimmedLeft(listArea().getWidth());
    auto head = main.removeFromTop(56).reduced(20, 14);
    exportFolder.setBounds(head.removeFromRight(130));
    head.removeFromRight(8);
    regenAll.setBounds(head.removeFromRight(130));
    viewport.setBounds(main);
    int w = viewport.getWidth() - viewport.getScrollBarThickness();
    rowsHolder.setSize(w, (int)rows.size() * kRowH);
    for (size_t i = 0; i < rows.size(); ++i) rows[i]->setBounds(0, (int)i * kRowH, w, kRowH);
    bool has = proc.kit != nullptr;
    regenAll.setVisible(has); exportFolder.setVisible(has);
}

void LibraryView::mouseMove(const juce::MouseEvent& e) {
    int h = -1;
    if (listArea().contains(e.getPosition()))
        for (auto& [y, id] : listItems()) if (id >= 0 && e.y >= y && e.y < y + kItemH) h = id;
    if (h != hoverCat) { hoverCat = h; repaint(listArea()); }
}

void LibraryView::mouseDown(const juce::MouseEvent& e) {
    if (!listArea().contains(e.getPosition())) return;
    for (auto& [y, id] : listItems()) if (id >= 0 && e.y >= y && e.y < y + kItemH) { select((Category)id); return; }
}

void LibraryView::select(Category c) { current = c; rebuildRows(); repaint(); }

void LibraryView::rebuildRows() {
    rows.clear();
    if (proc.kit) for (auto& s : proc.kit->inCategory(current)) { rows.push_back(std::make_unique<SampleRow>(*this, s)); rowsHolder.addAndMakeVisible(*rows.back()); }
    resized();
    viewport.setViewPosition(0, 0);
}

void LibraryView::kitChanged() { rebuildRows(); repaint(); }

void LibraryView::sampleReplaced(const std::string& oldId, SamplePtr s) {
    for (auto& r : rows) if (r->sample && r->sample->id == oldId) r->setSample(s);
    repaint(listArea());
}

void LibraryView::preview(SampleRow* r) {
    for (auto& x : rows) if (x->playing) { x->playing = false; x->repaint(); }
    r->playing = true; r->repaint();
    proc.previewSample(r->sample);
}

void LibraryView::regenerate(SampleRow* r) { if (!r->sample) return; r->pending = true; r->repaint(); proc.regenerateSample(r->sample->id); }

void LibraryView::exportSample(SampleRow* r) {
    if (!r->sample || !proc.kit) return;
    auto s = r->sample;
    auto def = juce::File::getSpecialLocation(juce::File::userMusicDirectory).getChildFile(juce::String(s->fileStem(proc.kit->slug())) + ".wav");
    chooser = std::make_unique<juce::FileChooser>("Export sample", def, "*.wav");
    chooser->launchAsync(juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting, [this, s](const juce::FileChooser& fc) {
        auto f = fc.getResult();
        if (f != juce::File()) proc.exportSampleTo(s, f.withFileExtension("wav"));
    });
}

void LibraryView::padMenu(SampleRow* r) {
    if (!r->sample) return;
    juce::PopupMenu m;
    m.addSectionHeader("Assign to pad");
    for (int c = 0; c < kChannels; ++c) {
        auto cur = proc.machine.channelSample(c);
        m.addItem(c + 1, juce::String(c + 1).paddedLeft('0', 2) + "   " + (cur ? juce::String::fromUTF8(cur->name.c_str()) : juce::String("(empty)")));
    }
    auto s = r->sample;
    m.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(r), [this, s](int res) { if (res > 0) proc.assignPad(res - 1, s); });
}

} // namespace ui
