#include "SingItBackProcessor.h"
#include "SingItBackEditor.h"
#include <SingItBackPresets.h>

namespace sib
{
namespace
{
    gk::OneKnobConfig makeConfig()
    {
        gk::OneKnobConfig cfg;
        cfg.productName = SIB_PRODUCT_NAME;
        cfg.suiteName = GK_SUITE_NAME;
        cfg.companyName = GK_COMPANY_NAME;
        // No presets: every setting is a parameter with a good default
        gk::PresetManager pm;
        const auto added = pm.add (juce::String::fromUTF8 (SIBPresets::default_json, SIBPresets::default_jsonSize));
        jassertquiet (added.wasOk());
        cfg.presets = std::move (pm);
        cfg.hasPresetParameter = false;
        cfg.amountParamId = "crowd";
        cfg.amountParamName = "Crowd";
        cfg.defaultAmount = 0.25f;
        cfg.knobLowLabel = "SOLO";
        cfg.knobHighLabel = "CROWD";
        cfg.switches = { { "snap", "Snap to scale", "SNAP", true } };
        cfg.knobs = { { "spread", "Spread", "SPREAD", 0.6f },
                      { "humanize", "Humanize", "HUMAN", 0.5f },
                      { "dropouts", "Dropouts", "DROPS", 0.3f },
                      { "room", "Room", "ROOM", 0.4f } };
        cfg.choices = { { "key", "Key", "KEY", { "C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B" }, 0 },
                        { "scale", "Scale", "SCALE", { "Majeure", "Mineure", "Dorienne", "Mixolydienne", "Penta majeure", "Penta mineure" }, 0 },
                        { "harmony", "Harmony", "HARMONY", { "Auto", "Octaves", "Tierces", "Quintes", "Quartes" }, 0 },
                        { "voices", "Voices", "VOICES", { "4", "8", "12" }, 1 },
                        { "roomSize", "Room size", "ROOM SIZE", { "Petite", "Club", "Stade" }, 1 },
                        { "style", "Style", "STYLE",
                          { "Club", "Gospel", "Stade", "Chorale", "Enfants", juce::String::fromUTF8 ("G\xc3\xa9" "ants"), "Robots" }, 0 },
                        { "response", juce::String::fromUTF8 ("R\xc3\xa9ponse"), juce::String::fromUTF8 ("R\xc3\x89PONSE"),
                          { "Ensemble", "1/2 mesure", "1 mesure" }, 0 } };
        cfg.editorBuildsControls = false; // the editor's own layout: scale bar, selectors, advanced panel
        cfg.engineAlwaysRuns = true;   // pitch tracking and the UI keep going at 0 %
        cfg.tailSeconds = 3.0;         // stadium reverb
        return cfg;
    }
} // namespace

SingItBackProcessor::SingItBackProcessor() : OneKnobProcessor (makeConfig(), std::make_unique<SingItBackDsp>()) {}

juce::AudioProcessorEditor* SingItBackProcessor::createEditor()
{
    return new SingItBackEditor (*this);
}
} // namespace sib
