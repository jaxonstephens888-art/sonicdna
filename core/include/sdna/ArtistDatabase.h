// SONIC DNA — offline artist / genre knowledge base and query analysis.
//
// HONESTY NOTE: the offline database is a hand-curated table of production
// descriptors written from widely-known, public descriptions of each artist's
// sound (genre, characteristic instruments and techniques). It is *not* live
// research and contains no audio. Online analysis (Claude + web search) is an
// optional, separate path implemented in the app layer.
#pragma once

#include "sdna/Profile.h"

#include <string>
#include <vector>

namespace sdna {

struct ArtistEntry {
    const char* name;
    const char* aliases;     // '|' separated alternative spellings
    bool isGenre;
    unsigned char d[kNumDesc]; // 0..100: dark distorted electronic lofi aggressive atmospheric industrial glitch vintage heavy energy melodic experimental hyper
    float bpm;
    float swing;
    const char* scale;
    const char* grooves;     // comma separated groove ids
    const char* genres;      // comma separated
    const char* summary;
    const char* signatures;  // '|' separated characteristic elements
};

const std::vector<ArtistEntry>& artistDatabase();
SonicProfile profileFromEntry(const ArtistEntry& e);

struct AnalysisResult {
    SonicProfile profile;
    std::string method;   // "offline-db", "genre-blend", "heuristic"
    std::string message;  // human readable explanation shown in UI
    std::vector<std::string> matched; // which entries contributed
};

// Analyse a free-text query (artist, producer, genre, or a mix like
// "burial meets nine inch nails"). Never fails: unknown names get an
// explicitly-flagged heuristic profile.
AnalysisResult analyseQuery(const std::string& query);

// Suggestions for autocomplete (prefix / fuzzy).
std::vector<std::string> suggestNames(const std::string& partial, int maxResults = 8);

std::string normaliseName(const std::string& s);

} // namespace sdna
