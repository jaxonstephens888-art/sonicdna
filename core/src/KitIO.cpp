#include "sdna/KitIO.h"
#include "sdna/KitGenerator.h"

#include <cstdio>
#include <cstring>
#include <fstream>

namespace sdna {

// ---------------------------------------------------------------- bytes
static void le16(std::vector<uint8_t>& o, uint16_t v) { o.push_back((uint8_t)v); o.push_back((uint8_t)(v >> 8)); }
static void le32(std::vector<uint8_t>& o, uint32_t v) { for (int i = 0; i < 4; ++i) o.push_back((uint8_t)(v >> (8 * i))); }
static void tag(std::vector<uint8_t>& o, const char* t) { o.insert(o.end(), t, t + 4); }
static uint16_t rd16(const uint8_t* p) { return (uint16_t)(p[0] | (p[1] << 8)); }
static uint32_t rd32(const uint8_t* p) { return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24); }

// ---------------------------------------------------------------- WAV
std::vector<uint8_t> encodeWav(const Buffer& src, const AudioFormat& fmt, int rootNote, bool loop) {
    Buffer b = fx::resampleTo(src, fmt.sampleRate);
    const int ch = std::max(1, b.numChannels());
    const int bits = (fmt.bitDepth == 16 || fmt.bitDepth == 24 || fmt.bitDepth == 32) ? fmt.bitDepth : 24;
    const bool isFloat = bits == 32;
    const int bps = bits / 8;
    const uint32_t frames = (uint32_t)b.length();
    const uint32_t dataBytes = frames * (uint32_t)(ch * bps);
    std::vector<uint8_t> o;
    o.reserve(dataBytes + 200);
    tag(o, "RIFF"); le32(o, 0); tag(o, "WAVE");
    tag(o, "fmt "); le32(o, 16); le16(o, isFloat ? 3 : 1); le16(o, (uint16_t)ch); le32(o, (uint32_t)fmt.sampleRate);
    le32(o, (uint32_t)fmt.sampleRate * (uint32_t)(ch * bps)); le16(o, (uint16_t)(ch * bps)); le16(o, (uint16_t)bits);
    if (rootNote >= 0 || loop) {
        tag(o, "smpl"); le32(o, 36 + (loop ? 24 : 0));
        le32(o, 0); le32(o, 0); le32(o, (uint32_t)(1.0e9 / fmt.sampleRate)); le32(o, (uint32_t)(rootNote >= 0 ? rootNote : 60)); le32(o, 0);
        le32(o, 0); le32(o, 0); le32(o, loop ? 1 : 0); le32(o, 0);
        if (loop) { le32(o, 0); le32(o, 0); le32(o, 0); le32(o, frames ? frames - 1 : 0); le32(o, 0); le32(o, 0); }
    }
    tag(o, "data"); le32(o, dataBytes);
    Rng dither(0xD17E5ull);
    for (uint32_t i = 0; i < frames; ++i) {
        for (int c = 0; c < ch; ++c) {
            float s = b.ch[(size_t)c][i];
            if (isFloat) { uint32_t u; std::memcpy(&u, &s, 4); le32(o, u); continue; }
            double maxv = bits == 16 ? 32767.0 : 8388607.0;
            double x = (double)clampf(s, -1.0f, 1.0f) * maxv;
            if (bits == 16 && fmt.dither) x += (dither.uni() - dither.uni());
            long v = std::lround(x);
            v = std::max(-(long)maxv - 1, std::min((long)maxv, v));
            if (bits == 16) le16(o, (uint16_t)(int16_t)v);
            else { o.push_back((uint8_t)(v & 0xFF)); o.push_back((uint8_t)((v >> 8) & 0xFF)); o.push_back((uint8_t)((v >> 16) & 0xFF)); }
        }
    }
    uint32_t riff = (uint32_t)o.size() - 8;
    std::memcpy(&o[4], &riff, 4); // little-endian hosts; fix below for portability
    o[4] = (uint8_t)riff; o[5] = (uint8_t)(riff >> 8); o[6] = (uint8_t)(riff >> 16); o[7] = (uint8_t)(riff >> 24);
    return o;
}

