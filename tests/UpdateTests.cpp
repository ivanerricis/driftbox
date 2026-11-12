// SPDX-License-Identifier: AGPL-3.0-or-later
// Controllo degli aggiornamenti. Nessun test tocca la rete: le funzioni pure si provano direttamente e il
// checker e le impostazioni usano richieste finte (iniettate) e un orologio finto.
#include "PluginEditor.h"
#include "PluginInfo.h"
#include "settings/SettingsManager.h"
#include "update/UpdateChecker.h"
#include <atomic>
#include <catch2/catch_test_macros.hpp>
#include <iterator>
#include <mutex>
#include <stdexcept>
#include <vector>
#include <juce_events/juce_events.h>

using namespace driftbox;
using namespace driftbox::update;

namespace
{
FetchOutcome blockedFetch()
{
    FetchOutcome o;
    o.error = "network disabled in tests";
    return o;
}

// Installa una richiesta finta per i SettingsManager creati finché vive; poi ripristina quella bloccante.
struct FakeFetcher
{
    explicit FakeFetcher(Fetcher f) { SettingsManager::setUpdateFetcherOverride(std::move(f)); }
    ~FakeFetcher() { SettingsManager::setUpdateFetcherOverride(&blockedFetch); }
};

// Orologio finto (millisecondi dall'epoca) per le impostazioni.
struct FakeClock
{
    FakeClock()
    {
        SettingsManager::setClockOverride([this] { return now.load(); });
    }
    ~FakeClock() { SettingsManager::setClockOverride({}); }
    std::atomic<juce::int64> now{1'800'000'000'000LL};
};

struct TempFile
{
    juce::File dir =
        juce::File::getSpecialLocation(juce::File::tempDirectory)
            .getChildFile("DriftboxUpdateTest-" + juce::String::toHexString(juce::Random().nextInt64()));
    juce::File file = dir.getChildFile("settings.settings");
    ~TempFile() { dir.deleteRecursively(); }
};

juce::String releaseUrl(const juce::String& tag)
{
    return "https://github.com/ivanerricis/driftbox/releases/tag/" + tag;
}

juce::String releaseJson(const juce::String& tag, bool draft = false, bool pre = false)
{
    return "{\"tag_name\":\"" + tag + "\",\"html_url\":\"" + releaseUrl(tag) +
           "\",\"name\":\"x\",\"draft\":" + (draft ? "true" : "false") +
           ",\"prerelease\":" + (pre ? "true" : "false") + ",\"body\":\"notes\"}";
}

FetchOutcome ok(const juce::String& body)
{
    FetchOutcome o;
    o.ok = true;
    o.status = 200;
    o.body = body;
    return o;
}

// Risposta con una sola release (l'elenco ne ha una).
FetchOutcome release(const juce::String& tag)
{
    return ok("[" + releaseJson(tag) + "]");
}

// I risultati arrivano di norma con MessageManager::callAsync; nei test non gira il message loop, quindi la
// suite raccoglie le chiamate postate e le esegue qui, sul thread del test (il message thread).
struct Mailbox
{
    std::mutex mutex;
    std::vector<std::function<void()>> queue;

    void post(std::function<void()> fn)
    {
        const std::lock_guard<std::mutex> lock(mutex);
        queue.push_back(std::move(fn));
    }
    void drain()
    {
        std::vector<std::function<void()>> work;
        {
            const std::lock_guard<std::mutex> lock(mutex);
            work.swap(queue);
        }
        for (auto& f : work)
        {
            f();
        }
    }
};

Mailbox& mailbox()
{
    static Mailbox m;
    return m;
}

[[maybe_unused]] const bool kMailboxInstalled = []
{
    update::setPosterForTests([](std::function<void()> fn) { mailbox().post(std::move(fn)); });
    return true;
}();

void pump(int ms)
{
    const auto end = juce::Time::getMillisecondCounter() + static_cast<juce::uint32>(ms);
    do
    {
        mailbox().drain();
        juce::Thread::sleep(5);
    } while (juce::Time::getMillisecondCounter() < end);
}

template <class Pred> bool waitFor(Pred pred, int timeoutMs = 5000)
{
    const auto end = juce::Time::getMillisecondCounter() + static_cast<juce::uint32>(timeoutMs);
    while (!pred() && juce::Time::getMillisecondCounter() < end)
    {
        mailbox().drain();
        juce::Thread::sleep(5);
    }
    mailbox().drain();
    return pred();
}

int cmp(const char* a, const char* b)
{
    const auto va = parseVersion(a);
    const auto vb = parseVersion(b);
    REQUIRE(va.has_value());
    REQUIRE(vb.has_value());
    return compareVersions(*va, *vb);
}
} // namespace

