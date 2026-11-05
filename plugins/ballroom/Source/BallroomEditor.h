#pragma once

#include "BallroomProcessor.h"

namespace bl
{
/** BALLROOM, the Groove Knobs one-knob front (420 x 520, as French Filter):
    Mira on top, SPACE in the middle (a mirror ball's facets, champagne gold
    rim), the presets at the bottom; ROOM (bottom right) opens the panel with
    every setting in three tabs — MAIN (MIX, PREDELAY, DECAY, HIGH CUT, DEPTH,
    MODE, and the decay curves, animated), EARLY (SIZE, CROSS, MOD RATE, MOD
    DEPTH, SEND, DIFFUSION), LATE (SIZE, CROSS, MOD RATE, MOD DEPTH, BASS
    MULT / XOVER, HIGH MULT / XOVER, FREEZE). CUSTOM (bottom left) lights
    when a ROOM setting moved since SPACE last did. Night, silver, champagne,
    Mira's gold and navy. */
class BallroomEditor final : public gk::OneKnobEditor
{
public:
    explicit BallroomEditor (BallroomProcessor& p);
    ~BallroomEditor() override;

    static gk::Palette makePalette();

    /** Screenshots: the ROOM panel open on a tab (-1: closed). */
    void showRoom (int tab);
    /** Screenshots: runs the animations (the twinkles) for `seconds`. */
    void warmUp (double seconds);

protected:
    void editorTick (double dt) override;

private:
    class RoomButton;
    class CustomChip;
    class RoomPanel;
    class KnobGlints;
    BallroomProcessor& proc;
    std::unique_ptr<RoomPanel> panel;
    std::unique_ptr<RoomButton> room;
    std::unique_ptr<CustomChip> customChip;
    std::unique_ptr<KnobGlints> glints;
};
} // namespace bl
