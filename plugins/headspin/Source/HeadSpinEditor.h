#pragma once

#include "HeadSpinProcessor.h"

namespace hs
{
class TopView;

/** HEAD SPIN, 640 x 440: the view from above in the middle (Kiko, the path,
    the sound), MIX under it; on the left WIDTH, DEPTH, ROTATION, LEVEL, the
    six shapes and the direction; on the right the speed (RATE in Hz or the
    DIVISION in Sync, the Hz / Sync switch), START, DISTANCE, GROOVE,
    RETRIGGER, RENDER and the mono check; the presets at the bottom.
    Neon pink, black and white. */
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
    class ShapeSelector;
    class DirectionButton;
    class MonoCheck;

    HeadSpinProcessor& proc;
    TopView* view = nullptr;
    std::vector<std::unique_ptr<gk::SmallKnob>> knobs;
    std::unique_ptr<gk::SmallKnob> rateHz, rateSync;
    std::unique_ptr<gk::ChoiceSelector> mode, render, retrigger;
    std::unique_ptr<ShapeSelector> shapes;
    std::unique_ptr<DirectionButton> direction;
    std::unique_ptr<MonoCheck> mono;
};
} // namespace hs
