#include "KickLockProcessor.h"
#include "KickLockEditor.h"
#include <KickLockPresets.h>

namespace kl
{
namespace
{
    gk::OneKnobConfig makeConfig()
    {
        gk::OneKnobConfig cfg;
        cfg.productName = KL_PRODUCT_NAME;
        cfg.version = KL_VERSION_STRING;
        cfg.updateSlug = "kick-lock"; // key in lalzin.com/plugins/versions.json
        cfg.suiteName = GK_SUITE_NAME;
        cfg.companyName = GK_COMPANY_NAME;
        gk::PresetManager pm;
        const auto added = pm.add (juce::String::fromUTF8 (KLPresets::default_json, KLPresets::default_jsonSize));
        jassertquiet (added.wasOk());
        cfg.presets = std::move (pm);
        cfg.hasPresetParameter = false;
        cfg.sidechain = true;              // the kick, routed from another track
        cfg.amountParamId = "mix";
        cfg.amountParamName = "Mix";
        cfg.defaultAmount = 0.6f;
        cfg.knobLowLabel = "DRY";
        cfg.knobHighLabel = "WET";
        // the engine does the dry / wet itself (MIX is that knob); the generic
        // crossfade only keeps 0 % bit-exact (the dry path)
        cfg.transparencyZone = 0.001f;
        cfg.switches = { { "lock", "Lock", "LOCK", false } };
        cfg.knobs = { { "targetPhase", "Target phase", "TARGET", 0.0f, 0.0f, 360.0f, 1.0f, juce::String::fromUTF8 ("\xc2\xb0") },
                      { "offsetMs", "Offset", "OFFSET", 0.0f, -20.0f, 20.0f, 1.0f, "ms" },
                      { "subCutoff", "Sub cutoff", "SUB CUT", 120.0f, 80.0f, 150.0f, 1.0f, "Hz" },
                      { "depth", "Depth", "DEPTH", 1.0f },
                      { "waveStart", "Wave start", "WAVE START", 0.0f, 0.0f, 360.0f, 1.0f, juce::String::fromUTF8 ("\xc2\xb0") },
                      { "kickThreshold", "Kick threshold", "THRESHOLD", -18.0f, -40.0f, 0.0f, 1.0f, "dBFS" } };
        cfg.choices = { { "shape", "Shape", "SHAPE", { "Exponentielle", juce::String::fromUTF8 ("Lin\xc3\xa9" "aire"), "En S", "Rebond" }, 0 },
                        { "release", "Release", "RELEASE", { "1/16", juce::String::fromUTF8 ("1/8 point\xc3\xa9"), "1/8", "1/4", "1/2" }, 2 } };
        cfg.engineAlwaysRuns = true; // the kick detector and the phase tracker never stop
        cfg.tailSeconds = 0.1;
        return cfg;
    }
} // namespace

KickLockProcessor::KickLockProcessor() : OneKnobProcessor (makeConfig(), std::make_unique<KickLockDsp>()) {}

juce::AudioProcessorEditor* KickLockProcessor::createEditor()
{
    return new KickLockEditor (*this);
}

void KickLockProcessor::resetAllToDefaults()
{
    for (auto* p : getParameters())
        if (auto* r = dynamic_cast<juce::RangedAudioParameter*> (p); r != nullptr && r != &getBypassParameterObject())
        {
            r->beginChangeGesture();
            r->setValueNotifyingHost (r->getDefaultValue());
            r->endChangeGesture();
        }
}
} // namespace kl
