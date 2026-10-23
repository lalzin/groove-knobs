#include "CrackleProcessor.h"
#include "CrackleEditor.h"
#include <CracklePresets.h>

namespace crackle
{
namespace
{
    const juce::StringArray typeNames { "Tube", "Transformer", "Speaker", "Tape", "Fold", "Hard" };

    gk::OneKnobConfig makeConfig (gk::PresetManager presets)
    {
        gk::OneKnobConfig cfg;
        cfg.productName = CR_PRODUCT_NAME;
        cfg.version = CR_VERSION_STRING;
        cfg.updateSlug = "crackle"; // key in lalzin.com/plugins/versions.json
        cfg.suiteName = GK_SUITE_NAME;
        cfg.companyName = GK_COMPANY_NAME;
        cfg.presets = std::move (presets);
        cfg.amountParamId = "grain";
        cfg.amountParamName = "Grain";
        cfg.knobLowLabel = "CLEAN";
        cfg.knobHighLabel = "GRAIN";
        // in CrackleDsp::Knob order; FOCUS on a log scale (1 kHz near the middle)
        const float hzSkew = 0.2f;
        cfg.knobs = { { "mix", "Mix", "MIX", 1.0f },
                      { "band", "Band", "BAND", 0.0f },
                      { "wow", "Wow", "WOW", 0.0f },
                      { "flutter", "Flutter", "FLUTTER", 0.0f },
                      { "hiss", "Hiss", "HISS", 0.0f },
                      { "saturation", "Saturation", "SAT", 0.0f },
                      { "vinyl", "Vinyl", "VINYL", 0.0f },
                      { "dropout", "Dropout", "DROPOUT", 0.0f },
                      { "crush", "Crush", "CRUSH", 0.0f },
                      { "radio", "Radio", "RADIO", 0.0f },
                      { "noise", "Noise", "NOISE", 0.5f },
                      { "focusLow", "Focus low", "FOCUS LO", 20.0f, 20.0f, 20000.0f, hzSkew, "Hz" },
                      { "focusHigh", "Focus high", "FOCUS HI", 20000.0f, 20.0f, 20000.0f, hzSkew, "Hz" },
                      { "level", "Volume", "VOL", 0.0f, -60.0f, 6.0f, 3.02f, "dB" } };
        // AUTO: each effect follows GRAIN until it is moved
        for (const auto* id : { "band", "wow", "flutter", "hiss", "saturation", "vinyl", "dropout", "crush", "radio" })
            cfg.switches.push_back ({ juce::String (id) + "Auto", juce::String (id).substring (0, 1).toUpperCase() + juce::String (id).substring (1) + " auto",
                                      "AUTO", true });
        cfg.choices = { { "type", "Saturation type", "TYPE", typeNames, (int) SatType::tape, true } };
        cfg.editorBuildsControls = false;
        cfg.presetSetsParameters = true; // a preset sets GRAIN, TYPE and the knobs (auval: a meta parameter)
        cfg.tailSeconds = 0.1;
        return cfg;
    }
} // namespace

/** Applies a preset when it changes, and turns an effect's AUTO off when its
    knob is moved (by hand or automation). Parameter callbacks may come from
    the audio thread: both are done on the message thread. */
class CrackleProcessor::Watcher final : private juce::AudioProcessorValueTreeState::Listener, private juce::AsyncUpdater
{
public:
    explicit Watcher (CrackleProcessor& p) : proc (p)
    {
        auto& s = proc.getValueTreeState();
        s.addParameterListener ("preset", this);
        for (int k = CrackleDsp::firstEffect; k < CrackleDsp::firstEffect + CrackleDsp::numEffects; ++k)
            s.addParameterListener (proc.getConfig().knobs[(size_t) k].id, this);
    }
    ~Watcher() override
    {
        auto& s = proc.getValueTreeState();
        s.removeParameterListener ("preset", this);
        for (int k = CrackleDsp::firstEffect; k < CrackleDsp::firstEffect + CrackleDsp::numEffects; ++k)
            s.removeParameterListener (proc.getConfig().knobs[(size_t) k].id, this);
        cancelPendingUpdate();
    }

private:
    void parameterChanged (const juce::String& id, float) override
    {
        if (id != "preset")
        {
            if (proc.applying || proc.loadingState)
                return;
            for (int k = CrackleDsp::firstEffect; k < CrackleDsp::firstEffect + CrackleDsp::numEffects; ++k)
                if (proc.getConfig().knobs[(size_t) k].id == id)
                    moved.fetch_or (1u << (k - CrackleDsp::firstEffect));
        }
        else
            presetMoved.store (true);
        if (juce::MessageManager::existsAndIsCurrentThread())
            handleAsyncUpdate();
        else
            triggerAsyncUpdate();
    }

