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
    ToggleChips* getToggleChips() noexcept { return toggleChips.get(); }
    ToggleChips* getSwitchChips() noexcept { return switchChips.get(); }
    /** Adds a plugin-specific component to the base canvas (420 x 520
        coordinates), e.g. a bottom bar replacing the preset bar. */
    void addToCanvas (juce::Component& c, juce::Rectangle<int> bounds);
    SmallKnob* getSmallKnob (int index) noexcept { return juce::isPositiveAndBelow (index, (int) smallKnobs.size()) ? smallKnobs[(size_t) index].get() : nullptr; }
    PresetBar& getPresetBar() noexcept { return *presetBar; }
    MascotComponent& getMascot() noexcept { return *mascot; }
    Palette& getPalette() noexcept { return palette; }

protected:
    /** Called every frame (and on snapshots) after the generic animation, for
        plugin-specific UI updates. */
    virtual void editorTick (double dt) { juce::ignoreUnused (dt); }

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
    std::unique_ptr<ToggleChips> toggleChips, switchChips;
    std::unique_ptr<ChoiceChips> choiceChips;
    std::vector<std::unique_ptr<SmallKnob>> smallKnobs;
    class CreditLink;
    std::unique_ptr<CreditLink> creditLink;
    int shownPreset = -1, shownSide = -2;
    std::unique_ptr<PowerButton> power;
    std::unique_ptr<juce::ButtonParameterAttachment> powerAttachment;

    SmoothParam uiAmount, uiBypass;
    double lastTimestamp = -1.0;
    float lastSaturation = 1.0f;
    juce::VBlankAttachment vblank;
};
} // namespace gk