bool decodeWav(const std::vector<uint8_t>& d, Buffer& out, int* rootNote, std::string* error) {
    auto fail = [&](const char* m) { if (error) *error = m; return false; };
    if (d.size() < 12 || std::memcmp(d.data(), "RIFF", 4) || std::memcmp(d.data() + 8, "WAVE", 4)) return fail("not a RIFF/WAVE file");
    size_t p = 12;
    int fmtTag = 0, ch = 0, bits = 0; uint32_t rate = 0;
    const uint8_t* data = nullptr; uint32_t dataLen = 0;
    if (rootNote) *rootNote = -1;
    while (p + 8 <= d.size()) {
        const uint8_t* c = d.data() + p;
        uint32_t len = rd32(c + 4);
        if (p + 8 + len > d.size()) len = (uint32_t)(d.size() - p - 8);
        if (!std::memcmp(c, "fmt ", 4) && len >= 16) {
            fmtTag = rd16(c + 8); ch = rd16(c + 10); rate = rd32(c + 12); bits = rd16(c + 22);
            if (fmtTag == 0xFFFE && len >= 40) fmtTag = rd16(c + 32); // WAVE_FORMAT_EXTENSIBLE subformat
        } else if (!std::memcmp(c, "data", 4)) { data = c + 8; dataLen = len; }
        else if (!std::memcmp(c, "smpl", 4) && len >= 36 && rootNote) { *rootNote = (int)rd32(c + 8 + 12); }
        p += 8 + len + (len & 1);
    }
    if (!data || ch <= 0 || rate == 0) return fail("missing fmt or data chunk");
    if (!((fmtTag == 1 && (bits == 16 || bits == 24 || bits == 32 || bits == 8)) || (fmtTag == 3 && bits == 32))) return fail("unsupported sample format");
    int bps = bits / 8;
    uint32_t frames = dataLen / (uint32_t)(bps * ch);
    out = Buffer(std::min(ch, 2), (int)frames, rate);
    for (uint32_t i = 0; i < frames; ++i) for (int c = 0; c < ch; ++c) {
        const uint8_t* s = data + ((size_t)i * ch + c) * bps;
        float v;
        if (fmtTag == 3) { uint32_t u = rd32(s); std::memcpy(&v, &u, 4); }
        else if (bits == 16) v = (int16_t)rd16(s) / 32768.0f;
        else if (bits == 24) { int32_t x = (int32_t)((uint32_t)s[0] << 8 | (uint32_t)s[1] << 16 | (uint32_t)s[2] << 24) >> 8; v = x / 8388608.0f; }
        else if (bits == 32) v = (int32_t)rd32(s) / 2147483648.0f;
        else v = (s[0] - 128) / 128.0f;
        if (c < 2) out.ch[(size_t)c][i] = v;
    }
    return true;
}

bool writeFile(const std::string& path, const std::vector<uint8_t>& data) {
    std::ofstream f(path, std::ios::binary);
    if (!f) return false;
    f.write((const char*)data.data(), (std::streamsize)data.size());
    return (bool)f;
}
bool readFile(const std::string& path, std::vector<uint8_t>& data) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    f.seekg(0, std::ios::end); auto n = f.tellg(); f.seekg(0);
    data.resize((size_t)n);
    f.read((char*)data.data(), n);
    return (bool)f;
}

// ---------------------------------------------------------------- ZIP
uint32_t crc32(const uint8_t* data, size_t n, uint32_t crc) {
    static uint32_t table[256]; static bool init = false;
    if (!init) { for (uint32_t i = 0; i < 256; ++i) { uint32_t c = i; for (int k = 0; k < 8; ++k) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1; table[i] = c; } init = true; }
    crc = ~crc;
    for (size_t i = 0; i < n; ++i) crc = table[(crc ^ data[i]) & 0xFF] ^ (crc >> 8);
    return ~crc;
}

void ZipWriter::add(const std::string& path, const std::vector<uint8_t>& data) {
    E e{ path, crc32(data.data(), data.size()), (uint32_t)data.size(), (uint32_t)buf.size() };
    le32(buf, 0x04034b50); le16(buf, 20); le16(buf, 0x0800 /*UTF-8*/); le16(buf, 0); le16(buf, 0); le16(buf, 0x21);
    le32(buf, e.crc); le32(buf, e.size); le32(buf, e.size); le16(buf, (uint16_t)path.size()); le16(buf, 0);
    buf.insert(buf.end(), path.begin(), path.end());
    buf.insert(buf.end(), data.begin(), data.end());
    entries.push_back(e);
}
void ZipWriter::add(const std::string& path, const std::string& text) { add(path, std::vector<uint8_t>(text.begin(), text.end())); }

