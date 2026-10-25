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
        cfg.tailSeconds = 0.4; // longest delay tap, the SPACE room
        // The "⋯" panel (BoogieTrioEditor), in BoogieTrioDsp::Knob order;
        // every default is neutral (the presets' makeup stays calibrated)
        cfg.knobs = { { "rate", "Rate", "RATE", 0.5f },
                      { "depth", "Depth", "DEPTH", 0.5f },
                      { "vibrato", "Vibrato", "VIBRATO", 0.5f },
                      { "drift", "Drift", "DRIFT", 0.0f },
                      { "tone", "Tone", "TONE", 0.5f },
                      { "lowcut", "Low cut", "LOW CUT", 0.5f },
                      { "feedback", "Feedback", "FEEDBACK", 0.0f },
                      { "space", "Space", "SPACE", 0.0f },
                      { "tremolo", "Tremolo", "TREMOLO", 0.0f },
                      { "phaser", "Phaser", "PHASER", 0.0f },
                      { "mix", "Mix", "MIX", 0.5f },
                      { "width", "Width", "WIDTH", 0.5f } };
        cfg.editorBuildsControls = false; // the editor's "⋯" panel shows them
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
        { BTPresets::tape_flutter_json, BTPresets::tape_flutter_jsonSize },
        { BTPresets::leslie_spin_json, BTPresets::leslie_spin_jsonSize },
        { BTPresets::glass_flanger_json, BTPresets::glass_flanger_jsonSize },
        { BTPresets::autopan_groove_json, BTPresets::autopan_groove_jsonSize },
        { BTPresets::dream_hall_json, BTPresets::dream_hall_jsonSize },
        { BTPresets::chop_shop_json, BTPresets::chop_shop_jsonSize },
        { BTPresets::dark_ensemble_json, BTPresets::dark_ensemble_jsonSize },
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
