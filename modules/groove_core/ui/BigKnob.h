#pragma once

namespace gk
{
/** The one big knob.

    Geometry (base coordinates, scaled by the editor): a 14 px progress ring
    from −135° to +135°, a body disc (palette.knobBands stripes) and a glossy
    "chicken-head" pointer cap. Shows the value in the centre while hovered or
    dragged.

    Interaction: vertical drag (250 px = full travel), Shift or Cmd/Ctrl for
    ×10 finer control, wheel (1 % per notch), double-click = rest position
    (0 %, or the centre when bipolar, with a small magnet there), arrow keys
    when focused, right-click = host context menu (MIDI learn, automation).
    Every edit is wrapped in begin/endChangeGesture via ParameterAttachment. */
class BigKnob : public juce::Component
{
public:
    static constexpr float dragPixelsForFullRange = 250.0f;
    static constexpr float startAngle = -0.75f * juce::MathConstants<float>::pi;
    static constexpr float endAngle = 0.75f * juce::MathConstants<float>::pi;

    /** @param bipolar  centred knob: the arc grows from the top towards
                        either side, double-click returns to the centre. */
    BigKnob (juce::RangedAudioParameter& parameter, const Palette& palette, bool bipolar = false,
             juce::String lowLabel = {}, juce::String highLabel = {});

    /** 0..1, what the knob currently shows. */
    float getValue() const noexcept { return value; }

    /** Musical position, used to pulse the excitement lines on the beat. */
    void setBeatPhase (float phase);

    /** A glow growing along the arc (0..1), e.g. a breath before a hit.
        Drawn in palette.pulse; 0 = none. */
    void setPulse (float amount);

    void paint (juce::Graphics&) override;
    void resized() override;
    bool hitTest (int x, int y) override;
    void mouseEnter (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    bool keyPressed (const juce::KeyPress&) override;
    void focusGained (FocusChangeType) override { repaint(); }
    void focusLost (FocusChangeType) override { repaint(); }
    std::unique_ptr<juce::AccessibilityHandler> createAccessibilityHandler() override;

    juce::RangedAudioParameter& getParameter() noexcept { return param; }

private:
    void setFromUser (float newValue);   // complete gesture
    void showContextMenu (const juce::MouseEvent&);
    void rebuildPaths();
    float angleFor (float v) const noexcept { return startAngle + v * (endAngle - startAngle); }
    float origin() const noexcept { return bipolar ? 0.5f : 0.0f; }
    /** 0 at rest, 1 at full travel (either side when bipolar). */
    float intensity() const noexcept { return bipolar ? std::abs (value - 0.5f) * 2.0f : value; }

    juce::RangedAudioParameter& param;
    const Palette& palette;
    const bool bipolar;
    const juce::String lowLabel, highLabel;
    juce::ParameterAttachment attachment;

    float value = 0.0f, dragStartValue = 0.0f, dragAnchorY = 0.0f, beatPhase = 0.0f, pulse = 0.0f;
    bool hovering = false, dragging = false, dragFine = false;

    // Pre-built geometry (rebuilt in resized(), never in paint())
    juce::Point<float> centre;
    float ringRadius = 0.0f, bodyRadius = 0.0f;
    juce::Path trackPath, capPath, capHighlight, bodyOutline;
    std::vector<juce::Path> bandPaths;
    std::array<juce::Path, 6> excitementLines;
    juce::Point<float> lowLabelPos, highLabelPos;
};
} // namespace gk
