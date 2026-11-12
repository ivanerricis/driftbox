// SPDX-License-Identifier: AGPL-3.0-or-later
#include "PluginProcessor.h"

#include "PluginEditor.h"
#include "PluginInfo.h"

#include <vector>

namespace driftbox
{
namespace
{
const juce::Identifier kStateVersionProp{"stateVersion"};
const juce::Identifier kProfileVersionProp{"profileVersion"};
const juce::Identifier kDriftSeedProp{"driftSeed"};
const juce::Identifier kUiWidthProp{"uiWidth"};
const juce::Identifier kUiHeightProp{"uiHeight"};

// Seed come stringa esadecimale di 16 cifre minuscole (sempre 16, con zeri iniziali).
juce::String seedToHex(std::uint64_t seed)
{
    return juce::String::formatted("%016llx", static_cast<unsigned long long>(seed));
}

// Da stringa esadecimale di 1-16 cifre (maiuscole o minuscole) al seed; false se mancante o malformata.
bool seedFromHex(const juce::var& v, std::uint64_t& out)
{
    if (!v.isString())
    {
        return false;
    }
    const auto s = v.toString();
    if (s.isEmpty() || s.length() > 16 || !s.containsOnly("0123456789abcdefABCDEF"))
    {
        return false;
    }
    out = static_cast<std::uint64_t>(s.getHexValue64());
    return true;
}

// Un cambio di parametro registrato. perform() viene eseguito quando il passo è registrato (il parametro ha
// già il nuovo valore, quindi non fa nulla) e al redo; undo() ripristina il vecchio valore. I valori sono
// normalizzati.
class ParameterChangeAction final : public juce::UndoableAction
{
public:
    ParameterChangeAction(juce::AudioProcessorParameter& p, float oldValue, float newValue)
        : param_(p), old_(oldValue), new_(newValue)
    {
    }
    bool perform() override { return apply(new_); }
    bool undo() override { return apply(old_); }
    int getSizeInUnits() override { return 1; }

private:
    bool apply(float value)
    {
        if (!juce::approximatelyEqual(param_.getValue(), value))
        {
            // Come un gesto, così l'host vede il cambio come qualunque altra modifica fatta dalla UI.
            param_.beginChangeGesture();
            param_.setValueNotifyingHost(value);
            param_.endChangeGesture();
        }
        return true;
    }

    juce::AudioProcessorParameter& param_;
    float old_, new_;
};
} // namespace

// Passo di undo di Randomize. Il seed non è un parametro, quindi serve un'azione dedicata: perform() imposta
// il nuovo seed (alla registrazione e al redo), undo() ripristina il vecchio. Entrambi con crossfade, mai
// con snap: un cambio di seed fatto dall'utente non deve produrre click.
class DriftboxAudioProcessor::SeedChangeAction final : public juce::UndoableAction
{
public:
    SeedChangeAction(DriftboxAudioProcessor& p, std::uint64_t oldSeed, std::uint64_t newSeed)
        : processor_(p), old_(oldSeed), new_(newSeed)
    {
    }
    bool perform() override
    {
        processor_.setSeed(new_);
        return true;
    }
    bool undo() override
    {
        processor_.setSeed(old_);
        return true;
    }
    int getSizeInUnits() override { return 1; }

private:
    DriftboxAudioProcessor& processor_;
    std::uint64_t old_, new_;
};

// Registra i gesti sui parametri della UI del plugin come passi di undo.
//
// Perché non l'UndoManager dell'AudioProcessorValueTreeState? L'APVTS copia i valori dei parametri nel suo
// albero da un timer (ogni 20-500 ms) e registra ogni copia nel proprio UndoManager, qualunque ne sia la
// causa: l'automazione dell'host riempirebbe la cronologia e a quel punto un cambio della UI non si
// distinguerebbe da un'automazione. Invece ogni modifica dalla UI passa già da
// beginChangeGesture()/endChangeGesture() (lo fanno gli attachment di JUCE e i valori digitati), mentre
// l'host non le chiama mai sui nostri listener. Un listener sui parametri vede quindi esattamente le
// modifiche della UI: il valore all'inizio e alla fine di un gesto diventano un solo passo. I gesti annidati
// in un altro si uniscono al passo di quello più esterno.
class DriftboxAudioProcessor::UndoRecorder final : private juce::AudioProcessorParameter::Listener
{
public:
    UndoRecorder(DriftboxAudioProcessor& processor, juce::UndoManager& um) : processor_(processor), um_(um)
    {
        const auto n = static_cast<size_t>(processor_.getParameters().size());
        start_.assign(n, 0.0f);
        open_.assign(n, false);
        for (auto* p : processor_.getParameters())
        {
            p->addListener(this);
        }
    }

