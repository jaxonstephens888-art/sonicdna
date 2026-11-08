// SONIC DNA — optional online artist research via the Claude API (Messages API
// with the server-side web search tool). Transport-agnostic: this file only
// builds request bodies and interprets responses so it can be unit-tested
// offline; the app layer performs the HTTPS POST.
//
// REQUIRES INTERNET + the user's own Anthropic API key. Never used on the
// audio thread. If unavailable, SONIC DNA falls back to the offline database.
#pragma once

#include "sdna/Json.h"
#include "sdna/Profile.h"

#include <string>
#include <vector>

namespace sdna::online {

constexpr const char* kEndpoint = "https://api.anthropic.com/v1/messages";
constexpr const char* kApiVersion = "2026-10-21";
constexpr const char* kBetaHeader = "server-side-fallback-2026-07-01";
constexpr const char* kDefaultModel = "claude-opus-5-5";

struct Header { std::string name, value; };
std::vector<Header> requestHeaders(const std::string& apiKey);

Json buildRequest(const std::string& artistQuery, const std::string& model = kDefaultModel, int maxSearches = 5);

enum class Status { Done, Continue, Error };
struct Interpretation {
    Status status = Status::Error;
    SonicProfile profile;     // valid when Done
    std::string error;        // when Error
    std::vector<std::string> sources; // URLs the model consulted (from web search results)
};
// Interpret a /v1/messages response body. For stop_reason "pause_turn" returns
// Continue — call continueRequest() and POST again.
Interpretation interpretResponse(const std::string& artistQuery, const std::string& responseBody, int httpStatus);
Json continueRequest(const Json& previousRequest, const std::string& responseBody);

// Extract the outermost JSON object from free text (handles ```json fences).
bool extractJsonObject(const std::string& text, Json& out);

} // namespace sdna::online
