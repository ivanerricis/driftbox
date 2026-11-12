// SPDX-License-Identifier: AGPL-3.0-or-later
#include <catch2/catch_session.hpp>
#include <juce_events/juce_events.h>

int main(int argc, char* argv[])
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    return Catch::Session().run(argc, argv);
}
