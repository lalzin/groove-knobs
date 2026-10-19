// Boogie Trio state, parameters and preset/toggle interaction tests.

#include "BtTestHelpers.h"

using namespace bttest;

TEST_CASE ("BT 7. State: save / restore round trip", "[bt][state]")
{
    BoogieTrioProcessor a;
    setParam (a.getAmountParameter(), 0.42f);
    setParam (a.getPresetParameter(), 3.0f); // Velvet: vintage off, wide off by default
    setParam (a.getVintageParameter(), 1.0f); // user overrides
    setParam (a.getBypassParameterObject(), 1.0f);
    a.editorScale.store (1.25f);
    REQUIRE (a.getVintageParameter().get());
    REQUIRE_FALSE (a.getWideParameter().get());

    juce::MemoryBlock state;
    a.getStateInformation (state);

    BoogieTrioProcessor b;
    b.setStateInformation (state.getData(), (int) state.getSize());
    CHECK (b.getAmountParameter().getValue() == Catch::Approx (0.42f).margin (1.0e-6));
    CHECK (b.getPresetParameter().getIndex() == 3);
    CHECK (b.getVintageParameter().get());       // the user's choice, not the preset default
    CHECK_FALSE (b.getWideParameter().get());
    CHECK (b.getBypassParameterObject().get());
    CHECK (b.editorScale.load() == Catch::Approx (1.25f));
}

TEST_CASE ("BT 7b. Changing preset applies its VINTAGE / WIDE defaults", "[bt][state]")
{
    BoogieTrioProcessor p;
    const bool expected[8][2] { { true, true }, { false, true }, { true, false }, { false, false }, { true, true }, { false, true }, { true, true }, { false, true } };
    CHECK (p.getVintageParameter().get() == expected[0][0]);
    CHECK (p.getWideParameter().get() == expected[0][1]);
    for (int i : { 1, 2, 3, 4, 5, 6, 7, 0, 3 })
    {
        CAPTURE (i);
        p.setCurrentProgram (i);
        CHECK (p.getVintageParameter().get() == expected[i][0]);
        CHECK (p.getWideParameter().get() == expected[i][1]);
    }
}

TEST_CASE ("BT 7c. Parameters: order, ids, defaults", "[bt][state]")
{
    BoogieTrioProcessor p;
    const auto& params = p.getParameters();
    REQUIRE (params.size() >= 5);
    const char* ids[] { "amount", "preset", "vintage", "wide", "bypass" };
    for (int i = 0; i < 5; ++i)
    {
        auto* rp = dynamic_cast<juce::RangedAudioParameter*> (params[i]);
        REQUIRE (rp != nullptr);
        CHECK (rp->getParameterID() == ids[i]);
    }
    CHECK (p.getAmountParameter().getDefaultValue() == Catch::Approx (0.0f));
    CHECK (p.getAmountParameter().getText (0.5f, 32) == "50 %");
    CHECK (p.getNumPrograms() == 8);
    CHECK (p.getProgramName (0) == "Disco Strings");
    CHECK (p.getBypassParameter() == &p.getBypassParameterObject());
    CHECK (p.getPresetParameter().isMetaParameter());
}

TEST_CASE ("BT 7d. Bus layouts: mono, stereo and mono -> stereo", "[bt][state]")
{
    BoogieTrioProcessor p;
    for (auto l : { Layout::mono, Layout::stereo, Layout::monoToStereo })
        setLayout (p, l);
    juce::AudioProcessor::BusesLayout bad;
    bad.inputBuses.add (juce::AudioChannelSet::stereo());
    bad.outputBuses.add (juce::AudioChannelSet::mono());
    CHECK_FALSE (p.checkBusesLayoutSupported (bad));
}

#include "BoogieTrioEditor.h"

TEST_CASE ("BT UI: drawing the full trio stays cheap", "[bt][ui]")
{
    // 120 frames of the mascot band at 100 % (three characters, sparkles),
    // at 200 % scale: must average well under a 60 fps frame budget.
    BoogieTrioProcessor p;
    setParam (p.getAmountParameter(), 1.0f);
    p.editorScale.store (2.0f);
    std::unique_ptr<juce::AudioProcessorEditor> ed (p.createEditor());
    auto& editor = dynamic_cast<gk::OneKnobEditor&> (*ed);
    editor.snapForSnapshot();
    auto& mascot = editor.getMascot();
    juce::Image target (juce::Image::ARGB, mascot.getWidth() * 2, mascot.getHeight() * 2, true);
    const auto start = juce::Time::getMillisecondCounterHiRes();
    constexpr int frames = 120;
    for (int i = 0; i < frames; ++i)
    {
        mascot.advance (1.0 / 60.0, 1.0f, 0.0f);
        juce::Graphics g (target);
        g.addTransform (juce::AffineTransform::scale (2.0f));
        mascot.paintEntireComponent (g, false);
    }
    const double msPerFrame = (juce::Time::getMillisecondCounterHiRes() - start) / frames;
    INFO ("mascot: " << msPerFrame << " ms per frame");
    WARN ("mascot draw time at 200 %: " << msPerFrame << " ms per frame");
    if (gktest::timingChecksEnforced())
        CHECK (msPerFrame < 4.0);
}
