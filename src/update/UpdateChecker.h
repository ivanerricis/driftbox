// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <atomic>
#include <condition_variable>
#include <functional>
#include <juce_core/juce_core.h>
#include <memory>
#include <mutex>
#include <optional>

namespace driftbox::update
{
// L'unica richiesta fatta: GET senza query string e con la sola intestazione "User-Agent:
// Driftbox/<versione>". Usa l'elenco (non /releases/latest, che esclude le pre-release).
inline constexpr const char* kReleasesApiUrl = "https://api.github.com/repos/ivanerricis/driftbox/releases";
// I link alle release che arrivano dalla risposta sono accettati (e poi aperti) solo sotto questo prefisso.
inline constexpr const char* kReleaseUrlPrefix = "https://github.com/ivanerricis/driftbox/";
inline constexpr int kTimeoutMs = 5000;
// Attesa massima per l'uscita del thread dopo l'annullamento (distruttore o nuovo controllo). La richiesta in
// corso viene interrotta subito, quindi di norma ritorna ben prima.
inline constexpr int kStopWaitMs = 2000;
inline constexpr juce::int64 kCheckIntervalMs = 24LL * 60LL * 60LL * 1000LL;

// ------------------------------------------------------------------------------ funzioni pure

// Versione semantica: MAJOR.MINOR.PATCH[-pre.release][+build]. I metadati di build sono ignorati.
struct Version
{
    int major = 0, minor = 0, patch = 0;
    juce::StringArray pre; // identificatori di pre-release, vuoto per una release
};

// Accetta una "v"/"V" iniziale facoltativa e gli spazi attorno. Tutto il resto viene rifiutato.
std::optional<Version> parseVersion(const juce::String& text);
// Precedenza semver: <0, 0 o >0. 0.1.0-rc2 < 0.1.0 < 0.1.1.
int compareVersions(const Version& a, const Version& b);

struct ReleaseInfo
{
    juce::String tag; // come pubblicato, ad esempio "v0.2.0"
    juce::String url; // pagina della release
};
// Legge la risposta dell'elenco delle release di GitHub (un array JSON): salta le bozze (draft), include le
// pre-release e sceglie la versione più alta secondo semver. Un elemento senza tag valido o senza un link
// https sotto kReleaseUrlPrefix viene ignorato. Se non resta nulla, o il JSON non è un array, restituisce
// nullopt e riempie `error` (se dato).
std::optional<ReleaseInfo> parseReleases(const juce::String& json, juce::String* error = nullptr);

// Vero se un nuovo controllo è permesso: mai fatto, orologio tornato indietro o passato almeno un giorno.
bool isCheckDue(juce::int64 lastCheckMs, juce::int64 nowMs);

struct FetchOutcome
{
    bool ok = false; // è arrivata una risposta
    int status = 0;  // stato HTTP
    juce::String body;
    juce::String error; // errore di trasporto se !ok
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
    juce::String version; // ultima versione (tag senza "v"), se nota
    juce::String url;     // pagina della release, per updateAvailable
    juce::String reason;  // per failed
};

// Trasforma una risposta HTTP (anche fallita) in un risultato, confrontando la release più alta con
// `currentVersion`.
UpdateResult evaluate(const FetchOutcome& response, const juce::String& currentVersion);

// ----------------------------------------------------------------------------------- controllo

// Permette di interrompere dall'esterno (message thread) la richiesta in corso su un thread in background.
// Thread-safe. La richiesta vera registra qui il proprio WebInputStream con attach()/detach(); cancel()
// chiama WebInputStream::cancel() su quello in uso e fa fallire subito i tentativi successivi.
class CancelToken
{
public:
    CancelToken() = default;

    // Richiede l'annullamento: interrompe lo stream registrato (se c'è) e sveglia chi è in waitForCancel().
    void cancel();
    bool isCancelled() const noexcept { return cancelled_.load(); }
    // Blocca fino all'annullamento o allo scadere di `timeoutMs`. Vero se è stato annullato (per i test).
    bool waitForCancel(int timeoutMs) const;
    // Registra lo stream in uso. Se l'annullamento è già stato richiesto lo annulla subito e restituisce
    // false. Lo stream deve restare vivo fino a detach().
    bool attach(juce::WebInputStream* stream);
    void detach();

private:
    mutable std::mutex mutex_;
    mutable std::condition_variable cv_;
    std::atomic<bool> cancelled_{false};
    juce::WebInputStream* stream_ = nullptr; // protetto da mutex_

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(CancelToken)
};

// Esegue la richiesta. Chiamata da un thread in BACKGROUND. Deve ritornare in fretta dopo token.cancel().
using Fetcher = std::function<FetchOutcome(CancelToken&)>;
// La richiesta HTTP vera (Windows e macOS). Altrove (nessun client HTTP) fallisce senza fare richieste.
FetchOutcome fetchReleases(CancelToken& token);

// Come un risultato arriva al message thread. Il predefinito è juce::MessageManager::callAsync; i test lo
// sostituiscono (una funzione vuota ripristina il predefinito) perché le build del plugin non hanno un ciclo
// di messaggi modale da far girare.
using Poster = std::function<void(std::function<void()>)>;
void setPosterForTests(Poster poster);

// Esegue un controllo alla volta su un thread in background e consegna il risultato sul message thread.
// SOLO MESSAGE THREAD (tranne il fetcher). Il distruttore annulla la richiesta in corso e attende il thread
// (entro kStopWaitMs).
class UpdateChecker final
{
public:
    explicit UpdateChecker(Fetcher fetcher = &fetchReleases);
    ~UpdateChecker();

    void setFetcher(Fetcher f);
    bool isBusy() const noexcept { return busy_.load(); }

    // Avvia un controllo di `currentVersion`. Restituisce false (senza fare nulla) se ne sta già girando un
    // altro. `onResult` gira sul message thread, mai dopo la distruzione di questo oggetto; proteggilo tu
    // (SafePointer) se tocca un oggetto che potrebbe non esserci più.
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
