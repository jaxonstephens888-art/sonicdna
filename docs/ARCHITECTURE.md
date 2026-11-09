# SONIC DNA — Technical architecture

## 1. Design decisions

| Decision | Choice | Why |
|---|---|---|
| Plugin framework | **JUCE 8 (C++17)** | One codebase → VST3, AU (macOS), Standalone. Mature hosting, GUI, audio I/O. |
| Sound engine | **Framework-free C++ core** (`core/`) | Testable headless (CLI + unit tests), reusable outside JUCE, no audio-thread surprises. |
| Sound generation | **Procedural DSP synthesis** (built in) + **pluggable engine interface** for AI models later | Generates genuinely original audio offline, deterministically, in seconds, with no licensing risk. |
| Artist analysis | **Offline descriptor database** (always) + **optional Claude API with web search** | Works instantly offline; online research is opt-in and clearly labelled. |
| Kit format | `.sdnakit` = ZIP (store) of `kit.json` + 32-bit float WAVs | Lossless, inspectable, versionable. |
| Exports | WAV 16 (TPDF dither) / 24 / 32f at 44.1/48 kHz with `smpl` root/loop chunk, SMF type-0 MIDI, ZIP | Works in every DAW. |

## 2. What is AI, what is DSP, what is plain programming

| Component | Technique | Network? |
|---|---|---|
| Reference-audio analysis | **DSP / music-information retrieval**: STFT, band-wise spectral flux, autocorrelation tempo with octave correction, beat/downbeat alignment, 16-step groove grids, swing from onset offsets, chroma + Krumhansl/diatonic key estimation with bass tie-break, per-bar chord templates, harmonic/percussive median filtering, per-hit kick/snare/hat measurement (median spectra, background-corrected decay), long-term spectrum, dynamics and stereo width (`core/src/AudioAnalysis.cpp`) | No |
| Offline artist/genre analysis | Hand-curated knowledge base (89 artists, 43 genres) + fuzzy matching, blending ("X meets Y"), descriptor parsing, seeded fallback | No |
| Online artist research | **AI**: Claude (`claude-opus-5-5`) Messages API + server-side `web_search` tool, returns a JSON sonic profile | **Yes** + API key |
| Prompt direction ("darker, more lo-fi") | Rule-based NL parser (intensifiers, negation, 300+ keywords) | No |
| Profile → synthesis parameters | Conventional mapping code (`KitDNA`, recipe weighting) | No |
| Sample generation | **DSP**: pitched membranes, modal resonators, noise bursts, 808-style square clusters, FM, subtractive, supersaw, Karplus-Strong, additive piano with inharmonicity, formant (source-filter) voice, granular, wavefolding, saturation, bitcrushing, filtering, reverb, delay, chorus, tape wow, resampling, stutter, layering | No |
| Drum machine / sampler | Real-time DSP, lock-free | No |
| Pattern randomiser | Rule/probability templates per groove (17 styles) | No |
| File formats | Conventional code (WAV, ZIP, MIDI, JSON) | No |

## 3. Generation pipeline

```
query ──► analyseQuery()  ─┐            (optional) OnlineAnalyser ─► Claude + web search ─► JSON profile
                           ▼
                     SonicProfile  (14 descriptors, bpm, swing, scale, grooves, signatures)
                           │  + user slider edits
                           │  + applyPrompt("make it darker…")
                           │  + Influence (drums / tonal / texture / processing / groove / variation)
                           ▼
                     deriveDNA()  ──►  KitDNA  (key, scale, tempo, progression, saturation flavour,
                           │                     room, bit-depth, tape wow, noise floor, tuning…)
                           ▼
        39 categories × N variants, run in parallel on worker threads
                           │
          EngineRegistry::engineFor(category) ─► SoundEngine::generate()  (recipe chosen by
                           │                                                weighted profile)
                           ▼
                     finishSample()  (shared kit "glue": tilt, saturation, crush, tape,
                                       transient, noise floor, trim, limit, normalise)
                           ▼
                          Kit  ──► library UI, pads, exports, auto-save
```

**Why kits sound coherent:** every sample in a kit shares one `KitDNA`: key/scale and chord
progression (bass, keys, pads, strings, arps, vocals and toms are all in key and the kick and
808s are tuned to the root), one tempo (fills, arps, risers, chop loops are tempo-synced), one
saturation type, one "room", one degradation chain. Some sound-design recipes *resample* other
engines (granular clouds of the kit's pads, stutters of its own drums, reversed tails of its own
snares), which ties textures to the kit's timbre.

## 4. Adding an engine (e.g. a neural model)

```cpp
class StableAudioEngine : public sdna::SoundEngine {
    std::string id() const override { return "ai.stable-audio-open"; }
    std::string description() const override { return "Local diffusion model"; }
    bool handles(sdna::Category c) const override { return c == sdna::Category::Atmosphere; }
    bool requiresNetwork() const override { return false; } // true for a remote API
    sdna::GenResult generate(sdna::Category c, const sdna::GenContext& ctx) const override {
        // build a text prompt from ctx.profile / ctx.dna, run the model, return audio
    }
};
registry->add(std::make_shared<StableAudioEngine>(), /*preferred*/ true);
```

`generate()` runs on worker threads (never the audio thread) and its result passes through
the same `finishSample()` glue, so AI-generated sounds inherit the kit's processing character.

Candidate engines investigated (none are bundled; check each licence before shipping):

| Option | Type | Notes |
|---|---|---|
| Stable Audio Open (Stability AI) | Local text-to-audio diffusion | Good for textures/one-shots; weights under Stability's community licence; GPU recommended. |
| AudioCraft AudioGen / MusicGen (Meta) | Local text-to-audio | Weights are non-commercial — unsuitable for a commercial product without a separate agreement. |
| ElevenLabs Sound Effects API | Remote text-to-SFX | Commercial API, requires internet/key; good fit as a `requiresNetwork()` engine. |
| RAVE (IRCAM) | Local real-time neural codec | Timbre transfer of generated sounds; needs a trained model per timbre. |
| DDSP (Google Magenta) | Differentiable DSP | Could learn synthesis parameters from audio. |

## 5. Real-time safety (plugin)

* `DrumMachine::process()` performs **no allocation, locking or freeing** (verified by the
  test-suite with an instrumented `operator new`).
* Samples reach the audio thread as raw pointers via `std::atomic`; the message thread owns
  them (`shared_ptr`) and only frees retired buffers once no voice references them.
* All parameters are atomics. UI pad hits travel through a lock-free SPSC ring buffer.
* Generation, regeneration, exports, kit loading and online analysis run on a `ThreadPool` /
  dedicated thread and hand results back with `MessageManager::callAsync`.
* The plugin keeps playing while a new kit is being generated.

## 6. Repository layout

```
core/include/sdna/   public headers (Dsp, Profile, ArtistDatabase, Kit, Engine, KitGenerator,
                     DrumMachine, KitIO, Json, OnlineAnalysis)
core/src/            implementation; core/src/engines/ = the six procedural engines
cli/                 `sonicdna` command-line generator
plugin/              JUCE plugin + standalone (Source/ui = views, Source/services = settings/HTTP)
tests/core_tests.cpp 85 unit/integration checks for the core
tests/host/          loads the built VST3 like a DAW and exercises it (31 checks)
third_party/JUCE     JUCE 8.0.4 (fetched separately, see README)
```
