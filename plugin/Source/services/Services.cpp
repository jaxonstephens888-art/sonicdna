#include "services/Services.h"

namespace services {

Settings& Settings::get() { static Settings s; return s; }

Settings::Settings() {
    juce::PropertiesFile::Options o;
    o.applicationName = "SonicDNA";
    o.filenameSuffix = ".settings";
    o.folderName = "SonicDNA";
    o.osxLibrarySubFolder = "Application Support";
    o.storageFormat = juce::PropertiesFile::storeAsXML;
    props = std::make_unique<juce::PropertiesFile>(o);
}
juce::File Settings::settingsFile() const { return props->getFile(); }
juce::String Settings::apiKey() const {
    if (auto env = juce::SystemStats::getEnvironmentVariable("SONICDNA_ANTHROPIC_API_KEY", {}); env.isNotEmpty()) return env;
    return props->getValue("apiKey");
}
void Settings::setApiKey(const juce::String& k) { props->setValue("apiKey", k.trim()); props->saveIfNeeded(); }
juce::String Settings::model() const { return props->getValue("model", sdna::online::kDefaultModel); }
void Settings::setModel(const juce::String& m) { props->setValue("model", m.trim().isEmpty() ? juce::String(sdna::online::kDefaultModel) : m.trim()); props->saveIfNeeded(); }
juce::File Settings::kitsFolder() const {
    auto def = juce::File::getSpecialLocation(juce::File::userDocumentsDirectory).getChildFile("SONIC DNA").getChildFile("Kits");
    auto f = juce::File(props->getValue("kitsFolder", def.getFullPathName()));
    return f.getFullPathName().isEmpty() ? def : f;
}
void Settings::setKitsFolder(const juce::File& f) { props->setValue("kitsFolder", f.getFullPathName()); props->saveIfNeeded(); }
bool Settings::autoSave() const { return props->getBoolValue("autoSave", true); }
void Settings::setAutoSave(bool b) { props->setValue("autoSave", b); props->saveIfNeeded(); }
int Settings::exportBits() const { return props->getIntValue("exportBits", 24); }
void Settings::setExportBits(int b) { props->setValue("exportBits", b); props->saveIfNeeded(); }
double Settings::sampleRate() const { return props->getDoubleValue("sampleRate", 44100.0); }
void Settings::setSampleRate(double r) { props->setValue("sampleRate", r); props->saveIfNeeded(); }
int Settings::perCategory() const { return juce::jlimit(1, 20, props->getIntValue("perCategory", 8)); }
void Settings::setPerCategory(int n) { props->setValue("perCategory", n); props->saveIfNeeded(); }

juce::File tempExportFolder() {
    auto f = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("SonicDNA_Drag");
    f.createDirectory();
    return f;
}

// ---------------------------------------------------------------------------
OnlineAnalyser::OnlineAnalyser() : juce::Thread("SonicDNA online analysis") {}
OnlineAnalyser::~OnlineAnalyser() { cancel(); }

bool OnlineAnalyser::start(const juce::String& q, Callback done) {
    if (isThreadRunning() || Settings::get().apiKey().isEmpty()) return false;
    query = q; callback = std::move(done);
    return startThread(juce::Thread::Priority::low);
}

void OnlineAnalyser::cancel() { signalThreadShouldExit(); stopThread(8000); }

void OnlineAnalyser::run() {
    auto finish = [this](bool ok, sdna::SonicProfile p, juce::String msg) {
        auto cb = callback;
        juce::MessageManager::callAsync([cb, ok, p, msg] { if (cb) cb(ok, p, msg); });
    };
    const auto key = Settings::get().apiKey().toStdString();
    const auto model = Settings::get().model().toStdString();
    sdna::Json req = sdna::online::buildRequest(query.toStdString(), model);
    juce::String headers;
    for (auto& h : sdna::online::requestHeaders(key)) headers << h.name << ": " << h.value << "\r\n";
    for (int attempt = 0; attempt < 4 && !threadShouldExit(); ++attempt) {
        int status = 0;
        juce::StringPairArray responseHeaders;
        juce::URL url = juce::URL(sdna::online::kEndpoint).withPOSTData(juce::String(req.dump(0)));
        auto opts = juce::URL::InputStreamOptions(juce::URL::ParameterHandling::inPostData)
                        .withExtraHeaders(headers)
                        .withConnectionTimeoutMs(180000)
                        .withStatusCode(&status)
                        .withResponseHeaders(&responseHeaders)
                        .withHttpRequestCmd("POST");
        std::unique_ptr<juce::InputStream> stream = url.createInputStream(opts);
        if (threadShouldExit()) return;
        if (!stream) { finish(false, {}, "Could not connect to api.anthropic.com. Online analysis needs an internet connection."); return; }
        std::string body = stream->readEntireStreamAsString().toStdString();
        auto res = sdna::online::interpretResponse(query.toStdString(), body, status);
        if (res.status == sdna::online::Status::Continue) { req = sdna::online::continueRequest(req, body); continue; }
        if (res.status == sdna::online::Status::Error) { finish(false, {}, juce::String(res.error)); return; }
        juce::String msg = "Researched online";
        if (!res.sources.empty()) msg << " (" << (int)res.sources.size() << " web sources)";
        msg << ".";
        finish(true, res.profile, msg);
        return;
    }
    finish(false, {}, "The analysis took too many steps; try again.");
}

} // namespace services
