// State, programs and parameter text tests.

#include "TestHelpers.h"
#include <catch2/catch_approx.hpp>

using namespace fftest;
using ff::FrenchFilterProcessor;

TEST_CASE ("6. State: save / restore round trip", "[state]")
{
    FrenchFilterProcessor a;
    setParam (a.getAmountParameter(), 0.37f);
    setParam (a.getPresetParameter(), 2.0f);
    setParam (a.getBypassParameterObject(), 1.0f);
    a.editorScale.store (1.5f);

    juce::MemoryBlock state;
    a.getStateInformation (state);
    REQUIRE (state.getSize() > 0);

    FrenchFilterProcessor b;
    b.setStateInformation (state.getData(), (int) state.getSize());
    CHECK (b.getAmountParameter().convertFrom0to1 (b.getAmountParameter().getValue()) == Catch::Approx (0.37f).margin (1.0e-6));
    CHECK (b.getPresetParameter().getIndex() == 2);
    CHECK (b.getBypassParameterObject().get());
    CHECK (b.editorScale.load() == Catch::Approx (1.5f));
    CHECK (b.getCurrentProgram() == 2);

    // The saved state carries a version number for future migrations.
    const auto xml = juce::AudioProcessor::getXmlFromBinary (state.getData(), (int) state.getSize());
    REQUIRE (xml != nullptr);
    CHECK (xml->getIntAttribute ("version") == gk::OneKnobProcessor::stateVersion);
    CHECK (xml->getStringAttribute ("preset") == "Muffled Club");
}

TEST_CASE ("6b. State: invalid or newer data is ignored", "[state]")
{
    FrenchFilterProcessor p;
    setParam (p.getAmountParameter(), 0.25f);

    const char garbage[] = "this is not a state";
    p.setStateInformation (garbage, (int) sizeof (garbage));
    CHECK (p.getAmountParameter().getValue() == Catch::Approx (0.25f));

    juce::XmlElement future ("GrooveKnobsState");
    future.setAttribute ("version", 99);
    future.setAttribute ("amount", 0.9);
    juce::MemoryBlock mb;
    juce::AudioProcessor::copyXmlToBinary (future, mb);
    p.setStateInformation (mb.getData(), (int) mb.getSize());
    CHECK (p.getAmountParameter().getValue() == Catch::Approx (0.25f));
}

TEST_CASE ("6c. Programs mirror the preset parameter", "[state]")
{
    FrenchFilterProcessor p;
    REQUIRE (p.getNumPrograms() == 9);
    CHECK (p.getProgramName (0) == "Filtered Disco");
    CHECK (p.getProgramName (3) == "Acid Disco");
    CHECK (p.getProgramName (7) == "Lady Groove");
    CHECK (p.getProgramName (8) == "Space");
    p.setCurrentProgram (3);
    CHECK (p.getPresetParameter().getIndex() == 3);
    setParam (p.getPresetParameter(), 1.0f);
    CHECK (p.getCurrentProgram() == 1);
    CHECK (p.getBypassParameter() == &p.getBypassParameterObject());
}

TEST_CASE ("6d. Amount parameter: id, order, default, text", "[state]")
{
    FrenchFilterProcessor p;
    const auto& params = p.getParameters();
    REQUIRE (params.size() >= 3);
    auto* first = dynamic_cast<juce::RangedAudioParameter*> (params[0]);
    REQUIRE (first != nullptr);
    CHECK (first->getParameterID() == "amount");
    CHECK (first->getDefaultValue() == Catch::Approx (0.5f)); // centred
    CHECK (first->getText (0.5f, 32) == juce::String (juce::CharPointer_UTF8 ("\xe2\x80\x94")));
    CHECK (first->getText (0.29f, 32) == "LP 42 %");
    CHECK (first->getText (0.0f, 32) == "LP 100 %");
    CHECK (first->getText (1.0f, 32) == "HP 100 %");
    CHECK (first->getText (0.65f, 32) == "HP 30 %");
    CHECK (first->getValueForText ("LP 40") == Catch::Approx (0.3f));
    CHECK (first->getValueForText ("HP 20 %") == Catch::Approx (0.6f));
    CHECK (first->getValueForText ("-40") == Catch::Approx (0.3f));
    CHECK (first->getValueForText ("+100") == Catch::Approx (1.0f));
    CHECK (first->getValueForText ("0") == Catch::Approx (0.5f));
}

TEST_CASE ("6e. State: a v1 session (unipolar knob) is migrated to the left side", "[state]")
{
    for (float v1 : { 0.0f, 0.4f, 1.0f })
    {
        CAPTURE (v1);
        juce::XmlElement old ("GrooveKnobsState");
        old.setAttribute ("version", 1);
        old.setAttribute ("amount", v1);
        old.setAttribute ("preset", "Acid Disco");
        juce::MemoryBlock mb;
        juce::AudioProcessor::copyXmlToBinary (old, mb);

        FrenchFilterProcessor p;
        p.setStateInformation (mb.getData(), (int) mb.getSize());
        const float knob = p.getAmountParameter().getValue();
        CHECK (p.macroFor (knob) == Catch::Approx (-v1).margin (1.0e-5));
        CHECK (p.getPresetParameter().getIndex() == 3);
    }
}
