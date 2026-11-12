// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <atomic>
#include <functional>
#include <juce_core/juce_core.h>
#include <memory>
#include <optional>

namespace driftbox::update
{
// L'unica richiesta fatta: GET senza query string e con la sola intestazione "User-Agent:
// Driftbox/<versione>". Usa l'elenco (non /releases/latest, che esclude le pre-release).
inline constexpr const char* kReleasesApiUrl = "https://api.github.com/repos/ivanerricis/driftbox/releases";
// I link alle release che arrivano dalla risposta sono accettati (e poi aperti) solo sotto questo prefisso.
inline constexpr const char* kReleaseUrlPrefix = "https://github.com/ivanerricis/driftbox/";
inline constexpr int kTimeoutMs = 5000;
inline constexpr juce::int64 kCheckIntervalMs = 24LL * 60LL * 60LL * 1000LL;

// ------------------------------------------------------------------------------- pure functions

// Versione semantica: MAJOR.MINOR.PATCH[-pre.release][+build]. I metadati di build sono ignorati.
struct Version
{
    int major = 0, minor = 0, patch = 0;
    juce::StringArray pre; // pre-release identifiers, empty for a release
};

// Accepts an optional leading "v"/"V" and surrounding white space. Anything else is rejected.
std::optional<Version> parseVersion(const juce::String& text);
// Semantic versioning precedence: <0, 0 or >0. 0.1.0-rc2 < 0.1.0 < 0.1.1.
int compareVersions(const Version& a, const Version& b);

struct ReleaseInfo
{
    juce::String tag; // as published, e.g. "v0.2.0"
    juce::String url; // release page
};
// Legge la risposta dell'elenco delle release di GitHub (un array JSON): salta le bozze (draft), include le
// pre-release e sceglie la versione più alta secondo semver. Un elemento senza tag valido o senza un link
// https sotto kReleaseUrlPrefix viene ignorato. Se non resta nulla, o il JSON non è un array, restituisce
// nullopt e riempie `error` (se dato).
std::optional<ReleaseInfo> parseReleases(const juce::String& json, juce::String* error = nullptr);

// True when a new check is allowed: never checked, clock moved backwards, or at least a day has passed.
bool isCheckDue(juce::int64 lastCheckMs, juce::int64 nowMs);

struct FetchOutcome
{
    bool ok = false; // a response was received
    int status = 0;  // HTTP status
    juce::String body;
    juce::String error; // transport error when !ok
};

struct UpdateResult
{
    enum class Status
    {
        upToDate,
        updateAvailable,
        failed
    };
    Status status = Status::failed;
    juce::String version; // latest version (tag without "v"), when known
    juce::String url;     // release page, for updateAvailable
    juce::String reason;  // for failed
};

// Turns a (possibly failed) HTTP response into a result, comparing the highest release with `currentVersion`.
UpdateResult evaluate(const FetchOutcome& response, const juce::String& currentVersion);

// ------------------------------------------------------------------------------------- checker

// Performs the request. Called on a BACKGROUND thread.
using Fetcher = std::function<FetchOutcome()>;
// The real HTTP request (Windows and macOS). Elsewhere (no HTTP backend) it fails without any request.
FetchOutcome fetchReleases();

// How a result reaches the message thread. The default is juce::MessageManager::callAsync; tests replace it
// (an empty function restores the default) because plugin builds have no modal dispatch loop to pump.
using Poster = std::function<void(std::function<void()>)>;
void setPosterForTests(Poster poster);

// Runs one check at a time on a background thread and delivers the result on the message thread.
// MESSAGE THREAD ONLY (except the fetcher). The destructor joins the thread (bounded by the timeout).
class UpdateChecker final
{
public:
    explicit UpdateChecker(Fetcher fetcher = &fetchReleases);
    ~UpdateChecker();

    void setFetcher(Fetcher f);
    bool isBusy() const noexcept { return busy_.load(); }

    // Starts a check of `currentVersion`. Returns false (and does nothing) while another one is running.
    // `onResult` runs on the message thread, never after this object is destroyed; guard it yourself
    // (SafePointer) if it touches an object that may be gone.
    bool check(const juce::String& currentVersion, std::function<void(const UpdateResult&)> onResult);

private:
    class Worker;
    void finish(const UpdateResult&, const std::function<void(const UpdateResult&)>&);

    Fetcher fetcher_;
    std::unique_ptr<Worker> worker_;
    std::atomic<bool> busy_{false};

    JUCE_DECLARE_WEAK_REFERENCEABLE(UpdateChecker)
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(UpdateChecker)
};
} // namespace driftbox::update