TEST_CASE("Version parsing", "[update]")
{
    SECTION("valid")
    {
        auto v = parseVersion("v0.2.0");
        REQUIRE(v);
        CHECK(v->major == 0);
        CHECK(v->minor == 2);
        CHECK(v->patch == 0);
        CHECK(v->pre.isEmpty());

        v = parseVersion("0.1.0-rc2");
        REQUIRE(v);
        CHECK(v->pre == juce::StringArray{"rc2"});

        v = parseVersion("  V10.20.30-alpha.1.x-y+build.5 ");
        REQUIRE(v);
        CHECK(v->major == 10);
        CHECK(v->minor == 20);
        CHECK(v->patch == 30);
        CHECK(v->pre == juce::StringArray{"alpha", "1", "x-y"});

        CHECK(parseVersion(info::kVersion).has_value()); // la versione in esecuzione deve essere capita
    }
    SECTION("garbage is rejected")
    {
        for (const char* bad : {"",
                                " ",
                                "v",
                                "latest",
                                "1",
                                "1.2",
                                "1.2.3.4",
                                "1..3",
                                ".1.2",
                                "1.2.",
                                "1.2.x",
                                "1.2.3-",
                                "1.2.3-.",
                                "1.2.3-a..b",
                                "1.2.3-a b",
                                "-1.2.3",
                                "1.-2.3",
                                "01.2.3",
                                "1.02.3",
                                "1.2.03",
                                "1.2.3-01",
                                "1.2.3+",
                                "1.2.3+a b",
                                "vv1.2.3",
                                "1.2.3 4",
                                "1.2.3-\xc3\xa9",
                                "99999999999.0.0",
                                "<html>",
                                "1,2,3",
                                "nightly-2026-01-01"})
        {
            INFO("input: \"" << bad << "\"");
            CHECK_FALSE(parseVersion(bad).has_value());
        }
    }
}

TEST_CASE("Version comparison follows semantic versioning", "[update]")
{
    CHECK(cmp("0.1.0-rc2", "0.1.0") < 0);
    CHECK(cmp("0.1.0", "0.1.1") < 0);
    CHECK(cmp("0.1.0-rc2", "0.1.1") < 0);
    CHECK(cmp("0.1.0", "0.1.0") == 0);
    CHECK(cmp("v0.1.0", "0.1.0") == 0);
    CHECK(cmp("0.1.0+a", "0.1.0+b") == 0); // i metadati di build si ignorano
    CHECK(cmp("0.9.0", "0.10.0") < 0);     // numerico, non testuale
    CHECK(cmp("1.0.0", "0.99.99") > 0);
    CHECK(cmp("2.0.0", "10.0.0") < 0);

    // Le release candidate si confrontano per numero: rc1 < rc2 < rc10 < release.
    CHECK(cmp("0.1.0-rc1", "0.1.0-rc2") < 0);
    CHECK(cmp("0.1.0-rc2", "0.1.0-rc10") < 0);
    CHECK(cmp("0.1.0-rc10", "0.1.0") < 0);
    CHECK(cmp("0.1.0-rc2", "0.1.0-rc2") == 0);
    CHECK(cmp("0.1.0-beta1", "0.1.0-rc1") < 0);

    // La catena di esempio della specifica di semver.
    const char* chain[] = {"1.0.0-alpha",  "1.0.0-alpha.1", "1.0.0-alpha.beta", "1.0.0-beta",
                           "1.0.0-beta.2", "1.0.0-beta.11", "1.0.0-rc.1",       "1.0.0"};
    for (size_t i = 0; i + 1 < std::size(chain); ++i)
    {
        INFO(chain[i] << " < " << chain[i + 1]);
        CHECK(cmp(chain[i], chain[i + 1]) < 0);
        CHECK(cmp(chain[i + 1], chain[i]) > 0);
    }
}

