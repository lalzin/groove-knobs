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
        cfg.suiteName = GK_SUITE_NAME;
        cfg.companyName = GK_COMPANY_NAME;
        cfg.presets = std::move (presets);
        cfg.isInstrument = true;
        cfg.defaultAmount = 0.6f;
        cfg.knobs = { { "reverb", "Room", "ROOM", 0.0f }, { "delay", "Echo", "ECHO", 0.0f } };
        cfg.choices = { { "delayDiv", "Echo division", "ECHO", { "1/16", "1/8 D", "1/8", "1/4" }, 1 } };
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
        { PPPresets::disco_tom_json, PPPresets::disco_tom_jsonSize },
        { PPPresets::laser_pew_json, PPPresets::laser_pew_jsonSize },
        { PPPresets::robo_zap_json, PPPresets::robo_zap_jsonSize },
        { PPPresets::space_toms_json, PPPresets::space_toms_jsonSize },
        { PPPresets::deep_drop_json, PPPresets::deep_drop_jsonSize },
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
