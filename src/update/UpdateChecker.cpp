// SPDX-License-Identifier: AGPL-3.0-or-later
#include "update/UpdateChecker.h"

#include "PluginInfo.h"
#include <juce_events/juce_events.h>
#include <mutex>

namespace driftbox::update
{
namespace
{
constexpr int kMaxBodyBytes = 2 * 1024 * 1024; // l'elenco di 30 release con le note può superare 512 KB
constexpr const char* kAlnumDash = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz-";

bool isAllDigits(const juce::String& s)
{
    return s.isNotEmpty() && s.containsOnly("0123456789");
}

// Plain decimal number without leading zeros, at most 9 digits.
bool parseNumber(const juce::String& s, int& out)
{
    if (!isAllDigits(s) || s.length() > 9 || (s.length() > 1 && s[0] == '0'))
    {
        return false;
    }
    out = s.getIntValue();
    return true;
}

int compareIdentifiers(const juce::String& a, const juce::String& b)
{
    const bool na = isAllDigits(a), nb = isAllDigits(b);
    if (na && nb)
    {
        const auto x = a.getLargeIntValue(), y = b.getLargeIntValue();
        return x == y ? 0 : (x < y ? -1 : 1);
    }
    if (na != nb)
    {
        return na ? -1 : 1; // numeric identifiers have lower precedence
    }
    // Estensione voluta rispetto a semver: "rc2" precede "rc10" (stesso prefisso alfabetico e numero finale
    // confrontato come numero), così le release candidate si ordinano come ci si aspetta.
    auto split = [](const juce::String& s, juce::String& prefix, juce::int64& number)
    {
        int i = s.length();
        while (i > 0 && s[i - 1] >= '0' && s[i - 1] <= '9')
        {
            --i;
        }
        prefix = s.substring(0, i);
        const auto digits = s.substring(i);
        number = digits.isEmpty() || digits.length() > 9 ? -1 : digits.getLargeIntValue();
        return number >= 0 && prefix.isNotEmpty();
    };
    juce::String pa, pb;
    juce::int64 xa = 0, xb = 0;
    if (split(a, pa, xa) && split(b, pb, xb) && pa == pb && xa != xb)
    {
        return xa < xb ? -1 : 1;
    }
    const int c = a.compare(b);
    return c == 0 ? 0 : (c < 0 ? -1 : 1);
}
} // namespace

std::optional<Version> parseVersion(const juce::String& text)
{
    auto t = text.trim();
    if (t.startsWithChar('v') || t.startsWithChar('V'))
    {
        t = t.substring(1);
    }
    if (t.isEmpty() || t.length() > 64)
    {
        return std::nullopt;
    }

    const auto plus = t.indexOfChar('+');
    if (plus >= 0)
    {
        const auto build = t.substring(plus + 1);
        if (build.isEmpty() || !(juce::String(kAlnumDash) + ".").containsChar(build[0]) ||
            !build.containsOnly(juce::String(kAlnumDash) + "."))
        {
            return std::nullopt;
        }
        t = t.substring(0, plus);
    }

    juce::String core = t, pre;
    bool hasPre = false;
    const auto dash = t.indexOfChar('-');
    if (dash >= 0)
    {
        core = t.substring(0, dash);
        pre = t.substring(dash + 1);
        hasPre = true;
    }

    if (core.startsWithChar('.') || core.endsWithChar('.'))
    {
        return std::nullopt;
    }
    const auto parts = juce::StringArray::fromTokens(core, ".", "");
    if (parts.size() != 3)
    {
        return std::nullopt;
    }
    Version v;
    if (!parseNumber(parts[0], v.major) || !parseNumber(parts[1], v.minor) || !parseNumber(parts[2], v.patch))
    {
        return std::nullopt;
    }

    if (hasPre)
    {
        if (pre.isEmpty() || pre.startsWithChar('.') || pre.endsWithChar('.') || pre.contains(".."))
        {
            return std::nullopt;
        }
        v.pre = juce::StringArray::fromTokens(pre, ".", "");
        for (const auto& id : v.pre)
        {
            if (id.isEmpty() || !id.containsOnly(kAlnumDash))
            {
                return std::nullopt;
            }
            if (isAllDigits(id) && id.length() > 1 && id[0] == '0')
            {
                return std::nullopt; // numeric identifiers have no leading zeros
            }
        }
    }
    return v;
}

int compareVersions(const Version& a, const Version& b)
{
    if (a.major != b.major)
    {
        return a.major < b.major ? -1 : 1;
    }
    if (a.minor != b.minor)
    {
        return a.minor < b.minor ? -1 : 1;
    }
    if (a.patch != b.patch)
    {
        return a.patch < b.patch ? -1 : 1;
    }
    if (a.pre.isEmpty() != b.pre.isEmpty())
    {
        return a.pre.isEmpty() ? 1 : -1; // a release is higher than any of its pre-releases
    }
    const int n = juce::jmin(a.pre.size(), b.pre.size());
    for (int i = 0; i < n; ++i)
    {
        if (const int c = compareIdentifiers(a.pre[i], b.pre[i]); c != 0)
        {
            return c;
        }
    }
    if (a.pre.size() == b.pre.size())
    {
        return 0;
    }
    return a.pre.size() < b.pre.size() ? -1 : 1;
}

std::optional<ReleaseInfo> parseReleases(const juce::String& json, juce::String* error)
{
    auto fail = [error](const char* why) -> std::optional<ReleaseInfo>
    {
        if (error != nullptr)
        {
            *error = why;
        }
        return std::nullopt;
    };
    juce::var root;
    if (json.isEmpty() || !juce::JSON::parse(json, root).wasOk() || !root.isArray())
    {
        return fail("Unexpected response from GitHub");
    }

    std::optional<ReleaseInfo> best;
    std::optional<Version> bestVersion;
    for (const auto& item : *root.getArray())
    {
        if (!item.isObject() || static_cast<bool>(item.getProperty("draft", false)))
        {
            continue;
        }
        const auto tagVar = item.getProperty("tag_name", {});
        const auto urlVar = item.getProperty("html_url", {});
        if (!tagVar.isString() || !urlVar.isString())
        {
            continue;
        }
        ReleaseInfo r{tagVar.toString().trim(), urlVar.toString().trim()};
        auto v = parseVersion(r.tag);
        if (!v || !r.url.startsWith(kReleaseUrlPrefix))
        {
            continue;
        }
        if (!bestVersion || compareVersions(*v, *bestVersion) > 0)
        {
            bestVersion = std::move(v);
            best = std::move(r);
        }
    }
    if (!best)
    {
        return fail("No release has been published yet");
    }
    return best;
}

bool isCheckDue(juce::int64 lastCheckMs, juce::int64 nowMs)
{
    return lastCheckMs <= 0 || nowMs < lastCheckMs || nowMs - lastCheckMs >= kCheckIntervalMs;
}

UpdateResult evaluate(const FetchOutcome& response, const juce::String& currentVersion)
{
    UpdateResult out;
    auto fail = [&out](const juce::String& why)
    {
        out.status = UpdateResult::Status::failed;
        out.reason = why;
        return out;
    };
    if (!response.ok)
    {
        return fail(response.error.isNotEmpty() ? response.error : juce::String("Could not reach GitHub"));
    }
    if (response.status == 403 || response.status == 429)
    {
        return fail("GitHub is limiting requests, try again later");
    }
    if (response.status < 200 || response.status >= 300)
    {
        return fail("GitHub answered with HTTP " + juce::String(response.status));
    }

    juce::String why;
    const auto release = parseReleases(response.body, &why);
    if (!release)
    {
        return fail(why);
    }
    const auto latest = parseVersion(release->tag);
    const auto current = parseVersion(currentVersion);
    if (!latest || !current)
    {
        return fail("Unrecognised version");
    }

    out.version = release->tag.trim().trimCharactersAtStart("vV");
    if (compareVersions(*latest, *current) > 0)
    {
        out.status = UpdateResult::Status::updateAvailable;
        out.url = release->url;
    }
    else
    {
        out.status = UpdateResult::Status::upToDate;
    }
    return out;
}

FetchOutcome fetchReleases()
{
    FetchOutcome out;
#if JUCE_WINDOWS || JUCE_MAC
    int status = 0;
    // Nessun parametro, nessun dato POST: l'unica intestazione è lo User-Agent che GitHub richiede
    // ("Driftbox/<versione>": nessun nome utente, e-mail o identificativo della macchina).
    const auto userAgent = "User-Agent: Driftbox/" + juce::String(info::kVersion) + "\r\n";
    auto stream =
        juce::URL(kReleasesApiUrl)
            .createInputStream(juce::URL::InputStreamOptions(juce::URL::ParameterHandling::inAddress)
                                   .withExtraHeaders(userAgent)
                                   .withConnectionTimeoutMs(kTimeoutMs)
                                   .withNumRedirectsToFollow(2)
                                   .withStatusCode(&status));
    if (stream == nullptr)
    {
        out.ok = status > 0; // an HTTP error status (JUCE gives no stream for 4xx/5xx)
        out.status = status;
        out.error = "Could not reach GitHub";
        return out;
    }
    juce::MemoryBlock data;
    char buffer[4096];
    while (data.getSize() < static_cast<size_t>(kMaxBodyBytes))
    {
        if (juce::Thread::currentThreadShouldExit())
        {
            out.error = "Cancelled";
            return out;
        }
        const int n = stream->read(buffer, static_cast<int>(sizeof(buffer)));
        if (n <= 0)
        {
            break;
        }
        data.append(buffer, static_cast<size_t>(n));
    }
    out.ok = true;
    out.status = status;
    out.body = juce::String::createStringFromData(data.getData(), static_cast<int>(data.getSize()));
#else
    out.error = "Update checks are not available on this platform";
#endif
    return out;
}

// ---------------------------------------------------------------------------------------- checker

namespace
{
std::mutex& posterMutex()
{
    static std::mutex m;
    return m;
}

Poster& posterOverride()
{
    static Poster p;
    return p;
}

void postToMessageThread(std::function<void()> fn)
{
    Poster p;
    {
        const std::lock_guard<std::mutex> lock(posterMutex());
        p = posterOverride();
    }
    if (p)
    {
        p(std::move(fn));
    }
    else
    {
        juce::MessageManager::callAsync(std::move(fn));
    }
}
} // namespace

void setPosterForTests(Poster poster)
{
    const std::lock_guard<std::mutex> lock(posterMutex());
    posterOverride() = std::move(poster);
}

class UpdateChecker::Worker final : public juce::Thread
{
public:
    Worker(Fetcher f, juce::String version, juce::WeakReference<UpdateChecker> owner,
           std::function<void(const UpdateResult&)> cb)
        : juce::Thread("Driftbox update check"), fetch_(std::move(f)), version_(std::move(version)),
          owner_(std::move(owner)), callback_(std::move(cb))
    {
    }