TEST_CASE("Release list parsing", "[update]")
{
    juce::String why;
    SECTION("drafts are skipped, pre-releases included, the highest version wins")
    {
        // In ordine di pubblicazione come li dà GitHub (il più recente per primo), ma la versione più alta
        // non è la prima e c'è una bozza con una versione ancora più alta.
        const auto json = "[" + releaseJson("v0.3.0", true) + "," + releaseJson("v0.2.0-rc2", false, true) +
                          "," + releaseJson("v0.2.0-rc10", false, true) + "," +
                          releaseJson("v0.2.0-rc1", false, true) + "," + releaseJson("v0.1.0") + "," +
                          releaseJson("nightly") + "]";
        const auto r = parseReleases(json, &why);
        REQUIRE(r);
        CHECK(r->tag == "v0.2.0-rc10");
        CHECK(r->url == releaseUrl("v0.2.0-rc10"));
    }
    SECTION("a final release beats its own release candidates")
    {
        const auto r =
            parseReleases("[" + releaseJson("v0.1.0-rc2", false, true) + "," + releaseJson("v0.1.0") + "," +
                              releaseJson("v0.1.0-rc1", false, true) + "]",
                          &why);
        REQUIRE(r);
        CHECK(r->tag == "v0.1.0");
    }
    SECTION("only a pre-release is enough")
    {
        const auto r = parseReleases("[" + releaseJson("v0.1.0-rc1", false, true) + "]", &why);
        REQUIRE(r);
        CHECK(r->tag == "v0.1.0-rc1");
    }
    SECTION("items that are not usable are ignored")
    {
        const juce::String good = releaseJson("v0.1.0");
        const auto r = parseReleases(
            "[1,\"x\",null,{}," + juce::String("{\"tag_name\":\"v9.0.0\"},") +
                "{\"tag_name\":\"v9.0.0\",\"html_url\":\"https://evil.example/x\"}," +
                "{\"tag_name\":\"v9.0.0\",\"html_url\":\"http://github.com/ivanerricis/driftbox/\"}," +
                "{\"tag_name\":\"v9.0.0\",\"html_url\":\"https://github.com.evil.example/ivanerricis/"
                "driftbox/\"}," +
                good + "]",
            &why);
        REQUIRE(r);
        CHECK(r->tag == "v0.1.0");
    }
    SECTION("nothing usable or malformed JSON is an error, never a crash")
    {
        const std::vector<juce::String> cases{juce::String(),
                                              "not json",
                                              "{",
                                              "[",
                                              "[{\"tag_name\":",
                                              "{}",
                                              "\"v1.0.0\"",
                                              "{\"tag_name\":\"v1.0.0\"}", // un oggetto, non un elenco
                                              "[]",
                                              "[" + releaseJson("v1.0.0", true) + "]", // solo bozze
                                              "[" + releaseJson("garbage") + "]",
                                              "[{\"tag_name\":1,\"html_url\":2}]",
                                              "<html>captive portal</html>"};
        for (const auto& bad : cases)
        {
            INFO("input: " << bad);
            why.clear();
            CHECK_FALSE(parseReleases(bad, &why).has_value());
            CHECK(why.isNotEmpty());
        }
    }
}

TEST_CASE("Check schedule", "[update]")
{
    const juce::int64 now = 1'800'000'000'000LL;
    CHECK(isCheckDue(0, now));
    CHECK(isCheckDue(-5, now));
    CHECK_FALSE(isCheckDue(now, now));
    CHECK_FALSE(isCheckDue(now - kCheckIntervalMs + 1, now));
    CHECK(isCheckDue(now - kCheckIntervalMs, now));
    CHECK(isCheckDue(now - 3 * kCheckIntervalMs, now));
    CHECK(isCheckDue(now + 1000, now)); // l'orologio è andato indietro
}

