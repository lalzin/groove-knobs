#include "FrenchFilterProcessor.h"
#include "FrenchFilterEditor.h"
#include <FrenchFilterPresets.h>

namespace ff
{
namespace
{
    gk::OneKnobConfig makeConfig (gk::PresetManager presets)
    {
        gk::OneKnobConfig cfg;
        cfg.productName = FF_PRODUCT_NAME;
        cfg.suiteName = GK_SUITE_NAME;
        cfg.companyName = GK_COMPANY_NAME;
        cfg.bipolar = true; // centre = neutral, left = low-pass side, right = high-pass side
        cfg.presets = std::move (presets);
        return cfg;
    }
} // namespace

gk::PresetManager FrenchFilterProcessor::loadBuiltInPresets()
{
    struct Entry { const char* data; int size; };
    const Entry entries[] = {
        { FFPresets::filtered_disco_json, FFPresets::filtered_disco_jsonSize },
        { FFPresets::sunrise_sweep_json, FFPresets::sunrise_sweep_jsonSize },
        { FFPresets::muffled_club_json, FFPresets::muffled_club_jsonSize },
        { FFPresets::acid_disco_json, FFPresets::acid_disco_jsonSize },
        { FFPresets::touch_98_json, FFPresets::touch_98_jsonSize },
        { FFPresets::robot_funk_json, FFPresets::robot_funk_jsonSize },
        { FFPresets::sounds_better_json, FFPresets::sounds_better_jsonSize },
        { FFPresets::lady_groove_json, FFPresets::lady_groove_jsonSize },
        { FFPresets::space_json, FFPresets::space_jsonSize },
    };

    gk::PresetManager pm;
    for (const auto& e : entries)
    {
        const auto r = pm.add (juce::String::fromUTF8 (e.data, e.size));
        jassertquiet (r.wasOk());
    }
    return pm;
}

FrenchFilterProcessor::FrenchFilterProcessor()
    : FrenchFilterProcessor (loadBuiltInPresets())
{
}

FrenchFilterProcessor::FrenchFilterProcessor (gk::PresetManager presets)
    : OneKnobProcessor (makeConfig (std::move (presets)), std::make_unique<FrenchFilterDsp>())
{
}

juce::AudioProcessorEditor* FrenchFilterProcessor::createEditor()
{
    return new FrenchFilterEditor (*this);
}
} // namespace ff
