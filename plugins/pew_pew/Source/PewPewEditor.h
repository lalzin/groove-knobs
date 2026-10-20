#pragma once

#include "PewPewProcessor.h"

namespace pp
{
/** Pew Pew UI, "Mirror" theme: gold / cherry / indigo, the PEW knob as a drum
    skin seen from above (it lifts with the pitch), ROOM and ECHO knobs with
    the echo division, Tomi the disco drummer. No chips (DECISIONS.md). */
class PewPewEditor final : public gk::OneKnobEditor
{
public:
    explicit PewPewEditor (PewPewProcessor& p);
    ~PewPewEditor() override;
    static gk::Palette makePalette();

    /** Screenshots: shows a hit `secondsAgo` (-1 = live). */
    void setHitOverride (float secondsAgo);

protected:
    void editorTick (double dt) override;

private:
    class ModeSwitch;
    PewPewProcessor& proc;
    std::unique_ptr<ModeSwitch> mode;
};
} // namespace pp
