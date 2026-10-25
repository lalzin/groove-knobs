#pragma once

#include "PewPewProcessor.h"

namespace pp
{
/** Pew Pew UI, "Mirror" theme: gold / cherry / indigo, the PEW knob as a drum
    skin seen from above (it lifts with the pitch), ROOM and ECHO knobs with
    the echo division, Tomi the disco drummer. "⋯" (left, above ROOM) opens
    the rest: WAVE, TOM (TUNE, DECAY, GLIDE, DETUNE), COLOUR (DRIVE, CLICK,
    NOISE, TONE), FX (WOBBLE, SPACE, FEEDBACK, WIDTH), PLAY (VEL, KEY TRACK,
    and the roll's HITS and RUSH). */
class PewPewEditor final : public gk::OneKnobEditor
{
public:
    explicit PewPewEditor (PewPewProcessor& p);
    ~PewPewEditor() override;
    static gk::Palette makePalette();

    /** Screenshots: shows a hit `secondsAgo` (-1 = live). */
    void setHitOverride (float secondsAgo);
    /** Screenshots: the settings panel open. */
    void setPanelOpen (bool open);

protected:
    void editorTick (double dt) override;

private:
    class ModeSwitch;
    class WaveSwitch;
    class MoreButton;
    class SettingsPanel;
    PewPewProcessor& proc;
    std::unique_ptr<ModeSwitch> mode;
    std::unique_ptr<SettingsPanel> panel;
    std::unique_ptr<MoreButton> more;
    const juce::Rectangle<int> closedButton { 18, 248, 46, 26 }, openButton { 16, 56, 46, 24 };
};
} // namespace pp
