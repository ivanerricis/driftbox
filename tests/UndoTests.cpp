// SPDX-License-Identifier: AGPL-3.0-or-later
// Undo/redo dei gesti della UI e di Randomize.
#include "PluginProcessor.h"
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using namespace driftbox;
using Catch::Approx;

namespace
{
float raw(DriftboxAudioProcessor& p, const char* id)
{
    return p.getAPVTS().getRawParameterValue(id)->load();
}

// Quello che fa l'automazione dell'host: un cambio di valore senza gesto.
void automate(DriftboxAudioProcessor& p, const char* id, float value)
{
    auto* param = p.getAPVTS().getParameter(id);
    param->setValueNotifyingHost(param->convertTo0to1(value));
}

// Un gesto completo dalla UI su un parametro (come fa un attachment).
void gesture(DriftboxAudioProcessor& p, const char* id, float value)
{
    juce::ParameterAttachment att(*p.getAPVTS().getParameter(id), [](float) {}, nullptr);
    att.setValueAsCompleteGesture(value);
}
} // namespace

TEST_CASE("Undo: a gesture is one step, undo restores and redo reapplies", "[undo]")
{
    DriftboxAudioProcessor p;
    auto& um = p.getUndoManager();
    CHECK_FALSE(um.canUndo());
    CHECK_FALSE(um.canRedo());

    gesture(p, pid::drift, 0.8f);
    CHECK(raw(p, pid::drift) == Approx(0.8f));
    CHECK(um.canUndo());
    CHECK_FALSE(um.canRedo());
    CHECK(um.getUndoDescriptions().size() == 1);

    CHECK(um.undo());
    CHECK(raw(p, pid::drift) == Approx(0.5f));
    CHECK_FALSE(um.canUndo());
    CHECK(um.canRedo());

    CHECK(um.redo());
    CHECK(raw(p, pid::drift) == Approx(0.8f));
    CHECK(um.canUndo());
    CHECK_FALSE(um.canRedo());
}

TEST_CASE("Undo: a drag is one step whatever the number of value changes", "[undo]")
{
    DriftboxAudioProcessor p;
    auto& um = p.getUndoManager();
    juce::Slider slider;
    slider.setRange(0.0, 1.0);
    juce::SliderParameterAttachment att(*p.getAPVTS().getParameter(pid::noise), slider, nullptr);
    {
        juce::Slider::ScopedDragNotification drag(slider);
        CHECK(p.isUiGestureOpen());
        for (double v : {0.1, 0.4, 0.9, 0.6})
        {
            slider.setValue(v, juce::sendNotificationSync);
        }
    }
    CHECK_FALSE(p.isUiGestureOpen());
    CHECK(raw(p, pid::noise) == Approx(0.6f).margin(0.01f));
    CHECK(um.getUndoDescriptions().size() == 1);

    CHECK(um.undo());
    CHECK(raw(p, pid::noise) == Approx(0.0f).margin(1.0e-3f));
    CHECK_FALSE(um.canUndo());
    CHECK(um.redo());
    CHECK(raw(p, pid::noise) == Approx(0.6f).margin(0.01f));
}

TEST_CASE("Undo: consecutive gestures are separate steps", "[undo]")
{
    DriftboxAudioProcessor p;
    auto& um = p.getUndoManager();
    gesture(p, pid::saturation, 0.7f);
    gesture(p, pid::gainTol, 0.1f);
    CHECK(um.getUndoDescriptions().size() == 2);

    CHECK(um.undo());
    CHECK(raw(p, pid::gainTol) == Approx(0.5f));
    CHECK(raw(p, pid::saturation) == Approx(0.7f));
    CHECK(um.undo());
    CHECK(raw(p, pid::saturation) == Approx(0.2f));
    CHECK_FALSE(um.undo());
}

TEST_CASE("Undo: no step when the value does not change", "[undo]")
{
    DriftboxAudioProcessor p;
    auto& um = p.getUndoManager();

    gesture(p, pid::drift, 0.5f); // il valore di default, di nuovo
    CHECK_FALSE(um.canUndo());

    juce::Slider slider;
    slider.setRange(0.0, 1.0);
    juce::SliderParameterAttachment att(*p.getAPVTS().getParameter(pid::drift), slider, nullptr);
    {
        juce::Slider::ScopedDragNotification touched(slider); // toccato, non mosso
    }
    CHECK_FALSE(um.canUndo());
}

TEST_CASE("Undo: a new change drops the redo steps", "[undo]")
{
    DriftboxAudioProcessor p;
    auto& um = p.getUndoManager();
    gesture(p, pid::drift, 0.9f);
    um.undo();
    CHECK(um.canRedo());
    gesture(p, pid::drift, 0.3f);
    CHECK_FALSE(um.canRedo());
}

