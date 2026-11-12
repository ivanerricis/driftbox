// SPDX-License-Identifier: AGPL-3.0-or-later
#include "PluginProcessor.h"

#include "PluginEditor.h"
#include "PluginInfo.h"

#include <array>
#include <vector>

namespace driftbox
{
namespace
{
const juce::Identifier kStateVersionProp{"stateVersion"};
const juce::Identifier kProfileVersionProp{"profileVersion"};
const juce::Identifier kLegacySeedProp{"driftSeed"};
const juce::Identifier kUiWidthProp{"uiWidth"};
const juce::Identifier kUiHeightProp{"uiHeight"};
const juce::Identifier kAdvancedOpenProp{"advancedOpen"};
const juce::Identifier kPresetNameProp{"presetName"};

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
            // Dentro randomize() e applyPresetValues() la transazione è già aperta: un nome nuovo la
            // rinominerebbe.
            if (depth_ == 0 && !processor_.groupedEdit_)
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

DriftboxAudioProcessor::DriftboxAudioProcessor() : DriftboxAudioProcessor(drawChannelPair(0, 0)) {}

DriftboxAudioProcessor::DriftboxAudioProcessor(ChannelPair initialChannels)
    : AudioProcessor(BusesProperties()
                         .withInput("Input", juce::AudioChannelSet::stereo(), true)
                         .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      apvts_(*this, nullptr, juce::Identifier(kStateTag),
             createParameterLayout(juce::jlimit(1, kNumConsoleChannels, initialChannels.first),
                                   juce::jlimit(1, kNumConsoleChannels, initialChannels.second))),
      params_(apvts_)
{
    // L'APVTS non riceve un UndoManager di proposito, vedi UndoRecorder.
    undoRecorder_ = std::make_unique<UndoRecorder>(*this, undoManager_);
    // La coppia di canali casuale di questa istanza è il DEFAULT dei parametri (vedi il costruttore
    // delegante), quindi valore == default fin dall'inizio, come richiede CLAP; uno stato salvato la
    // sostituisce.
    // Solo la cartella: la lettura del disco avviene quando serve (l'host può creare molte istanze).
    presets_.setUserFolder(settings_->getEffectivePresetFolder());
    startTimerHz(30);
}

DriftboxAudioProcessor::~DriftboxAudioProcessor()
{
    stopTimer();
}

DriftboxAudioProcessor::ChannelPair DriftboxAudioProcessor::drawChannelPair() const
{
    return drawChannelPair(juce::roundToInt(params_.chanL->load()), juce::roundToInt(params_.chanR->load()));
}

DriftboxAudioProcessor::ChannelPair DriftboxAudioProcessor::drawChannelPair(int curL, int curR)
{
    int l = curL, r = curR;
    while (l == curL && r == curR)
    {
        l = juce::Random::getSystemRandom().nextInt(kNumConsoleChannels) + 1;
        r = l % kNumConsoleChannels + 1;
    }
    return {l, r};
}

void DriftboxAudioProcessor::randomize()
{
    jassert(juce::MessageManager::getInstance()->isThisTheMessageThread());
    const auto [l, r] = drawChannelPair();
    auto* pL = apvts_.getParameter(pid::chanL);
    auto* pR = apvts_.getParameter(pid::chanR);

    // Un solo passo: i due gesti annidati vengono uniti dal recorder nella transazione "Randomize".
    undoManager_.beginNewTransaction("Randomize");
    groupedEdit_ = true;
    pL->beginChangeGesture();
    pR->beginChangeGesture();
    pL->setValueNotifyingHost(pL->convertTo0to1(static_cast<float>(l)));
    pR->setValueNotifyingHost(pR->convertTo0to1(static_cast<float>(r)));
    pL->endChangeGesture();
    pR->endChangeGesture();
    groupedEdit_ = false;
}

void DriftboxAudioProcessor::applyPresetValues(const PresetValues& values, const juce::String& undoName)
{
    jassert(juce::MessageManager::getInstance()->isThisTheMessageThread());
    std::array<juce::RangedAudioParameter*, kNumPresetParams> params{};
    for (int i = 0; i < kNumPresetParams; ++i)
    {
        params[static_cast<size_t>(i)] = apvts_.getParameter(kPresetParamIds[i]);
        jassert(params[static_cast<size_t>(i)] != nullptr);
    }

    // Un solo passo: i gesti annidati vengono uniti dal recorder nella transazione col nome del preset.
    // I parametri si impostano uno per uno: lo stato (e quindi chanL, chanR, bypass e le proprietà) non si
    // tocca.
    undoManager_.beginNewTransaction(undoName);
    groupedEdit_ = true;
    for (auto* p : params)
    {
        p->beginChangeGesture();
    }
    for (int i = 0; i < kNumPresetParams; ++i)
    {
        auto* p = params[static_cast<size_t>(i)];
        p->setValueNotifyingHost(p->convertTo0to1(values[static_cast<size_t>(i)]));
    }
    for (auto* p : params)
    {
        p->endChangeGesture();
    }
    groupedEdit_ = false;
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

    // Versione del generatore dei profili di canale: per ora esiste solo la 1 (kProfileVersion), quindi non
    // c'è nulla da migrare. Una versione mancante vale 1; una più recente del plugin viene rifiutata come
    // stateVersion, perché lo stesso numero di canale darebbe tolleranze diverse. La proprietà non resta
    // nell'albero dell'APVTS: getStateInformation() la riscrive.
    const int profileVersion = static_cast<int>(tree.getProperty(kProfileVersionProp, 1));
    if (profileVersion > kProfileVersion)
    {
        return;
    }
    tree.removeProperty(kProfileVersionProp, nullptr);
    // Gli stati delle build di sviluppo con il vecchio modello a seed portano ancora questa proprietà: si
    // scarta, così non viene riscritta all'infinito.
    tree.removeProperty(kLegacySeedProp, nullptr);

    migrateState(tree, version);
    apvts_.replaceState(tree);
    clearUndoHistory(); // l'intero stato è stato sostituito: i passi precedenti non valgono più
    // Dice all'host che i valori dei parametri sono cambiati. Il wrapper CLAP traduce "programChanged" in
    // params.rescan(VALUES), che clap-validator richiede quando il caricamento dello stato cambia i valori.
    updateHostDisplay(ChangeDetails().withProgramChanged(true));
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

bool DriftboxAudioProcessor::hasStoredUiSize() const
{
    return apvts_.state.hasProperty(kUiWidthProp) && apvts_.state.hasProperty(kUiHeightProp);
}

juce::String DriftboxAudioProcessor::getPresetName() const
{
    return apvts_.state.getProperty(kPresetNameProp, juce::String()).toString();
}

void DriftboxAudioProcessor::setPresetName(const juce::String& name)
{
    // Senza UndoManager: il nome non è un passo di undo.
    apvts_.state.setProperty(kPresetNameProp, name, nullptr);
}

juce::String DriftboxAudioProcessor::getDebugInfo() const
{
    // Il wrapper CLAP di JUCE non imposta un tipo: tra i formati che compiliamo (VST3, CLAP, AU) "undefined"
    // vuol dire CLAP.
    const char* format = "CLAP";
    switch (wrapperType)
    {
        case wrapperType_VST3:
            format = "VST3";
            break;
        case wrapperType_AudioUnit:
        case wrapperType_AudioUnitv3:
            format = "AU";
            break;
        default:
            break;
    }
    juce::String text;
    text << info::kName << " " << info::kVersion << "\n"
         << "Format: " << format << "\n"
         << "Host: " << juce::PluginHostType().getHostDescription() << "\n"
         << "OS: " << juce::SystemStats::getOperatingSystemName() << "\n"
         << "Sample rate: " << juce::String(getSampleRate(), 0) << " Hz\n"
         << "Block size: " << getBlockSize() << "\n";
    return text;
}

bool DriftboxAudioProcessor::getAdvancedOpen() const
{
    return static_cast<bool>(apvts_.state.getProperty(kAdvancedOpenProp, false));
}

void DriftboxAudioProcessor::setAdvancedOpen(bool open)
{
    // Senza UndoManager: aprire o chiudere il pannello non è un passo di undo.
    apvts_.state.setProperty(kAdvancedOpenProp, open, nullptr);
}
} // namespace driftbox
