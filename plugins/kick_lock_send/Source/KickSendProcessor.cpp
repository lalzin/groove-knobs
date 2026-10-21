#include "KickSendProcessor.h"
#include "KickSendEditor.h"
#include <KickLockSendPresets.h>

namespace kls
{
namespace
{
    gk::OneKnobConfig makeConfig()
    {
        gk::OneKnobConfig cfg;
        cfg.productName = KLS_PRODUCT_NAME;
        cfg.version = KLS_VERSION_STRING;
        cfg.updateSlug = "kick-lock"; // ships with Kick Lock
        cfg.suiteName = GK_SUITE_NAME;
        cfg.companyName = GK_COMPANY_NAME;
        gk::PresetManager pm;
        const auto added = pm.add (juce::String::fromUTF8 (KLSPresets::default_json, KLSPresets::default_jsonSize));
        jassertquiet (added.wasOk());
        cfg.presets = std::move (pm);
        cfg.hasPresetParameter = false;
        cfg.amountParamId = "sensitivity";
        cfg.amountParamName = "Sensitivity";
        cfg.defaultAmount = 0.5f;
        cfg.knobLowLabel = "LOW";
        cfg.knobHighLabel = "HIGH";
        cfg.transparencyZone = 0.0f;   // the output is the (delayed) kick whatever the knob
        cfg.engineAlwaysRuns = true;   // the detector never stops
        cfg.tailSeconds = 0.0;
        return cfg;
    }
} // namespace

KickSendProcessor::KickSendProcessor() : OneKnobProcessor (makeConfig(), std::make_unique<KickSendDsp>())
{
    getDsp().getSender().setName (trackName);
}

void KickSendProcessor::updateTrackProperties (const TrackProperties& properties)
{
    if (properties.name.has_value() && properties.name->isNotEmpty())
    {
        trackName = *properties.name;
        getDsp().getSender().setName (trackName);
    }
}

juce::AudioProcessorEditor* KickSendProcessor::createEditor()
{
    return new KickSendEditor (*this);
}
} // namespace kls