TEST_CASE("Undo: changes made by the host without a gesture are not recorded", "[undo]")
{
    DriftboxAudioProcessor p;
    auto& um = p.getUndoManager();
    for (int i = 0; i < 50; ++i)
    {
        automate(p, pid::drift, 0.01f * static_cast<float>(i));
        automate(p, pid::outGain, -12.0f + 0.2f * static_cast<float>(i));
    }
    CHECK_FALSE(um.canUndo());
    CHECK(um.getNumberOfUnitsTakenUpByStoredCommands() == 0);
}

TEST_CASE("Undo: toggles and choices", "[undo]")
{
    DriftboxAudioProcessor p;
    auto& um = p.getUndoManager();

    juce::ToggleButton button;
    juce::ButtonParameterAttachment att(*p.getAPVTS().getParameter(pid::bypass), button, nullptr);
    button.setToggleState(true, juce::sendNotificationSync); // quello che fa un click
    CHECK(raw(p, pid::bypass) > 0.5f);
    CHECK(um.undo());
    CHECK(raw(p, pid::bypass) < 0.5f);
    CHECK(um.redo());
    CHECK(raw(p, pid::bypass) > 0.5f);
    um.clearUndoHistory();

    gesture(p, pid::channels, 3.0f);
    CHECK(raw(p, pid::channels) == Approx(3.0f));
    CHECK(um.undo());
    CHECK(raw(p, pid::channels) == Approx(static_cast<float>(kDefaultChannelIndex)));
}

TEST_CASE("Undo: Randomize is one step named Randomize", "[undo]")
{
    DriftboxAudioProcessor p;
    auto& um = p.getUndoManager();
    const auto before = p.getSeed();

    p.randomize();
    const auto after = p.getSeed();
    CHECK(after != before);
    REQUIRE(um.getUndoDescriptions().size() == 1);
    CHECK(um.getUndoDescription() == "Randomize");

    CHECK(um.undo());
    CHECK(p.getSeed() == before);
    CHECK_FALSE(um.canUndo());
    CHECK(um.canRedo());
    CHECK(um.getRedoDescription() == "Randomize");

    CHECK(um.redo());
    CHECK(p.getSeed() == after);
    CHECK(um.canUndo());
    CHECK_FALSE(um.canRedo());
}

TEST_CASE("Undo: each Randomize is its own step and the seed always changes", "[undo]")
{
    DriftboxAudioProcessor p;
    auto& um = p.getUndoManager();
    const auto s0 = p.getSeed();
    p.randomize();
    const auto s1 = p.getSeed();
    p.randomize();
    const auto s2 = p.getSeed();
    CHECK(s1 != s0);
    CHECK(s2 != s1);
    CHECK(um.getUndoDescriptions().size() == 2);

    um.undo();
    CHECK(p.getSeed() == s1);
    um.undo();
    CHECK(p.getSeed() == s0);
}

TEST_CASE("Undo: Randomize and parameter gestures share one history", "[undo]")
{
    DriftboxAudioProcessor p;
    auto& um = p.getUndoManager();
    const auto s0 = p.getSeed();
    gesture(p, pid::drift, 0.9f);
    p.randomize();
    const auto s1 = p.getSeed();

    um.undo();
    CHECK(p.getSeed() == s0);
    CHECK(raw(p, pid::drift) == Approx(0.9f));
    um.undo();
    CHECK(raw(p, pid::drift) == Approx(0.5f));
    um.redo();
    um.redo();
    CHECK(p.getSeed() == s1);
    CHECK(raw(p, pid::drift) == Approx(0.9f));
}

TEST_CASE("Undo: restoring a state clears the history", "[undo]")
{
    DriftboxAudioProcessor p;
    auto& um = p.getUndoManager();

    gesture(p, pid::drift, 0.9f);
    p.randomize();
    REQUIRE(um.canUndo());
    um.undo();
    REQUIRE(um.canRedo());

    juce::MemoryBlock block;
    p.getStateInformation(block);
    p.setStateInformation(block.getData(), static_cast<int>(block.getSize()));
    CHECK_FALSE(um.canUndo());
    CHECK_FALSE(um.canRedo());

    // Dati non validi non cambiano nulla e mantengono la cronologia.
    gesture(p, pid::drift, 0.3f);
    const char junk[] = "junk";
    p.setStateInformation(junk, sizeof(junk));
    CHECK(um.canUndo());
}

TEST_CASE("Undo: the history is bounded", "[undo]")
{
    DriftboxAudioProcessor p;
    auto& um = p.getUndoManager();
    for (int i = 0; i < 1000; ++i)
    {
        gesture(p, pid::drift, i % 2 == 0 ? 0.9f : 0.1f);
    }
    CHECK(um.getNumberOfUnitsTakenUpByStoredCommands() <= DriftboxAudioProcessor::kUndoMaxUnits + 1);
    CHECK(um.getUndoDescriptions().size() >= DriftboxAudioProcessor::kUndoMinTransactions);
    CHECK(um.getUndoDescriptions().size() <= DriftboxAudioProcessor::kUndoMaxUnits + 1);
}