    void run() override
    {
        FetchOutcome response;
        try
        {
            if (fetch_)
            {
                response = fetch_();
            }
            else
            {
                response.error = "No update source";
            }
        }
        catch (...)
        {
            response = {};
            response.error = "The update check failed";
        }
        auto result = evaluate(response, version_);
        if (threadShouldExit())
        {
            return;
        }
        postToMessageThread(
            [owner = owner_, result = std::move(result), cb = std::move(callback_)]
            {
                if (owner != nullptr)
                {
                    owner->finish(result, cb);
                }
            });
    }

private:
    Fetcher fetch_;
    juce::String version_;
    juce::WeakReference<UpdateChecker> owner_;
    std::function<void(const UpdateResult&)> callback_;
};

UpdateChecker::UpdateChecker(Fetcher fetcher) : fetcher_(std::move(fetcher)) {}

UpdateChecker::~UpdateChecker()
{
    masterReference.clear(); // pending message-thread callbacks become no-ops
    if (worker_ != nullptr)
    {
        worker_->stopThread(kTimeoutMs + 4000);
    }
    worker_.reset();
}

void UpdateChecker::setFetcher(Fetcher f)
{
    jassert(!busy_.load());
    fetcher_ = std::move(f);
}

bool UpdateChecker::check(const juce::String& currentVersion,
                          std::function<void(const UpdateResult&)> onResult)
{
    if (busy_.exchange(true))
    {
        return false;
    }
    if (worker_ != nullptr)
    {
        worker_->stopThread(kTimeoutMs + 4000); // already finished: only waits for the thread to exit
    }
    worker_.reset();
    worker_ = std::make_unique<Worker>(fetcher_, currentVersion, this, std::move(onResult));
    worker_->startThread();
    return true;
}

void UpdateChecker::finish(const UpdateResult& r, const std::function<void(const UpdateResult&)>& cb)
{
    busy_.store(false);
    if (cb)
    {
        cb(r);
    }
}
} // namespace driftbox::update
