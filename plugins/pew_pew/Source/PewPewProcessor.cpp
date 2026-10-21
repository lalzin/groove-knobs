#include "PewPewProcessor.h"
#include "PewPewEditor.h"
#include <PewPewPresets.h>

namespace pp
{
namespace
{
    gk::OneKnobConfig makeConfig (gk::PresetManager presets)
    {
        gk::OneKnobConfig cfg;
        cfg.productName = PP_PRODUCT_NAME;
        cfg.version = PP_VERSION_STRING;
        cfg.updateSlug = "pew-pew"; // key in lalzin.com/plugins/versions.json
        cfg.suiteName = GK_SUITE_NAME;
        cfg.companyName = GK_COMPANY_NAME;
        cfg.presets = std::move (presets);
        cfg.isInstrument = true;
        // UP: the glide rises; LONG: hold and decay doubled; ROLL: a note
        // becomes a tempo roll of sixteenths. Each preset gives its defaults.
        cfg.toggles = { { "up", "Up", "UP" }, { "long", "Long", "LONG" }, { "roll", "Roll", "ROLL" } };
        cfg.defaultAmount = 0.6f;
        // VOL: an instrument has no dry signal to balance, it needs a level
        // (-inf .. +6 dB, 0 dB at three quarters of the travel)
        cfg.knobs = { { "reverb", "Room", "ROOM", 0.0f },
                      { "delay", "Echo", "ECHO", 0.0f },
                      { "volume", "Volume", "VOL", 0.0f, -60.0f, 6.0f, 3.02f, "dB" } };
        // shortest to longest (saved by name: older sessions keep their value)
        // MODE: MULTI (default, up to 8 overlap) or SOLO (one pew at a time,
        // each note cuts the previous one); last, so the earlier parameters
        // keep their places; shown by the editor's own SOLO | MULTI switch
        cfg.choices = { { "delayDiv", "Echo division", "SYNC", { "1/16 T", "1/16", "1/8 T", "1/8", "1/8 D", "1/4" }, 4 },
                        { "mode", "Mode", "MODE", { "Multi", "Solo" }, 0, true } };
        cfg.knobLowLabel = "PLAIN";
        cfg.knobHighLabel = "PEW";
        cfg.engineAlwaysRuns = true;   // voices and tails keep ringing at any knob position
        cfg.featuresFromOutput = true; // no input: the UI follows the output
        cfg.tailSeconds = 3.0;         // reverb tail
        return cfg;
    }
} // namespace

gk::PresetManager PewPewProcessor::loadBuiltInPresets()
{
    struct Entry { const char* data; int size; };
    const Entry entries[] = {
        { PPPresets::laser_pew_json, PPPresets::laser_pew_jsonSize }, // the reference (DECISIONS.md), default
        { PPPresets::disco_tom_json, PPPresets::disco_tom_jsonSize },
        { PPPresets::robo_zap_json, PPPresets::robo_zap_jsonSize },
        { PPPresets::space_toms_json, PPPresets::space_toms_jsonSize },
        { PPPresets::deep_drop_json, PPPresets::deep_drop_jsonSize },
        { PPPresets::syndrum_76_json, PPPresets::syndrum_76_jsonSize },
        { PPPresets::bubble_pop_json, PPPresets::bubble_pop_jsonSize },
        { PPPresets::arcade_zap_json, PPPresets::arcade_zap_jsonSize },
        { PPPresets::disco_cowbell_json, PPPresets::disco_cowbell_jsonSize },
        { PPPresets::siren_sweep_json, PPPresets::siren_sweep_jsonSize },
    };
    gk::PresetManager pm;
    for (const auto& e : entries)
    {
        const auto r = pm.add (juce::String::fromUTF8 (e.data, e.size));
        jassertquiet (r.wasOk());
    }
    return pm;
}

PewPewProcessor::PewPewProcessor() : PewPewProcessor (loadBuiltInPresets()) {}

PewPewProcessor::PewPewProcessor (gk::PresetManager presets)
    : OneKnobProcessor (makeConfig (std::move (presets)), std::make_unique<PewPewDsp>())
{
}

juce::AudioProcessorEditor* PewPewProcessor::createEditor()
{
    return new PewPewEditor (*this);
}
} // namespace pp
