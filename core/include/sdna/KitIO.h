// SONIC DNA — WAV, ZIP, kit save/load and export.
#pragma once

#include "sdna/DrumMachine.h"
#include "sdna/Json.h"
#include "sdna/Kit.h"

#include <cstdint>
#include <string>
#include <vector>

namespace sdna {

struct AudioFormat {
    double sampleRate = 44100.0; // 44100 or 48000 (any rate is accepted)
    int bitDepth = 24;           // 16, 24 or 32 (float)
    bool dither = true;          // TPDF dither when reducing to 16-bit
};

// WAV encode/decode. rootNote/loop metadata are written in a 'smpl' chunk.
std::vector<uint8_t> encodeWav(const Buffer& b, const AudioFormat& fmt, int rootNote = -1, bool loop = false);
bool decodeWav(const std::vector<uint8_t>& data, Buffer& out, int* rootNote = nullptr, std::string* error = nullptr);

bool writeFile(const std::string& path, const std::vector<uint8_t>& data);
bool readFile(const std::string& path, std::vector<uint8_t>& data);

// Minimal ZIP (store method). Output is a standard ZIP readable everywhere.
class ZipWriter {
public:
    void add(const std::string& path, const std::vector<uint8_t>& data);
    void add(const std::string& path, const std::string& text);
    std::vector<uint8_t> finish();
    size_t count() const { return entries.size(); }
private:
    struct E { std::string name; uint32_t crc, size, offset; };
    std::vector<E> entries;
    std::vector<uint8_t> buf;
};
// Reads stored (uncompressed) entries. Deflated entries are reported as unsupported.
bool readZip(const std::vector<uint8_t>& zip, std::vector<std::pair<std::string, std::vector<uint8_t>>>& entries, std::string* error = nullptr);
uint32_t crc32(const uint8_t* data, size_t n, uint32_t crc = 0);

// JSON (de)serialisation
Json profileToJson(const SonicProfile& p);
SonicProfile profileFromJson(const Json& j);
Json patternToJson(const PatternState& st);
PatternState patternFromJson(const Json& j);
Json kitToJson(const Kit& kit, const PatternState* pattern, const std::string& audioDirPrefix);

std::string samplePath(const Kit& kit, const Sample& s); // "Drums/Kicks/<stem>.wav"

struct ExportOptions {
    AudioFormat format;
    bool includePatternMidi = true;
    bool includeLoop = true;
    int loopBars = 2;
};

// Full kit as a sample-pack ZIP (folders per group/category + kit.json + README + MIDI + loop).
std::vector<uint8_t> buildKitZip(const Kit& kit, const PatternState* pattern, const std::array<PlaybackPtr, kChannels>* padSamples, const ExportOptions& opt);
bool exportKitZip(const Kit& kit, const PatternState* pattern, const std::array<PlaybackPtr, kChannels>* padSamples, const ExportOptions& opt, const std::string& path);

// Native kit file (.sdnakit): a ZIP with kit.json + 32-bit float WAVs (lossless round trip).
bool saveKitFile(const Kit& kit, const PatternState* pattern, const std::string& path, std::string* error = nullptr);
bool loadKitFile(const std::string& path, Kit& kit, PatternState* pattern, std::string* error = nullptr);

} // namespace sdna
