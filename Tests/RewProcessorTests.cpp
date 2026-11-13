#include <catch2/catch_test_macros.hpp>
#include "PluginProcessor.h"

// REW sequencer spec §3: the last sequence persists with the plugin state

TEST_CASE ("REW sequencer in processor: the sequence persists in the plugin state", "[rew][processor][state]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    auto src = std::make_unique<SPLMeterAudioProcessor>();
    CHECK (src->getRewSequence().steps.empty());   // default without a stored node

    rew::Sequence q;
    q.name = "Living room";
    q.loops = 3;
    rew::Step s;
    s.name = "L";
    s.output = juce::String ("L");
    s.level = rew::Level { -20.0, "dBFS" };
    q.steps = { s };
    rew::addPositions (q, 0, 2);
    src->setRewSequence (q);

    juce::MemoryBlock blob;
    src->getStateInformation (blob);
    auto restored = std::make_unique<SPLMeterAudioProcessor>();
    restored->setStateInformation (blob.getData(), (int) blob.getSize());
    CHECK (restored->getRewSequence() == q);
}
