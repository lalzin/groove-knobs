#include "BoogieTrioProcessor.h"
#include "BoogieTrioEditor.h"
#include <BoogieTrioPresets.h>

namespace bt
{
namespace
{
    gk::OneKnobConfig makeConfig (gk::PresetManager presets)
    {
        gk::OneKnobConfig cfg;
        cfg.productName = BT_PRODUCT_NAME;
        cfg.version = BT_VERSION_STRING;
        cfg.updateSlug = "boogie-trio"; // key in lalzin.com/plugins/versions.json
        cfg.suiteName = GK_SUITE_NAME;
        cfg.companyName = GK_COMPANY_NAME;
        cfg.presets = std::move (presets);
        cfg.toggles = { { "vintage", "Vintage", "VINTAGE" }, { "wide", "Wide", "WIDE" } };
        cfg.allowMonoToStereo = true;
        cfg.knobLowLabel = "SOLO";
        cfg.knobHighLabel = "TRIO";
        cfg.tailSeconds = 0.05; // longest delay tap
        return cfg;
    }
} // namespace

gk::PresetManager BoogieTrioProcessor::loadBuiltInPresets()
{
    struct Entry { const char* data; int size; };
    const Entry entries[] = {
        { BTPresets::disco_strings_json, BTPresets::disco_strings_jsonSize },
        { BTPresets::lush_pad_json, BTPresets::lush_pad_jsonSize },
        { BTPresets::warble_json, BTPresets::warble_jsonSize },
        { BTPresets::velvet_json, BTPresets::velvet_jsonSize },
        { BTPresets::funk_keys_json, BTPresets::funk_keys_jsonSize },
        { BTPresets::jet_set_json, BTPresets::jet_set_jsonSize },
        { BTPresets::disco_gate_json, BTPresets::disco_gate_jsonSize },
        { BTPresets::moon_phaser_json, BTPresets::moon_phaser_jsonSize },
    };
    gk::PresetManager pm;
    for (const auto& e : entries)
    {
        const auto r = pm.add (juce::String::fromUTF8 (e.data, e.size));
        jassertquiet (r.wasOk());
    }
    return pm;
}

BoogieTrioProcessor::BoogieTrioProcessor() : BoogieTrioProcessor (loadBuiltInPresets()) {}

BoogieTrioProcessor::BoogieTrioProcessor (gk::PresetManager presets)
    : OneKnobProcessor (makeConfig (std::move (presets)), std::make_unique<BoogieTrioDsp>())
{
}

juce::AudioProcessorEditor* BoogieTrioProcessor::createEditor()
{
    return new BoogieTrioEditor (*this);
}
} // namespace bt
