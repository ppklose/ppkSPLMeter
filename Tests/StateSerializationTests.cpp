#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_core/juce_core.h>

using Catch::Approx;

// Test the state serialization format used by the processor.
// We test the XML round-trip logic without needing the full AudioProcessor,
// by replicating the get/setStateInformation serialization format.

static juce::ValueTree createTestState()
{
    juce::ValueTree state ("Parameters");
    state.setProperty ("calOffset", 132.5f, nullptr);
    state.setProperty ("peakHoldTime", 3.0f, nullptr);
    state.setProperty ("logDuration", 120.0f, nullptr);
    state.setProperty ("splTimeWeight", 1, nullptr); // SLOW
    state.setProperty ("fftGain", 5.0f, nullptr);
    return state;
}

TEST_CASE ("State serialization: XML round-trip preserves values", "[state]")
{
    auto state = createTestState();

    // Serialize to XML
    std::unique_ptr<juce::XmlElement> xml (state.createXml());
    REQUIRE (xml != nullptr);

    // Add MIDI CC attributes (same as getStateInformation)
    xml->setAttribute ("midiCC_calOffset", 7);
    xml->setAttribute ("midiCC_peakHoldTime", -1);
    xml->setAttribute ("midiCC_fftGain", 11);

    // Add channel names
    xml->setAttribute ("channelName_0", "Mic1");
    xml->setAttribute ("channelName_1", "Mic2");

    // Serialize to binary
    juce::MemoryBlock destData;
    juce::AudioProcessor::copyXmlToBinary (*xml, destData);

    REQUIRE (destData.getSize() > 0);

    // Deserialize from binary
    std::unique_ptr<juce::XmlElement> restored (
        juce::AudioProcessor::getXmlFromBinary (destData.getData(),
                                                 static_cast<int> (destData.getSize())));
    REQUIRE (restored != nullptr);
    REQUIRE (restored->hasTagName ("Parameters"));

    // Verify parameter values
    auto restoredState = juce::ValueTree::fromXml (*restored);
    REQUIRE (static_cast<float> (restoredState.getProperty ("calOffset")) == Approx (132.5f));
    REQUIRE (static_cast<float> (restoredState.getProperty ("logDuration")) == Approx (120.0f));
    REQUIRE (static_cast<int> (restoredState.getProperty ("splTimeWeight")) == 1);

    // Verify MIDI CC
    REQUIRE (restored->getIntAttribute ("midiCC_calOffset", -1) == 7);
    REQUIRE (restored->getIntAttribute ("midiCC_peakHoldTime", -1) == -1);
    REQUIRE (restored->getIntAttribute ("midiCC_fftGain", -1) == 11);

    // Verify channel names
    REQUIRE (restored->getStringAttribute ("channelName_0") == "Mic1");
    REQUIRE (restored->getStringAttribute ("channelName_1") == "Mic2");
}

TEST_CASE ("State serialization: empty state produces valid XML", "[state]")
{
    juce::ValueTree state ("Parameters");
    std::unique_ptr<juce::XmlElement> xml (state.createXml());
    REQUIRE (xml != nullptr);

    juce::MemoryBlock destData;
    juce::AudioProcessor::copyXmlToBinary (*xml, destData);

    auto restored = juce::AudioProcessor::getXmlFromBinary (destData.getData(),
                                                             static_cast<int> (destData.getSize()));
    REQUIRE (restored != nullptr);
    REQUIRE (restored->hasTagName ("Parameters"));
}

TEST_CASE ("State serialization: invalid binary returns nullptr", "[state]")
{
    const char garbage[] = "not valid binary data at all";
    auto result = juce::AudioProcessor::getXmlFromBinary (garbage, sizeof (garbage));
    REQUIRE (result == nullptr);
}

TEST_CASE ("State serialization: channel names default to empty", "[state]")
{
    auto state = createTestState();
    std::unique_ptr<juce::XmlElement> xml (state.createXml());

    // No channel names set — getAttribute should return empty default
    for (int i = 0; i < 32; ++i)
        REQUIRE (xml->getStringAttribute ("channelName_" + juce::String (i), "").isEmpty());
}
