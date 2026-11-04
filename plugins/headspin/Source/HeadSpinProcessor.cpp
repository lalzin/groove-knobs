#include "HeadSpinProcessor.h"
#include "HeadSpinEditor.h"
#include <HeadSpinPresets.h>

namespace hs
{
namespace
{
    juce::StringArray divisionNames()
    {
        juce::StringArray names;
        for (int i = 0; i < headspin::division::count; ++i)
            names.add (headspin::division::name (i));
        return names;
    }

    gk::OneKnobConfig makeConfig (gk::PresetManager presets)
    {
        gk::OneKnobConfig cfg;
        cfg.productName = HS_PRODUCT_NAME;
        cfg.version = HS_VERSION_STRING;
        cfg.updateSlug = "head-spin";
        cfg.suiteName = GK_SUITE_NAME;
        cfg.companyName = GK_COMPANY_NAME;
        cfg.presets = std::move (presets);
        cfg.amountParamId = "mix";
        cfg.amountParamName = "Mix";
        cfg.defaultAmount = 1.0f;
        cfg.knobLowLabel = "DRY";
        cfg.knobHighLabel = "SPIN";
        cfg.allowMonoToStereo = true;
        cfg.wantsMidi = true;          // RETRIGGER MIDI
        cfg.engineAlwaysRuns = true;   // the motion keeps time at MIX 0 too (and the room's tail)
        cfg.transparencyZone = 0.01f;  // (MIX 0 is the input exactly in the engine already)
        cfg.presetSetsParameters = true;
        cfg.editorBuildsControls = false;
        cfg.editorWidth = 760; // the canvas of Kick Lock, whose layout HEAD SPIN takes (Alan)
        cfg.editorHeight = 480;
        cfg.tailSeconds = 0.5;         // the small room
        const auto shapeText = [] (float v)
        {
            const char* names[] { "Circle", "Oval", "Triangle", "Square", "Eight", "Star" };
            const int a = juce::jlimit (0, 5, (int) std::floor (v + 1.0e-4f));
            const float f = v - (float) a;
            if (f < 0.01f || a == 5)
                return juce::String (names[a]);
            return juce::String (names[a]) + " > " + names[a + 1] + " " + juce::String (juce::roundToInt (f * 100.0f)) + " %";
        };
        cfg.knobs = { { "shape", "Shape", "SHAPE", 0.0f, 0.0f, 5.0f, 1.0f, "" },
                      { "width", "Width", "WIDTH", 1.0f, 0.2f, 1.0f, 1.0f, "%" },
                      { "depth", "Depth", "DEPTH", 1.0f, 0.2f, 1.0f, 1.0f, "%" },
                      { "rotation", "Rotation", "ROTATION", 0.0f, -180.0f, 180.0f, 1.0f, "°" },
                      { "rateHz", "Rate", "RATE", 0.5f, 0.01f, 20.0f, 0.2306f, "Hz" }, // 1 Hz at the middle
                      { "phase", "Start", "START", 0.0f, 0.0f, 360.0f, 1.0f, "°" },
                      { "distance", "Distance", "DISTANCE", 0.5f },
                      { "groove", "Groove", "GROOVE", 0.0f },
                      { "level", "Level", "LEVEL", 0.0f, -12.0f, 6.0f, 1.0f, "dB" } };
        cfg.knobs[HeadSpinDsp::shapeKnob].textFromValue = shapeText;
        cfg.knobs[HeadSpinDsp::rateHzKnob].textFromValue = [] (float v) { return juce::String (v, v < 1.0f ? 2 : 1) + " Hz"; };
        cfg.knobs[HeadSpinDsp::levelKnob].textFromValue = [] (float v) { return juce::String (v, 1) + " dB"; };
        cfg.choices = { { "direction", "Direction", "DIRECTION", { "Left to right", "Right to left" }, 0, true },
                        { "rateMode", "Mode", "MODE", { "Hz", "Sync" }, 1, true },
                        { "rateSync", "Division", "DIVISION", divisionNames(), headspin::division::oneBar, true },
                        { "render", "Render", "RENDER", { "Speakers", "Headphones" }, 0, true },
                        { "retrigger", "Retrigger", "RETRIGGER", { "Off", "MIDI", "Transient" }, 0, true } };
        return cfg;
    }
} // namespace

/** Applies a preset's motion when the preset changes (the preset parameter
    may move on the audio thread: deferred to the message thread). */
class HeadSpinProcessor::PresetWatcher final : private juce::AudioProcessorValueTreeState::Listener, private juce::AsyncUpdater
{
public:
    explicit PresetWatcher (HeadSpinProcessor& p) : proc (p) { proc.getValueTreeState().addParameterListener ("preset", this); }
    ~PresetWatcher() override
    {
        proc.getValueTreeState().removeParameterListener ("preset", this);
        cancelPendingUpdate();
    }

private:
    void parameterChanged (const juce::String&, float) override
    {
        if (juce::MessageManager::existsAndIsCurrentThread())
            handleAsyncUpdate();
        else
            triggerAsyncUpdate();
    }
    void handleAsyncUpdate() override { proc.applyPreset (proc.getPresetParameter().getIndex()); }
    HeadSpinProcessor& proc;
};

gk::PresetManager HeadSpinProcessor::loadBuiltInPresets()
{
    struct Entry { const char* data; int size; };
    const Entry entries[] = {
        { HSPresets::merry_go_round_json, HSPresets::merry_go_round_jsonSize }, // the default
        { HSPresets::ping_pong_club_json, HSPresets::ping_pong_club_jsonSize },
        { HSPresets::triangle_trance_json, HSPresets::triangle_trance_jsonSize },
        { HSPresets::behind_you_json, HSPresets::behind_you_jsonSize },
        { HSPresets::headspin_json, HSPresets::headspin_jsonSize },
        { HSPresets::figure_eight_json, HSPresets::figure_eight_jsonSize },
        { HSPresets::reverse_ride_json, HSPresets::reverse_ride_jsonSize },
    };
    gk::PresetManager pm;
    for (const auto& e : entries)
    {
        const auto r = pm.add (juce::String::fromUTF8 (e.data, e.size));
        jassertquiet (r.wasOk());
    }
    return pm;
}

HeadSpinProcessor::HeadSpinProcessor() : HeadSpinProcessor (loadBuiltInPresets()) {}

HeadSpinProcessor::HeadSpinProcessor (gk::PresetManager presets)
    : OneKnobProcessor (makeConfig (std::move (presets)), std::make_unique<HeadSpinDsp>())
{
    applyPreset (0);
    watcher = std::make_unique<PresetWatcher> (*this);
    startTimerHz (30);
}

HeadSpinProcessor::~HeadSpinProcessor()
{
    stopTimer();
}

juce::AudioProcessorEditor* HeadSpinProcessor::createEditor()
{
    return new HeadSpinEditor (*this);
}

void HeadSpinProcessor::applyPreset (int index)
{
    if (loadingState || index == appliedPreset || index < 0 || index >= getPresets().size())
        return;
    appliedPreset = index;
    const auto params = getPresets()[index].json.getProperty ("params", {});
    auto set = [] (juce::RangedAudioParameter& p, float plain)
    {
        p.beginChangeGesture();
        p.setValueNotifyingHost (p.convertTo0to1 (plain));
        p.endChangeGesture();
    };
    // the motion; LEVEL stays the user's (its volume, not the preset's)
    const auto& knobs = getConfig().knobs;
    for (int k = 0; k < (int) knobs.size(); ++k)
        if (k != HeadSpinDsp::levelKnob)
            set (getKnobParameter (k), (float) params.getProperty (knobs[(size_t) k].id, knobs[(size_t) k].defaultValue));
    // direction and speed by name; RENDER and RETRIGGER stay the user's
    const auto& choices = getConfig().choices;
    for (int c : { (int) HeadSpinDsp::directionChoice, (int) HeadSpinDsp::rateModeChoice, (int) HeadSpinDsp::rateSyncChoice })
    {
        const auto& spec = choices[(size_t) c];
        const int i = spec.options.indexOf (params.getProperty (spec.id, {}).toString());
        set (getChoiceParameter (c), (float) (i >= 0 ? i : spec.defaultIndex));
    }
    updateShapeNow();
}

void HeadSpinProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    // the saved settings win over the preset's: the restored preset is marked applied
    loadingState = true;
    OneKnobProcessor::setStateInformation (data, sizeInBytes);
    loadingState = false;
    appliedPreset = getPresetParameter().getIndex();
    updateShapeNow();
}
} // namespace hs