std::vector<uint8_t> ZipWriter::finish() {
    uint32_t cdStart = (uint32_t)buf.size();
    for (auto& e : entries) {
        le32(buf, 0x02014b50); le16(buf, 20); le16(buf, 20); le16(buf, 0x0800); le16(buf, 0); le16(buf, 0); le16(buf, 0x21);
        le32(buf, e.crc); le32(buf, e.size); le32(buf, e.size); le16(buf, (uint16_t)e.name.size()); le16(buf, 0); le16(buf, 0);
        le16(buf, 0); le16(buf, 0); le32(buf, 0); le32(buf, e.offset);
        buf.insert(buf.end(), e.name.begin(), e.name.end());
    }
    uint32_t cdSize = (uint32_t)buf.size() - cdStart;
    le32(buf, 0x06054b50); le16(buf, 0); le16(buf, 0); le16(buf, (uint16_t)entries.size()); le16(buf, (uint16_t)entries.size());
    le32(buf, cdSize); le32(buf, cdStart); le16(buf, 0);
    return std::move(buf);
}

bool readZip(const std::vector<uint8_t>& z, std::vector<std::pair<std::string, std::vector<uint8_t>>>& out, std::string* error) {
    auto fail = [&](const char* m) { if (error) *error = m; return false; };
    if (z.size() < 22) return fail("file too small to be a zip");
    size_t eocd = std::string::npos;
    for (size_t i = z.size() - 22 + 1; i-- > 0 && z.size() - i < 70000;) if (rd32(&z[i]) == 0x06054b50) { eocd = i; break; }
    if (eocd == std::string::npos) return fail("zip end record not found");
    uint16_t n = rd16(&z[eocd + 10]);
    uint32_t cd = rd32(&z[eocd + 16]);
    size_t p = cd;
    for (int k = 0; k < n; ++k) {
        if (p + 46 > z.size() || rd32(&z[p]) != 0x02014b50) return fail("corrupt central directory");
        uint16_t method = rd16(&z[p + 10]);
        uint32_t csize = rd32(&z[p + 20]);
        uint16_t nl = rd16(&z[p + 28]), xl = rd16(&z[p + 30]), cl = rd16(&z[p + 32]);
        uint32_t off = rd32(&z[p + 42]);
        std::string name((const char*)&z[p + 46], nl);
        p += 46 + nl + xl + cl;
        if (method != 0) return fail("compressed zip entries are not supported (expected a SONIC DNA kit)");
        if (off + 30 > z.size()) return fail("corrupt local header");
        uint16_t lnl = rd16(&z[off + 26]), lxl = rd16(&z[off + 28]);
        size_t dstart = off + 30 + lnl + lxl;
        if (dstart + csize > z.size()) return fail("truncated zip entry");
        out.push_back({ name, std::vector<uint8_t>(z.begin() + (long)dstart, z.begin() + (long)(dstart + csize)) });
    }
    return true;
}

// ---------------------------------------------------------------- JSON
Json profileToJson(const SonicProfile& p) {
    Json j = Json::object();
    j["query"] = p.query; j["name"] = p.displayName; j["source"] = p.source; j["confidence"] = p.confidence;
    j["summary"] = p.summary;
    Json d = Json::object(); for (int i = 0; i < kNumDesc; ++i) d[descId((Desc)i)] = std::round(p.d[(size_t)i] * 1000) / 1000.0;
    j["descriptors"] = d;
    j["bpm"] = p.bpm; j["swing"] = p.swing; j["key"] = p.keyRoot; j["scale"] = scaleName(p.scale);
    Json g = Json::array(); for (auto x : p.grooves) g.push(grooveId(x)); j["grooves"] = g;
    Json gen = Json::array(); for (auto& x : p.genres) gen.push(x); j["genres"] = gen;
    Json sig = Json::array(); for (auto& x : p.signatures) sig.push(x); j["signatures"] = sig;
    return j;
}

