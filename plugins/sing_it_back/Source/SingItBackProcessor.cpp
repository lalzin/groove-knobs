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
        cfg.version = SIB_VERSION_STRING;
        cfg.updateSlug = "sing-it-back"; // key in lalzin.com/plugins/versions.json
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
                      { "room", "Room", "ROOM", 0.4f },
                      // VOL: the output level, -inf .. +12 dB (0 dB at ~70 % of the travel)
                      { "volume", "Volume", "VOL", 0.0f, -60.0f, 12.0f, 1.96f, "dB" } };
        cfg.outputGainKnob = 4;
        // the voice panel (like a harmony engine): per voice, a level, a pan,
        // a size (formants: + smaller / brighter, - bigger / darker) and a part
        for (int k = 0; k < SingItBackDsp::maxVoices; ++k)
            cfg.knobs.push_back ({ "v" + juce::String (k + 1) + "Level", "Voice " + juce::String (k + 1) + " level", "LEVEL", 0.0f, -30.0f, 6.0f, 1.0f, "dB" });
        for (int k = 0; k < SingItBackDsp::maxVoices; ++k)
            cfg.knobs.push_back ({ "v" + juce::String (k + 1) + "Pan", "Voice " + juce::String (k + 1) + " pan", "PAN", SingItBackDsp::defaultPan (k), -1.0f, 1.0f });
        for (int k = 0; k < SingItBackDsp::maxVoices; ++k)
            cfg.knobs.push_back ({ "v" + juce::String (k + 1) + "Size", "Voice " + juce::String (k + 1) + " size", "SIZE", 0.0f, -0.3f, 0.3f });
        cfg.choices = { { "key", "Key", "KEY", { "C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B" }, 0 },
                        { "scale", "Scale", "SCALE", { "Majeure", "Mineure", "Dorienne", "Mixolydienne", "Penta majeure", "Penta mineure" }, 0 },
                        { "harmony", "Harmony", "HARMONY", { "Unisson", "Auto", "Octaves", "Tierces", "Quintes", "Quartes" }, 0 },
                        { "voices", "Voices", "VOICES", { "4", "8", "12" }, 1 },
                        { "roomSize", "Room size", "ROOM SIZE", { "Petite", "Club", "Stade" }, 1 },
                        { "style", "Style", "STYLE",
                          { "Club", "Gospel", "Stade", "Chorale", "Enfants", juce::String::fromUTF8 ("G\xc3\xa9" "ants"), "Robots" }, 0 },
                        { "response", juce::String::fromUTF8 ("R\xc3\xa9ponse"), juce::String::fromUTF8 ("R\xc3\x89PONSE"),
                          { "Ensemble", "1/2 mesure", "1 mesure" }, 0 } };
        for (int k = 0; k < SingItBackDsp::maxVoices; ++k)
            cfg.choices.push_back ({ "v" + juce::String (k + 1) + "Part", "Voice " + juce::String (k + 1) + " part", "PART", SingItBackDsp::partNames(), 0, true });
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