TEST_CASE("Responses are evaluated against the running version", "[update]")
{
    using S = UpdateResult::Status;
    SECTION("newer release")
    {
        const auto r = evaluate(release("v0.2.0"), "0.1.0");
        CHECK(r.status == S::updateAvailable);
        CHECK(r.version == "0.2.0");
        CHECK(r.url == releaseUrl("v0.2.0"));
    }
    SECTION("a newer pre-release counts too")
    {
        const auto r = evaluate(release("v0.2.0-rc1"), "0.1.0");
        CHECK(r.status == S::updateAvailable);
        CHECK(r.version == "0.2.0-rc1");
    }
    SECTION("same or older release")
    {
        CHECK(evaluate(release("v0.1.0"), "0.1.0").status == S::upToDate);
        const auto r = evaluate(release("v0.1.0"), "0.2.0"); // una build di sviluppo
        CHECK(r.status == S::upToDate);
        CHECK(r.url.isEmpty());
        CHECK(evaluate(release("v0.1.0-rc2"), "0.1.0").status == S::upToDate);
    }
    SECTION("a release is newer than the running release candidate")
    {
        CHECK(evaluate(release("v0.1.0"), "0.1.0-rc2").status == S::updateAvailable);
    }
    SECTION("failures")
    {
        FetchOutcome down;
        down.error = "Timed out";
        auto r = evaluate(down, "0.1.0");
        CHECK(r.status == S::failed);
        CHECK(r.reason == "Timed out");

        FetchOutcome o;
        o.ok = true;
        o.status = 404;
        CHECK(evaluate(o, "0.1.0").status == S::failed);
        o.status = 403;
        CHECK(evaluate(o, "0.1.0").reason.containsIgnoreCase("limiting"));
        o.status = 500;
        CHECK(evaluate(o, "0.1.0").reason.contains("500"));
        o.status = 200;
        o.body = "<html>captive portal</html>";
        CHECK(evaluate(o, "0.1.0").status == S::failed);
        o.body = "[]";
        CHECK(evaluate(o, "0.1.0").status == S::failed);
        CHECK(evaluate(release("v9.9.9"), "garbage").status == S::failed);
        CHECK(evaluate(FetchOutcome{}, "0.1.0").reason.isNotEmpty());
    }
}

TEST_CASE("Checker runs the request in the background and delivers the result to the caller's thread",
          "[update]")
{
    std::atomic<int> fetches{0};
    std::atomic<juce::Thread::ThreadID> fetchThread{nullptr};
    UpdateChecker checker(
        [&]
        {
            ++fetches;
            fetchThread = juce::Thread::getCurrentThreadId();
            juce::Thread::sleep(50);
            return release("v3.0.0");
        });

    bool delivered = false;
    juce::Thread::ThreadID callbackThread = nullptr;
    UpdateResult result;
    REQUIRE(checker.check("0.1.0",
                          [&](const UpdateResult& r)
                          {
                              delivered = true;
                              callbackThread = juce::Thread::getCurrentThreadId();
                              result = r;
                          }));
    CHECK(checker.isBusy());
    CHECK_FALSE(checker.check("0.1.0", [](const UpdateResult&) { FAIL("second check must not run"); }));
    REQUIRE(waitFor([&] { return delivered; }));
    CHECK(callbackThread == juce::Thread::getCurrentThreadId());     // consegnato dove si svuota la mailbox
    CHECK(fetchThread.load() != juce::Thread::getCurrentThreadId()); // la richiesta non gira mai qui
    CHECK(result.status == UpdateResult::Status::updateAvailable);
    CHECK(result.version == "3.0.0");
    CHECK_FALSE(checker.isBusy());
    CHECK(fetches == 1);

    // Dopo se ne può fare un altro.
    delivered = false;
    REQUIRE(checker.check("3.0.0",
                          [&](const UpdateResult& r)
                          {
                              delivered = true;
                              result = r;
                          }));
    REQUIRE(waitFor([&] { return delivered; }));
    CHECK(result.status == UpdateResult::Status::upToDate);
    CHECK(fetches == 2);
}

TEST_CASE("Checker reports a failing or throwing fetcher", "[update]")
{
    UpdateResult result;
    bool delivered = false;
    SECTION("failure")
    {
        UpdateChecker checker([] { return blockedFetch(); });
        REQUIRE(checker.check("0.1.0",
                              [&](const UpdateResult& r)
                              {
                                  delivered = true;
                                  result = r;
                              }));
        REQUIRE(waitFor([&] { return delivered; }));
        CHECK(result.status == UpdateResult::Status::failed);
        CHECK(result.reason == "network disabled in tests");
    }
    SECTION("exception")
    {
        UpdateChecker checker([]() -> FetchOutcome { throw std::runtime_error("boom"); });
        REQUIRE(checker.check("0.1.0",
                              [&](const UpdateResult& r)
                              {
                                  delivered = true;
                                  result = r;
                              }));
        REQUIRE(waitFor([&] { return delivered; }));
        CHECK(result.status == UpdateResult::Status::failed);
    }
}