SonicProfile profileFromJson(const Json& j) {
    SonicProfile p = neutralProfile();
    p.query = j.get("query").str(); p.displayName = j.get("name").str(); p.source = j.get("source").str("custom");
    p.confidence = (float)j.get("confidence").num(0.5); p.summary = j.get("summary").str();
    const Json& d = j.get("descriptors");
    for (auto& kv : d.members()) { Desc x; if (descFromId(kv.first, x)) p[x] = clampf((float)kv.second.num(0.5), 0, 1); }
    p.bpm = (float)clampd(j.get("bpm").num(120), 40, 220); p.swing = (float)clampd(j.get("swing").num(0), 0, 1);
    p.keyRoot = (int)j.get("key").num(-1); if (p.keyRoot > 11) p.keyRoot = -1;
    Scale sc; if (scaleFromName(j.get("scale").str(), sc)) p.scale = sc;
    if (j.get("grooves").isArray()) { p.grooves.clear(); for (auto& g : j.get("grooves").items()) { Groove x; if (grooveFromId(g.str(), x)) p.grooves.push_back(x); } }
    if (p.grooves.empty()) p.grooves = { Groove::FourFloor };
    for (auto& g : j.get("genres").items()) p.genres.push_back(g.str());
    for (auto& g : j.get("signatures").items()) p.signatures.push_back(g.str());
    return p;
}

Json patternToJson(const PatternState& st) {
    Json j = Json::object();
    j["length"] = st.length; j["bpm"] = st.bpm; j["swing"] = st.swing; j["master_db"] = st.masterDb;
    Json chs = Json::array();
    for (int c = 0; c < kChannels; ++c) {
        const auto& cs = st.ch[(size_t)c];
        Json x = Json::object();
        x["sample"] = cs.sampleId; x["role"] = categoryInfo(cs.role).id;
        x["volume_db"] = cs.volumeDb; x["pan"] = cs.pan; x["pitch"] = cs.pitch; x["tone"] = cs.tone; x["drive"] = cs.drive;
        x["reverb"] = cs.reverb; x["delay"] = cs.delay; x["decay"] = cs.decay; x["mute"] = cs.mute; x["solo"] = cs.solo; x["choke"] = cs.choke;
        Json steps = Json::array(); for (int k = 0; k < kSteps; ++k) steps.push((int)st.vel[(size_t)c][(size_t)k]);
        x["steps"] = steps;
        chs.push(x);
    }
    j["channels"] = chs;
    return j;
}

PatternState patternFromJson(const Json& j) {
    PatternState st;
    auto roles = defaultChannelRoles();
    for (int c = 0; c < kChannels; ++c) st.ch[(size_t)c].role = roles[(size_t)c];
    st.length = (int)j.get("length").num(16); st.bpm = (float)j.get("bpm").num(120); st.swing = (float)j.get("swing").num(0); st.masterDb = (float)j.get("master_db").num(0);
    const Json& chs = j.get("channels");
    for (int c = 0; c < kChannels && c < (int)chs.size(); ++c) {
        const Json& x = chs[(size_t)c]; auto& cs = st.ch[(size_t)c];
        cs.sampleId = x.get("sample").str();
        Category cat; if (categoryFromId(x.get("role").str(), cat)) cs.role = cat;
        cs.volumeDb = (float)x.get("volume_db").num(0); cs.pan = (float)x.get("pan").num(0); cs.pitch = (float)x.get("pitch").num(0); cs.tone = (float)x.get("tone").num(0);
        cs.drive = (float)x.get("drive").num(0); cs.reverb = (float)x.get("reverb").num(0); cs.delay = (float)x.get("delay").num(0); cs.decay = (float)x.get("decay").num(1);
        cs.mute = x.get("mute").boolean(); cs.solo = x.get("solo").boolean(); cs.choke = (int)x.get("choke").num(0);
        const Json& steps = x.get("steps");
        for (int k = 0; k < kSteps && k < (int)steps.size(); ++k) st.vel[(size_t)c][(size_t)k] = (uint8_t)clampd(steps[(size_t)k].num(0), 0, 127);
    }
    return st;
}

std::string samplePath(const Kit& kit, const Sample& s) {
    const auto& ci = categoryInfo(s.category);
    return std::string(groupFolder(ci.group)) + "/" + ci.label + "/" + s.fileStem(kit.slug()) + ".wav";
}

