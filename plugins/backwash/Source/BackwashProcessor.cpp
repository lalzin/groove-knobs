#include "BackwashProcessor.h"
#include "BackwashEditor.h"
#include <BackwashPresets.h>

namespace bw
{
namespace
{
    gk::OneKnobConfig makeConfig (gk::PresetManager presets)
    {
        gk::OneKnobConfig cfg;
        cfg.productName = BW_PRODUCT_NAME;
        cfg.suiteName = GK_SUITE_NAME;
        cfg.companyName = GK_COMPANY_NAME;
        cfg.presets = std::move (presets);
        cfg.toggles = { { "long", "Long", "LONG" }, { "shimmer", "Shimmer", "SHIMMER" } };
        cfg.knobLowLabel = "CALM";
        cfg.knobHighLabel = "TSUNAMI";
        cfg.engineAlwaysRuns = true;   // 2 s of history must always be there
        cfg.featuresFromOutput = true; // the UI follows what is heard, not what comes in
        cfg.tailSeconds = 0.1;
        return cfg;
    }
} // namespace

gk::PresetManager BackwashProcessor::loadBuiltInPresets()
{
    struct Entry { const char* data; int size; };
    const Entry entries[] = {
        { BWPresets::snare_swell_json, BWPresets::snare_swell_jsonSize },
        { BWPresets::vocal_suck_json, BWPresets::vocal_suck_jsonSize },
        { BWPresets::clap_rewind_json, BWPresets::clap_rewind_jsonSize },
        { BWPresets::ghost_pad_json, BWPresets::ghost_pad_jsonSize },
        { BWPresets::big_inhale_json, BWPresets::big_inhale_jsonSize },
    };
    gk::PresetManager pm;
    for (const auto& e : entries)
    {
        const auto r = pm.add (juce::String::fromUTF8 (e.data, e.size));
        jassertquiet (r.wasOk());
    }
    return pm;
}

BackwashProcessor::BackwashProcessor() : BackwashProcessor (loadBuiltInPresets()) {}

BackwashProcessor::BackwashProcessor (gk::PresetManager presets)
    : OneKnobProcessor (makeConfig (std::move (presets)), std::make_unique<BackwashDsp>())
{
}

juce::AudioProcessorEditor* BackwashProcessor::createEditor()
{
    return new BackwashEditor (*this);
}
} // namespace bw
