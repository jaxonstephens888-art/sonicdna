#include "sdna/OnlineAnalysis.h"
#include "sdna/KitIO.h"

#include <algorithm>

namespace sdna::online {

std::vector<Header> requestHeaders(const std::string& apiKey) {
    return { { "content-type", "application/json" }, { "x-api-key", apiKey }, { "anthropic-version", kApiVersion }, { "anthropic-beta", kBetaHeader } };
}

static std::string systemPrompt() {
    std::string grooves, scales;
    for (int i = 0; i < (int)Groove::Count; ++i) grooves += std::string(i ? ", " : "") + grooveId((Groove)i);
    for (int i = 0; i < (int)Scale::Count; ++i) scales += std::string(i ? ", " : "") + scaleName((Scale)i);
    std::string descs;
    for (int i = 0; i < kNumDesc; ++i) descs += std::string(i ? ", " : "") + descId((Desc)i) + " (" + descHelp((Desc)i) + ")";
    return
        "You are the analysis engine of SONIC DNA, a music tool that synthesises ORIGINAL drum kits and sounds that are inspired by "
        "the general production characteristics of an artist, producer or genre. Use web search to research how the requested "
        "artist's music is produced: drum sounds, instruments and synthesizers, processing (distortion, saturation, reverb), "
        "rhythmic feel, tempo range, tonality and atmosphere. Prefer interviews, gear write-ups and reputable reviews.\n"
        "Describe production characteristics only. Do not quote lyrics and do not describe how to copy specific recordings or samples.\n"
        "Reply with ONLY one JSON object (no prose, no markdown) with exactly these keys:\n"
        "  name: canonical artist/genre name\n"
        "  summary: one sentence (max 40 words) describing the sound\n"
        "  confidence: 0..1 — how sure you are you identified the right artist and their sound\n"
        "  genres: array of genre strings\n"
        "  signatures: 3-6 short strings naming characteristic drums, instruments or techniques\n"
        "  descriptors: object with numbers 0..1 for: " + descs + "\n"
        "  bpm: typical tempo as a number\n"
        "  swing: 0..1 (0 straight, 0.5 shuffled 2-step/boom-bap, 1 extreme)\n"
        "  key: -1 unless one tonic pitch class (0=C..11=B) clearly dominates\n"
        "  scale: one of: " + scales + "\n"
        "  grooves: 1-3 values, most typical first, from: " + grooves + "\n"
        "If the name is ambiguous or you cannot find the artist, say so in summary and use confidence below 0.3.";
}

Json buildRequest(const std::string& artistQuery, const std::string& model, int maxSearches) {
    Json req = Json::object();
    req["model"] = model;
    req["max_tokens"] = 16000;
    req["fallbacks"] = "default";
    Json oc = Json::object(); oc["effort"] = "medium"; req["output_config"] = oc;
    req["system"] = systemPrompt();
    Json tool = Json::object();
    tool["type"] = "web_search_20260209"; tool["name"] = "web_search"; tool["max_uses"] = maxSearches;
    Json tools = Json::array(); tools.push(tool);
    req["tools"] = tools;
    Json msg = Json::object();
    msg["role"] = "user";
    msg["content"] = "Analyse the production sound of: " + artistQuery;
    Json msgs = Json::array(); msgs.push(msg);
    req["messages"] = msgs;
    return req;
}

bool extractJsonObject(const std::string& text, Json& out) {
    // Scan for the last balanced {...} that parses as an object.
    for (size_t start = text.find('{'); start != std::string::npos; start = text.find('{', start + 1)) {
        int depth = 0; bool inStr = false, esc = false;
        for (size_t i = start; i < text.size(); ++i) {
            char c = text[i];
            if (inStr) { if (esc) esc = false; else if (c == '\\') esc = true; else if (c == '"') inStr = false; continue; }
            if (c == '"') inStr = true;
            else if (c == '{') ++depth;
            else if (c == '}' && --depth == 0) {
                std::string err;
                Json j = Json::parse(text.substr(start, i - start + 1), &err);
                if (err.empty() && j.isObject() && (j.has("descriptors") || j.has("summary"))) { out = j; return true; }
                break;
            }
        }
    }
    return false;
}

Interpretation interpretResponse(const std::string& query, const std::string& body, int httpStatus) {
    Interpretation r;
    std::string perr;
    Json resp = Json::parse(body, &perr);
    if (httpStatus != 200) {
        std::string msg = resp.get("error").get("message").str();
        if (httpStatus == 401) msg = "API key rejected (401). Check the key in Settings.";
        else if (httpStatus == 429) msg = "Rate limited (429). Try again shortly.";
        else if (msg.empty()) msg = "HTTP " + std::to_string(httpStatus);
        r.error = msg;
        return r;
    }
    if (!perr.empty() || !resp.isObject()) { r.error = "Unreadable response from the API: " + perr; return r; }
    std::string stop = resp.get("stop_reason").str();
    for (auto& block : resp.get("content").items()) {
        if (block.get("type").str() == "web_search_tool_result" && block.get("content").isArray())
            for (auto& res : block.get("content").items()) if (!res.get("url").str().empty()) r.sources.push_back(res.get("url").str());
    }
    if (stop == "pause_turn") { r.status = Status::Continue; return r; }
    if (stop == "refusal") { r.error = "The model declined this request."; return r; }
    std::string text;
    for (auto& block : resp.get("content").items()) if (block.get("type").str() == "text") text += block.get("text").str();
    Json pj;
    if (!extractJsonObject(text, pj)) {
        r.error = stop == "max_tokens" ? "Response was cut off before the profile was complete." : "No sound profile found in the response.";
        return r;
    }
    SonicProfile p = profileFromJson(pj);
    p.query = query;
    p.source = "online";
    if (p.displayName.empty()) p.displayName = query;
    p.confidence = std::clamp((float)pj.get("confidence").num(0.6), 0.0f, 1.0f);
    r.profile = p;
    r.status = Status::Done;
    return r;
}

Json continueRequest(const Json& prev, const std::string& body) {
    Json req = prev;
    Json resp = Json::parse(body);
    Json msgs = Json::array();
    for (auto& m : prev.get("messages").items()) msgs.push(m);
    Json a = Json::object(); a["role"] = "assistant"; a["content"] = resp.get("content");
    msgs.push(a);
    req["messages"] = msgs;
    return req;
}

} // namespace sdna::online