TEST_CASE("Destroying the checker mid-request never calls back", "[update]")
{
    std::atomic<bool> started{false};
    bool called = false;
    {
        UpdateChecker checker(
            [&]
            {
                started = true;
                juce::Thread::sleep(150);
                return release("v3.0.0");
            });
        REQUIRE(checker.check("0.1.0", [&](const UpdateResult&) { called = true; }));
        REQUIRE(waitFor([&] { return started.load(); }));
    } // ferma il thread
    pump(100); // svuota quanto già postato
    CHECK_FALSE(called);
}

TEST_CASE("Automatic check is on by default and at most once a day", "[update][settings]")
{
    std::atomic<int> fetches{0};
    std::atomic<const char*> tag{"v9.0.0"};
    FakeFetcher fake(
        [&]
        {
            ++fetches;
            return release(tag.load());
        });
    FakeClock clock;
    TempFile tmp;
    SettingsManager m(tmp.file);
    using Phase = SettingsManager::UpdateState::Phase;
    auto done = [&] { return m.getUpdateState().phase == Phase::done; };

    CHECK(m.get().checkUpdates);
    CHECK(m.getLastUpdateCheckMs() == 0);
    CHECK(m.getUpdateState().phase == Phase::idle);

    SECTION("switched off: no request, no timestamp")
    {
        auto s = m.get();
        s.checkUpdates = false;
        m.set(s);
        m.checkForUpdatesIfDue();
        pump(100);
        CHECK(fetches == 0);
        CHECK(m.getUpdateState().phase == Phase::idle);
        CHECK(m.getLastUpdateCheckMs() == 0);
    }
    SECTION("on: one check, stored, then not again within a day")
    {
        m.checkForUpdatesIfDue();
        REQUIRE(waitFor(done));
        CHECK(fetches == 1);
        CHECK(m.getUpdateState().result.status == UpdateResult::Status::updateAvailable);
        CHECK(m.getUpdateState().result.version == "9.0.0");
        CHECK(m.getLastUpdateCheckMs() == clock.now.load());

        // persistito, e un'altra istanza (stesso file) non ricontrolla
        SettingsManager again(tmp.file);
        CHECK(again.getLastUpdateCheckMs() == m.getLastUpdateCheckMs());
        CHECK(again.get().checkUpdates);
        again.checkForUpdatesIfDue();
        pump(100);
        CHECK(fetches == 1);

        clock.now += kCheckIntervalMs - 1;
        m.checkForUpdatesIfDue();
        pump(100);
        CHECK(fetches == 1);

        // un giorno dopo è di nuovo dovuto
        clock.now += 1;
        tag = "v0.0.1";
        m.checkForUpdatesIfDue();
        REQUIRE(
            waitFor([&] { return fetches == 2 && done() && m.getUpdateState().result.version == "0.0.1"; }));
        CHECK(m.getUpdateState().result.status == UpdateResult::Status::upToDate);
        CHECK(m.getLastUpdateCheckMs() == clock.now.load());
    }
    SECTION("a check done by another process is respected")
    {
        {
            SettingsManager other(tmp.file); // scrive l'orario nel file condiviso
            other.checkForUpdatesIfDue();
            REQUIRE(waitFor([&] { return other.getUpdateState().phase == Phase::done; }));
        }
        CHECK(fetches == 1);
        m.checkForUpdatesIfDue(); // `m` era stato creato prima: rilegge l'orario dal file
        pump(100);
        CHECK(fetches == 1);
    }
    SECTION("Check now ignores the switch and the schedule")
    {
        auto s = m.get();
        s.checkUpdates = false;
        m.set(s);
        CHECK(m.checkForUpdatesNow());
        CHECK(m.getUpdateState().phase == Phase::checking);
        REQUIRE(waitFor(done));
        CHECK(fetches == 1);
        CHECK(m.getLastUpdateCheckMs() == 0); // un controllo manuale non tocca la pianificazione

        std::atomic<int> notified{0};
        struct L : juce::ChangeListener
        {
            std::atomic<int>* n = nullptr;
            void changeListenerCallback(juce::ChangeBroadcaster*) override { ++*n; }
        } listener;
        listener.n = &notified;
        m.updateChanges().addChangeListener(&listener);
        CHECK(m.checkForUpdatesNow());
        CHECK_FALSE(m.checkForUpdatesNow()); // ne sta già girando uno
        REQUIRE(waitFor([&] { return fetches == 2 && done(); }));
        CHECK(notified >= 1);
        m.updateChanges().removeChangeListener(&listener);
    }
    SECTION("a failed check is reported")
    {
        FakeFetcher failing([] { return blockedFetch(); });
        SettingsManager other(tmp.file);
        CHECK(other.checkForUpdatesNow());
        REQUIRE(waitFor([&] { return other.getUpdateState().phase == Phase::done; }));
        CHECK(other.getUpdateState().result.status == UpdateResult::Status::failed);
    }
}

