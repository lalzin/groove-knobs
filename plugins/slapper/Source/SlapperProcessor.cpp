#include "SlapperProcessor.h"
#include "SlapperEditor.h"
#include <SlapperPresets.h>

namespace slapper
{
namespace
{
    gk::OneKnobConfig makeConfig (gk::PresetManager presets)
    {
        gk::OneKnobConfig cfg;
        cfg.productName = SL_PRODUCT_NAME;
        cfg.version = SL_VERSION_STRING;
        cfg.updateSlug = "slapper"; // key in lalzin.com/plugins/versions.json
        cfg.suiteName = GK_SUITE_NAME;
        cfg.companyName = GK_COMPANY_NAME;
        cfg.presets = std::move (presets);
        cfg.isInstrument = true;
        cfg.amountParamId = "funk";
        cfg.amountParamName = "Funk";
        cfg.defaultAmount = 0.5f;
        cfg.amountRampSeconds = 0.008; // FUNK heard at once when turned fast (the engine smooths what would click)
        cfg.knobLowLabel = "ROUND";
        cfg.knobHighLabel = "FUNK";
        // in SlapperDsp::Knob order; VOLUME: -inf at the bottom, 0 dB at three quarters, +6 dB
        cfg.knobs = { { "attack", "Attack", "ATTACK", 0.5f },          // 0 finger, 0.5 slap, 1 pop
                      { "pick", "Pick position", "PICK", 0.15f, 0.05f, 0.5f },
                      { "string", "String", "STRING", 0.4f },
                      { "body", "Body", "BODY", 0.3f },
                      { "mute", "Mute", "MUTE", 0.0f },
                      { "drive", "Drive", "DRIVE", 0.3f },
                      { "tone", "Tone", "TONE", 0.0f, -1.0f, 1.0f },
                      { "human", "Human", "HUMAN", 0.2f },
                      { "level", "Volume", "VOL", 0.0f, -60.0f, 6.0f, 3.02f, "dB" },
                      // (added after: the earlier ones keep their places)
                      { "slide", "Slide", "SLIDE", 0.0f },   // MONO: 0 every note plucked, up: overlapping notes slide
                      { "ghost", "Ghost", "GHOST", 0.5f },   // the velocity under which a note is a ghost: 0 .. 0.3 (0.15)
                      { "vel", "Velocity", "VEL", 1.0f },    // how much the velocity counts
                      { "release", "Release", "RELEASE", 0.45f }, // the damping at note-off: 0.02 .. 0.6 s
                      { "sub", "Sub", "SUB", 0.5f },         // the sub sine: 0 .. x2 of FUNK's
                      { "bend", "Bend range", "BEND", 2.0f, 1.0f, 12.0f, 1.0f, "st" },
                      { "octave", "Octave", "OCTAVE", 0.0f, -2.0f, 2.0f, 1.0f, "oct" } };
        cfg.choices = { { "mode", "Mode", "MODE", { "Mono", "Poly" }, 0, true },
                        { "engine", "Engine", "ENGINE", { "String", "Analog", "FM", "Pulse" }, 0, true } };
        cfg.editorBuildsControls = false;
        cfg.presetSetsParameters = true; // a preset sets FUNK, ENGINE and the knobs (auval: a meta parameter)
        cfg.engineAlwaysRuns = true;   // the strings ring at any knob position
        cfg.featuresFromOutput = true; // no input: the UI follows the output
        cfg.tailSeconds = 5.0;         // a low E rings ~5 s
        return cfg;
    }
} // namespace

/** Applies a preset's FUNK and knobs when the preset changes (the preset
    parameter may move on the audio thread: deferred to the message thread). */
class SlapperProcessor::PresetWatcher final : private juce::AudioProcessorValueTreeState::Listener, private juce::AsyncUpdater
{
public:
    explicit PresetWatcher (SlapperProcessor& p) : proc (p) { proc.getValueTreeState().addParameterListener ("preset", this); }
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
    SlapperProcessor& proc;
};

gk::PresetManager SlapperProcessor::loadBuiltInPresets()
{
    struct Entry { const char* data; int size; };
    const Entry entries[] = {
        { SLPresets::thumb_party_json, SLPresets::thumb_party_jsonSize }, // the default
        { SLPresets::pop_shop_json, SLPresets::pop_shop_jsonSize },
        { SLPresets::finger_round_json, SLPresets::finger_round_jsonSize },
        { SLPresets::ghost_groove_json, SLPresets::ghost_groove_jsonSize },
        { SLPresets::dusty_wood_json, SLPresets::dusty_wood_jsonSize },
        { SLPresets::disco_octaves_json, SLPresets::disco_octaves_jsonSize },
        { SLPresets::soul_mute_json, SLPresets::soul_mute_jsonSize },
        { SLPresets::slap_attack_json, SLPresets::slap_attack_jsonSize },
        { SLPresets::mothership_json, SLPresets::mothership_jsonSize },
        { SLPresets::rubber_band_json, SLPresets::rubber_band_jsonSize },
        { SLPresets::floor_pedal_json, SLPresets::floor_pedal_jsonSize },
        { SLPresets::acid_funk_json, SLPresets::acid_funk_jsonSize },
        { SLPresets::velvet_sub_json, SLPresets::velvet_sub_jsonSize },
        { SLPresets::square_strut_json, SLPresets::square_strut_jsonSize },
        { SLPresets::solid_fm_json, SLPresets::solid_fm_jsonSize },
        { SLPresets::fm_slap_json, SLPresets::fm_slap_jsonSize },
        { SLPresets::glass_bass_json, SLPresets::glass_bass_jsonSize },
        { SLPresets::bell_bottom_json, SLPresets::bell_bottom_jsonSize },
        { SLPresets::wood_block_json, SLPresets::wood_block_jsonSize },
        { SLPresets::boogie_pulse_json, SLPresets::boogie_pulse_jsonSize },
        { SLPresets::mirrorball_json, SLPresets::mirrorball_jsonSize },
        { SLPresets::roller_disco_json, SLPresets::roller_disco_jsonSize },
        { SLPresets::night_drive_json, SLPresets::night_drive_jsonSize },
        { SLPresets::hot_wire_json, SLPresets::hot_wire_jsonSize },
    };
    gk::PresetManager pm;
    for (const auto& e : entries)
    {
        const auto r = pm.add (juce::String::fromUTF8 (e.data, e.size));
        jassertquiet (r.wasOk());
    }
    return pm;
}

SlapperProcessor::SlapperProcessor() : SlapperProcessor (loadBuiltInPresets()) {}

SlapperProcessor::SlapperProcessor (gk::PresetManager presets)
    : OneKnobProcessor (makeConfig (std::move (presets)), std::make_unique<SlapperDsp>())
{
    applyPreset (0);
    watcher = std::make_unique<PresetWatcher> (*this);
}

SlapperProcessor::~SlapperProcessor() = default;

void SlapperProcessor::applyPreset (int index)
{
    if (loadingState || index == appliedPreset || index < 0 || index >= getPresets().size())
        return;
    appliedPreset = index;
    const auto& json = getPresets()[index].json;
    auto set = [] (juce::RangedAudioParameter& p, float plain)
    {
        p.beginChangeGesture();
        p.setValueNotifyingHost (p.convertTo0to1 (plain));
        p.endChangeGesture();
    };
    set (getAmountParameter(), (float) json.getProperty ("funk", 0.5));
    set (getEngineParameter(), (float) SlapperDsp::engineIndexOf (json));
    const auto params = json.getProperty ("params", {});
    const auto& knobs = getConfig().knobs;
    for (int k = 0; k < (int) knobs.size(); ++k)
        if (knobs[(size_t) k].id != "level") // (the volume is the player's, not the preset's)
            set (getKnobParameter (k), (float) params.getProperty (knobs[(size_t) k].id, knobs[(size_t) k].defaultValue));
}

void SlapperProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    // the saved FUNK and knobs win over the preset's: the preset restored with
    // the state is marked as applied, its change notification does nothing
    loadingState = true;
    OneKnobProcessor::setStateInformation (data, sizeInBytes);
    loadingState = false;
    appliedPreset = getPresetParameter().getIndex();
}

juce::AudioProcessorEditor* SlapperProcessor::createEditor()
{
    return new SlapperEditor (*this);
}
} // namespace slapper