static Json dnaToJson(const KitDNA& d) {
    Json j = Json::object();
    j["seed"] = std::to_string(d.seed); j["key"] = noteName(d.keyRoot); j["key_pc"] = d.keyRoot; j["scale"] = scaleName(d.scale);
    j["bpm"] = d.bpm; j["swing"] = d.swing; j["saturation"] = satTypeName(d.satType); j["sat_amount"] = d.satAmount;
    j["crush_bits"] = d.crushBits; j["crush_downsample"] = d.crushDownsample; j["crush_mix"] = d.crushMix;
    j["room_size"] = d.roomSize; j["room_damp"] = d.roomDamp; j["room_wet"] = d.roomWet; j["predelay_ms"] = d.predelayMs;
    j["tape_wow"] = d.tapeWow; j["noise_floor_db"] = d.noiseFloorDb; j["vinyl"] = d.vinyl; j["tone"] = d.tone;
    j["drum_tune"] = d.drumTune; j["transient_db"] = d.transientDb; j["fm_bias"] = d.fmBias; j["kick_note"] = d.kickNote;
    Json pr = Json::array(); for (int x : d.progression) pr.push(x); j["progression"] = pr;
    if (d.ref.valid) {
        Json r = Json::object();
        r["weight"] = d.ref.weight; r["kick_hz"] = d.ref.kickHz; r["kick_decay"] = d.ref.kickDecay; r["kick_drop"] = d.ref.kickDrop;
        r["snare_hz"] = d.ref.snareHz; r["snare_decay"] = d.ref.snareDecay; r["snare_bright"] = d.ref.snareBright;
        r["hat_decay"] = d.ref.hatDecay; r["hat_bright"] = d.ref.hatBright; r["bandwidth_hz"] = d.ref.bandwidthHz;
        if (d.ref.hasGrid) { Json g = Json::array(); for (auto& row : d.ref.grid) { Json rr = Json::array(); for (float v : row) rr.push(std::round(v * 1000) / 1000.0); g.push(rr); } r["grid"] = g; }
        j["reference_targets"] = r;
    }
    return j;
}
static KitDNA dnaFromJson(const Json& j) {
    KitDNA d;
    d.seed = std::strtoull(j.get("seed").str("1").c_str(), nullptr, 10);
    d.keyRoot = (int)j.get("key_pc").num(9); Scale sc; if (scaleFromName(j.get("scale").str(), sc)) d.scale = sc;
    d.bpm = (float)j.get("bpm").num(120); d.swing = (float)j.get("swing").num(0);
    std::string st = j.get("saturation").str("soft");
    for (int t = 0; t <= (int)SatType::Bitcrush; ++t) if (st == satTypeName((SatType)t)) d.satType = (SatType)t;
    d.satAmount = (float)j.get("sat_amount").num(0.2); d.crushBits = (int)j.get("crush_bits").num(16); d.crushDownsample = (int)j.get("crush_downsample").num(1);
    d.crushMix = (float)j.get("crush_mix").num(0); d.roomSize = (float)j.get("room_size").num(0.5); d.roomDamp = (float)j.get("room_damp").num(0.5);
    d.roomWet = (float)j.get("room_wet").num(0.15); d.predelayMs = (float)j.get("predelay_ms").num(12); d.tapeWow = (float)j.get("tape_wow").num(0);
    d.noiseFloorDb = (float)j.get("noise_floor_db").num(-120); d.vinyl = j.get("vinyl").boolean(); d.tone = (float)j.get("tone").num(0);
    d.drumTune = (float)j.get("drum_tune").num(0); d.transientDb = (float)j.get("transient_db").num(0); d.fmBias = (float)j.get("fm_bias").num(0);
    d.kickNote = (int)j.get("kick_note").num(33);
    d.progression.clear(); for (auto& x : j.get("progression").items()) d.progression.push_back((int)x.num(0));
    if (d.progression.empty()) d.progression = { 0, 5, 2, 6 };
    if (j.has("reference_targets")) {
        const Json& r = j.get("reference_targets");
        d.ref.valid = true; d.ref.weight = (float)r.get("weight").num(0.7);
        d.ref.kickHz = (float)r.get("kick_hz").num(0); d.ref.kickDecay = (float)r.get("kick_decay").num(0); d.ref.kickDrop = (float)r.get("kick_drop").num(0);
        d.ref.snareHz = (float)r.get("snare_hz").num(0); d.ref.snareDecay = (float)r.get("snare_decay").num(0); d.ref.snareBright = (float)r.get("snare_bright").num(0);
        d.ref.hatDecay = (float)r.get("hat_decay").num(0); d.ref.hatBright = (float)r.get("hat_bright").num(0); d.ref.bandwidthHz = (float)r.get("bandwidth_hz").num(0);
        if (r.get("grid").size() == 3) { d.ref.hasGrid = true; for (int b = 0; b < 3; ++b) for (int s = 0; s < 16; ++s) d.ref.grid[(size_t)b][(size_t)s] = (float)r.get("grid")[(size_t)b][(size_t)s].num(0); }
    }
    return d;
}

