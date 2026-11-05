#include "BallroomProcessor.h"
#include "BallroomEditor.h"
#include "SpaceMap.h"
#include <BallroomPresets.h>

namespace bl
{
namespace
{
    /** A log range: the middle of the knob at the geometric middle. */
    gk::KnobSpec logKnob (const char* id, const char* name, const char* caption, float def, float lo, float hi, const char* unit)
    {
        const float mid = std::sqrt (lo * hi);
        gk::KnobSpec k { id, name, caption, def, lo, hi, (float) (std::log (0.5) / std::log ((mid - lo) / (hi - lo))), unit };
        return k;
    }

    juce::String hz (float v) { return v >= 1000.0f ? juce::String (v / 1000.0f, v < 10000.0f ? 1 : 0) + " kHz" : juce::String (juce::roundToInt (v)) + " Hz"; }
    juce::String seconds (float v) { return v < 10.0f ? juce::String (v, 2) + " s" : juce::String (v, 1) + " s"; }
    juce::String times (float v) { return juce::String::fromUTF8 ("\xc3\x97") + juce::String (v, 2); }

    gk::OneKnobConfig makeConfig (gk::PresetManager presets)
    {
        gk::OneKnobConfig cfg;
        cfg.productName = BL_PRODUCT_NAME;
        cfg.version = BL_VERSION_STRING;
        cfg.updateSlug = "ballroom";
        cfg.suiteName = GK_SUITE_NAME;
        cfg.companyName = GK_COMPANY_NAME;
        cfg.presets = std::move (presets);
        cfg.amountParamId = "space";
        cfg.amountParamName = "Space";
        cfg.defaultAmount = 0.55f;
        cfg.knobLowLabel = "ROOM";
        cfg.knobHighLabel = "COSMOS";
        cfg.allowMonoToStereo = true;
        cfg.alwaysWet = true;          // SPACE 0 is still a small room: the reverb (its MIX) makes the dry / wet
        cfg.engineAlwaysRuns = true;   // the tail rings on
        cfg.presetSetsParameters = true;
        cfg.amountIsMeta = true;       // SPACE sets MIX, DECAY, SIZE, DEPTH, PREDELAY, the MOD DEPTHs (auval)
        cfg.editorBuildsControls = false;
        cfg.tailSeconds = 30.0;        // Afterparty: 20 s
        cfg.knobs = {
            { "mix", "Mix", "MIX", 0.30f },
            { "predelay", "Predelay", "PREDELAY", 10.0f, 0.0f, 500.0f, 0.35f, "ms" },
            logKnob ("decay", "Decay", "DECAY", 2.0f, 0.1f, 100.0f, "s"),
            logKnob ("highCut", "High cut", "HIGH CUT", 8000.0f, 500.0f, 20000.0f, "Hz"),
            { "depth", "Depth", "DEPTH", 0.60f },
            { "earlySize", "Early size", "SIZE", 40.0f, 2.0f, 200.0f, 0.5f, "ms" },
            { "earlyCross", "Early cross", "CROSS", 0.20f },
            logKnob ("earlyModRate", "Early mod rate", "MOD RATE", 0.4f, 0.05f, 5.0f, "Hz"),
            { "earlyModDepth", "Early mod depth", "MOD DEPTH", 0.15f },
            { "earlySend", "Early send", "SEND", 0.5f },
            { "diffusion", "Diffusion", "DIFFUSION", 0.8f },
            { "lateSize", "Late size", "SIZE", 0.60f },
            { "lateCross", "Late cross", "CROSS", 0.30f },
            logKnob ("lateModRate", "Late mod rate", "MOD RATE", 0.5f, 0.05f, 5.0f, "Hz"),
            { "lateModDepth", "Late mod depth", "MOD DEPTH", 0.25f },
            logKnob ("bassMult", "Bass mult", "BASS MULT", 1.2f, 0.25f, 4.0f, "x"),
            logKnob ("bassXover", "Bass crossover", "BASS XOVER", 500.0f, 100.0f, 2000.0f, "Hz"),
            logKnob ("highMult", "High mult", "HIGH MULT", 0.5f, 0.1f, 1.0f, "x"),
            logKnob ("highXover", "High crossover", "HIGH XOVER", 5000.0f, 1000.0f, 12000.0f, "Hz") };
        auto& k = cfg.knobs;
        for (int i : { BallroomDsp::highCutKnob, BallroomDsp::bassXoverKnob, BallroomDsp::highXoverKnob })
            k[(size_t) i].textFromValue = hz;
        k[BallroomDsp::decayKnob].textFromValue = seconds;
        for (int i : { BallroomDsp::bassMultKnob, BallroomDsp::highMultKnob })
            k[(size_t) i].textFromValue = times;
        for (int i : { BallroomDsp::earlyModRateKnob, BallroomDsp::lateModRateKnob })
            k[(size_t) i].textFromValue = [] (float v) { return juce::String (v, 2) + " Hz"; };
        k[BallroomDsp::earlySendKnob].textFromValue = [] (float v) { return juce::String (v, 2); };
        k[BallroomDsp::lateCrossKnob].textFromValue = [] (float v) { return juce::String (v, 2); };
        juce::StringArray modes;
        for (int m = 0; m < (int) ballroom::Mode::count; ++m)
            modes.add (ballroom::modeName ((ballroom::Mode) m));
        cfg.choices = { { "mode", "Mode", "MODE", modes, (int) ballroom::Mode::club, true },
                        { "freeze", "Freeze", "FREEZE", { "Off", "On" }, 0, true } };
        return cfg;
    }

