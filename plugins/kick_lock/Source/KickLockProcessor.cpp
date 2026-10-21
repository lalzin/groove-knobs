#include "KickLockProcessor.h"
#include "KickLockEditor.h"
#include <KickLockPresets.h>

namespace kl
{
namespace
{
    gk::OneKnobConfig makeConfig()
    {
        gk::OneKnobConfig cfg;
        cfg.productName = KL_PRODUCT_NAME;
        cfg.version = KL_VERSION_STRING;
        cfg.updateSlug = "kick-lock"; // key in lalzin.com/plugins/versions.json
        cfg.suiteName = GK_SUITE_NAME;
        cfg.companyName = GK_COMPANY_NAME;
        gk::PresetManager pm;
        const auto added = pm.add (juce::String::fromUTF8 (KLPresets::default_json, KLPresets::default_jsonSize));
        jassertquiet (added.wasOk());
        cfg.presets = std::move (pm);
        cfg.hasPresetParameter = false;
        cfg.sidechain = true;              // the kick, routed from another track
        cfg.amountParamId = "mix";
        cfg.amountParamName = "Mix";
        cfg.defaultAmount = 0.6f;
        cfg.knobLowLabel = "DRY";
        cfg.knobHighLabel = "WET";
        // the engine does the dry / wet itself (MIX is that knob); the generic
        // crossfade only keeps 0 % bit-exact (the dry path)
        cfg.transparencyZone = 0.001f;
        cfg.switches = { { "lock", "Lock", "LOCK", false } };
        cfg.knobs = { { "targetPhase", "Target phase", "TARGET", 0.0f, 0.0f, 360.0f, 1.0f, juce::String::fromUTF8 ("\xc2\xb0") },
                      { "offsetMs", "Offset", "OFFSET", 0.0f, -20.0f, 20.0f, 1.0f, "ms" },
                      { "subCutoff", "Sub cutoff", "SUB CUT", 120.0f, 80.0f, 150.0f, 1.0f, "Hz" },
                      { "depth", "Depth", "DEPTH", 1.0f },
                      { "waveStart", "Wave start", "WAVE START", 0.0f, 0.0f, 360.0f, 1.0f, juce::String::fromUTF8 ("\xc2\xb0") },
                      { "kickThreshold", "Kick threshold", "THRESHOLD", -18.0f, -40.0f, 0.0f, 1.0f, "dBFS" } };
        cfg.choices = { { "shape", "Shape", "SHAPE", { "Exponential", "Linear", "S-curve", "Bounce", "Custom" }, 0 },
                        { "release", "Release", "RELEASE", { "1/16", "1/8 dotted", "1/8", "1/4", "1/2" }, 2 },
                        // Hold: the correction held from kick to kick; Transition: only
                        // around each kick, then back to the original bass
                        { "lockMode", "Lock mode", "MODE", { "Hold", "Transition" }, 0 },
                        // what triggers the ducking and the lock: the sidechain's kick, else
                        // (no kick for 2 s, or no sidechain at all) the host's beats
                        { "trigger", "Trigger", "TRIGGER", { "Auto", "Sidechain", "Beat" }, 0 } };
        cfg.editorBuildsControls = false; // KickLockEditor: its own 760 x 480 layout
        cfg.editorWidth = 760;
        cfg.editorHeight = 480;
        cfg.engineAlwaysRuns = true; // the kick detector and the phase tracker never stop
        cfg.tailSeconds = 0.1;
        return cfg;
    }
} // namespace

KickLockProcessor::KickLockProcessor() : OneKnobProcessor (makeConfig(), std::make_unique<KickLockDsp>())
{
    customCurve = KickLockDsp::presetCurve (KickLockDsp::exponential);
    getDsp().setCustomCurve (customCurve);
}

std::vector<KickLockDsp::CurveNode> KickLockProcessor::curveFor (int shape) const
{
    return shape == KickLockDsp::custom ? customCurve : KickLockDsp::presetCurve (shape);
}

void KickLockProcessor::setCustomCurve (std::vector<KickLockDsp::CurveNode> nodes)
{
    // sane: 2..32 points, x from 0 to 1 in order, gains 0..1, tensions ±12
    if (nodes.size() < 2)
        nodes = KickLockDsp::presetCurve (KickLockDsp::exponential);
    if (nodes.size() > (size_t) KickLockDsp::maxCurveNodes)
        nodes.resize ((size_t) KickLockDsp::maxCurveNodes);
    std::sort (nodes.begin(), nodes.end(), [] (const auto& a, const auto& b) { return a.x < b.x; });
    for (auto& n : nodes)
    {
        n.x = juce::jlimit (0.0f, 1.0f, n.x);
        n.y = juce::jlimit (0.0f, 1.0f, n.y);
        n.tension = juce::jlimit (-12.0f, 12.0f, n.tension);
    }
    nodes.front().x = 0.0f;
    nodes.back().x = 1.0f;
    customCurve = std::move (nodes);
    getDsp().setCustomCurve (customCurve);
    ++curveVersion;
}

void KickLockProcessor::saveExtraState (juce::ValueTree& state)
{
    juce::StringArray pts;
    for (const auto& n : customCurve)
        pts.add (juce::String (n.x, 4) + "," + juce::String (n.y, 4) + "," + juce::String (n.tension, 3));
    state.setProperty ("curve", pts.joinIntoString (";"), nullptr);
}

void KickLockProcessor::loadExtraState (const juce::ValueTree& state)
{
    std::vector<KickLockDsp::CurveNode> nodes;
    for (const auto& p : juce::StringArray::fromTokens (state.getProperty ("curve").toString(), ";", {}))
    {
        const auto v = juce::StringArray::fromTokens (p, ",", {});
        if (v.size() == 3)
            nodes.push_back ({ v[0].getFloatValue(), v[1].getFloatValue(), v[2].getFloatValue() });
    }
    setCustomCurve (nodes.size() >= 2 ? nodes : KickLockDsp::presetCurve (KickLockDsp::exponential));
}

juce::AudioProcessorEditor* KickLockProcessor::createEditor()
{
    return new KickLockEditor (*this);
}

void KickLockProcessor::resetAllToDefaults()
{
    for (auto* p : getParameters())
        if (auto* r = dynamic_cast<juce::RangedAudioParameter*> (p); r != nullptr && r != &getBypassParameterObject())
        {
            r->beginChangeGesture();
            r->setValueNotifyingHost (r->getDefaultValue());
            r->endChangeGesture();
        }
    setCustomCurve (KickLockDsp::presetCurve (KickLockDsp::exponential));
}
} // namespace kl
