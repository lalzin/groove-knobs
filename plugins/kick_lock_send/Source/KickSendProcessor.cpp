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
        // the big knob: PUNCH, a transient shaper on the kick (centred = untouched)
        cfg.amountParamId = "punch";
        cfg.amountParamName = "Punch";
        cfg.bipolar = true;
        cfg.lowSideLabel = "SOFT";
        cfg.highSideLabel = "PUNCH";
        cfg.knobs = { { "sustain", "Sustain", "SUSTAIN", 0.0f, -1.0f, 1.0f },
                      { "sensitivity", "Sensitivity", "SENS", 0.5f } }; // the kick detector (what is sent to Kick Lock)
        // LINK 0..10: the Kick Locks set to the same number receive this track's kicks
        cfg.choices = { { "channel", "Link channel", "LINK", { "0", "1", "2", "3", "4", "5", "6", "7", "8", "9", "10" }, 1 } };
        cfg.transparencyZone = 0.0f;
        cfg.alwaysWet = true;          // SUSTAIN acts with PUNCH at the centre; the engine is transparent at rest
        cfg.wetValidAtOnce = true;      // no warm-up after a host's reset (the first beat at play)
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