    void handleAsyncUpdate() override
    {
        if (presetMoved.exchange (false))
            proc.applyPreset (proc.getPresetParameter().getIndex());
        const auto bits = moved.exchange (0u);
        for (int i = 0; i < CrackleDsp::numEffects; ++i)
            if ((bits >> i) & 1u)
            {
                auto& a = proc.getAutoParameter (CrackleDsp::firstEffect + i);
                if (a.get())
                {
                    a.beginChangeGesture();
                    a.setValueNotifyingHost (0.0f);
                    a.endChangeGesture();
                }
            }
    }

    CrackleProcessor& proc;
    std::atomic<std::uint32_t> moved { 0 };
    std::atomic<bool> presetMoved { false };
};

gk::PresetManager CrackleProcessor::loadBuiltInPresets()
{
    struct Entry { const char* data; int size; };
    const Entry entries[] = {
        { CRPresets::vinyl_sunday_json, CRPresets::vinyl_sunday_jsonSize }, // the default
        { CRPresets::cassette_dream_json, CRPresets::cassette_dream_jsonSize },
        { CRPresets::am_radio_ghost_json, CRPresets::am_radio_ghost_jsonSize },
        { CRPresets::basement_tape_json, CRPresets::basement_tape_jsonSize },
        { CRPresets::dusty_drum_loop_json, CRPresets::dusty_drum_loop_jsonSize },
        { CRPresets::walkman_bus_json, CRPresets::walkman_bus_jsonSize },
        { CRPresets::broken_sampler_json, CRPresets::broken_sampler_jsonSize },
        { CRPresets::sunday_kitchen_json, CRPresets::sunday_kitchen_jsonSize },
        { CRPresets::telephone_booth_json, CRPresets::telephone_booth_jsonSize },
        { CRPresets::crushed_dub_tape_json, CRPresets::crushed_dub_tape_jsonSize },
    };
    gk::PresetManager pm;
    for (const auto& e : entries)
    {
        const auto r = pm.add (juce::String::fromUTF8 (e.data, e.size));
        jassertquiet (r.wasOk());
    }
    return pm;
}

CrackleProcessor::CrackleProcessor() : CrackleProcessor (loadBuiltInPresets()) {}

CrackleProcessor::CrackleProcessor (gk::PresetManager presets)
    : OneKnobProcessor (makeConfig (std::move (presets)), std::make_unique<CrackleDsp>())
{
    applyPreset (0);
    watcher = std::make_unique<Watcher> (*this);
}

CrackleProcessor::~CrackleProcessor() = default;

void CrackleProcessor::applyPreset (int index)
{
    if (loadingState || index == appliedPreset || index < 0 || index >= getPresets().size())
        return;
    appliedPreset = index;
    const juce::ScopedValueSetter<bool> busy (applying, true);
    const auto& json = getPresets()[index].json;
    auto set = [] (juce::RangedAudioParameter& p, float plain)
    {
        p.beginChangeGesture();
        p.setValueNotifyingHost (p.convertTo0to1 (plain));
        p.endChangeGesture();
    };
    set (getAmountParameter(), (float) json.getProperty ("grain", 0.3));
    const int type = typeNames.indexOf (json.getProperty ("type", "tape").toString(), true);
    set (getTypeParameter(), (float) (type >= 0 ? type : (int) SatType::tape));
    const auto params = json.getProperty ("params", {});
    const auto& knobs = getConfig().knobs;
    for (int k = 0; k < (int) knobs.size(); ++k)
    {
        const auto& id = knobs[(size_t) k].id;
        if (id == "level")
            continue; // (the volume is the player's, not the preset's)
        const bool named = params.hasProperty (id);
        set (getKnobParameter (k), named ? (float) params.getProperty (id, 0.0) : knobs[(size_t) k].defaultValue);
        if (isEffectKnob (k))
            set (getAutoParameter (k), named ? 0.0f : 1.0f); // named: its own; the others follow GRAIN
    }
}

void CrackleProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    // the saved GRAIN, knobs and AUTOs win over the preset's
    loadingState = true;
    OneKnobProcessor::setStateInformation (data, sizeInBytes);
    loadingState = false;
    appliedPreset = getPresetParameter().getIndex();
}

juce::AudioProcessorEditor* CrackleProcessor::createEditor()
{
    return new CrackleEditor (*this);
}
} // namespace crackle
