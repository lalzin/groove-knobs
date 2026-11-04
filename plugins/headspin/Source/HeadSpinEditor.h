#pragma once

#include "HeadSpinProcessor.h"

namespace hs
{
class TopView;

/** HEAD SPIN, 760 x 480, Kick Lock's layout: on the left Kiko (small, from
    the front), the six shapes and the direction; in the middle the view from
    above (Kiko from above, the path, the sound) and under it the big MIX
    knob, with RETRIGGER, RENDER, LEVEL and the mono check; on the right the
    motion (RATE in Hz or the DIVISION in Sync and the MODE switch, START,
    DISTANCE, GROOVE, ROTATION, WIDTH, DEPTH); the presets at the bottom.
    In Kiko's colours: the cap's pink, navy, the headphones' blue, white. */
class HeadSpinEditor final : public gk::OneKnobEditor
{
public:
    explicit HeadSpinEditor (HeadSpinProcessor& p);
    ~HeadSpinEditor() override;

    static gk::Palette makePalette();

    /** Screenshots: no motion, a full trail. */
    void snapView();

protected:
    void editorTick (double dt) override;

private:
    class Panels;
    class ShapeSelector;
    class DirectionButton;
    class MonoCheck;

    HeadSpinProcessor& proc;
    std::unique_ptr<Panels> panels;
    std::unique_ptr<TopView> view;
    std::vector<std::unique_ptr<gk::SmallKnob>> knobs;
    std::unique_ptr<gk::SmallKnob> rateHz, rateSync;
    std::unique_ptr<gk::ChoiceSelector> mode, render, retrigger;
    std::unique_ptr<ShapeSelector> shapes;
    std::unique_ptr<DirectionButton> direction;
    std::unique_ptr<MonoCheck> mono;
};
} // namespace hs
