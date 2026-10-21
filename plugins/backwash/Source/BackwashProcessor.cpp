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
        cfg.version = BW_VERSION_STRING;
        cfg.updateSlug = "backwash"; // key in lalzin.com/plugins/versions.json
        cfg.suiteName = GK_SUITE_NAME;
        cfg.companyName = GK_COMPANY_NAME;
        cfg.presets = std::move (presets);
        cfg.toggles = { { "long", "Long", "LONG" }, { "shimmer", "Shimmer", "SHIMMER" } };
        // LIVE: zero latency, the swells come from a memory of the last bar;
        // LOCK: freezes that memory. Global (a preset change keeps them).
        // On by default (Alan): zero latency, and the first bar heard is kept
        cfg.switches = { { "live", "Live (zero latency)", "LIVE", true }, { "lock", "Lock memory", "LOCK", true } };
        // Timing: swell length and which hits get one
        cfg.choices = { { "swell", "Swell length", "SWELL", { "PRESET", "1/4 BEAT", "1/2 BEAT", "1 BEAT", "2 BEATS", "1 BAR" }, 0 },
                        { "hits", "Swell on", "HITS", { "ALL", "1 2 3 4", "BEAT 1", "2 + 4", "LAST", "1 / 2 BARS" }, 0 } };
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
        { BWPresets::reverse_echo_json, BWPresets::reverse_echo_jsonSize },
        { BWPresets::stutter_suck_json, BWPresets::stutter_suck_jsonSize },
        { BWPresets::dusty_tape_json, BWPresets::dusty_tape_jsonSize },
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

void BackwashProcessor::saveExtraState (juce::ValueTree& state)
{
    // The LIVE memory (one bar of mono audio, 16-bit): the first hits after
    // reopening the set already have their swell.
    const auto memory = getBackwashDsp().copyMemory();
    if (memory.empty())
        return;
    juce::MemoryBlock block (memory.size() * sizeof (std::int16_t));
    auto* pcm = static_cast<std::int16_t*> (block.getData());
    for (size_t i = 0; i < memory.size(); ++i)
        pcm[i] = (std::int16_t) juce::roundToInt (juce::jlimit (-1.0f, 1.0f, memory[i]) * 32767.0f);
    state.setProperty ("liveMemory", block.toBase64Encoding(), nullptr);
}

void BackwashProcessor::loadExtraState (const juce::ValueTree& state)
{
    if (! state.hasProperty ("liveMemory"))
        return;
    juce::MemoryBlock block;
    if (! block.fromBase64Encoding (state.getProperty ("liveMemory").toString()) || block.getSize() < 2)
        return;
    const auto* pcm = static_cast<const std::int16_t*> (block.getData());
    std::vector<float> memory (block.getSize() / sizeof (std::int16_t));
    for (size_t i = 0; i < memory.size(); ++i)
        memory[i] = (float) pcm[i] / 32767.0f;
    getBackwashDsp().loadMemory (std::move (memory));
}

juce::AudioProcessorEditor* BackwashProcessor::createEditor()
{
    return new BackwashEditor (*this);
}
} // namespace bw
