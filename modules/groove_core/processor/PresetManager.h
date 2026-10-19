#pragma once

namespace gk
{
/** Makeup gain table: 101 values in dB for amount = 0, 0.01, ... 1,
    linearly interpolated. Produced offline by the calibration tool. */
class MakeupTable
{
public:
    static constexpr int size = 101;

    MakeupTable() { values.fill (0.0f); }

    float dbAt (float amount) const noexcept
    {
        const float x = dspmath::clamp01 (amount) * (float) (size - 1);
        const int i = juce::jmin ((int) x, size - 2);
        const float f = x - (float) i;
        return values[(size_t) i] + f * (values[(size_t) i + 1] - values[(size_t) i]);
    }

    std::array<float, size> values;
};

/** One preset: a name, the plugin-specific JSON (kept as a juce::var so the
    core stays generic) and the calibrated makeup table(s).

    Unipolar plugins use one table ("makeupDb", amount 0..1). Bipolar plugins
    (knob centred, two sides) use one table per side, indexed by the distance
    from the centre: "makeupLowDb" (left) and "makeupHighDb" (right). */
struct PresetData
{
    juce::String name;
    juce::var json;
    MakeupTable makeup;     // unipolar, or the high (right) side
    MakeupTable makeupLow;  // bipolar only: the low (left) side
    bool hasMakeup = false, bipolar = false;

    /** Plugins with on/off toggles (unipolar): one table per toggle
        combination, "makeupDb@<mask>" (bit 0 = first toggle). `makeup` is
        mask 0; variants[mask - 1] the others. Up to 3 toggles; the makeup
        follows the first two (instruments have no makeup). */
    static constexpr int maxToggles = 3;
    std::array<MakeupTable, (1 << maxToggles) - 1> variants;
    std::array<bool, (1 << maxToggles) - 1> hasVariant {};

    const MakeupTable& tableForMask (int mask) const noexcept
    {
        return mask > 0 && hasVariant[(size_t) mask - 1] ? variants[(size_t) mask - 1] : makeup;
    }

    /** Makeup (dB) for a signed macro value (-1..1, or 0..1 when unipolar). */
    float makeupDbAt (float macro) const noexcept
    {
        return macro < 0.0f ? makeupLow.dbAt (-macro) : makeup.dbAt (macro);
    }

    /** Makeup (dB) with toggles (0..1 each, fading): bilinear between the
        tables of the neighbouring combinations. */
    float makeupDbAt (float macro, const float* toggles, int numToggles) const noexcept
    {
        if (numToggles <= 0 || bipolar)
            return makeupDbAt (macro);
        const float t0 = toggles[0], t1 = numToggles > 1 ? toggles[1] : 0.0f;
        const float a = tableForMask (0).dbAt (macro), b = tableForMask (1).dbAt (macro);
        const float lo = a + t0 * (b - a);
        if (numToggles == 1)
            return lo;
        const float c = tableForMask (2).dbAt (macro), d = tableForMask (3).dbAt (macro);
        const float hi = c + t0 * (d - c);
        return lo + t1 * (hi - lo);
    }
};

/** Loads presets from JSON text (BinaryData or files), off the audio thread. */
class PresetManager
{
public:
    /** Parses one preset. Returns an error for invalid JSON or a missing name. */
    static juce::Result parse (const juce::String& jsonText, PresetData& out);

    /** Serialises a preset back to JSON text (used by the calibration tool). */
    static juce::String toJson (const PresetData& preset);

    juce::Result add (const juce::String& jsonText);
    void add (PresetData preset) { presets.push_back (std::move (preset)); }

    int size() const noexcept { return (int) presets.size(); }
    const PresetData& operator[] (int index) const { return presets[(size_t) juce::jlimit (0, size() - 1, index)]; }
    int indexOf (const juce::String& name) const;
    juce::StringArray getNames() const;

private:
    std::vector<PresetData> presets;
};
} // namespace gk