    // the parameters SPACE sets (and the preset's own values may then change)
    constexpr int macroKnobs[] { BallroomDsp::mixKnob, BallroomDsp::decayKnob, BallroomDsp::lateSizeKnob, BallroomDsp::depthKnob,
                                 BallroomDsp::predelayKnob, BallroomDsp::lateModDepthKnob, BallroomDsp::earlyModDepthKnob };
} // namespace

gk::PresetManager BallroomProcessor::loadBuiltInPresets()
{
    struct Entry { const char* data; int size; };
    const Entry entries[] = {
        { BLPresets::disco_ballroom_json, BLPresets::disco_ballroom_jsonSize }, // the default: the disco reverb
        { BLPresets::tight_club_json, BLPresets::tight_club_jsonSize },
        { BLPresets::mirror_ball_json, BLPresets::mirror_ball_jsonSize },
        { BLPresets::vocal_plate_json, BLPresets::vocal_plate_jsonSize },
        { BLPresets::snare_chamber_json, BLPresets::snare_chamber_jsonSize },
        { BLPresets::warm_hall_json, BLPresets::warm_hall_jsonSize },
        { BLPresets::gated_80s_json, BLPresets::gated_80s_jsonSize },
        { BLPresets::velvet_pad_json, BLPresets::velvet_pad_jsonSize },
        { BLPresets::afterparty_json, BLPresets::afterparty_jsonSize },
        { BLPresets::frozen_floor_json, BLPresets::frozen_floor_jsonSize },
    };
    gk::PresetManager pm;
    for (const auto& e : entries)
    {
        const auto r = pm.add (juce::String::fromUTF8 (e.data, e.size));
        jassertquiet (r.wasOk());
    }
    return pm;
}

BallroomProcessor::BallroomProcessor() : BallroomProcessor (loadBuiltInPresets()) {}

BallroomProcessor::BallroomProcessor (gk::PresetManager presets) : OneKnobProcessor (makeConfig (std::move (presets)), std::make_unique<BallroomDsp>())
{
    updater.fn = [this] { handleUpdate(); };
    applyPreset (0);
    auto& vts = getValueTreeState();
    vts.addParameterListener ("space", this);
    vts.addParameterListener ("preset", this);
    for (const auto& k : getConfig().knobs)
        vts.addParameterListener (k.id, this);
    for (const auto& c : getConfig().choices)
        vts.addParameterListener (c.id, this);
}

BallroomProcessor::~BallroomProcessor()
{
    auto& vts = getValueTreeState();
    vts.removeParameterListener ("space", this);
    vts.removeParameterListener ("preset", this);
    for (const auto& k : getConfig().knobs)
        vts.removeParameterListener (k.id, this);
    for (const auto& c : getConfig().choices)
        vts.removeParameterListener (c.id, this);
    updater.cancelPendingUpdate();
}

juce::AudioProcessorEditor* BallroomProcessor::createEditor()
{
    return new BallroomEditor (*this);
}

void BallroomProcessor::set (juce::RangedAudioParameter& p, float plain)
{
    p.beginChangeGesture();
    p.setValueNotifyingHost (p.convertTo0to1 (plain));
    p.endChangeGesture();
}

void BallroomProcessor::parameterChanged (const juce::String& id, float)
{
    if (applying || loadingState)
        return;
    if (id == "space")
        macroPending = true;
    else if (id == "preset")
        presetPending = true;
    else
        custom = true; // a ROOM setting moved: out of the macro
    if (macroPending || presetPending)
    {
        if (juce::MessageManager::existsAndIsCurrentThread())
            handleUpdate();
        else
            updater.triggerAsyncUpdate();
    }
}

void BallroomProcessor::handleUpdate()
{
    if (presetPending.exchange (false))
    {
        macroPending = false;
        applyPreset (getPresetParameter().getIndex());
    }
    if (macroPending.exchange (false))
        applyMacro();
}

void BallroomProcessor::applyMacro()
{
    const juce::ScopedValueSetter<bool> guard (applying, true);
    const float s = getAmountParameter().convertFrom0to1 (getAmountParameter().getValue());
    ballroom::Params p = ballroom::space::apply ({}, s);
    const float values[] { (float) p.mix, (float) p.decaySeconds, (float) p.lateSize, (float) p.depth, (float) p.predelayMs, (float) p.lateModDepth,
                           (float) p.earlyModDepth };
    for (size_t i = 0; i < std::size (macroKnobs); ++i)
        set (getKnobParameter (macroKnobs[i]), values[i]);
    // then the preset's own values that SPACE also sets (e.g. Afterparty's 20 s)
    const int preset = appliedPreset;
    if (juce::isPositiveAndBelow (preset, getPresets().size()))
        if (const auto* o = getPresets()[preset].json.getProperty ("overrides", {}).getDynamicObject())
            for (const auto& prop : o->getProperties())
                for (int k : macroKnobs)
                    if (getConfig().knobs[(size_t) k].id == prop.name.toString())
                        set (getKnobParameter (k), (float) prop.value);
    custom = false;
}

void BallroomProcessor::applyPreset (int index)
{
    if (loadingState || index < 0 || index >= getPresets().size())
        return;
    const juce::ScopedValueSetter<bool> guard (applying, true);
    appliedPreset = index;
    if (getPresetParameter().getIndex() != index) // (called directly: the parameter follows)
        set (getPresetParameter(), (float) index);
    const auto& json = getPresets()[index].json;
    // every setting back to its default, then the preset
    const auto& knobs = getConfig().knobs;
    for (int k = 0; k < (int) knobs.size(); ++k)
        set (getKnobParameter (k), knobs[(size_t) k].defaultValue);
    const auto& modes = getConfig().choices[BallroomDsp::modeChoice].options;
    set (getChoice (BallroomDsp::modeChoice), (float) juce::jmax (0, modes.indexOf (json.getProperty ("mode", "Club").toString())));
    set (getChoice (BallroomDsp::freezeChoice), (bool) json.getProperty ("freeze", false) ? 1.0f : 0.0f);
    set (getAmountParameter(), (float) json.getProperty ("space", 0.5));
    applyMacro();
    if (const auto* o = json.getProperty ("overrides", {}).getDynamicObject())
        for (const auto& prop : o->getProperties())
            for (int k = 0; k < (int) knobs.size(); ++k)
                if (knobs[(size_t) k].id == prop.name.toString())
                    set (getKnobParameter (k), (float) prop.value);
    custom = false;
}

void BallroomProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    // the saved settings as they were (no macro, no preset applied over them)
    {
        const juce::ScopedValueSetter<bool> guard (loadingState, true);
        OneKnobProcessor::setStateInformation (data, sizeInBytes);
    }
    macroPending = false;
    presetPending = false;
    appliedPreset = getPresetParameter().getIndex();
}

void BallroomProcessor::saveExtraState (juce::ValueTree& state)
{
    state.setProperty ("custom", custom.load(), nullptr);
}

void BallroomProcessor::loadExtraState (const juce::ValueTree& state)
{
    custom = (bool) state.getProperty ("custom", false);
}
} // namespace bl
