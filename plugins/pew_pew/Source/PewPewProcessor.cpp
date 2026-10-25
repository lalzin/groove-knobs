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
                      { "volume", "Volume", "VOL", 0.0f, -60.0f, 6.0f, 3.02f, "dB" },
                      // The "⋯" panel (PewPewDsp::PanelKnob order; published after the
                      // choices: the earlier parameters keep their places). Each default
                      // leaves the preset as it is.
                      { "tune", "Tune", "TUNE", 0.0f, -12.0f, 12.0f, 1.0f, "st" },
                      { "decay", "Decay", "DECAY", 0.5f },
                      { "glide", "Glide", "GLIDE", 0.5f },
                      { "detune", "Detune", "DETUNE", 0.5f },
                      { "drive", "Drive", "DRIVE", 0.5f },
                      { "click", "Click", "CLICK", 0.5f },
                      { "noise", "Noise", "NOISE", 0.0f },
                      { "tone", "Tone", "TONE", 0.5f },
                      { "wobble", "Wobble", "WOBBLE", 0.0f },
                      { "space", "Space", "SPACE", 0.5f },
                      { "feedback", "Feedback", "FEEDBACK", 0.5f },
                      { "width", "Width", "WIDTH", 0.5f },
                      { "vel", "Velocity", "VEL", 0.5f },
                      { "keytrack", "Key track", "KEY TRACK", 1.0f },
                      { "hits", "Roll hits", "HITS", 5.0f, 2.0f, 16.0f, 1.0f, "hits" },
                      { "rush", "Roll rush", "RUSH", 0.0f, -1.0f, 1.0f, 1.0f, "%" } };
        for (size_t k = PewPewDsp::tuneKnob; k < cfg.knobs.size(); ++k)
            cfg.knobs[k].late = true;
        // shortest to longest (saved by name: older sessions keep their value)
        // MODE: MULTI (default, up to 8 overlap) or SOLO (one pew at a time,
        // each note cuts the previous one); last, so the earlier parameters
        // keep their places; shown by the editor's own SOLO | MULTI switch
        cfg.choices = { { "delayDiv", "Echo division", "SYNC", { "1/16 T", "1/16", "1/8 T", "1/8", "1/8 D", "1/4" }, 4 },
                        { "mode", "Mode", "MODE", { "Multi", "Solo" }, 0, true },
                        // WAVE (the "⋯" panel): the preset's oscillator, or another
                        { "wave", "Wave", "WAVE", { "Preset", "Sine", "Triangle", "Square", "FM", "Metal" }, 0, true } };
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
        { PPPresets::simmons_tom_json, PPPresets::simmons_tom_jsonSize },
        { PPPresets::coin_8bit_json, PPPresets::coin_8bit_jsonSize },
        { PPPresets::ufo_wobble_json, PPPresets::ufo_wobble_jsonSize },
        { PPPresets::snare_zap_json, PPPresets::snare_zap_jsonSize },
        { PPPresets::zap_burst_json, PPPresets::zap_burst_jsonSize },
        { PPPresets::metal_ping_json, PPPresets::metal_ping_jsonSize },
        { PPPresets::reverse_riser_json, PPPresets::reverse_riser_jsonSize },
        { PPPresets::dub_siren_json, PPPresets::dub_siren_jsonSize },
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