Json kitToJson(const Kit& kit, const PatternState* pattern, const std::string& audioDirPrefix) {
    Json j = Json::object();
    j["format"] = "sonic-dna-kit"; j["version"] = 1;
    j["name"] = kit.name; j["prompt"] = kit.prompt; j["created"] = kit.createdAt;
    j["seed"] = std::to_string(kit.seed); j["sample_rate"] = kit.sampleRate; j["per_category"] = kit.perCategory;
    j["profile"] = profileToJson(kit.profile);
    Json inf = Json::object();
    inf["drums"] = kit.influence.drums; inf["tonal"] = kit.influence.tonal; inf["texture"] = kit.influence.texture;
    inf["processing"] = kit.influence.processing; inf["groove"] = kit.influence.groove; inf["variation"] = kit.influence.variation;
    j["influence"] = inf;
    j["dna"] = dnaToJson(kit.dna);
    Json arr = Json::array();
    for (auto& s : kit.samples) {
        Json x = Json::object();
        x["id"] = s->id; x["name"] = s->name; x["category"] = categoryInfo(s->category).id; x["variant"] = s->variant;
        x["seed"] = std::to_string(s->seed); x["engine"] = s->engine; x["recipe"] = s->recipe; x["root_note"] = s->rootNote;
        x["loop"] = s->isLoop; x["bpm"] = s->bpm; x["seconds"] = std::round(s->audio.seconds() * 1000) / 1000.0;
        x["file"] = audioDirPrefix + samplePath(kit, *s);
        arr.push(x);
    }
    j["samples"] = arr;
    if (pattern) j["pattern"] = patternToJson(*pattern);
    return j;
}

static std::string readme(const Kit& kit, const ExportOptions& opt) {
    std::string s;
    s += "SONIC DNA — " + kit.name + "\n";
    s += std::string(40, '=') + "\n\n";
    s += "Generated: " + kit.createdAt + "\n";
    s += "Reference: " + kit.profile.displayName + " (" + kit.profile.source + ")\n";
    if (!kit.prompt.empty()) s += "Direction: " + kit.prompt + "\n";
    s += "Key: " + std::string(noteName(kit.dna.keyRoot)) + " " + scaleName(kit.dna.scale) + "   Tempo: " + std::to_string((int)kit.dna.bpm) + " BPM\n";
    s += "Format: " + std::to_string((int)opt.format.sampleRate) + " Hz / " + std::to_string(opt.format.bitDepth) + "-bit WAV\n\n";
    s += "Every sound in this pack was synthesised procedurally by SONIC DNA's DSP engines.\n";
    s += "No recordings or third-party samples were used. The kit is *inspired by* the general\n";
    s += "sonic characteristics of the reference and does not reproduce any recording.\n\n";
    s += "Tonal samples carry their root note in the filename and in the WAV 'smpl' chunk.\n";
    s += "MIDI patterns map pad 1..16 to notes C1 (36) .. D#2 (51), channel 10.\n";
    return s;
}

std::vector<uint8_t> buildKitZip(const Kit& kit, const PatternState* pattern, const std::array<PlaybackPtr, kChannels>* pads, const ExportOptions& opt) {
    ZipWriter z;
    std::string root = "SonicDNA_" + kit.slug() + "/";
    for (auto& s : kit.samples) z.add(root + samplePath(kit, *s), encodeWav(s->audio, opt.format, s->rootNote, s->isLoop));
    if (pattern && opt.includePatternMidi) z.add(root + "Patterns/" + kit.slug() + "_Pattern_" + std::to_string((int)pattern->bpm) + "bpm.mid", patternToMidi(*pattern, opt.loopBars));
    if (pattern && pads && opt.includeLoop) {
        Buffer loop = DrumMachine::renderLoop(*pattern, *pads, opt.loopBars, opt.format.sampleRate, true);
        fx::normalize(loop, -1.0f);
        z.add(root + "Loops/" + kit.slug() + "_DrumLoop_" + std::to_string((int)pattern->bpm) + "bpm.wav", encodeWav(loop, opt.format, -1, true));
    }
    z.add(root + "kit.json", kitToJson(kit, pattern, "").dump(2));
    z.add(root + "README.txt", readme(kit, opt));
    return z.finish();
}