    ~UndoRecorder() override
    {
        for (auto* p : processor_.getParameters())
        {
            p->removeListener(this);
        }
    }

    bool isGestureOpen() const noexcept { return depth_ > 0; }

private:
    void parameterValueChanged(int, float) override {}

    void parameterGestureChanged(int index, bool starting) override
    {
        // Solo message thread; undo/redo e il ripristino dello stato non registrano se stessi.
        if (!juce::MessageManager::getInstance()->isThisTheMessageThread() || um_.isPerformingUndoRedo() ||
            index < 0 || static_cast<size_t>(index) >= open_.size())
        {
            return;
        }
        const auto i = static_cast<size_t>(index);
        auto* param = processor_.getParameters()[index];

        if (starting)
        {
            if (open_[i])
            {
                return;
            }
            if (depth_ == 0)
            {
                um_.beginNewTransaction(param->getName(64));
            }
            open_[i] = true;
            start_[i] = param->getValue();
            ++depth_;
        }
        else
        {
            if (!open_[i])
            {
                return;
            }
            open_[i] = false;
            --depth_;
            const float now = param->getValue();
            if (!juce::approximatelyEqual(start_[i], now))
            {
                um_.perform(new ParameterChangeAction(*param, start_[i], now));
            }
        }
    }

    DriftboxAudioProcessor& processor_;
    juce::UndoManager& um_;
    std::vector<float> start_;
    std::vector<bool> open_;
    int depth_ = 0;
};

DriftboxAudioProcessor::DriftboxAudioProcessor()
    : AudioProcessor(BusesProperties()
                         .withInput("Input", juce::AudioChannelSet::stereo(), true)
                         .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      apvts_(*this, nullptr, juce::Identifier(kStateTag), createParameterLayout()), params_(apvts_)
{
    // L'APVTS non riceve un UndoManager di proposito, vedi UndoRecorder.
    undoRecorder_ = std::make_unique<UndoRecorder>(*this, undoManager_);
    // Ogni nuova istanza parte con tolleranze proprie; uno stato salvato le sostituisce.
    seed_.store(static_cast<std::uint64_t>(juce::Random::getSystemRandom().nextInt64()),
                std::memory_order_release);
    startTimerHz(30);
}

DriftboxAudioProcessor::~DriftboxAudioProcessor()
{
    stopTimer();
}

void DriftboxAudioProcessor::setSeed(std::uint64_t seed) noexcept
{
    seed_.store(seed, std::memory_order_release);
}

void DriftboxAudioProcessor::randomize()
{
    jassert(juce::MessageManager::getInstance()->isThisTheMessageThread());
    const auto oldSeed = getSeed();
    std::uint64_t newSeed = oldSeed;
    while (newSeed == oldSeed)
    {
        newSeed = static_cast<std::uint64_t>(juce::Random::getSystemRandom().nextInt64());
    }
    // Un solo passo: perform() imposta subito il nuovo seed.
    undoManager_.beginNewTransaction("Randomize");
    undoManager_.perform(new SeedChangeAction(*this, oldSeed, newSeed));
}

void DriftboxAudioProcessor::clearUndoHistory()
{
    // Alcuni host ripristinano lo stato da un altro thread: l'UndoManager si tocca solo dal message thread
    // (la richiesta viene raccolta dal timer).
    if (juce::MessageManager::getInstance()->isThisTheMessageThread())
    {
        undoManager_.clearUndoHistory();
    }
    else
    {
        pendingUndoClear_.store(true, std::memory_order_release);
    }
}

bool DriftboxAudioProcessor::isUiGestureOpen() const noexcept
{
    return undoRecorder_ != nullptr && undoRecorder_->isGestureOpen();
}

void DriftboxAudioProcessor::timerCallback()
{
    if (pendingUndoClear_.exchange(false, std::memory_order_acq_rel))
    {
        undoManager_.clearUndoHistory();
    }
}

const juce::String DriftboxAudioProcessor::getName() const
{
    return info::kName;
}

void DriftboxAudioProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    currentSampleRate_ = sampleRate;
    engine_.prepare(sampleRate, samplesPerBlock, juce::jmax(1, getTotalNumOutputChannels()));
    engine_.setSettings(params_.snapshot());
    engine_.setSeed(getSeed(), false);
    seedSnapPending_.store(false, std::memory_order_release); // il seed vale già, senza crossfade
}

void DriftboxAudioProcessor::releaseResources() {}

bool DriftboxAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    const auto& out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
    {
        return false;
    }
    return out == layouts.getMainInputChannelSet();
}

void DriftboxAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const auto startTicks = juce::Time::getHighResolutionTicks();
    for (int ch = getTotalNumInputChannels(); ch < getTotalNumOutputChannels(); ++ch)
    {
        buffer.clear(ch, 0, buffer.getNumSamples());
    }

    engine_.setSettings(params_.snapshot());
    // Dopo un ripristino dello stato il seed si applica subito (snap); un Randomize passa dal crossfade.
    const bool snap = seedSnapPending_.exchange(false, std::memory_order_acq_rel);
    engine_.setSeed(getSeed(), !snap);
    engine_.process(buffer);

    // Frazione smussata del tempo del blocco spesa qui (economico, realtime safe).
    if (currentSampleRate_ > 0.0 && buffer.getNumSamples() > 0)
    {
        const double elapsed =
            juce::Time::highResolutionTicksToSeconds(juce::Time::getHighResolutionTicks() - startTicks);
        const double budget = buffer.getNumSamples() / currentSampleRate_;
        auto& load = engine_.meters().cpuLoad;
        const float prev = load.load(std::memory_order_relaxed);
        load.store(prev + 0.1f * (static_cast<float>(elapsed / budget) - prev), std::memory_order_relaxed);
    }
}

juce::AudioProcessorEditor* DriftboxAudioProcessor::createEditor()
{
    return new DriftboxAudioProcessorEditor(*this);
}

juce::AudioParameterBool* DriftboxAudioProcessor::getBypassParameter() const
{
    return dynamic_cast<juce::AudioParameterBool*>(apvts_.getParameter(pid::bypass));
}

void DriftboxAudioProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    auto state = apvts_.copyState();
    state.setProperty(kStateVersionProp, kStateVersion, nullptr);
    state.setProperty(kProfileVersionProp, kProfileVersion, nullptr);
    state.setProperty(kDriftSeedProp, seedToHex(getSeed()), nullptr);
    if (auto xml = state.createXml())
    {
        copyXmlToBinary(*xml, destData);
    }
}

void DriftboxAudioProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    if (data == nullptr || sizeInBytes <= 0)
    {
        return;
    }

    auto xml = getXmlFromBinary(data, sizeInBytes);
    if (xml == nullptr || !xml->hasTagName(kStateTag))
    {
        return;
    }

    auto tree = juce::ValueTree::fromXml(*xml);
    if (!tree.isValid())
    {
        return;
    }

    // Una versione mancante vale 1. Uno stato scritto da un plugin più recente non viene caricato.
    const int version = static_cast<int>(tree.getProperty(kStateVersionProp, 1));
    if (version > kStateVersion)
    {
        return;
    }

    // Versione del generatore del profilo: per ora esiste solo la 1 (kProfileVersion), quindi non c'è nulla
    // da migrare. Una versione mancante vale 1; una più recente del plugin viene rifiutata come
    // stateVersion, perché lo stesso seed darebbe tolleranze diverse.
    const int profileVersion = static_cast<int>(tree.getProperty(kProfileVersionProp, 1));
    if (profileVersion > kProfileVersion)
    {
        return;
    }

    // Seed mancante o malformato: il seed corrente resta invariato. Le due proprietà non restano
    // nell'albero dell'APVTS: getStateInformation() le riscrive dal seed vivo.
    std::uint64_t seed = 0;
    const bool haveSeed = seedFromHex(tree.getProperty(kDriftSeedProp), seed);
    tree.removeProperty(kDriftSeedProp, nullptr);
    tree.removeProperty(kProfileVersionProp, nullptr);

    migrateState(tree, version);
    apvts_.replaceState(tree);
    if (haveSeed)
    {
        // Prima il seed, poi il flag: l'audio thread che vede il flag vede anche il seed nuovo.
        seed_.store(seed, std::memory_order_release);
        seedSnapPending_.store(true, std::memory_order_release);
    }
    clearUndoHistory(); // l'intero stato è stato sostituito: i passi precedenti non valgono più
}

void DriftboxAudioProcessor::migrateState(juce::ValueTree&, int)
{
    // Punto per le migrazioni future: quando kStateVersion aumenta, aggiornare l'albero qui passo per passo
    // (if (fromVersion < 2) { ... }). La versione 1 è il formato corrente, quindi per ora non c'è nulla da
    // fare.
}

juce::Point<int> DriftboxAudioProcessor::getUiSize() const
{
    const auto& s = apvts_.state;
    return {juce::jlimit(200, 4000, static_cast<int>(s.getProperty(kUiWidthProp, kDefaultUiWidth))),
            juce::jlimit(200, 4000, static_cast<int>(s.getProperty(kUiHeightProp, kDefaultUiHeight)))};
}

void DriftboxAudioProcessor::setUiSize(int width, int height)
{
    apvts_.state.setProperty(kUiWidthProp, width, nullptr);
    apvts_.state.setProperty(kUiHeightProp, height, nullptr);
}
} // namespace driftbox
