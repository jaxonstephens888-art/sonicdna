#include "sdna/DrumMachine.h"

#include <cstring>

namespace sdna {

PlaybackPtr makePlayback(const Sample& s) {
    auto p = std::make_shared<PlaybackSample>();
    p->sampleRate = s.audio.sampleRate;
    p->L = s.audio.ch.empty() ? std::vector<float>() : s.audio.ch[0];
    p->R = s.audio.numChannels() > 1 ? s.audio.ch[1] : p->L;
    p->rootNote = s.rootNote;
    p->sampleId = s.id;
    p->name = s.name;
    return p;
}

DrumMachine::DrumMachine() {
    for (auto& row : steps) for (auto& s : row) s.store(0);
    for (auto& s : slot) s.store(nullptr);
    for (auto& v : voiceRefs) v.store(nullptr);
    for (auto& m : channelMeter) m.store(0);
    for (auto& t : lastTone) t = -99;
    auto roles = defaultChannelRoles();
    for (int c = 0; c < kChannels; ++c) params[c].role.store((int)roles[(size_t)c]);
    params[5].choke.store(1); params[6].choke.store(1); params[7].choke.store(1); // hats choke each other
}

DrumMachine::~DrumMachine() = default;

void DrumMachine::prepare(double sampleRate, int maxBlockSize) {
    sr = sampleRate;
    maxBlock = std::max(32, maxBlockSize);
    for (int c = 0; c < kChannels; ++c) { chL[c].assign((size_t)maxBlock, 0); chR[c].assign((size_t)maxBlock, 0); toneL[c].reset(); toneR[c].reset(); lastTone[c] = -99; }
    revInL.assign((size_t)maxBlock, 0); revInR.assign((size_t)maxBlock, 0); dlyInL.assign((size_t)maxBlock, 0); dlyInR.assign((size_t)maxBlock, 0); prevL.assign((size_t)maxBlock, 0); prevR.assign((size_t)maxBlock, 0);
    static const int combT[8] = { 1116, 1188, 1277, 1356, 1422, 1491, 1557, 1617 };
    static const int apT[4] = { 556, 441, 341, 225 };
    double sc = sr / 44100.0;
    for (int i = 0; i < 8; ++i) { cl[i].b.assign((size_t)(combT[i] * sc), 0); cr[i].b.assign((size_t)((combT[i] + 23) * sc), 0); cl[i].i = cr[i].i = 0; cl[i].st = cr[i].st = 0; }
    for (int i = 0; i < 4; ++i) { al[i].b.assign((size_t)(apT[i] * sc), 0); ar[i].b.assign((size_t)((apT[i] + 23) * sc), 0); al[i].i = ar[i].i = 0; }
    dlBufL.assign((size_t)(sr * 4) + 1, 0); dlBufR.assign((size_t)(sr * 4) + 1, 0); dlIdx = 0;
    for (auto& v : voices) v.active = false;
}

void DrumMachine::setChannelSample(int ch, PlaybackPtr s) {
    if (ch < 0 || ch >= kChannels) return;
    if (owned[(size_t)ch]) retired.push_back({ owned[(size_t)ch], std::chrono::steady_clock::now() });
    owned[(size_t)ch] = s;
    slot[ch].store(s.get());
    collectGarbage();
}

void DrumMachine::previewSample(PlaybackPtr s, float gain) {
    if (ownedPreview) retired.push_back({ ownedPreview, std::chrono::steady_clock::now() });
    ownedPreview = s;
    pendingPreviewGain.store(gain);
    pendingPreview.store(s.get());
    collectGarbage();
}

void DrumMachine::stopPreview() { stopPreviewFlag.store(true); }

void DrumMachine::collectGarbage(bool force) {
    auto now = std::chrono::steady_clock::now();
    retired.erase(std::remove_if(retired.begin(), retired.end(), [&](const Retired& r) {
        if (force) return true;
        if (now - r.when < std::chrono::milliseconds(500)) return false;
        const PlaybackSample* p = r.p.get();
        for (auto& v : voiceRefs) if (v.load() == p) return false;
        for (auto& s : slot) if (s.load() == p) return false;
        if (pendingPreview.load() == p) return false;
        return true;
    }), retired.end());
}

void DrumMachine::setState(const PatternState& s) {
    for (int c = 0; c < kChannels; ++c) {
        for (int k = 0; k < kSteps; ++k) steps[c][k].store(s.vel[(size_t)c][(size_t)k]);
        auto& p = params[c]; const auto& cs = s.ch[(size_t)c];
        p.volumeDb = cs.volumeDb; p.pan = cs.pan; p.pitch = cs.pitch; p.tone = cs.tone; p.drive = cs.drive;
        p.reverb = cs.reverb; p.delay = cs.delay; p.decay = cs.decay; p.mute = cs.mute; p.solo = cs.solo; p.choke = cs.choke; p.role = (int)cs.role;
    }
    length = std::clamp(s.length, 1, kSteps); bpm = s.bpm; swing = s.swing; masterDb = s.masterDb;
}

PatternState DrumMachine::getState() const {
    PatternState s;
    for (int c = 0; c < kChannels; ++c) {
        for (int k = 0; k < kSteps; ++k) s.vel[(size_t)c][(size_t)k] = steps[c][k].load();
        auto& p = params[c]; auto& cs = s.ch[(size_t)c];
        cs.volumeDb = p.volumeDb; cs.pan = p.pan; cs.pitch = p.pitch; cs.tone = p.tone; cs.drive = p.drive; cs.reverb = p.reverb;
        cs.delay = p.delay; cs.decay = p.decay; cs.mute = p.mute; cs.solo = p.solo; cs.choke = p.choke; cs.role = (Category)p.role.load();
        cs.sampleId = owned[(size_t)c] ? owned[(size_t)c]->sampleId : std::string();
    }
    s.length = length; s.bpm = bpm; s.swing = swing; s.masterDb = masterDb;
    return s;
}

void DrumMachine::triggerFromUI(int ch, float velocity) {
    int w = qWrite.load(std::memory_order_relaxed);
    int nw = (w + 1) % kQ;
    if (nw == qRead.load(std::memory_order_acquire)) return; // full: drop
    queue[w] = { ch, velocity };
    qWrite.store(nw, std::memory_order_release);
}

void DrumMachine::allNotesOff() { for (auto& v : voices) { v.active = false; } for (auto& r : voiceRefs) r.store(nullptr); }

void DrumMachine::startVoice(int ch, const PlaybackSample* s, float vel, int offset, bool preview, float gainOverride) {
    if (!s || s->length() == 0) return;
    // Choke groups (only for pads)
    if (!preview) {
        int choke = params[ch].choke.load();
        for (auto& v : voices) {
            if (!v.active || v.preview) continue;
            if (v.ch == ch || (choke > 0 && params[v.ch].choke.load() == choke)) { v.fadeStep = 1.0f / (float)std::max(1.0, 0.004 * sr); }
        }
    } else {
        for (auto& v : voices) if (v.active && v.preview) v.fadeStep = 1.0f / (float)std::max(1.0, 0.01 * sr);
    }
    // find free voice (or steal the quietest)
    int idx = -1; float minEnv = 2.0f;
    for (int i = 0; i < kVoices; ++i) { if (!voices[i].active) { idx = i; break; } if (voices[i].env * voices[i].fade < minEnv) { minEnv = voices[i].env * voices[i].fade; idx = i; } }
    Voice& v = voices[idx];
    v.s = s; v.ch = ch; v.preview = preview; v.active = true;
    v.pos = -(double)offset; // start after `offset` samples within this block
    double pitch = preview ? 0.0 : params[ch].pitch.load();
    v.rate = std::pow(2.0, pitch / 12.0) * (s->sampleRate / sr);
    v.gain = preview ? gainOverride : vel;
    v.env = 1.0f; v.fade = 1.0f; v.fadeStep = 0.0f;
    float dec = preview ? 1.0f : params[ch].decay.load();
    if (dec >= 0.999f) v.envMul = 1.0f;
    else { double t60 = 0.03 + dec * dec * 3.0; v.envMul = (float)std::exp(-6.907755 / (t60 * sr)); }
    voiceRefs[idx].store(s);
}

void DrumMachine::renderVoices(int start, int end) {
    for (int vi = 0; vi < kVoices; ++vi) {
        Voice& v = voices[vi];
        if (!v.active) continue;
        float* outL = v.preview ? prevL.data() : chL[v.ch].data();
        float* outR = v.preview ? prevR.data() : chR[v.ch].data();
        const float* sL = v.s->L.data(); const float* sR = v.s->R.data();
        const int len = v.s->length();
        for (int i = start; i < end; ++i) {
            if (v.pos < 0) { v.pos += 1.0; continue; }
            int i0 = (int)v.pos;
            if (i0 >= len - 1 || v.fade <= 0.0f || v.env < 1e-4f) { v.active = false; voiceRefs[vi].store(nullptr); break; }
            float fr = (float)(v.pos - i0);
            float g = v.gain * v.env * v.fade;
            outL[i] += (sL[i0] + (sL[i0 + 1] - sL[i0]) * fr) * g;
            outR[i] += (sR[i0] + (sR[i0 + 1] - sR[i0]) * fr) * g;
            v.env *= v.envMul;
            if (v.fadeStep > 0) v.fade -= v.fadeStep;
            v.pos += v.rate;
        }
    }
}

// Steps are scheduled in "16th-step time". Odd steps are delayed by swing.
static inline double swungTime(long long k, float swing) { return (double)k + ((k & 1) ? (double)swing * 0.33 : 0.0); }

void DrumMachine::runSequencer(int n, const HostTransport& host, int& firstTrig) {
    firstTrig = -1;
    bool hostDrives = followHost.load() && host.valid && host.playing;
    bool run = hostDrives || playing.load();
    if (!run) {
        if (wasPlaying) { currentStep.store(-1); }
        wasPlaying = false; internalSteps = 0; lastHostSteps = -1;
        return;
    }
    double tempo = hostDrives ? host.bpm : (double)bpm.load();
    tempo = clampd(tempo, 20.0, 400.0);
    double samplesPerStep = sr * 60.0 / tempo / 4.0;
    double t0 = hostDrives ? host.ppqPosition * 4.0 : internalSteps;
    if (!wasPlaying) t0 = hostDrives ? t0 : 0.0, internalSteps = t0;
    double t1 = t0 + n / samplesPerStep;
    float sw = swing.load();
    int len = std::clamp(length.load(), 1, kSteps);
    // When (re)starting exactly on a step, make sure it fires.
    double scanFrom = wasPlaying ? t0 : t0 - 1e-6;
    long long kStart = (long long)std::floor(scanFrom) - 1, kEnd = (long long)std::ceil(t1) + 1;
    bool anySolo = false; for (auto& p : params) if (p.solo.load()) anySolo = true;
    for (long long k = kStart; k <= kEnd; ++k) {
        if (k < 0) continue;
        double t = swungTime(k, sw);
        if (t < scanFrom - 1e-9 || t >= t1 - 1e-9 / samplesPerStep) continue;
        int offset = (int)std::floor((t - t0) * samplesPerStep + 1e-6);
        offset = std::clamp(offset, 0, n - 1);
        int step = (int)(k % len);
        currentStep.store(step);
        if (firstTrig < 0) firstTrig = offset;
        for (int c = 0; c < kChannels; ++c) {
            uint8_t vel = steps[c][step].load();
            if (!vel) continue;
            if (params[c].mute.load() || (anySolo && !params[c].solo.load())) continue;
            startVoice(c, slot[c].load(), vel / 127.0f, offset, false);
        }
    }
    internalSteps = t1;
    lastHostSteps = t1;
    wasPlaying = true;
}

void DrumMachine::process(float* L, float* R, int n, const HostTransport& host, const MidiNote* notes, int numNotes) {
    // Handle blocks larger than prepared size by splitting.
    if (n > maxBlock) {
        int done = 0;
        while (done < n) {
            int chunk = std::min(maxBlock, n - done);
            HostTransport h = host;
            if (h.valid) h.ppqPosition += (done / sr) * (h.bpm / 60.0);
            // forward notes that fall inside this chunk
            MidiNote local[256]; int ln = 0;
            for (int i = 0; i < numNotes && ln < 256; ++i) if (notes[i].offset >= done && notes[i].offset < done + chunk) { local[ln] = notes[i]; local[ln].offset -= done; ++ln; }
            process(L + done, R + done, chunk, h, local, ln);
            done += chunk;
        }
        return;
    }
    for (int c = 0; c < kChannels; ++c) { std::memset(chL[c].data(), 0, sizeof(float) * (size_t)n); std::memset(chR[c].data(), 0, sizeof(float) * (size_t)n); }
    std::memset(revInL.data(), 0, sizeof(float) * (size_t)n); std::memset(revInR.data(), 0, sizeof(float) * (size_t)n);
    std::memset(dlyInL.data(), 0, sizeof(float) * (size_t)n); std::memset(dlyInR.data(), 0, sizeof(float) * (size_t)n);
    std::memset(prevL.data(), 0, sizeof(float) * (size_t)n); std::memset(prevR.data(), 0, sizeof(float) * (size_t)n);

    // Preview handoff
    if (stopPreviewFlag.exchange(false)) for (auto& v : voices) if (v.active && v.preview) v.fadeStep = 1.0f / (float)(0.01 * sr);
    if (const PlaybackSample* p = pendingPreview.exchange(nullptr)) startVoice(0, p, 1.0f, 0, true, pendingPreviewGain.load());
    // UI pad triggers
    for (int r = qRead.load(std::memory_order_relaxed); r != qWrite.load(std::memory_order_acquire); r = (r + 1) % kQ) {
        auto [ch, vel] = queue[r];
        if (ch >= 0 && ch < kChannels) startVoice(ch, slot[ch].load(), vel, 0, false);
        qRead.store((r + 1) % kQ, std::memory_order_release);
    }
    // MIDI notes
    for (int i = 0; i < numNotes; ++i) {
        int ch = notes[i].note - kBaseMidiNote;
        if (ch >= 0 && ch < kChannels && notes[i].velocity > 0) startVoice(ch, slot[ch].load(), notes[i].velocity, std::clamp(notes[i].offset, 0, n - 1), false);
    }
    int firstTrig;
    runSequencer(n, host, firstTrig);
    renderVoices(0, n);

    // Channel strips
    float mL = 0, mR = 0;
    std::memset(L, 0, sizeof(float) * (size_t)n); std::memset(R, 0, sizeof(float) * (size_t)n);
    for (int c = 0; c < kChannels; ++c) {
        auto& p = params[c];
        float tone = p.tone.load();
        if (std::abs(tone - lastTone[c]) > 1e-4f) {
            if (tone < -0.01f) { double hz = 20000.0 * std::pow(2.0, tone * 7.5); toneL[c].set(sr, hz, 0.75); toneR[c].set(sr, hz, 0.75); }
            else if (tone > 0.01f) { double hz = 20.0 * std::pow(2.0, tone * 9.0); toneL[c].set(sr, hz, 0.75); toneR[c].set(sr, hz, 0.75); }
            lastTone[c] = tone;
        }
        float drive = p.drive.load();
        float pre = 1.0f + drive * 12.0f, post = drive > 0.001f ? 1.0f / std::tanh(pre) : 1.0f;
        float gain = dbToGain(p.volumeDb.load());
        float pan = clampf(p.pan.load(), -1, 1);
        float gL = gain * std::cos((pan + 1) * 0.25f * (float)kPi) * 1.41421f, gR = gain * std::sin((pan + 1) * 0.25f * (float)kPi) * 1.41421f;
        float rs = p.reverb.load(), ds = p.delay.load();
        float* a = chL[c].data(); float* b = chR[c].data();
        float peak = 0;
        for (int i = 0; i < n; ++i) {
            float l = a[i], r = b[i];
            if (tone < -0.01f) { l = toneL[c].lp(l); r = toneR[c].lp(r); }
            else if (tone > 0.01f) { l = toneL[c].hp(l); r = toneR[c].hp(r); }
            if (drive > 0.001f) { l = std::tanh(l * pre) * post; r = std::tanh(r * pre) * post; }
            l *= gL; r *= gR;
            L[i] += l; R[i] += r;
            revInL[(size_t)i] += l * rs; revInR[(size_t)i] += r * rs;
            dlyInL[(size_t)i] += l * ds; dlyInR[(size_t)i] += r * ds;
            peak = std::max(peak, std::max(std::abs(l), std::abs(r)));
        }
        channelMeter[c].store(std::max(peak, channelMeter[c].load() * 0.85f));
    }
    // Send FX: reverb
    const float fb = 0.84f, damp = 0.25f;
    for (int i = 0; i < n; ++i) {
        float in = (revInL[(size_t)i] + revInR[(size_t)i]) * 0.015f;
        float ol = 0, orr = 0;
        for (int k = 0; k < 8; ++k) {
            RtComb& x = cl[k]; float o = x.b[x.i]; x.st = o * (1 - damp) + x.st * damp; x.b[x.i] = in + x.st * fb; if (++x.i >= x.b.size()) x.i = 0; ol += o;
            RtComb& y = cr[k]; float o2 = y.b[y.i]; y.st = o2 * (1 - damp) + y.st * damp; y.b[y.i] = in + y.st * fb; if (++y.i >= y.b.size()) y.i = 0; orr += o2;
        }
        for (int k = 0; k < 4; ++k) {
            RtAp& x = al[k]; float bo = x.b[x.i]; float o = -ol + bo; x.b[x.i] = ol + bo * 0.5f; if (++x.i >= x.b.size()) x.i = 0; ol = o;
            RtAp& y = ar[k]; float bo2 = y.b[y.i]; float o2 = -orr + bo2; y.b[y.i] = orr + bo2 * 0.5f; if (++y.i >= y.b.size()) y.i = 0; orr = o2;
        }
        L[i] += ol * 3.0f; R[i] += orr * 3.0f;
    }
    // Send FX: tempo-synced ping-pong delay (dotted 8th)
    {
        double tempo = (followHost.load() && host.valid) ? host.bpm : (double)bpm.load();
        size_t d = (size_t)clampd(sr * 60.0 / clampd(tempo, 20, 400) * 0.75, 1, (double)dlBufL.size() - 1);
        size_t sz = dlBufL.size();
        for (int i = 0; i < n; ++i) {
            size_t rd = (dlIdx + sz - d) % sz;
            float yl = dlBufL[rd], yr = dlBufR[rd];
            float inM = (dlyInL[(size_t)i] + dlyInR[(size_t)i]) * 0.5f;
            dlLP_L += 0.35f * ((inM + yr * 0.45f) - dlLP_L);
            dlLP_R += 0.35f * ((yl * 0.45f) - dlLP_R);
            dlBufL[dlIdx] = dlLP_L; dlBufR[dlIdx] = dlLP_R;
            dlIdx = (dlIdx + 1) % sz;
            L[i] += yl; R[i] += yr;
        }
    }
    // Preview + master
    float mg = dbToGain(masterDb.load());
    for (int i = 0; i < n; ++i) {
        float l = (L[i] + prevL[(size_t)i]) * mg, r = (R[i] + prevR[(size_t)i]) * mg;
        // gentle safety clipper
        if (std::abs(l) > 0.9f) l = (l > 0 ? 1.0f : -1.0f) * (0.9f + 0.1f * std::tanh((std::abs(l) - 0.9f) / 0.1f));
        if (std::abs(r) > 0.9f) r = (r > 0 ? 1.0f : -1.0f) * (0.9f + 0.1f * std::tanh((std::abs(r) - 0.9f) / 0.1f));
        L[i] = l; R[i] = r;
        mL = std::max(mL, std::abs(l)); mR = std::max(mR, std::abs(r));
    }
    masterMeter.store(std::max(std::max(mL, mR), masterMeter.load() * 0.85f));
    (void)firstTrig;
}

Buffer DrumMachine::renderLoop(const PatternState& st, const std::array<PlaybackPtr, kChannels>& samples, int bars, double sampleRate, bool wrapTail) {
    auto dm = std::make_unique<DrumMachine>();
    const int block = 512;
    dm->prepare(sampleRate, block);
    dm->setState(st);
    for (int c = 0; c < kChannels; ++c) dm->setChannelSample(c, samples[(size_t)c]);
    dm->followHost = false;
    dm->playing = true;
    int len = std::clamp(st.length, 1, kSteps);
    double stepSec = 60.0 / clampd(st.bpm, 20, 400) / 4.0;
    int loopLen = (int)std::llround(stepSec * len * bars * sampleRate * (16.0 / len));
    // A "bar" is 16 steps; with shorter pattern lengths the pattern simply repeats inside the bar.
    loopLen = (int)std::llround(stepSec * 16 * bars * sampleRate);
    int tail = wrapTail ? (int)(sampleRate * 2.5) : 0;
    Buffer out(2, loopLen + tail, sampleRate);
    HostTransport none;
    int pos = 0;
    while (pos < loopLen + tail) {
        int nn = std::min(block, loopLen + tail - pos);
        if (pos >= loopLen) dm->playing = false; // stop scheduling, let tails ring
        dm->process(out.data(0) + pos, out.data(1) + pos, nn, none, nullptr, 0);
        pos += nn;
    }
    if (wrapTail) {
        for (int c = 0; c < 2; ++c) for (int i = loopLen; i < out.length(); ++i) out.ch[(size_t)c][(size_t)(i - loopLen) % (size_t)loopLen] += out.ch[(size_t)c][(size_t)i];
        out.resize(loopLen);
    }
    return out;
}

// ---------------------------------------------------------------------------
std::array<Category, kChannels> defaultChannelRoles() {
    return { Category::Kick, Category::Kick, Category::Snare, Category::Clap, Category::Rim, Category::ClosedHat, Category::OpenHat, Category::PedalHat,
             Category::Ride, Category::Crash, Category::Tom, Category::Tom, Category::Perc, Category::Perc, Category::Bass808, Category::VocalChop };
}

void assignKitToChannels(const Kit& kit, PatternState& st) {
    std::array<int, kNumCategories> used{};
    for (int c = 0; c < kChannels; ++c) {
        Category role = st.ch[(size_t)c].role;
        auto list = kit.inCategory(role);
        // prefer one-shots for pads
        std::vector<SamplePtr> oneShots; for (auto& s : list) if (!s->isLoop) oneShots.push_back(s);
        auto& pool = oneShots.empty() ? list : oneShots;
        if (pool.empty()) { st.ch[(size_t)c].sampleId.clear(); continue; }
        int idx = used[(size_t)role]++;
        if (role == Category::Tom) idx = c == 10 ? 0 : std::min<int>(2, (int)pool.size() - 1);
        st.ch[(size_t)c].sampleId = pool[(size_t)idx % pool.size()]->id;
    }
}

Groove pickGroove(const SonicProfile& p, uint64_t seed) {
    if (p.grooves.empty()) return Groove::FourFloor;
    Rng r(seed);
    std::vector<double> w; for (size_t i = 0; i < p.grooves.size(); ++i) w.push_back(1.0 / (1.0 + i * 1.2));
    return p.grooves[r.weighted(w)];
}

namespace {
enum Role { R_KICK, R_SNARE, R_RIM, R_HATC, R_HATO, R_RIDE, R_CRASH, R_TOM, R_BASS, R_VOX, R_PERC, R_COUNT };
Role roleOf(Category c, int ch) {
    switch (c) {
        case Category::Kick: return R_KICK;
        case Category::Snare: case Category::Clap: return R_SNARE;
        case Category::Rim: return R_RIM;
        case Category::ClosedHat: case Category::PedalHat: case Category::Shaker: return R_HATC;
        case Category::OpenHat: return R_HATO;
        case Category::Ride: return R_RIDE;
        case Category::Crash: return R_CRASH;
        case Category::Tom: return R_TOM;
        case Category::Bass808: case Category::SubBass: case Category::BassShot: case Category::DistBass: case Category::SynthBass: return R_BASS;
        case Category::VocalChop: case Category::VocalFx: case Category::Chant: return R_VOX;
        default: (void)ch; return R_PERC;
    }
}
using Grid = std::array<float, 16>;
Grid g(std::initializer_list<int> on, float p = 1.0f) { Grid x{}; for (int s : on) x[(size_t)s] = p; return x; }
Grid all(float p) { Grid x; x.fill(p); return x; }
Grid add(Grid a, const Grid& b) { for (int i = 0; i < 16; ++i) a[(size_t)i] = std::max(a[(size_t)i], b[(size_t)i]); return a; }

std::array<Grid, R_COUNT> templ(Groove gr) {
    std::array<Grid, R_COUNT> t{};
    auto eighthsOff = g({ 2, 6, 10, 14 });
    switch (gr) {
        case Groove::FourFloor: t[R_KICK] = g({ 0, 4, 8, 12 }); t[R_SNARE] = g({ 4, 12 }); t[R_HATC] = add(g({ 2, 6, 10, 14 }, 0.95f), all(0.35f)); t[R_HATO] = g({ 2, 6, 10, 14 }, 0.6f); t[R_PERC] = g({ 3, 7, 11, 15 }, 0.35f); t[R_RIDE] = g({ 0, 4, 8, 12 }, 0.2f); t[R_BASS] = g({ 2, 6, 10, 14 }, 0.4f); break;
        case Groove::Trap: t[R_KICK] = add(g({ 0 }), add(g({ 7, 10 }, 0.6f), g({ 3, 11, 14 }, 0.25f))); t[R_SNARE] = g({ 8 }); t[R_HATC] = all(0.85f); t[R_HATO] = g({ 6, 14 }, 0.3f); t[R_BASS] = add(g({ 0 }), g({ 7, 10 }, 0.5f)); t[R_PERC] = g({ 5, 13 }, 0.25f); t[R_VOX] = g({ 12 }, 0.3f); break;
        case Groove::Breakbeat: t[R_KICK] = add(g({ 0, 10 }), g({ 2, 11 }, 0.35f)); t[R_SNARE] = add(g({ 4, 12 }), g({ 7, 9, 15 }, 0.3f)); t[R_HATC] = g({ 0, 2, 4, 6, 8, 10, 12, 14 }, 0.9f); t[R_HATO] = g({ 14 }, 0.4f); t[R_RIDE] = g({ 0, 4, 8, 12 }, 0.2f); t[R_PERC] = g({ 3, 13 }, 0.3f); break;
        case Groove::HalfTime: t[R_KICK] = add(g({ 0 }), g({ 11, 14 }, 0.45f)); t[R_SNARE] = g({ 8 }); t[R_HATC] = g({ 0, 2, 4, 6, 8, 10, 12, 14 }, 0.8f); t[R_PERC] = g({ 6, 14 }, 0.4f); t[R_BASS] = add(g({ 0 }), g({ 11 }, 0.4f)); t[R_RIM] = g({ 3, 13 }, 0.3f); break;
        case Groove::BoomBap: t[R_KICK] = add(g({ 0 }), add(g({ 10 }, 0.75f), g({ 7, 15 }, 0.4f))); t[R_SNARE] = g({ 4, 12 }); t[R_HATC] = g({ 0, 2, 4, 6, 8, 10, 12, 14 }, 0.9f); t[R_HATO] = g({ 14 }, 0.3f); t[R_RIM] = g({ 7, 15 }, 0.2f); break;
        case Groove::Garage: t[R_KICK] = add(g({ 0 }), add(g({ 10 }, 0.8f), g({ 7 }, 0.3f))); t[R_SNARE] = g({ 4, 12 }); t[R_HATC] = add(eighthsOff, all(0.4f)); t[R_HATO] = g({ 2, 10 }, 0.3f); t[R_PERC] = g({ 3, 6, 11, 14 }, 0.35f); t[R_BASS] = g({ 0, 10 }, 0.5f); t[R_VOX] = g({ 6, 14 }, 0.3f); break;
        case Groove::Hardstyle: t[R_KICK] = g({ 0, 4, 8, 12 }); t[R_SNARE] = g({ 4, 12 }); t[R_HATC] = eighthsOff; t[R_HATO] = g({ 2, 6, 10, 14 }, 0.5f); t[R_CRASH] = g({ 0 }, 0.3f); t[R_RIDE] = g({ 2, 6, 10, 14 }, 0.3f); break;
        case Groove::Industrial: t[R_KICK] = add(g({ 0, 4, 8, 12 }), g({ 3, 11 }, 0.4f)); t[R_SNARE] = g({ 4, 12 }); t[R_PERC] = g({ 2, 6, 10, 14 }, 0.6f); t[R_HATC] = all(0.5f); t[R_TOM] = g({ 7, 15 }, 0.3f); t[R_RIM] = g({ 1, 9 }, 0.3f); break;
        case Groove::Electroclash: t[R_KICK] = g({ 0, 4, 8, 12 }); t[R_SNARE] = g({ 4, 12 }); t[R_HATC] = all(0.7f); t[R_HATO] = g({ 2, 10 }, 0.4f); t[R_PERC] = g({ 7, 15 }, 0.3f); t[R_BASS] = g({ 0, 3, 6, 8, 11, 14 }, 0.4f); break;
        case Groove::Drill: t[R_KICK] = add(g({ 0 }), g({ 3, 10 }, 0.6f)); t[R_SNARE] = add(g({ 8 }), g({ 13 }, 0.6f)); t[R_HATC] = g({ 0, 3, 6, 8, 11, 14 }, 0.9f); t[R_BASS] = add(g({ 0 }), g({ 3, 10 }, 0.5f)); t[R_PERC] = g({ 7, 15 }, 0.3f); break;
        case Groove::Jersey: t[R_KICK] = add(g({ 0, 4, 7, 10, 12 }), g({ 14 }, 0.6f)); t[R_SNARE] = g({ 4, 12 }); t[R_HATC] = eighthsOff; t[R_VOX] = g({ 2, 6, 10, 14 }, 0.4f); t[R_PERC] = g({ 3, 11 }, 0.4f); break;
        case Groove::DnB: t[R_KICK] = add(g({ 0, 10 }), g({ 2 }, 0.2f)); t[R_SNARE] = add(g({ 4, 12 }), g({ 7, 15 }, 0.3f)); t[R_HATC] = g({ 0, 2, 4, 6, 8, 10, 12, 14 }, 0.9f); t[R_RIDE] = g({ 2, 6, 10, 14 }, 0.3f); t[R_BASS] = g({ 0, 10 }, 0.5f); break;
        case Groove::Ambient: t[R_KICK] = add(g({ 0 }, 0.7f), g({ 8 }, 0.3f)); t[R_RIM] = g({ 12 }, 0.4f); t[R_HATC] = all(0.2f); t[R_PERC] = all(0.15f); t[R_VOX] = g({ 6 }, 0.2f); break;
        case Groove::RnB: t[R_KICK] = add(g({ 0 }), g({ 3, 9, 11 }, 0.45f)); t[R_SNARE] = g({ 4, 12 }); t[R_HATC] = g({ 0, 2, 4, 6, 8, 10, 12, 14 }, 0.85f); t[R_RIM] = g({ 7, 15 }, 0.3f); t[R_HATO] = g({ 14 }, 0.3f); t[R_BASS] = add(g({ 0 }), g({ 9 }, 0.4f)); break;
        case Groove::Hyperpop: t[R_KICK] = add(g({ 0, 8 }), g({ 3, 11 }, 0.5f)); t[R_SNARE] = g({ 4, 12 }); t[R_HATC] = all(0.8f); t[R_PERC] = all(0.3f); t[R_VOX] = g({ 6, 14 }, 0.4f); t[R_BASS] = g({ 0, 3, 8, 11 }, 0.4f); break;
        case Groove::Phonk: t[R_KICK] = add(g({ 0, 10 }), g({ 3, 6 }, 0.45f)); t[R_SNARE] = g({ 8 }); t[R_PERC] = all(0.5f); t[R_HATC] = g({ 0, 2, 4, 6, 8, 10, 12, 14 }, 0.8f); t[R_BASS] = add(g({ 0 }), g({ 10 }, 0.6f)); break;
        case Groove::Reggaeton: t[R_KICK] = g({ 0, 4, 8, 12 }); t[R_SNARE] = g({ 3, 6, 11, 14 }); t[R_HATC] = g({ 0, 2, 4, 6, 8, 10, 12, 14 }, 0.8f); t[R_PERC] = g({ 7, 15 }, 0.4f); t[R_BASS] = g({ 0, 3, 8, 11 }, 0.4f); break;
        default: t[R_KICK] = g({ 0, 8 }); t[R_SNARE] = g({ 4, 12 }); t[R_HATC] = all(0.6f); break;
    }
    t[R_CRASH] = add(t[R_CRASH], g({ 0 }, 0.15f));
    return t;
}
}

void randomizePattern(PatternState& st, const SonicProfile& p, Groove gr, uint64_t seed, float density, float humanize) {
    Rng r(seed);
    auto t = templ(gr);
    float energy = p[Desc::Energy];
    float dens = clampf(0.55f + density * 0.6f + (energy - 0.5f) * 0.3f, 0.2f, 1.3f);
    bool seenRole[R_COUNT] = {};
    for (int c = 0; c < kChannels; ++c) {
        Role role = roleOf(st.ch[(size_t)c].role, c);
        bool second = seenRole[role];
        seenRole[role] = true;
        for (int s = 0; s < kSteps; ++s) {
            float prob = t[role][(size_t)s];
            if (second) prob *= (role == R_KICK || role == R_SNARE) ? 0.25f : 0.45f; // a second pad of the same role is sparser
            if (role != R_KICK || s != 0) prob = clampf(prob * dens, 0, 1);
            if (role == R_KICK && s == 0 && !second) prob = 1.0f;
            bool on = prob >= 0.999f || r.chance(prob);
            if (!on) { st.vel[(size_t)c][(size_t)s] = 0; continue; }
            float base = (s % 4 == 0) ? 112.0f : (s % 2 == 0) ? 92.0f : 74.0f;
            if (prob < 0.4f) base -= 18.0f; // ghost-ish
            float v = base + (float)r.gauss() * 10.0f * humanize;
            st.vel[(size_t)c][(size_t)s] = (uint8_t)clampf(v, 20, 127);
        }
    }
    st.bpm = std::round(p.bpm);
    st.swing = clampf(p.swing, 0, 1);
}

// ---------------------------------------------------------------------------
static void putVar(std::vector<uint8_t>& o, uint32_t v) {
    uint8_t buf[5]; int n = 0;
    buf[n++] = v & 0x7F;
    while (v >>= 7) buf[n++] = (uint8_t)((v & 0x7F) | 0x80);
    while (n--) o.push_back(buf[n]);
}
static void put32(std::vector<uint8_t>& o, uint32_t v) { for (int i = 3; i >= 0; --i) o.push_back((uint8_t)(v >> (i * 8))); }
static void put16(std::vector<uint8_t>& o, uint16_t v) { o.push_back((uint8_t)(v >> 8)); o.push_back((uint8_t)v); }

std::vector<uint8_t> patternToMidi(const PatternState& st, int bars) {
    const int ppq = 96, stepTicks = ppq / 4;
    struct Ev { uint32_t tick; uint8_t a, b, c; };
    std::vector<Ev> ev;
    int len = std::clamp(st.length, 1, kSteps);
    for (int bar = 0; bar < bars; ++bar)
        for (int k = 0; k < 16; ++k) {
            int s = k % len;
            uint32_t globalStep = (uint32_t)(bar * 16 + k);
            uint32_t tick = globalStep * (uint32_t)stepTicks + ((globalStep & 1) ? (uint32_t)std::lround(st.swing * 0.33 * stepTicks) : 0u);
            for (int c = 0; c < kChannels; ++c) {
                uint8_t v = st.vel[(size_t)c][(size_t)s];
                if (!v || st.ch[(size_t)c].mute) continue;
                uint8_t note = (uint8_t)(kBaseMidiNote + c);
                ev.push_back({ tick, 0x99, note, v });                       // channel 10 note on
                ev.push_back({ tick + (uint32_t)stepTicks - 2, 0x89, note, 0 }); // note off
            }
        }
    std::stable_sort(ev.begin(), ev.end(), [](const Ev& a, const Ev& b) { return a.tick < b.tick || (a.tick == b.tick && a.a < b.a); });
    std::vector<uint8_t> trk;
    // tempo + time signature + track name
    putVar(trk, 0); trk.insert(trk.end(), { 0xFF, 0x51, 0x03 });
    uint32_t uspq = (uint32_t)std::lround(60000000.0 / clampd(st.bpm, 20, 400));
    trk.push_back((uint8_t)(uspq >> 16)); trk.push_back((uint8_t)(uspq >> 8)); trk.push_back((uint8_t)uspq);
    putVar(trk, 0); trk.insert(trk.end(), { 0xFF, 0x58, 0x04, 0x04, 0x02, 0x18, 0x08 });
    const char* name = "SONIC DNA Pattern";
    putVar(trk, 0); trk.insert(trk.end(), { 0xFF, 0x03 }); putVar(trk, (uint32_t)std::strlen(name)); trk.insert(trk.end(), name, name + std::strlen(name));
    uint32_t last = 0;
    for (auto& e : ev) { putVar(trk, e.tick - last); last = e.tick; trk.push_back(e.a); trk.push_back(e.b); trk.push_back(e.c); }
    uint32_t endTick = (uint32_t)(bars * 16 * stepTicks);
    putVar(trk, endTick > last ? endTick - last : 0); trk.insert(trk.end(), { 0xFF, 0x2F, 0x00 });
    std::vector<uint8_t> out = { 'M', 'T', 'h', 'd' };
    put32(out, 6); put16(out, 0); put16(out, 1); put16(out, (uint16_t)ppq);
    out.insert(out.end(), { 'M', 'T', 'r', 'k' });
    put32(out, (uint32_t)trk.size());
    out.insert(out.end(), trk.begin(), trk.end());
    return out;
}

} // namespace sdna