bool exportKitZip(const Kit& kit, const PatternState* pattern, const std::array<PlaybackPtr, kChannels>* pads, const ExportOptions& opt, const std::string& path) {
    return writeFile(path, buildKitZip(kit, pattern, pads, opt));
}

bool saveKitFile(const Kit& kit, const PatternState* pattern, const std::string& path, std::string* error) {
    ZipWriter z;
    AudioFormat f; f.sampleRate = kit.sampleRate; f.bitDepth = 32;
    for (auto& s : kit.samples) z.add("audio/" + s->id + ".wav", encodeWav(s->audio, f, s->rootNote, s->isLoop));
    Json j = kitToJson(kit, pattern, "");
    Json arr = Json::array();
    for (auto& x : j.get("samples").items()) { Json y = x; y["file"] = "audio/" + x.get("id").str() + ".wav"; arr.push(y); }
    j["samples"] = arr;
    z.add("kit.json", j.dump(2));
    if (!writeFile(path, z.finish())) { if (error) *error = "could not write " + path; return false; }
    return true;
}

bool loadKitFile(const std::string& path, Kit& kit, PatternState* pattern, std::string* error) {
    std::vector<uint8_t> bytes;
    if (!readFile(path, bytes)) { if (error) *error = "could not read " + path; return false; }
    std::vector<std::pair<std::string, std::vector<uint8_t>>> entries;
    if (!readZip(bytes, entries, error)) return false;
    const std::vector<uint8_t>* js = nullptr;
    for (auto& e : entries) if (e.first == "kit.json") js = &e.second;
    if (!js) { if (error) *error = "kit.json missing"; return false; }
    std::string perr;
    Json j = Json::parse(std::string(js->begin(), js->end()), &perr);
    if (j.get("format").str() != "sonic-dna-kit") { if (error) *error = "not a SONIC DNA kit" + (perr.empty() ? "" : ": " + perr); return false; }
    Kit k;
    k.name = j.get("name").str(); k.prompt = j.get("prompt").str(); k.createdAt = j.get("created").str();
    k.seed = std::strtoull(j.get("seed").str("1").c_str(), nullptr, 10);
    k.sampleRate = j.get("sample_rate").num(44100); k.perCategory = (int)j.get("per_category").num(8);
    k.profile = profileFromJson(j.get("profile"));
    const Json& inf = j.get("influence");
    k.influence.drums = (float)inf.get("drums").num(1); k.influence.tonal = (float)inf.get("tonal").num(1); k.influence.texture = (float)inf.get("texture").num(1);
    k.influence.processing = (float)inf.get("processing").num(1); k.influence.groove = (float)inf.get("groove").num(1); k.influence.variation = (float)inf.get("variation").num(0.5);
    k.dna = dnaFromJson(j.get("dna"));
    for (auto& x : j.get("samples").items()) {
        auto s = std::make_shared<Sample>();
        s->id = x.get("id").str(); s->name = x.get("name").str();
        if (!categoryFromId(x.get("category").str(), s->category)) continue;
        s->variant = (int)x.get("variant").num(0); s->seed = std::strtoull(x.get("seed").str("0").c_str(), nullptr, 10);
        s->engine = x.get("engine").str(); s->recipe = x.get("recipe").str(); s->rootNote = (int)x.get("root_note").num(-1);
        s->isLoop = x.get("loop").boolean(); s->bpm = (float)x.get("bpm").num(0);
        std::string file = x.get("file").str();
        bool found = false;
        for (auto& e : entries) if (e.first == file) { int rn; std::string werr; found = decodeWav(e.second, s->audio, &rn, &werr); break; }
        if (!found) { if (error) *error = "missing audio for " + s->id; return false; }
        k.samples.push_back(s);
    }
    if (pattern && j.has("pattern")) *pattern = patternFromJson(j.get("pattern"));
    kit = std::move(k);
    return true;
}

} // namespace sdna
