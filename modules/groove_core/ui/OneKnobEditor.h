#pragma once

namespace gk
{
/** Generic Groove Knobs editor: header, mascot, big knob, preset bar and
    an on/off button, laid out on a 420 × 520 base canvas that scales from
    75 % to 200 % with a fixed ratio (corner resizer). A plugin provides its
    palette, title and mascot (frames + optional decorations). */
class OneKnobEditor : public juce::AudioProcessorEditor
{
public:
    static constexpr int baseWidth = 420, baseHeight = 520;
    static constexpr float minScale = 0.75f, maxScale = 2.0f;

    using MascotFactory = std::function<std::unique_ptr<MascotComponent> (AudioFeatures&, const Palette&)>;

    OneKnobEditor (OneKnobProcessor& processor, Palette palette, juce::String title, const MascotFactory& makeMascot);
    ~OneKnobEditor() override;

    void paint (juce::Graphics&) override {}
    void resized() override;

    /** Brings every animation to the state implied by the parameters, with no
        motion (deterministic screenshots). */
    void snapForSnapshot();

    BigKnob& getKnob() noexcept { return *knob; }
    PresetBar& getPresetBar() noexcept { return *presetBar; }
    MascotComponent& getMascot() noexcept { return *mascot; }
    Palette& getPalette() noexcept { return palette; }

private:
    class Canvas;
    class PowerButton;

    void tick (double timestampSeconds);
    void updateFromParameters (double dt, bool snap);

    OneKnobProcessor& proc;
    Palette palette;
    GrooveLookAndFeel lookAndFeel;
    juce::String title;

    std::unique_ptr<Canvas> canvas;
    std::unique_ptr<MascotComponent> mascot;
    std::unique_ptr<BigKnob> knob;
    std::unique_ptr<PresetBar> presetBar;
    std::unique_ptr<PresetTags> presetTags;
    int shownPreset = -1, shownSide = -2;
    std::unique_ptr<PowerButton> power;
    std::unique_ptr<juce::ButtonParameterAttachment> powerAttachment;

    SmoothParam uiAmount, uiBypass;
    double lastTimestamp = -1.0;
    float lastSaturation = 1.0f;
    juce::VBlankAttachment vblank;
};
} // namespace gk
