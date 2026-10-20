#pragma once

#include "SingItBackProcessor.h"

namespace sib
{
/** Sing It Back UI, "Stage" theme: orange / teal / plum on the suite's
    charcoal. Sugar and her crowd on top, the CROWD knob (SOLO → CROWD), the
    crowd's style and RÉPONSE on the left, harmony and voices on the right, the
    scale bar at the bottom (key, scale, snap, the note being sung) instead of
    a preset bar, and the advanced panel behind "⋯" (spread, humanize,
    dropouts, room, room size). */
class SingItBackEditor final : public gk::OneKnobEditor
{
public:
    explicit SingItBackEditor (SingItBackProcessor& p);
    ~SingItBackEditor() override;
    static gk::Palette makePalette();

    /** Screenshots: Sugar singing (or not), the advanced panel open. */
    void setPreview (bool singing, int syllable, bool advancedOpen);

protected:
    void editorTick (double dt) override;

private:
    class ScaleBar;
    class AdvancedPanel;
    class MoreButton;

    SingItBackProcessor& proc;
    std::unique_ptr<gk::ChoiceSelector> style, response, harmony, voices;
    std::unique_ptr<ScaleBar> scaleBar;
    std::unique_ptr<AdvancedPanel> advanced;
    std::unique_ptr<MoreButton> more;
    std::unique_ptr<gk::SmallKnob> volume;
    float pulse = 0.0f;
    int previewDegree = -3; // screenshots: the note shown (-3 = live)
};
} // namespace sib
