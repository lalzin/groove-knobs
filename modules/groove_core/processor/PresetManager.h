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

    /** Makeup (dB) for a signed macro value (-1..1, or 0..1 when unipolar). */
    float makeupDbAt (float macro) const noexcept
    {
        return macro < 0.0f ? makeupLow.dbAt (-macro) : makeup.dbAt (macro);
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