TEST_CASE("The default settings never reach the network in tests", "[update][settings]")
{
    // La suite installa una richiesta che fallisce: anche un controllo esplicito non si connette.
    TempFile tmp;
    SettingsManager m(tmp.file);
    CHECK(m.checkForUpdatesNow());
    REQUIRE(waitFor([&] { return m.getUpdateState().phase == SettingsManager::UpdateState::Phase::done; }));
    CHECK(m.getUpdateState().result.status == UpdateResult::Status::failed);
    CHECK(m.getUpdateState().result.reason == "network disabled in tests");
}

namespace
{
// Pannello delle impostazioni con una richiesta finta; riporta le impostazioni condivise ai valori
// predefiniti.
struct PanelFixture
{
    PanelFixture() : fake([] { return release("v0.1.1"); }), processor(), editor(processor)
    {
        processor.getSettings().resetToDefaults();
        waitFor(
            [this]
            {
                return processor.getSettings().getUpdateState().phase !=
                       SettingsManager::UpdateState::Phase::checking;
            });
        editor.openSettings();
    }
    ~PanelFixture()
    {
        editor.closeModal();
        processor.getSettings().resetToDefaults();
    }
    ui::SettingsPanel& panel() { return *editor.getSettingsPanel(); }

    FakeFetcher fake;
    DriftboxAudioProcessor processor;
    DriftboxAudioProcessorEditor editor;
};
} // namespace

TEST_CASE("Settings panel: the Updates row follows the check and the switch", "[gui][update][settings]")
{
    PanelFixture f;
    REQUIRE(f.editor.isSettingsOpen());
    auto& sec = f.panel().updatesSection();
    CHECK(sec.isVisible());
    CHECK(sec.getBounds().getHeight() == ui::SettingsPanel::kRowH);
    CHECK(sec.toggle().getToggleState()); // acceso di default

    SECTION("switch off and on again, Reset restores it")
    {
        sec.toggle().setToggleState(false, juce::dontSendNotification);
        sec.toggle().onClick();
        CHECK_FALSE(f.processor.getSettings().get().checkUpdates);

        f.panel().resetButton().onClick();
        CHECK(f.processor.getSettings().get().checkUpdates);
        CHECK(sec.toggle().getToggleState());
    }
    SECTION("Check now shows the progress, then the new version with a Download link")
    {
        REQUIRE(sec.checkNowButton().isEnabled());
        sec.checkNowButton().onClick();
        CHECK(sec.statusText().startsWith("Checking"));
        REQUIRE(waitFor([&] { return sec.statusText().startsWith("Version"); }));
        CHECK(sec.statusText() == "Version 0.1.1 available");
        CHECK(sec.downloadLink().isVisible());
        CHECK(sec.downloadLink().url() == releaseUrl("v0.1.1"));
    }
}

TEST_CASE("Snapshot: settings modal with an update available", "[gui][update][snapshot]")
{
    PanelFixture f;
    REQUIRE(f.editor.isSettingsOpen());
    f.panel().updatesSection().checkNowButton().onClick();
    auto& sec = f.panel().updatesSection();
    REQUIRE(waitFor([&] { return sec.statusText().startsWith("Version"); }));
    f.panel().updateLayout();

    auto img = f.editor.createComponentSnapshot(f.editor.getLocalBounds(), false, 1.0f);
    const auto dir = juce::SystemStats::getEnvironmentVariable("DRIFTBOX_SNAPSHOT_DIR", {});
    if (dir.isNotEmpty())
    {
        juce::File out = juce::File(dir).getChildFile("settings-updates.png");
        out.getParentDirectory().createDirectory();
        out.deleteFile();
        juce::FileOutputStream stream(out);
        if (stream.openedOk())
        {
            juce::PNGImageFormat().writeImageToStream(img, stream);
        }
    }
    CHECK(img.getWidth() == f.editor.getWidth());
}
